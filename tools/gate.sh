#!/usr/bin/env bash
# The TamaPoke pre-PR gate, in one command with real exit codes and short output.
#
#   tools/gate.sh               everything
#   tools/gate.sh --no-build    skip the two firmware builds (quick runs)
#
# Steps: host tests (+ASan/UBSan), i18n format check, no-network check over the
# firmware sources, and both firmware builds (1.75C and the original 1.75).
#
# The firmware builds run from a staged copy of the working tree (uncommitted
# changes included) in a folder literally named "TamaPoke": arduino-cli refuses
# a sketch whose folder name differs from the main .ino, which is the case in
# every agent worktree. Temp dirs are removed on exit.
#
# Exit status is non-zero if any step failed; the last lines are one summary
# line per step. Works with macOS (BSD) and Linux tools.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 2

BUILD=1
for a in "$@"; do
  case "$a" in
    --no-build) BUILD=0 ;;
    -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
    *) echo "Unknown arg: $a" >&2; exit 2 ;;
  esac
done

TMP="$(mktemp -d "${TMPDIR:-/tmp}/tamapoke-gate.XXXXXX")" || exit 2
trap 'rm -rf "$TMP"' EXIT
ESC="$(printf '\033')"

SUMMARY=()
FAILED=0
record() {  # record <name> <exit code> [extra]
  local name="$1" rc="$2" extra="${3:-}"
  if [[ "$rc" -eq 0 ]]; then
    SUMMARY+=("PASS  $name${extra:+  ($extra)}")
  else
    SUMMARY+=("FAIL  $name (exit $rc)${extra:+  ($extra)}")
    FAILED=1
  fi
}
strip_ansi() { sed "s/${ESC}\\[[0-9;]*[A-Za-z]//g"; }
# print the last lines of a log when the filtered view came out empty
tail_if_empty() {  # tail_if_empty <filtered-count> <log>
  [[ "$1" -eq 0 ]] && tail -n 15 "$2" | strip_ansi
}

# ---------------------------------------------------------------- host tests
echo "== host tests (--asan) =="
./test/run_tests.sh --asan >"$TMP/tests.log" 2>&1
rc=$?
strip_ansi <"$TMP/tests.log" \
  | grep -E 'warning|error|Warning|Error|FAIL|KNOWN|XPASS|tests, |^Ran |^OK|skipped|Sanitizer|TODO OK|HAY FALLOS' \
  | tee "$TMP/tests.f"
[[ "$rc" -ne 0 ]] && tail_if_empty "$(wc -l <"$TMP/tests.f")" "$TMP/tests.log"
record "host tests (--asan)" "$rc"

# ---------------------------------------------------------------- i18n
echo "== i18n formats =="
python3 tools/test_i18n_formats.py 2>&1
rc=$?
record "i18n formats" "$rc"

# ---------------------------------------------------------------- no network
# Hard rule: no Wi-Fi/BLE/ESP-NOW/HTTP/NTP/MQTT/OTA in the firmware. Matches
# includes of network/radio libraries and calls that start a radio or a network
# service. Calls that only stop/release a radio that was never started
# (esp_wifi_stop/deinit, WiFi.mode(WIFI_OFF), btStop(), esp_bt_controller_mem_release)
# and the esp_wifi.h / esp_bt.h headers they need are deliberately not matched.
echo "== no-network check =="
NET_INC='#[[:space:]]*include[[:space:]]*[<"](WiFi|HTTPClient|HTTPUpdate|WebServer|ESPAsyncWebServer|AsyncTCP|ESPmDNS|ArduinoOTA|PubSubClient|BluetoothSerial|BLE|NimBLE|esp_now|esp_gap_ble|esp_gatt|esp_bluedroid|esp_bt_main|esp_http_client|esp_http_server|esp_https_ota|esp_sntp|esp_netif|esp_eth|lwip/|sntp)'
NET_CALL='configTime[[:space:]]*\(|sntp_|esp_now_|esp_wifi_(init|start|connect|set_mode|scan_start)|esp_bt_controller_(init|enable)|esp_(gap_ble|gatt|ble)_|esp_http_client_|esp_https_ota|esp_sntp_|esp_netif_|esp_eth_|WiFi\.(begin|softAP)'
NET_FILES=()
for f in ./*.ino ./*.cpp ./*.h; do [[ -f "$f" ]] && NET_FILES+=("$f"); done
grep -nE -e "$NET_INC" -e "$NET_CALL" "${NET_FILES[@]}" >"$TMP/net.out" 2>&1
grc=$?
if [[ "$grc" -eq 1 ]]; then
  echo "no network includes or calls in ${#NET_FILES[@]} firmware sources"
  record "no-network" 0
else
  cat "$TMP/net.out"
  record "no-network" 1 "forbidden pattern in firmware source"
fi

# ---------------------------------------------------------------- builds
sketch_warn_re='TamaPoke/[^:]+:[0-9]+:[0-9]+: warning:'
show_build() {  # show_build <label> <log> <exit code>
  local label="$1" log="$2" rc="$3" n
  strip_ansi <"$log" \
    | grep -iE "can't open|error|${sketch_warn_re}|Sketch uses|build OK" | sort -u >"$log.f"
  cat "$log.f"
  [[ "$rc" -ne 0 ]] && tail_if_empty "$(wc -l <"$log.f")" "$log"
  n="$(strip_ansi <"$log" | grep -E "$sketch_warn_re" | sort -u | wc -l | tr -d ' ')"
  echo "$label: $n sketch warning(s)"
  record "$label build" "$rc" "$n sketch warnings"
}

if [[ "$BUILD" -eq 1 ]]; then
  STAGE="$TMP/stage/TamaPoke"
  mkdir -p "$STAGE"
  rsync -a \
    --exclude '/.git' --exclude '/out' --exclude '/test/build' --exclude '/backups' \
    --exclude '/.claude/worktrees' --exclude '/tools/build' --exclude '/.venv' \
    "$ROOT/" "$STAGE/"

  echo "== build 1.75C =="
  (cd "$STAGE" && ./tools/build_175c.sh --out "$TMP/out175c") >"$TMP/b175c.log" 2>&1
  show_build "1.75C" "$TMP/b175c.log" $?

  echo "== build 1.75 (original) =="
  (cd "$STAGE" && arduino-cli compile --warnings=all \
    --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" \
    --build-path "$TMP/b175") >"$TMP/b175.log" 2>&1
  show_build "1.75" "$TMP/b175.log" $?
else
  SUMMARY+=("SKIP  firmware builds (--no-build)")
fi

# ---------------------------------------------------------------- summary
echo
echo "== gate summary =="
printf '%s\n' "${SUMMARY[@]}"
if [[ "$FAILED" -eq 0 ]]; then echo "GATE GREEN"; else echo "GATE FAILED"; fi
exit "$FAILED"

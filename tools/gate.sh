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
    -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
    *) echo "Unknown arg: $a" >&2; exit 2 ;;
  esac
done

TMP="$(mktemp -d "${TMPDIR:-/tmp}/tamapoke-gate.XXXXXX")" || exit 2
KEEP="$ROOT/test/build/gate-logs"   # failing runs keep their full logs here (gitignored)
keep_logs() {
  if [[ "${FAILED:-0}" -ne 0 ]]; then
    rm -rf "$KEEP" && mkdir -p "$KEEP" && cp "$TMP"/*.log "$KEEP"/ 2>/dev/null
    echo "full logs of this failed run: $KEEP"
  fi
  rm -rf "$TMP"
}
trap keep_logs EXIT
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
# Failing/known tests with their indented assertion lines, the "Fallos:" list,
# summaries, and every warning or error.
strip_ansi <"$TMP/tests.log" \
  | awk '/ (FAIL|KNOWN|XPASS) /{print; d=1; next}
         d && /^        /{print; next} {d=0}
         /^Fallos:|^Bugs conocidos/{print; l=1; next}
         l && /^  - /{print; next} {l=0}
         /warning|error|Warning|Error|FAIL|tests, |^Ran |^OK|skipped|Sanitizer|TODO OK|HAY FALLOS/{print}' \
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
# service; it catches the common cases, not everything, so reviewers still read
# the diff. Comments are scanned too (a commented-out include fails). Calls that
# only stop/release a radio that was never started (esp_wifi_stop/deinit via
# esp_wifi.h, btStop(), esp_bt_controller_mem_release via esp_bt.h) and those two
# headers are deliberately not matched. (WiFi.mode(WIFI_OFF) is not usable: it
# needs WiFi.h, which is matched.)
echo "== no-network check =="
NET_INC='#[[:space:]]*include[[:space:]]*[<"](WiFi|HTTPClient|HTTPUpdate|WebServer|ESPAsyncWebServer|AsyncTCP|AsyncUDP|ESPmDNS|ArduinoOTA|PubSubClient|BluetoothSerial|SimpleBLE|BLE|NimBLE|Network|ESP32_NOW|ETH\.h|DNSServer|NetBIOS|PPP\.h|Matter|RMaker|Insights|OThread|Zigbee|ESP_HostedOTA|NTPClient|AsyncMqtt|ArduinoMqtt|mqtt_client|esp_mqtt|esp_websocket|esp_now|esp_gap_ble|esp_gatt|esp_bluedroid|esp_bt_main|esp_http_client|esp_http_server|esp_https_ota|esp_sntp|esp_netif|esp_eth|lwip/|sntp|host/ble|nimble/)'
NET_CALL='configTime[[:space:]]*\(|configTzTime[[:space:]]*\(|btStart(Mode)?[[:space:]]*\(|sntp_|esp_now_|esp_wifi_(init|start|connect|set_mode|scan_start)|esp_bt_controller_(init|enable)|esp_(gap_ble|gatt|ble)_|esp_http_client_|esp_https_ota|esp_sntp_|esp_netif_|esp_eth_|WiFi\.(begin|softAP)'
# Everything arduino-cli compiles: sketch-root sources and anything under src/.
NET_FILES=()
for f in ./*.ino ./*.cpp ./*.h ./*.hpp ./*.c ./*.S; do [[ -f "$f" ]] && NET_FILES+=("$f"); done
if [[ -d src ]]; then
  while IFS= read -r f; do NET_FILES+=("$f"); done < <(find src -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o -name '*.S' -o -name '*.ino' \))
fi
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
# Known sketch warnings per build on main (exact count; update it in the PR that
# changes it). More than this fails the gate: only new warnings are findings.
WARN_BASELINE="${WARN_BASELINE:-20}"
show_build() {  # show_build <label> <log> <exit code>
  local label="$1" log="$2" rc="$3" n
  # in build order (a sorted view separates an error from its source line);
  # awk drops exact repeats only
  strip_ansi <"$log" \
    | grep -iE "can't open|error|${sketch_warn_re}|Sketch uses|build OK" | awk '!seen[$0]++' >"$log.f"
  cat "$log.f"
  if [[ "$rc" -ne 0 ]]; then
    tail_if_empty "$(wc -l <"$log.f")" "$log"
    record "$label build" "$rc" "sketch warnings n/a: build failed"
    return
  fi
  n="$(strip_ansi <"$log" | grep -E "$sketch_warn_re" | sort -u | wc -l | tr -d ' ')"
  echo "$label: $n sketch warning(s), baseline $WARN_BASELINE"
  if (( n > WARN_BASELINE )); then
    record "$label build" 1 "$n sketch warnings: +$((n - WARN_BASELINE)) NEW over baseline $WARN_BASELINE"
  else
    record "$label build" 0 "$n sketch warnings"
  fi
}

if [[ "$BUILD" -eq 1 ]]; then
  STAGE="$TMP/stage/TamaPoke"
  mkdir -p "$STAGE"
  if ! rsync -a \
    --exclude '/.git' --exclude '/out' --exclude '/test/build' --exclude '/backups' \
    --exclude '/.claude/worktrees' --exclude '/tools/build' --exclude '/.venv' \
    "$ROOT/" "$STAGE/"; then
    record "stage working tree for builds" 1 "rsync failed; builds skipped"
    BUILD=0
  fi
fi
if [[ "$BUILD" -eq 1 ]]; then
  echo "== build 1.75C =="
  (cd "$STAGE" && ./tools/build_175c.sh --out "$TMP/out175c") >"$TMP/b175c.log" 2>&1
  show_build "1.75C" "$TMP/b175c.log" $?

  echo "== build 1.75 (original) =="
  (cd "$STAGE" && arduino-cli compile --warnings=all \
    --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" \
    --build-path "$TMP/b175") >"$TMP/b175.log" 2>&1
  show_build "1.75" "$TMP/b175.log" $?
else
  SUMMARY+=("SKIP  firmware builds")
fi

# ---------------------------------------------------------------- summary
echo
echo "== gate summary =="
printf '%s\n' "${SUMMARY[@]}"
if [[ "$FAILED" -eq 0 ]]; then echo "GATE GREEN"; else echo "GATE FAILED"; fi
exit "$FAILED"

#!/usr/bin/env bash
# The TamaPoke pre-PR gate, in one command with real exit codes and short output.
#
#   tools/gate.sh               everything
#   tools/gate.sh --no-build    skip the simulator and the firmware builds (quick runs)
#   tools/gate.sh --no-firmware skip only the firmware builds (for reviewers, who take
#                               those from CI's "Compile firmware"); the simulator runs
#   tools/gate.sh --clean       firmware builds from scratch: reuse nothing from earlier runs
#   TAMAPOKE_JOBS=8 tools/gate.sh   compile jobs per firmware build: default 3, 0 = all cores
#
# Steps, fastest first so most breakage shows in seconds: host tests
# (+ASan/UBSan), i18n format check, no-network check over the firmware sources,
# the desktop simulator for both boards (tools/sim.sh: it compiles the real
# sketch on the host; when SDL2 is installed, also its interactive window,
# built and driven by synthetic events with --selftest under SDL's dummy video
# driver, with no display needed; without SDL2 that line is a SKIP), then both
# firmware builds (1.75C and the original 1.75, one after the other).
#
# The firmware builds run from a staged copy of the working tree (uncommitted
# changes included; only the parts the firmware build reads) in a folder
# literally named "TamaPoke": arduino-cli refuses a sketch whose folder name
# differs from the main .ino, which is the case in every agent worktree. The
# staged copy and both build folders persist between runs (tools/build_cache.sh
# says where), so a rebuild reuses the compiled core and libraries. Every
# sketch file is compiled again on every run, so the sketch warning count
# covers the whole sketch each time. One firmware-building gate at a time per
# checkout (a lock); --no-build and --no-firmware take none.
#
# Exit status is non-zero if any step failed; the last lines are one summary
# line per step. Works with macOS (BSD) and Linux tools.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 2

BUILD=1      # the simulator and the firmware builds
FIRMWARE=1   # the firmware builds alone
CLEAN=0
for a in "$@"; do
  case "$a" in
    --no-build) BUILD=0 ;;
    --no-firmware) FIRMWARE=0 ;;
    --clean) CLEAN=1 ;;
    -h|--help) awk 'NR > 1 && /^set -/ { exit } NR > 1' "$0"; exit 0 ;;
    *) echo "Unknown arg: $a" >&2; exit 2 ;;
  esac
done
if [[ "$BUILD" -eq 0 ]]; then FIRMWARE=0; fi
if [[ "$FIRMWARE" -eq 0 && "$CLEAN" -eq 1 ]]; then
  echo "--clean is for the firmware builds, which --no-build and --no-firmware skip; pass one or the other" >&2
  exit 2
fi

# The sketch-warning count and WARN_BASELINE, shared with CI's firmware builds.
# shellcheck source=sketch_warnings.sh
. "$ROOT/tools/sketch_warnings.sh"

# Parallel compile jobs for each firmware build (arduino-cli's own default is
# every core). Checked up front so a typo fails in a second, not after the tests.
JOBS="${TAMAPOKE_JOBS:-3}"
JOBS_RE='^(0|[1-9][0-9]{0,3})$'
if [[ "$FIRMWARE" -eq 1 && ! "$JOBS" =~ $JOBS_RE ]]; then
  echo "TAMAPOKE_JOBS must be 0 (all cores) or a whole number of jobs, up to 9999; got '$JOBS'" >&2
  exit 2
fi
# The sketch-warning baseline is checked the same way: a typo fails now, not
# after both builds.
if [[ "$FIRMWARE" -eq 1 ]]; then tp_warn_baseline_ok || exit 2; fi

# The staged copy and the build folders (see tools/build_cache.sh). Taken
# before any step runs, so a second gate in this checkout stops at once.
# shellcheck source=build_cache.sh
. "$ROOT/tools/build_cache.sh"
trap tp_unlock_all EXIT
if [[ "$FIRMWARE" -eq 1 ]]; then
  CACHE="$(tp_cache_dir "$ROOT")"
  tp_cache_prune "$CACHE" "$ROOT" || exit 2
  CACHE="$CACHE/gate"
  tp_lock "$CACHE" || exit 2
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/tamapoke-gate.XXXXXX")" || exit 2
KEEP="$ROOT/test/build/gate-logs"   # failing runs keep their full logs here (gitignored)
keep_logs() {
  if [[ "${FAILED:-0}" -ne 0 ]]; then
    rm -rf "$KEEP" && mkdir -p "$KEEP" && cp "$TMP"/*.log "$KEEP"/ 2>/dev/null
    echo "full logs of this failed run: $KEEP"
  fi
  rm -rf "$TMP"
  tp_unlock_all
}
trap keep_logs EXIT

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
# print the last lines of a log when the filtered view came out empty
tail_if_empty() {  # tail_if_empty <filtered-count> <log>
  [[ "$1" -eq 0 ]] && tail -n 15 "$2" | tp_strip_ansi
}

# ---------------------------------------------------------------- host tests
echo "== host tests (--asan) =="
./test/run_tests.sh --asan >"$TMP/tests.log" 2>&1
rc=$?
# Failing/known tests with their indented assertion lines, the "Fallos:" list,
# summaries, and every warning or error.
tp_strip_ansi <"$TMP/tests.log" \
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

# ---------------------------------------------------------------- simulator
# The desktop simulator compiles the real sketch on the host (sim/, tools/sim.sh):
# a firmware change that breaks the host build or a screen script shows up here.
# Skipped with --no-build like the firmware builds (it needs the same Arduino libraries).
if [[ "$BUILD" -eq 1 ]]; then
  echo "== desktop simulator =="
  want="$(grep -cE '^[[:space:]]*shot[[:space:]]' sim/scripts/screens.txt)"
  for b in 175c 175; do
    ./tools/sim.sh --board "$b" --lang en --script sim/scripts/screens.txt --out "$TMP/sim-$b" --quiet \
      >"$TMP/sim-$b.log" 2>&1
    rc=$?
    got="$(find "$TMP/sim-$b" -name '*.png' 2>/dev/null | wc -l | tr -d ' ')"
    grep -iE 'error|fail|warning:' "$TMP/sim-$b.log" | head -n 20
    w="$(grep -c 'warning:' "$TMP/sim-$b.log")"
    [[ "$rc" -ne 0 ]] && tail -n 15 "$TMP/sim-$b.log"
    if [[ "$rc" -eq 0 && "$got" -ne "$want" ]]; then rc=1; fi
    record "simulator $b" "$rc" "$got/$want screens, $w host build warning(s)"
  done
  # The interactive window (sim/window.cpp) is opt-in and needs SDL2 (Homebrew), which a
  # machine may not have: no working sdl2-config, no window lines. Its --selftest pushes synthetic
  # mouse and key events through the window's real event handler under SDL's dummy video
  # driver and checks the fake board saw them.
  if sdl2-config --version >/dev/null 2>&1; then
    for b in 175c 175; do
      SDL_VIDEODRIVER=dummy ./tools/sim.sh --window --board "$b" --selftest --quiet --out "$TMP/win-$b" \
        >"$TMP/win-$b.log" 2>&1
      rc=$?
      grep -iE 'error|fail|warning:' "$TMP/win-$b.log" | head -n 20
      w="$(grep -c 'warning:' "$TMP/win-$b.log")"
      [[ "$rc" -ne 0 ]] && tail -n 15 "$TMP/win-$b.log"
      if [[ "$rc" -eq 0 ]] && ! grep -q '^window selftest ok' "$TMP/win-$b.log"; then rc=1; fi
      if [[ "$rc" -eq 0 ]]; then
        record "window $b" "$rc" "selftest ok, $w host build warning(s)"
      else
        record "window $b" "$rc" "selftest or build failed, $w host build warning(s)"
      fi
    done
  else
    SUMMARY+=("SKIP  window (no SDL2)")
  fi
else
  SUMMARY+=("SKIP  simulator")
fi

# ---------------------------------------------------------------- builds
# Sketch warnings are counted by tools/sketch_warnings.sh (WARN_BASELINE lives
# there), the same code CI's firmware builds run.
show_build() {  # show_build <label> <log> <exit code>
  local label="$1" log="$2" rc="$3" wrc
  # in build order (a sorted view separates an error from its source line);
  # awk drops exact repeats only
  tp_strip_ansi <"$log" \
    | grep -iE "can't open|error|${TP_SKETCH_WARN_RE}|Sketch uses|build OK" | awk '!seen[$0]++' >"$log.f"
  cat "$log.f"
  if [[ "$rc" -ne 0 ]]; then
    tail_if_empty "$(wc -l <"$log.f")" "$log"
    record "$label build" "$rc" "sketch warnings n/a: build failed"
    return
  fi
  tp_sketch_warnings "$log" >"$log.w"
  wrc=$?
  echo "$label: $(cat "$log.w")"
  case "$wrc" in
    0) record "$label build" 0 "$TP_WARN_COUNT sketch warnings" ;;
    1) record "$label build" 1 "$TP_WARN_COUNT sketch warnings: +$((TP_WARN_COUNT - TP_WARN_BASELINE)) NEW over baseline $TP_WARN_BASELINE" ;;
    *) record "$label build" "$wrc" "sketch warnings could not be counted" ;;
  esac
}

if [[ "$FIRMWARE" -eq 1 ]]; then
  # Mirror the working tree into the same folder every run: rsync keeps the
  # mtimes, and --delete/--delete-excluded drop whatever is no longer there.
  # Folders the firmware build never reads stay out. arduino-cli rebuilds
  # everything when the set of source-like files anywhere under the sketch
  # changes (its "additionalFiles" build option), so without this a new test,
  # doc or simulator file would cost a full rebuild. A folder missing from this
  # list is only staged for nothing; tools/ is reduced to the build scripts.
  # --checksum: rsync's size-and-mtime quick check misses a same-size edit made
  # in the same second as the previous sync (mtimes are whole seconds on some
  # rsyncs), and the stage now persists between runs, so it would never arrive.
  STAGE="$CACHE/stage/TamaPoke"
  if [[ "$CLEAN" -eq 1 ]]; then rm -rf "$CACHE/stage"; fi
  mkdir -p "$STAGE"
  if ! rsync -a --checksum --delete --delete-excluded \
    --include '/tools/' --include '/tools/build_175c.sh' --include '/tools/build_cache.sh' \
    --exclude '/tools/*' \
    --exclude '/.git' --exclude '/.github' --exclude '/.claude' --exclude '/.venv' \
    --exclude '/test' --exclude '/sim' --exclude '/docs' --exclude '/web' \
    --exclude '/out' --exclude '/backups' --exclude '/shots' \
    "$ROOT/" "$STAGE/"; then
    record "stage working tree for builds" 1 "rsync failed; builds skipped"
    FIRMWARE=0
  fi
fi
if [[ "$FIRMWARE" -eq 1 ]]; then
  CLEAN_ARG=()
  how="--jobs $JOBS"
  if [[ "$CLEAN" -eq 1 ]]; then CLEAN_ARG=(--clean); how="$how, --clean"; fi
  echo "== build 1.75C ($how) =="
  (cd "$STAGE" && TAMAPOKE_JOBS="$JOBS" ./tools/build_175c.sh ${CLEAN_ARG[@]+"${CLEAN_ARG[@]}"} \
    --build-path "$CACHE/build-175c" --out "$TMP/out175c") >"$TMP/b175c.log" 2>&1
  show_build "1.75C" "$TMP/b175c.log" $?

  echo "== build 1.75 (original, $how) =="
  rc=0
  tp_prepare_build "$CACHE/build-175" "$STAGE" "$CLEAN" >"$TMP/b175.log" 2>&1 || rc=$?
  if [[ "$rc" -eq 0 ]]; then
    arduino-cli compile --warnings=all --jobs "$JOBS" ${CLEAN_ARG[@]+"${CLEAN_ARG[@]}"} \
      --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" \
      --build-path "$CACHE/build-175" "$STAGE" >>"$TMP/b175.log" 2>&1 || rc=$?
  fi
  show_build "1.75" "$TMP/b175.log" "$rc"
else
  SUMMARY+=("SKIP  firmware builds")
fi

# ---------------------------------------------------------------- summary
echo
echo "== gate summary =="
printf '%s\n' "${SUMMARY[@]}"
if [[ "$FAILED" -eq 0 ]]; then echo "GATE GREEN"; else echo "GATE FAILED"; fi
exit "$FAILED"

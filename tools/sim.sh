#!/usr/bin/env bash
# TamaPoke desktop simulator: builds sim/ (only what changed) and runs a scripted
# session on the Mac, writing PNG screenshots. No board, no flashing, no installs:
# it uses the Arduino libraries arduino-cli already has (GFX Library for Arduino, U8g2).
#
#   tools/sim.sh --lang ja --script care-page.txt --out shots/
#   tools/sim.sh --board 175 --script my.txt     # the original 1.75 instead of the 1.75C
#   tools/sim.sh --help                          # all simulator options
#
# --script takes a path, or the name of a script in sim/scripts/ (with or without .txt).
# Script steps are listed in sim/script.h and the README ("Simulator").
# ARDUINO_LIBS overrides where the Arduino libraries are found.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BOARD=175c
ARGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --board)
      [[ $# -ge 2 ]] || { echo "sim.sh: --board needs 175c or 175" >&2; exit 2; }
      BOARD="$2"; shift 2 ;;
    --script)
      [[ $# -ge 2 ]] || { echo "sim.sh: --script needs a file" >&2; exit 2; }
      # a path as given, else a script from sim/scripts/ (with or without .txt)
      script="$2"
      if [[ ! -e "$script" ]]; then
        for cand in "$ROOT/sim/scripts/$script" "$ROOT/sim/scripts/$script.txt"; do
          [[ -f "$cand" ]] && { script="$cand"; break; }
        done
      fi
      ARGS+=(--script "$script"); shift 2 ;;
    *) ARGS+=("$1"); shift ;;
  esac
done
case "$BOARD" in 175c|175) ;; *) echo "sim.sh: --board must be 175c or 175" >&2; exit 2 ;; esac

if [[ -z "${ARDUINO_LIBS:-}" ]]; then
  user_dir="$(arduino-cli config get directories.user 2>/dev/null || true)"
  if [[ -z "$user_dir" ]]; then
    if [[ "$(uname)" == Darwin ]]; then user_dir="$HOME/Documents/Arduino"; else user_dir="$HOME/Arduino"; fi
  fi
  ARDUINO_LIBS="$user_dir/libraries"
fi

LOG="$(mktemp "${TMPDIR:-/tmp}/tamapoke-sim-build.XXXXXX")"
trap 'rm -f "$LOG"' EXIT
if ! make -C "$ROOT/sim" -s -j3 BOARD="$BOARD" ARDUINO_LIBS="$ARDUINO_LIBS" >"$LOG" 2>&1; then
  cat "$LOG" >&2
  echo "sim.sh: simulator build failed" >&2
  exit 1
fi
# the firmware's own compiler warnings, if it was rebuilt (same ones the board build shows)
grep -E 'warning:' "$LOG" >&2 || true

# sprites from the repo unless the caller passes --sprites (the last one wins)
exec "$ROOT/sim/build/$BOARD/tamapoke_sim" --sprites "$ROOT/tools/sdcard" ${ARGS[@]+"${ARGS[@]}"}

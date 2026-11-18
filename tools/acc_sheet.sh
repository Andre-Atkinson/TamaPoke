#!/usr/bin/env bash
# Contact sheets of an accessory on all 151 species (#116), for checking by eye where it sits
# on every pose the main screen shows: sim/accsheet.cpp, built with the simulator's firmware
# objects, so the sheets show what the board draws (drawAccOn, accfix.h's nudges included).
# No board, no Pillow, no installs.
#
#   tools/acc_sheet.sh                         # the Cap, every pose, into acc-sheets/
#   tools/acc_sheet.sh --acc crown --pose stand --pose sad --out /tmp/sheets
#   tools/acc_sheet.sh --scale 2               # smaller pixels (default 3)
#
# Options go to the program as they are (sim/accsheet.cpp says what each does). The 1.75C build
# of the simulator draws them; the art and the heads are the same on both boards.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BOARD=175c

if [[ -z "${ARDUINO_LIBS:-}" ]]; then
  user_dir="$(arduino-cli config get directories.user 2>/dev/null || true)"
  if [[ -z "$user_dir" ]]; then
    if [[ "$(uname)" == Darwin ]]; then user_dir="$HOME/Documents/Arduino"; else user_dir="$HOME/Arduino"; fi
  fi
  ARDUINO_LIBS="$user_dir/libraries"
fi

LOG="$(mktemp "${TMPDIR:-/tmp}/tamapoke-accsheet-build.XXXXXX")"
trap 'rm -f "$LOG"' EXIT
if ! make -C "$ROOT/sim" -s -j3 BOARD="$BOARD" ARDUINO_LIBS="$ARDUINO_LIBS" accsheet >"$LOG" 2>&1; then
  cat "$LOG" >&2
  echo "acc_sheet.sh: build failed" >&2
  exit 1
fi
grep -E 'warning:' "$LOG" >&2 || true

# sprites from the repo unless the caller passes --sprites (the last one wins)
exec "$ROOT/sim/build/$BOARD/tamapoke_accsheet" --sprites "$ROOT/tools/sdcard" "$@"

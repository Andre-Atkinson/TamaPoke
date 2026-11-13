#!/usr/bin/env bash
# Update the firmware on a 1.75C without losing the pet, in one go:
#   1. back up the pet's save           (tools/pet_save.sh backup)
#   2. build the 1.75C firmware         (tools/build_175c.sh; skip with --no-build)
#   3. flash the app partition only     (tools/flash_175c.sh --app-only)
#   4. set the board clock from this computer once it is back up
#
# Usage:
#   tools/update_175c.sh [--port PORT] [--out DIR] [--no-build]
set -euo pipefail

source "$(dirname "$0")/board_env.sh"
OUT="${TAMAPOKE_OUT:-${ROOT}/../out}"
PORT=""
BUILD=1
while [[ $# -gt 0 ]]; do
  case "$1" in
    --port) PORT="$2"; shift 2 ;;
    --out)  OUT="$2"; shift 2 ;;
    --no-build) BUILD=0; shift ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; exit 1 ;;
  esac
done
PORT="$(find_port "$PORT")"
LOG="$(mktemp "${TMPDIR:-/tmp}/tamapoke-update.XXXXXX")"
trap 'rm -f "$LOG"' EXIT

# Run a step quietly; on failure show its log and stop. $1 = grep pattern for
# the lines worth showing on success, the rest is the command.
step() {
  local show="$1"; shift
  if ! "$@" > "$LOG" 2>&1; then
    tr '\r' '\n' < "$LOG" | grep -vE 'Writing at|Reading from' | tail -25 >&2
    echo "Stopped: $* failed. The pet is untouched by this step." >&2
    exit 1
  fi
  tr '\r' '\n' < "$LOG" | grep -E "$show" || true
}

echo "== 1/4 backing up the pet"
"${ROOT}/tools/pet_save.sh" backup --port "$PORT"

if [[ "$BUILD" -eq 1 ]]; then
  echo "== 2/4 building"
  step 'Sketch uses|build OK' "${ROOT}/tools/build_175c.sh" --out "$OUT"
else
  echo "== 2/4 skipping the build (--no-build)"
fi

echo "== 3/4 flashing the app (pet, settings and sprites stay)"
step 'Wrote|verified' "${ROOT}/tools/flash_175c.sh" --port "$PORT" --out "$OUT" --app-only

echo "== 4/4 setting the clock"
for _ in 1 2 3 4 5 6 7 8 9 10; do
  sleep 1
  if [[ -e "$PORT" ]] && python3 "${ROOT}/tools/board.py" --port "$PORT" clock 2>/dev/null; then
    echo "Update done."
    exit 0
  fi
done
echo "The board did not answer on $PORT; set the clock later with: python3 tools/board.py clock" >&2
exit 1

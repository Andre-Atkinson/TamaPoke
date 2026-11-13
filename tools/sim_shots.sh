#!/usr/bin/env bash
# Screenshots of every main screen in all 8 languages, with the desktop simulator.
#
#   tools/sim_shots.sh                 -> shots/<lang>/*.png (git-ignored)
#   tools/sim_shots.sh --out DIR       -> DIR/<lang>/*.png
#   tools/sim_shots.sh --board 175     -> the original 1.75 instead of the 1.75C
#
# The screens are scripted in sim/scripts/screens.txt.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/shots"
BOARD=175c
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) [[ $# -ge 2 ]] || { echo "sim_shots.sh: --out needs a folder" >&2; exit 2; }; OUT="$2"; shift 2 ;;
    --board) [[ $# -ge 2 ]] || { echo "sim_shots.sh: --board needs 175c or 175" >&2; exit 2; }; BOARD="$2"; shift 2 ;;
    -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
    *) echo "sim_shots.sh: unknown option $1" >&2; exit 2 ;;
  esac
done

SCRIPT="$ROOT/sim/scripts/screens.txt"
start=$SECONDS
for lang in es en fr de it pt ja ko; do
  "$ROOT/tools/sim.sh" --board "$BOARD" --lang "$lang" --script "$SCRIPT" --out "$OUT/$lang" --quiet
done
per="$(grep -cE '^[[:space:]]*shot[[:space:]]' "$SCRIPT")"
echo "sim_shots: 8 languages x $per screens in $OUT ($((SECONDS - start)) s)"

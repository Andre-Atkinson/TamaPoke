#!/usr/bin/env bash
# Back up or restore the pet's save: the whole NVS partition (pet, Pokédex,
# streak, medals, settings), 20 KB at 0x9000 on both the 1.75C and the
# original 1.75. The sprites and the firmware are not touched.
#
# Usage:
#   tools/pet_save.sh backup  [--port PORT] [--file FILE]
#   tools/pet_save.sh restore  --file FILE [--port PORT]
#
# backup:  asks the running firmware to write pending changes (SAVE), then
#          reads the partition to FILE (default: backups/pet-<date>-<time>.nvs.bin).
# restore: first backs up what is on the board now (backups/pre-restore-...),
#          then writes FILE back and reboots. The pet comes back as it was in
#          FILE; anything since then is lost (it's in the pre-restore file).
#
# Reads use the ROM loader (--no-stub): esptool's stub drops off USB-Serial/JTAG
# when it sends some flash blocks back, and 20 KB is quick either way.
set -euo pipefail

source "$(dirname "$0")/board_env.sh"
BACKUP_DIR="${TAMAPOKE_BACKUP_DIR:-${ROOT}/backups}"

ACTION="${1:-}"; shift || true
PORT=""; FILE=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --port) PORT="$2"; shift 2 ;;
    --file) FILE="$2"; shift 2 ;;
    -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; exit 1 ;;
  esac
done
case "$ACTION" in
  backup|restore) ;;
  -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
  *) echo "Usage: tools/pet_save.sh backup|restore [--port PORT] [--file FILE]" >&2; exit 1 ;;
esac

PORT="$(find_port "$PORT")"
ESPTOOL="$(find_esptool)"
NVS_OFF="$(part_off nvs)"
NVS_SIZE="$(part_size nvs)"
NVS_BYTES=$((NVS_SIZE))

# Read the NVS partition into $1 and check its size. $2 is esptool's --after
# (hard-reset restarts the firmware; no-reset stays in the loader for a write).
read_nvs() {
  local dest="$1" after="$2"
  mkdir -p "$(dirname "$dest")"
  "$ESPTOOL" --chip esp32s3 --port "$PORT" --no-stub --after "$after" \
    read-flash "$NVS_OFF" "$NVS_SIZE" "$dest" >/dev/null
  local got
  got=$(wc -c < "$dest" | tr -d ' ')
  if [[ "$got" -ne "$NVS_BYTES" ]]; then
    echo "Read $got bytes, expected $NVS_BYTES; not a usable backup: $dest" >&2
    return 1
  fi
  ( cd "$(dirname "$dest")" && shasum -a 256 "$(basename "$dest")" > "$(basename "$dest").sha256" )
}

stamp() { date +%Y%m%d-%H%M%S; }

if [[ "$ACTION" == backup ]]; then
  FILE="${FILE:-${BACKUP_DIR}/pet-$(stamp).nvs.bin}"
  # Best effort: an older firmware has no SAVE command, and a board that is
  # already in download mode doesn't answer. The read works either way.
  python3 "${ROOT}/tools/board.py" --port "$PORT" save 2>/dev/null \
    || echo "note: the board did not answer SAVE; backing up what is in flash" >&2
  read_nvs "$FILE" hard-reset
  echo "Backed up the pet's save to $FILE"
  exit 0
fi

# restore
[[ -n "$FILE" ]] || { echo "restore needs --file (a backup made by this script)" >&2; exit 1; }
[[ -f "$FILE" ]] || { echo "No such file: $FILE" >&2; exit 1; }
size=$(wc -c < "$FILE" | tr -d ' ')
if [[ "$size" -ne "$NVS_BYTES" ]]; then
  echo "$FILE is $size bytes; a pet save is exactly $NVS_BYTES. Refusing to write it." >&2
  exit 1
fi
if [[ -f "${FILE}.sha256" ]]; then
  ( cd "$(dirname "$FILE")" && shasum -a 256 -c "$(basename "$FILE").sha256" >/dev/null ) \
    || { echo "$FILE does not match its .sha256; refusing to write it." >&2; exit 1; }
fi

python3 "${ROOT}/tools/board.py" --port "$PORT" save >/dev/null 2>&1 || true
safety="${BACKUP_DIR}/pre-restore-$(stamp).nvs.bin"
read_nvs "$safety" no-reset
echo "Saved the current pet to $safety first."

"$ESPTOOL" --chip esp32s3 --port "$PORT" --after hard-reset write-flash "$NVS_OFF" "$FILE" >/dev/null
echo "Restored $FILE. The board has restarted with that pet."

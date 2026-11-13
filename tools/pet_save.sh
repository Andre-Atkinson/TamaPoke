#!/usr/bin/env bash
# Back up or restore the pet's save: the whole NVS partition (pet, Pokédex,
# streak, medals, settings), 20 KB at 0x9000 on both the 1.75C and the
# original 1.75. The sprites and the firmware are not touched.
#
# Usage:
#   tools/pet_save.sh backup  [--port PORT] [--file FILE]
#   tools/pet_save.sh restore  --file FILE [--port PORT] [--force]
#   (the action comes first, then the options)
#
# backup:  asks the running firmware to write pending changes (SAVE), then
#          reads the partition to FILE (default: backups/pet-<date>-<time>.nvs.bin).
# restore: checks FILE (exactly the partition size, NVS page headers, and the
#          .sha256 next to it; --force only waives a missing .sha256), then
#          backs up what is on the board now (backups/pre-restore-...), writes
#          FILE back and reboots. The pet is FILE's pet, but on boot the
#          firmware treats the time since the backup as time spent switched off
#          (up to 14 days: stats fall towards their floors, the pet ages, may
#          poop), and a missed day can reset the care streak. Anything since
#          the backup is only in the pre-restore file.
#
# Reads use the ROM loader (--no-stub): esptool's stub drops off USB-Serial/JTAG
# when it sends some flash blocks back, and 20 KB is quick either way.
set -euo pipefail

source "$(dirname "$0")/board_env.sh"
BACKUP_DIR="${TAMAPOKE_BACKUP_DIR:-${ROOT}/backups}"

USAGE="Usage: tools/pet_save.sh backup|restore [--port PORT] [--file FILE] [--force]  (the action comes first)"
ACTION="${1:-}"; shift || true
PORT=""; FILE=""; FORCE=0
case "$ACTION" in
  backup|restore) ;;
  -h|--help) sed -n '2,23p' "$0"; exit 0 ;;
  *) echo "$USAGE" >&2; exit 1 ;;
esac
while [[ $# -gt 0 ]]; do
  case "$1" in
    --port) PORT="$2"; shift 2 ;;
    --file) FILE="$2"; shift 2 ;;
    --force) FORCE=1; shift ;;
    -h|--help) sed -n '2,23p' "$0"; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; echo "$USAGE" >&2; exit 1 ;;
  esac
done

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
# Each 4 KB NVS page starts with its state word: erased, active, full or freeing.
for ((off = 0; off < NVS_BYTES; off += 4096)); do
  word="$(od -An -tx1 -v -N4 -j "$off" "$FILE" | tr -d ' \n')"
  case "$word" in
    ffffffff|feffffff|fcffffff|f8ffffff) ;;
    *) echo "$FILE is not an NVS partition (page at offset $off starts with $word). Refusing to write it." >&2
       exit 1 ;;
  esac
done
# Compare the hash of FILE itself with the first field of its .sha256 (not the
# filename recorded inside it, which breaks if the pair was renamed or copied).
if [[ -f "${FILE}.sha256" ]]; then
  want="$(awk '{print $1; exit}' "${FILE}.sha256")"
  have="$(shasum -a 256 "$FILE" | cut -d' ' -f1)"
  [[ -n "$want" && "$want" == "$have" ]] \
    || { echo "$FILE does not match its .sha256; refusing to write it." >&2; exit 1; }
elif [[ "$FORCE" -eq 1 ]]; then
  echo "warning: no ${FILE}.sha256; writing it anyway (--force)" >&2
else
  echo "No ${FILE}.sha256 to check it against (backups made by this script have one). Refusing; pass --force to write it anyway." >&2
  exit 1
fi

python3 "${ROOT}/tools/board.py" --port "$PORT" save >/dev/null 2>&1 || true
safety="${BACKUP_DIR}/pre-restore-$(stamp).nvs.bin"
read_nvs "$safety" no-reset
echo "Saved the current pet to $safety first."

"$ESPTOOL" --chip esp32s3 --port "$PORT" --after hard-reset write-flash "$NVS_OFF" "$FILE" >/dev/null
echo "Restored $FILE. The board has restarted with that pet."

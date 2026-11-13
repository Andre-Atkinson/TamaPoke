#!/usr/bin/env bash
# Flash TamaPoke 1.75C firmware and/or the LittleFS sprite image.
#
# Build first with tools/build_175c.sh. The partition table is generated here
# from boards/175c/partitions.csv (never from whatever the last build left), and
# the app/LittleFS offsets are read from that same file.
#
# Usage:
#   tools/flash_175c.sh [--port PORT]              # bootloader + partitions + otadata + app + FS
#   tools/flash_175c.sh [--port PORT] --app-only   # app0 only: keeps the pet, settings and sprites
#   tools/flash_175c.sh [--port PORT] --fw-only    # everything except the sprite image
#   tools/flash_175c.sh [--port PORT] --fs-only    # sprite image only
# --port defaults to the only /dev/cu.usbmodem* or /dev/ttyACM* present.
set -euo pipefail

source "$(dirname "$0")/board_env.sh"
OUT="${TAMAPOKE_OUT:-${ROOT}/../out}"
PORT=""
MODE=all
BAUD="${BAUD:-921600}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --port) PORT="$2"; shift 2 ;;
    --out)  OUT="$2"; shift 2 ;;
    --fs-only)  MODE=fs; shift ;;
    --fw-only)  MODE=fw; shift ;;
    --app-only) MODE=app; shift ;;
    --baud) BAUD="$2"; shift 2 ;;
    -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; exit 1 ;;
  esac
done

PORT="$(find_port "$PORT")"
ESPTOOL="$(find_esptool)"

BOOTLOADER_OFF=0x0
PARTITIONS_OFF=0x8000
BOOT_APP0_OFF="$(part_off otadata)"
APP_OFF="$(part_off app0)"
FS_OFF="$(part_off storage)"

args=( --chip esp32s3 --port "$PORT" --baud "$BAUD" write_flash -z --flash_mode dio --flash_freq 80m --flash_size 32MB )

app="${OUT}/TamaPoke.ino.bin"
if [[ "$MODE" != fs ]]; then
  [[ -f "$app" ]] || { echo "Missing $app — run tools/build_175c.sh first (see PORTING.md)" >&2; exit 1; }
fi

if [[ "$MODE" == all || "$MODE" == fw ]]; then
  boot="${OUT}/TamaPoke.ino.bootloader.bin"
  [[ -f "$boot" ]] || { echo "Missing $boot — run tools/build_175c.sh first (see PORTING.md)" >&2; exit 1; }
  # The core's partition tool and its blank otadata image (boot_app0.bin),
  # which points the bootloader at app0 as an Arduino IDE upload does.
  CORE_TOOLS="$(ls -d "${HOME}"/.arduino15/packages/esp32/hardware/esp32/*/tools \
                      "${HOME}"/Library/Arduino15/packages/esp32/hardware/esp32/*/tools 2>/dev/null | sort -V | tail -n1 || true)"
  GEN="${CORE_TOOLS}/gen_esp32part.py"
  [[ -n "$CORE_TOOLS" && -f "$GEN" ]] || { echo "gen_esp32part.py not found (install the esp32 core)" >&2; exit 1; }
  parts="$(mktemp "${TMPDIR:-/tmp}/tamapoke-175c-parts.XXXXXX")"
  trap 'rm -f "$parts"' EXIT
  python3 "$GEN" -q --flash-size 32MB "$PARTS_CSV" "$parts"
  args+=( "$BOOTLOADER_OFF" "$boot" "$PARTITIONS_OFF" "$parts" )
  bootapp="${OUT}/TamaPoke.ino.boot_app0.bin"
  [[ -f "$bootapp" ]] || bootapp="${CORE_TOOLS}/partitions/boot_app0.bin"
  if [[ -f "$bootapp" ]]; then
    args+=( "$BOOT_APP0_OFF" "$bootapp" )
  else
    echo "warning: boot_app0.bin not found; otadata left as it is" >&2
  fi
fi

if [[ "$MODE" != fs ]]; then
  args+=( "$APP_OFF" "$app" )
fi

if [[ "$MODE" == all || "$MODE" == fs ]]; then
  fs="${OUT}/storage.littlefs.bin"
  if [[ ! -f "$fs" ]]; then
    fs="${ROOT}/tools/build/storage.littlefs.bin"
  fi
  if [[ ! -f "$fs" ]]; then
    echo "LittleFS image not found. Run tools/build_spritefs.sh first." >&2
    exit 1
  fi
  args+=( "$FS_OFF" "$fs" )
fi

echo "+ $ESPTOOL ${args[*]}"
"$ESPTOOL" "${args[@]}"
echo "Done. Reset the board if it does not restart by itself."

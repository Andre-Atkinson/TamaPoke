# Shared helpers for the 1.75C board scripts. Source it, don't run it:
#   source "$(dirname "$0")/board_env.sh"
# Provides ROOT, PARTS_CSV, find_esptool, part_off, part_size, find_port.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PARTS_CSV="${ROOT}/boards/175c/partitions.csv"

# esptool: $ESPTOOL if set, else the one bundled with the Arduino ESP32 core
# (~/.arduino15 on Linux, ~/Library/Arduino15 on macOS), else a venv or PATH.
find_esptool() {
  if [[ -n "${ESPTOOL:-}" ]]; then echo "$ESPTOOL"; return 0; fi
  local c
  for c in \
    "${HOME}/.arduino15/packages/esp32/tools/esptool_py/"*/esptool \
    "${HOME}/.arduino15/packages/esp32/tools/esptool_py/"*/esptool.py \
    "${HOME}/Library/Arduino15/packages/esp32/tools/esptool_py/"*/esptool \
    "${HOME}/Library/Arduino15/packages/esp32/tools/esptool_py/"*/esptool.py \
    "${ROOT}/.venv/bin/esptool.py" \
    "$(command -v esptool.py || true)" \
    "$(command -v esptool || true)"; do
    if [[ -n "$c" && -e "$c" ]]; then echo "$c"; return 0; fi
  done
  echo "esptool not found (install the esp32 Arduino core, or set ESPTOOL=)" >&2
  return 1
}

# Column of a partition row in boards/175c/partitions.csv, by name.
_part_col() {
  [[ -f "$PARTS_CSV" ]] || { echo "Missing $PARTS_CSV" >&2; return 1; }
  local v
  v="$(awk -F, -v n="$1" -v c="$2" '!/^[ \t]*#/ { gsub(/[ \t\r]/, ""); if ($1 == n) print $c }' "$PARTS_CSV")"
  [[ -n "$v" ]] || { echo "No partition '$1' in $PARTS_CSV" >&2; return 1; }
  echo "$v"
}
part_off()  { _part_col "$1" 4; }
part_size() { _part_col "$1" 5; }

# The board's USB serial port: $1 if given, else the only /dev/cu.usbmodem*
# (macOS) or /dev/ttyACM* (Linux). Refuses to guess between several.
find_port() {
  if [[ -n "${1:-}" ]]; then echo "$1"; return 0; fi
  local ports=() p
  for p in /dev/cu.usbmodem* /dev/ttyACM*; do [[ -e "$p" ]] && ports+=("$p"); done
  if [[ ${#ports[@]} -eq 1 ]]; then echo "${ports[0]}"; return 0; fi
  if [[ ${#ports[@]} -eq 0 ]]; then
    echo "No board found. Plug it in with a USB-C data cable, or pass --port." >&2
  else
    echo "Several serial ports (${ports[*]}); pass --port." >&2
  fi
  return 1
}

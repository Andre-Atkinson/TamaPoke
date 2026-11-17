#!/usr/bin/env bash
# Build TamaPoke for the Waveshare ESP32-S3-Touch-AMOLED-1.75C (experimental).
#
# The 1.75C needs two things the default build does not have:
#   - the compile flag -DBOARD_AMOLED_175C=1 (board.h defaults to the original 1.75)
#   - the 32 MB partition table in boards/175c/partitions.csv (3 MB app + LittleFS
#     for the sprites). It is passed explicitly here; it is NOT in the sketch root,
#     because a root partitions.csv would override the scheme of every build.
#
# Usage:
#   tools/build_175c.sh                      # -> ../out (or $TAMAPOKE_OUT)
#   tools/build_175c.sh --out DIR
#   tools/build_175c.sh --flags '-DSOME_FLAG=1'   # extra compiler flags
#   tools/build_175c.sh --clean              # full build: reuse nothing from earlier builds
#   TAMAPOKE_JOBS=8 tools/build_175c.sh      # parallel compile jobs: default 3, 0 = all cores
#   tools/build_175c.sh --build-path DIR     # for tools/gate.sh: build folder DIR, no staging (see below)
# It builds a staged copy of this checkout's working tree, uncommitted changes
# included, in a folder named TamaPoke (arduino-cli refuses a sketch whose
# folder name differs from its .ino, as in every agent worktree): the same
# narrowed copy tools/gate.sh builds, made by the same code (tools/build_cache.sh,
# tp_stage), so adding or removing a file in test/, sim/, docs/, web/ or an
# agent worktree does not make the next run a full build. The staged copy and
# the build folder persist between runs (tools/build_cache.sh says where), so a
# rebuild reuses the compiled core and libraries. Every sketch file is compiled
# again on every run. One run at a time per checkout. Each run also deletes the
# cache folders of removed checkouts and of idle worktrees, and says so.
# With --build-path the caller has staged already (the gate runs the staged
# copy of this script): it builds the folder it is in, and prunes nothing.
# CI runs this same script (with --clean), so a green CI means this command works.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${TAMAPOKE_OUT:-${ROOT}/../out}"
EXTRA=""
BUILD=""
CLEAN=0
FQBN="${FQBN_175C:-esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=32M,PSRAM=opi,PartitionScheme=custom}"
PARTS="${ROOT}/boards/175c/partitions.csv"
# arduino-cli's own default is every core; a few builds at once made a 10-core
# Mac hot and slow, so cap it. A plain decimal only: arduino-cli would read a
# leading 0 as octal.
JOBS="${TAMAPOKE_JOBS:-3}"
JOBS_RE='^(0|[1-9][0-9]{0,3})$'

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out)   OUT="$2"; shift 2 ;;
    --flags) EXTRA="$2"; shift 2 ;;
    --build-path) BUILD="$2"; shift 2 ;;
    --clean) CLEAN=1; shift ;;
    -h|--help) awk 'NR > 1 && /^set -/ { exit } NR > 1' "$0"; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; exit 1 ;;
  esac
done

[[ "$JOBS" =~ $JOBS_RE ]] || { echo "TAMAPOKE_JOBS must be 0 (all cores) or a whole number of jobs, up to 9999; got '$JOBS'" >&2; exit 1; }

[[ -f "$PARTS" ]] || { echo "Missing $PARTS" >&2; exit 1; }
if [[ -f "${ROOT}/partitions.csv" ]]; then
  echo "A partitions.csv in the sketch root would override every build; remove it." >&2
  exit 1
fi
# Size limit for the image = app0 in the partition table (0x300000). Without it
# the core checks against 16 MB (boards.txt's limit for PartitionScheme=custom)
# and an oversize image would spill into the sprite partition at 0x310000.
# APP_MAX_OVERRIDE is only for testing the guard; it can lower the limit, never
# raise it above app0.
APP0_HEX="$(awk -F, '$1 ~ /^app0/ { gsub(/[ \t\r]/, "", $5); print $5 }' "$PARTS")"
APP0_SIZE="$(printf '%d' "$APP0_HEX" 2>/dev/null || true)"
[[ "$APP0_SIZE" =~ ^[0-9]+$ && "$APP0_SIZE" -gt 0 ]] || { echo "Could not read app0 size from $PARTS" >&2; exit 1; }
APP_MAX="${APP_MAX_OVERRIDE:-$APP0_SIZE}"
[[ "$APP_MAX" =~ ^[0-9]+$ && "$APP_MAX" -gt 0 && "$APP_MAX" -le "$APP0_SIZE" ]] || APP_MAX="$APP0_SIZE"

# shellcheck source=build_cache.sh
. "${ROOT}/tools/build_cache.sh"
trap tp_unlock_all EXIT
SKETCH="$ROOT"
STAGE=""
if [[ -z "$BUILD" ]]; then
  CACHE="$(tp_cache_dir "$ROOT")"
  tp_cache_prune "$CACHE" "$ROOT" || exit 1
  BUILD="$CACHE/build-175c"
  STAGE="$CACHE/stage"
fi
tp_lock "$BUILD" || exit 1   # on its own, this lock covers the staged copy too
if [[ -n "$STAGE" ]]; then
  tp_stage "$ROOT" "$STAGE" "$CLEAN" || { echo "Could not stage $ROOT into $STAGE" >&2; exit 1; }
  SKETCH="$(tp_stage_sketch "$STAGE")"
fi
tp_prepare_build "$BUILD" "$SKETCH" "$CLEAN"
mkdir -p "$OUT"
# The checks below must read what this run produced, never a leftover: the
# hook writes the table again (tp_prepare_build removed the old one) and
# arduino-cli exports a new image.
rm -f "$OUT/TamaPoke.ino.bin"

# The ESP32 core's first prebuild hook copies a sketch-root partitions.csv into
# the build folder. Point that hook at the 1.75C table instead, so the table is
# used on every build (even after arduino-cli cleans the build folder).
CLEAN_ARG=()
if [[ "$CLEAN" -eq 1 ]]; then CLEAN_ARG=(--clean); fi
arduino-cli compile --warnings=all --jobs "$JOBS" ${CLEAN_ARG[@]+"${CLEAN_ARG[@]}"} \
  --fqbn "$FQBN" \
  --build-path "$BUILD" \
  --build-property "upload.maximum_size=${APP_MAX}" \
  --build-property "compiler.cpp.extra_flags=-DBOARD_AMOLED_175C=1 ${EXTRA}" \
  --build-property "recipe.hooks.prebuild.1.pattern=cp -f \"${PARTS}\" \"{build.path}/partitions.csv\"" \
  --output-dir "$OUT" \
  "$SKETCH"

# arduino-cli checks the sum of the sections; the .bin also carries headers and
# padding (about 250 bytes more), so check the real file against app0 as well.
[[ -f "$OUT/TamaPoke.ino.bin" ]] || { echo "The build wrote no $OUT/TamaPoke.ino.bin" >&2; exit 1; }
BIN_SIZE="$(wc -c < "$OUT/TamaPoke.ino.bin" | tr -d ' ')"
if (( BIN_SIZE > APP_MAX )); then
  echo "TamaPoke.ino.bin is $BIN_SIZE bytes, larger than app0 ($APP_MAX)." >&2
  exit 1
fi

# Check the table that went into the image really is the 1.75C one.
if ! diff -q <(grep -v '^#' "$PARTS" | tr -d ' \r' | grep -v '^$') \
             <(grep -v '^#' "$BUILD/partitions.csv" | tr -d ' \r' | grep -v '^$') >/dev/null; then
  echo "Build used the wrong partition table ($BUILD/partitions.csv)" >&2
  exit 1
fi
cp -f "${ROOT}/boards/175c/partitions.csv" "$OUT/partitions.csv"
echo "1.75C build OK -> $OUT (partitions: boards/175c/partitions.csv)"

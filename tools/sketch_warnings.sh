#!/usr/bin/env bash
# Count the sketch's compiler warnings in a firmware build log and check the
# count against WARN_BASELINE. tools/gate.sh sources this file and CI's
# firmware builds run it, so both count the same way.
#
#   tools/sketch_warnings.sh LOG          prints "N sketch warning(s), baseline B"
#   WARN_BASELINE=21 tools/sketch_warnings.sh LOG
#
# Exit status: 0 if N is at most B, 1 if it is over, 2 if LOG can't be read or
# WARN_BASELINE is not a whole number. LOG is arduino-cli compile --warnings=all
# output (colour codes are ignored).
#
# A sketch warning is a "file:line:col: warning:" line for a file in a folder
# named TamaPoke (the sketch: the gate's staged copy, CI's checkout). The same
# line twice (a header included by two files) counts once. Warnings from the
# ESP32 core and the libraries never count.

# Known sketch warnings per build on main (exact count; update it in the PR that
# changes it). More than this fails: only new warnings are findings.
# arduino-cli prints a warning only when it compiles the file, so builds that
# are counted must compile every sketch file: CI builds from scratch, and the
# gate's incremental builds compile the sketch again every run
# (tools/build_cache.sh). The core and libraries they reuse never counted.
TP_WARN_BASELINE="${WARN_BASELINE:-20}"
TP_SKETCH_WARN_RE='TamaPoke/[^:]+:[0-9]+:[0-9]+: warning:'

# tp_sketch_warnings <log>: print "N sketch warning(s), baseline B", set
# TP_WARN_COUNT to N; return 0 if N <= B, 1 if over, 2 on bad input.
tp_sketch_warnings() {
  local log="$1" esc
  if [[ ! "$TP_WARN_BASELINE" =~ ^[0-9]+$ ]]; then
    echo "WARN_BASELINE must be a whole number; got '$TP_WARN_BASELINE'" >&2
    return 2
  fi
  if [[ ! -f "$log" || ! -r "$log" ]]; then
    echo "Can't read the build log '$log'" >&2
    return 2
  fi
  esc="$(printf '\033')"
  TP_WARN_COUNT="$(sed "s/${esc}\\[[0-9;]*[A-Za-z]//g" <"$log" | grep -E "$TP_SKETCH_WARN_RE" | sort -u | wc -l | tr -d ' ')"
  echo "$TP_WARN_COUNT sketch warning(s), baseline $TP_WARN_BASELINE"
  (( TP_WARN_COUNT <= TP_WARN_BASELINE ))
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  set -uo pipefail
  case "${1:-}" in
    -h|--help) awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; exit 0 ;;
  esac
  if [[ $# -ne 1 ]]; then
    echo "Usage: $0 LOG" >&2
    exit 2
  fi
  tp_sketch_warnings "$1"
  rc=$?
  if [[ "$rc" -eq 1 ]]; then
    echo "+$((TP_WARN_COUNT - TP_WARN_BASELINE)) NEW sketch warning(s) over the baseline: fix them, or raise the baseline in tools/sketch_warnings.sh in the PR that adds them" >&2
  fi
  exit "$rc"
fi

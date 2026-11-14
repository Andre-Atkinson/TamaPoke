# shellcheck shell=bash
# Sourced by tools/build_175c.sh and tools/gate.sh; not run on its own.
#
# Persistent firmware build folders, so a rebuild only recompiles what changed
# (arduino-cli keeps an object while its source and the headers it read are
# older than it). One set per checkout, keyed on its path:
#
#   <cache>/<checkout folder>-<hash of its path>/
#     build-175c/               tools/build_175c.sh run on its own
#     gate/stage/TamaPoke/      the gate's staged copy of the working tree (what the build reads)
#     gate/build-175c/          the gate's 1.75C build
#     gate/build-175/           the gate's original-1.75 build
#     checkout                  the checkout's path
#
# <cache> is ~/Library/Caches/tamapoke on macOS and ${XDG_CACHE_HOME:-~/.cache}/tamapoke
# elsewhere. A build folder takes about 1.5 GB, so the folders of a checkout
# that no longer exists (a removed worktree) are deleted by the next build in
# any checkout (tp_cache_prune). Any of them can also be deleted by hand while
# no build uses it; the next build is then a full one, as with --clean.
#
# Each folder is used by one run at a time: tp_lock takes <folder>.lock (a mkdir
# lock, since macOS has no flock) and tp_unlock_all, run from the caller's EXIT
# trap, gives it back. A lock whose owner is gone (killed with SIGKILL, or a
# reboot) is taken over; the owner is recorded as its pid plus that process's
# start time, so a reused pid does not keep a dead lock alive.
#
# Works with macOS's bash 3.2 and BSD tools, and with Linux.

tp_cache_dir() {  # tp_cache_dir <checkout root>: print its cache folder
  local root="$1" base hash
  if [[ "$(uname -s)" == Darwin ]]; then
    base="$HOME/Library/Caches/tamapoke"
  else
    base="${XDG_CACHE_HOME:-$HOME/.cache}/tamapoke"
  fi
  if command -v shasum >/dev/null 2>&1; then
    hash="$(printf '%s' "$root" | shasum -a 256)"
  else
    hash="$(printf '%s' "$root" | sha256sum)"
  fi
  printf '%s/%s-%s\n' "$base" "$(basename "$root")" "${hash:0:12}"
}

# The start time of a running process, or nothing if there is no such process.
tp_proc_start() { ps -o lstart= -p "$1" 2>/dev/null | sed 's/^ *//; s/ *$//'; }

tp_lock_is_live() {  # tp_lock_is_live <lock folder>: its owner still runs (or is still writing it)
  local owner pid
  owner="$(cat "$1/owner" 2>/dev/null)"
  pid="${owner%% *}"
  [[ ! "$pid" =~ ^[0-9]+$ || "$pid $(tp_proc_start "$pid")" == "$owner" ]]
}

# Record which checkout <cache folder> belongs to, and delete the cache folders
# of checkouts that no longer exist. A folder with no record, or with a lock
# whose owner still runs, is left alone.
tp_cache_prune() {  # tp_cache_prune <cache folder of this checkout> <checkout root>
  local mine="$1" root="$2" dir checkout lock live
  mkdir -p "$mine" && printf '%s\n' "$root" >"$mine/checkout" || return 1
  for dir in "$(dirname "$mine")"/*; do
    [[ -d "$dir" && "$dir" != "$mine" ]] || continue
    checkout="$(cat "$dir/checkout" 2>/dev/null)"
    [[ -n "$checkout" && ! -e "$checkout" ]] || continue
    live=0
    for lock in "$dir"/*.lock "$dir"/*/*.lock; do
      if [[ -d "$lock" ]] && tp_lock_is_live "$lock"; then live=1; fi
    done
    if [[ "$live" -eq 0 ]]; then rm -rf "$dir"; fi
  done
  return 0
}

TP_LOCKS=()
tp_lock() {  # tp_lock <folder>: take <folder>.lock, or say who holds it and return 1
  local lock="$1.lock" me owner pid
  me="$$ $(tp_proc_start "$$")"
  mkdir -p "$(dirname "$lock")" || return 1
  if ! mkdir "$lock" 2>/dev/null; then
    owner="$(cat "$lock/owner" 2>/dev/null)"
    pid="${owner%% *}"
    # The owner is gone if no process has its pid and start time. Only one run
    # may clear it (the .stale folder is the turnstile), and only if the owner
    # did not change meanwhile.
    if ! tp_lock_is_live "$lock" && mkdir "$lock.stale" 2>/dev/null; then
      if [[ "$(cat "$lock/owner" 2>/dev/null)" == "$owner" ]]; then
        rm -rf "$lock"
        echo "Removed a stale lock, $lock: its owner (pid $pid) is no longer running." >&2
      fi
      rmdir "$lock.stale"
    fi
    if ! mkdir "$lock" 2>/dev/null; then
      owner="$(cat "$lock/owner" 2>/dev/null)"
      echo "$1 is in use by another run (pid and start: ${owner:-not recorded yet})." >&2
      echo "Wait for it to finish: two runs must not share a build folder." >&2
      echo "If no gate or build is running, remove the lock and try again: rm -rf '$lock' '$lock.stale'" >&2
      return 1
    fi
  fi
  printf '%s\n' "$me" >"$lock/owner" || { rm -rf "$lock"; return 1; }
  TP_LOCKS+=("$lock")
}

# Get a build folder ready (hold its lock first). The compiled core and
# libraries are reused; every sketch file is compiled again.
tp_prepare_build() {  # tp_prepare_build <build folder> <sketch folder> <clean: 0|1>
  local build="$1" sketch="$2" clean="$3" copy
  if [[ "$clean" -eq 1 ]]; then rm -rf "$build" || return 1; fi
  if [[ -d "$build/sketch" ]]; then
    # arduino-cli builds from its own copy of the sketch, $build/sketch: it
    # compiles every source there and resolves the sketch's includes there. A
    # copy of a file the sketch no longer has (deleted or renamed) would still
    # be built or included, so then the whole copy starts over. Its own
    # outputs there (.o, .d, .a, the merged .ino.cpp) are not copies.
    while IFS= read -r copy; do
      if [[ ! -e "$sketch/${copy#"$build/sketch/"}" ]]; then
        rm -rf "$build/sketch" || return 1
        break
      fi
    done < <(find "$build/sketch" -type f ! -name '*.o' ! -name '*.d' ! -name '*.a' \
               ! -name '*.ino.cpp' ! -name '*.ino.cpp.merged')
  fi
  # arduino-cli prints a file's warnings only when it compiles that file, and
  # the gate counts the sketch's: so no sketch object is kept. Its library
  # detection there (the .libsdetect.d files) is still reused.
  if [[ -d "$build/sketch" ]]; then find "$build/sketch" -name '*.o' -delete || return 1; fi
  mkdir -p "$build"
}

tp_unlock_all() {  # give back every lock this process took (and only those)
  local lock me
  me="$$ $(tp_proc_start "$$")"
  for lock in ${TP_LOCKS[@]+"${TP_LOCKS[@]}"}; do
    [[ "$(cat "$lock/owner" 2>/dev/null)" == "$me" ]] && rm -rf "$lock"
  done
  TP_LOCKS=()
}

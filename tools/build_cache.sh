# shellcheck shell=bash
# Sourced by tools/build_175c.sh and tools/gate.sh; not run on its own.
#
# Persistent firmware build folders, so a rebuild only recompiles what changed
# (arduino-cli keeps an object while its source and the headers it read are
# older than it). One set per checkout, keyed on its path:
#
#   <cache>/<checkout folder>-<hash of its path>/
#     stage/TamaPoke/           tools/build_175c.sh run on its own: its staged copy (what it builds)
#     build-175c/               ... and its build
#     gate/stage/TamaPoke/      the gate's staged copy (what both of its builds read)
#     gate/build-175c/          the gate's 1.75C build
#     gate/build-175/           the gate's original-1.75 build
#     checkout                  the checkout's path, written again by every firmware build there
#
# <cache> is ~/Library/Caches/tamapoke on macOS and ${XDG_CACHE_HOME:-~/.cache}/tamapoke
# elsewhere. A build folder takes about 1.5 GB, and a checkout that ran both the
# gate and a standalone build has three, plus two staged copies: so every
# firmware build (the gate's, or tools/build_175c.sh on its own) deletes the
# cache folders of other checkouts that no longer exist (a removed worktree),
# and those of git worktrees with no firmware build for TP_CACHE_IDLE_DAYS
# days, and says what it freed (tp_cache_prune). Any of them can also be deleted by hand while no
# build uses it; the next build is then a full one, as with --clean.
#
# tools/build_175c.sh and the gate both build a staged copy of the working tree
# (tp_stage), never the checkout itself: arduino-cli wants the sketch's folder
# named after its .ino, and the copy leaves out what the firmware build never
# reads, so a change there does not make the next build a full one.
#
# Each folder is used by one run at a time: tp_lock takes <folder>.lock (a mkdir
# lock, since macOS has no flock) and tp_unlock_all, run from the caller's EXIT
# trap, gives it back. build-175c.lock covers stage/ as well, and gate.lock all
# of gate/. A lock whose owner is gone (killed with SIGKILL, or a
# reboot) is taken over; the owner is recorded as its pid plus that process's
# start time (in UTC and the C locale), so a reused pid does not keep a dead
# lock alive. A lock with no owner recorded is taken over after a minute.
#
# Works with macOS's bash 3.2 and BSD tools, and with Linux.

# A git worktree's cache folder goes after this many days with no firmware build there.
TP_CACHE_IDLE_DAYS=7
# The locks a build takes at the top of a cache folder (tools/build_175c.sh on
# its own, and tools/gate.sh), which a prune takes before deleting the folder.
TP_CACHE_LOCKS=(build-175c gate)

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
# ps prints it in the caller's locale and time zone, which differ between, say,
# an agent's shell and Terminal: pin both, so every run writes and reads one form.
tp_proc_start() { LC_ALL=C TZ=UTC0 ps -o lstart= -p "$1" 2>/dev/null | sed 's/^ *//; s/ *$//'; }

tp_lock_is_live() {  # tp_lock_is_live <lock folder>: its owner still runs
  local owner pid
  owner="$(cat "$1/owner" 2>/dev/null)"
  pid="${owner%% *}"
  if [[ "$pid" =~ ^[0-9]+$ ]]; then
    [[ "$pid $(tp_proc_start "$pid")" == "$owner" ]]
  else
    # No owner yet: a run is between taking the lock and writing it, or was
    # killed right there. Give it a minute.
    [[ -n "$(find "$1" -maxdepth 0 -mmin -1 2>/dev/null)" ]]
  fi
}

# Why another checkout's <cache folder> should go, on stdout; return 1 if it
# should stay. It goes when its checkout no longer exists, or when that
# checkout is a git worktree (its .git is a file: the primary checkout and any
# other clone have a folder there, and are never pruned for being idle) with no
# firmware build for TP_CACHE_IDLE_DAYS days. Every firmware build writes the
# folder's checkout record again, so the record's age is the time since the
# last one. A folder with no record is not ours, and stays.
tp_cache_stale() {  # tp_cache_stale <cache folder>
  local dir="$1" checkout
  checkout="$(cat "$dir/checkout" 2>/dev/null)"
  [[ -n "$checkout" ]] || return 1
  if [[ ! -e "$checkout" ]]; then
    echo "its checkout no longer exists"
  elif [[ -f "$checkout/.git" && -n "$(find "$dir/checkout" -mmin +$((TP_CACHE_IDLE_DAYS * 1440)) 2>/dev/null)" ]]; then
    echo "no firmware build there since $(date -r "$dir/checkout" +%Y-%m-%d 2>/dev/null)"
  else
    return 1
  fi
}

tp_size() {  # tp_size <KiB>: say it in GB, MB or KB
  awk -v k="${1:-0}" 'BEGIN { if (k >= 1048576) printf "%.1f GB", k / 1048576
                              else if (k >= 1024) printf "%d MB", k / 1024
                              else printf "%d KB", k }'
}

tp_is_cache_lock() {  # tp_is_cache_lock <name>: it is one of TP_CACHE_LOCKS' lock folders
  local n
  for n in "${TP_CACHE_LOCKS[@]}"; do
    if [[ "$1" == "$n.lock" ]]; then return 0; fi
  done
  return 1
}

# Delete <cache folder> unless a run holds it, and say so. Its build locks
# (TP_CACHE_LOCKS) are taken first: a build there that holds one keeps the
# folder, and one that starts there meanwhile is refused (with the usual "in
# use" message) instead of losing its files halfway. Any other lock in it whose
# owner still runs (the gate's own 1.75C build holds gate/build-175c.lock)
# keeps it too. With the locks held, the folder is checked again, in case a
# build there wrote its record, or another prune deleted it, in the meantime.
# The checkout record goes last, so a deletion cut short, or one that could
# not delete everything, is finished by a later prune.
tp_cache_remove() {  # tp_cache_remove <cache folder>
  local dir="$1" name lock entry checkout why kb ok=1 taken=()
  for name in "${TP_CACHE_LOCKS[@]}"; do
    if ! tp_lock "$dir/$name" 2>/dev/null; then
      rm -rf ${taken[@]+"${taken[@]}"}
      return 1
    fi
    taken+=("$dir/$name.lock")
  done
  for lock in "$dir"/*.lock "$dir"/*/*.lock; do
    [[ -d "$lock" ]] || continue
    if [[ "${lock%/*}" == "$dir" ]] && tp_is_cache_lock "${lock##*/}"; then continue; fi   # ours now
    if tp_lock_is_live "$lock"; then
      rm -rf "${taken[@]}"
      return 1
    fi
  done
  if ! why="$(tp_cache_stale "$dir")"; then
    rm -rf "${taken[@]}"
    rmdir "$dir" 2>/dev/null || true   # left empty by another prune: our locks made it again
    return 1
  fi
  checkout="$(cat "$dir/checkout" 2>/dev/null)" || true
  kb="$(du -sk "$dir" 2>/dev/null | awk '{ print $1 }')" || true
  # Hidden entries too (a .DS_Store): bash 3.2 has no dotglob, and rmdir below
  # would fail on one. An unmatched pattern stays literal, hence the -e test.
  for entry in "$dir"/* "$dir"/.[!.]* "$dir"/..?*; do
    [[ -e "$entry" || -L "$entry" ]] || continue
    name="${entry##*/}"
    if [[ "$name" == checkout ]] || tp_is_cache_lock "$name"; then continue; fi
    rm -rf "$entry" 2>/dev/null || ok=0
  done
  if [[ "$ok" -eq 1 ]]; then rm -f "$dir/checkout" || ok=0; fi
  rm -rf "${taken[@]}"
  if [[ "$ok" -eq 1 ]] && rmdir "$dir" 2>/dev/null; then
    echo "Build cache: deleted the folder of $checkout ($why): $(tp_size "$kb") freed." >&2
  else
    echo "Build cache: could not delete all of $dir ($why); the next build tries again." >&2
  fi
  return 0
}

# Record which checkout <cache folder> belongs to (which also marks it used
# now), and delete the cache folders of other checkouts that tp_cache_stale
# says should go. A folder with no record, or that a run holds, is left alone.
tp_cache_prune() {  # tp_cache_prune <cache folder of this checkout> <checkout root>
  local mine="$1" root="$2" dir
  mkdir -p "$mine" && printf '%s\n' "$root" >"$mine/checkout" || return 1
  for dir in "$(dirname "$mine")"/*; do
    [[ -d "$dir" && "$dir" != "$mine" ]] || continue
    if tp_cache_stale "$dir" >/dev/null; then tp_cache_remove "$dir" || true; fi
  done
  return 0
}

# Mirror <checkout root>'s working tree (uncommitted changes included) into
# <stage folder>/TamaPoke, the sketch a build compiles; with clean 1 the stage
# folder starts empty. Hold the stage's lock first. The folder is named after
# TamaPoke.ino because arduino-cli refuses a sketch whose folder name differs
# from its main .ino, which is the case in every agent worktree. rsync keeps
# the mtimes, and --delete/--delete-excluded drop whatever is no longer there.
# Folders the firmware build never reads stay out: arduino-cli rebuilds
# everything when the set of source-like files anywhere under the sketch
# changes (its "additionalFiles" build option), so without this a new test, doc
# or simulator file would cost a full rebuild. A folder missing from this list
# is only staged for nothing; tools/ is reduced to the build scripts (the gate
# runs the staged build_175c.sh).
# --checksum: rsync's size-and-mtime quick check misses a same-size edit made
# in the same second as the previous sync (mtimes are whole seconds on some
# rsyncs), and the stage persists between runs, so it would never arrive.
tp_stage() {  # tp_stage <checkout root> <stage folder> <clean: 0|1>
  local root="$1" stage="$2" clean="$3"
  if [[ "$clean" -eq 1 ]]; then rm -rf "$stage" || return 1; fi
  mkdir -p "$stage/TamaPoke" || return 1
  rsync -a --checksum --delete --delete-excluded \
    --include '/tools/' --include '/tools/build_175c.sh' --include '/tools/build_cache.sh' \
    --exclude '/tools/*' \
    --exclude '/.git' --exclude '/.github' --exclude '/.claude' --exclude '/.venv' \
    --exclude '/test' --exclude '/sim' --exclude '/docs' --exclude '/web' \
    --exclude '/out' --exclude '/build' --exclude '/backups' --exclude '/shots' \
    "$root/" "$stage/TamaPoke/"
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
  # The core's prebuild hooks copy the scheme's partition table only when
  # {build.path}/partitions.csv does not exist, and arduino-cli does not notice
  # a change to a .csv, so a table left by an earlier build would be kept
  # forever. Every build's hooks write it again.
  rm -f "$build/partitions.csv" || return 1
  mkdir -p "$build"
}

tp_unlock_all() {  # give back every lock this process took (and only those)
  local lock me
  me="$$ $(tp_proc_start "$$")"
  for lock in ${TP_LOCKS[@]+"${TP_LOCKS[@]}"}; do
    if [[ "$(cat "$lock/owner" 2>/dev/null)" == "$me" ]]; then rm -rf "$lock"; fi
  done
  TP_LOCKS=()
}

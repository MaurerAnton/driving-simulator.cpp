# SPDX-License-Identifier: AGPL-3.0-only
# Sourced by the tools: where the repository is, and how the compiler cache treats precompiled headers.
FS_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime
FS_BUILD="${FS_BUILD:-$FS_ROOT/build}"
# One build at a time on this machine, whoever starts it and in whichever copy of the repository:
# several agents compile here, and four small cores serve one build well and five builds not at all.
# (fs_locked <command …> waits its turn; FS_NOLOCK=1 runs at once.)
FS_LOCK="${FS_LOCK:-/tmp/fahrsim-build.lock}"
fs_locked() {
  if [ -n "$FS_NOLOCK" ] || ! command -v flock > /dev/null; then "$@"; else flock "$FS_LOCK" "$@"; fi
}
# a program run (a test, a tool) takes one of two run slots machine-wide: several helpers each loading the map
# at once, beside the builds, ran the machine out of memory (8 GB – it rebooted twice)
fs_run() {
  if [ -n "$FS_NOLOCK" ] || ! command -v flock > /dev/null; then "$@"; return; fi
  while true; do
    for slot in 1 2; do
      exec {fd}> "/tmp/fahrsim-run-$slot.lock"
      if flock -n "$fd"; then "$@"; local st=$?; exec {fd}>&-; return $st; fi
      exec {fd}>&-
    done
    sleep 3
  done
}
fs_configure() {
  # (again when a test or a tool is newer than the build's plan: its "// LINK:" line is read when the plan is made;
  #  and when FS_CMAKE_ARGS asks for something the plan was not made with – e.g. FS_CMAKE_ARGS=-DFS_SDL3=ON, docs/SDL.md)
  if [ ! -f "$FS_BUILD/build.ninja" ] || [ -n "$(find "$FS_ROOT/tests" "$FS_ROOT/tools/dev" -maxdepth 1 -name '*.cpp' -newer "$FS_BUILD/build.ninja" 2>/dev/null | head -1)" ] ||
     { [ -n "$FS_CMAKE_ARGS" ] && [ "$(cat "$FS_BUILD/.cmake-args" 2>/dev/null)" != "$FS_CMAKE_ARGS" ]; }; then
    # shellcheck disable=SC2086
    fs_locked cmake -S "$FS_ROOT" -B "$FS_BUILD" -G Ninja $FS_CMAKE_ARGS > "$FS_BUILD.configure.log" 2>&1 || { cat "$FS_BUILD.configure.log"; return 1; }
    [ -z "$FS_CMAKE_ARGS" ] || echo "$FS_CMAKE_ARGS" > "$FS_BUILD/.cmake-args"
  fi
}

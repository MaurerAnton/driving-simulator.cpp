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
fs_configure() {
  # (again when a test or a tool is newer than the build's plan: its "// LINK:" line is read when the plan is made)
  if [ ! -f "$FS_BUILD/build.ninja" ] || [ -n "$(find "$FS_ROOT/tests" "$FS_ROOT/tools/dev" -maxdepth 1 -name '*.cpp' -newer "$FS_BUILD/build.ninja" 2>/dev/null | head -1)" ]; then
    fs_locked cmake -S "$FS_ROOT" -B "$FS_BUILD" -G Ninja > "$FS_BUILD.configure.log" 2>&1 || { cat "$FS_BUILD.configure.log"; return 1; }
  fi
}

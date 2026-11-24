# SPDX-License-Identifier: AGPL-3.0-only
# Sourced by the tools: where the repository is, and how the compiler cache treats precompiled headers.
FS_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime
FS_BUILD="${FS_BUILD:-$FS_ROOT/build}"
fs_configure() {
  [ -f "$FS_BUILD/build.ninja" ] || cmake -S "$FS_ROOT" -B "$FS_BUILD" -G Ninja > "$FS_BUILD.configure.log" 2>&1 || { cat "$FS_BUILD.configure.log"; return 1; }
}

#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-only
# Build and run one test or tool:   tools/t.sh lanes_test [arguments …]
# (it waits until no other build runs on this machine – see tools/env.sh; FS_JOBS: how many compilers at once, default 3)
. "$(dirname "$0")/env.sh"
name="$1"; shift
fs_configure || exit 1
fs_locked cmake --build "$FS_BUILD" -j"${FS_JOBS:-3}" -t "$name" 2>&1 | grep -vE "^\[[0-9]+/[0-9]+\] (Building|Linking|Re-running|Generating)|^-- |^ninja: no work" 
[ "${PIPESTATUS[0]}" = 0 ] || exit 1
cd "$FS_ROOT" && "$FS_BUILD/$name" "$@"

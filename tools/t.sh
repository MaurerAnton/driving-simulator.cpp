#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-only
# Build and run one test or tool:   tools/t.sh lanes_test [arguments …]
# (FS_JOBS: how many compilers at once, default 2 – the tablet this is developed on has four small cores)
. "$(dirname "$0")/env.sh"
name="$1"; shift
fs_configure || exit 1
cmake --build "$FS_BUILD" -j"${FS_JOBS:-2}" -t "$name" 2>&1 | grep -vE "^\[[0-9]+/[0-9]+\] (Building|Linking|Re-running|Generating)|^-- |^ninja: no work" 
[ "${PIPESTATUS[0]}" = 0 ] || exit 1
cd "$FS_ROOT" && "$FS_BUILD/$name" "$@"

#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-only
# Build everything and run every test:   tools/check.sh
. "$(dirname "$0")/env.sh"
fs_configure || exit 1
fs_locked cmake --build "$FS_BUILD" -j"${FS_JOBS:-3}" -t all tests 2>&1 | grep -vE "^\[[0-9]+/[0-9]+\] (Building|Linking|Re-running|Generating)|^-- "
[ "${PIPESTATUS[0]}" = 0 ] || exit 1
ctest --test-dir "$FS_BUILD" --output-on-failure -j2 "$@"

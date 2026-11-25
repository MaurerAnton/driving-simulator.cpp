#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-only
# Check that source files compile (no object is written – the quickest check there is):
#   tools/cc.sh src/world/lanes.cpp [more files …]
. "$(dirname "$0")/env.sh"
status=0
for f in "$@"; do
  # (one of these checks at a time on this machine, beside the one build: memory is short)
  FS_LOCK="/tmp/fahrsim-cc.lock" fs_locked g++ -std=gnu++26 -fsyntax-only -O0 -Wall -Wextra -Wno-unused-parameter -I"$FS_ROOT/src" -I"$FS_ROOT" -DFS_NATIVE_GL=1 -DFS_HAVE_PNG=1 ${FS_CC_FLAGS:-} "$f" || status=1
done
exit $status

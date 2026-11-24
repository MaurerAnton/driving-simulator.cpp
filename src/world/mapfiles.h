// SPDX-License-Identifier: AGPL-3.0-only
// Origin: tools/mapgen/mapfiles.ts @ v1.7.7
/** The files of a map folder on disk, for the map loader (src/world/loader.h) in tools and tests. */
#pragma once

#include <memory>
#include <string>

#include "world/loader.h"

namespace fs {

#ifndef __EMSCRIPTEN__
/**
 * The folder `dir` as the files of a map: a file is read when it is asked for (and gunzipped
 * if it is gzipped); the answer comes with the next turn of the host's loop (plat::soon), as
 * the original's comes with the next turn of the promises – whoever asked goes on first.
 */
std::shared_ptr<MapFiles> diskFiles(std::string dir);
#endif

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/netstats.ts @ v1.7.7
/**
 * How much has been downloaded since the page opened – the program, the
 * map, the tiles, the pictures – for the loading screens ("12.4 MB").
 *
 * (The original counts it from the browser's own resource timing – `startNetStats`,
 * `downloaded` – which the host layer has no word for yet: the two are not here.  What
 * is here is how the number is written.)
 */
#pragma once

#include <string>

namespace fs {

/** "12.4 MB" (or kB below one megabyte), in the language's number format. */
std::string formatBytes(double n, const std::string& locale = "en");

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
/**
 * 2.0: play on another region in one step.  The big map shows the roads of a
 * region that is not loaded; "Put the car here" on one of them keeps the place
 * here (in the session's store, for the one reload that follows), chooses that
 * region in the settings and reloads – and the game, once the region is loaded,
 * begins a free drive at that place at once, without the menu, and forgets it.
 * (New in 2.0: 1.7.7 shows one region at a time.)
 */
#pragma once

#include <optional>
#include <string>

#include "world/types.h"

namespace fs {

/** where the place is kept (sessionStorage) */
inline constexpr const char* START_AT_KEY = "fahrsim2.startAt";

/** Keep the place a drive is to begin at in region `region` ('' : the site's own map) after the reload. */
void saveStartAt(const std::string& region, const SpawnJson& spot);

/**
 * The place kept for region `region` – as a rough place on a street (put into a lane once the roads
 * there are loaded) – and forget it.  Null: none kept, or one for another region (forgotten too).
 */
std::optional<SpawnJson> takeStartAt(const std::string& region);

}  // namespace fs

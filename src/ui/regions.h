// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/regions.ts @ v1.7.7
/**
 * The settings' part for the region: the site's own map, or one of the regions
 * of map-assets (world/regions.ts) – choosing one.  A region is a map of its own
 * (its own extent, roads and places to start at); its files come from
 * map-assets (world/regionfiles.ts), and one is loaded instead of the site's
 * map when the game is loaded again.
 *
 * The list is read once from `regions.json` beside the parts index.  A region
 * the index does not name is not offered; if the index cannot be read (an older
 * repository without regions, or offline) only the site's own map is offered.
 *
 * (2.0: the big map shows every region and lets the player pick one there –
 * ui/bigmap.h, docs/WORLD-MAP.md; this part of the settings stays as it was.)
 */
#pragma once

#include <functional>
#include <optional>
#include <string>

#include "core/task.h"
#include "game/settings.h"
#include "i18n/index.h"
#include "platform/dom.h"
#include "ui/geoip.h"
#include "world/regions.h"

namespace fs {

/**
 * Where the index of regions is had: the repository over raw.githubusercontent (CORS ok), or the site.
 *
 * (The original's defaults are `fetchRepo = true, siteBase = 'map-assets/'`.  They are written
 * out where it is called: world/regions.h has a loadRegionIndex(siteBase) of its own – the same
 * search with the port's addresses –, and a call with one text must stay that one.)
 */
Task<std::optional<RegionIndex>> loadRegionIndex(bool fetchRepo, std::string siteBase, FetchFn fetchFn = nullptr);

/** The region with this id in the index, or null. */
const Region* findRegion(const RegionIndex* index, const std::string& id);

/**
 * The settings' part for the region: a choice among the regions of map-assets,
 * and the site's own map as the first entry.  Changing it is applied the next
 * time the game is loaded (a button does that).
 * (`s`: the game's settings – they outlive the element.)
 */
dom::El regionSettings(Settings& s, std::function<std::string(const std::string& key, const TrParams& vars)> t);

}  // namespace fs

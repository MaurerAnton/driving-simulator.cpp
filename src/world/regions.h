// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The regions of the game: the maps it can load besides the one the site
 * serves (map-assets/regions.json of the original repository).
 *
 * The site ships one map (North Hesse); the other regions are maps of their own –
 * a region has its own extent, its own roads, its own places to start at – and
 * their files live in map-assets of the original repository, read from
 * raw.githubusercontent.com (which answers with a CORS header).
 *
 * `regions.json` is the index of them: what exists and where each one's files are:
 *
 *   { "latest": "hesse",
 *     "regions": [ { "id": "bremen", "name": "Bremen", "box": [minX, minZ, maxX, maxZ],
 *                    "map": "regions/bremen/map.json", "tiles": ["34.tar"] }, … ] }
 *
 * Every region – Germany and abroad alike – was built in one shared frame (origin
 * E 528000 / N 5678000, UTM zone 32: geo/utm), so one map of the world can show them all.
 *
 * Origin: src/world/regions.ts and the index loading of src/ui/regions.ts @ v1.7.7
 */
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "core/task.h"
#include "platform/json.h"

namespace fs {

/** One region as the index names it. */
struct Region {
  /** the name the region is referred to by (its folder under map-assets/regions/) */
  std::string id;
  /** what it is called in the interface */
  std::string name;
  /** where it lies in the world frame: [minX, minZ, maxX, maxZ] in metres */
  std::array<double, 4> box{0, 0, 0, 0};
  /** the path of its map.json, relative to map-assets/ */
  std::string map;
  /** the parts (files of map-assets) that carry its tiles */
  std::vector<std::string> tiles;
};

/** The index of map-assets: what regions exist, which one is the default. */
struct RegionIndex {
  /** the region the index names as its latest */
  std::string latest;
  std::vector<Region> regions;
};

/** The folder every region's files live under, relative to map-assets/. */
inline const char* const REGIONS_DIR = "regions";
/** where map-assets is read from: the original repository over raw.githubusercontent.com */
inline const char* const MAP_ASSETS_URL = "https://raw.githubusercontent.com/MaurerAnton/driving-simulator/main/map-assets/";

/** The path of a region's map.json from its id (the folder it is kept in). */
std::string regionMapPath(const std::string& id);
/**
 * The index the game reads: `latest` and the regions, in the order they are
 * listed (which is the order they are offered in).  Anything unusable is left
 * out rather than failing the whole index – a region the game cannot read is
 * one the player simply does not see.
 */
std::optional<RegionIndex> parseRegions(const Json& json);
/** The region the index names as its default, or null. */
const Region* defaultRegion(const RegionIndex* index);
/** The region with this id, or null. */
const Region* regionById(const RegionIndex* index, const std::string& id);
/** Is the point inside the region's box (world metres)? – where the big map may show it. */
bool boxContains(const std::array<double, 4>& box, double x, double z);
/** The region a point lies in, or null (it lies in none of them). */
const Region* regionAt(const RegionIndex* index, double x, double z);
/**
 * The index: from the original repository, else the copy the site carries
 * (`siteBase` + "regions.json").  Null: neither could be read (offline).
 */
Task<std::optional<RegionIndex>> loadRegionIndex(std::string siteBase);

}  // namespace fs

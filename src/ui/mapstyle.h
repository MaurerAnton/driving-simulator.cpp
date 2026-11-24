// SPDX-License-Identifier: AGPL-3.0-only
/**
 * 2.0: how the big map looks – the dark style of 1.7.7 (the default) or a light
 * one that looks like a familiar street map – and the map's own preferences
 * (the style, whether the search field may ask the online place search).
 *
 * The colours of land, water, the roads by their kind and the names are all
 * here, for the big map's own drawing (ui/bigmap.cpp, ui/mapoverview.cpp) and
 * the world behind it (ui/worldlayer.cpp).  (New in 2.0: 1.7.7 has the dark map only.)
 */
#pragma once

#include <string>

namespace fs {

/** The kinds of road the map tells apart (from the road classes of world/types.h). */
enum class RoadKind : int { service = 0, minor = 1, tertiary = 2, secondary = 3, primary = 4, motorway = 5 };
constexpr int ROAD_KINDS = 6;

/** a road class (world/types.h RoadClass, 0 … 14) as the map draws it */
RoadKind roadKindOfClass(int cls);
/** a priority tier (Edge.tier, world/types.h CLASS_TIER) as the map draws it */
RoadKind roadKindOfTier(double tier);

struct MapStyle {
  /** 'dark' | 'light' */
  const char* name;
  /** outside the map and under the world: sea */
  const char* background;
  /** the area of the loaded map */
  const char* area;
  /** the world: sea, land, borders, coasts, the countries' names */
  const char* sea;
  const char* land;
  const char* border;
  const char* coast;
  const char* countryName;
  /** the regions: their boxes, the loaded one, the chosen one; their names and the names' halo */
  const char* region;
  const char* regionFill;
  const char* current;
  const char* currentFill;
  const char* chosenFill;
  const char* regionText;
  /** the roads by RoadKind: their colour, and the line around them (null: none) */
  const char* road[ROAD_KINDS];
  const char* casing[ROAD_KINDS];
  /** the names of places: cities, the others, the halo around them */
  const char* bigName;
  const char* placeName;
  const char* halo;
};

/** the style of that name ('light'; anything else: 'dark') */
const MapStyle& mapStyleOf(const std::string& name);

/** what the player chose for the big map (kept on this device) */
struct MapPrefs {
  /** 'dark' (1.7.7's look) | 'light' */
  std::string style = "dark";
  /** may the search field ask the online place search (Photon by komoot)? */
  bool online = true;
};

MapPrefs loadMapPrefs();
void saveMapPrefs(const MapPrefs& p);

}  // namespace fs

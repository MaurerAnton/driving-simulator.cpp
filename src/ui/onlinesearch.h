// SPDX-License-Identifier: AGPL-3.0-only
/**
 * 2.0: places anywhere, from the online place search Photon by komoot
 * (https://photon.komoot.io – OpenStreetMap data, ODbL; it answers with a
 * CORS header).  The big map's search field asks it when the player has
 * stopped typing; its hits lead into the loaded map, into another region, or
 * are only listed (greyed) where there is no map of the game.
 * Credit, shown under the hits: PHOTON_CREDIT (map2.onlineCredit).
 * (New in 2.0: 1.7.7 searches the map's own lists only.)
 */
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/task.h"

namespace fs {

inline constexpr const char* PHOTON_API = "https://photon.komoot.io/api/";

/** A place the online search found. */
struct OnlinePlace {
  /** its name ("Kassel", "Wilhelmshöher Allee 12") */
  std::string name;
  /** where it is ("Hessen, Deutschland"): its town, state and country as far as known */
  std::string detail;
  double lat = 0;
  double lon = 0;
  /** OpenStreetMap's kind of it ("city", "town", "residential" …) */
  std::string kind;
};

/** The address of a search: `q`, the place to prefer hits near (lat, lon), how many, the language of the names (only those Photon knows: de, en, fr, it). */
std::string photonUrl(const std::string& q, std::optional<std::pair<double, double>> near, int limit, const std::string& lang);

/** The places of Photon's answer (GeoJSON); nothing for anything else. */
std::vector<OnlinePlace> parsePhoton(const std::string& text);

/** Ask Photon (nothing when it does not answer). */
Task<std::vector<OnlinePlace>> searchPhoton(std::string q, std::optional<std::pair<double, double>> near, int limit, std::string lang);

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/history.ts @ v1.7.7
/**
 * What was looked for on the map before: the places picked in its search,
 * and the ways that were driven (to one place or several) – kept in the
 * browser, so that they are there at the next visit and a tap sets them
 * again.  Like the tours driven (tourlog.h), only on this device.
 */
#pragma once

#include <string>
#include <vector>

namespace fs {

/** A place picked in the search of the map. */
struct PlaceRecord {
  std::string name;
  double x = 0;
  double z = 0;
  double yaw = 0;
  /** when (ISO) */
  std::string date;
};

/** A place by its name: where it is, and which way one faces there (a PlaceRecord without its date – a stop of a way, a favourite). */
struct NamedPlace {
  std::string name;
  double x = 0;
  double z = 0;
  double yaw = 0;
};

/** A way that was started: its places in order, whether the car drove it or the player. */
struct WayRecord {
  std::vector<NamedPlace> stops;
  bool self = false;
  std::string date;
};

/** how many of each are kept */
inline constexpr size_t PLACES_KEPT = 12;
inline constexpr size_t WAYS_KEPT = 10;

/** The places picked before, the newest first. */
std::vector<PlaceRecord> loadPlaces();

/** A place picked: at the front of the list (once – a place picked again moves up).  `now`: the moment, ms since 1970. */
std::vector<PlaceRecord> savePlace(const NamedPlace& p);
std::vector<PlaceRecord> savePlace(const NamedPlace& p, double now);

/** The ways started before, the newest first. */
std::vector<WayRecord> loadWays();

/** A way started: at the front of the list (the same places again move it up). */
std::vector<WayRecord> saveWay(const std::vector<NamedPlace>& stops, bool self);
std::vector<WayRecord> saveWay(const std::vector<NamedPlace>& stops, bool self, double now);

// ------------------------------------------------------------------ favourite places

/** A place the player keeps: a name (given, or that of the road there) and where it is. */
using FavouriteRecord = NamedPlace;

/** how many are kept at most */
inline constexpr size_t FAVOURITES_KEPT = 50;

/** The favourite places, in the order they were added (the newest first). */
std::vector<FavouriteRecord> loadFavourites();

/** A place added to the favourites (a place within 20 m of one already there replaces it). */
std::vector<FavouriteRecord> saveFavourite(const FavouriteRecord& p);

/** A favourite taken out again (the one within 20 m of x, z). */
std::vector<FavouriteRecord> removeFavourite(double x, double z);

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/placesearch.ts @ v1.7.7
/**
 * Looking for a place on the big map: one field for towns and villages,
 * stops of trams and buses, streets and house numbers.  The towns of the
 * map (world.json places) and the stops (stops.json.gz) come first, then
 * what the address search finds (game/address.ts) – the map goes to the
 * place picked.
 *
 * 2.0: the field also finds the regions of the map of all regions, by their
 * name and their country (searchRegions) – listed after the places of the
 * loaded map.
 */
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game/address.h"

namespace fs {

/** A town, a village or a part of one (world.json `places`). */
struct TownSource {
  std::string name;
  std::string kind;
  double x = 0;
  double z = 0;
};

/** A stop of trams or buses (world/stops.ts StopPlace). */
struct StopSource {
  std::string name;
  std::string town;
  double x = 0;
  double z = 0;
};

/**
 * A line of public transport as the search may find it: its number ("5", "52",
 * "N3"), whether it is a tram or a bus, and where it runs (for putting the map
 * there).  The lines come from lines.json.gz (trams) and buslines.json.gz
 * (world/buslines.ts); `ref` is what the player types.
 */
struct LineSource {
  /** the line's number as it is shown ("5", "52") */
  std::string ref;
  /** 'tram' or 'bus' (the timetable's kinds) */
  std::string kind;
  /** where it runs: its first stop (for putting the map there) */
  double x = 0;
  double z = 0;
  /** the line's index in its own list (to show its whole way) */
  int index = 0;
};

/** What the map's search field finds: an address hit, with what kind of place it is. */
struct PlaceHit : AddressHit {
  /** 'town' (also a village or a part of a town), 'stop', 'line' (public transport), or 'address' (a street or a house) – 2.0: or 'region' */
  std::string what;
  /** for a 'line': the line itself (transitmap.ts shows its way) */
  std::optional<LineSource> line;
  /** 2.0: for a 'region': its id (WorldLayer::byId; "" is the site's own map) */
  std::string region;
};

/** where the one who asks is (`{ x, z } | null`) */
using PlaceNear = std::optional<AddressIndex::Near>;
/** the address search (game/address.ts AddressIndex.search), or null */
using AddressSearch = std::function<std::vector<AddressHit>(const std::string& q, size_t limit, const PlaceNear& near)>;

/**
 * The places that fit `query`: towns, then stops, then the addresses of
 * `addresses` (a street, a house) – each once, at most `limit`; what lies
 * near `near` first within a kind.
 */
std::vector<PlaceHit> searchPlaces(const std::string& query, const std::vector<TownSource>& towns, const std::vector<StopSource>& stops, const AddressSearch& addresses, size_t limit = 8,
                                   const PlaceNear& near = std::nullopt, const std::vector<LineSource>& lines = {});

/** 2.0: A region as the search may find it: its name and the country it lies in, and its middle (for putting the map there). */
struct RegionSource {
  std::string id;
  std::string name;
  /** '' where it is not known */
  std::string country;
  double x = 0;
  double z = 0;
};

/**
 * 2.0: The regions that fit `query` – each word begins a word of the region's name or of
 * its country – at most `limit`: those whose name begins with what was typed first, then
 * in the order they are given (the order they are offered in).  `label`: how a hit is
 * written (default: "Name, Country").
 */
std::vector<PlaceHit> searchRegions(const std::string& query, const std::vector<RegionSource>& regions, size_t limit = 3, const std::function<std::string(const RegionSource&)>& label = nullptr);

}  // namespace fs

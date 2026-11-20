// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Start or jump to an address: search in the house numbers of the map
 * (OpenStreetMap `addr:*`, see tools/build-addresses.ts) and find the spot on
 * the road in front of the house.
 *
 * Origin: src/game/address.ts (MaurerAnton/driving-simulator @ 1c08919, v0.5.0).
 */
#pragma once

#include <set>
#include <string>
#include <vector>

#include "core/geom.h"
#include "world/lanes.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

/** What `public/map/addresses.json` holds: world coordinates in whole metres. */
struct AddressJson {
  struct StreetJson {
    /** street */
    std::string n;
    /** town, with the district where known */
    std::string c;
    /** a point of the street (for "anywhere in this street") */
    double x = 0, z = 0;
    /**
     * house numbers: number, x, z, number, x, z, … — as written in the file,
     * strings for the numbers and numbers for the coordinates (the original's
     * `Array<string | number>`).
     */
    std::vector<std::string> h;
  };
  int version = 0;
  std::vector<StreetJson> streets;
};

/** Parse `public/map/addresses.json`; false when it is not that document. */
bool parseAddressJson(const std::string& text, AddressJson& out);

struct AddressHit {
  /** "Am Stadtpark 5, Baunatal" */
  std::string label;
  std::string street;
  double x = 0, z = 0;
  /** the house number asked for was found */
  bool exact = false;
};

/**
 * Lower case, no accents, "straße" = "strasse" = "str.", "ue" = "ü" = "u":
 * whatever the keyboard at hand makes of a German street name.
 */
std::string normaliseAddress(const std::string& text);

class AddressIndex {
 public:
  explicit AddressIndex(const AddressJson& json);

  size_t size() const;
  size_t streetCount() const { return streets_.size(); }

  /**
   * "am stadtpark 5", "hauptstr grossenritte", "5 Marktplatz" …
   * Best matches first; with a house number the houses of the matching
   * streets, without one the streets themselves.  `near` (optional): of
   * streets that match equally well the nearest comes first.
   */
  std::vector<AddressHit> search(const std::string& query, int limit = 8,
                                 const double* nearX = nullptr, const double* nearZ = nullptr) const;

 private:
  struct House {
    std::string no, key;
    double x = 0, z = 0;
  };
  struct Street {
    std::string name, town, key, flat, townKey;
    double x = 0, z = 0;
    std::vector<House> houses;
  };
  std::vector<Street> streets_;
};

/**
 * The lanes of the connected road network: those one can reach from the town
 * centre and get back from.  At the rim of the map there are fragments of
 * streets whose connections lie outside it – nobody should be put there.
 */
std::set<int> mainNetwork(World& world, LaneGraph& graph);

/**
 * Where to put the car for a house at (x, z): on the nearest road one may
 * drive on – preferably the street of that name –, in the right-hand lane,
 * with the house on the right, clear of the junctions.
 */
SpawnJson findRoadSpot(World& world, LaneGraph& graph, double x, double z,
                       const std::string& street = "", double maxDist = 220,
                       const std::set<int>* main = nullptr);

}  // namespace fs

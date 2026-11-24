// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/address.ts @ v1.7.7
/**
 * Start or jump to an address: search in the house numbers of the map
 * (OpenStreetMap `addr:*`, see tools/build-addresses.ts) and find the spot on
 * the road in front of the house.
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>

#include "game/settings.h"
#include "platform/json.h"
#include "world/lanes.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

/** What `public/map/addresses.json` holds: world coordinates in whole metres. */
struct AddressJson {
  int version = 0;
  struct Street {
    /** street */
    std::string n;
    /** town, with the district where known */
    std::string c;
    /** a point of the street (for "anywhere in this street") */
    double x = 0;
    double z = 0;
    /** house numbers: number, x, z, number, x, z, … */
    std::vector<std::variant<std::string, double>> h;
  };
  std::vector<Street> streets;
};
/** (`jsonParse(text, addresses)`: the file as it is written) */
void fromJson(JsonReader& r, AddressJson::Street& s);
void fromJson(JsonReader& r, AddressJson& j);

/**
 * What the search finds – label: "Am Stadtpark 5, Baunatal"; street; x, z: position of the house (or of the
 * street) in world coordinates; exact: the house number asked for was found.  (The settings keep where the last
 * drive began as the same thing: game/settings.h.)
 */
using AddressHit = StartAddress;

/**
 * Lower case, no accents, "straße" = "strasse" = "str.", "ue" = "ü" = "u":
 * whatever the keyboard at hand makes of a German street name.
 */
std::string normaliseAddress(const std::string& text);

/** `a.localeCompare(b, 'de')`: which of two texts comes first as German sorts them (< 0: a, > 0: b, 0: none) */
int compareGerman(std::string_view a, std::string_view b);

class AddressIndex {
 public:
  explicit AddressIndex(const AddressJson& json);

  /**
   * The index of a file's text (addresses.json), street by street – nothing else is kept of the file, which is
   * large.  Null: the text is no such file.
   */
  static std::unique_ptr<AddressIndex> parse(std::string_view text);

  /** how many houses */
  size_t size() const;

  size_t streetCount() const { return streets.size(); }

  /** where the one who asks is */
  struct Near {
    double x = 0;
    double z = 0;
  };

  /**
   * "am stadtpark 5", "hauptstr grossenritte", "5 Marktplatz" …
   * Best matches first; with a house number the houses of the matching
   * streets, without one the streets themselves.
   *
   * `near`: where the one who asks is – of streets that match equally well
   * (every town has its Marktplatz) the nearest comes first.
   */
  std::vector<AddressHit> search(const std::string& query, size_t limit = 8, const std::optional<Near>& near = std::nullopt) const;

 private:
  struct House {
    std::string no;
    std::string key;
    double x = 0;
    double z = 0;
  };
  struct Street {
    std::string name;
    std::string town;
    /** normalised, with and without spaces */
    std::string key;
    std::string flat;
    std::string townKey;
    double x = 0;
    double z = 0;
    std::vector<House> houses;
  };
  std::vector<Street> streets;

  AddressIndex() = default;
  void add(const AddressJson::Street& s);
};

// ------------------------------------------------------------------ road spot

/**
 * The lanes of the connected road network: those one can reach from the town
 * centre and get back from.  At the rim of the map there are fragments of
 * streets whose connections lie outside it – nobody should be put there.
 * (The numbers of the lanes: Lane.id.)
 */
std::unordered_set<int> mainNetwork(World& world, LaneGraph& graph);

/**
 * Where to put the car for a house at (x, z): on the nearest road one may
 * drive on – preferably the street of that name –, in the right-hand lane,
 * with the house on the right, clear of the junctions.
 * (`main`: the lanes that may be used – all unless given; `accept`: the roads; null: no such road near.)
 */
std::optional<SpawnJson> findRoadSpot(World& world, LaneGraph& graph, double x, double z, const std::string& street = "", double maxDist = 220,
                                      const std::function<bool(const Lane& lane)>& main = nullptr, const std::function<bool(const Edge& e)>& accept = nullptr, double room = 2);

}  // namespace fs

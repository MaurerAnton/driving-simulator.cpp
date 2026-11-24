// SPDX-License-Identifier: AGPL-3.0-only
/**
 * 2.0: the streets of the map as the search field finds them where the map has
 * no address list (the regions): every named road of the pieces that were
 * loaded so far, with the town it lies in – collected as the pieces come, and
 * kept while the game runs.
 * (New in 2.0: 1.7.7 finds streets in the site's address list only.)
 */
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "game/address.h"

namespace fs {

class StreetBook {
 public:
  /** a street seen at (x, z), in `town` ('': not known) – each street of a town once */
  void add(const std::string& name, double x, double z, const std::string& town);
  /** is that street of that town known already? */
  bool has(const std::string& name, const std::string& town) const;
  size_t size() const { return streets_.size(); }
  /** is a street of that name known within `d` metres of (x, z)? (then the same street again needs no town looked up) */
  bool near(const std::string& name, double x, double z, double d) const;
  /** the streets whose name (and town) begin with the words typed, the name begun with first, then the nearest */
  std::vector<AddressHit> search(const std::string& query, size_t limit, const std::optional<AddressIndex::Near>& near) const;
  /** the `limit` streets nearest to (x, z) */
  std::vector<AddressHit> nearest(double x, double z, size_t limit) const;

 private:
  struct Street {
    std::string name;
    std::string town;
    std::string key;
    double x = 0;
    double z = 0;
  };
  std::vector<Street> streets_;
  std::unordered_map<std::string, size_t> byKey_;
  /** the streets of each name (normalised) */
  std::unordered_map<std::string, std::vector<size_t>> byName_;
};

}  // namespace fs

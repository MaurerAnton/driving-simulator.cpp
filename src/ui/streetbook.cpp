// SPDX-License-Identifier: AGPL-3.0-only
// 2.0: the streets of the loaded pieces, for the search field (ui/streetbook.h)
#include "ui/streetbook.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"

namespace fs {

namespace {
std::vector<std::string> wordsOf(const std::string& text) {
  std::vector<std::string> out;
  for (const std::string& w : split(normaliseAddress(text), " "))
    if (!w.empty()) out.push_back(w);
  return out;
}
}  // namespace

void StreetBook::add(const std::string& name, double x, double z, const std::string& town) {
  if (trim(name).empty()) return;
  const std::string key = normaliseAddress(name) + "|" + normaliseAddress(town);
  if (byKey_.count(key)) return;
  byKey_.emplace(key, streets_.size());
  byName_[normaliseAddress(name)].push_back(streets_.size());
  streets_.push_back({name, town, normaliseAddress(name + " " + town), x, z});
}

bool StreetBook::near(const std::string& name, double x, double z, double d) const {
  const auto it = byName_.find(normaliseAddress(name));
  if (it == byName_.end()) return false;
  for (const size_t i : it->second)
    if (std::hypot(streets_[i].x - x, streets_[i].z - z) <= d) return true;
  return false;
}

bool StreetBook::has(const std::string& name, const std::string& town) const { return byKey_.count(normaliseAddress(name) + "|" + normaliseAddress(town)) > 0; }

std::vector<AddressHit> StreetBook::search(const std::string& query, size_t limit, const std::optional<AddressIndex::Near>& near) const {
  const std::vector<std::string> words = wordsOf(query);
  if (words.empty()) return {};
  const std::string joined = join(words, " ");
  struct Found {
    const Street* s;
    bool whole;
    double d;
  };
  std::vector<Found> found;
  for (const Street& s : streets_) {
    const std::vector<std::string> parts = split(s.key, " ");
    bool ok = true;
    for (const std::string& w : words) {
      bool some = false;
      for (const std::string& p : parts)
        if (startsWith(p, w)) {
          some = true;
          break;
        }
      if (!some) {
        ok = false;
        break;
      }
    }
    if (!ok) continue;
    found.push_back({&s, startsWith(s.key, joined), near ? std::hypot(s.x - near->x, s.z - near->z) : 0.0});
  }
  std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
    if (a.whole != b.whole) return a.whole;
    return a.d < b.d;
  });
  std::vector<AddressHit> out;
  for (size_t i = 0; i < found.size() && out.size() < limit; i++) {
    const Street& s = *found[i].s;
    AddressHit h;
    h.label = s.town.empty() ? s.name : s.name + ", " + s.town;
    h.street = s.name;
    h.x = s.x;
    h.z = s.z;
    h.exact = false;
    out.push_back(std::move(h));
  }
  return out;
}

std::vector<AddressHit> StreetBook::nearest(double x, double z, size_t limit) const {
  std::vector<std::pair<double, size_t>> order;
  order.reserve(streets_.size());
  for (size_t i = 0; i < streets_.size(); i++) order.emplace_back(std::hypot(streets_[i].x - x, streets_[i].z - z), i);
  const size_t n = std::min(limit, order.size());
  std::partial_sort(order.begin(), order.begin() + static_cast<long>(n), order.end());
  std::vector<AddressHit> out;
  for (size_t k = 0; k < n; k++) {
    const Street& s = streets_[order[k].second];
    AddressHit h;
    h.label = s.town.empty() ? s.name : s.name + ", " + s.town;
    h.street = s.name;
    h.x = s.x;
    h.z = s.z;
    h.exact = false;
    out.push_back(std::move(h));
  }
  return out;
}

}  // namespace fs

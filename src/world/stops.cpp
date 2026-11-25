// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/stops.ts @ v1.7.7
#include "world/stops.h"

#include <cmath>
#include <limits>
#include <set>
#include <unordered_set>

namespace fs {

namespace {
constexpr double CELL = 250;
}

Stops::Stops(const StopsJson& json) {
  for (const auto& [name, town, kind, x, z] : json.places) {
    StopPlace p;
    p.id = static_cast<int>(places.size());
    p.name = name;
    p.town = town >= 0 && town < static_cast<int>(json.towns.size()) ? json.towns[static_cast<size_t>(town)] : "";
    p.kind = kind;
    p.x = x;
    p.z = z;
    places.push_back(std::move(p));
  }
  for (const auto& [pi, x, z, flags] : json.poles) {
    if (pi < 0 || pi >= static_cast<int>(places.size())) continue;
    StopPlace& place = places[static_cast<size_t>(pi)];
    place.poles.push_back(StopPole{&place, x, z, flags});
  }
  for (const auto& [pi, flags, width, pts] : json.platforms) {
    if (pi < 0 || pi >= static_cast<int>(places.size()) || pts.size() < 4) continue;
    StopPlace& place = places[static_cast<size_t>(pi)];
    StopPlatform f;
    f.place = &place;
    f.flags = flags;
    f.width = width;
    f.pts.assign(pts.begin(), pts.end());
    f.area = (flags & STOP_FLAG_AREA) != 0;
    place.platforms.push_back(std::move(f));
  }
  for (StopPlace& p : places) {
    // a place is wherever its members are: listed in every cell they reach (in the order the original's Set has them)
    std::vector<int64_t> cells;
    std::unordered_set<int64_t> seen;
    const auto add = [&](int64_t k) {
      if (seen.insert(k).second) cells.push_back(k);
    };
    add(key(p.x, p.z));
    for (const StopPole& q : p.poles) add(key(q.x, q.z));
    for (const StopPlatform& f : p.platforms)
      for (size_t i = 0; i + 1 < f.pts.size(); i += 2) add(key(f.pts[i], f.pts[i + 1]));
    for (int64_t k : cells) grid_[k].push_back(&p);
  }
}

int64_t Stops::key(double x, double z) {
  return (static_cast<int64_t>(std::floor(x / CELL)) + 32768) * 65536 + (static_cast<int64_t>(std::floor(z / CELL)) + 32768);
}

std::vector<StopPlace*>& Stops::near(double x, double z, double r, std::vector<StopPlace*>& out) const {
  out.clear();
  std::unordered_set<int> seen;
  const int64_t i0 = static_cast<int64_t>(std::floor((x - r) / CELL));
  const int64_t i1 = static_cast<int64_t>(std::floor((x + r) / CELL));
  const int64_t j0 = static_cast<int64_t>(std::floor((z - r) / CELL));
  const int64_t j1 = static_cast<int64_t>(std::floor((z + r) / CELL));
  for (int64_t i = i0; i <= i1; i++) {
    for (int64_t j = j0; j <= j1; j++) {
      auto it = grid_.find((i + 32768) * 65536 + (j + 32768));
      if (it == grid_.end()) continue;
      for (StopPlace* p : it->second) {
        if (!seen.insert(p->id).second) continue;
        out.push_back(p);
      }
    }
  }
  return out;
}

std::vector<const StopPlace*> Stops::near(double x, double z, double r) const {
  std::vector<StopPlace*> out;
  near(x, z, r, out);
  return std::vector<const StopPlace*>(out.begin(), out.end());
}

std::vector<StopPlace*> Stops::tramPlaces() {
  std::vector<StopPlace*> out;
  for (StopPlace& p : places)
    if ((p.kind & STOP_FLAG_TRAM) != 0) out.push_back(&p);
  return out;
}

StopPlace* Stops::named(const std::string& name, double x, double z) {
  StopPlace* best = nullptr;
  double bd = std::numeric_limits<double>::infinity();
  for (StopPlace& p : places) {
    if (p.name != name) continue;
    const double d = std::hypot(p.x - x, p.z - z);
    if (d < bd) {
      bd = d;
      best = &p;
    }
  }
  return best;
}

}  // namespace fs

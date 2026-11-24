// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/routepresets.ts @ v1.7.7
#include "game/routepresets.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/geom.h"
#include "core/js.h"
#include "world/types.h"

namespace fs {

const RoutePreset* presetOfMode(const std::string& mode) {
  if (!startsWith(mode, "route:")) return nullptr;
  const std::string id = mode.substr(6);
  for (const RoutePreset& p : ROUTE_PRESETS)
    if (p.id == id) return &p;
  return nullptr;
}

namespace {

/** May a set route pass along this way? (no private roads, no yards and car parks, no walking-pace streets) */
bool passable(const RouteGraph& g, size_t w) { return g.len[w] > 0 && (g.kind[w] & (RG_NO_ENTRY | RG_ACCESS_ONLY | RG_MINOR)) == 0; }

/** Is way `w` a road of the kind a place asks for? */
bool fits(const RouteGraph& g, size_t w, const std::string& road) {
  const int c = g.cls[w];
  if (road == "motorway") return c == MOTORWAY;
  if (c < PRIMARY || c > RESIDENTIAL) return false;
  if (road == "rural") return (g.kind[w] & RG_URBAN) == 0;
  return true;
}

/** squared distance of (x, z) from the line of way w (its ends joined straight) */
double dist2(const RouteGraph& g, size_t w, double x, double z) {
  const double ax = g.x0[w];
  const double az = g.z0[w];
  const double dx = static_cast<double>(g.x1[w]) - ax;
  const double dz = static_cast<double>(g.z1[w]) - az;
  const double l2 = dx * dx + dz * dz;
  double t = l2 > 1e-9 ? ((x - ax) * dx + (z - az) * dz) / l2 : 0;
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  const double px = ax + dx * t - x;
  const double pz = az + dz * t - z;
  return px * px + pz * pz;
}

/** what driving along a road of the route a second time costs a leg, in seconds */
const double AGAIN_SECONDS = 25;

double metresOf(const RouteGraph& g, const std::vector<int>& ways) {
  double m = 0;
  for (int w : ways) m += static_cast<double>(g.len[static_cast<size_t>(w)]);
  return m;
}

}  // namespace

std::vector<int> snapPoint(const RouteGraph& g, const RoutePoint& p, const std::vector<uint8_t>& main, double maxDist) {
  std::ptrdiff_t best = -1;
  double bd = maxDist * maxDist;
  for (size_t w = 0; w < static_cast<size_t>(g.n); w++) {
    if (!main[w] || !passable(g, w) || !fits(g, w, p.road)) continue;
    // (a quick look at the box first: most of the map is far away)
    const double x0 = g.x0[w];
    const double x1 = g.x1[w];
    const double z0 = g.z0[w];
    const double z1 = g.z1[w];
    if (std::min(x0, x1) - maxDist > p.x || std::max(x0, x1) + maxDist < p.x) continue;
    if (std::min(z0, z1) - maxDist > p.z || std::max(z0, z1) + maxDist < p.z) continue;
    const double d = dist2(g, w, p.x, p.z);
    if (d < bd) {
      bd = d;
      best = static_cast<std::ptrdiff_t>(w);
    }
  }
  if (best < 0) return {};
  const size_t other = static_cast<size_t>(best) ^ 1;
  if (main[other] && passable(g, other) && fits(g, other, p.road)) return {static_cast<int>(best), static_cast<int>(other)};
  return {static_cast<int>(best)};
}

std::optional<PresetPlan> planPreset(const RouteGraph& g, int start, const RoutePreset& preset) { return planPreset(g, start, preset, mainWays(g)); }

std::optional<PresetPlan> planPreset(const RouteGraph& g, int start, const RoutePreset& preset, const std::vector<uint8_t>& main) {
  const size_t n = preset.points.size();
  if (n < 2) return std::nullopt;
  std::vector<std::vector<int>> options;
  for (const RoutePoint& p : preset.points) options.push_back(snapPoint(g, p, main));
  for (const std::vector<int>& o : options)
    if (o.empty()) return std::nullopt;
  std::vector<int> ways;
  std::vector<int> at;
  int here = start;
  // Roads already on the route cost more: the legs look for other streets rather than go back the
  // way they came.  (On top of what the road events make dearer or close today.)
  std::unordered_set<int> used = {start >> 1};
  const std::function<double(int)> before = routeFor.wayCost;
  routeFor.wayCost = [&before, &used](int w) { return (before ? before(w) : 0) + (used.count(w >> 1) ? AGAIN_SECONDS : 0); };
  /** (whatever happens: the cost of the ways is what it was before) */
  struct Restore {
    const std::function<double(int)>& before;
    ~Restore() { routeFor.wayCost = before; }
  } restore{before};

  // the first place again at the end: a round trip
  for (size_t i = 0; i <= n; i++) {
    std::vector<int> cand = options[i % n];
    const RoutePoint& next = preset.points[(i + 1) % n];
    std::optional<std::vector<int>> best;
    double bestCost = std::numeric_limits<double>::infinity();
    int bestWay = -1;
    if (i == n && !at.empty()) {
      // (back to the way the first place was put on, if that can be)
      std::vector<int> first = {at[0]};
      for (int c : cand)
        if (c != at[0]) first.push_back(c);
      cand = std::move(first);
    }
    for (int w : cand) {
      const std::optional<std::vector<int>> lead = w == here ? std::optional<std::vector<int>>(std::vector<int>{}) : quickest(g, here, w);
      if (!lead) continue;
      // (the way onwards counts as well: which way along the road the place is passed)
      const double onward = i < n ? jsHypot(static_cast<double>(g.x1[static_cast<size_t>(w)]) - next.x, static_cast<double>(g.z1[static_cast<size_t>(w)]) - next.z) : 0;
      const double cost = metresOf(g, *lead) + onward + (i == n && w != at[0] ? 400 : 0);
      if (cost < bestCost) {
        bestCost = cost;
        best = *lead;
        bestWay = w;
      }
    }
    if (!best) return std::nullopt;
    ways.insert(ways.end(), best->begin(), best->end());
    for (int w : *best) used.insert(w >> 1);
    at.push_back(bestWay);
    here = bestWay;
  }
  PresetPlan plan;
  plan.metres = metresOf(g, ways);
  plan.ways = std::move(ways);
  plan.at = std::move(at);
  return plan;
}

std::vector<float> routeLine(const RouteGraph& g, int start, const std::vector<int>& ways, const std::function<std::vector<double>(int e)>& course) {
  std::vector<float> out;
  const auto one = [&](int way) {
    const size_t w = static_cast<size_t>(way);
    const std::vector<double> c = course ? course(way >> 1) : std::vector<double>();
    if (c.size() < 4) {
      out.push_back(g.x0[w]);
      out.push_back(g.z0[w]);
      out.push_back(g.x1[w]);
      out.push_back(g.z1[w]);
      return;
    }
    if ((way & 1) == 0) {
      for (size_t i = 0; i + 1 < c.size(); i += 2) {
        out.push_back(static_cast<float>(c[i]));
        out.push_back(static_cast<float>(c[i + 1]));
      }
    } else {
      for (size_t i = c.size() - 2;; i -= 2) {
        out.push_back(static_cast<float>(c[i]));
        out.push_back(static_cast<float>(c[i + 1]));
        if (i < 2) break;
      }
    }
  };
  one(start);
  for (int w : ways) one(w);
  return out;
}

namespace {
/** metres on a road off the route before it counts as leaving it (a junction's length, a lane change into a slip road) */
const double OFF_METRES = 40;
/** metres along the route the wrong way before it counts (turning round in a street takes a few) */
const double BACK_METRES = 25;
}  // namespace

RouteWatch::RouteWatch(int start, const std::vector<int>& ways) {
  on.insert(start);
  for (int w : ways) on.insert(w);
}

std::optional<RouteFault> RouteWatch::update(int way, double odometer) {
  if (way < 0) return std::nullopt;
  if (on.count(way)) {
    offSince = backSince = -1;
    leftSaid = backSaid = false;
    return std::nullopt;
  }
  if (on.count(way ^ 1)) {
    // on the route, but against its direction
    offSince = -1;
    if (backSince < 0) backSince = odometer;
    if (!backSaid && odometer - backSince > BACK_METRES) {
      backSaid = true;
      return "routeReverse";
    }
    return std::nullopt;
  }
  backSince = -1;
  if (offSince < 0) offSince = odometer;
  if (!leftSaid && odometer - offSince > OFF_METRES) {
    leftSaid = true;
    return "routeLeft";
  }
  return std::nullopt;
}

}  // namespace fs

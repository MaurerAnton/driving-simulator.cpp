// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/narrowing.ts @ v1.7.7
/**
 * Kerb build-outs that narrow a road (OpenStreetMap traffic_calming=choker |
 * chicane on a node of the road): where they stand, how deep they reach in
 * and what they leave free of the carriageway.
 *
 * The map build reads them from the nodes (`buildOutEntries`) and stores them
 * with each road as EdgeJson.bo; it draws the kerbed, planted beds from them
 * (tools/mapgen/streetpaint.ts), and the game finds the same beds again with
 * `narrowingsOf` – the traffic and the autopilot drive through the gap they
 * leave, the player's car runs into their kerbs (src/sim/narrowings.ts).
 *
 * On a narrow two-way street without lane markings a build-out leaves one
 * lane (BUILD_OUT_PASS metres): the traffic of one direction waits for the
 * other – whoever has the build-out on their side lets the oncoming traffic
 * through first (§ 6 StVO).  On a road with painted lanes it keeps clear of
 * the lanes.
 *
 * What the original asks of a road by its shape (`NarrowRoad`), the port asks
 * by a template: an `Edge` will do, and so will any struct with those fields.
 */
#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/js.h"
#include "world/types.h"

namespace fs {

/** How deep a build-out reaches in from the kerb when nothing says (m). */
inline constexpr double BUILD_OUT_DEPTH = 1.6;
/** What a build-out leaves of a narrow unmarked two-way street: one lane to pass at a time (m). */
inline constexpr double BUILD_OUT_PASS = 3.2;
/** numbers per entry of EdgeJson.bo: station of the middle, side (+1 left of a→b, −1 right), length, depth wanted (0: as the road allows), BO flags */
inline constexpr size_t BO_STRIDE = 5;
/** flags of an entry of EdgeJson.bo (BO) */
/** one of a pair (a choker on both sides): the free width is shared, neither side goes first */
inline constexpr int BO_PAIRED = 1;
/** a red-white beacon post on its nose */
inline constexpr int BO_BEACON = 2;
/** not planted */
inline constexpr int BO_BARE = 4;
/** no tree in it */
inline constexpr int BO_NO_TREE = 8;
/** below this free width (m) only one vehicle passes at a time */
inline constexpr double SINGLE_LANE = 5.2;

/** What `buildOutEntries` needs of a node on a road (tools/mapgen/roads.ts EdgeFeature). */
struct BuildOutFeature {
  std::string kind;
  double s = 0;
  std::map<std::string, std::string> tags;
};

/** The build-outs of a road's nodes as EdgeJson.bo entries (stride BO_STRIDE). */
std::vector<double> buildOutEntries(const std::vector<BuildOutFeature>& feats);

/** What the geometry needs of a road (src/world/world.h Edge). */
struct NarrowRoad {
  double width = 0;
  double marginL = 0;
  double marginR = 0;
  double laneW = 0;
  int lanesF = 0;
  int lanesB = 0;
  int flags = 0;
  double len = 0;
  double cutA = 0;
  double cutB = 0;
};

/**
 * How deep a build-out may reach in from the kerb (m).  On a road with
 * painted lanes it stays 1.1 m clear of the middle of the outer lane (as
 * traffic islands do).  A narrow street without lane markings is one where
 * a build-out narrows the carriageway to a single lane – the traffic of one
 * direction waits for the other: there it leaves BUILD_OUT_PASS metres free,
 * shared by the build-outs on both sides (`both`: there is one opposite at
 * the same place).  `side` +1: left of a→b, −1: right.
 */
template <class Road>
double buildOutRoom(const Road& e, int side, bool both) {
  const bool marked = (e.flags & EF_MARKED) != 0;
  if (marked || e.lanesF == 0 || e.lanesB == 0 || e.lanesF + e.lanesB > 2) return (side > 0 ? e.marginL : e.marginR) + e.laneW / 2 - 1.1;
  return jsMax(0, (e.width - BUILD_OUT_PASS) / (both ? 2 : 1));
}

/** One build-out as it stands on its road. */
struct Narrowing {
  /** the stretch of road it takes (stations from a, s0 < s1) */
  double s0 = 0;
  double s1 = 0;
  /** where it was mapped (station) */
  double at = 0;
  /** +1 on the left of a→b, −1 on the right */
  int side = 1;
  /** how far it reaches in from the edge of the carriageway (m) */
  double depth = 0;
  /** the gap it leaves – with any build-out opposite – from `lo` to `hi` (m from the centreline, + left of a→b) */
  double lo = 0;
  double hi = 0;
  /** only one vehicle passes at a time */
  bool single = false;
  /** BO flags */
  int flags = 0;
};

/** The build-outs of a road from its EdgeJson.bo entries (those with no room left out), with the gap each leaves. */
template <class Road>
std::vector<Narrowing> narrowingsOf(const Road& e, const std::vector<double>* bo) {
  std::vector<Narrowing> out;
  if (!bo) return out;
  const double half = e.width / 2;
  const double lo = e.cutA + 1;
  const double hi = e.len - e.cutB - 1;
  for (size_t i = 0; i + BO_STRIDE <= bo->size(); i += BO_STRIDE) {
    const double at = (*bo)[i];
    const int side = (*bo)[i + 1] > 0 ? 1 : -1;
    const double len = (*bo)[i + 2];
    const double want = (*bo)[i + 3];
    const int flags = jsInt32((*bo)[i + 4]);
    const int lanesHere = side > 0 ? (e.lanesB > 0 ? e.lanesB : e.lanesF) : e.lanesF > 0 ? e.lanesF : e.lanesB;
    if (lanesHere == 0) continue;
    const double room = buildOutRoom(e, side, (flags & BO_PAIRED) != 0);
    const double depth = want > 0 ? jsMin(want, room) : jsMin(BUILD_OUT_DEPTH, room);
    if (depth < 0.4) continue;
    const double s0 = jsMax(lo, at - len / 2);
    const double s1 = jsMin(hi, at + len / 2);
    if (s1 - s0 < 1.2) continue;
    Narrowing n;
    n.s0 = s0;
    n.s1 = s1;
    n.at = at;
    n.side = side;
    n.depth = depth;
    n.lo = -half;
    n.hi = half;
    n.single = false;
    n.flags = flags;
    out.push_back(n);
  }
  // the gap: what this one and those opposite it leave
  for (Narrowing& n : out) {
    for (const Narrowing& m : out) {
      if (m.s0 >= n.s1 || n.s0 >= m.s1) continue;
      if (m.side > 0) n.hi = jsMin(n.hi, half - m.depth);
      else n.lo = jsMax(n.lo, -half + m.depth);
    }
    n.single = n.hi - n.lo < SINGLE_LANE;
  }
  return out;
}

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/routegraph.ts @ v1.7.7
/**
 * The road network as a route planner sees it: every road in each direction
 * it can be driven in is a "way", and from the end of each way lead the turns
 * the junction allows.  No lanes, no geometry – a few numbers per way, held
 * in plain arrays, so that the network of a whole region fits into memory
 * and can be searched quickly.
 *
 * It is derived from the lane network (`routeGraphOf`); the routes found on
 * it are lists of ways, which the navigation turns back into lanes and the
 * paths through the junctions (`connectorsAlong` in game/nav.ts).
 *
 * (The arrays of 32-bit floats keep their numbers as floats, as the original's
 * Float32Arrays do: calculate with them as doubles – `double(g.len[w])`.)
 */
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace fs {

struct Lane;
class LaneGraph;

/** Properties of a way (bits of RouteGraph.kind) (RG). */
enum RG : int {
  /** "Anlieger frei", a car park, a yard: to get somewhere, not to drive through */
  RG_ACCESS_ONLY = 1,
  /** private, or for farm and forest traffic: not to be driven at all */
  RG_NO_ENTRY = 2,
  /** a driveway, a track, a traffic-calmed street: walking pace, or not a street at all */
  RG_MINOR = 4,
  /** it ends where the map ends */
  RG_TO_MAP_END = 8,
  /** in a built-up area */
  RG_URBAN = 16,
  /** part of a roundabout */
  RG_ROUNDABOUT = 32,
  /** too narrow for two cars to pass each other */
  RG_NARROW = 64,
};

struct RouteGraph {
  /** number of ways: one for each road and direction – way = edge * 2 + (0: from node a to b, 1: from b to a) */
  int n = 0;
  /** length in metres; 0: the road cannot be driven in this direction */
  std::vector<float> len;
  /** seconds it takes at the speed allowed (at most 130 km/h) */
  std::vector<float> secs;
  /** RG bits */
  std::vector<uint8_t> kind;
  /** road class (CLS) */
  std::vector<uint8_t> cls;
  /** where the way begins and ends (x, z) */
  std::vector<float> x0;
  std::vector<float> z0;
  std::vector<float> x1;
  std::vector<float> z1;
  /** the turns at the end of way w: entries first[w] … first[w + 1] - 1 of `to` (the way turned into), `turn` (TURN) and `across` (metres through the junction) */
  std::vector<uint32_t> first;
  std::vector<uint32_t> to;
  std::vector<uint8_t> turn;
  std::vector<float> across;
  /** the same turns sorted by where they lead: the ways `from[k]` turn into way w for k = firstIn[w] … firstIn[w + 1] - 1 (`inTurn`: index into `to`) */
  std::vector<uint32_t> firstIn;
  std::vector<uint32_t> from;
  std::vector<uint32_t> inTurn;
};

/** The way a lane belongs to. */
int wayOf(const Lane& lane);

RouteGraph routeGraphOf(const LaneGraph& lanes);

/** A binary heap of (id, key) pairs, smallest key first. */
class MinHeap {
 public:
  size_t size = 0;

  explicit MinHeap(size_t capacity = 1024) : ids_(capacity), keys_(capacity) {}

  void clear() { size = 0; }

  void push(uint32_t id, double key) {
    if (size == ids_.size()) {
      ids_.resize(size * 2);
      keys_.resize(size * 2);
    }
    size_t i = size++;
    while (i > 0) {
      const size_t p = (i - 1) >> 1;
      if (keys_[p] <= key) break;
      ids_[i] = ids_[p];
      keys_[i] = keys_[p];
      i = p;
    }
    ids_[i] = id;
    keys_[i] = key;
  }

  /** the key of the smallest pair (read before pop) */
  double topKey() const { return keys_[0]; }

  /** Remove the smallest pair and return its id. */
  uint32_t pop() {
    const uint32_t top = ids_[0];
    const size_t n = --size;
    if (n > 0) {
      const uint32_t id = ids_[n];
      const double key = keys_[n];
      size_t i = 0;
      for (;;) {
        size_t c = i * 2 + 1;
        if (c >= n) break;
        if (c + 1 < n && keys_[c + 1] < keys_[c]) c++;
        if (keys_[c] >= key) break;
        ids_[i] = ids_[c];
        keys_[i] = keys_[c];
        i = c;
      }
      ids_[i] = id;
      keys_[i] = key;
    }
    return top;
  }

 private:
  std::vector<uint32_t> ids_;
  std::vector<double> keys_;
};

/**
 * The quickest route from the end of way `start` to way `goal`: the ways to
 * drive along after `start`, `goal` last – or null when there is none.
 * Private roads are not used (the goal itself may be one); "Anlieger frei"
 * and car parks are no short cuts.
 */
std::optional<std::vector<int>> quickest(const RouteGraph& g, int start, int goal);

/** The same to a road, whichever way it is then driven along: the quicker of the two. */
std::optional<std::vector<int>> quickestToEdge(const RouteGraph& g, int start, int edge);

/** A route with the seconds it takes (what `quickest` works with; the comparison with the original prints the seconds). */
struct TimedRoute {
  std::vector<int> ways;
  double secs = 0;
};
std::optional<TimedRoute> quickestTimed(const RouteGraph& g, int start, int goal);

/**
 * What the ways are planned for: a vehicle that may not use the motorway (a tractor) is led round it;
 * `wayCost` adds seconds to a way – Infinity where it is closed today (sim/roadevents.ts); none: nothing.
 */
struct RouteFor {
  bool noMotorway = false;
  std::function<double(int way)> wayCost;
};
extern RouteFor routeFor;

/**
 * The ways of the network one can drive around in: from each of them every
 * other can be reached and it from them, by public roads.  (A village joined
 * to the rest by a farm track only, a yard behind a gate, are not part of it.)
 */
std::vector<uint8_t> mainWays(const RouteGraph& g);

}  // namespace fs

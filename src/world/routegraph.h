// SPDX-License-Identifier: AGPL-3.0-only
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
 * Origin: src/world/routegraph.ts (MaurerAnton/driving-simulator @ 3420ad0,
 * v0.8.0).
 */
#pragma once

#include <cstdint>
#include <vector>

namespace fs {

struct Lane;
class LaneGraph;

/** Properties of a way (bits of RouteGraph::kind). */
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
  /** less than 2.4 m wide */
  RG_NARROW = 64,
};

/** A binary heap of (id, key) pairs, smallest key first (the JS MinHeap). */
class MinHeap {
 public:
  size_t size() const { return ids_.size(); }
  void clear() {
    ids_.clear();
    keys_.clear();
  }
  void push(int id, double key) {
    ids_.push_back(id);
    keys_.push_back(key);
    size_t i = ids_.size() - 1;
    while (i > 0) {
      size_t p = (i - 1) >> 1;
      if (keys_[p] <= key) break;
      ids_[i] = ids_[p];
      keys_[i] = keys_[p];
      i = p;
    }
    ids_[i] = id;
    keys_[i] = key;
  }
  /** the key of the smallest pair (read before pop) */
  double topKey() const { return keys_.empty() ? 0 : keys_[0]; }
  /** Remove the smallest pair and return its id. */
  int pop() {
    const int top = ids_[0];
    int lastId = ids_.back();
    double lastKey = keys_.back();
    ids_.pop_back();
    keys_.pop_back();
    size_t n = ids_.size();
    if (n > 0) {
      size_t i = 0;
      for (;;) {
        size_t c = i * 2 + 1;
        if (c >= n) break;
        if (c + 1 < n && keys_[c + 1] < keys_[c]) c++;
        if (keys_[c] >= lastKey) break;
        ids_[i] = ids_[c];
        keys_[i] = keys_[c];
        i = c;
      }
      ids_[i] = lastId;
      keys_[i] = lastKey;
    }
    return top;
  }

 private:
  std::vector<int> ids_;
  std::vector<double> keys_;
};

/**
 * The network as plain arrays: `n` ways (road * 2 + direction), the turns at
 * the end of each way (`first` … `first[w+1]-1` of `to`/`turn`/`across`), and
 * the same turns sorted by where they lead (`firstIn`/`from`/`inTurn`).
 */
struct RouteGraph {
  int n = 0;
  /** metres (at most 0.5: a way that cannot be driven) */
  std::vector<double> len;
  /** seconds it takes at the speed allowed (at most 130 km/h) */
  std::vector<double> secs;
  /** RG bits */
  std::vector<uint8_t> kind;
  /** road class (CLS) */
  std::vector<uint8_t> cls;
  /** where the way begins and ends (x, z) */
  std::vector<double> x0, z0, x1, z1;
  std::vector<uint32_t> first;
  std::vector<uint32_t> to;
  std::vector<uint8_t> turn;
  std::vector<double> across;
  std::vector<uint32_t> firstIn;
  std::vector<uint32_t> from;
  std::vector<uint32_t> inTurn;
};

/** The way a lane belongs to: its road, and which way it is driven along. */
int wayOfLane(const Lane& lane);

/** The network of the roads of the map, from its lanes. */
RouteGraph routeGraphOf(const LaneGraph& lanes);

/** The same, from the whole map's network file (netfile.h). */
struct NetData;
RouteGraph routeGraphOfNet(const NetData& d);

/** What a turn costs on top of the time it takes: seconds. */
double turnSeconds(const RouteGraph& g, int w, int k);

/**
 * The quickest route from the end of way `start` to way `goal`: the ways to
 * drive along after `start`, `goal` last – or null when there is none.
 * Private roads are not used (the goal itself may be one); "Anlieger frei"
 * and car parks are no short cuts.
 */
std::vector<int> quickest(const RouteGraph& g, int start, int goal);

/** The same to a road, whichever way it is then driven along: the quicker of the two. */
std::vector<int> quickestToEdge(const RouteGraph& g, int start, int edge);

/** The same, with the seconds it takes (0 ways when there is none). */
bool quickestTimed(const RouteGraph& g, int start, int goal, std::vector<int>& ways, double& secs);

/**
 * The ways of the network one can drive around in: from each of them every
 * other can be reached and it from them, by public roads.  (A village joined
 * to the rest by a farm track only, a yard behind a gate, are not part of it.)
 */
std::vector<uint8_t> mainWays(const RouteGraph& g);

}  // namespace fs

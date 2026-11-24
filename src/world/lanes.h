// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/lanes.ts @ v1.7.7
/**
 * Lane-level network derived from the road graph: driving lanes on every
 * edge, connectors through every junction, who has to give way to whom, and
 * shortest-path routing.  Used by the traffic simulation, the rule monitor
 * and the navigation hints.
 *
 * Who owns what: the `LaneGraph` owns its lanes and the paths through the
 * junctions (connectors); each keeps its address for as long as it is part of
 * the graph.  What is taken out again (`removeEdge`, `removeNode`) stays valid
 * until the next piece of the map is taken out of the world.
 */
#pragma once

#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/geom.h"
#include "world/sparse.h"
#include "world/types.h"

namespace fs {

class World;
class LaneGraph;
struct Edge;
struct Node;
struct Arm;
struct Connector;

/** TURN */
enum Turn : int { TURN_STRAIGHT = 0, TURN_LEFT = 1, TURN_RIGHT = 2, TURN_UTURN = 3 };

struct Lane {
  int id = 0;
  Edge* edge = nullptr;
  /** +1 travels a→b, -1 travels b→a */
  int dir = 1;
  /** 0 = right-most lane of its direction */
  int idx = 0;
  /** number of lanes in this direction */
  int count = 0;
  /** lateral offset of the lane centre from the edge centreline (+ = left of a→b) */
  double off = 0;
  double width = 0;
  /** edge stations where the lane starts and ends (in travel order) */
  double s0 = 0;
  double s1 = 0;
  double len = 0;
  Node* from = nullptr;
  Node* to = nullptr;
  /** arm of `to` this lane arrives on / arm of `from` it leaves by */
  Arm* armTo = nullptr;
  Arm* armFrom = nullptr;
  std::vector<Connector*> out;
  std::vector<Connector*> in;
  /** speed limit in m/s (Infinity = none) */
  double limit = std::numeric_limits<double>::infinity();
  /** distance from the lane start to the stop/yield line before `to` */
  double stopAt = 0;
};

struct Conflict {
  Connector* other = nullptr;
  /** distance along this connector / the other one to the conflict point */
  double sSelf = 0;
  double sOther = 0;
  bool merge = false;
  /** this connector has to give way to `other` */
  bool yield = false;
};

struct Sibling {
  Connector* other = nullptr;
  double until = 0;
  double untilOther = 0;
};

/**
 * A path through a junction.  Which other paths it conflicts with is costly
 * to find (every pair of paths of the junction is compared) and only matters
 * where traffic actually is – so it is worked out for a junction when one of
 * its paths is first asked, not for the whole map at the start.
 *
 * (`conflicts` and `siblings` are read like the lists they are in the
 * original – `for (const Conflict& k : c->conflicts)`, `c->conflicts.empty()` –
 * and work the junction out when they are first looked at.)
 */
struct Connector {
  int id = 0;
  Node* node = nullptr;
  Lane* from = nullptr;
  Lane* to = nullptr;
  int turn = TURN_STRAIGHT;
  Polyline3 pl;
  double len = 0;

  /** A list that is worked out when it is first looked at. */
  template <class T>
  class Found {
   public:
    const std::vector<T>& get() const {
      self_->resolve();
      return self_->*list_;
    }
    operator const std::vector<T>&() const { return get(); }
    auto begin() const { return get().begin(); }
    auto end() const { return get().end(); }
    size_t size() const { return get().size(); }
    bool empty() const { return get().empty(); }
    const T& operator[](size_t i) const { return get()[i]; }

   private:
    friend struct Connector;
    Found(const Connector* self, std::vector<T> Connector::* list) : self_(self), list_(list) {}
    const Connector* self_;
    std::vector<T> Connector::* list_;
  };

  /** the paths through the junction this one crosses or merges with (worked out when first asked for) */
  const Found<Conflict> conflicts{this, &Connector::foundConflicts};
  /** connectors leaving the same lane, with the stations up to which the two paths overlap */
  const Found<Sibling> siblings{this, &Connector::foundSiblings};

  /** filled by LaneGraph::resolveNode */
  std::vector<Conflict> foundConflicts;
  std::vector<Sibling> foundSiblings;

  Connector() = default;
  Connector(const Connector&) = delete;
  Connector& operator=(const Connector&) = delete;

 private:
  friend class LaneGraph;
  LaneGraph* graph_ = nullptr;
  /** where it stands in LaneGraph::connectors */
  size_t slot_ = 0;
  void resolve() const;
};

/** A point on the centre line of a lane (or of a path through a junction) and how far it is from what was asked about. */
struct PathFoot {
  double x = 0;
  double z = 0;
  double d = 0;
};

/** Position on a lane at distance d from its start. */
PolySample& lanePoint(const Lane& lane, double d, PolySample& out);

class LaneGraph {
 public:
  /** the lanes of each edge: [forward lanes right→left, backward lanes right→left] */
  struct EdgeLanes {
    std::vector<Lane*> fwd;
    std::vector<Lane*> back;
  };

  World& world;
  /** by their number – with gaps once parts of the network were taken away again */
  Sparse<Lane> lanes;
  std::vector<Connector*> connectors;
  /** lanes of each edge (by the number of the edge) */
  Sparse<EdgeLanes> edgeLanes;

  /** The lanes of everything the world holds; what is added to the world later is added here with `addEdge` and `completeNode`. */
  explicit LaneGraph(World& world);
  LaneGraph(const LaneGraph&) = delete;
  LaneGraph& operator=(const LaneGraph&) = delete;

  /** The lanes of a road that was added to the world. */
  void addEdge(Edge& e);

  /** A road was taken out of the world: its lanes go (the paths at its ends went with their junctions). */
  void removeEdge(Edge& e);

  /** A junction is no longer whole (its piece of the map was taken away): nothing leads through it any more. */
  void removeNode(Node& node);

  /** Lanes arriving at / leaving the node on an arm. */
  const std::vector<Lane*>& lanesInto(const Arm& arm) const;
  const std::vector<Lane*>& lanesOutOf(const Arm& arm) const;

  /**
   * A junction is whole (all its roads have their lanes): the paths through
   * it, and where the lanes that arrive at it have to stop.
   */
  void completeNode(Node& node);

  /** Work out the conflicts of every junction now (tools and tests; the game leaves it to resolveNode). */
  void resolveAll();

  /** Compare the paths through a junction with each other: where do they cross, merge or share their way, and who yields? */
  void resolveNode(int nodeId);

  /**
   * The nearest centre line of a lane, or of a path through a junction, to a
   * point – on the same level (`y`; NaN: on any).  Returns the distance and
   * writes the foot point to `out`; Infinity when none is closer than `reach`.
   * What stands closer to such a line than a vehicle is wide is in the way.
   */
  double nearestPath(double x, double z, double y, double reach, PathFoot& out);

  /** Does a wall from (x1, z1) to (x2, z2) come closer than `need` to the middle of a lane (on the level `y`; NaN: on any)? */
  bool wallInLane(double x1, double z1, double x2, double z2, double y, double need);

  /**
   * Where a post (a sign, a lamp) may stand: the place itself when it keeps
   * `need` metres from the middle of every lane, otherwise the nearest place
   * beside the lanes within a few metres – or null when there is none.
   */
  PathFoot* besideLanes(double x, double z, double y, double need, PathFoot& out);

  /** The paths through a junction. */
  const std::vector<Connector*>& pathsThrough(const Node& node) const;

  /** May ordinary traffic use this edge in this direction? */
  static bool drivable(const Edge& e, bool forTraffic);

  /** ways are looked for for a truck with a trailer: turns that double back within a few metres are avoided */
  bool longVehicle = false;
  /** the vehicle may not use the motorway (a tractor): ways do not lead over it */
  bool noMotorway = false;
  /** what a way (edge * 2 + 0 a → b, 1 b → a) costs on top (s): Infinity where it is closed today (sim/roadevents.ts) – null: nothing */
  std::function<double(int way)> wayCost;

  /**
   * Shortest route (by travel time) from the end of lane `start` to any lane
   * of `goal` (`goalDir`: only one that runs a→b (1) or b→a (-1)).  Returns
   * the connectors to take, or null – here: none (a route has at least one).
   * `forTraffic` restricts the search to ordinary public roads.
   */
  std::vector<Connector*> route(const Lane& start, const Edge& goal, bool forTraffic = false, int goalDir = 0);

 private:
  /** the paths through each junction, and which junctions have their conflicts worked out */
  std::unordered_map<int, std::vector<Connector*>> pathsAt_;
  std::unordered_set<int> resolved_;
  int nextLane_ = 0;
  int nextPath_ = 0;
  /** the paths themselves */
  std::unordered_map<const Connector*, std::unique_ptr<Connector>> paths_;
  /** what was taken out with the last piece of the map that went (kept until the next one goes) */
  std::vector<std::unique_ptr<Lane>> goneLanes_;
  std::vector<std::unique_ptr<Connector>> gonePaths_;
  std::vector<std::unique_ptr<EdgeLanes>> goneEdgeLanes_;
  uint64_t goneWith_ = 0;

  void dropPath(Connector* c);
  void addConnector(Node& node, Lane& from, Lane& to, int turn);
  /** Must connector `c1` give way to `c2`? */
  bool yields(const Connector& c1, const Connector& c2, bool merge) const;
  /** (what went with an earlier piece of the map is given back to the memory) */
  void letGo();
};

}  // namespace fs

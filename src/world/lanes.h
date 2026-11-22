// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Lane graph: per-direction lanes along the edges, connectors between them at
 * junctions, the conflicts between those connectors, and the routing the
 * navigator and the AI drivers use.
 *
 * Origin: src/world/lanes.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#pragma once

#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/geom.h"
#include "world/types.h"

namespace fs {

class World;
struct Edge;
struct Node;
struct Arm;

/** Movement type of a connector (turn:lanes). */
enum Turn : int { TURN_STRAIGHT = 0, TURN_LEFT = 1, TURN_RIGHT = 2, TURN_UTURN = 3 };

/** A lane along one direction of an edge. */
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
  double s0 = 0, s1 = 0;
  double len = 0;
  Node* from = nullptr;
  Node* to = nullptr;
  /** arm of `to` this lane arrives on / arm of `from` it leaves by */
  Arm* armTo = nullptr;
  Arm* armFrom = nullptr;
  std::vector<struct Connector*> out;
  std::vector<struct Connector*> in;
  /** speed limit in m/s (infinity = none) */
  double limit = std::numeric_limits<double>::infinity();
  /** distance from the lane start to the stop/yield line before `to` */
  double stopAt = 0;
};

/** A legal move from one lane to another across a junction. */
struct Connector {
  int id = 0;
  Node* node = nullptr;
  Lane* from = nullptr;
  Lane* to = nullptr;
  /** TURN_* */
  int turn = TURN_STRAIGHT;
  Polyline3 pl;
  double len = 0;
  std::vector<struct Conflict> conflicts;
  /** connectors leaving the same lane, with the stations up to which the two
   *  paths overlap */
  std::vector<struct Sibling> siblings;
};

/** Where two connectors' paths meet, and who gives way. */
struct Conflict {
  Connector* other = nullptr;
  /** distance along this connector / the other one to the conflict point */
  double sSelf = 0, sOther = 0;
  bool merge = false;
  /** this connector has to give way to `other` */
  bool yield = false;
  bool operator==(const Conflict& o) const {
    return other == o.other && sSelf == o.sSelf && sOther == o.sOther && merge == o.merge &&
           yield == o.yield;
  }
};

struct Sibling {
  Connector* other = nullptr;
  double until = 0, untilOther = 0;
  bool operator==(const Sibling& o) const {
    return other == o.other && until == o.until && untilOther == o.untilOther;
  }
};

class LaneGraph {
 public:
  explicit LaneGraph(World& world);

  World& world;
  std::vector<Lane> lanes;
  std::vector<Connector> connectors;
  /** lanes of each edge: [forward lanes right→left, backward lanes right→left] */
  struct EdgeLanes {
    std::vector<Lane*> fwd;
    std::vector<Lane*> back;
  };
  std::vector<EdgeLanes> edgeLanes;

  /** Work out the conflicts of every junction now (tools and tests; the game leaves it to resolveNode). */
  void resolveAll();
  /**
   * Compare the paths through a junction with each other: where do they cross,
   * merge or share their way, and who yields?  Costs every pair of paths of
   * the junction, so it runs once per junction, when one of its paths is
   * first asked (the original: the JunctionPath getters).
   */
  void resolveNode(int nodeId);

  /** Lanes arriving at / leaving the node on an arm. */
  std::vector<Lane*> lanesInto(const Arm& arm);
  std::vector<Lane*> lanesOutOf(const Arm& arm);

  /** May ordinary traffic use this edge in this direction? */
  static bool drivable(const Edge& e, bool forTraffic);

  /**
   * Shortest route (by travel time) from the end of lane `start` to any lane
   * of `goal`.  Returns the connectors to take, or an empty vector when
   * there is none.  `forTraffic` restricts the search to ordinary public roads.
   */
  std::vector<Connector*> route(const Lane& start, const Edge& goal, bool forTraffic = false);

 private:
  void buildLanes();
  void buildConnectors();
  void addConnector(Node& node, Lane& from, Lane& to, int turn);
  bool yields(const Connector& c1, const Connector& c2, bool merge) const;

  /** the paths through each junction, and which junctions have their conflicts worked out */
  std::map<int, std::vector<int>> pathsAt_;   // node id → connector ids
  std::set<int> resolved_;
};

/** Position on a lane at distance d from its start. */
PolySample lanePoint(const Lane& lane, double d, PolySample out);

}  // namespace fs

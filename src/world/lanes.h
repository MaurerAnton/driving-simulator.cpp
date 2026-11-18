// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Lane graph: per-direction lanes along the edges, connectors between them at
 * junctions, and the routing (A*) the navigator and the AI drivers use.
 *
 * Origin: src/world/lanes.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#pragma once

#include <string>
#include <vector>

#include "core/geom.h"
#include "world/types.h"

namespace fs {

class World;
struct Edge;
struct Node;

/** Movement type of a connector (turn:lanes). */
enum Turn : int { TURN_STRAIGHT = 0, TURN_LEFT = 1, TURN_RIGHT = 2, TURN_UTURN = 3 };

/** A lane along one direction of an edge. */
struct Lane {
  int id = 0;
  Edge* edge = nullptr;
  /** +1 = along a→b, -1 = along b→a */
  int dir = 1;
  /** 0-based index of the lane across the carriageway, counted from the right
   *  when facing travel direction */
  int idx = 0;
  /** lateral offset of the lane centre from the edge centreline (m, left = +) */
  double off = 0;
  /** width of the lane (m) */
  double width = 0;
  Polyline3 pl;
  double len = 0;
  /** speed limit in km/h (0 = class default) */
  int maxKmh = 0;
  /** surface (SURF) */
  int surf = 0;
  /** edge flag bits this lane inherits */
  int flags = 0;
  /** true when traffic may not enter from the far end (one-way) */
  bool noEntry = false;
  /** outgoing connectors (from this lane) */
  std::vector<struct Connector*> out;
};

/** A legal move from one lane to another across a junction. */
struct Connector {
  int id = 0;
  Lane* from = nullptr;
  Lane* to = nullptr;
  /** junction node this connector crosses */
  Node* node = nullptr;
  /** TURN_* */
  int turn = TURN_STRAIGHT;
  /** false when the move is forbidden (banned movement, wrong direction) */
  bool allowed = true;
  /** metres travelled through the junction */
  double len = 0;
  /** stop/yield line distance from the node on the approach (-1 = none) */
  double stopS = -1;
  /** approach role (ROLE) for traffic arriving on `from` */
  int role = ROLE_NONE;
  /** signal group index on the node's controller, or -1 */
  int sg = -1;
  /** traffic light state for this connector's group (updated by SignalSystem) */
  int light = 0;
  /** right-of-way: how much distance is needed past the junction */
  double clearance = 0;
};

/** A conflict between two connectors at the same junction. */
struct Conflict {
  Connector* a = nullptr;
  Connector* b = nullptr;
};

class LaneGraph {
 public:
  explicit LaneGraph(World& world);

  /** All lanes, in creation order. */
  const std::vector<Lane>& lanes() const { return lanes_; }
  const std::vector<Connector>& connectors() const { return connectors_; }

  /** Lane with the given id (nullptr when out of range). */
  Lane* lane(int id) { return id >= 0 && static_cast<size_t>(id) < lanes_.size() ? &lanes_[static_cast<size_t>(id)] : nullptr; }
  Connector* connector(int id) { return id >= 0 && static_cast<size_t>(id) < connectors_.size() ? &connectors_[static_cast<size_t>(id)] : nullptr; }

  /** Lanes of an edge (both directions, forward lanes first). */
  std::vector<Lane*> lanesOf(const Edge& e);

  /** Position/tangent along a lane at distance d. */
  static PolySample lanePoint(const Lane& lane, double d, PolySample out);

  /**
   * Shortest route from `from` to `goal`, as the connectors to follow.
   * Returns false when there is no route (as the JS `route` does).
   */
  bool route(const Lane& from, const Edge* goal, bool allowUTurn,
             std::vector<Connector*>& out);

  /** Nearest lane to (x, z) on the given edge, or nullptr. */
  Lane* nearestLane(const Edge& e, double x, double z, double* outOffset = nullptr);

 private:
  World& world_;
  std::vector<Lane> lanes_;
  std::vector<Connector> connectors_;

  void build();
};

}  // namespace fs

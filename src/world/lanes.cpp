// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Lane graph: per-direction lanes along the edges, connectors between them at
 * junctions, and the routing (A*) the navigator and the AI drivers use.
 *
 * Origin: src/world/lanes.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#include "world/lanes.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <set>

#include "world/world.h"

namespace fs {

namespace {
/** Turn class from a turn:lanes cell (OSM left→right, as written). */
int turnFromCell(char c) {
  switch (c) {
    case 'L': return TURN_LEFT;
    case 'R': return TURN_RIGHT;
    case 'T': return TURN_STRAIGHT;
    case 'U': return TURN_UTURN;
    default: return TURN_STRAIGHT;  // '' or ';' separators mean "straight ahead"
  }
}
}  // namespace

LaneGraph::LaneGraph(World& world) : world_(world) { build(); }

PolySample LaneGraph::lanePoint(const Lane& lane, double d, PolySample out) {
  return samplePolyline(lane.pl, d, out);
}

std::vector<Lane*> LaneGraph::lanesOf(const Edge& e) {
  std::vector<Lane*> out;
  for (auto& l : lanes_)
    if (l.edge == &e) out.push_back(&l);
  return out;
}

Lane* LaneGraph::nearestLane(const Edge& e, double x, double z, double* outOffset) {
  Lane* best = nullptr;
  double bestD = 1e300;
  for (auto& l : lanes_) {
    if (l.edge != &e) continue;
    PolyProjection p = projectOnPolyline(l.pl, x, z, PolyProjection());
    if (p.d2 < bestD) {
      bestD = p.d2;
      best = &l;
      if (outOffset) *outOffset = p.off;
    }
  }
  return best;
}

void LaneGraph::build() {
  auto& edges = world_.edges();

  // --- lanes along every edge -------------------------------------------
  for (auto& e : edges) {
    double laneW = e.laneW > 0 ? e.laneW : CLASS_LANE_WIDTH[e.cls];
    // forward lanes (dir = +1), right to left when facing travel direction
    for (int i = 0; i < e.lanesF; i++) {
      Lane lane;
      lane.id = static_cast<int>(lanes_.size());
      lane.edge = &e;
      lane.dir = 1;
      lane.idx = i;
      lane.off = e.width / 2 - laneW * (i + 0.5);
      lane.width = laneW;
      lane.pl = e.pl;
      lane.len = e.len;
      lane.maxKmh = static_cast<int>(e.maxF);
      lane.surf = e.surf;
      lane.flags = e.flags;
      lane.noEntry = false;
      lanes_.push_back(lane);
    }
    // backward lanes (dir = -1), mirrored across the centreline
    for (int i = 0; i < e.lanesB; i++) {
      Lane lane;
      lane.id = static_cast<int>(lanes_.size());
      lane.edge = &e;
      lane.dir = -1;
      lane.idx = i;
      lane.off = -(e.width / 2 - laneW * (i + 0.5));
      lane.width = laneW;
      lane.pl = e.pl;
      lane.len = e.len;
      lane.maxKmh = static_cast<int>(e.maxB);
      lane.surf = e.surf;
      lane.flags = e.flags;
      lane.noEntry = false;
      lanes_.push_back(lane);
    }
  }

  // --- connectors at every junction --------------------------------------
  for (auto& node : world_.nodes()) {
    std::vector<Lane*> incoming, outgoing;
    for (auto& arm : node.arms) {
      for (auto& l : lanes_) {
        if (l.edge != arm.edge) continue;
        // arriving on this arm: the lane whose direction points at the node
        bool arrives = arm.out ? l.dir == -1 : l.dir == 1;
        bool leaves = arm.out ? l.dir == 1 : l.dir == -1;
        if (arrives && arm.isApproach) incoming.push_back(&l);
        if (leaves && arm.isExit) outgoing.push_back(&l);
      }
    }
    if (incoming.empty() || outgoing.empty()) continue;

    for (Lane* from : incoming) {
      // the arm the vehicle arrives on (for stop line / role / signal group)
      const Arm* approach = nullptr;
      for (auto& arm : node.arms) {
        if (arm.edge != from->edge) continue;
        bool arrives = arm.out ? from->dir == -1 : from->dir == 1;
        if (arrives) {
          approach = &arm;
          break;
        }
      }
      for (Lane* to : outgoing) {
        if (to->edge == from->edge && from->dir == to->dir) continue;  // no U-turn by default
        if (world_.isBanned(from->edge->id, node.id, to->edge->id)) continue;
        Connector c;
        c.id = static_cast<int>(connectors_.size());
        c.from = from;
        c.to = to;
        c.node = &node;
        c.turn = TURN_STRAIGHT;
        if (approach) {
          c.role = approach->role;
          c.sg = approach->sg;
          c.stopS = approach->stopS;
        }
        // turn class: compare the two lane directions through the node
        double ax = 0, az = 0, bx = 0, bz = 0;
        if (from->dir == 1) {
          ax = from->edge->pl.pts.size() >= 6 ? from->edge->pl.pts[3] - from->edge->pl.pts[0] : 1;
          az = from->edge->pl.pts.size() >= 6 ? from->edge->pl.pts[5] - from->edge->pl.pts[2] : 0;
        } else {
          ax = from->edge->pl.pts.size() >= 6 ? from->edge->pl.pts[0] - from->edge->pl.pts[3] : -1;
          az = from->edge->pl.pts.size() >= 6 ? from->edge->pl.pts[2] - from->edge->pl.pts[5] : 0;
        }
        if (to->dir == 1) {
          bx = to->edge->pl.pts.size() >= 6 ? to->edge->pl.pts[3] - to->edge->pl.pts[0] : 1;
          bz = to->edge->pl.pts.size() >= 6 ? to->edge->pl.pts[5] - to->edge->pl.pts[2] : 0;
        } else {
          bx = to->edge->pl.pts.size() >= 6 ? to->edge->pl.pts[0] - to->edge->pl.pts[3] : -1;
          bz = to->edge->pl.pts.size() >= 6 ? to->edge->pl.pts[2] - to->edge->pl.pts[5] : 0;
        }
        double la = std::hypot(ax, az);
        double lb = std::hypot(bx, bz);
        if (la > 1e-9 && lb > 1e-9) {
          double cross = (ax * bz - az * bx) / (la * lb);
          double dot = (ax * bx + az * bz) / (la * lb);
          if (cross > 0.35) c.turn = TURN_LEFT;
          else if (cross < -0.35) c.turn = TURN_RIGHT;
          else if (dot < -0.5) c.turn = TURN_UTURN;
        }
        // the length travelled inside the junction
        c.len = std::max(1.0, from->edge->width / 2 + to->edge->width / 2);
        connectors_.push_back(c);
      }
    }
  }

  // link the connectors into the lanes' outgoing lists
  for (auto& c : connectors_) c.from->out.push_back(&c);
}

bool LaneGraph::route(const Lane& from, const Edge* goal, bool allowUTurn,
                      std::vector<Connector*>& out) {
  out.clear();
  if (!goal) return false;
  if (from.edge == goal) return true;

  // Dijkstra over lanes: cost = metres of travel plus a junction penalty
  struct Entry {
    double cost;
    int lane;
  };
  struct Cmp {
    bool operator()(const Entry& a, const Entry& b) const { return a.cost > b.cost; }
  };
  std::priority_queue<Entry, std::vector<Entry>, Cmp> pq;
  std::vector<double> dist(lanes_.size(), 1e300);
  std::vector<int> prev(lanes_.size(), -1);       // connector id that led here
  std::vector<int> prevLane(lanes_.size(), -1);

  dist[static_cast<size_t>(from.id)] = 0;
  pq.push({0, from.id});
  bool reached = false;
  int endLane = -1;

  while (!pq.empty()) {
    Entry e = pq.top();
    pq.pop();
    if (e.cost > dist[static_cast<size_t>(e.lane)] + 1e-9) continue;
    Lane* lane = &lanes_[static_cast<size_t>(e.lane)];
    if (lane->edge == goal) {
      reached = true;
      endLane = e.lane;
      break;
    }
    for (Connector* c : lane->out) {
      if (!c->allowed) continue;
      if (!allowUTurn && c->turn == TURN_UTURN) continue;
      double next = e.cost + c->to->len + c->len + (c->turn == TURN_STRAIGHT ? 0 : 8);
      if (next < dist[static_cast<size_t>(c->to->id)] - 1e-9) {
        dist[static_cast<size_t>(c->to->id)] = next;
        prev[static_cast<size_t>(c->to->id)] = c->id;
        prevLane[static_cast<size_t>(c->to->id)] = e.lane;
        pq.push({next, c->to->id});
      }
    }
  }
  if (!reached) return false;

  // walk back to the start
  std::vector<Connector*> rev;
  int cur = endLane;
  while (prev[static_cast<size_t>(cur)] >= 0) {
    Connector* c = &connectors_[static_cast<size_t>(prev[static_cast<size_t>(cur)])];
    rev.push_back(c);
    cur = prevLane[static_cast<size_t>(cur)];
  }
  out.assign(rev.rbegin(), rev.rend());
  return true;
}

}  // namespace fs

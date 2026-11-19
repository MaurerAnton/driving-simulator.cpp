// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Lane graph: lanes, connectors, conflicts and routing.
 *
 * Origin: src/world/lanes.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * The construction mirrors the JavaScript buildLanes/buildConnectors/
 * buildConflicts, including the turn classification and the right-of-way
 * rules they encode.
 */
#include "world/lanes.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <queue>

#include "world/world.h"

namespace fs {

namespace {
PolySample gTmpA, gTmpB;

int rank(const Arm& arm) {
  switch (arm.role) {
    case ROLE_PRIORITY: return 3;
    case ROLE_RBL: return 2;
    case ROLE_YIELD:
    case ROLE_STOP: return 1;
    default: return 0;
  }
}

/** OSM turn:lanes cell → bit mask (1 straight, 2 left, 4 right), or -1. */
std::vector<int> parseTurns(const std::string& spec, int lanes) {
  if (spec.empty()) return {};
  std::vector<std::string> parts;
  size_t start = 0;
  for (;;) {
    size_t bar = spec.find('|', start);
    parts.push_back(spec.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
    if (bar == std::string::npos) break;
    start = bar + 1;
  }
  if (static_cast<int>(parts.size()) != lanes) return {};
  // result indexed from the right-most lane; bit 1 = straight, 2 = left, 4 = right
  std::vector<int> out;
  for (int i = lanes - 1; i >= 0; i--) {
    const std::string& p = parts[static_cast<size_t>(i)];
    int m = 0;
    if (p.empty() || p == "none") {
      m = 7;
    } else {
      auto has = [&](const char* needle) { return p.find(needle) != std::string::npos; };
      if (has("through") || has("merge")) m |= 1;
      if (has("left") && !has("merge")) m |= 2;
      if (has("right") && !has("merge")) m |= 4;
      if (has("reverse")) m |= 2;
      if (m == 0) m = 7;
    }
    out.push_back(m);
  }
  return out;
}

/** Station on `a` from which it runs closer than `limit` metres to `b`. */
double convergence(const Polyline3& a, const Polyline3& b, double limit) {
  PolyProjection proj;
  PolySample smp;
  int steps = std::max(1, static_cast<int>(std::ceil(a.len / 0.6)));
  for (int i = 0; i <= steps; i++) {
    double s = a.len * i / steps;
    smp = samplePolyline(a, s, smp);
    proj = projectOnPolyline(b, smp.x, smp.z, proj);
    if (proj.d2 < limit * limit)
      return (i == steps && i > 0 && limit < 2) ? a.len : std::max(0.0, s - 0.6);
  }
  return a.len;
}

/** Station on `a` up to which it stays within 2.4 m of `b`. */
double divergence(const Polyline3& a, const Polyline3& b) {
  PolyProjection proj;
  PolySample smp;
  int steps = std::max(1, static_cast<int>(std::ceil(a.len / 0.6)));
  for (int i = 0; i <= steps; i++) {
    double s = a.len * i / steps;
    smp = samplePolyline(a, s, smp);
    proj = projectOnPolyline(b, smp.x, smp.z, proj);
    if (proj.d2 > 2.4 * 2.4) return s;
  }
  return a.len;
}

/** First intersection of two stride-3 polylines (stations on a and b). */
bool firstCrossing(const Polyline3& a, const Polyline3& b, double& outA, double& outB) {
  const auto& pa = a.pts;
  const auto& pb = b.pts;
  size_t na = a.cum.size();
  size_t nb = b.cum.size();
  for (size_t i = 0; i + 1 < na; i++) {
    for (size_t j = 0; j + 1 < nb; j++) {
      LineHit r = segSeg(pa[i * 3], pa[i * 3 + 2], pa[i * 3 + 3], pa[i * 3 + 5],
                         pb[j * 3], pb[j * 3 + 2], pb[j * 3 + 3], pb[j * 3 + 5]);
      if (r.has) {
        outA = a.cum[i] + (a.cum[i + 1] - a.cum[i]) * r.t;
        outB = b.cum[j] + (b.cum[j + 1] - b.cum[j]) * r.u;
        return true;
      }
    }
  }
  return false;
}

/** Binary min-heap over lane ids keyed by distance (the JS MinHeap). */
class MinHeap {
 public:
  size_t size() const { return ids_.size(); }
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
  bool pop(int& id, double& key) {
    if (ids_.empty()) return false;
    id = ids_[0];
    key = keys_[0];
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
    return true;
  }

 private:
  std::vector<int> ids_;
  std::vector<double> keys_;
};
}  // namespace

// ---------------------------------------------------------------------------
// LaneGraph
// ---------------------------------------------------------------------------

LaneGraph::LaneGraph(World& world_) : world(world_) {
  buildLanes();
  // every lane of every edge can reach every other lane at its two junctions
  size_t totalConnectors = 0;
  for (const auto& group : edgeLanes)
    totalConnectors += (group.fwd.size() + group.back.size()) * 8;
  connectors.reserve(totalConnectors);
  buildConnectors();
  buildConflicts();
}

PolySample lanePoint(const Lane& lane, double d, PolySample out) {
  double s = lane.s0 + lane.dir * clamp(d, 0, lane.len);
  out = samplePolyline(lane.edge->pl, s, out);
  double ox = out.tz * lane.off;
  double oz = -out.tx * lane.off;
  out.x += ox;
  out.z += oz;
  if (lane.dir < 0) {
    out.tx = -out.tx;
    out.tz = -out.tz;
  }
  return out;
}

void LaneGraph::buildLanes() {
  // the lanes keep pointers to each other (from/to/armTo) and to the edges
  // and arms, so the vectors are sized up front and never reallocate
  size_t totalLanes = 0;
  for (const auto& e : world.edges())
    totalLanes += static_cast<size_t>(e.lanesF + e.lanesB);
  lanes.reserve(totalLanes);
  edgeLanes.reserve(world.edges().size());

  for (auto& e : world.edges()) {
    EdgeLanes group;
    double W = e.width;
    double oR = -W / 2 + e.margin;
    double oD = oR + e.lanesF * e.laneW;
    double sA = e.cutA;
    double sB = e.len - e.cutB;
    double len = std::max(0.0, sB - sA);
    for (int k = 0; k < e.lanesF; k++) {
      Lane lane;
      lane.id = static_cast<int>(lanes.size());
      lane.edge = &e;
      lane.dir = 1;
      lane.idx = k;
      lane.count = e.lanesF;
      lane.off = oR + (k + 0.5) * e.laneW;
      lane.width = e.laneW;
      lane.s0 = sA;
      lane.s1 = sB;
      lane.len = len;
      lane.from = e.a;
      lane.to = e.b;
      lane.armTo = e.armB;
      lane.armFrom = e.armA;
      lane.limit = e.maxF > 0 ? e.maxF / 3.6 : std::numeric_limits<double>::infinity();
      lane.stopAt = std::max(0.0, len - std::max(0.0, e.armB->stopS - e.cutB));
      lanes.push_back(std::move(lane));
      group.fwd.push_back(&lanes.back());
    }
    for (int k = 0; k < e.lanesB; k++) {
      // right-most backward lane is the left-most seen along a→b
      double off = e.lanesF > 0 ? W / 2 - e.margin - (k + 0.5) * e.laneW
                                : oD + (e.lanesB - k - 0.5) * e.laneW;
      Lane lane;
      lane.id = static_cast<int>(lanes.size());
      lane.edge = &e;
      lane.dir = -1;
      lane.idx = k;
      lane.count = e.lanesB;
      lane.off = off;
      lane.width = e.laneW;
      lane.s0 = sB;
      lane.s1 = sA;
      lane.len = len;
      lane.from = e.b;
      lane.to = e.a;
      lane.armTo = e.armA;
      lane.armFrom = e.armB;
      lane.limit = e.maxB > 0 ? e.maxB / 3.6 : std::numeric_limits<double>::infinity();
      lane.stopAt = std::max(0.0, len - std::max(0.0, e.armA->stopS - e.cutA));
      lanes.push_back(std::move(lane));
      group.back.push_back(&lanes.back());
    }
    edgeLanes.push_back(std::move(group));
  }
}

std::vector<Lane*> LaneGraph::lanesInto(const Arm& arm) {
  const EdgeLanes& el = edgeLanes[static_cast<size_t>(arm.edge->id)];
  return arm.out ? el.back : el.fwd;
}

std::vector<Lane*> LaneGraph::lanesOutOf(const Arm& arm) {
  const EdgeLanes& el = edgeLanes[static_cast<size_t>(arm.edge->id)];
  return arm.out ? el.fwd : el.back;
}

void LaneGraph::addConnector(Node& node, Lane& from, Lane& to, int turn) {
  gTmpA = lanePoint(from, from.len, gTmpA);
  gTmpB = lanePoint(to, 0, gTmpB);
  double ax = gTmpA.x, az = gTmpA.z;
  double bx = gTmpB.x, bz = gTmpB.z;
  double dist = std::hypot(bx - ax, bz - az);
  std::vector<double> pts;
  if (dist < 0.3) {
    pts = {ax, world.planeY(node, ax, az), az, bx, world.planeY(node, bx, bz), bz};
  } else {
    double k = turn == TURN_UTURN ? clamp(dist * 0.9, 3, 9) : clamp(dist * 0.42, 0.5, 22);
    double c1x = ax + gTmpA.tx * k, c1z = az + gTmpA.tz * k;
    double c2x = bx - gTmpB.tx * k, c2z = bz - gTmpB.tz * k;
    int steps = std::max(2, std::min(16, static_cast<int>(std::ceil(dist / 1.6))));
    for (int i = 0; i <= steps; i++) {
      double t = static_cast<double>(i) / steps;
      double u = 1 - t;
      double x = u * u * u * ax + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * bx;
      double z = u * u * u * az + 3 * u * u * t * c1z + 3 * u * t * t * c2z + t * t * t * bz;
      pts.push_back(x);
      pts.push_back(world.planeY(node, x, z));
      pts.push_back(z);
    }
  }
  Connector c;
  c.id = static_cast<int>(connectors.size());
  c.node = &node;
  c.from = &from;
  c.to = &to;
  c.turn = turn;
  c.pl = makePolyline(pts);
  c.len = c.pl.len;
  connectors.push_back(std::move(c));
  from.out.push_back(&connectors.back());
  to.in.push_back(&connectors.back());
}

void LaneGraph::buildConnectors() {
  for (auto& node : world.nodes()) {
    auto& arms = node.arms;
    for (auto& a : arms) {
      std::vector<Lane*> ins = lanesInto(a);
      if (ins.empty()) continue;
      double inAng = angleOf(-a.dx, -a.dz);
      // candidate exits with their turn type
      struct Move {
        Arm* arm;
        int turn;
      };
      std::vector<Move> moves;
      for (auto& b : arms) {
        if (&b == &a) continue;
        if (lanesOutOf(b).empty()) continue;
        if (world.isBanned(a.edge->id, node.id, b.edge->id)) continue;
        double d = wrapAngle(angleOf(b.dx, b.dz) - inAng);
        double ad = std::fabs(d);
        int turn = ad <= 0.62 ? TURN_STRAIGHT : (ad > 2.8 ? TURN_UTURN : (d > 0 ? TURN_LEFT : TURN_RIGHT));
        moves.push_back({&b, turn});
      }
      if (moves.empty() && arms.size() == 1 && !lanesOutOf(a).empty()) {
        moves.push_back({&a, TURN_UTURN});  // turn round at the end of a cul-de-sac
      }
      if (moves.empty()) continue;
      // at a fork without a straight option the gentlest branch counts as "straight on"
      if (arms.size() > 2 &&
          !std::any_of(moves.begin(), moves.end(), [](const Move& m) { return m.turn == TURN_STRAIGHT; })) {
        int best = -1;
        double bestAd = 1.05;
        for (size_t i = 0; i < moves.size(); i++) {
          double ad = std::fabs(wrapAngle(angleOf(moves[i].arm->dx, moves[i].arm->dz) - inAng));
          if (ad < bestAd) {
            bestAd = ad;
            best = static_cast<int>(i);
          }
        }
        if (best >= 0) moves[static_cast<size_t>(best)].turn = TURN_STRAIGHT;
      }
      size_t n = ins.size();
      std::vector<int> tagged = parseTurns(a.out ? a.edge->turnB : a.edge->turnF, static_cast<int>(n));
      auto allowed = [&](int laneIdx, int turn) -> bool {
        if (arms.size() <= 2) return true;
        int bit = turn == TURN_STRAIGHT ? 1 : (turn == TURN_RIGHT ? 4 : 2);
        if (!tagged.empty()) return (tagged[static_cast<size_t>(laneIdx)] & bit) != 0;
        if (n == 1) return true;
        if (turn == TURN_STRAIGHT) return true;
        if (turn == TURN_RIGHT) return laneIdx == 0;
        return laneIdx == static_cast<int>(n) - 1;  // left and U-turn from the left-most lane
      };
      for (Lane* lane : ins) {
        int made = 0;
        auto connect = [&](bool force) {
          for (const Move& m : moves) {
            if (!force && !allowed(lane->idx, m.turn)) continue;
            std::vector<Lane*> outs = lanesOutOf(*m.arm);
            if (outs.empty()) continue;
            Lane* target = m.turn == TURN_RIGHT
                               ? outs[0]
                               : (m.turn == TURN_LEFT || m.turn == TURN_UTURN
                                      ? outs[outs.size() - 1]
                                      : outs[std::min(static_cast<size_t>(lane->idx), outs.size() - 1)]);
            addConnector(node, *lane, *target, m.turn);
            made++;
          }
        };
        connect(false);
        if (made == 0) connect(true);  // never strand a lane
      }
    }
  }
}

bool LaneGraph::yields(const Connector& c1, const Connector& c2, bool merge) const {
  const Arm* a1 = c1.from->armTo;
  const Arm* a2 = c2.from->armTo;
  const Node* node = c1.node;
  if (a1 == a2) {
    // two lanes of one carriageway running into one: the lane that ends gives way
    return merge && c1.from->idx > c2.from->idx;
  }
  if (node->ctrl == CTRL_SIGNALS && a1->sg >= 0 && a2->sg >= 0) {
    // both signal-controlled: only permissive conflicts within the same green remain
    if (a1->sg != a2->sg) return false;
  } else {
    int r1 = rank(*a1);
    int r2 = rank(*a2);
    if (r1 != r2) return r1 < r2;
  }
  bool opposite = a1->dx * a2->dx + a1->dz * a2->dz < -0.64;
  bool turning1 = c1.turn == TURN_LEFT || c1.turn == TURN_UTURN;
  bool turning2 = c2.turn == TURN_LEFT || c2.turn == TURN_UTURN;
  if (opposite || a1->role == ROLE_PRIORITY) {
    // § 9 (3): turning left, let oncoming traffic through
    if (turning1 && !turning2) return true;
    if (!turning1 && turning2) return false;
    if (turning1 && turning2) return false;  // both turn left: pass in front of each other
    if (merge) return c1.turn != TURN_STRAIGHT && c2.turn == TURN_STRAIGHT;
    return false;
  }
  // equal standing, crossing roads: the one coming from the right goes first
  double rx = a1->dz;  // right of travel direction (-dx,-dz) is (dz,-dx)
  double rz = -a1->dx;
  double side = a2->dx * rx + a2->dz * rz;
  if (side > 0.25) return true;
  if (side < -0.25) return false;
  return turning1 && !turning2;
}

void LaneGraph::buildConflicts() {
  std::map<int, std::vector<Connector*>> byNode;
  for (auto& c : connectors) byNode[c.node->id].push_back(&c);
  for (auto& kv : byNode) {
    auto& list = kv.second;
    for (size_t i = 0; i < list.size(); i++) {
      for (size_t j = i + 1; j < list.size(); j++) {
        Connector& c1 = *list[i];
        Connector& c2 = *list[j];
        if (c1.from == c2.from) {
          // diverging from one lane: no right-of-way question, but the paths share their first metres
          double u1 = divergence(c1.pl, c2.pl);
          double u2 = divergence(c2.pl, c1.pl);
          c1.siblings.push_back({&c2, u1, u2});
          c2.siblings.push_back({&c1, u2, u1});
          continue;
        }
        bool sameArm = c1.from->armTo == c2.from->armTo;
        double s1 = -1, s2 = -1;
        bool merge = false;
        if (c1.to == c2.to) {
          // merging paths conflict from where they come too close to pass side by side
          merge = true;
          s1 = convergence(c1.pl, c2.pl, 2.4);
          s2 = convergence(c2.pl, c1.pl, 2.4);
        } else if (c1.len > 0.5 && c2.len > 0.5) {
          double hitA = 0, hitB = 0;
          bool hit = !sameArm && firstCrossing(c1.pl, c2.pl, hitA, hitB);
          if (hit) {
            s1 = hitA;
            s2 = hitB;
          } else {
            // paths that never intersect but squeeze past each other closer than a car is wide
            double p1 = convergence(c1.pl, c2.pl, 1.9);
            if (p1 < c1.len) {
              s1 = p1;
              s2 = convergence(c2.pl, c1.pl, 1.9);
              merge = true;
            }
          }
        }
        if (s1 < 0) continue;
        bool y12 = yields(c1, c2, merge);
        bool y21 = !y12 && yields(c2, c1, merge);
        c1.conflicts.push_back({&c2, s1, s2, merge, y12});
        c2.conflicts.push_back({&c1, s2, s1, merge, y21});
      }
    }
  }
}

bool LaneGraph::drivable(const Edge& e, bool forTraffic) {
  if (e.access == ACCESS_PRIVATE || e.access == ACCESS_AGRICULTURAL) return false;
  if (forTraffic && (e.tier < 2 || e.laneW < 2.4 || (e.flags & EF_MINOR_SERVICE) != 0)) return false;
  return true;
}

std::vector<Connector*> LaneGraph::route(const Lane& start, const Edge& goal, bool forTraffic) {
  std::vector<double> dist(lanes.size(), std::numeric_limits<double>::infinity());
  std::vector<Connector*> prev(lanes.size(), nullptr);
  MinHeap heap;
  dist[static_cast<size_t>(start.id)] = 0;
  heap.push(start.id, 0);
  Lane* foundLane = nullptr;
  int id;
  double d;
  while (heap.pop(id, d)) {
    if (d > dist[static_cast<size_t>(id)]) continue;
    Lane& lane = lanes[static_cast<size_t>(id)];
    if (lane.edge == &goal && &lane != &start) {
      foundLane = &lane;
      break;
    }
    for (Connector* c : lane.out) {
      Lane& next = *c->to;
      if (next.edge != &goal && !drivable(*next.edge, forTraffic)) continue;
      double v = std::min(next.limit, 36.0);
      // Turning round is for when there is no other way: it needs a turning
      // circle or a three-point turn, and where the map ends (often a fast
      // rural road) it is a last resort.
      double uturn = c->from->to->ctrl == CTRL_MAP_END ? 900 : 150;
      double turnCost = c->turn == TURN_LEFT ? 6
                        : (c->turn == TURN_RIGHT ? 3
                           : (c->turn == TURN_UTURN ? uturn : 0));
      // two left turns a few metres apart = turning round through a gap in the
      // median (the previous connector on this lane, if it came from one)
      Connector* prevC = prev[static_cast<size_t>(lane.id)];
      if (c->turn == TURN_LEFT && lane.len < 15 && prevC && prevC->turn == TURN_LEFT)
        turnCost += 150;
      // "Anlieger frei" and car-park shortcuts are not through routes
      double through = next.edge != &goal &&
                               (next.edge->access == ACCESS_DESTINATION ||
                                (next.edge->flags & EF_MINOR_SERVICE) != 0)
                           ? 240
                           : 0;
      double nd = d + c->len / 6 + next.len / std::max(v, 2.0) + turnCost + through;
      if (nd < dist[static_cast<size_t>(next.id)]) {
        dist[static_cast<size_t>(next.id)] = nd;
        prev[static_cast<size_t>(next.id)] = c;
        heap.push(next.id, nd);
      }
    }
  }
  if (!foundLane) return {};
  std::vector<Connector*> path;
  Lane* cur = foundLane;
  while (cur != &start) {
    Connector* c = prev[static_cast<size_t>(cur->id)];
    if (!c) return {};
    path.push_back(c);
    cur = c->from;
  }
  std::reverse(path.begin(), path.end());
  return path;
}

}  // namespace fs

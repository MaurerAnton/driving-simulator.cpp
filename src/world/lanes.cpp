// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/lanes.ts @ v1.7.7
/**
 * Lane-level network derived from the road graph: driving lanes on every
 * edge, connectors through every junction, who has to give way to whom, and
 * shortest-path routing (see lanes.h).
 */
#include "world/lanes.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "world/world.h"

namespace fs {

namespace {

PolySample tmpA = newPolySample();
PolySample tmpB = newPolySample();
PolyProjection pathProj;
PathFoot postFoot;

const std::vector<Lane*> NO_LANES;
const std::vector<Connector*> NO_PATHS;

std::optional<std::vector<int>> parseTurns(const std::string& spec, int lanes) {
  if (spec.empty()) return std::nullopt;
  const std::vector<std::string> parts = split(spec, "|");
  if (static_cast<int>(parts.size()) != lanes) return std::nullopt;
  // result indexed from the right-most lane; bit 1 = straight, 2 = left, 4 = right
  std::vector<int> out;
  for (int i = lanes - 1; i >= 0; i--) {
    const std::string& p = parts[static_cast<size_t>(i)];
    auto has = [&](const char* word) { return p.find(word) != std::string::npos; };
    int m = 0;
    if (p.empty() || p == "none") m = 7;
    else {
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

int rank(const Arm& arm) {
  switch (arm.role) {
    case ROLE_PRIORITY:
      return 3;
    case ROLE_RBL:
      return 2;
    case ROLE_YIELD:
    case ROLE_STOP:
      return 1;
    default:
      return 0;
  }
}

/** Does the path through a junction double back (by more than 115°) on a radius under 7 m? */
bool hairpin(const Connector& c) {
  const std::vector<double>& p = c.pl.pts;
  const size_t n = p.size() / 3;
  if (n < 2 || c.len > 22) return false;
  const double ax = p[3] - p[0];
  const double az = p[5] - p[2];
  const double bx = p[(n - 1) * 3] - p[(n - 2) * 3];
  const double bz = p[(n - 1) * 3 + 2] - p[(n - 2) * 3 + 2];
  const double la = jsHypot(ax, az);
  const double lb = jsHypot(bx, bz);
  if (la < 1e-6 || lb < 1e-6) return false;
  const double turn = std::acos(jsMax(-1, jsMin(1, (ax * bx + az * bz) / (la * lb))));
  return turn > 2 && c.len / turn < 7;
}

/** Squared distance of a point from a segment. */
double pointSegDist2(double x, double z, double ax, double az, double bx, double bz) {
  const double dx = bx - ax;
  const double dz = bz - az;
  const double l2 = dx * dx + dz * dz;
  double t = l2 > 1e-12 ? ((x - ax) * dx + (z - az) * dz) / l2 : 0;
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  const double ex = x - (ax + dx * t);
  const double ez = z - (az + dz * t);
  return ex * ex + ez * ez;
}

/** Squared distance between two segments in the plane (0 when they cross). */
double segSegDist2(double ax, double az, double bx, double bz, double cx, double cz, double dx, double dz) {
  // crossing: each segment has the ends of the other on different sides
  const double d1 = (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
  const double d2 = (bx - ax) * (dz - az) - (bz - az) * (dx - ax);
  const double d3 = (dx - cx) * (az - cz) - (dz - cz) * (ax - cx);
  const double d4 = (dx - cx) * (bz - cz) - (dz - cz) * (bx - cx);
  if (d1 * d2 < 0 && d3 * d4 < 0) return 0;
  return jsMin(jsMin(jsMin(pointSegDist2(ax, az, cx, cz, dx, dz), pointSegDist2(bx, bz, cx, cz, dx, dz)), pointSegDist2(cx, cz, ax, az, bx, bz)),
               pointSegDist2(dx, dz, ax, az, bx, bz));
}

/** Station on `a` from which it runs closer than `limit` metres to `b` (the end of `a` if never). */
double convergence(const Polyline3& a, const Polyline3& b, double limit) {
  PolyProjection proj;
  PolySample smp = newPolySample();
  const double steps = jsMax(1, std::ceil(a.len / 0.6));
  for (double i = 0; i <= steps; i++) {
    const double s = (a.len * i) / steps;
    samplePolyline(a, s, smp);
    projectOnPolyline(b, smp.x, smp.z, proj);
    if (proj.d2 < limit * limit) return i == steps && i > 0 && limit < 2 ? a.len : jsMax(0, s - 0.6);
  }
  return a.len;
}

/** Station on `a` up to which it stays within 2.4 m of `b` (both start at the same point). */
double divergence(const Polyline3& a, const Polyline3& b) {
  PolyProjection proj;
  PolySample smp = newPolySample();
  const double steps = jsMax(1, std::ceil(a.len / 0.6));
  for (double i = 0; i <= steps; i++) {
    const double s = (a.len * i) / steps;
    samplePolyline(a, s, smp);
    projectOnPolyline(b, smp.x, smp.z, proj);
    if (proj.d2 > 2.4 * 2.4) return s;
  }
  return a.len;
}

/** First intersection of two stride-3 polylines: [station on a, station on b] (in `t` and `u`). */
LineHit firstCrossing(const Polyline3& a, const Polyline3& b) {
  const std::vector<double>& pa = a.pts;
  const std::vector<double>& pb = b.pts;
  const size_t na = a.cum.size();
  const size_t nb = b.cum.size();
  for (size_t i = 0; i + 1 < na; i++) {
    for (size_t j = 0; j + 1 < nb; j++) {
      const LineHit r = segSeg(pa[i * 3], pa[i * 3 + 2], pa[i * 3 + 3], pa[i * 3 + 5], pb[j * 3], pb[j * 3 + 2], pb[j * 3 + 3], pb[j * 3 + 5]);
      if (r) {
        LineHit hit;
        hit.t = a.cum[i] + (a.cum[i + 1] - a.cum[i]) * r.t;
        hit.u = b.cum[j] + (b.cum[j + 1] - b.cum[j]) * r.u;
        hit.has = true;
        return hit;
      }
    }
  }
  return LineHit();
}

class MinHeap {
 public:
  size_t size() const { return ids_.size(); }

  void push(int id, double key) {
    size_t i = ids_.size();
    ids_.push_back(id);
    keys_.push_back(key);
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

  void pop(int& topId, double& topKey) {
    topId = ids_[0];
    topKey = keys_[0];
    const int lastId = ids_.back();
    const double lastKey = keys_.back();
    ids_.pop_back();
    keys_.pop_back();
    const size_t n = ids_.size();
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
  }

 private:
  std::vector<int> ids_;
  std::vector<double> keys_;
};

}  // namespace

void Connector::resolve() const { graph_->resolveNode(node->id); }

PolySample& lanePoint(const Lane& lane, double d, PolySample& out) {
  const double s = lane.s0 + lane.dir * clamp(d, 0, lane.len);
  samplePolyline(lane.edge->pl, s, out);
  const double ox = out.tz * lane.off;
  const double oz = -out.tx * lane.off;
  out.x += ox;
  out.z += oz;
  if (lane.dir < 0) {
    out.tx = -out.tx;
    out.tz = -out.tz;
  }
  return out;
}

LaneGraph::LaneGraph(World& world_) : world(world_) {
  for (Edge* e : world.allEdges) addEdge(*e);
  for (Node* node : world.allNodes)
    if (!node->stub) completeNode(*node);
}

void LaneGraph::addEdge(Edge& e) {
  if (edgeLanes[e.id]) return;
  auto el = std::make_unique<EdgeLanes>();
  std::vector<Lane*>& fwd = el->fwd;
  std::vector<Lane*>& back = el->back;
  const double W = e.width;
  // (beside the lanes, up to the kerb: the hard strip, a cycle lane, parked cars – src/world/xsection.h)
  const double oR = -W / 2 + e.marginR;
  const double oD = oR + e.lanesF * e.laneW;
  const double sA = e.cutA;
  const double sB = e.len - e.cutB;
  const double len = jsMax(0, sB - sA);
  for (int k = 0; k < e.lanesF; k++) {
    auto lane = std::make_unique<Lane>();
    lane->id = nextLane_++;
    lane->edge = &e;
    lane->dir = 1;
    lane->idx = k;
    lane->count = e.lanesF;
    lane->off = oR + (k + 0.5) * e.laneW;
    lane->width = e.laneW;
    lane->s0 = sA;
    lane->s1 = sB;
    lane->len = len;
    lane->from = e.a;
    lane->to = e.b;
    lane->armTo = e.armB;
    lane->armFrom = e.armA;
    lane->limit = e.maxF > 0 ? e.maxF / 3.6 : JS_INF;
    lane->stopAt = jsMax(0, len - jsMax(0, e.armB->stopS - e.cutB));
    const int id = lane->id;
    fwd.push_back(lanes.set(id, std::move(lane)));
  }
  for (int k = 0; k < e.lanesB; k++) {
    // right-most backward lane is the left-most seen along a→b
    const double off = e.lanesF > 0 ? W / 2 - e.marginL - (k + 0.5) * e.laneW : oD + (e.lanesB - k - 0.5) * e.laneW;
    auto lane = std::make_unique<Lane>();
    lane->id = nextLane_++;
    lane->edge = &e;
    lane->dir = -1;
    lane->idx = k;
    lane->count = e.lanesB;
    lane->off = off;
    lane->width = e.laneW;
    lane->s0 = sB;
    lane->s1 = sA;
    lane->len = len;
    lane->from = e.b;
    lane->to = e.a;
    lane->armTo = e.armA;
    lane->armFrom = e.armB;
    lane->limit = e.maxB > 0 ? e.maxB / 3.6 : JS_INF;
    lane->stopAt = jsMax(0, len - jsMax(0, e.armA->stopS - e.cutA));
    const int id = lane->id;
    back.push_back(lanes.set(id, std::move(lane)));
  }
  edgeLanes.set(e.id, std::move(el));
}

void LaneGraph::letGo() {
  if (goneWith_ == world.removals) return;
  goneLanes_.clear();
  gonePaths_.clear();
  goneEdgeLanes_.clear();
  goneWith_ = world.removals;
}

void LaneGraph::removeEdge(Edge& e) {
  letGo();
  EdgeLanes* el = edgeLanes[e.id];
  if (!el) return;
  std::vector<Lane*> both = el->fwd;
  both.insert(both.end(), el->back.begin(), el->back.end());
  for (Lane* lane : both) {
    // (paths of a junction that stays, as the end of another road, cannot lead here any more)
    std::vector<Connector*> paths = lane->out;
    paths.insert(paths.end(), lane->in.begin(), lane->in.end());
    for (Connector* c : paths) {
      // (what the paths that stay at that junction found out about this one goes with it: nothing may point to it any more)
      auto at = pathsAt_.find(c->node->id);
      if (at != pathsAt_.end()) {
        for (Connector* other : at->second) {
          if (other == c) continue;
          std::erase_if(other->foundConflicts, [c](const Conflict& k) { return k.other == c; });
          std::erase_if(other->foundSiblings, [c](const Sibling& k) { return k.other == c; });
        }
      }
      dropPath(c);
    }
    goneLanes_.push_back(lanes.take(lane->id));
  }
  goneEdgeLanes_.push_back(edgeLanes.take(e.id));
}

void LaneGraph::dropPath(Connector* c) {
  auto owned = paths_.find(c);
  if (owned == paths_.end()) return;
  std::vector<Connector*>& out = c->from->out;
  auto o = std::find(out.begin(), out.end(), c);
  if (o != out.end()) out.erase(o);
  std::vector<Connector*>& into = c->to->in;
  auto i = std::find(into.begin(), into.end(), c);
  if (i != into.end()) into.erase(i);
  auto at = pathsAt_.find(c->node->id);
  if (at != pathsAt_.end()) {
    auto k = std::find(at->second.begin(), at->second.end(), c);
    if (k != at->second.end()) at->second.erase(k);
  }
  // (its place in the list is taken by the last one, as in the original – found without looking through the list)
  if (c->slot_ < connectors.size() && connectors[c->slot_] == c) {
    connectors[c->slot_] = connectors.back();
    connectors[c->slot_]->slot_ = c->slot_;
    connectors.pop_back();
  }
  gonePaths_.push_back(std::move(owned->second));
  paths_.erase(owned);
}

void LaneGraph::removeNode(Node& node) {
  letGo();
  auto at = pathsAt_.find(node.id);
  if (at != pathsAt_.end()) {
    const std::vector<Connector*> paths = at->second;
    for (Connector* c : paths) dropPath(c);
  }
  pathsAt_.erase(node.id);
  resolved_.erase(node.id);
}

const std::vector<Lane*>& LaneGraph::lanesInto(const Arm& arm) const {
  const EdgeLanes* el = edgeLanes[arm.edge->id];
  if (!el) return NO_LANES;
  return arm.out ? el->back : el->fwd;
}

const std::vector<Lane*>& LaneGraph::lanesOutOf(const Arm& arm) const {
  const EdgeLanes* el = edgeLanes[arm.edge->id];
  if (!el) return NO_LANES;
  return arm.out ? el->fwd : el->back;
}

void LaneGraph::completeNode(Node& node) {
  if (pathsAt_.count(node.id)) return;
  pathsAt_[node.id] = {};
  for (const Arm* a : node.arms) {
    for (Lane* lane : lanesInto(*a)) lane->stopAt = jsMax(0, lane->len - jsMax(0, a->stopS - a->cut));
  }
  {
    const std::vector<Arm*>& arms = node.arms;
    for (Arm* a : arms) {
      const std::vector<Lane*>& ins = lanesInto(*a);
      if (ins.empty()) continue;
      const double inAng = angleOf(-a->dx, -a->dz);
      // candidate exits with their turn type
      struct Move {
        Arm* arm;
        int turn;
      };
      std::vector<Move> moves;
      for (Arm* b : arms) {
        if (b == a) continue;
        if (lanesOutOf(*b).empty()) continue;
        if (world.isBanned(*a->edge, node, *b->edge)) continue;
        const double d = wrapAngle(angleOf(b->dx, b->dz) - inAng);
        const double ad = std::fabs(d);
        const int turn = ad <= 0.62 ? TURN_STRAIGHT : ad > 2.8 ? TURN_UTURN : d > 0 ? TURN_LEFT : TURN_RIGHT;
        moves.push_back({b, turn});
      }
      if (moves.empty() && arms.size() == 1 && !lanesOutOf(*a).empty()) {
        moves.push_back({a, TURN_UTURN});  // turn round at the end of a cul-de-sac
      }
      if (moves.empty()) continue;
      // at a fork without a straight option the gentlest branch counts as "straight on"
      bool anyStraight = false;
      for (const Move& m : moves)
        if (m.turn == TURN_STRAIGHT) anyStraight = true;
      if (arms.size() > 2 && !anyStraight) {
        int best = -1;
        double bestAd = 1.05;
        for (size_t i = 0; i < moves.size(); i++) {
          const double ad = std::fabs(wrapAngle(angleOf(moves[i].arm->dx, moves[i].arm->dz) - inAng));
          if (ad < bestAd) {
            bestAd = ad;
            best = static_cast<int>(i);
          }
        }
        if (best >= 0) moves[static_cast<size_t>(best)].turn = TURN_STRAIGHT;
      }
      const int n = static_cast<int>(ins.size());
      const std::optional<std::vector<int>> tagged = parseTurns(a->out ? a->edge->turnB : a->edge->turnF, n);
      auto allowed = [&](int laneIdx, int turn) -> bool {
        if (arms.size() <= 2) return true;
        const int bit = turn == TURN_STRAIGHT ? 1 : turn == TURN_RIGHT ? 4 : 2;
        if (tagged) return ((*tagged)[static_cast<size_t>(laneIdx)] & bit) != 0;
        if (n == 1) return true;
        if (turn == TURN_STRAIGHT) return true;
        if (turn == TURN_RIGHT) return laneIdx == 0;
        return laneIdx == n - 1;  // left and U-turn from the left-most lane
      };
      // (the list of lanes is the road's own: it does not change while paths are added)
      for (Lane* lane : ins) {
        int made = 0;
        auto connect = [&](bool force) {
          for (const Move& m : moves) {
            if (!force && !allowed(lane->idx, m.turn)) continue;
            const std::vector<Lane*>& outs = lanesOutOf(*m.arm);
            Lane* target = m.turn == TURN_RIGHT
                               ? outs[0]
                               : m.turn == TURN_LEFT || m.turn == TURN_UTURN ? outs[outs.size() - 1] : outs[std::min(static_cast<size_t>(lane->idx), outs.size() - 1)];
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

void LaneGraph::addConnector(Node& node, Lane& from, Lane& to, int turn) {
  lanePoint(from, from.len, tmpA);
  lanePoint(to, 0, tmpB);
  const double ax = tmpA.x;
  const double az = tmpA.z;
  const double bx = tmpB.x;
  const double bz = tmpB.z;
  const double dist = jsHypot(bx - ax, bz - az);
  std::vector<double> pts;
  if (dist < 0.3) {
    pts = {ax, world.planeY(node, ax, az), az, bx, world.planeY(node, bx, bz), bz};
  } else {
    const double k = turn == TURN_UTURN ? clamp(dist * 0.9, 3, 9) : clamp(dist * 0.42, 0.5, 22);
    const double c1x = ax + tmpA.tx * k;
    const double c1z = az + tmpA.tz * k;
    const double c2x = bx - tmpB.tx * k;
    const double c2z = bz - tmpB.tz * k;
    const double steps = jsMax(2, jsMin(16, std::ceil(dist / 1.6)));
    for (double i = 0; i <= steps; i++) {
      const double t = i / steps;
      const double u = 1 - t;
      const double x = u * u * u * ax + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * bx;
      const double z = u * u * u * az + 3 * u * u * t * c1z + 3 * u * t * t * c2z + t * t * t * bz;
      pts.push_back(x);
      pts.push_back(world.planeY(node, x, z));
      pts.push_back(z);
    }
  }
  auto made = std::make_unique<Connector>();
  Connector* c = made.get();
  c->graph_ = this;
  c->id = nextPath_++;
  c->node = &node;
  c->from = &from;
  c->to = &to;
  c->turn = turn;
  c->pl = makePolyline(std::move(pts));
  c->len = c->pl.len;
  c->slot_ = connectors.size();
  connectors.push_back(c);
  paths_[c] = std::move(made);
  pathsAt_[node.id].push_back(c);
  from.out.push_back(c);
  to.in.push_back(c);
}

bool LaneGraph::yields(const Connector& c1, const Connector& c2, bool merge) const {
  const Arm& a1 = *c1.from->armTo;
  const Arm& a2 = *c2.from->armTo;
  const Node& node = *c1.node;
  if (&a1 == &a2) {
    // two lanes of one carriageway running into one: the lane that ends gives way
    return merge && c1.from->idx > c2.from->idx;
  }
  if (node.ctrl == CTRL_SIGNALS && a1.sg >= 0 && a2.sg >= 0) {
    // both signal-controlled: only permissive conflicts within the same green remain
    if (a1.sg != a2.sg) return false;
  } else {
    const int r1 = rank(a1);
    const int r2 = rank(a2);
    if (r1 != r2) return r1 < r2;
  }
  const bool opposite = a1.dx * a2.dx + a1.dz * a2.dz < -0.64;
  const bool turning1 = c1.turn == TURN_LEFT || c1.turn == TURN_UTURN;
  const bool turning2 = c2.turn == TURN_LEFT || c2.turn == TURN_UTURN;
  if (opposite || a1.role == ROLE_PRIORITY) {
    // § 9 (3): turning left, let oncoming traffic through
    if (turning1 && !turning2) return true;
    if (!turning1 && turning2) return false;
    if (turning1 && turning2) return false;  // both turn left: pass in front of each other
    if (merge) return c1.turn != TURN_STRAIGHT && c2.turn == TURN_STRAIGHT;
    return false;
  }
  // equal standing, crossing roads: the one coming from the right goes first
  const double rx = a1.dz;  // right of travel direction (-dx,-dz) is (dz,-dx)
  const double rz = -a1.dx;
  const double side = a2.dx * rx + a2.dz * rz;
  if (side > 0.25) return true;
  if (side < -0.25) return false;
  return turning1 && !turning2;
}

void LaneGraph::resolveAll() {
  std::vector<int> ids;
  ids.reserve(pathsAt_.size());
  for (const auto& entry : pathsAt_) ids.push_back(entry.first);
  for (int id : ids) resolveNode(id);
}

void LaneGraph::resolveNode(int nodeId) {
  if (resolved_.count(nodeId)) return;
  resolved_.insert(nodeId);
  auto at = pathsAt_.find(nodeId);
  if (at == pathsAt_.end()) return;
  const std::vector<Connector*>& list = at->second;
  {
    for (size_t i = 0; i < list.size(); i++) {
      for (size_t j = i + 1; j < list.size(); j++) {
        Connector* c1 = list[i];
        Connector* c2 = list[j];
        if (c1->from == c2->from) {
          // diverging from one lane: no right-of-way question, but the paths share their first metres
          const double u1 = divergence(c1->pl, c2->pl);
          const double u2 = divergence(c2->pl, c1->pl);
          c1->foundSiblings.push_back({c2, u1, u2});
          c2->foundSiblings.push_back({c1, u2, u1});
          continue;
        }
        const bool sameArm = c1->from->armTo == c2->from->armTo;
        double s1 = -1;
        double s2 = -1;
        bool merge = false;
        if (c1->to == c2->to) {
          // merging paths conflict from where they come too close to pass side by side
          merge = true;
          s1 = convergence(c1->pl, c2->pl, 2.4);
          s2 = convergence(c2->pl, c1->pl, 2.4);
        } else if (c1->len > 0.5 && c2->len > 0.5) {
          const LineHit hit = sameArm ? LineHit() : firstCrossing(c1->pl, c2->pl);
          if (hit) {
            s1 = hit.t;
            s2 = hit.u;
          } else {
            // paths that never intersect but squeeze past each other closer than a car is wide
            const double p1 = convergence(c1->pl, c2->pl, 1.9);
            if (p1 < c1->len) {
              s1 = p1;
              s2 = convergence(c2->pl, c1->pl, 1.9);
              merge = true;
            }
          }
        }
        if (s1 < 0) continue;
        const bool y12 = yields(*c1, *c2, merge);
        const bool y21 = !y12 && yields(*c2, *c1, merge);
        c1->foundConflicts.push_back({c2, s1, s2, merge, y12});
        c2->foundConflicts.push_back({c1, s2, s1, merge, y21});
      }
    }
  }
}

double LaneGraph::nearestPath(double x, double z, double y, double reach, PathFoot& out) {
  double best = reach;
  bool found = false;
  PolyProjection& proj = pathProj;
  PolySample& smp = tmpA;
  world.segmentsInBox(x - reach, z - reach, x + reach, z + reach, [&](Edge& e, int seg) {
    const EdgeLanes* el = edgeLanes[e.id];
    if (!el) return;
    if (el->fwd.size() + el->back.size() == 0) return;
    projectOnPolyline(e.pl, x, z, proj, seg, seg);
    const double far = e.width / 2 + best;
    if (proj.d2 > far * far) return;
    // (the lanes run between the junction areas at the two ends)
    samplePolyline(e.pl, clamp(proj.s, e.cutA, jsMax(e.cutA, e.len - e.cutB)), smp);
    if (!(y != y) && std::fabs(smp.y - y) > 2.6) return;
    for (int side = 0; side < 2; side++) {
      const std::vector<Lane*>& list = side == 0 ? el->fwd : el->back;
      for (size_t i = 0; i < list.size(); i++) {
        const double px = smp.x + smp.tz * list[i]->off;
        const double pz = smp.z - smp.tx * list[i]->off;
        const double d = jsHypot(x - px, z - pz);
        if (d < best) {
          best = d;
          out.x = px;
          out.z = pz;
          found = true;
        }
      }
    }
  });
  world.nodesInBox(x - reach, z - reach, x + reach, z + reach, [&](Node& node) {
    if (!(y != y) && std::fabs(world.planeY(node, x, z) - y) > 2.6) return;
    auto at = pathsAt_.find(node.id);
    if (at == pathsAt_.end()) return;
    const std::vector<Connector*>& paths = at->second;
    for (size_t i = 0; i < paths.size(); i++) {
      projectOnPolyline(paths[i]->pl, x, z, proj);
      if (proj.d2 >= best * best) continue;
      best = std::sqrt(proj.d2);
      samplePolyline(paths[i]->pl, proj.s, smp);
      out.x = smp.x;
      out.z = smp.z;
      found = true;
    }
  });
  out.d = found ? best : JS_INF;
  return out.d;
}

bool LaneGraph::wallInLane(double x1, double z1, double x2, double z2, double y, double need) {
  const double minX = jsMin(x1, x2) - need;
  const double maxX = jsMax(x1, x2) + need;
  const double minZ = jsMin(z1, z2) - need;
  const double maxZ = jsMax(z1, z2) + need;
  const double need2 = need * need;
  bool hit = false;
  world.segmentsInBox(minX, minZ, maxX, maxZ, [&](Edge& e, int seg) {
    if (hit) return;
    const EdgeLanes* el = edgeLanes[e.id];
    if (!el) return;
    if (el->fwd.size() + el->back.size() == 0) return;
    const std::vector<double>& p = e.pl.pts;
    const size_t o = static_cast<size_t>(seg) * 3;
    const double ax = p[o];
    const double ay = p[o + 1];
    const double az = p[o + 2];
    const double bx = p[o + 3];
    const double bz = p[o + 5];
    if (!(y != y) && std::fabs((ay + p[o + 4]) / 2 - y) > 2.6 + std::fabs(ay - p[o + 4]) / 2) return;
    // (most pieces of road in reach of the grid are nowhere near the wall)
    const double far = e.width / 2 + need;
    if (jsMax(ax, bx) + far < minX + need || jsMin(ax, bx) - far > maxX - need || jsMax(az, bz) + far < minZ + need || jsMin(az, bz) - far > maxZ - need) return;
    const double l = jsHypot(bx - ax, bz - az);
    if (l < 1e-6) return;
    // left of the direction of travel a→b
    const double nx = (bz - az) / l;
    const double nz = -(bx - ax) / l;
    for (int side = 0; side < 2 && !hit; side++) {
      const std::vector<Lane*>& list = side == 0 ? el->fwd : el->back;
      for (size_t i = 0; i < list.size(); i++) {
        const double off = list[i]->off;
        if (segSegDist2(x1, z1, x2, z2, ax + nx * off, az + nz * off, bx + nx * off, bz + nz * off) < need2) {
          hit = true;
          break;
        }
      }
    }
  });
  if (hit) return true;
  world.nodesInBox(minX, minZ, maxX, maxZ, [&](Node& node) {
    if (hit) return;
    if (!(y != y) && std::fabs(world.planeY(node, (x1 + x2) / 2, (z1 + z2) / 2) - y) > 2.6) return;
    auto at = pathsAt_.find(node.id);
    if (at == pathsAt_.end()) return;
    const std::vector<Connector*>& paths = at->second;
    for (size_t k = 0; k < paths.size() && !hit; k++) {
      const std::vector<double>& p = paths[k]->pl.pts;
      for (size_t i = 0; i + 5 < p.size(); i += 3) {
        if (segSegDist2(x1, z1, x2, z2, p[i], p[i + 2], p[i + 3], p[i + 5]) < need2) {
          hit = true;
          break;
        }
      }
    }
  });
  return hit;
}

PathFoot* LaneGraph::besideLanes(double x, double z, double y, double need, PathFoot& out) {
  out.x = x;
  out.z = z;
  for (int tries = 0; tries < 5; tries++) {
    const double d = nearestPath(out.x, out.z, y, need, postFoot);
    if (d >= need) {
      out.d = jsHypot(out.x - x, out.z - z);
      return &out;
    }
    // away from the middle of that lane (straight out of it; from dead centre towards where the post came from)
    double ux = out.x - postFoot.x;
    double uz = out.z - postFoot.z;
    if (d < 0.05) {
      ux = x - postFoot.x;
      uz = z - postFoot.z;
      if (jsHypot(ux, uz) < 0.05) return nullptr;
    }
    const double l = jsHypot(ux, uz);
    out.x = postFoot.x + (ux / l) * (need + 0.15);
    out.z = postFoot.z + (uz / l) * (need + 0.15);
    if (jsHypot(out.x - x, out.z - z) > 4.5) return nullptr;
  }
  return nullptr;
}

const std::vector<Connector*>& LaneGraph::pathsThrough(const Node& node) const {
  auto at = pathsAt_.find(node.id);
  return at == pathsAt_.end() ? NO_PATHS : at->second;
}

bool LaneGraph::drivable(const Edge& e, bool forTraffic) {
  if (e.access == ACCESS_PRIVATE || e.access == ACCESS_AGRICULTURAL) return false;
  if (forTraffic && (e.tier < 2 || e.laneW < 2.4 || (e.flags & EF_MINOR_SERVICE) != 0)) return false;
  return true;
}

std::vector<Connector*> LaneGraph::route(const Lane& start, const Edge& goal, bool forTraffic, int goalDir) {
  std::unordered_map<int, double> dist;
  std::unordered_map<int, Connector*> prev;
  MinHeap heap;
  dist[start.id] = 0;
  heap.push(start.id, 0);
  const Lane* found = nullptr;
  while (heap.size() > 0) {
    int id = 0;
    double d = 0;
    heap.pop(id, d);
    auto known = dist.find(id);
    if (d > (known == dist.end() ? JS_INF : known->second)) continue;
    const Lane* lane = lanes[id];
    if (lane->edge == &goal && lane != &start && (goalDir == 0 || lane->dir == goalDir)) {
      found = lane;
      break;
    }
    for (Connector* c : lane->out) {
      const Lane* next = c->to;
      if (next->edge != &goal && !LaneGraph::drivable(*next->edge, forTraffic)) continue;
      if (noMotorway && next->edge != &goal && next->edge->cls <= MOTORWAY_LINK) continue;
      // (round a road closed today, a little longer through a work zone)
      const double extra = wayCost ? wayCost(next->edge->id * 2 + (next->dir > 0 ? 0 : 1)) : 0;
      if (extra == JS_INF && next->edge != &goal) continue;
      const double v = jsMin(next->limit, 36);
      // Turning round is for when there is no other way: it needs a turning
      // circle or a three-point turn, and where the map ends (often a fast
      // rural road) it is a last resort.
      const double uturn = c->from->to->ctrl == CTRL_MAP_END ? 900 : 150;
      double turnCost = c->turn == TURN_LEFT ? 6 : c->turn == TURN_RIGHT ? 3 : c->turn == TURN_UTURN ? uturn : 0;
      // a long vehicle does not get round a hairpin at a junction: like turning round, a last resort –
      // and for everybody else it is an awkward way to go where there is another
      if (c->turn != TURN_STRAIGHT && c->turn != TURN_UTURN && hairpin(*c)) turnCost += longVehicle ? 400 : 120;
      // two left turns a few metres apart = turning round through a gap in the median
      if (c->turn == TURN_LEFT && lane->len < 15) {
        auto before = prev.find(lane->id);
        if (before != prev.end() && before->second->turn == TURN_LEFT) turnCost += 150;
      }
      // "Anlieger frei" and car-park shortcuts are not through routes
      const double through = next->edge != &goal && (next->edge->access == ACCESS_DESTINATION || (next->edge->flags & EF_MINOR_SERVICE) != 0) ? 240 : 0;
      const double nd = d + c->len / 6 + next->len / jsMax(v, 2) + turnCost + through + (extra == JS_INF ? 0 : extra);
      auto have = dist.find(next->id);
      if (nd < (have == dist.end() ? JS_INF : have->second)) {
        dist[next->id] = nd;
        prev[next->id] = c;
        heap.push(next->id, nd);
      }
    }
  }
  if (!found) return {};
  std::vector<Connector*> path;
  const Lane* cur = found;
  while (cur != &start) {
    auto c = prev.find(cur->id);
    if (c == prev.end()) return {};
    path.push_back(c->second);
    cur = c->second->from;
  }
  std::reverse(path.begin(), path.end());
  return path;
}

}  // namespace fs

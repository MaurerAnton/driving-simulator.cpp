// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/narrowings.ts @ v1.7.7
#include "sim/narrowings.h"

#include <algorithm>
#include <cmath>

#include "sim/collision.h"
#include "sim/rules.h"
#include "sim/traffic.h"

namespace fs {

namespace {

/** what a vehicle keeps from the kerb of the bed (m) */
constexpr double MARGIN = 0.25;
/** how comfortably a vehicle eases off for it (m/s²) */
constexpr double EASE = 1.5;

/** Distance along a lane of an edge station. */
double along(const Lane& lane, double s) { return (s - lane.s0) * lane.dir; }

/** Where the bed begins and ends for traffic on `lane` (distances along it). */
std::pair<double, double> spanOn(const Lane& lane, const Narrowing& n) {
  return lane.dir > 0 ? std::pair<double, double>{along(lane, n.s0), along(lane, n.s1)} : std::pair<double, double>{along(lane, n.s1), along(lane, n.s0)};
}

PolySample tmp;

}  // namespace

int sideFor(const Narrowing& n, int dir) {
  if (n.flags & BO_PAIRED) return 0;
  return n.side * dir < 0 ? 1 : -1;
}

double gapShift(const Lane& lane, const Narrowing& n, double hw) {
  const double lo = n.lo + hw + MARGIN;
  const double hi = n.hi - hw - MARGIN;
  const double t = lo <= hi ? clamp(lane.off, lo, hi) : (n.lo + n.hi) / 2;
  return (t - lane.off) * lane.dir;
}

double Narrowings::shift(const Lane& lane, double mid, double hw, double hl) {
  if (!lane.edge->narrow) return 0;
  double out = 0;
  for (const Narrowing& n : *lane.edge->narrow) {
    const double full = gapShift(lane, n, hw);
    if (std::abs(full) < 0.01) continue;
    const auto [a, b] = spanOn(lane, n);
    const double inA = a - hl - 0.5;
    const double outB = b + hl + 0.3;
    if (mid <= inA - NARROW_RAMP || mid >= outB + NARROW_RAMP) continue;
    const double w = mid < inA ? smoothstep(inA - NARROW_RAMP, inA, mid) : mid > outB ? 1 - smoothstep(outB, outB + NARROW_RAMP, mid) : 1;
    const double s = full * w;
    if (std::abs(s) > std::abs(out)) out = s;
  }
  return out;
}

void Narrowings::offset(const Lane& lane, double d, PolySample& p, double hw, double hl) {
  const double s = shift(lane, d, hw, hl);
  if (s == 0) return;
  p.x += p.tz * s;
  p.z -= p.tx * s;
}

bool Narrowings::passing(const Edge& edge, double s, int dir) {
  if (!edge.narrow) return false;
  for (const Narrowing& n : *edge.narrow)
    if (s > n.s0 - NARROW_RAMP - 4 && s < n.s1 + NARROW_RAMP + 4 && sideFor(n, dir) == 1) return true;
  return false;
}

NarrowGate Narrowings::gate(const Lane& lane, double front, double v, double length, const TrafficCar* self) {
  NarrowGate out;
  if (!lane.edge->narrow) return out;
  for (const Narrowing& n : *lane.edge->narrow) {
    if (!n.single) continue;
    const auto [a, b] = spanOn(lane, n);
    if (front - length > b + 0.5 || a - front > 150) continue;
    // slowly through it: down to 20 km/h by the line where one waits
    const double wait = a - NARROW_WAIT;
    if (front < b) out.cap = std::min(out.cap, std::sqrt(NARROW_SPEED * NARROW_SPEED + 2 * EASE * std::max(0.0, wait - front)));
    // once past the line it is in it: on through
    if (front > wait + 0.3) continue;
    const int mine = sideFor(n, lane.dir);
    const double myWait = wait - front;
    const int64_t myId = self ? self->id : -1;
    /** how long it would take to be through and out (s) */
    const double myClear = (b + length - front) / NARROW_SPEED;
    /** how long until it is at its line (s) */
    const double myLine = std::max(0.0, myWait) / std::max(v, 1.0);
    bool hold = false;
    for (const Other& o : oncoming(lane, n, self)) {
      if (o.rear >= o.exit) continue;
      // somebody coming the other way is in it (past its own line, not yet out): nobody drives in
      if (o.wait < -0.3) hold = true;
      else if (o.wait + NARROW_WAIT < 80 && (o.v > 1 || o.wait < 2)) {
        // the build-out on our side: whoever comes the other way and would be at the gap before we are through goes first (§ 6 StVO)
        if (mine == 1) hold = (o.wait + NARROW_WAIT) / std::max(o.v, 1.0) < myClear + 1.5;
        else if (mine == 0) {
          // on both sides: whoever is there first
          const double oLine = std::max(0.0, o.wait) / std::max(o.v, 1.0);
          hold = oLine < myLine - 0.3 || (std::abs(oLine - myLine) <= 0.3 && o.id < myId);
        }
      }
      if (hold) break;
    }
    if (hold && myWait < out.stop) {
      out.stop = std::max(0.0, myWait);
      out.why = "oncoming";
    }
  }
  return out;
}

std::vector<Narrowings::Other>& Narrowings::oncoming(const Lane& lane, const Narrowing& n, const TrafficCar* self) {
  std::vector<Other>& out = others;
  out.clear();
  const LaneGraph::EdgeLanes* el = road.graph.edgeLanes[lane.edge->id];
  if (!el) return out;
  for (const Lane* l2 : lane.dir > 0 ? el->back : el->fwd) {
    const std::vector<TrafficCar*>* cars = road.carsOn(*l2);
    if (!cars) continue;
    const auto [a, b] = spanOn(*l2, n);
    for (const TrafficCar* c : *cars) {
      if (c == self) continue;
      const double f = c->s + c->length / 2;
      out.push_back(Other{a - NARROW_WAIT - f, f - c->length, b, c->v, c->id});
    }
  }
  const PlayerState* p = player;
  if (self && p && !p->onFoot) {
    const Edge& e = *lane.edge;
    const double mid = (n.s0 + n.s1) / 2;
    samplePolyline(e.pl, mid, tmp);
    if (std::abs(p->x - tmp.x) < 80 && std::abs(p->z - tmp.z) < 80 && std::abs(p->y - tmp.y) < 4) {
      projectOnPolyline(e.pl, p->x, p->z, proj);
      if (std::abs(proj.off) < e.width / 2 + 0.5) {
        // (which way it goes along the road)
        samplePolyline(e.pl, proj.s, tmp);
        const double fwd = -std::sin(p->yaw) * tmp.tx - std::cos(p->yaw) * tmp.tz;
        const double dir = p->speed >= 0 ? jsSign(fwd) : -jsSign(fwd);
        if (dir == -lane.dir) {
          // (its front along the way it goes, the entry and the end of the bed for it)
          const double f = proj.s * dir + p->length / 2;
          const double entry = dir > 0 ? n.s0 : -n.s1;
          const double exit = dir > 0 ? n.s1 : -n.s0;
          out.push_back(Other{entry - NARROW_WAIT - f, f - p->length, exit, std::abs(p->speed), -1});
        }
      }
    }
  }
  return out;
}

void Narrowings::judge(RuleMonitor& rules, const PlayerSnapshot& p, const Edge& edge, double s, int dir) {
  if (!edge.narrow) {
    prevFront.reset();
    return;
  }
  const double front = s + dir * (p.front ? *p.front : p.length / 2);
  if (prevFront && prevFront->edge == &edge && prevFront->dir == dir && rules.time - prevFront->t < 0.6 && p.speed > 0.3) {
    const Front prev = *prevFront;
    for (const Narrowing& n : *edge.narrow) {
      if (!n.single || sideFor(n, dir) != 1) continue;
      const double entry = dir > 0 ? n.s0 : n.s1;
      if (!((prev.s - entry) * dir < 0 && (front - entry) * dir >= 0)) continue;
      const LaneGraph::EdgeLanes* lanes = road.graph.edgeLanes[edge.id];
      const Lane* l = nullptr;
      if (lanes) {
        const std::vector<Lane*>& list = dir > 0 ? lanes->fwd : lanes->back;
        if (!list.empty()) l = list[0];
      }
      if (!l) continue;
      // somebody coming the other way past its line (in the gap, or moving into it)
      for (const Other& o : oncoming(*l, n, nullptr)) {
        if (o.wait < 0 && o.rear < o.exit) {
          rules.noteAt("narrowYield", "minor", p.x, p.z, {}, 10);
          break;
        }
      }
    }
  }
  prevFront = Front{&edge, dir, front, rules.time};
}

void narrowCollidersOf(const Edge& e, std::vector<float>& segs, std::vector<double>& discs) {
  if (!e.narrow) return;
  PolySample p;
  const double half = e.width / 2;
  for (const Narrowing& n : *e.narrow) {
    const double o0 = n.side * (half - n.depth);
    const double o1 = n.side * half;
    const int k = static_cast<int>(std::max(1.0, std::ceil((n.s1 - n.s0) / 3)));
    double px = 0;
    double pz = 0;
    for (int i = 0; i <= k; i++) {
      samplePolyline(e.pl, n.s0 + ((n.s1 - n.s0) * i) / k, p);
      const double x = p.x + p.tz * o0;
      const double z = p.z - p.tx * o0;
      if (i > 0) segs.insert(segs.end(), {static_cast<float>(px), static_cast<float>(pz), static_cast<float>(x), static_cast<float>(z), static_cast<float>(p.y)});
      if (i == 0 || i == k)
        segs.insert(segs.end(), {static_cast<float>(x), static_cast<float>(z), static_cast<float>(p.x + p.tz * o1), static_cast<float>(p.z - p.tx * o1), static_cast<float>(p.y)});
      px = x;
      pz = z;
    }
    if (n.flags & BO_BEACON) {
      samplePolyline(e.pl, (n.s0 + n.s1) / 2, p);
      const double o = n.side * (half - n.depth + 0.25);
      discs.insert(discs.end(), {p.x + p.tz * o, p.y, p.z - p.tx * o, 0.08});
    }
  }
}

void narrowCollideOne(SceneryCollider& collider, const Edge& e, bool on) {
  if (!e.narrow || e.narrow->empty()) return;
  const std::string key = "narrow:" + std::to_string(e.id);
  collider.remove(key);
  if (!on) return;
  std::vector<float> segs;
  std::vector<double> discs;
  narrowCollidersOf(e, segs, discs);
  CollisionSource src;
  src.key = key;
  src.segments = &segs;
  src.discs = discs;
  collider.add(src);
}

}  // namespace fs

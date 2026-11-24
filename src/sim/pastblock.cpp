// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/pastblock.ts @ v1.7.7
#include "sim/pastblock.h"

#include <algorithm>
#include <cmath>

#include "sim/jsmath.h"
#include "sim/traffic.h"
#include "world/types.h"

namespace fs {

namespace {
/** below this (m/s) a thing counts as standing */
const double STILL = 0.3;
/** how far a stop line, a light ahead has to be for the car to go round (m) */
const double LINE_CLEAR = 8;
const double LIGHT_CLEAR = 60;
}  // namespace

std::shared_ptr<PastState> newPast() { return std::make_shared<PastState>(); }

double offsetAt(const PastState& p, double dist) {
  const double most = RATE * jsMax(0, dist);
  return p.off + clamp(p.to - p.off, -most, most);
}

PastBlocks::PastBlocks(PastRoad& road_) : road(road_) {}

void PastBlocks::boxes(const std::vector<TrafficCar*>& cars, std::vector<PlayerState*>& out) {
  listed.clear();
  for (TrafficCar* c : cars) {
    const std::shared_ptr<PastState>& p = c->past;
    if (!p || std::abs(p->off) < 0.3) continue;
    PlayerState& b = p->box;
    b.x = c->x;
    b.y = c->y;
    b.z = c->z;
    b.yaw = c->yaw;
    b.speed = c->v;
    b.length = c->length;
    b.width = c->width;
    out.push_back(&b);
    listed.push_back(p);
  }
}

PastBlocks::Lead PastBlocks::lead(TrafficCar& car, double horizon) {
  Lead out;
  out.gap = car.past && car.past->hold ? 0.3 : JS_INF;
  out.v = 0;
  for (TrafficCar* op : road.cars) {
    TrafficCar& o = *op;
    if (&o == &car || std::abs(o.x - car.x) > horizon + 10 || std::abs(o.z - car.z) > horizon + 10 || std::abs(o.y - car.y) > 4) continue;
    box.x = o.x;
    box.y = o.y;
    box.z = o.z;
    box.yaw = o.yaw;
    box.speed = o.v;
    box.length = o.length;
    box.width = o.width;
    const double gap = road.gapTo(car, box, horizon);
    if (gap >= out.gap) continue;
    out.gap = gap;
    out.v = jsMax(0, o.v * (jsSin(o.yaw) * jsSin(car.yaw) + jsCos(o.yaw) * jsCos(car.yaw)));
  }
  return out;
}

void PastBlocks::look(TrafficCar& car, double dt, double gap, bool held, const PlayerState* player, bool self) {
  std::shared_ptr<PastState> p = car.past;
  if (!p) {
    if (car.v > STILL || gap > NEAR || held || !car.lane || car.side != 0 || car.em || car.merge) return;
    p = car.past = newPast();
  }
  p->t += dt;
  if (p->phase == "wait") {
    if (car.v > 0.6 || held || !car.lane || gap > NEAR + 2 || car.merge) {
      car.past = nullptr;
      return;
    }
    if (p->t < PATIENCE) return;
    p->look -= dt;
    if (p->look > 0) return;
    p->look = 0.5 + static_cast<double>(car.id % 5) * 0.07;
    const std::optional<PastPlan> plan_ = plan(car, player, self);
    if (!plan_) return;
    if (plan_->kind == "lane" && !self) {
      // (the traffic's own moving over: src/sim/traffic moveOver)
      car.merge = plan_->lane;
      car.past = nullptr;
      return;
    }
    p->phase = "out";
    p->t = 0;
    p->kind = plan_->kind;
    p->to = plan_->to;
    p->end = plan_->end;
    p->cap = plan_->cap;
    p->lane = car.lane;
    p->hold = false;
  } else if (p->phase == "out") {
    if (car.lane != p->lane) return drop(car);
    // something came along on the way it goes round on: wait for it, half out – or, just begun, back
    if (p->kind == "oncoming" && (p->look -= dt) <= 0) {
      p->look = 0.4;
      p->hold = !clearFor(car, p->to, p->end, player, self);
      if (p->hold && std::abs(p->off) < 0.3) p->to = 0;
    }
    if (p->to == 0) {
      p->phase = "back";
      p->hold = false;
      p->t = 0;
    } else if (std::abs(p->off - p->to) < 0.05) {
      p->phase = "by";
      p->t = 0;
      p->hold = false;
    } else if (p->t > 25) {
      giveUp(*p);
    }
  } else if (p->phase == "by") {
    if (car.lane != p->lane) return drop(car);
    if (car.s >= p->end) {
      p->phase = "back";
      p->to = 0;
      p->t = 0;
    } else if (p->t > 30 && car.v < STILL) {
      giveUp(*p);
    }
  } else if (p->phase == "back") {
    if (car.lane != p->lane) return drop(car);
    if (p->off == 0) {
      car.past = nullptr;
      return;
    }
  }
  // the indicator: out to the side it goes, back the other way
  if (!self) {
    const double side = p->phase == "back" ? -jsSign(p->off) : jsSign(p->to);
    car.blinker = side > 0 ? -1 : side < 0 ? 1 : 0;
  }
}

void PastBlocks::giveUp(PastState& p) {
  p.phase = "back";
  p.to = 0;
  p.t = 0;
  p.hold = false;
}

void PastBlocks::drop(TrafficCar& car) { car.past = nullptr; }

void PastBlocks::slide(TrafficCar& car, double ds, double dt) {
  const std::shared_ptr<PastState>& p = car.past;
  if (!p) return;
  const double before = p->off;
  double most = RATE * ds;
  if (p->phase == "back" && car.v < STILL) most = jsMax(most, 0.35 * dt);
  p->off += clamp(p->to - p->off, -most, most);
  if (std::abs(p->off - p->to) < 0.01) p->off = p->to;
  p->turn = ds > 0.001 ? (p->off - before) / ds : 0;
}

// --- deciding ----------------------------------------------------------------------

bool PastBlocks::gather(TrafficCar& car, const PlayerState* player, bool self) {
  Lane* lane = car.lane;
  if (!lane) return false;
  smp = lanePoint(*lane, car.s, smp);
  const double ox = smp.x;
  const double oz = smp.z;
  const double tx = smp.tx;
  const double tz = smp.tz;
  things.clear();
  const double R = 260;
  const auto add = [&](const void* who, double x, double y, double z, double yaw, double length, double width, double speed, bool block, bool permanent, bool person) {
    const double dx = x - ox;
    const double dz = z - oz;
    if (std::abs(dx) > R || std::abs(dz) > R || std::abs(y - car.y) > 4) return;
    const double fx = -jsSin(yaw);
    const double fz = -jsCos(yaw);
    const double cos = fx * tx + fz * tz;
    const double sin = fx * tz - fz * tx;
    Thing t;
    t.who = who;
    t.a = dx * tx + dz * tz;
    t.l = dx * tz - dz * tx;
    t.hl = (std::abs(cos) * length + std::abs(sin) * width) / 2;
    t.hw = (std::abs(sin) * length + std::abs(cos) * width) / 2;
    t.v = speed * cos;
    t.block = block && std::abs(speed) < STILL;
    t.permanent = permanent;
    t.person = person;
    t.part = false;
    things.push_back(t);
  };
  for (const TrafficCar* op : road.cars) {
    const TrafficCar& o = *op;
    if (&o == &car) continue;
    // a car of the traffic is gone round when it has broken down or crashed, or is a bus at its stop – not when it waits in a queue
    const bool broken = o.crashed > 0;
    add(&o, o.x, o.y, o.z, o.yaw, o.length, o.width, o.v, broken || (o.bus ? o.bus->dwell : 0) > 0, o.crashed > 30, false);
  }
  // (the player's car standing in the lane – not for the autopilot, which is the player's car)
  if (player && !self && std::abs(player->y - car.y) < 4)
    add(player, player->x, player->y, player->z, player->yaw, player->length, player->width, player->speed, !player->onFoot, false, player->onFoot == true);
  const std::vector<PlayerState*>& obstacles = road.obstacles;
  const PlayerState* own = car.past ? &car.past->box : nullptr;
  for (const PlayerState* o : road.blockList()) {
    if (o == own) continue;
    const bool game = std::find(obstacles.begin(), obstacles.end(), o) != obstacles.end();
    // a parked vehicle, the player's trailer: gone round; people, bicycles, fences and barriers: not
    const bool vehicle = game && (o->parked == true || (o->own == true && !self));
    add(o, o->x, o->y, o->z, o->yaw, o->length, o->width, o->speed, vehicle, o->parked == true, !vehicle && o->length < 2.2);
  }
  return true;
}

std::optional<PastPlan> PastBlocks::plan(TrafficCar& car, const PlayerState* player, bool self) {
  Lane* lane = car.lane;
  if (!lane || car.side != 0 || car.em) return std::nullopt;
  if (!gather(car, player, self)) return std::nullopt;
  const double half = lane->width / 2;
  const double front = car.length / 2;
  // what holds it up: the nearest thing ahead in its lane
  Thing* first = nullptr;
  for (Thing& t : things) {
    if (t.a - t.hl < front - 0.5 || t.a - t.hl > front + NEAR) continue;
    if (std::abs(t.l) > half + t.hw - 0.3) continue;
    if (!first || t.a - t.hl < first->a - first->hl) first = &t;
  }
  if (!first || !first->block) return std::nullopt;
  // … together with whatever stands right in front of it and may be gone round as well (a truck and its trailer)
  const double rear = first->a - first->hl;
  double fore = first->a + first->hl;
  double lo = first->l - first->hw;
  double hi = first->l + first->hw;
  bool permanent = first->permanent;
  first->part = true;
  {
    bool grown = true;
    for (int n = 0; grown && n < 4; n++) {
      grown = false;
      for (Thing& t : things) {
        if (t.part || !t.block || std::abs(t.l) > half + t.hw - 0.3) continue;
        if (t.a - t.hl > fore + 3 || t.a + t.hl < rear) continue;
        t.part = true;
        fore = jsMax(fore, t.a + t.hl);
        lo = jsMin(lo, t.l - t.hw);
        hi = jsMax(hi, t.l + t.hw);
        permanent = permanent && t.permanent;
        grown = true;
      }
    }
  }
  if (fore - rear > 40) return std::nullopt;
  // where it is past: its rear 1.5 m beyond the thing's front – and room in its lane there to come back into
  const double end = car.s + fore + 1.5 + front;
  const double room = fore + car.length + 6;
  // not in a junction, nor coming back just before one; not where a light ahead holds the traffic
  if (car.s + room + LINE_CLEAR > lane->stopAt) return std::nullopt;
  if (lane->armTo->sg >= 0 && lane->to->sc >= 0 && lane->stopAt - (car.s + fore) < LIGHT_CLEAR) return std::nullopt;
  for (const Thing& t : things) {
    if (t.part || std::abs(t.l) > half + t.hw - 0.2) continue;
    if (t.a + t.hl > fore && t.a - t.hl < room && !(t.v > car.v + 3)) return std::nullopt;
  }
  // not where roadworks are, nor over a crossing
  RoadEvents* re = road.roadEvents;
  for (double d = -front; d <= room; d += 4) {
    const double s = car.s + d;
    if (re && (re->blocked(*lane, s) || re->shift(*lane, s) != 0)) return std::nullopt;
  }
  for (const CrossingJson& x : lane->edge->xings) {
    const double pos = lane->dir > 0 ? x.s - lane->s0 : lane->s0 - x.s;
    if (pos > car.s - front - 3 && pos < car.s + room + 3) return std::nullopt;
  }
  const double w = car.width / 2;
  const double left = hi + w + 0.4;
  const double right = lo - w - 0.4;
  const Edge& e = *lane->edge;
  // 1) within its own lane, where the thing stands far enough to the side
  for (const double to : {left, right}) {
    if (std::abs(to) + w > half + 0.15) continue;
    if (free(car, to, room, 3)) return PastPlan{"squeeze", to, nullptr, end, 3};
  }
  // 2) a lane beside going the same way
  static const std::vector<Lane*> none;
  const LaneGraph::EdgeLanes* el = road.graph.edgeLanes[e.id];
  const std::vector<Lane*>& mine = el ? (lane->dir > 0 ? el->fwd : el->back) : none;
  for (const int k : {lane->idx + 1, lane->idx - 1}) {
    Lane* other = k >= 0 && static_cast<size_t>(k) < mine.size() ? mine[static_cast<size_t>(k)] : nullptr;
    if (!other) continue;
    const double at = (other->off - lane->off) * lane->dir;
    if (at + other->width / 2 > lo && at - other->width / 2 < hi) continue;
    if (re && !re->laneOpen(*other, car.s + front)) continue;
    if (!road.roomOn(*other, car.s, car.v, car.length)) continue;
    if (self && !free(car, at, room, 5)) continue;
    return PastPlan{"lane", at, other, end, 5};
  }
  // 3) over the oncoming lane of a road with one lane each way
  const std::vector<Lane*>& theirs = el ? (lane->dir > 0 ? el->back : el->fwd) : none;
  const bool twoWay = mine.size() == 1 && theirs.size() == 1;
  const bool solid = (e.flags & EF_SOLID_CENTER) != 0;
  if (twoWay && (!solid || permanent) && !(e.flags & (EF_ROUNDABOUT | EF_DUAL)) && e.cls > MOTORWAY_LINK && left - w < half + theirs[0]->width - 0.3) {
    if (free(car, left, room, self ? 2.7 : 5)) return PastPlan{"oncoming", left, nullptr, end, self ? 2.7 : 5};
  }
  // 4) half onto the verge: outside town, no pavement there, not on a bridge, nothing else there
  const double pavement = lane->dir > 0 ? e.swR : e.swL;
  if (lane->idx == 0 && pavement <= 0 && !(e.flags & (EF_URBAN | EF_BRIDGE | EF_TUNNEL)) && -right < half + e.margin + 0.9 && verge(car, right, room)) {
    if (free(car, right, room, 2)) return PastPlan{"verge", right, nullptr, end, 2};
  }
  return std::nullopt;
}

bool PastBlocks::free(const TrafficCar& car, double to, double until, double cap) const {
  const double w = car.width / 2;
  const double v = jsMax(car.v, cap * 0.6);
  const double T = (until + car.length / 2) / v + 2;
  for (const Thing& t : things) {
    if (t.part) continue;
    const double room = t.hw + w + (t.person ? 1.0 : 0.35);
    // (from the middle of the lane out to `to`: the whole way across)
    const double lo = jsMin(0, to) - room;
    const double hi = jsMax(0, to) + room;
    if (t.l < lo || t.l > hi) continue;
    // beside or behind the car in its own lane: not in the way out
    if (t.a + t.hl < -car.length / 2 + 0.5) continue;
    if (t.a - t.hl < until + 3) {
      // in the stretch: only one going away fast does not matter
      if (t.v > car.v + 3 && t.a > car.length) continue;
      return false;
    }
    // further on and coming towards it: it must not get there before the car is back
    if (std::abs(t.l - to) < room && t.v < -0.5 && (t.a - t.hl - until) / -t.v < T + 2.5) return false;
  }
  return true;
}

bool PastBlocks::clearFor(TrafficCar& car, double to, double end, const PlayerState* player, bool self) {
  if (!gather(car, player, self)) return false;
  // (the thing it goes round is not in the way: everything beside the middle of its lane counts only by the way out)
  for (Thing& t : things)
    if (t.block && std::abs(t.l) < (car.lane ? car.lane->width : 3) / 2) t.part = true;
  return free(car, to, end - car.s + car.length + 4, 4);
}

bool PastBlocks::verge(const TrafficCar& car, double to, double until) {
  const Lane* lane = car.lane;
  if (!lane) return false;
  const double out = to - car.width / 2;
  for (double d = -car.length / 2; d <= until; d += 3) {
    smp = lanePoint(*lane, jsMin(lane->len, car.s + d), smp);
    const double x = smp.x + smp.tz * out;
    const double z = smp.z - smp.tx * out;
    if (road.world.surfaceAt(x, z, car.y + 0.5, hit) && (hit.node != nullptr || hit.zone == 1 || hit.edge != lane->edge)) return false;
  }
  return true;
}

}  // namespace fs

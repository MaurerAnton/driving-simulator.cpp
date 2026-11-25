// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/emergency.ts @ v1.7.7
#include "sim/emergency.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

#include "sim/rules.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "world/types.h"

namespace fs {

namespace {

/** The paint of each kind (what the record of a drive keeps of it: the models carry their own livery). */
constexpr std::array<int, 4> EM_PAINT = {0xc9ced3, 0xf3f3ef, 0xb3201c, 0xf3f3ef};

/** how far ahead an emergency vehicle looks along its way (m) */
constexpr double REACH = 260;
/** the others wait at a junction it crosses from this many seconds before it gets there */
constexpr double HOLD_T = 9;

/** Where the middle of a lane is across its road, seen in its own direction of travel (m, + left of the road's middle). */
double latOf(const Lane& lane) { return lane.off * lane.dir; }

}  // namespace

Emergencies::Emergencies(Traffic& traffic_, double every_) : traffic(traffic_), every(every_ > 0 ? every_ : 0) { wait = every > 0 ? every : 70 + rand() * 80; }

// --- every step, before the cars move ------------------------------------------------

void Emergencies::update(double dt, const PlayerState& player_) {
  time += dt;
  player = &player_;
  const std::vector<TrafficCar*>& cars = traffic.cars;
  // (the original keeps where a car comes to rest in a WeakMap, and holds the cars by reference: here what is kept of
  // cars no longer there goes)
  if (!holds.empty() || pj.car) {
    std::unordered_set<TrafficCar*> here(cars.begin(), cars.end());
    for (auto it = holds.begin(); it != holds.end();) it = here.count(it->first) ? std::next(it) : holds.erase(it);
    if (pj.car && !here.count(pj.car)) pj.car = nullptr;
  }
  // the end of a call; police cars on patrol among the new cars
  int calls = 0;
  int64_t maxId = seenId;
  for (TrafficCar* car : cars) {
    if (car->id > seenId) {
      patrol(*car);
      maxId = std::max(maxId, car->id);
    }
    EmergencyCar* em = car->em.get();
    if (!em || !em->call) continue;
    em->t += dt;
    if (arrived(*car)) endCall(*car);
    else calls++;
  }
  seenId = maxId;
  // a new call now and then (not while one is still under way)
  if (autoCalls && traffic.targetCount > 0) {
    wait -= dt;
    if (wait <= 0) {
      if (calls == 0 && call(player_)) wait = every != 0 ? every : interval(player_);
      else wait = every != 0 ? std::min(every, 15.0) : 15;
    }
  }
  // the way ahead of each one on its call, what is in it – and how each of the others makes way
  runs.clear();
  for (TrafficCar* car : cars)
    if (car->em && car->em->call) runs.push_back(runOf(*car));
  ways.clear();
  for (Run& run : runs) clearWay(run, dt);
  for (TrafficCar* car : cars) {
    if (car->em && car->em->call) continue;
    const Way* w = !runs.empty() ? makeWay(*car) : nullptr;
    if (!w || w->stop == JS_INF) holds.erase(car);
  }
  // everybody moves across the lane to where it wants to be
  for (TrafficCar* car : cars) slideAcross(*car, dt);
  watchPlayer(player_);
}

/** The way of an emergency vehicle ahead of it: the lanes and paths through junctions it will drive along. */
Emergencies::Run Emergencies::runOf(TrafficCar& car) {
  const EmergencyCar& em = *car.em;
  Run run;
  run.car = &car;
  double at = -car.s;
  Lane* lane = car.lane;
  Connector* conn = car.conn;
  // (the connectors to come: on a lane the next one, then those of its way)
  int k = car.lane ? -1 : 0;
  const double v = std::max(car.v, 4.0);
  for (int hop = 0; hop < 16 && at < REACH; hop++) {
    if (conn) {
      run.pieces.push_back(Piece{nullptr, conn, at});
      run.nodes.push_back(RunNode{conn->node, at, std::max(0.0, at - car.length / 2) / v});
      at += conn->len;
      lane = conn->to;
      conn = nullptr;
    } else if (lane) {
      run.pieces.push_back(Piece{lane, nullptr, at});
      at += lane->len;
      Connector* c = k < 0 ? car.next : static_cast<size_t>(k) < em.route.size() ? em.route[static_cast<size_t>(k)] : nullptr;
      k++;
      if (!c || c->from != lane) break;
      conn = c;
      lane = nullptr;
    } else break;
  }
  return run;
}

/**
 * Where something on the road is as the emergency vehicle of `run` sees it
 * – on a lane of a road on its way (either direction) or on a path of its
 * way through a junction; false if it is elsewhere.  `s`: its middle along
 * the lane or path; `side`: how far left of the middle of its lane it is.
 */
bool Emergencies::see(const Run& run, const Lane* lane, const Connector* conn, double s, double side, Seen& out) {
  const std::vector<Piece>& pieces = run.pieces;
  for (size_t i = 0; i < pieces.size(); i++) {
    const Piece& p = pieces[i];
    if (lane && p.lane && p.lane->edge == lane->edge) {
      // (the lanes of a road are measured alike: by the stations of the road)
      const bool same = lane->dir == p.lane->dir;
      out.along = p.at + (lane->s0 + lane->dir * s - p.lane->s0) * p.lane->dir;
      out.lat = (lane->off - p.lane->off) * p.lane->dir + (same ? side : -side);
      out.same = same;
      out.piece = i;
      return true;
    }
    if (conn && p.conn == conn) {
      out.along = p.at + s;
      out.lat = side;
      out.same = true;
      out.piece = i;
      return true;
    }
  }
  return false;
}

/**
 * What lies ahead of an emergency vehicle: the nearest vehicle in its way
 * that it cannot pass (beside it there is not room enough), how fast it
 * may squeeze past those it can, and where across its lane it wants to be –
 * in the corridor on a road with several lanes, else as far to the left as
 * the cars at the right need and the road and the oncoming traffic allow.
 */
void Emergencies::clearWay(Run& run, double dt) {
  TrafficCar& car = *run.car;
  EmergencyCar& em = *car.em;
  const double half = car.width / 2;
  const double side = car.side;
  Seen& seen_ = seen;
  double gap = JS_INF;
  double gapV = 0;
  double passV = JS_INF;
  TrafficCar* blocker = nullptr;
  Lane* lane = car.lane;
  const std::vector<Lane*>* lanes = lane ? &carriageway(*lane) : nullptr;
  // (where it wants to be across a road with one lane in its direction: past the cars at the right, as far as there is
  // room – going by those on the piece of road it is on; in a junction by those on its path there and just beyond)
  double need = lane ? 0.35 : 0;
  double room = lane ? lane->edge->width / 2 - 0.2 - half - latOf(*lane) : 1.6;
  const size_t near = lane ? 0 : 1;
  const auto look = [&](double len, double width, double v, TrafficCar* other) {
    const double reach = (car.length + len) / 2;
    const double g = seen_.along - reach;
    if (seen_.along < -reach || g > 150) return;
    const double clear_ = std::abs(seen_.lat - side) - half - width / 2;
    if (clear_ < 0.25) {
      // in its way: it stays behind (the player is seen by the traffic itself)
      if (other && seen_.along > 0 && g < gap) {
        gap = std::max(0.0, g);
        gapV = seen_.same ? v : 0;
        blocker = other;
      }
    } else if (clear_ < 1.3 && g < 12 + car.v * 1.5) {
      // squeezing past: slowly – a little faster than the one it passes, if that is moving
      passV = std::min(passV, std::max(7 + 6 * clear_, (seen_.same ? v : 0) + 4 + 4 * clear_));
    }
    // (from a little before it until its tail is past: beside it the room is needed most)
    if (seen_.along < -reach - 2 || seen_.piece > near) return;
    // (in a junction it does not swerve round anybody: it only keeps clear of whoever it is beside already)
    if (seen_.same) {
      if (g < (lane ? 60 : 1)) need = std::max(need, seen_.lat + width / 2 + 0.3 + half);
    } else if (g < 70) room = std::min(room, seen_.lat - width / 2 - 0.3 - half);
  };
  for (TrafficCar* o : traffic.cars) {
    if (o == &car || !see(run, o->lane, o->conn, o->s, o->side, seen_)) continue;
    look(o->length, o->width, o->v, o);
  }
  const PlayerState* p = player;
  if (p && !p->onFoot && p->lane && see(run, p->lane, nullptr, p->s.value_or(0), across(*p), seen_)) look(p->length, p->width, std::abs(p->speed), nullptr);
  // where across its lane
  double target = 0;
  if (lane && lanes) {
    if (lanes->size() >= 2) {
      // the corridor: between the left-most lane and the one beside it
      const Lane& left = *lanes->back();
      target = latOf(left) - left.width / 2 - latOf(*lane);
    } else target = std::max(0.0, std::min(need, room));
    // (coming up to a junction where it turns: back to the middle of its lane – unless it is passing somebody)
    if (car.next && car.next->turn != TURN_STRAIGHT && lane->len - car.s < 14 + car.v && need <= 0.35) target = 0;
  } else {
    // through a junction: back to the middle of its path – but not into somebody beside it
    target = std::max(0.0, std::min(need, room));
  }
  // never across into anybody beside it – whatever lane or path they are on, the player too: how far there is
  // room to either side, from where they are now
  double lo = -JS_INF;
  double hi = JS_INF;
  const double fx = -std::sin(car.yaw);
  const double fz = -std::cos(car.yaw);
  const auto beside = [&](double x, double z, double len, double width) {
    const double dx = x - car.x;
    const double dz = z - car.z;
    if (std::abs(dx) > 16 || std::abs(dz) > 16) return;
    if (std::abs(dx * fx + dz * fz) > (car.length + len) / 2 + 0.5) return;
    // (left of the way it faces, (fx, fz), is (fz, −fx))
    const double left = dx * fz - dz * fx;
    const double free = std::max(0.0, std::abs(left) - width / 2 - half - 0.2);
    if (left > 0) hi = std::min(hi, side + free);
    else lo = std::max(lo, side - free);
  };
  for (TrafficCar* o : traffic.cars)
    if (o != &car && std::abs(o->y - car.y) < 3) beside(o->x, o->z, o->length, o->width);
  if (p && !p->onFoot && std::abs(p->y - car.y) < 3) beside(p->x, p->z, p->length, p->width);
  target = lo > hi ? side : std::min(hi, std::max(lo, target));
  // held up behind somebody who cannot make room: after a while that one drives on, slowly, to clear the way
  TrafficCar* b = blocker;
  if (b && gap < 20) {
    if (em.stuckOn != b->id) {
      em.stuckOn = b->id;
      em.stuckT = 0;
    } else if (car.v < 3) em.stuckT += dt;
  } else {
    em.stuckOn = -1;
    em.stuckT = 0;
  }
  em.gap = gap;
  em.gapV = gapV;
  em.passV = passV;
  em.target = target;
}

/** How a car makes way for the emergency vehicles on their calls (null: it need not). */
const Emergencies::Way* Emergencies::makeWay(TrafficCar& car) {
  Way* way = nullptr;
  for (const Run& run : runs) {
    const Way* w_ = wayFor(run, car);
    if (!w_) continue;
    if (!way) {
      way = &ways[&car];
      way->cap = w_->cap;
      way->stop = w_->stop;
      way->side = w_->side;
      continue;
    }
    way->cap = std::min(way->cap, w_->cap);
    way->stop = std::min(way->stop, w_->stop);
    if (std::abs(w_->side) > std::abs(way->side)) way->side = w_->side;
  }
  return way;
}

/** How a car makes way for one emergency vehicle (null: not at all) – the answer is only good until the next call. */
const Emergencies::Way* Emergencies::wayFor(const Run& run, TrafficCar& car) {
  const TrafficCar& e = *run.car;
  const EmergencyCar& em = *e.em;
  w.cap = JS_INF;
  w.stop = JS_INF;
  w.side = 0;
  Seen& seen_ = seen2;
  // waiting at the junction it is about to cross (unless over the line already, or too close to stop)
  const auto hold = [&]() -> const Way* {
    Lane* lane = car.lane;
    if (!lane || !car.next) return nullptr;
    for (const RunNode& n : run.nodes) {
      if (n.node != lane->to) continue;
      // (on the motorway nobody stops where it merges: they make way as they go – nor in a roundabout, nor inside
      // a junction made of several: they would block it)
      if (n.eta > HOLD_T || n.d > 160 || n.node->ctrl == CTRL_MERGE || n.node->ctrl == CTRL_ROUNDABOUT || lane->len < 25 || (lane->edge->flags & EF_ROUNDABOUT)) return nullptr;
      const double d = lane->stopAt - car.s - car.length / 2;
      if (d < -1 || d < (car.v * car.v) / 9 - 1) return nullptr;
      w.stop = std::max(0.0, d);
      w.side = 0;
      return &w;
    }
    return nullptr;
  };
  if (see(run, car.lane, car.conn, car.s, car.side, seen_)) {
    const double reach = (e.length + car.length) / 2;
    const double g = seen_.along - reach;
    Lane& lane = car.lane ? *car.lane : *car.conn->to;
    const double limit = std::isfinite(lane.limit) ? lane.limit : 36;
    if (seen_.same) {
      // ahead of it on its way: aside – and slower, near it to a stop
      if (seen_.along < -reach - 10 || g > REACH) return nullptr;
      w.side = car.conn ? car.side : aside(lane, car, false);
      // (in a junction – in a roundabout, between the nodes of a junction made of several – out of it first)
      if (car.conn || lane.len < 25 || (lane.edge->flags & EF_ROUNDABOUT)) return &w;
      // it cannot pass, and has waited: the queue in front of it drives on, slowly, to clear the way
      if (em.stuckOn >= 0 && em.stuckT > 2.5 && seen_.along > 0 && g < 120) {
        w.cap = std::max(6.0, limit * 0.6);
        return &w;
      }
      // (on a motorway in flowing traffic: aside, but on)
      if (lane.edge->cls <= TRUNK_LINK && car.v > 12) {
        w.cap = limit * 0.7;
        return &w;
      }
      if (g < 70 || seen_.along <= 0 || car.v < 5) w.stop = stopHere(car);
      else w.cap = std::max(5.0, limit * 0.5);
      return &w;
    }
    // coming towards it: keep right and slow down – at a crawl where the road is narrow (or wait at the junction it crosses)
    if (seen_.along < -reach || g > 180) return nullptr;
    if (hold()) return &w;
    w.side = car.conn ? car.side : aside(lane, car, true);
    const bool narrow = carriageway(lane).size() == 1 && lane.edge->width < 7.6;
    w.cap = narrow && g < 50 ? 2.5 : std::max(5.0, limit * 0.5);
    return &w;
  }
  return hold();
}

/**
 * How far a car on `lane` moves across to make way (m, + left): from the
 * left-most lane of a road with several to the left, from every other one
 * to the right (`right`: to the right in any case) – as far as there is room.
 */
double Emergencies::aside(const Lane& lane, const TrafficCar& car, bool right) {
  const std::vector<Lane*>& lanes = carriageway(const_cast<Lane&>(lane));
  const bool toLeft = !right && lanes.size() >= 2 && lane.idx == static_cast<int>(lanes.size()) - 1;
  const Edge& e = *lane.edge;
  // (at the edge of the road the strip beside the lanes – and the kerb, half onto which it may go;
  // towards the middle of a road with traffic both ways only a little over the line)
  const double outer = toLeft ? (e.lanesF > 0 && e.lanesB > 0 ? 0.1 : e.margin + 0.2) : lane.idx == 0 ? e.margin + 0.45 : 0.15;
  const double pull = clamp((lane.width - car.width) / 2 + outer, 0.3, 1.4);
  return toLeft ? pull : -pull;
}

/**
 * How far ahead of its front a car that stops to make way comes to rest
 * (m): braking gently, before the line if it is coming up to a junction –
 * or, too close to the line for that, not at all (it drives through first).
 */
double Emergencies::stopHere(TrafficCar& car) {
  const void* on = car.lane ? static_cast<const void*>(car.lane) : static_cast<const void*>(car.conn);
  if (!on) return JS_INF;
  const double front = car.s + car.length / 2;
  auto it = holds.find(&car);
  if (it == holds.end() || it->second.on != on) {
    double s = front + std::max(2.0, (car.v * car.v) / (2 * 2.2));
    if (car.lane && s > car.lane->stopAt - 0.5) {
      const double line = car.lane->stopAt - 0.5;
      if (line - front < (car.v * car.v) / (2 * 4.5) - 0.5) return JS_INF;
      s = std::max(front, line);
    }
    it = holds.insert_or_assign(&car, Hold{on, s}).first;
  }
  return std::max(0.0, it->second.s - front);
}

/** Move a car across its lane towards where it wants to be – slowly, a little faster when it drives. */
void Emergencies::slideAcross(TrafficCar& car, double dt) {
  const EmergencyCar* em = car.em.get();
  double target = 0;
  if (em && em->call) target = em->target;
  else {
    const auto it = ways.find(&car);
    target = it != ways.end() ? it->second.side : 0;
  }
  const double now = car.side;
  if (now == 0 && target == 0) return;
  const double rate = em && em->call ? 0.9 + car.v * 0.05 : 0.35 + std::min(car.v, 12.0) * 0.08;
  const double next = now + clamp(target - now, -rate * dt, rate * dt);
  car.side = target == 0 && std::abs(next) < 0.01 ? 0 : next;
}

// --- what the traffic asks while a car takes its step ------------------------------

double Emergencies::desired(TrafficCar& car, double v0, double limit) {
  EmergencyCar* em = car.em.get();
  if (em && em->call) {
    // (in a bend, a roundabout or a junction it keeps the speed the others would take there)
    const bool free = v0 >= limit * car.temper - 1e-6;
    const double want = std::min(free ? limit + clamp(limit * 0.4, 4, 9) : v0, em->passV);
    em->want = want;
    return want;
  }
  const auto it = ways.find(&car);
  return it != ways.end() ? std::min(v0, it->second.cap) : v0;
}

double Emergencies::stopFor(TrafficCar& car) {
  const auto it = ways.find(&car);
  return it != ways.end() ? it->second.stop : JS_INF;
}

Emergencies::Lead Emergencies::lead(TrafficCar& car) {
  const EmergencyCar* em = car.em.get();
  return Lead{em ? em->gap : JS_INF, em ? em->gapV : 0};
}

double Emergencies::through(TrafficCar& car, const Connector& next, double distToLine, bool mustStop) {
  Lane* lane = car.lane;
  if (!lane || distToLine < -1.5) return JS_INF;
  const Node& node = *next.node;
  const Arm& arm = *lane->armTo;
  SignalSystem& signals = traffic.signals;
  bool care;
  if (arm.sg >= 0 && node.sc >= 0) {
    const int st = signals.get(node.sc, arm.sg);
    care = st != SIG_GREEN && !(next.turn == TURN_RIGHT && signals.arrow(&arm) == SIG_GREEN);
  } else if (node.ctrl == CTRL_MERGE) {
    // (onto the motorway it merges like everybody else: into a gap – nobody stops for it there)
    return mustStop ? -1 : JS_INF;
  } else {
    care = (arm.role != ROLE_PRIORITY && arm.role != ROLE_NONE) || node.ctrl == CTRL_RBL;
    if (!care)
      for (const Conflict& k : next.conflicts)
        if (k.yield) care = true;
  }
  // its own way is free: on as it is (what would hold an ordinary car there – the oncoming traffic when it turns
  // left on green, somebody in the junction – it takes with the same care: the others are waiting for it)
  if (!care && !mustStop) return JS_INF;
  if (distToLine < 30 && !clear(car, next)) return -1;
  return std::sqrt(4.5 * 4.5 + 2 * 2.5 * std::max(0.0, distToLine));
}

/** Is the junction an emergency vehicle is about to cross by `next` free – nobody in it, nobody coming who could not stop any more? */
bool Emergencies::clear(TrafficCar& car, const Connector& next) {
  if (!traffic.clearOf(const_cast<Connector&>(next))) return false;
  const Node& node = *next.node;
  for (const Conflict& k : next.conflicts) {
    const Connector* other = k.other;
    for (const TrafficCar* o : traffic.cars) {
      if (o == &car || o->lane != other->from || (o->next != nullptr && o->next != other)) continue;
      const double d = other->from->stopAt - o->s - o->length / 2;
      if (d > -1.5 && o->v > 1.5 && d < (o->v * o->v) / 7 + 4) return false;
    }
  }
  for (const Pedestrian* ped : traffic.peds())
    if (ped->state == "cross" && std::hypot(ped->x - node.x, ped->z - node.z) < 24) return false;
  const PlayerState* p = player;
  if (!p) return true;
  const double dp = std::hypot(p->x - node.x, p->z - node.z);
  if (p->onFoot) return !(dp < 14 && p->speed > 0.3);
  // the player coming into the junction from another side, too fast or too close to stop – or in it
  if (dp < 70 && p->lane && p->lane->to == &node && p->lane->armTo != (car.lane ? car.lane->armTo : nullptr)) {
    const double d = p->lane->stopAt - (p->s.value_or(0) + p->length / 2);
    const double v = std::abs(p->speed);
    if (d > -30 && v > 1 && d < (v * v) / 6 + 5) return false;
  }
  return true;
}

// --- calls ---------------------------------------------------------------------

TrafficCar* Emergencies::dispatch(int kind, Lane& lane, double s, const std::vector<Connector*>& route) {
  TrafficCar* car = traffic.addCar(lane, s);
  equip(*car, kind, true, route);
  std::vector<Connector*>& way = car->em->route;
  if (!way.empty() && way.front()->from == &lane) {
    car->next = way.front();
    way.erase(way.begin());
  }
  car->v = std::min(std::isfinite(lane.limit) ? lane.limit : 30, 14.0) * 0.9;
  return car;
}

/** Make a car of the traffic an emergency vehicle: its size, its paint, on a call or on patrol. */
void Emergencies::equip(TrafficCar& car, int kind, bool call_, std::vector<Connector*> route) {
  const EmSize& size = EM_SIZE[static_cast<size_t>(kind)];
  car.style = emStyle(kind, call_);
  car.length = size.length;
  car.width = size.width;
  car.color = EM_PAINT[static_cast<size_t>(kind)];
  car.temper = call_ ? 1 : 0.95;
  auto em = std::make_shared<EmergencyCar>();
  em->kind = kind;
  em->call = call_;
  em->last = route.empty() ? nullptr : route.back();
  em->route = std::move(route);
  em->t = 0;
  em->pitch = 0.97 + rand() * 0.06;
  em->phase = rand() * 3;
  em->gap = JS_INF;
  em->gapV = 0;
  em->passV = JS_INF;
  em->target = 0;
  em->want = 0;
  em->told = 0;
  em->faults = 0;
  em->stuckOn = -1;
  em->stuckT = 0;
  car.em = std::move(em);
}

/** Now and then a police car on patrol among the new cars of the traffic (one at a time). */
void Emergencies::patrol(TrafficCar& car) {
  if (car.em || car.bus || patrolShare <= 0 || rand() >= patrolShare) return;
  for (const TrafficCar* o : traffic.cars)
    if (o->em && !o->em->call) return;
  equip(car, EM_POLICE, false, {});
}

/** Has it got where it was called – a little beyond the end of its way (or lost its way, or been out too long)? */
bool Emergencies::arrived(TrafficCar& car) {
  const EmergencyCar& em = *car.em;
  if (em.t > 300) return true;
  const Connector* last = em.last;
  if (!last) return em.t > 25;
  // (off its way: the next connector does not lead to the next of its way)
  if (car.lane && car.next && !em.route.empty() && em.route[0]->from != car.next->to) return true;
  return car.lane == last->to && car.s > std::min(40.0, car.lane->len * 0.6);
}

/** The call is over: lights and siren off, and on with the traffic (a police car on patrol). */
void Emergencies::endCall(TrafficCar& car) {
  EmergencyCar& em = *car.em;
  em.call = false;
  em.route.clear();
  em.target = 0;
  car.style = emStyle(em.kind, false);
  car.temper = 0.92 + rand() * 0.06;
}

/** Seconds until the next call: every few minutes in town, more rarely outside. */
double Emergencies::interval(const PlayerState& p) {
  const bool urban = p.lane ? (p.lane->edge->flags & EF_URBAN) != 0 : false;
  return urban ? 150 + rand() * 120 : 300 + rand() * 240;
}

/** Police, ambulance, fire engine, the doctor's car – about as often as they are out. */
int Emergencies::pickKind() {
  const double r = rand();
  return r < 0.35 ? EM_POLICE : r < 0.7 ? EM_AMBULANCE : r < 0.87 ? EM_FIRE : EM_DOCTOR;
}

/**
 * A call near the player: one coming up from behind on the player's road,
 * one crossing the junction ahead, one coming towards the player – or a
 * police car on patrol near by gets the call; where none of that can be
 * (the player is not on a road), one somewhere near.  False if none could
 * be sent.
 */
bool Emergencies::call(const PlayerState& p) {
  const int kind = pickKind();
  Lane* lane = p.onFoot ? nullptr : p.lane;
  if (rand() < 0.3 && patrolCall(p)) return true;
  if (lane) {
    const double r = rand();
    const std::array<int, 3> order = r < 0.5 ? std::array<int, 3>{0, 1, 2} : r < 0.75 ? std::array<int, 3>{1, 0, 2} : std::array<int, 3>{2, 0, 1};
    for (const int k : order)
      if (k == 0 ? fromBehind(p, *lane, kind) : k == 1 ? fromSide(p, *lane, kind) : fromAhead(p, *lane, kind)) return true;
  }
  return somewhere(p, kind);
}

/** The lanes of `lane`'s road in its direction, right to left. */
const std::vector<Lane*>& Emergencies::carriageway(Lane& lane) {
  static thread_local std::vector<Lane*> one;
  const LaneGraph::EdgeLanes* el = traffic.graph.edgeLanes[lane.edge->id];
  if (el) return lane.dir > 0 ? el->fwd : el->back;
  one.assign(1, &lane);
  return one;
}

/** The lanes of `lane`'s road in the other direction, right to left. */
const std::vector<Lane*>& Emergencies::opposite(Lane& lane) {
  static const std::vector<Lane*> none;
  const LaneGraph::EdgeLanes* el = traffic.graph.edgeLanes[lane.edge->id];
  return el ? (lane.dir > 0 ? el->back : el->fwd) : none;
}

/** How the way goes on at the end of `lane` most likely: straight on, along the bigger road – never a U-turn, never onto a road an emergency vehicle should not take. */
Connector* Emergencies::onward(const Lane& lane) {
  Connector* best = nullptr;
  double bestScore = -JS_INF;
  for (Connector* c : lane.out) {
    if (c->turn == TURN_UTURN || !LaneGraph::drivable(*c->to->edge, true)) continue;
    const double score = (c->turn == TURN_STRAIGHT ? 3 : 0) + c->to->edge->tier * 0.5 + rand() * 1.5 - ((c->to->edge->flags & EF_ROUNDABOUT) ? 1 : 0);
    if (score > bestScore) {
      bestScore = score;
      best = c;
    }
  }
  return best;
}

/** The same backwards: how one most likely came to `lane`. */
Connector* Emergencies::inward(const Lane& lane) {
  Connector* best = nullptr;
  double bestScore = -JS_INF;
  for (Connector* c : lane.in) {
    if (c->turn == TURN_UTURN || !LaneGraph::drivable(*c->from->edge, true)) continue;
    const double score = (c->turn == TURN_STRAIGHT ? 3 : 0) + c->from->edge->tier * 0.5 + rand() * 1.5;
    if (score > bestScore) {
      bestScore = score;
      best = c;
    }
  }
  return best;
}

/** The way on from the end of `lane` for about `metres`, as connectors. */
std::vector<Connector*>& Emergencies::wayOn(Lane& lane, double metres, std::vector<Connector*>& out) {
  double d = 0;
  Lane* l = &lane;
  for (int i = 0; i < 40 && d < metres; i++) {
    Connector* c = onward(*l);
    if (!c) break;
    out.push_back(c);
    d += c->len + c->to->len;
    l = c->to;
  }
  return out;
}

/** Walk back from the end of `end` (its start lies `back` metres before the place aimed at) until `d` metres: the lane reached, how far along it, and the connectors from there. */
bool Emergencies::walkBack(Lane& end, double back, double d, Found& out) {
  std::vector<Connector*> path;
  Lane* at = &end;
  for (int i = 0; i < 24 && back < d; i++) {
    Connector* c = inward(*at);
    if (!c) return false;
    path.insert(path.begin(), c);
    at = c->from;
    back += c->len + at->len;
  }
  if (back < d) return false;
  out.lane = at;
  out.s = back - d;
  out.path = std::move(path);
  return true;
}

/** Is there room for an emergency vehicle to appear `s` metres along `lane` – nobody near on the road, and out of the player's sight? */
bool Emergencies::freeAt(const Lane& lane, double s, const PlayerState& p) {
  if (s < 6 || s > lane.len - 6) return false;
  for (const TrafficCar* o : traffic.cars)
    if (o->lane && o->lane->edge == lane.edge && std::abs(o->s - (lane.dir == o->lane->dir ? s : lane.len - s)) < 16) return false;
  lanePoint(lane, s, tmp);
  return std::hypot(tmp.x - p.x, tmp.z - p.z) > 120 && std::abs(tmp.y - p.y) < 40;
}

/** One coming up from behind along the player's road – it passes the player, who has to make way. */
bool Emergencies::fromBehind(const PlayerState& p, Lane& lane, int kind) {
  Found found;
  const double back = p.s ? *p.s : lane.len / 2;
  if (!walkBack(lane, back, 170 + rand() * 110, found) || !freeAt(*found.lane, found.s, p)) return false;
  std::vector<Connector*> route = found.path;
  wayOn(lane, 250 + rand() * 150, route);
  dispatch(kind, *found.lane, found.s, route);
  return true;
}

/** One crossing the junction ahead of the player about when the player gets there – who has to wait before it. */
bool Emergencies::fromSide(const PlayerState& p, Lane& lane, int kind) {
  const Node& node = *lane.to;
  const double dist = lane.stopAt - (p.s.value_or(0) + p.length / 2);
  if (dist < 25 || dist > 260 || node.arms.size() < 3 || node.ctrl == CTRL_MAP_END || node.stub) return false;
  const Arm* mine = lane.armTo;
  std::vector<Lane*> from;
  for (const Arm* arm : node.arms) {
    // (from the side: about at right angles to the player's way)
    if (arm == mine || std::abs(arm->dx * mine->dx + arm->dz * mine->dz) > 0.7) continue;
    for (Lane* l : traffic.graph.lanesInto(*arm))
      if (l->idx == 0 && LaneGraph::drivable(*l->edge, true)) from.push_back(l);
  }
  if (from.empty()) return false;
  Lane* into = from[static_cast<size_t>(std::floor(rand() * static_cast<double>(from.size())))];
  Connector* across_ = onward(*into);
  if (!across_) return false;
  // (it gets there a little before the player)
  const double d = clamp((dist / std::max(std::abs(p.speed), 7.0)) * 14, 110, 230);
  Found found;
  if (!walkBack(*into, into->len, d, found) || !freeAt(*found.lane, found.s, p)) return false;
  std::vector<Connector*> route = found.path;
  route.push_back(across_);
  wayOn(*across_->to, 250, route);
  dispatch(kind, *found.lane, found.s, route);
  return true;
}

/** One coming towards the player along the player's road. */
bool Emergencies::fromAhead(const PlayerState& p, Lane& lane, int kind) {
  const double d = 220 + rand() * 100;
  std::vector<Connector*> fwd;
  Lane* at = &lane;
  double ahead = lane.len - p.s.value_or(0);
  for (int i = 0; i < 24 && ahead < d; i++) {
    Connector* c = onward(*at);
    if (!c) return false;
    fwd.push_back(c);
    at = c->to;
    ahead += c->len + at->len;
  }
  if (ahead < d) return false;
  // on that road the right-most lane the other way, at the same place
  const std::vector<Lane*>& opp = opposite(*at);
  if (opp.empty()) return false;
  Lane* back = opp[0];
  const double s = ahead - d;
  // its way back towards the player: at each junction into the road the player's way came along
  std::vector<Connector*> route;
  Lane* cur = back;
  for (int i = static_cast<int>(fwd.size()) - 1; i >= 0; i--) {
    const Edge* edge = fwd[static_cast<size_t>(i)]->from->edge;
    Connector* c = nullptr;
    for (Connector* k : cur->out)
      if (k->to->edge == edge && k->turn != TURN_UTURN) {
        c = k;
        break;
      }
    if (!c) return false;
    route.push_back(c);
    cur = c->to;
  }
  if (!freeAt(*back, s, p)) return false;
  dispatch(kind, *back, s, wayOn(*cur, 250, route));
  return true;
}

/** A police car on patrol near the player gets a call: blue lights and siren on, and off it goes. */
bool Emergencies::patrolCall(const PlayerState& p) {
  for (TrafficCar* car : traffic.cars) {
    EmergencyCar* em = car->em.get();
    if (!em || em->call || em->kind != EM_POLICE || !car->lane || !car->next) continue;
    if (std::hypot(car->x - p.x, car->z - p.z) > 350) continue;
    std::vector<Connector*> route;
    wayOn(*car->next->to, 450, route);
    if (route.empty()) continue;
    em->call = true;
    em->last = route.back();
    em->route = std::move(route);
    em->t = 0;
    em->told = 0;
    em->faults = 0;
    car->style = emStyle(EM_POLICE, true);
    car->temper = 1;
    return true;
  }
  return false;
}

/** One on a road somewhere near – heard more than seen. */
bool Emergencies::somewhere(const PlayerState& p, int kind) {
  std::vector<Lane*> near;
  LaneGraph& graph = traffic.graph;
  for (Edge* e : traffic.world.allEdges) {
    const double x = (e->minX + e->maxX) / 2;
    const double z = (e->minZ + e->maxZ) / 2;
    const double d = std::hypot(x - p.x, z - p.z);
    if (d < 200 || d > 360 || !LaneGraph::drivable(*e, true) || e->len < 40 || (e->flags & EF_ROUNDABOUT)) continue;
    const LaneGraph::EdgeLanes* el = graph.edgeLanes[e->id];
    if (!el) continue;
    // (a lane that leads towards the player: it is heard coming, and passes near by)
    for (const std::vector<Lane*>* side : {&el->fwd, &el->back}) {
      for (Lane* l : *side) {
        if (l->idx != 0) continue;
        lanePoint(*l, l->len / 2, tmp);
        if ((p.x - tmp.x) * tmp.tx + (p.z - tmp.z) * tmp.tz > 0.3 * std::hypot(p.x - tmp.x, p.z - tmp.z)) near.push_back(l);
      }
    }
  }
  for (int tries = 0; tries < 6 && !near.empty(); tries++) {
    Lane* lane = near[static_cast<size_t>(std::floor(rand() * static_cast<double>(near.size())))];
    const double s = lane->len / 2;
    if (!freeAt(*lane, s, p)) continue;
    std::vector<Connector*> route;
    dispatch(kind, *lane, s, wayOn(*lane, 400, route));
    return true;
  }
  return false;
}

// --- the player ------------------------------------------------------------------

/** How far left of the middle of its lane the player is (m). */
double Emergencies::across(const PlayerState& p) {
  if (!p.lane) return 0;
  lanePoint(*p.lane, p.s.value_or(0), tmp);
  // (left of the direction of travel (tx, tz) is (tz, -tx))
  return (p.x - tmp.x) * tmp.tz - (p.z - tmp.z) * tmp.tx;
}

/**
 * What the player does about the emergency vehicles near by: in front of
 * one, holding it up without having pulled over; in slow traffic on a road
 * with several lanes outside the corridor; into the junction it is about to
 * cross.  The instructor says what to do when one first comes near.
 */
void Emergencies::watchPlayer(const PlayerState& p) {
  pj.blocking = false;
  pj.corridor = false;
  if (!pj.junction) pj.car = nullptr;
  Lane* lane = p.lane;
  if (p.onFoot || !lane) {
    lastLane = nullptr;
    return;
  }
  const double s = p.s.value_or(0);
  const double front = s + p.length / 2;
  const double across_ = across(p);
  const std::vector<Lane*>& lanes = carriageway(*lane);
  const size_t nLanes = lanes.size();
  const bool crossed = lastLane == lane && lastFront < lane->stopAt && front >= lane->stopAt;
  lastLane = lane;
  lastFront = front;
  Seen& seen_ = seen;
  // (aside: the left-most lane of several to the left, any other to the right)
  const bool leftmost = nLanes >= 2 && lane->idx == static_cast<int>(nLanes) - 1;
  const bool aside_ = leftmost ? across_ > 0.4 : across_ < -0.4;
  for (const Run& run : runs) {
    TrafficCar& car = *run.car;
    EmergencyCar& em = *car.em;
    if (see(run, lane, nullptr, s, across_, seen_)) {
      const double g = seen_.along - (car.length + p.length) / 2;
      if (seen_.along <= 0 || g > 300) continue;
      if (!seen_.same) {
        if (g < 200) say(em, 2, "emTipOncoming");
        continue;
      }
      // coming up from behind
      if (g < 230) say(em, 1, nLanes >= 2 ? "emTipCorridor" : "emTipBehind");
      // close behind the player, who has not made way: not moved over to the right on a road with one lane
      // this way – on one with several, still in the way it takes through the corridor
      const double clear_ = std::abs(seen_.lat - em.target) - car.width / 2 - p.width / 2;
      if (g < 50 && (nLanes >= 2 ? clear_ < 0.25 : !aside_)) {
        pj.blocking = true;
        pj.car = &car;
      }
      if (nLanes >= 2 && std::abs(p.speed) < 8.5 && g < 200 && !aside_) {
        pj.corridor = true;
        pj.car = &car;
      }
      continue;
    }
    // the junction ahead of the player, which it is about to cross
    for (const RunNode& n : run.nodes) {
      if (n.node != lane->to) continue;
      if (n.d < 180 && front < lane->stopAt + 1) say(em, 4, "emTipJunction");
      if (crossed && (n.d < 80 || n.eta < 6)) {
        pj.junction = true;
        pj.car = &car;
      }
    }
  }
}

/** The instructor's word about an emergency vehicle (each thing once). */
void Emergencies::say(EmergencyCar& em, int bit, const std::string& key) {
  if (em.told & bit) return;
  em.told |= bit;
  pendingTip = key;
  hasTip = true;
  tipAt = time;
}

std::string Emergencies::tip() {
  const std::string t = pendingTip;
  const bool had = hasTip;
  pendingTip.clear();
  hasTip = false;
  return had && time - tipAt < 2 ? t : "";
}

const std::vector<EmergencyFault>& Emergencies::judge(double dt, const PlayerSnapshot& /*p*/) {
  std::vector<EmergencyFault>& out = faults;
  out.clear();
  if (!judging) {
    blockT = corridorT = 0;
    pj.junction = false;
    return out;
  }
  blockT = pj.blocking ? blockT + dt : std::max(0.0, blockT - dt);
  corridorT = pj.corridor ? corridorT + dt : std::max(0.0, corridorT - dt * 2);
  EmergencyCar* em = pj.car ? pj.car->em.get() : nullptr;
  if (em) {
    if (corridorT > 5 && !(em->faults & 2)) {
      em->faults |= 2;
      out.push_back({"emergencyCorridor", "major"});
    } else if (blockT > 4 && !(em->faults & 3)) {
      em->faults |= 1;
      out.push_back({"emergencyBlock", "major"});
    }
    if (pj.junction && !(em->faults & 4)) {
      em->faults |= 4;
      out.push_back({"emergencyJunction", "major"});
    }
  }
  pj.junction = false;
  return out;
}

}  // namespace fs

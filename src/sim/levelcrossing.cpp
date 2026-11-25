// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/levelcrossing.ts @ v1.7.7
#include "sim/levelcrossing.h"

#include <algorithm>
#include <cmath>

#include "sim/rules.h"
#include "sim/traffic.h"

namespace fs {

namespace {

PolySample smp;
PolyProjection proj;

/** Distance along a lane of an edge station. */
double along(const Lane& lane, double s) { return (s - lane.s0) * lane.dir; }

}  // namespace

bool passive(const LevelCrossing& c) { return c.eq.light == "none" && c.eq.barrier == "none"; }

double entryOf(const LevelCrossing& c, int dir) { return dir > 0 ? c.s0 - LX.CROSS : c.s1 + LX.CROSS; }

double exitOf(const LevelCrossing& c, int dir) { return dir > 0 ? c.s1 + LX.ZONE : c.s0 - LX.ZONE; }

bool redOn(const LevelCrossing& c) { return c.phase == "red" || c.phase == "lowering" || c.phase == "closed"; }

bool ringing(const LevelCrossing& c) {
  if (!c.eq.bell) return false;
  if (c.phase == "yellow" || c.phase == "lowering") return true;
  if (c.phase == "red") return c.eq.barrier != "none" || c.t < 8;
  return false;
}

bool secured(const LevelCrossing& c) {
  if (passive(c)) return true;
  if (c.eq.barrier != "none") return c.phase == "closed";
  return c.phase == "red" && c.t >= 4;
}

std::string mustWait(const LevelCrossing& c, double v, double d) {
  if (c.occupied) return "train";
  if (passive(c)) return c.eta < (c.eq.rail ? LX.PASSIVE_WAIT : LX.TRAM_WAIT) ? "train" : "";
  if (c.eq.barrier != "none" && (c.phase == "lowering" || c.phase == "closed" || c.phase == "raising")) return "barrier";
  if (c.phase == "red" || c.phase == "lowering" || c.phase == "closed" || c.phase == "raising") return "light";
  if (c.phase == "yellow" && d > 0 && (v * v) / (2 * std::max(d, 0.5)) < 3.2) return "light";
  return "";
}

LevelCrossings::LevelCrossings(World& world, Traffic& road) : world(world), road(road) {}

LevelCrossings::~LevelCrossings() = default;

const std::vector<LevelCrossing*>* LevelCrossings::on(const Edge& edge) const {
  const auto it = byEdge.find(edge.id);
  return it == byEdge.end() ? nullptr : &it->second;
}

LevelCrossing* LevelCrossings::of(const CrossingJson& x) const {
  const auto it = ofXing.find(&x);
  return it == ofXing.end() ? nullptr : it->second;
}

// ------------------------------------------------------------------ the installations

void LevelCrossings::refresh() {
  const std::vector<CrossingJson*>& all = world.crossings;
  bool changed = false;
  // (roads that went: their installations go)
  for (int i = static_cast<int>(list.size()) - 1; i >= 0; i--) {
    LevelCrossing* c = list[i].get();
    if (world.edges[c->edgeId] == c->edge && world.allEdges.has(c->edge)) continue;
    const auto on = byEdge.find(c->edgeId);
    if (on != byEdge.end()) {
      on->second.erase(std::find(on->second.begin(), on->second.end(), c));
      if (on->second.empty()) byEdge.erase(on);
    }
    for (auto it = ofXing.begin(); it != ofXing.end();) it = it->second == c ? ofXing.erase(it) : std::next(it);
    list.erase(list.begin() + i);
    changed = true;
  }
  for (const CrossingJson* x : all) {
    if (x->k != XING_RAIL && x->k != XING_TRAM) continue;
    if (ofXing.count(x)) continue;
    Edge* edge = world.edges[x->e];
    if (!edge) continue;
    std::vector<LevelCrossing*>& on = byEdge[edge->id];
    LevelCrossing* c = nullptr;
    for (LevelCrossing* o : on)
      if (x->s >= o->s0 - LX.GROUP && x->s <= o->s1 + LX.GROUP) {
        c = o;
        break;
      }
    if (c) {
      c->xings.push_back(x);
      c->s0 = std::min(c->s0, x->s);
      c->s1 = std::max(c->s1, x->s);
    } else {
      auto made = std::make_unique<LevelCrossing>();
      made->id = nextId++;
      made->edge = edge;
      made->edgeId = edge->id;
      made->xings = {x};
      made->s0 = x->s;
      made->s1 = x->s;
      c = made.get();
      list.push_back(std::move(made));
      on.push_back(c);
    }
    ofXing[x] = c;
    equip(*c);
    findTracks(*c);
    changed = true;
  }
  known = static_cast<int>(all.size());
  if (changed) indexTracks();
}

void LevelCrossings::equip(LevelCrossing& c) {
  const Edge& e = *c.edge;
  samplePolyline(e.pl, (c.s0 + c.s1) / 2, smp);
  c.x = smp.x;
  c.z = smp.z;
  bool rail = false;
  for (const CrossingJson* x : c.xings) rail = rail || x->k == XING_RAIL;
  // (the map's tags are at the node of one of its tracks: the nearest of them)
  Equipment best = equipmentFor(c.x, c.z, rail, e.tier);
  for (const CrossingJson* x : c.xings) {
    samplePolyline(e.pl, x->s, smp);
    const Equipment eq = equipmentFor(smp.x, smp.z, rail, e.tier);
    if (eq.mapped && !best.mapped) best = eq;
  }
  c.eq = best;
  // the map announces a railway's crossing on the bigger roads (Zeichen 151 at 140 m); outside built-up areas we put the beacons there
  const bool announced = rail && e.tier >= 2;
  c.beacons = announced && (e.flags & EF_URBAN) == 0;
  c.zone = announced ? (c.beacons ? LX.BEACONS[0] : LX.WARNING) : 0;
}

void LevelCrossings::findTracks(LevelCrossing& c) {
  c.hits.clear();
  if (rail) rail->findTracks(*this, c);
}

void LevelCrossings::indexTracks() {
  byPath.clear();
  for (const auto& c : list)
    for (const TrackHit& h : c->hits) byPath[h.path].push_back(OnPath{c.get(), h.s});
}

// ------------------------------------------------------------------ each step

void LevelCrossings::update(double dt, double x, double z) {
  time += dt;
  refreshT -= dt;
  if (known != static_cast<int>(world.crossings.size()) || refreshT <= 0) {
    refreshT = 3;
    refresh();
  }
  // (those near the player: the others stay open, and no train is near them)
  active.clear();
  for (const auto& c : list) {
    c->eta = JS_INF;
    c->occupied = false;
    if (std::abs(c->x - x) > LX.ACTIVE || std::abs(c->z - z) > LX.ACTIVE) {
      c->phase = "open";
      c->t = 0;
      c->lower = 0;
    } else active.push_back(c.get());
  }
  holds.clear();
  if (rail && !byPath.empty() && !active.empty()) rail->ahead(*this);
  for (LevelCrossing* c : active) step(*c, dt);
  // the trains brake for a crossing ahead that is not secured yet (the installation switched on above)
  if (rail) rail->hold(*this);
}

void LevelCrossings::step(LevelCrossing& c, double dt) {
  if (passive(c)) {
    c.phase = "open";
    return;
  }
  const bool need = c.occupied || c.eta <= LX.WARN;
  c.quiet = need ? 0 : c.quiet + dt;
  c.t += dt;
  const bool barrier = c.eq.barrier != "none";
  const auto go = [&](const char* p) {
    c.phase = p;
    c.t = 0;
  };
  if (c.phase == "open") {
    if (need) go(c.eq.light == "yellowRed" ? "yellow" : c.eq.light == "blink" ? "red" : "lowering");
  } else if (c.phase == "yellow") {
    if (c.t >= LX.YELLOW) go("red");
  } else if (c.phase == "red") {
    if (barrier && c.t >= LX.RED_BEFORE) go("lowering");
    else if (!barrier && c.quiet >= LX.CLEAR && c.t >= 3) go("open");
  } else if (c.phase == "lowering") {
    c.lower = std::min(1.0, c.lower + dt / LX.LOWER);
    if (c.lower >= 1) go("closed");
  } else if (c.phase == "closed") {
    c.lower = 1;
    if (c.quiet >= LX.CLEAR) go("raising");
  } else if (c.phase == "raising") {
    c.lower = std::max(0.0, c.lower - dt / LX.RAISE);
    // (another train: down again at once – the lights come on with it)
    if (need) go("lowering");
    else if (c.lower <= 0) go("open");
  }
}

// ------------------------------------------------------------------ the road traffic

LxGate LevelCrossings::gate(const Lane& lane, double front, double v, double length, const Connector* next, const TrafficCar* self, bool isPlayer) {
  LxGate out;
  if (list.empty()) return out;
  gateOn(lane, -front, v, length, self, isPlayer, out);
  if (next && lane.len - front < 150) gateOn(*next->to, lane.len - front + next->len, v, length, self, isPlayer, out);
  return out;
}

void LevelCrossings::gateOn(const Lane& l, double base, double v, double length, const TrafficCar* self, bool isPlayer, LxGate& out) {
  const auto it = byEdge.find(l.edge->id);
  if (it == byEdge.end()) return;
  for (const LevelCrossing* c : it->second) {
    const double stopAt = base + along(l, entryOf(*c, l.dir)) - (LX.STOP - LX.CROSS);
    if (stopAt < -0.6 || stopAt > 150) continue;
    // (§ 19 Abs. 1 StVO: a moderate speed where no barriers are – to be able to stop for a train)
    if (c->eq.barrier == "none" && stopAt < 80) out.cap = std::min(out.cap, std::max(LX.MODERATE, std::sqrt(LX.MODERATE * LX.MODERATE + 2 * 1.5 * std::max(0.0, stopAt - 10))));
    std::string why = mustWait(*c, v, stopAt);
    // (and no further than there is room for the whole car behind the tracks)
    if (why.empty() && stopAt < 45 && blockedBeyond(l, base, base + along(l, exitOf(*c, l.dir)), length, self, isPlayer)) why = "queue";
    if (!why.empty() && std::max(0.0, stopAt) < out.stop) {
      out.stop = std::max(0.0, stopAt);
      out.why = why;
    }
  }
}

bool LevelCrossings::blockedBeyond(const Lane& l, double base, double exitAt, double length, const TrafficCar* self, bool isPlayer) {
  const double need = exitAt + length + 1;
  const auto slow = [&](double rear, double v) { return rear > 0.2 && rear < need && v < 2.5; };
  if (const std::vector<TrafficCar*>* cars = road.carsOn(l)) {
    for (const TrafficCar* car : *cars) {
      if (car == self) continue;
      if (slow(base + car->s - car->length / 2, car->v)) return true;
    }
  }
  const PlayerState* p = player;
  if (p && !isPlayer && p->lane == &l && p->s && !p->onFoot && slow(base + *p->s - p->length / 2, std::abs(p->speed))) return true;
  // (beyond the end of the lane: in the junction, and on the lanes after it)
  const double end = base + l.len;
  if (need > end) {
    for (const Connector* k : l.out) {
      if (const std::vector<TrafficCar*>* cars = road.carsOnConn(*k))
        for (const TrafficCar* car : *cars)
          if (car != self && slow(end + car->s - car->length / 2, car->v)) return true;
      if (need > end + k->len) {
        if (const std::vector<TrafficCar*>* cars = road.carsOn(*k->to))
          for (const TrafficCar* car : *cars)
            if (car != self && slow(end + k->len + car->s - car->length / 2, car->v)) return true;
        if (p && !isPlayer && p->lane == k->to && p->s && slow(end + k->len + *p->s - p->length / 2, std::abs(p->speed))) return true;
      }
    }
  }
  return false;
}

// ------------------------------------------------------------------ the player

void LevelCrossings::judge(RuleMonitor& rules, const PlayerSnapshot& p, const Edge& edge, double s, int dir, double dt) {
  const auto it = byEdge.find(edge.id);
  if (it == byEdge.end()) {
    prevFront.reset();
    standT = 0;
    sides.clear();
    return;
  }
  const double front = s + dir * (p.front ? *p.front : p.length / 2);
  const double rear = front - dir * p.length;
  const double kmh = std::abs(p.speed) * 3.6;
  bool onTracks = false;
  for (const LevelCrossing* c : it->second) {
    const double line = entryOf(*c, dir) + dir * 0.5;
    if (prevFront && prevFront->edge == &edge && prevFront->dir == dir && rules.time - prevFront->t < 0.6 && p.speed > 0.3 && (prevFront->s - line) * dir < 0 && (front - line) * dir >= 0) {
      // over the cross: what did it show?
      if (c->occupied || (passive(*c) && c->eta < (c->eq.rail ? LX.PASSIVE_FAULT : LX.TRAM_FAULT))) rules.noteAt("levelCrossingTrain", "major", p.x, p.z, {}, 10);
      else if (c->eq.barrier != "none" && (c->phase == "lowering" || c->phase == "closed" || (c->phase == "raising" && c->lower > 0.3))) rules.noteAt("levelCrossingBarrier", "major", p.x, p.z, {}, 10);
      else if (c->phase == "red" || c->phase == "lowering" || c->phase == "closed") rules.noteAt("levelCrossingRed", "major", p.x, p.z, {}, 10);
      // (yellow: a fault where the car could have stopped comfortably when it came on – at its speed it was v · t away)
      else if (c->phase == "yellow" && p.speed / (2 * std::max(c->t, 0.1)) < 3.5) rules.noteAt("levelCrossingYellow", "minor", p.x, p.z, {}, 10);
      if (c->eq.barrier == "none" && kmh > LX.MODERATE * 3.6 + 5) rules.noteAt("levelCrossingSpeed", "minor", p.x, p.z, {{"limit", jsRound(LX.MODERATE * 3.6)}, {"speed", jsRound(kmh)}}, 10);
    }
    // on the tracks: the car's body over the danger area
    const double lo = std::min(front, rear);
    const double hi = std::max(front, rear);
    if (hi > c->s0 - LX.ZONE && lo < c->s1 + LX.ZONE) onTracks = true;
    // overtaking from the sign that announces it to the tracks (§ 19 Abs. 1 Satz 3 StVO)
    if (c->zone > 0) overtaking(rules, p, edge, s, dir, *c);
  }
  if (onTracks && std::abs(p.speed) < 0.3) {
    standT += dt;
    if (standT > 1.5) rules.noteAt("levelCrossingStop", "major", p.x, p.z, {}, 30);
  } else standT = 0;
  prevFront = Front{&edge, dir, front, rules.time};
}

void LevelCrossings::overtaking(RuleMonitor& rules, const PlayerSnapshot& p, const Edge& edge, double s, int dir, const LevelCrossing& c) {
  const double entry = entryOf(c, dir);
  const double from = entry - dir * c.zone;
  const double exit = exitOf(c, dir);
  const bool inZone = (s - from) * dir >= 0 && (s - exit) * dir <= 0;
  if (!inZone) {
    sides.clear();
    return;
  }
  const LaneGraph::EdgeLanes* el = road.graph.edgeLanes[edge.id];
  if (!el) return;
  std::unordered_set<const TrafficCar*> seen;
  for (const Lane* lane : dir > 0 ? el->fwd : el->back) {
    const std::vector<TrafficCar*>* cars = road.carsOn(*lane);
    if (!cars) continue;
    for (const TrafficCar* car : *cars) {
      const double st = lane->s0 + lane->dir * car->s;
      if (std::abs(st - s) > 40) continue;
      seen.insert(car);
      const double side = (st - s) * dir;
      const auto was = sides.find(car);
      if (was != sides.end() && was->second > 1 && side < -1 && p.speed > car->v + 0.5) rules.noteAt("levelCrossingOvertake", "major", p.x, p.z, {}, 20);
      if (std::abs(side) > 1) sides[car] = side;
    }
  }
  for (auto k = sides.begin(); k != sides.end();) k = seen.count(k->first) ? std::next(k) : sides.erase(k);
}

// ------------------------------------------------------------------ the signs

std::vector<SignPost> LevelCrossings::signsFor(const LevelCrossing& c, const std::function<double(double s, double x, double z)>& y) {
  const Edge& e = *c.edge;
  std::vector<SignPost> out;
  const bool lit = !passive(c);
  for (const int dir : {1, -1}) {
    if ((dir > 0 ? e.lanesF : e.lanesB) == 0) continue;
    const double entry = entryOf(c, dir);
    if (lit) {
      SignPost sp = postAt(e, entry, dir, {"201-50"}, y);
      // (above the lights)
      sp.h = 2.45;
      out.push_back(sp);
    }
    if (c.beacons) {
      static const char* const codes[] = {"156-10", "159-10", "162-10"};
      for (size_t i = 0; i < LX.BEACONS.size(); i++) {
        const double at = entry - dir * LX.BEACONS[i];
        if (at < 1 || at > e.len - 1) continue;
        SignPost sp = postAt(e, at, dir, {codes[i]}, y);
        sp.h = 0.6;
        out.push_back(sp);
      }
    }
  }
  return out;
}

SignPost LevelCrossings::postAt(const Edge& e, double s, int dir, const std::vector<std::string>& codes, const std::function<double(double s, double x, double z)>& y) {
  const double st = std::max(0.5, std::min(e.len - 0.5, s));
  samplePolyline(e.pl, st, smp);
  const int lat = -dir;
  const double sw = lat > 0 ? e.swL : e.swR;
  const double outward = e.width / 2 + (sw > 0 ? std::min(0.55, sw * 0.4) : 1.0);
  const double x = smp.x + smp.tz * outward * lat;
  const double z = smp.z - smp.tx * outward * lat;
  const double fx = -smp.tx * dir;
  const double fz = -smp.tz * dir;
  SignPost p;
  p.x = x;
  p.y = y(st, x, z) + (sw > 0 ? 0.15 : -0.15);
  p.z = z;
  p.yaw = std::atan2(-fx, -fz);
  p.s = codes;
  p.h = (e.flags & EF_URBAN) != 0 ? 2.1 : 1.7;
  return p;
}

std::optional<SignPost> LevelCrossings::editPost(const SignPost& p) {
  const bool cross = std::find(p.s.begin(), p.s.end(), "201-50") != p.s.end();
  const bool warn = std::find(p.s.begin(), p.s.end(), "151") != p.s.end();
  if (!cross && !warn) return p;
  // (a tile may come before the first step: the installations of the roads loaded so far)
  if (known != static_cast<int>(world.crossings.size())) refresh();
  std::vector<std::string> drop;
  for (const auto& c : list) {
    if (std::abs(c->x - p.x) > 200 || std::abs(c->z - p.z) > 200) continue;
    projectOnPolyline(c->edge->pl, p.x, p.z, proj);
    if (std::abs(proj.off) > c->edge->width / 2 + 4) continue;
    if (cross && !passive(*c) && proj.s > c->s0 - 8 && proj.s < c->s1 + 8) drop.push_back("201-50");
    if (warn && c->beacons && (std::abs(proj.s - (c->s0 - LX.WARNING)) < 8 || std::abs(proj.s - (c->s1 + LX.WARNING)) < 8)) drop.push_back("151");
  }
  if (drop.empty()) return p;
  std::vector<std::string> left;
  for (const std::string& code : p.s)
    if (std::find(drop.begin(), drop.end(), code) == drop.end()) left.push_back(code);
  if (left.empty()) return std::nullopt;
  SignPost q = p;
  q.s = left;
  return q;
}

}  // namespace fs

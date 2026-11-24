// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/rules.ts @ v1.7.7
#include "sim/rules.h"

#include <algorithm>
#include <cmath>

#include "core/geom.h"
#include "core/js.h"
#include "sim/jsmath.h"

namespace fs {

const std::map<Severity, int> SEVERITY_POINTS = {{"hint", 0}, {"minor", 1}, {"major", 3}, {"fail", 10}};

namespace {
/** How far from where it stood solidly the last road keeps its limit (m). */
const double HOLD_DIST = 40;
/** … and how far above or below (m). */
const double HOLD_DY = 1.5;
}  // namespace

RuleMonitor::RuleMonitor(World& world_, LaneGraph& graph_, SignalSystem& signals_, Traffic& traffic_) : world(world_), graph(graph_), signals(signals_), traffic(traffic_) {}

void RuleMonitor::setVehicle(const std::optional<Caps>& caps_, const std::string& capFault_, bool heavy_, bool noMotorway_) {
  caps = caps_;
  capFault = capFault_;
  heavy = heavy_;
  noMotorway = noMotorway_;
}

void RuleMonitor::truck(bool on) { setVehicle(on ? std::optional<Caps>(Caps{60, 80}) : std::nullopt, "truckSpeed", on, false); }

bool RuleMonitor::truck() const { return heavy; }

void RuleMonitor::reset() {
  log.clear();
  cooldown.clear();
  lane = nullptr;
  entry = std::nullopt;
  redLine = std::nullopt;
  oncoming = std::nullopt;
  lastEdge = nullptr;
  hold = std::nullopt;
  time = 0;
  speedOver = wrongSide = wrongWayT = sidewalkT = offRoadT = tailgateT = 0;
}

void RuleMonitor::placed() {
  lane = nullptr;
  entry = std::nullopt;
  redLine = std::nullopt;
  oncoming = std::nullopt;
  lastEdge = nullptr;
  hold = std::nullopt;
  stoppedNearLine = false;
}

const RuleMonitor::Hold* RuleMonitor::holdNow(const PlayerSnapshot& p) const {
  if (!hold) return nullptr;
  const Hold& h = *hold;
  if (jsHypot(p.x - h.x, p.z - h.z) > HOLD_DIST || std::abs(p.y - h.y) > HOLD_DY) return nullptr;
  return &h;
}

int RuleMonitor::points() const {
  int a = 0;
  for (const Violation& v : log) {
    auto it = SEVERITY_POINTS.find(v.severity);
    a += it == SEVERITY_POINTS.end() ? 0 : it->second;
  }
  return a;
}

bool RuleMonitor::failed() const {
  for (const Violation& v : log)
    if (v.severity == "fail") return true;
  return false;
}

void RuleMonitor::noteAt(const std::string& id, const Severity& severity, double x, double z, const ViolationData& data, double cooldown_) {
  PlayerSnapshot at;
  at.x = x;
  at.z = z;
  report(id, severity, at, data, cooldown_);
}

void RuleMonitor::note(const std::string& id, const Severity& severity, const ViolationData& data, double cooldown_) {
  auto it = cooldown.find(id);
  const double until = it == cooldown.end() ? -1 : it->second;
  if (time < until) return;
  cooldown[id] = time + cooldown_;
  log.push_back(Violation{id, severity, time, lastX, lastZ, data});
  if (onViolation) onViolation(log.back());
}

void RuleMonitor::report(const std::string& id, Severity severity, const PlayerSnapshot& p, const ViolationData& data, double cooldown_) {
  auto it = cooldown.find(id);
  const double until = it == cooldown.end() ? -1 : it->second;
  if (time < until) return;
  cooldown[id] = time + cooldown_;
  if (strict && severity == "hint") severity = "minor";
  log.push_back(Violation{id, severity, time, p.x, p.z, data});
  if (onViolation) onViolation(log.back());
}

Lane* RuleMonitor::laneFor(const Edge& edge, double s, double off, int dir) const {
  const LaneGraph::EdgeLanes* el = graph.edgeLanes[edge.id];
  if (!el) return nullptr;
  const std::vector<Lane*>& list = dir > 0 ? el->fwd : el->back;
  if (list.empty()) return nullptr;
  Lane* best = nullptr;
  double bd = JS_INF;
  for (Lane* l : list) {
    const double d = std::abs(l->off - off);
    if (d < bd) {
      bd = d;
      best = l;
    }
  }
  (void)s;
  return best;
}

void RuleMonitor::update(double dt, const PlayerSnapshot& p, const std::vector<TrafficEvent>& events) {
  time += dt;
  lastX = p.x;
  lastZ = p.z;
  RuleContext& ctx = context;
  ctx.limitWhen = "";
  if (yard) {
    ctx.limit = -1;
    ctx.zone = 0;
    ctx.urban = false;
    ctx.roadName = "";
    ctx.junction = std::nullopt;
    ctx.onRoad = true;
    ctx.wrongWay = false;
    return;
  }
  const double fx = -jsSin(p.yaw);
  const double fz = -jsCos(p.yaw);
  const double kmh = std::abs(p.speed) * 3.6;
  const bool moving = std::abs(p.speed) > 0.6;

  // indicator / look history (seconds within a sliding window)
  const auto decay = [dt](double v, bool on) { return on ? jsMin(6, v + dt) : jsMax(0, v - dt * 1.5); };
  indL = decay(indL, p.indicator < 0 && !p.hazard);
  indR = decay(indR, p.indicator > 0 && !p.hazard);
  lookL = decay(lookL, p.lookYaw > 0.9);
  lookR = decay(lookR, p.lookYaw < -0.9);

  // --- where are we? ---
  const bool onSurface = world.surfaceAt(p.x, p.z, p.y + 0.6, hit);
  ctx.onRoad = onSurface && hit.zone == 0;
  ctx.wrongWay = false;
  // a solid hit: the point stands on this road's carriageway – only such a hit may
  // change the limit (the fallback below and a junction area may not)
  const bool solid = onSurface && hit.zone == 0 && hit.edge != nullptr;
  Edge* edge = onSurface ? hit.edge : nullptr;
  // A wheel over the edge line or a clipped corner must not make us lose
  // track of the road we are on: fall back to the nearest carriageway.
  bool near = false;
  if (!onSurface || (hit.zone != 0 && !hit.edge)) {
    if (world.locate(p.x, p.z, p.y, FALLBACK_MAX_DIST, frontHit, FALLBACK_MAX_DY) && frontHit.edge) {
      near = true;
      edge = frontHit.edge;
      hit.edge = edge;
      hit.s = frontHit.s;
      hit.off = frontHit.off;
    }
  }
  int dir = 0;
  double headingCos = 0;
  if (edge) {
    // travel direction relative to the edge
    const Polyline3& pl = edge->pl;
    const int i = std::min(static_cast<int>(pl.cum.size()) - 2, std::max(0, segIndex(*edge, hit.s)));
    const double tx = pl.pts[static_cast<size_t>(i * 3 + 3)] - pl.pts[static_cast<size_t>(i * 3)];
    const double tz = pl.pts[static_cast<size_t>(i * 3 + 5)] - pl.pts[static_cast<size_t>(i * 3 + 2)];
    double tl = jsHypot(tx, tz);
    if (tl == 0 || tl != tl) tl = 1;
    // Which way the car faces, not which way it creeps: at a standstill the
    // speed is ±0, and a car that rolls back a hand's breadth at the stop
    // line has not changed to the oncoming lane.
    headingCos = (fx * tx + fz * tz) / tl;
    dir = headingCos >= 0 ? 1 : -1;
  }

  RoadEvents* works = traffic.roadEvents;
  if (edge && (hit.zone == 0 || near)) {
    // A weak match (a gap, a junction area, the nearest-road fallback) does not
    // change what we know about the road: the last road the car stood solidly on
    // keeps its limit – and its class for the caps – while the car is still near
    // that place.  Only a solid hit of a road takes its limit over at once.
    const Hold* held = solid ? nullptr : holdNow(p);
    Edge& src = held ? *held->edge : *edge;
    const double srcS = held ? held->s : hit.s;
    const int srcDir = held ? held->dir : dir;
    // a sign with a time on it ("22–6 h") counts only then, in place of the road's own limit (world/extrasigns)
    const std::optional<TimedRules::Limit> timed_ = timed ? timed->limit(src.id, srcDir, heavy) : std::nullopt;
    const double limit = timed_ ? timed_->v : srcDir > 0 ? src.maxF : src.maxB;
    // car parks, driveways and tracks have no signed limit: nothing to show
    const bool unsigned_ = src.cls >= SERVICE && src.zone == 0 && !timed_;
    ctx.limit = src.zone == 7 ? 7 : unsigned_ ? -1 : limit;
    if (timed_ && src.zone != 7) ctx.limitWhen = timed_->when;
    // what the signs allow, but never more than this vehicle may drive there
    bool capped = false;
    if (caps && src.zone != 7 && !unsigned_) {
      const double cap = src.cls <= MOTORWAY_LINK ? caps->motorway : (src.flags & EF_URBAN) != 0 ? 0 : caps->rural;
      if (cap > 0 && (ctx.limit == 0 || ctx.limit > cap)) {
        ctx.limit = cap;
        capped = true;
      }
    }
    // roadworks of the day (src/sim/roadevents): their own limit, from its sign to the end of the zone
    if (works && !unsigned_ && src.zone != 7) {
      const double zl = works->limitAt(src, srcS, srcDir, ctx.limit);
      if (zl != ctx.limit) {
        ctx.limit = zl;
        capped = false;
      }
    }
    // a tractor on the motorway (or its slip road): not allowed at all
    if (noMotorway && edge->cls <= MOTORWAY_LINK && hit.zone == 0) report("motorwayBan", "major", p, {}, 120);
    ctx.zone = src.zone;
    ctx.urban = (src.flags & EF_URBAN) != 0;
    ctx.roadName = !src.name.empty() ? src.name : src.ref;
    Lane* laneHere = laneFor(*edge, hit.s, hit.off, dir);
    const bool oneWayAgainst = (dir > 0 ? edge->lanesF : edge->lanesB) == 0;

    // --- wrong way in a one-way street ---
    // crossing a one-way carriageway at a junction is not driving against it
    const bool inJunctionZone = hit.s < edge->cutA + 2 || hit.s > edge->len - edge->cutB - 2;
    if (oneWayAgainst && moving && p.speed > 0 && std::abs(headingCos) > 0.7 && !inJunctionZone && !(edge->flags & EF_MINOR_SERVICE)) {
      wrongWayT += dt;
      ctx.wrongWay = true;
      if (wrongWayT > 1.2) report("wrongWay", edge->cls <= TRUNK_LINK ? "fail" : "major", p, {}, 20);
    } else {
      wrongWayT = 0;
    }

    // --- keep right ---
    // (not where a side closed for roadworks is passed on the oncoming lane)
    if (laneHere && !oneWayAgainst && !inJunctionZone && std::abs(headingCos) > 0.8 && edge->lanesF > 0 && edge->lanesB > 0 && edge->laneW >= 2.5 && moving && p.speed > 3 &&
        !(works && works->passing(*edge, hit.s, dir)) && !(traffic.narrowings && traffic.narrowings->passing(*edge, hit.s, dir))) {
      const double myOff = hit.off * dir;  // + = left of our direction
      const double centre = (dir > 0 ? -edge->width / 2 + edge->marginR + edge->lanesF * edge->laneW : edge->width / 2 - edge->marginL - edge->lanesB * edge->laneW) * dir;
      // car centre beyond the middle of the road
      if (myOff > centre + 0.45) {
        wrongSide += dt;
        if (wrongSide > 3.5) report("keepRight", (edge->flags & EF_MARKED) != 0 ? "major" : "minor", p, {}, 25);
      } else {
        wrongSide = jsMax(0, wrongSide - dt * 2);
      }
    }

    // --- speed ---
    const double lim = unsigned_ ? 35 : ctx.limit;
    if (lim > 0) {
      const double tolerance = lim <= 10 ? 5 : 6;
      const double over = kmh - lim;
      if (over > tolerance) {
        speedOver += dt;
        if (speedOver > 1.6) {
          const Severity sev = over > 25 ? "fail" : over > 14 ? "major" : "minor";
          report(src.zone == 7 ? "speedWalk" : capped ? capFault : "speed", sev, p, {{"limit", lim}, {"speed", jsRound(kmh)}}, sev == "minor" ? 18 : 10);
        }
      } else {
        speedOver = jsMax(0, speedOver - dt * 2);
      }
    }

    // (the time shows only while the limit of the timed sign is the one that holds)
    if (timed_ && ctx.limit != timed_->v) ctx.limitWhen = "";
    // the limit is settled now: the road the car stands solidly on keeps it for the
    // weak matches that follow (a gap, a junction area, the fallback)
    if (solid) hold = Hold{&src, srcS, srcDir, p.x, p.y, p.z, ctx.limit, ctx.limitWhen};

    // --- restricted roads ---
    if (edge != lastEdge) {
      const bool isAllowed = allowed && allowed->count(edge) > 0;
      if (edge->access == ACCESS_PRIVATE && edge->tier >= 1) {
        report("accessPrivate", "minor", p, {}, 30);
      } else if (edge->access == ACCESS_AGRICULTURAL) {
        report("accessAgricultural", "minor", p, {}, 30);
      } else if (edge->access == ACCESS_DESTINATION && !isAllowed) {
        report("accessDestination", "hint", p, {}, 60);
      } else if (!isAllowed) {
        // shut at this time by a sign with a time on it – to motor vehicles, or to lorries
        const std::string shut = timed ? timed->shut(edge->id, dir, heavy) : "";
        if (!shut.empty()) report("accessTimed", "minor", p, {{"when", shut}}, 30);
      }
      lastEdge = edge;
    }

    // --- lane tracking: stop lines, signals, entering the junction ---
    if (laneHere && p.speed >= -0.5) trackLane(*laneHere, hit.s, dt, p, fx, fz);
    else if (!laneHere) lane = nullptr;
    // the site light, the narrowing (Zeichen 208), the fenced strip, a road closed for a march
    if (works) works->judge(*this, p, *edge, hit.s, hit.off, dir, dt);
    // into a narrowing on the side of its kerb build-out while somebody comes the other way (§ 6 StVO; src/sim/narrowings)
    if (traffic.narrowings) traffic.narrowings->judge(*this, p, *edge, hit.s, dir);
    // level crossings: the lights, the barriers, a train; not on the tracks, no overtaking before them (src/sim/levelcrossing)
    if (traffic.levelCrossings) traffic.levelCrossings->judge(*this, p, *edge, hit.s, dir, dt);
  } else {
    // nothing solid under the car: the last road we stood on keeps its limit while
    // we are still near it (a junction area, a gap, the pavement) – otherwise there
    // is no limit to show
    const Hold* held = holdNow(p);
    ctx.limit = held ? held->limit : -1;
    if (held) ctx.limitWhen = held->when;
    wrongWayT = 0;
    wrongSide = 0;
    // inside a junction area we keep the lane we came from; far from any road we forget it
    if (!onSurface || hit.node == nullptr) {
      lostT += dt;
      if (lostT > 1.5) lane = nullptr;
    } else {
      lostT = 0;
    }
  }
  if (edge) lostT = 0;

  // --- sidewalk and off-road ---
  if (onSurface && hit.zone == 1 && moving) {
    sidewalkT += dt;
    if (sidewalkT > 0.7) report("sidewalk", "major", p, {}, 15);
  } else {
    sidewalkT = 0;
  }
  if (!onSurface && moving) {
    offRoadT += dt;
    if (offRoadT > 2.5) report("offRoad", "minor", p, {}, 20);
  } else {
    offRoadT = 0;
  }

  judgeRedLine(p);

  if (entry) entry->vMax = jsMax(entry->vMax, std::abs(p.speed));
  // --- leaving the junction: was the turn signalled? ---
  if (entry && edge && (hit.zone == 0 || near)) {
    const Entry& e = *entry;
    // (on the road itself, not on the part of it that lies in a junction: where several roads meet
    // within a few metres, the ground of the junction belongs to more than one of them, and the car
    // is not turning into each road whose ground it crosses)
    const bool onRoad = hit.s > edge->cutA + 0.5 && hit.s < edge->len - edge->cutB - 0.5;
    if (edge != e.arm->edge && onRoad) {
      judgeTurn(*edge, p);
      entry = std::nullopt;
    } else if (time - e.time > 25 && edge != e.arm->edge) {
      // (on other ground for a long while without having turned into a road: nothing to judge.
      // Whoever still stands on the road they came on – waiting just past the line for the
      // traffic to clear – has not turned at all yet, however long it takes.)
      entry = std::nullopt;
    }
  }

  // --- crossings (zebra, pedestrian lights) ---
  if (edge && moving && p.speed > 0) checkCrossings(*edge, hit.s, dir, p);

  // --- events from the traffic ---
  for (const TrafficEvent& ev : events) {
    if (ev.type == "collision") report("crash", "fail", p, {{"speed", jsRound(ev.speed * 3.6)}}, 6);
    else if (ev.type == "pedestrianEndangered") report("pedestrian", "fail", p, {}, 8);
    // a cyclist who rode straight on had to brake for the car that turned – or was knocked over
    else if (ev.type == "cyclistEndangered") report(ev.hit ? "cyclistHit" : "cyclistCutOff", ev.hit ? "fail" : "major", p, {}, 8);
    else if (ev.type == "forcedBrake") judgePriority(*ev.car, p, ev.cutIn);
  }
  if (p.hitScenery > 1.2) report("crashObject", p.hitScenery > 4 ? "fail" : "major", p, {{"speed", jsRound(p.hitScenery * 3.6)}}, 5);

  // --- blue lights and siren: make way at once, the corridor, not into the junction (§ 38 Abs. 1, § 11 Abs. 2 StVO; src/sim/emergency) ---
  if (traffic.emergency)
    for (const EmergencyFault& f : traffic.emergency->judge(dt, p)) report(f.id, f.severity, p, {}, 30);

  // --- small things ---
  if (p.stalled) report("stall", "hint", p, {}, 3);
  if (!p.seatbelt && kmh > 8) report("seatbelt", "minor", p, {}, 120);
  if (p.handbrake > 0.5 && kmh > 12) {
    handbrakeT += dt;
    if (handbrakeT > 2) report("handbrakeOn", "hint", p, {}, 30);
  } else {
    handbrakeT = 0;
  }
  if (p.indicator != 0 && !p.hazard && kmh > 20) {
    indicatorOnT += dt;
    if (indicatorOnT > 25) report("indicatorLeftOn", "hint", p, {}, 40);
  } else {
    indicatorOnT = 0;
  }

  // --- following distance ---
  if (kmh > 30) {
    double gap = JS_INF;
    for (const TrafficCar* c : traffic.cars) {
      const double dx = c->x - p.x;
      const double dz = c->z - p.z;
      const double lf = dx * fx + dz * fz;
      if (lf < 2 || lf > 60) continue;
      const double lr = std::abs(dx * fz - dz * fx);
      if (lr > 1.3 || std::abs(c->y - p.y) > 2) continue;
      gap = jsMin(gap, lf - 4.3);
    }
    // "halber Tacho" outside towns, about one second in town
    const double need = std::abs(p.speed) * (ctx.urban ? 0.75 : 1.2);
    if (gap < need * 0.6) {
      tailgateT += dt;
      if (tailgateT > 3) report("distance", gap < need * 0.35 ? "major" : "minor", p, {{"gap", jsRound(gap)}}, 25);
    } else {
      tailgateT = jsMax(0, tailgateT - dt);
    }
  } else {
    tailgateT = 0;
  }
}

int RuleMonitor::segIndex(const Edge& edge, double s) const {
  const std::vector<double>& cum = edge.pl.cum;
  int lo = 0;
  int hi = static_cast<int>(cum.size()) - 2;
  while (lo < hi) {
    const int mid = (lo + hi + 1) >> 1;
    if (cum[static_cast<size_t>(mid)] <= s) lo = mid;
    else hi = mid - 1;
  }
  return lo;
}

void RuleMonitor::trackLane(Lane& lane_, double s, double dt, const PlayerSnapshot& p, double fx, double fz) {
  RuleContext& ctx = context;
  // position of the front bumper along the lane
  const double centre = lane_.dir > 0 ? s - lane_.s0 : lane_.s0 - s;
  const double front = centre + (p.front ? *p.front : p.length / 2);
  if (&lane_ != lane) {
    lane = &lane_;
    prevLaneS = front;
    stoppedNearLine = false;
  }
  laneS = front;
  Node& node = *lane_.to;
  Arm& arm = *lane_.armTo;
  const double dist = lane_.stopAt - front;
  const bool controlled = node.arms.size() > 2;
  if (controlled && dist < 150 && dist > -2) {
    RuleContext::Junction j;
    j.dist = jsMax(0, dist);
    j.role = arm.role;
    j.ctrl = node.ctrl;
    j.signal = arm.sg >= 0 && node.sc >= 0 ? signals.get(node.sc, arm.sg) : -1;
    j.arrow = signals.arrows.has(&arm) ? signals.arrow(&arm) : -2;
    j.dark = node.dark;
    ctx.junction = j;
  } else {
    ctx.junction = std::nullopt;
  }
  if (!controlled) {
    prevLaneS = front;
    return;
  }
  if (dist < 9 && dist > -1 && std::abs(p.speed) < 0.25) stoppedNearLine = true;

  // crossing the stop / give-way line
  if (prevLaneS < lane_.stopAt && front >= lane_.stopAt && p.speed > 0.3) {
    bool onArrow = false;
    if (arm.sg >= 0 && node.sc >= 0) {
      const int st = signals.get(node.sc, arm.sg);
      const double age = signals.age(node.sc, arm.sg);
      if ((st == SIG_RED || st == SIG_RED_AMBER) && signals.arrow(&arm) >= 0) {
        // The green arrow beside the light is lit (or has just turned amber): whoever turns right
        // may go.  Whether the car does turn right is seen when it leaves the junction (judgeTurn).
        onArrow = true;
      } else if (st == SIG_RED || st == SIG_RED_AMBER) {
        // judged in judgeRedLine(); a moment of grace right after the change to red
        redLine = RedLine{&lane_, p.x, p.z, jsMax(1, jsMin(lane_.len - lane_.stopAt, 2)), st == SIG_RED && age < 0.6 ? "major" : "fail", false};
      } else if (st == SIG_AMBER && age > 1 + p.speed / 7) {
        // Amber: stop if that is possible with normal braking.  Whoever was
        // closer than reaction distance plus a 3.5 m/s² stop when it came on
        // may carry on – the amber phase is timed for exactly that.
        report("amberLight", "minor", p, {}, 8);
      }
    } else if (arm.role == ROLE_STOP && !stoppedNearLine) {
      report("stopSign", "major", p, {}, 8);
    }
    Entry e;
    e.node = &node;
    e.arm = &arm;
    e.lane = &lane_;
    e.time = time;
    // seconds of signalling before the line; a signal that is on right now always counts
    e.indicatorL = jsMax(indL, p.indicator < 0 && !p.hazard ? 1 : 0);
    e.indicatorR = jsMax(indR, p.indicator > 0 && !p.hazard ? 1 : 0);
    e.nowL = p.indicator < 0 && !p.hazard;
    e.nowR = p.indicator > 0 && !p.hazard;
    e.lookedL = lookL > 0.2;
    e.lookedR = lookR > 0.2;
    e.yaw = p.yaw;
    e.signalRed = false;
    e.onArrow = onArrow;
    e.vMax = std::abs(p.speed);
    entry = e;
    // roundabout: no indicator when entering (§ 8 (1a) StVO)
    if (node.ctrl == CTRL_ROUNDABOUT && !(lane_.edge->flags & EF_ROUNDABOUT) && p.indicator != 0 && !p.hazard) {
      report("roundaboutEntryIndicator", "hint", p, {}, 20);
    }
  }
  prevLaneS = front;
  (void)dt;
  (void)fx;
  (void)fz;
}

void RuleMonitor::judgeRedLine(const PlayerSnapshot& p) {
  if (!redLine) return;
  RedLine& rl = *redLine;
  const Node& node = *rl.lane->to;
  const int st = signals.get(node.sc, rl.lane->armTo->sg);
  if (st != SIG_RED && st != SIG_RED_AMBER) {
    // green (or amber again) before the car got any further: a rolling start
    if (!rl.lineReported) report("stopLine", "minor", p, {}, 8);
    redLine = std::nullopt;
    return;
  }
  // backed off behind the line again
  if (lane == rl.lane && laneS < rl.lane->stopAt - 0.2) {
    redLine = std::nullopt;
    return;
  }
  const double beyond = jsHypot(p.x - rl.x, p.z - rl.z);
  if (beyond > rl.tol) {
    report("redLight", rl.severity, p, {}, 8);
    if (entry && entry->lane == rl.lane) entry->signalRed = true;
    redLine = std::nullopt;
  } else if (!rl.lineReported && std::abs(p.speed) < 0.25) {
    report("stopLine", "minor", p, {}, 8);
    rl.lineReported = true;
  }
}

void RuleMonitor::judgeTurn(Edge& exitEdge, const PlayerSnapshot& p) {
  Entry& e = *entry;
  Node& node = *e.node;
  const Arm* exitArm = nullptr;
  for (const Arm* a : node.arms) {
    if (a->edge == &exitEdge) {
      exitArm = a;
      break;
    }
  }
  if (!exitArm) return;
  if (node.arms.size() < 3) return;
  // turn angle from entry direction to exit direction (+ = left)
  const double inAng = jsAtan2(e.arm->dz, -e.arm->dx);  // direction of travel into the node = -(dx,dz); angleOf = atan2(-dz, dx)
  const double outAng = jsAtan2(-exitArm->dz, exitArm->dx);
  double d = outAng - inAng;
  while (d > PI) d -= 2 * PI;
  while (d < -PI) d += 2 * PI;
  const double ad = std::abs(d);
  int turn = ad <= 0.62 ? TURN_STRAIGHT : d > 0 ? TURN_LEFT : TURN_RIGHT;
  // same classification as the lane graph (and therefore as the navigation hints)
  const Connector* conn = nullptr;
  for (const Connector* c : e.lane->out) {
    if (c->to->edge == &exitEdge) {
      conn = c;
      break;
    }
  }
  if (conn) turn = conn->turn == TURN_UTURN ? TURN_LEFT : conn->turn;
  // over the line on red with the green arrow lit – and then not turned right: the arrow was not for this car
  if (e.onArrow && turn != TURN_RIGHT) {
    report("redLight", "fail", p, {}, 8);
    e.signalRed = true;
  }
  // an oncoming car had to brake while we crossed: that is on us if we were the one turning left
  if (oncoming) {
    if (oncoming->node == &node && time - oncoming->time < 25 && turn == TURN_LEFT) report("leftTurnOncoming", "major", p, {}, 10);
    oncoming = std::nullopt;
  }
  indL = 0;
  indR = 0;
  const bool ringIn = (e.arm->edge->flags & EF_ROUNDABOUT) != 0;
  const bool ringOut = (exitEdge.flags & EF_ROUNDABOUT) != 0;
  if (node.ctrl == CTRL_ROUNDABOUT) {
    // leaving the roundabout must be indicated to the right
    if (ringIn && !ringOut && e.indicatorR < 0.6) report("roundaboutExitIndicator", "minor", p, {}, 10);
    return;
  }
  // following a priority road round a bend counts as turning, too – keep it simple: by geometry
  if (turn == TURN_LEFT) {
    if (e.indicatorL < 0.8) report(e.indicatorR > 0.8 ? "wrongIndicator" : "noIndicator", "minor", p, {{"dir", std::string("left")}}, 6);
    if (!e.lookedL && strict) report("noShoulderCheck", "hint", p, {{"dir", std::string("left")}}, 6);
  } else if (turn == TURN_RIGHT) {
    if (e.indicatorR < 0.8) report(e.indicatorL > 0.8 ? "wrongIndicator" : "noIndicator", "minor", p, {{"dir", std::string("right")}}, 6);
    if (!e.lookedR) report("noShoulderCheck", "hint", p, {{"dir", std::string("right")}}, 6);
    // a truck turns right at walking pace in town: cyclists and pedestrians beside it are hard to see
    if (heavy && context.urban && e.vMax * 3.6 > 12) report("truckRightTurn", "minor", p, {{"speed", jsRound(e.vMax * 3.6)}}, 6);
  } else if ((e.nowL && e.indicatorL > 2.5) || (e.nowR && e.indicatorR > 2.5)) {
    report("wrongIndicator", "hint", p, {{"dir", std::string("none")}}, 10);
  }
  // entering forbidden directions
  if (world.isBanned(*e.arm->edge, node, exitEdge)) report("turnBan", "major", p, {}, 6);
}

void RuleMonitor::judgePriority(const TrafficCar& car, const PlayerSnapshot& p, bool cutIn) {
  const Lane* lane_ = lane;
  const Entry* entry_ = entry ? &*entry : nullptr;
  const Node* node = entry_ ? entry_->node : lane_ && lane_->stopAt - laneS < 12 ? lane_->to : nullptr;
  const Arm* myArm = entry_ ? entry_->arm : lane_ ? lane_->armTo : nullptr;
  const Arm* carArm = car.conn ? car.conn->from->armTo : car.lane ? car.lane->armTo : nullptr;
  if (!node || !myArm || !carArm || carArm->node != node || carArm == myArm) {
    // Not a matter of this junction: pulled out or cut in right in front of someone who travels
    // the same way.  (A car coming from the side at a junction made of several nodes – a dual
    // carriageway, a roundabout – brakes for whoever is in its way, whoever has the right of
    // way there: that cannot be judged from here, and is not held against the driver.)
    // (And a car that saw the driver ahead from far away was not cut up: its distance is its own business.)
    const double along = jsSin(car.yaw) * jsSin(p.yaw) + jsCos(car.yaw) * jsCos(p.yaw);
    if (cutIn && std::abs(p.speed) > 1 && along > 0.6) report("cutOff", "minor", p, {}, 15);
    return;
  }
  if (entry_ && entry_->signalRed) return;  // already recorded as a red-light offence
  const auto rank = [](const Arm& a) { return a.role == ROLE_PRIORITY ? 3 : a.role == ROLE_RBL ? 2 : a.role == ROLE_YIELD || a.role == ROLE_STOP ? 1 : 0; };
  // § 9 (3), (4): whoever turns left lets oncoming traffic through – also those turning right.
  // A car that turns left across our way and has to brake for us was the one to wait.
  const Connector* theirPath = car.conn ? car.conn : car.next;
  const bool theyTurnLeft = theirPath && (theirPath->turn == TURN_LEFT || theirPath->turn == TURN_UTURN);
  // Whether the driver is the one turning left is known when the car leaves the junction
  // (judgeTurn): going straight on or turning right, it had the right of way over that car.
  if (node->ctrl == CTRL_SIGNALS && myArm->sg >= 0 && carArm->sg >= 0) {
    if (myArm->sg == carArm->sg && !theyTurnLeft) oncoming = Oncoming{const_cast<Node*>(node), time};
    return;
  }
  const int mine = rank(*myArm);
  const int theirs = rank(*carArm);
  if (mine < theirs) {
    const char* id = myArm->role == ROLE_MINOR ? "priorityDriveway" : node->ctrl == CTRL_ROUNDABOUT ? "priorityRoundabout" : "prioritySign";
    report(id, "fail", p, {}, 10);
  } else if (mine == theirs) {
    const bool opposite = myArm->dx * carArm->dx + myArm->dz * carArm->dz < -0.64;
    if (opposite || myArm->role == ROLE_PRIORITY) {
      if (!theyTurnLeft) oncoming = Oncoming{const_cast<Node*>(node), time};
    } else {
      // is the other one coming from our right?
      const double rx = myArm->dz;
      const double rz = -myArm->dx;
      if (carArm->dx * rx + carArm->dz * rz > 0.25) report("rightBeforeLeft", "fail", p, {}, 10);
    }
  }
}

void RuleMonitor::checkCrossings(const Edge& edge, double s, int dir, const PlayerSnapshot& p) {
  for (const CrossingJson& c : edge.xings) {
    if (c.k != XING_ZEBRA && c.k != XING_SIGNAL) continue;
    // signed distance of the front bumper to the near edge of the crossing
    const double d = (c.s - s) * dir - c.w / 2 - (p.front ? *p.front : p.length / 2);
    const auto was = crossingState.find(&c);
    const bool known = was != crossingState.end();
    const double prev = known ? was->second : 0;
    crossingState[&c] = d;
    if (!known) continue;
    if (c.k == XING_SIGNAL && c.sc) {
      // the crossing is the protected area; the bumper a hand's breadth into it is not yet "over"
      if (!(prev > -0.3 && d <= -0.3)) continue;
      const int st = signals.get(*c.sc, c.g.value_or(0));
      if (st == SIG_RED || st == SIG_RED_AMBER) report("redLight", "fail", p, {}, 8);
    } else if (c.k == XING_ZEBRA) {
      if (!(prev > 0 && d <= 0)) continue;
      const Pedestrian* ped = traffic.pedestrianAt(c);
      if (ped && (ped->state == "cross" || ped->waited > 0.6)) report("zebra", "fail", p, {}, 8);
      else if (std::abs(p.speed) * 3.6 > 35 && ped) report("zebraSpeed", "minor", p, {}, 8);
    }
  }
}

}  // namespace fs

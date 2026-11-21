// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The driving examiner.
 *
 * Origin: src/sim/rules.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * The judging logic mirrors the JavaScript original: same thresholds, same
 * cooldowns, same severities.
 */
#include "sim/rules.h"

#include <algorithm>
#include <cmath>

namespace fs {

namespace {
const std::map<Severity, int> SEVERITY_POINTS = {
    {Severity::Hint, 0}, {Severity::Minor, 1}, {Severity::Major, 3}, {Severity::Fail, 10}};
}  // namespace

int severityPoints(Severity s) {
  auto it = SEVERITY_POINTS.find(s);
  return it == SEVERITY_POINTS.end() ? 0 : it->second;
}

RuleMonitor::RuleMonitor(World& world, LaneGraph& graph, SignalSystem& signals, Traffic& traffic)
    : world(world), graph(graph), signals(signals), traffic(traffic) {}

void RuleMonitor::reset() {
  log.clear();
  cooldown_.clear();
  lane_ = nullptr;
  entry_ = nullptr;
  hasRedLine_ = false;
  lastEdge_ = nullptr;
  time = 0;
  speedOver_ = wrongSide_ = wrongWayT_ = sidewalkT_ = offRoadT_ = tailgateT_ = 0;
}

int RuleMonitor::points() const {
  int total = 0;
  for (const auto& v : log) total += severityPoints(v.severity);
  return total;
}

bool RuleMonitor::failed() const {
  return std::any_of(log.begin(), log.end(),
                     [](const Violation& v) { return v.severity == Severity::Fail; });
}

void RuleMonitor::report(const std::string& id, Severity severity, const PlayerSnapshot& p,
                         const std::vector<std::pair<std::string, std::string>>& data,
                         double cooldown) {
  auto it = cooldown_.find(id);
  double until = it == cooldown_.end() ? -1 : it->second;
  if (time < until) return;
  cooldown_[id] = time + cooldown;
  if (strict && severity == Severity::Hint) severity = Severity::Minor;
  Violation v;
  v.id = id;
  v.severity = severity;
  v.time = time;
  v.x = p.x;
  v.z = p.z;
  v.data = data;
  log.push_back(v);
  if (onViolation) onViolation(log.back());
}

Lane* RuleMonitor::laneFor(const Edge& e, double s, double off, int dir) {
  const LaneGraph::EdgeLanes& el = graph.edgeLanes[static_cast<size_t>(e.id)];
  const auto& list = dir > 0 ? el.fwd : el.back;
  if (list.empty()) return nullptr;
  Lane* best = nullptr;
  double bd = 1e300;
  for (Lane* l : list) {
    double d = std::fabs(l->off - off);
    if (d < bd) {
      bd = d;
      best = l;
    }
  }
  (void)s;
  return best;
}

int RuleMonitor::segIndex(const Edge& e, double s) const {
  const auto& cum = e.pl.cum;
  size_t lo = 0, hi = cum.size() - 2;
  while (lo < hi) {
    size_t mid = (lo + hi + 1) >> 1;
    if (cum[mid] <= s) lo = mid;
    else hi = mid - 1;
  }
  return static_cast<int>(lo);
}

void RuleMonitor::note(const std::string& id, Severity severity,
                       const std::vector<std::pair<std::string, std::string>>& data,
                       double cooldown) {
  auto it = cooldown_.find(id);
  double until = it == cooldown_.end() ? -1 : it->second;
  if (time < until) return;
  cooldown_[id] = time + cooldown;
  Violation v;
  v.id = id;
  v.severity = severity;
  v.time = time;
  v.x = lastX_;
  v.z = lastZ_;
  v.data = data;
  log.push_back(v);
  if (onViolation) onViolation(log.back());
}

void RuleMonitor::update(double dt, const PlayerSnapshot& p, const std::vector<TrafficEvent>& events) {
  time += dt;
  lastX_ = p.x;
  lastZ_ = p.z;
  RuleContext& ctx = context;
  double fx = -std::sin(p.yaw);
  double fz = -std::cos(p.yaw);
  double kmh = std::fabs(p.speed) * 3.6;
  bool moving = std::fabs(p.speed) > 0.6;

  // indicator / look history (seconds within a sliding window)
  auto decay = [&](double v, bool on) {
    return on ? std::min(6.0, v + dt) : std::max(0.0, v - dt * 1.5);
  };
  indL_ = decay(indL_, p.indicator < 0 && !p.hazard);
  indR_ = decay(indR_, p.indicator > 0 && !p.hazard);
  lookL_ = decay(lookL_, p.lookYaw > 0.9);
  lookR_ = decay(lookR_, p.lookYaw < -0.9);

  // --- where are we? ---
  bool onSurface = world.surfaceAt(p.x, p.z, p.y + 0.6, hit_);
  SurfaceHit& hit = hit_;
  ctx.onRoad = onSurface && hit.zone == 0;
  ctx.wrongWay = false;
  Edge* edge = onSurface ? hit.edge : nullptr;
  // A wheel over the edge line or a clipped corner must not make us lose
  // track of the road we are on: fall back to the nearest carriageway.
  bool near = false;
  if (!onSurface || (hit.zone != 0 && !hit.edge)) {
    if (world.locate(p.x, p.z, p.y, 2.2, frontHit_) && frontHit_.edge) {
      near = true;
      edge = frontHit_.edge;
      hit.edge = edge;
      hit.s = frontHit_.s;
      hit.off = frontHit_.off;
    }
  }
  int dir = 0;
  double headingCos = 0;
  if (edge) {
    // travel direction relative to the edge
    const Polyline3& pl = edge->pl;
    size_t i = static_cast<size_t>(std::min(static_cast<int>(pl.cum.size()) - 2,
                                            std::max(0, segIndex(*edge, hit.s))));
    double tx = pl.pts[i * 3 + 3] - pl.pts[i * 3];
    double tz = pl.pts[i * 3 + 5] - pl.pts[i * 3 + 2];
    double tl = std::hypot(tx, tz);
    if (tl == 0) tl = 1;
    // Which way the car faces, not which way it creeps: at a standstill the
    // speed is ±0, and a car that rolls back a hand's breadth at the stop
    // line has not changed to the oncoming lane.
    headingCos = (fx * tx + fz * tz) / tl;
    dir = headingCos >= 0 ? 1 : -1;
  }

  if (edge && (hit.zone == 0 || near)) {
    int limit = dir > 0 ? static_cast<int>(edge->maxF) : static_cast<int>(edge->maxB);
    // car parks, driveways and tracks have no signed limit: nothing to show
    bool unsignedRoad = edge->cls >= SERVICE && edge->zone == 0;
    ctx.limit = edge->zone == 7 ? 7 : (unsignedRoad ? -1 : limit);
    ctx.zone = edge->zone;
    ctx.urban = (edge->flags & EF_URBAN) != 0;
    ctx.roadName = edge->name.empty() ? edge->ref : edge->name;
    Lane* lane = laneFor(*edge, hit.s, hit.off, dir);
    bool oneWayAgainst = (dir > 0 ? edge->lanesF : edge->lanesB) == 0;

    // --- wrong way in a one-way street ---
    // crossing a one-way carriageway at a junction is not driving against it
    bool inJunctionZone = hit.s < edge->cutA + 2 || hit.s > edge->len - edge->cutB - 2;
    if (oneWayAgainst && moving && p.speed > 0 && std::fabs(headingCos) > 0.7 &&
        !inJunctionZone && (edge->flags & EF_MINOR_SERVICE) == 0) {
      wrongWayT_ += dt;
      ctx.wrongWay = true;
      if (wrongWayT_ > 1.2)
        report("wrongWay", edge->cls <= TRUNK_LINK ? Severity::Fail : Severity::Major, p, {}, 20);
    } else {
      wrongWayT_ = 0;
    }

    // --- keep right ---
    if (lane && !oneWayAgainst && !inJunctionZone && std::fabs(headingCos) > 0.8 &&
        edge->lanesF > 0 && edge->lanesB > 0 && edge->laneW >= 2.5 && moving && p.speed > 3) {
      double myOff = hit.off * dir;  // + = left of our direction
      double centre = (dir > 0 ? -edge->width / 2 + edge->margin + edge->lanesF * edge->laneW
                               : edge->width / 2 - edge->margin - edge->lanesB * edge->laneW) * dir;
      // car centre beyond the middle of the road
      if (myOff > centre + 0.45) {
        wrongSide_ += dt;
        if (wrongSide_ > 3.5)
          report("keepRight", (edge->flags & EF_MARKED) != 0 ? Severity::Major : Severity::Minor, p,
                 {}, 25);
      } else {
        wrongSide_ = std::max(0.0, wrongSide_ - dt * 2);
      }
    }

    // --- speed ---
    int lim = unsignedRoad ? 35 : ctx.limit;
    if (lim > 0) {
      int tolerance = lim <= 10 ? 5 : 6;
      double over = kmh - lim;
      if (over > tolerance) {
        speedOver_ += dt;
        if (speedOver_ > 1.6) {
          Severity sev = over > 25 ? Severity::Fail : (over > 14 ? Severity::Major : Severity::Minor);
          report(edge->zone == 7 ? "speedWalk" : "speed", sev, p,
                 {{"limit", std::to_string(lim)}, {"speed", std::to_string(std::lround(kmh))}},
                 sev == Severity::Minor ? 18 : 10);
        }
      } else {
        speedOver_ = std::max(0.0, speedOver_ - dt * 2);
      }
    }

    // --- restricted roads ---
    if (edge != lastEdge_) {
      if (edge->access == ACCESS_PRIVATE && edge->tier >= 1)
        report("accessPrivate", Severity::Minor, p, {}, 30);
      else if (edge->access == ACCESS_AGRICULTURAL)
        report("accessAgricultural", Severity::Minor, p, {}, 30);
      else if (edge->access == ACCESS_DESTINATION &&
               (!allowed || !allowed->count(edge)))
        report("accessDestination", Severity::Hint, p, {}, 60);
      lastEdge_ = edge;
    }

    // --- lane tracking: stop lines, signals, entering the junction ---
    if (lane && p.speed >= -0.5) {
      trackLane(*lane, hit.s, dt, p, fx, fz);
    } else if (!lane) {
      lane_ = nullptr;
    }
  } else {
    ctx.limit = -1;
    wrongWayT_ = 0;
    wrongSide_ = 0;
    // inside a junction area we keep the lane we came from; far from any road we forget it
    if (!onSurface || hit.node == nullptr) {
      lostT_ += dt;
      if (lostT_ > 1.5) lane_ = nullptr;
    } else {
      lostT_ = 0;
    }
  }
  if (edge) lostT_ = 0;

  // --- sidewalk and off-road ---
  if (onSurface && hit.zone == 1 && moving) {
    sidewalkT_ += dt;
    if (sidewalkT_ > 0.7) report("sidewalk", Severity::Major, p, {}, 15);
  } else {
    sidewalkT_ = 0;
  }
  if (!onSurface && moving) {
    offRoadT_ += dt;
    if (offRoadT_ > 2.5) report("offRoad", Severity::Minor, p, {}, 20);
  } else {
    offRoadT_ = 0;
  }

  judgeRedLine(p);

  // --- leaving the junction: was the turn signalled? ---
  if (entry_ && edge && (hit.zone == 0 || near)) {
    Entry e = *entry_;
    if (edge != e.arm->edge || time - e.time > 25) {
      judgeTurn(*edge, p);
      entry_ = nullptr;
    }
  }

  // --- crossings (zebra, pedestrian lights) ---
  if (edge && moving && p.speed > 0) checkCrossings(*edge, hit.s, dir, p);

  // --- events from the traffic ---
  for (const auto& ev : events) {
    if (auto* c = std::get_if<CollisionEvent>(&ev)) {
      report("crash", Severity::Fail, p,
             {{"speed", std::to_string(std::lround(c->speed * 3.6))}}, 6);
    } else if (auto* pe = std::get_if<PedestrianEndangeredEvent>(&ev)) {
      (void)pe;
      report("pedestrian", Severity::Fail, p, {}, 8);
    } else if (auto* fb = std::get_if<ForcedBrakeEvent>(&ev)) {
      judgePriority(*fb->car, p);
    }
  }
  if (p.hitScenery > 1.2)
    report("crashObject", p.hitScenery > 4 ? Severity::Fail : Severity::Major, p,
           {{"speed", std::to_string(std::lround(p.hitScenery * 3.6))}}, 5);

  // --- small things ---
  if (p.stalled) report("stall", Severity::Hint, p, {}, 3);
  if (!p.seatbelt && kmh > 8) report("seatbelt", Severity::Minor, p, {}, 120);
  if (p.handbrake > 0.5 && kmh > 12) {
    handbrakeT_ += dt;
    if (handbrakeT_ > 2) report("handbrakeOn", Severity::Hint, p, {}, 30);
  } else {
    handbrakeT_ = 0;
  }
  if (p.indicator != 0 && !p.hazard && kmh > 20) {
    indicatorOnT_ += dt;
    if (indicatorOnT_ > 25) report("indicatorLeftOn", Severity::Hint, p, {}, 40);
  } else {
    indicatorOnT_ = 0;
  }

  // --- following distance ---
  if (kmh > 30) {
    double gap = 1e300;
    for (const auto& c : traffic.cars) {
      double dx = c.x - p.x;
      double dz = c.z - p.z;
      double lf = dx * fx + dz * fz;
      if (lf < 2 || lf > 60) continue;
      double lr = std::fabs(dx * fz - dz * fx);
      if (lr > 1.3 || std::fabs(c.y - p.y) > 2) continue;
      gap = std::min(gap, lf - 4.3);
    }
    // "halber Tacho" outside towns, about one second in town
    double need = std::fabs(p.speed) * (ctx.urban ? 0.75 : 1.2);
    if (gap < need * 0.6) {
      tailgateT_ += dt;
      if (tailgateT_ > 3)
        report("distance", gap < need * 0.35 ? Severity::Major : Severity::Minor, p,
               {{"gap", std::to_string(std::lround(gap))}}, 25);
    } else {
      tailgateT_ = std::max(0.0, tailgateT_ - dt);
    }
  } else {
    tailgateT_ = 0;
  }
}

void RuleMonitor::trackLane(Lane& lane, double s, double dt, const PlayerSnapshot& p,
                            double fx, double fz) {
  (void)dt;
  (void)fx;
  (void)fz;
  RuleContext& ctx = context;
  // position of the front bumper along the lane
  double centre = lane.dir > 0 ? s - lane.s0 : lane.s0 - s;
  double front = centre + p.length / 2;
  if (&lane != lane_) {
    lane_ = &lane;
    prevLaneS_ = front;
    stoppedNearLine_ = false;
  }
  laneS_ = front;
  Node* node = lane.to;
  Arm* arm = lane.armTo;
  if (!node || !arm) return;
  double dist = lane.stopAt - front;
  bool controlled = node->arms.size() > 2;
  if (controlled && dist < 150 && dist > -2) {
    ctx.junction.set = true;
    ctx.junction.dist = std::max(0.0, dist);
    ctx.junction.role = arm->role;
    ctx.junction.ctrl = node->ctrl;
    ctx.junction.signal =
        arm->sg >= 0 && node->sc >= 0 ? signals.get(node->sc, arm->sg) : -1;
  } else {
    ctx.junction.set = false;
  }
  if (!controlled) {
    prevLaneS_ = front;
    return;
  }
  if (dist < 9 && dist > -1 && std::fabs(p.speed) < 0.25) stoppedNearLine_ = true;

  // crossing the stop / give-way line
  if (prevLaneS_ < lane.stopAt && front >= lane.stopAt && p.speed > 0.3) {
    if (arm->sg >= 0 && node->sc >= 0) {
      int st = signals.get(node->sc, arm->sg);
      double age = signals.age(node->sc, arm->sg);
      if (st == SIG_RED || st == SIG_RED_AMBER) {
        // judged in judgeRedLine(); a moment of grace right after the change to red
        redLine_.lane = &lane;
        redLine_.x = p.x;
        redLine_.z = p.z;
        redLine_.tol = std::max(1.0, std::min(lane.len - lane.stopAt, 2.0));
        redLine_.severity = st == SIG_RED && age < 0.6 ? Severity::Major : Severity::Fail;
        redLine_.lineReported = false;
        hasRedLine_ = true;
      } else if (st == SIG_AMBER && age > 1 + p.speed / 7) {
        // Amber: stop if that is possible with normal braking.  Whoever was
        // closer than reaction distance plus a 3.5 m/s² stop when it came on
        // may carry on – the amber phase is timed for exactly that.
        report("amberLight", Severity::Minor, p, {}, 8);
      }
    } else if (arm->role == ROLE_STOP && !stoppedNearLine_) {
      report("stopSign", Severity::Major, p, {}, 8);
    }
    entryStore_.node = node;
    entryStore_.arm = arm;
    entryStore_.lane = &lane;
    entryStore_.time = time;
    // seconds of signalling before the line; a signal that is on right now always counts
    entryStore_.indicatorL = std::max(indL_, (p.indicator < 0 && !p.hazard) ? 1.0 : 0.0);
    entryStore_.indicatorR = std::max(indR_, (p.indicator > 0 && !p.hazard) ? 1.0 : 0.0);
    entryStore_.nowL = p.indicator < 0 && !p.hazard;
    entryStore_.nowR = p.indicator > 0 && !p.hazard;
    entryStore_.lookedL = lookL_ > 0.2;
    entryStore_.lookedR = lookR_ > 0.2;
    entryStore_.yaw = p.yaw;
    entryStore_.signalRed = false;
    entry_ = &entryStore_;
    // roundabout: no indicator when entering (§ 8 (1a) StVO)
    if (node->ctrl == CTRL_ROUNDABOUT && (lane.edge->flags & EF_ROUNDABOUT) == 0 &&
        p.indicator != 0 && !p.hazard) {
      report("roundaboutEntryIndicator", Severity::Hint, p, {}, 20);
    }
  }
  prevLaneS_ = front;
}

void RuleMonitor::judgeRedLine(const PlayerSnapshot& p) {
  if (!hasRedLine_) return;
  Lane* lane = redLine_.lane;
  if (!lane || !lane->to || !lane->armTo) {
    hasRedLine_ = false;
    return;
  }
  Node* node = lane->to;
  int st = signals.get(node->sc, lane->armTo->sg);
  if (st != SIG_RED && st != SIG_RED_AMBER) {
    // green (or amber again) before the car got any further: a rolling start
    if (!redLine_.lineReported) report("stopLine", Severity::Minor, p, {}, 8);
    hasRedLine_ = false;
    return;
  }
  // backed off behind the line again
  if (lane_ == lane && laneS_ < lane->stopAt - 0.2) {
    hasRedLine_ = false;
    return;
  }
  double beyond = std::hypot(p.x - redLine_.x, p.z - redLine_.z);
  if (beyond > redLine_.tol) {
    report("redLight", redLine_.severity, p, {}, 8);
    if (entry_ && entry_->lane == lane) entry_->signalRed = true;
    hasRedLine_ = false;
  } else if (!redLine_.lineReported && std::fabs(p.speed) < 0.25) {
    report("stopLine", Severity::Minor, p, {}, 8);
    redLine_.lineReported = true;
  }
}

void RuleMonitor::judgeTurn(Edge& exitEdge, const PlayerSnapshot& p) {
  if (!entry_) return;
  Entry& e = *entry_;
  Node* node = e.node;
  Arm* exitArm = nullptr;
  for (auto& a : node->arms)
    if (a.edge == &exitEdge) {
      exitArm = &a;
      break;
    }
  if (!exitArm) return;
  if (node->arms.size() < 3) return;
  // turn angle from entry direction to exit direction (+ = left)
  double inAng = std::atan2(e.arm->dz, -e.arm->dx);
  double outAng = std::atan2(-exitArm->dz, exitArm->dx);
  double d = outAng - inAng;
  while (d > PI_) d -= TAU;
  while (d < -PI_) d += TAU;
  double ad = std::fabs(d);
  int turn = ad <= 0.62 ? TURN_STRAIGHT : (d > 0 ? TURN_LEFT : TURN_RIGHT);
  // same classification as the lane graph (and therefore as the navigation hints)
  for (Connector* c : e.lane->out) {
    if (c->to->edge == &exitEdge) {
      turn = c->turn == TURN_UTURN ? TURN_LEFT : c->turn;
      break;
    }
  }
  indL_ = 0;
  indR_ = 0;
  bool ringIn = (e.arm->edge->flags & EF_ROUNDABOUT) != 0;
  bool ringOut = (exitEdge.flags & EF_ROUNDABOUT) != 0;
  if (node->ctrl == CTRL_ROUNDABOUT) {
    // leaving the roundabout must be indicated to the right
    if (ringIn && !ringOut && e.indicatorR < 0.6)
      report("roundaboutExitIndicator", Severity::Minor, p, {}, 10);
    return;
  }
  // following a priority road round a bend counts as turning, too – keep it simple: by geometry
  if (turn == TURN_LEFT) {
    if (e.indicatorL < 0.8)
      report(e.indicatorR > 0.8 ? "wrongIndicator" : "noIndicator", Severity::Minor, p,
             {{"dir", "left"}}, 6);
    if (!e.lookedL && strict) report("noShoulderCheck", Severity::Hint, p, {{"dir", "left"}}, 6);
  } else if (turn == TURN_RIGHT) {
    if (e.indicatorR < 0.8)
      report(e.indicatorL > 0.8 ? "wrongIndicator" : "noIndicator", Severity::Minor, p,
             {{"dir", "right"}}, 6);
    if (!e.lookedR) report("noShoulderCheck", Severity::Hint, p, {{"dir", "right"}}, 6);
  } else if ((e.nowL && e.indicatorL > 2.5) || (e.nowR && e.indicatorR > 2.5)) {
    report("wrongIndicator", Severity::Hint, p, {{"dir", "none"}}, 10);
  }
  // entering forbidden directions
  if (world.isBanned(e.arm->edge->id, node->id, exitEdge.id))
    report("turnBan", Severity::Major, p, {}, 6);
}

void RuleMonitor::judgePriority(TrafficCar& car, const PlayerSnapshot& p) {
  Lane* lane = lane_;
  Entry* entry = entry_;
  Node* node = entry ? entry->node
                     : (lane && lane->stopAt - laneS_ < 12 ? lane->to : nullptr);
  Arm* myArm = entry ? entry->arm : (lane ? lane->armTo : nullptr);
  Arm* carArm = car.conn ? car.conn->from->armTo : (car.lane ? car.lane->armTo : nullptr);
  if (!node || !myArm || !carArm || carArm->node != node || carArm == myArm) {
    // not a junction matter: pulled out or cut in right in front of someone
    if (std::fabs(p.speed) > 1) report("cutOff", Severity::Minor, p, {}, 15);
    return;
  }
  if (entry && entry->signalRed) return;  // already recorded as a red-light offence
  auto rank = [](const Arm& a) {
    return a.role == ROLE_PRIORITY ? 3 : (a.role == ROLE_RBL ? 2
                                         : ((a.role == ROLE_YIELD || a.role == ROLE_STOP) ? 1 : 0));
  };
  if (node->ctrl == CTRL_SIGNALS && myArm->sg >= 0 && carArm->sg >= 0) {
    if (myArm->sg == carArm->sg) report("leftTurnOncoming", Severity::Major, p, {}, 10);
    return;
  }
  int mine = rank(*myArm);
  int theirs = rank(*carArm);
  if (mine < theirs) {
    const char* id = myArm->role == ROLE_MINOR
                         ? "priorityDriveway"
                         : (node->ctrl == CTRL_ROUNDABOUT ? "priorityRoundabout" : "prioritySign");
    report(id, Severity::Fail, p, {}, 10);
  } else if (mine == theirs) {
    bool opposite = myArm->dx * carArm->dx + myArm->dz * carArm->dz < -0.64;
    if (opposite || myArm->role == ROLE_PRIORITY) {
      report("leftTurnOncoming", Severity::Major, p, {}, 10);
    } else {
      // is the other one coming from our right?
      double rx = myArm->dz;
      double rz = -myArm->dx;
      if (carArm->dx * rx + carArm->dz * rz > 0.25)
        report("rightBeforeLeft", Severity::Fail, p, {}, 10);
    }
  }
}

void RuleMonitor::checkCrossings(const Edge& edge, double s, int dir, const PlayerSnapshot& p) {
  for (const auto& c : world.crossings()) {
    if (c.e != edge.id) continue;
    if (c.k != XING_ZEBRA && c.k != XING_SIGNAL) continue;
    // signed distance of the front bumper to the near edge of the crossing
    double d = (c.s - s) * dir - c.w / 2 - p.length / 2;
    auto it = crossingState_.find(&c);
    double prev = it == crossingState_.end() ? -1e300 : it->second;
    crossingState_[&c] = d;
    if (it == crossingState_.end()) continue;
    if (c.k == XING_SIGNAL && c.sc >= 0) {
      // the crossing is the protected area; the bumper a hand's breadth into
      // it is not yet "over"
      if (!(prev > -0.3 && d <= -0.3)) continue;
      int st = signals.get(c.sc, c.g >= 0 ? c.g : 0);
      if (st == SIG_RED || st == SIG_RED_AMBER) report("redLight", Severity::Fail, p, {}, 8);
    } else if (c.k == XING_ZEBRA) {
      if (!(prev > 0 && d <= 0)) continue;
      Pedestrian* ped = traffic.pedestrianAt(c);
      if (ped && (ped->state == "cross" || ped->waited > 0.6))
        report("zebra", Severity::Fail, p, {}, 8);
      else if (std::fabs(p.speed) * 3.6 > 35 && ped)
        report("zebraSpeed", Severity::Minor, p, {}, 8);
    }
  }
}

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/autodriver.ts @ v1.7.7
#include "sim/autodriver.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "sim/jsmath.h"
#include "world/types.h"

namespace fs {

namespace {

/** route.find((c) => c.from === lane) */
Connector* routeFrom(const std::vector<Connector*>& route, const Lane* lane) {
  for (Connector* c : route)
    if (c->from == lane) return c;
  return nullptr;
}

/** route.find((r) => r.from === l) ?? (l.out.length === 1 ? l.out[0] : null) */
Connector* onward(const std::vector<Connector*>& route, const Lane* l) {
  if (Connector* c = routeFrom(route, l)) return c;
  return l->out.size() == 1 ? l->out[0] : nullptr;
}

AutoDriverOptions resolved(const AutoDriverOptions& opts) {
  AutoDriverOptions out = opts;
  out.caps = opts.caps.has_value() ? *opts.caps : opts.truck ? std::optional<RuleMonitor::Caps>(RuleMonitor::Caps{60, 80}) : std::optional<RuleMonitor::Caps>();
  return out;
}

}  // namespace

AutoDriver::AutoDriver(World& world_, SignalSystem& signals_, RuleMonitor& rules_, Traffic* traffic_, const AutoDriverOptions& opts_)
    : world(world_),
      signals(signals_),
      rules(rules_),
      traffic(traffic_),
      opts(resolved(opts_)),
      // speed in a turn at a junction (a truck turns right at walking pace in town, § 9 Abs. 6 StVO)
      turnSpeed(opts.truck ? 2.7 : 4.5),
      // the deceleration the driver plans with when stopping at a line (15 t take longer)
      decel(opts.truck ? 1.3 : 2.2) {
  me.length = 4.3;
  me.width = 1.8;
  me.length = opts_.spec.length.value_or(4.28);
  me.width = opts_.spec.width.value_or(1.79);
}

double AutoDriver::bendCap(Lane* lane, double from, Connector* next, const std::vector<Connector*>& route, double speed) {
  const double STEP = 6;
  /** the turn is measured over 2 × SPAN steps: a kink in the line of a road is a gentle bend, a hairpin is not */
  const int SPAN = 2;
  const double pull = opts.truck ? 1.5 : 2.2;
  const double ease = opts.truck ? 1.1 : 1.7;
  const double reach = jsMin(240, (speed * speed) / (2 * ease) + 45);
  const int n = static_cast<int>(jsMin(48, std::ceil(reach / STEP) + 2 * SPAN + 1));
  std::array<float, 2 * 48>& dir = bendDir;
  PolySample& smp_ = bendSmp;
  // along the lane the car is on, the path through the junction behind it, the lane after that, …
  Lane* pieceLane = lane;
  Connector* pieceConn = lane ? nullptr : next;
  bool onLane = lane != nullptr;
  Connector* after = lane ? next : nullptr;
  double s = jsMax(0, from);
  int count = 0;
  while (count < n && (pieceLane || pieceConn)) {
    const double len = onLane ? pieceLane->len : pieceConn->len;
    if (s > len) {
      s -= len;
      if (onLane) {
        Lane* l = pieceLane;
        pieceConn = after ? after : onward(route, l);
        pieceLane = nullptr;
        after = nullptr;
      } else {
        pieceLane = pieceConn->to;
        pieceConn = nullptr;
      }
      onLane = !onLane;
      continue;
    }
    if (onLane) smp_ = lanePoint(*pieceLane, s, smp_);
    else smp_ = samplePolyline(pieceConn->pl, s, smp_);
    dir[static_cast<size_t>(count * 2)] = static_cast<float>(smp_.tx);
    dir[static_cast<size_t>(count * 2 + 1)] = static_cast<float>(smp_.tz);
    count++;
    s += STEP;
  }
  double cap = JS_INF;
  for (int i = SPAN; i + SPAN < count; i++) {
    const size_t a = static_cast<size_t>((i - SPAN) * 2);
    const size_t b = static_cast<size_t>((i + SPAN) * 2);
    const double turn = jsAcos(jsMax(-1, jsMin(1, static_cast<double>(dir[a]) * static_cast<double>(dir[b]) + static_cast<double>(dir[a + 1]) * static_cast<double>(dir[b + 1]))));
    if (turn < 0.05) continue;
    const double bend = std::sqrt((pull * 2 * SPAN * STEP) / turn);
    // (the bend begins where the stretch it was measured over begins)
    const double d = jsMax(0, (i - SPAN) * STEP - 4);
    cap = jsMin(cap, std::sqrt(bend * bend + 2 * ease * d));
  }
  return cap;
}

void AutoDriver::reset() {
  bendTick = 0;
  bendLimit = JS_INF;
  stoppedFor = 0;
  yieldWait = 0;
  changeT = 0;
  netWait = 0;
  lastTurn = 0;
  lastNext = nullptr;
  haltLane = nullptr;
  past = nullptr;
  cmd.arrived = false;
  cmd.turnRound = nullptr;
}

AutoCommand& AutoDriver::step(double dt, const Vehicle& v, const double* mpos, const std::vector<Connector*>& route) {
  const VehicleSpec& spec = opts.spec;
  const double FRONT = opts.front;
  const bool TRUCK = opts.truck;
  const bool obey = opts.obey;
  const std::optional<RuleMonitor::Caps>& caps = *opts.caps;
  const double TURN_SPEED = turnSpeed;
  const double DECEL = decel;
  Lane* const lane = rules.laneNow();
  const double laneS = rules.laneFront();
  AutoRemark remark = "";

  // --- path: current lane, then the route's connectors ---
  Connector* next = nullptr;
  /** the lane the route goes on from is taken: wait for a gap before moving over */
  bool laneBlocked = false;
  /** moving over to another lane of the carriageway: -1 to the left, 1 to the right */
  int change = 0;
  // the lane to follow: the one the monitor has us in – or its neighbour, if the route goes on from that one
  Lane* path = lane;
  if (lane) {
    next = routeFrom(route, lane);
    if (!next) {
      Connector* rc = nullptr;
      for (Connector* c : route) {
        if (c->from->edge == lane->edge && c->from->dir == lane->dir) {
          rc = c;
          break;
        }
      }
      if (rc) {
        next = rc;
        path = rc->from;  // get in lane
        // to which side: indicated, and looked over the shoulder, before the car moves over
        change = (rc->from->off - lane->off) * lane->dir > 0 ? -1 : 1;
        // … and only where there is room: nobody beside, nobody coming up from behind
        if (traffic && !traffic->roomOn(*rc->from, laneS - FRONT, jsMax(0, v.speed), me.length)) {
          path = lane;
          laneBlocked = true;
        } else if (traffic && changeT < 1) {
          path = lane;
        }
      } else {
        next = lane->out.size() == 1 ? lane->out[0] : nullptr;
      }
    }
  }
  // roadworks (src/sim/roadevents): a lane closed ahead – over to the open one in good time, and not back before its end
  RoadEvents* works = traffic ? traffic->roadEvents : nullptr;
  // kerb build-outs (src/sim/narrowings): through the gap they leave
  Narrowings* narrow = traffic ? traffic->narrowings : nullptr;
  const double nhw = me.width / 2;
  // level crossings (src/sim/levelcrossing): moderately towards one without barriers; before the cross while it says so
  LevelCrossings* lxs = traffic ? traffic->levelCrossings : nullptr;
  double limit = 8;
  bool crossStop = false;
  const auto levelCrossing = [&](const LxGate& g) {
    if (g.cap < limit) limit = g.cap;
    if (!obey || g.stop == JS_INF) return;
    const double dd = jsMax(0, g.stop - 0.3);
    limit = jsMin(limit, std::sqrt(2 * DECEL * dd));
    if (dd < 0.6) crossStop = true;
    remark = g.why == "queue" ? "follow" : "red";
  };
  const double nhl = me.length / 2;
  if (works && lane) {
    if (path && path != lane && !works->laneOpen(*path, laneS)) {
      path = lane;
      change = 0;
      laneBlocked = false;
    }
    Lane* open = works->laneOpen(*lane, laneS) ? nullptr : works->openLane(*lane);
    if (open) {
      change = (open->off - lane->off) * lane->dir > 0 ? -1 : 1;
      path = traffic && (!traffic->roomOn(*open, laneS - FRONT, jsMax(0, v.speed), me.length) || changeT < 1) ? lane : open;
    }
  }
  if (next) lastNext = next;
  else if (!lane) next = lastNext;
  // pure pursuit target point: lookahead along lane → connector → next lane
  const double speed = jsMax(0, v.speed);
  // look less far ahead in tight turns so the corner is not cut
  const bool tight = next != nullptr && next->turn != TURN_STRAIGHT && next->len < 25;
  // (and in a very tight one – the way out of a car park into a narrow street – hardly at all:
  // the car's middle would otherwise run over the corner of the pavement)
  const double la = tight ? jsMin(2.6 + speed * 0.45, 1.5 + next->len * 0.22) : 5 + speed * 0.9;
  double tx = mpos[0] - jsSin(v.yaw) * 5;
  double tz = mpos[2] - jsCos(v.yaw) * 5;
  double distToLine = JS_INF;
  bool mustStop = false;
  int turn = 0;
  /** distance to a junction behind joints of the road, where the turn to announce is */
  double ahead = JS_INF;
  // A learner looks further than the next junction: every turn within braking
  // distance along the route limits the speed (short links between junctions
  // would otherwise hide a sharp turn until it is too late).
  // `first` begins `dist` metres ahead of the front bumper (negative: we are on it).
  /**
   * How fast the car may still be `d` metres before a turn at a junction:
   * one steady easing off from far away down to the speed of the turn – no
   * step in it anywhere, or whoever follows would have to brake hard.
   */
  const auto turnCap = [&](double d) {
    const double near = jsMin(45, jsMax(0, d));
    return TURN_SPEED + near * (TRUCK ? 0.08 : 0.12) + jsMax(0, d - 45) * (TRUCK ? 0.045 : 0.065);
  };
  /** how fast a lane may be driven: its limit, walking pace in a traffic-calmed area, slowly on yards and car parks */
  const auto allowed = [&](const Lane& l) {
    double a = std::isfinite(l.limit) ? l.limit : 36;
    if (caps) a = jsMin(a, l.edge->cls <= MOTORWAY_LINK ? caps->motorway / 3.6 : (l.edge->flags & EF_URBAN) != 0 ? a : caps->rural / 3.6);
    if (l.edge->zone == 7) a = 1.7;
    if (l.edge->cls >= 13) a = jsMin(a, 6);
    return a;
  };
  /** a stop on the way ahead (a light, a stop sign) is what holds the speed down: braked for firmly */
  bool stopAhead = false;
  const auto preview = [&](Connector* first, double dist, double cap) {
    Connector* c = first;
    double d = dist;
    double out = cap;
    for (int hop = 0; c && hop < 10 && d < 360; hop++) {
      if (c->turn != TURN_STRAIGHT) out = jsMin(out, turnCap(d - 2));
      Lane* l2 = c->to;
      // a lower limit on the road ahead (a sign, a zone, a car park) is reached at that speed,
      // not braked down to behind it – whoever follows would have to brake hard, too
      const double v2 = allowed(*l2);
      if (v2 < out) out = jsMin(out, std::sqrt(v2 * v2 + 2 * (TRUCK ? 1.0 : 1.4) * jsMax(0, d + c->len - 2)));
      // a red light or a stop sign at the end of the lane behind it
      const Arm* a2 = l2->armTo;
      const double dStop = d + c->len + l2->stopAt;
      // (amber: stop if that is still possible with normal braking – otherwise on, without dithering)
      // (the green arrow beside it lets the car go on if it turns right there)
      const Connector* on2 = routeFrom(route, l2);
      const int sg2 = a2->sg < 0 || l2->to->sc < 0 || (on2 && on2->turn == TURN_RIGHT && signals.arrow(a2) == SIG_GREEN) ? static_cast<int>(SIG_GREEN) : signals.get(l2->to->sc, a2->sg);
      const bool red = sg2 == SIG_RED || sg2 == SIG_RED_AMBER || (sg2 == SIG_AMBER && (speed * speed) / (2 * jsMax(dStop - 2.5, 0.5)) < 3);
      // (a line the front of the car has only just reached still counts: the car is not in the junction yet)
      if (obey && dStop > -0.6 && l2->to->arms.size() > 2 && (red || a2->role == ROLE_STOP)) {
        const double room = dStop - 2.5;
        // (A light right behind a junction: wait before it, do not creep up to the line – by the
        // time the car counts as being on that short lane, its front is at the line or past it.
        // A stop sign there is crept up to: the car has to get to its line to stop at it.)
        const double cap2 = red && room < 0.3 ? 0 : std::sqrt(2 * (DECEL - 0.2) * jsMax(room, 0.3));
        if (cap2 < out) {
          out = cap2;
          stopAhead = true;
        }
        if (red && room < 25 && remark == "") remark = "red";
      }
      d += c->len + l2->len;
      const Lane* from = l2;
      c = onward(route, from);
    }
    return out;
  };
  // inside a junction the monitor's lane position is frozen: measure along the connector instead
  const bool inJunction = world.surfaceAt(mpos[0], mpos[2], mpos[1] + 0.6, hit) && hit.node != nullptr && hit.node->arms.size() > 1;
  // the monitor may already count us on the lane we are heading for (wide junction areas):
  // then that lane is the path, not the connector at its far end
  const bool onExitLane = lane != nullptr && laneS < jsMin(lane->stopAt, lane->len) - 0.5;
  /** crossings of lane `l`, whose start is `offset` metres ahead of the front bumper */
  const auto scanCrossings = [&](const Lane& l, double offset) {
    const std::vector<CrossingJson>& xs = l.edge->xings;
    if (xs.empty()) return;
    for (const CrossingJson& c : xs) {
      const double pos = l.dir > 0 ? c.s - l.s0 : l.s0 - c.s;
      const double d = offset + pos - c.w / 2;
      if (d < -0.5 || d > 70) continue;
      bool stop = false;
      if (c.sc) {
        const int st = signals.get(*c.sc, c.g.value_or(0));
        stop = st == SIG_RED || st == SIG_RED_AMBER || (st == SIG_AMBER && (speed * speed) / (2 * jsMax(d, 0.5)) < 3);
        if (stop) remark = "red";
      }
      // people: whoever is on a crossing is let across, whatever the light says by now – and at
      // a zebra crossing whoever stands at it as well
      const Pedestrian* walker = !stop && traffic ? traffic->pedestrianAt(c) : nullptr;
      if (walker && (walker->state == "cross" || c.k == XING_ZEBRA)) {
        stop = true;
        remark = "zebra";
      }
      if (stop) {
        const double dd = jsMax(0, d - 1.5);
        limit = jsMin(limit, std::sqrt(2 * DECEL * dd));
        if (dd < 0.6) crossStop = true;
      }
    }
  };
  /**
   * The point to steer at: `la` metres ahead along the way (`sample(d)` puts
   * the point `d` metres ahead into smp) – nearer in a bend, so that the
   * chord to it does not cut the bend, and with it the kerb on the inside or
   * the side of the road where others come the other way.
   */
  const auto aim = [&](const std::function<void(double d)>& sample) {
    sample(la);
    if (tight) return;
    const double bx = smp.tx;
    const double bz = smp.tz;
    // … and a little further on: a bend is steered into where it begins, not where it comes into view
    // (steering at a point 10 m ahead, the car turned in 10 m early and cut a sharp bend by a metre)
    sample(jsMin(la * 1.6, la + 14));
    const double fx2 = smp.tx;
    const double fz2 = smp.tz;
    sample(0);
    const double t0x = smp.tx;
    const double t0z = smp.tz;
    const double bend = jsAcos(jsMax(-1, jsMin(1, bx * t0x + bz * t0z)));
    const double far = jsAcos(jsMax(-1, jsMin(1, fx2 * t0x + fz2 * t0z)));
    const double turnAhead = jsMax(bend, far);
    const double d = turnAhead > 0.06 ? jsMax(jsMax(3.5, speed * 0.55), jsMin(la, std::sqrt((2.4 * la) / turnAhead))) : la;
    sample(d);
    // What is left of the cut – about d times the turn up to the point, by 8 – is made up for by
    // putting the point that far to the outside: in a bend to the left the inside is where the
    // others come the other way.
    const double turned = jsAcos(jsMax(-1, jsMin(1, smp.tx * t0x + smp.tz * t0z)));
    const double out = jsMin(TRUCK ? 0.25 : 0.4, (d * turned) / 8);
    if (out > 0.03) {
      const double side = t0x * smp.tz - t0z * smp.tx < 0 ? 1 : -1;
      smp.x += -smp.tz * out * side;
      smp.z += smp.tx * out * side;
    }
  };
  me.v = speed;
  me.x = mpos[0];
  me.y = mpos[1];
  me.z = mpos[2];
  me.yaw = v.yaw;
  me.lane = nullptr;
  me.conn = nullptr;
  me.next = nullptr;
  me.route = &route;
  me.past = past;
  bool halting = false;
  /** at the place to halt at (not merely standing near it, behind somebody) */
  bool atHalt = false;
  Connector* turnRound = nullptr;
  /** how far along the path through the junction the car is (when it is not on a lane) */
  double connS = 0;
  if (((inJunction && !onExitLane) || !lane) && next) {
    proj = projectOnPolyline(next->pl, mpos[0], mpos[2], proj);
    Connector* on = next;
    const double at = proj.s;
    connS = at;
    aim([&](double d) {
      const double sj = at + d;
      if (sj <= on->len) {
        smp = samplePolyline(on->pl, sj, smp);
      } else {
        smp = lanePoint(*on->to, jsMin(on->to->len, sj - on->len), smp);
        if (works) works->offset(*on->to, jsMin(on->to->len, sj - on->len), smp);
        if (narrow) narrow->offset(*on->to, jsMin(on->to->len, sj - on->len), smp, nhw, nhl);
      }
    });
    tx = smp.x;
    tz = smp.z;
    limit = next->turn == TURN_STRAIGHT ? jsMin(std::isfinite(next->to->limit) ? next->to->limit : 36, 14) : TURN_SPEED;
    // (measured as for the car, whose front is 2.14 m ahead of its middle)
    limit = preview(next, -proj.s - (FRONT - 2.14), limit);
    turn = lastTurn;
    me.conn = next;
    me.s = proj.s;
    if (obey) scanCrossings(*next->to, next->len - proj.s - FRONT);
    if (lxs) levelCrossing(lxs->gate(*next->to, -(next->len - proj.s - FRONT), speed, me.length, nullptr, nullptr, true));
  } else if (lane) {
    const double centre = laneS - FRONT;
    me.lane = lane;
    me.s = centre;
    me.next = next;
    // (a truck with a trailer: 80 km/h on the motorway, 60 km/h on other roads outside built-up areas)
    limit = allowed(*lane);
    Lane* here = path ? path : lane;
    Connector* onward_ = next;
    aim([&](double d) {
      const double sl = centre + d;
      // (where a side closed for roadworks is passed on the oncoming lane: over there)
      if (sl <= lane->len || !onward_) {
        smp = lanePoint(*here, jsMin(lane->len, sl), smp);
        if (works) works->offset(*here, jsMin(lane->len, sl), smp);
        if (narrow) narrow->offset(*here, jsMin(lane->len, sl), smp, nhw, nhl);
      } else if (sl - lane->len <= onward_->len) {
        smp = samplePolyline(onward_->pl, sl - lane->len, smp);
      } else {
        smp = lanePoint(*onward_->to, jsMin(onward_->to->len, sl - lane->len - onward_->len), smp);
        if (works) works->offset(*onward_->to, jsMin(onward_->to->len, sl - lane->len - onward_->len), smp);
        if (narrow) narrow->offset(*onward_->to, jsMin(onward_->to->len, sl - lane->len - onward_->len), smp, nhw, nhl);
      }
    });
    if (centre + la <= lane->len || next) {
      tx = smp.x;
      tz = smp.z;
    }
    limit = preview(next, jsMax(0, lane->len - laneS), limit);
    // roadworks and closures (src/sim/roadevents): their limit; the site light, the oncoming traffic at a narrowing, the end of a closed lane, a closed road
    if (works) {
      const Gate g = works->gate(*lane, laneS, speed, next);
      if (g.cap < limit) limit = g.cap;
      // (the end of a closed lane does not hold up a car that is moving over already)
      if (obey && g.stop < JS_INF && !(g.why == "lane" && path != lane)) {
        const double dd = jsMax(0, g.stop - 1.2);
        limit = jsMin(limit, std::sqrt(2 * DECEL * dd));
        if (dd < 0.6) crossStop = true;
        remark = g.why == "light" ? "red" : g.why == "oncoming" ? "yield" : g.why == "closed" ? "closed" : "lane";
      }
    }
    // a level crossing (src/sim/levelcrossing): its lights and barriers, a train – and room for the whole car behind the tracks
    if (lxs) levelCrossing(lxs->gate(*lane, laneS, speed, me.length, next, nullptr, true));
    // a kerb build-out narrowing the road (src/sim/narrowings): slowly through it; where it is on our side, the oncoming traffic first
    if (narrow) {
      const NarrowGate g = narrow->gate(*lane, laneS, speed, me.length, nullptr);
      if (g.cap < limit) limit = g.cap;
      if (obey && g.stop < JS_INF) {
        const double dd = jsMax(0, g.stop - 1.2);
        limit = jsMin(limit, std::sqrt(2 * DECEL * dd));
        if (dd < 0.6) crossStop = true;
        remark = "yield";
      }
    }
    distToLine = lane->stopAt - laneS;
    const Node& node = *lane->to;
    const Arm& arm = *lane->armTo;
    // A light or a stop sign at the end of the lane counts whether the way goes on behind it or not:
    // the last road of a way may end at a red light, and the place to halt at may lie behind its line.
    if (node.arms.size() > 2 && !next) {
      if (obey && arm.sg >= 0 && node.sc >= 0) {
        const int st = signals.get(node.sc, arm.sg);
        if (st == SIG_RED || st == SIG_RED_AMBER) mustStop = true;
        if (st == SIG_AMBER && (speed * speed) / (2 * jsMax(distToLine, 0.5)) < 3) mustStop = distToLine > 0;
        if (mustStop) remark = "red";
      } else if (obey && arm.role == ROLE_STOP && stoppedFor < 1) {
        mustStop = true;
        remark = "stop";
      }
    }
    if (next && node.arms.size() > 2) {
      turn = next->turn == TURN_LEFT ? -1 : next->turn == TURN_RIGHT ? 1 : 0;
      // roundabout: indicate right only when leaving
      const bool ring = (lane->edge->flags & EF_ROUNDABOUT) != 0;
      if (node.ctrl == CTRL_ROUNDABOUT) {
        turn = ring && !(next->to->edge->flags & EF_ROUNDABOUT) ? 1 : 0;
        if (distToLine < 40) remark = ring ? (turn ? "ringExit" : remark) : "roundabout";
      } else if (node.ctrl == CTRL_RBL && distToLine < 40) {
        remark = "rbl";
      }
      // the lights are out of order: the signs decide (said here; "stop" and "give way" follow where they apply)
      if (node.dark && distToLine < 50) remark = "lightsOut";
      if (obey && arm.sg >= 0 && node.sc >= 0) {
        const int st = signals.get(node.sc, arm.sg);
        // the green arrow beside the light: turning right is allowed while it is lit, whatever the main light shows
        const int arrow = next->turn == TURN_RIGHT ? signals.arrow(&arm) : -1;
        if (arrow == SIG_GREEN) {
          if (st != SIG_GREEN && distToLine < 40) remark = "arrow";
        } else {
          if (st == SIG_RED || st == SIG_RED_AMBER) mustStop = true;
          if ((st == SIG_AMBER || arrow == SIG_AMBER) && (speed * speed) / (2 * jsMax(distToLine, 0.5)) < 3) mustStop = distToLine > 0;
          else if (arrow == SIG_AMBER) mustStop = false;
          if (mustStop) remark = "red";
        }
      } else if (obey && arm.role == ROLE_STOP && stoppedFor < 1) {
        mustStop = true;
        remark = "stop";
      }
      // slow down for the turn – and towards a junction where others may have the right of way
      if (next->turn != TURN_STRAIGHT) limit = jsMin(limit, turnCap(distToLine));
      else if (arm.role != ROLE_PRIORITY) limit = jsMin(limit, 7 + jsMax(0, distToLine - 30) * 0.1);
      // A red light a few metres behind this junction – the two belong together, as where a
      // road crosses a dual carriageway: wait here, in between there is no room to stand.
      const Lane* l2 = next->to;
      const Arm* a2 = l2->armTo;
      if (obey && !mustStop && l2->len < 9 && a2->sg >= 0 && l2->to->sc >= 0) {
        const int st2 = signals.get(l2->to->sc, a2->sg);
        if (st2 == SIG_RED || st2 == SIG_RED_AMBER || (st2 == SIG_AMBER && distToLine > 2)) {
          mustStop = true;
          remark = "red";
        }
      }
    } else if (next) {
      // A mere joint in the road (the way was mapped in pieces): the junction behind it decides
      // what to indicate and where to look – a learner sees it coming, however short the pieces are.
      const Lane* l2 = next->to;
      Connector* c2 = onward(route, l2);
      double d = jsMax(0, lane->len - laneS) + next->len + l2->len;
      for (int hop = 0; c2 && hop < 6 && d < 45 && l2->to->arms.size() <= 2; hop++) {
        l2 = c2->to;
        d += c2->len + l2->len;
        const Lane* from = l2;
        c2 = onward(route, from);
      }
      if (c2 && d < 45 && l2->to->arms.size() > 2 && l2->to->ctrl != CTRL_ROUNDABOUT) {
        turn = c2->turn == TURN_LEFT ? -1 : c2->turn == TURN_RIGHT ? 1 : 0;
        ahead = d;
      }
    }
    // --- the others at the junction: give way, and never drive into a car that is crossing ---
    bool yielding = false;
    if (traffic && obey && next && !mustStop && (node.arms.size() > 2 || next->conflicts.size() > 0) && distToLine > -1.5 && distToLine < 28) {
      bool ok = traffic->mayPass(me, *next, jsMax(distToLine, 0));
      // everybody waits for somebody: after a while, with the junction empty, take a tighter gap
      if (!ok && yieldWait > 12 && traffic->clearOf(*next) && traffic->mayPass(me, *next, jsMax(distToLine, 0), true)) ok = true;
      if (!ok) {
        mustStop = true;
        yielding = true;
        remark = "yield";
      } else if (node.arms.size() > 2 && !traffic->roomBehind(*next->to, me.length + 2.5)) {
        // The junction is kept clear: where the traffic stands behind it, nobody drives into it –
        // not with the right of way and not on green (§ 11 Abs. 1 StVO).  Whoever waits in the
        // junction is in the way of those who cross.
        mustStop = true;
        yielding = true;
        remark = "follow";
      } else if (next->turn == TURN_RIGHT && traffic->cyclists && traffic->cyclists->blocksRightTurn(*lane, laneS - FRONT, me.length / 2 + 0.5)) {
        // Turning right: the cyclists who ride straight on – beside the car, coming up behind it –
        // go first (§ 9 Abs. 3 StVO).  Seen by the look over the shoulder, waited for at the line.
        mustStop = true;
        remark = "cyclist";
      }
    }
    if (yielding && speed < 0.5) yieldWait += dt;
    else if (!yielding) yieldWait = 0;
    // --- crossings on the way: a red pedestrian light, people at a zebra crossing ---
    // (on this lane and on the ones the way goes on to: a crossing may lie right behind a turn)
    if (obey) {
      scanCrossings(*lane, -laneS);
      double off = lane->len - laneS;
      Connector* c = next;
      for (int hop = 0; c && hop < 4 && off < 60; hop++) {
        const Lane* l2 = c->to;
        off += c->len;
        scanCrossings(*l2, off);
        off += l2->len;
        c = onward(route, l2);
      }
    }
    // the lane to get into is taken: hold back, and wait before the junction rather than push in
    if (laneBlocked) {
      limit = jsMin(limit, jsMax(3, speed - 1));
      if (distToLine < 14) {
        mustStop = true;
        remark = "yield";
      }
    }
    // --- the place to halt at: on this lane, or passed already ---
    if (halt) {
      const Halt& h = *halt;
      const Edge& e = *lane->edge;
      if (haltLane != lane && h.x > e.minX - 8 && h.x < e.maxX + 8 && h.z > e.minZ - 8 && h.z < e.maxZ + 8) {
        proj = projectOnPolyline(e.pl, h.x, h.z, proj);
        if (proj.d2 < (e.width / 2 + 6) * (e.width / 2 + 6)) {
          haltLane = lane;
          haltS = lane->dir > 0 ? proj.s - lane->s0 : lane->s0 - proj.s;
        }
      }
      if (haltLane == lane) {
        // the middle of the car at the place; if it lies behind already, stop where it is safe: here
        const double dd = jsMax(0, jsMin(haltS, lane->len - 3) - centre);
        limit = jsMin(limit, std::sqrt(2 * (DECEL * 0.7) * dd));
        halting = dd < 45;
        if (halting) remark = "halt";
        if (dd < 0.6) {
          crossStop = true;
          atHalt = true;
        }
      }
    }
    // --- the end of the street, and the way leads back out of it: stop short of the end ---
    if (next && next->from == lane && next->turn == TURN_UTURN && lane->to->arms.size() == 1) {
      const double dd = jsMax(0, lane->len - 3.5 - centre);
      limit = jsMin(limit, std::sqrt(2 * (DECEL * 0.7) * dd));
      if (dd < 45) {
        halting = true;
        remark = "deadEnd";
      }
      if (dd < 0.6) {
        crossStop = true;
        turnRound = next;
      }
    }
    if ((distToLine > 45 && ahead > 45) || distToLine < -3) turn = 0;
    lastTurn = turn;
  } else {
    // inside a junction or off the lane: keep following the last planned connector
    limit = 5;
  }
  // --- crossing the way of others: turning left, or through a junction made of several nodes ---
  // (where the two carriageways of a wide road are crossed one after the other, the lights
  // and the right of way of the map end at each of them – the driver looks for himself)
  if (traffic && obey && next && (me.lane || me.conn)) {
    const bool left = next->turn == TURN_LEFT || next->turn == TURN_UTURN;
    const bool cluster = (lane != nullptr && lane->len < 9) || next->to->len < 9;
    if ((left || cluster) && (me.conn == next || distToLine < 18)) {
      const double xd = traffic->crossingAhead(me, 26);
      // (a car's length into their way already: on, and out of it)
      if (xd < JS_INF && xd > FRONT + 0.8) {
        if (me.lane && distToLine > 0.5) {
          // still before the junction: wait at the line, not somewhere inside in somebody's way
          mustStop = true;
        } else {
          const double room = xd - FRONT - 3.2;
          limit = jsMin(limit, std::sqrt(2 * DECEL * jsMax(0, room)));
          if (room < 0.6) crossStop = true;
        }
        remark = "yield";
      }
    }
  }
  // --- whoever is ahead on the way: two seconds behind, and never faster than could be stopped ---
  /** it stands for a reason of the road (a line, a light, a crossing, the place to halt at) – not for what is in its way */
  const bool held = mustStop || crossStop || halting;
  double gap = JS_INF;
  if (traffic && (me.lane || me.conn)) {
    Traffic::Ahead lead = traffic->aheadOf(me, jsMin(110, 18 + speed * 4));
    // somebody standing half in the way who does not move either (he waits for us, we for him):
    // after a while, past him at a crawl
    if (lead.loose && lead.v < 0.3 && speed < 0.3 && lead.gap < 6) netWait += dt;
    else if (speed > 2 || !lead.loose) netWait = 0;
    if (lead.loose && netWait > 6) {
      // … with what room there is: at half a metre it is a standstill again
      limit = jsMin(limit, 1.2);
      lead.gap = lead.gap < 0.6 ? lead.gap : JS_INF;
    }
    gap = lead.gap;
    if (lead.gap < JS_INF) {
      const double room = lead.gap - 3;
      const double follow = lead.v + 0.45 * (room - 1.9 * speed);
      const double stoppable = std::sqrt(lead.v * lead.v + 2 * (DECEL * 0.85) * jsMax(room, 0));
      const double cap = jsMax(0, jsMin(follow, stoppable));
      if (cap < limit) {
        if (cap < limit - 0.6 && remark == "") remark = "follow";
        limit = cap;
      }
      if (lead.gap < 2.2) crossStop = true;
    }
  }
  // --- something standing in the way that does not move: after a while round it, as the traffic does (src/sim/pastblock) ---
  if (traffic && obey && (me.lane || me.conn)) {
    traffic->passBy(me, dt, gap, held);
    past = me.past;
    const PastState* p = past.get();
    if (p && p->phase != "wait") {
      limit = jsMin(limit, p->cap);
      // (the point steered at: as far off the middle of the lane as the car will be there)
      const double o = offsetAt(*p, la);
      tx += -jsCos(v.yaw) * o;
      tz += jsSin(v.yaw) * o;
      if (turn == 0) turn = p->phase == "back" ? (p->off > 0 ? 1 : -1) : p->to > 0 ? -1 : 1;
    }
  } else {
    past = nullptr;
  }
  // changing lanes is announced like a turn: indicator on, a look over the shoulder, then over
  changeT = change != 0 ? changeT + dt : 0;
  double look = turn != 0 && jsMin(distToLine, ahead) < 25 ? (turn < 0 ? 1.6 : -1.6) : 0;
  if (turn != 0 && remark == "") remark = "turn";
  if (traffic && change != 0 && turn == 0) {
    turn = change;
    if (changeT < 1.6) look = change < 0 ? 1.6 : -1.6;
    if (remark == "") remark = "lane";
  }
  // --- controls ---
  const double fx = -jsSin(v.yaw);
  const double fz = -jsCos(v.yaw);
  const double dx = tx - mpos[0];
  const double dz = tz - mpos[2];
  const double lat = dx * jsCos(v.yaw) - dz * jsSin(v.yaw);  // + = target to the right
  const double fwd = dx * fx + dz * fz;
  const double curv = (2 * lat) / jsMax(dx * dx + dz * dz, 1);
  const double steer = jsMax(-1, jsMin(1, (jsAtan(spec.wheelbase * curv) / spec.maxWheelAngle) * (fwd > 0 ? 1 : 3)));
  // keep the lateral acceleration comfortable
  const double vCurve = std::sqrt((TRUCK ? 1.5 : 2.2) / jsMax(std::abs(curv), 0.002));
  // … and the bends further ahead are come up to at the speed they can be driven at (looked at a few times a second)
  if (bendTick++ % 6 == 0) bendLimit = lane || next ? bendCap(lane, lane ? laneS - FRONT : connS, next, route, speed) : JS_INF;
  const double vBend = bendLimit;
  double target = jsMin(jsMin(limit * 0.94, vCurve), vBend);
  double brake = 0;
  if (mustStop && distToLine > -1) {
    const double d = jsMax(0, distToLine - (TRUCK ? 1.8 : 1.2));
    target = jsMin(target, std::sqrt(2 * DECEL * d));
    if (d < 0.6) {
      target = 0;
      brake = 0.5;
    }
  }
  if (crossStop) {
    target = 0;
    brake = 0.5;
  }
  if (v.speed < 0.15 && mustStop && distToLine < 4) stoppedFor += dt;
  else if (!mustStop && distToLine > 12) stoppedFor = 0;
  double throttle = 0;
  const double err = target - v.speed;
  if (err > 0) {
    throttle = jsMin(TRUCK ? 0.85 : 0.6, err * 0.35);
  } else {
    // Braking for a line, for somebody in the way or at the destination is as firm as it takes;
    // easing off for a limit, a bend or a junction ahead stays gentle – whoever follows is not
    // made to brake hard, and a learner sees how early one lifts off.
    // (A bend that asks for less speed than the car has is braked for as firmly as it takes, too:
    // the way off a motorway does not wait.)
    const bool firm = mustStop || crossStop || halting || stopAhead || gap < 40 || vCurve < limit * 0.94 || vBend < v.speed - 3 || err < -4;
    const double most = firm ? 0.8 : (TRUCK ? 1.6 : 2.1) / (TRUCK ? 6.5 : 9.5);
    brake = jsMax(brake, jsMin(most, -err * 0.3));
  }

  // Standing where the way is not free – a red light behind the next joint in the road, somebody
  // crossing: held on the brake.  (Left to itself, the car creeps on at walking pace, over the line.)
  if (target < 0.05 && v.speed < 1.2) brake = jsMax(brake, 0.4);

  cmd.steer = steer;
  cmd.throttle = brake > 0 ? 0 : throttle;
  cmd.brake = brake;
  cmd.indicator = turn;
  cmd.look = look;
  cmd.remark = remark;
  cmd.waiting = mustStop || crossStop || (gap < 12 && target < 0.3);
  cmd.arrived = atHalt && std::abs(v.speed) < 0.1;
  cmd.turnRound = turnRound && std::abs(v.speed) < 0.1 ? turnRound : nullptr;
  seen.next = next;
  seen.tx = tx;
  seen.tz = tz;
  seen.limit = limit;
  seen.distToLine = distToLine;
  seen.mustStop = mustStop;
  seen.crossStop = crossStop;
  seen.turn = turn;
  seen.gap = gap;
  return cmd;
}

}  // namespace fs

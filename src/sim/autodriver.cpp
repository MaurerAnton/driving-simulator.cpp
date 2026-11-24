// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The rule-keeping driver of the demonstration drives (origin:
 * src/sim/autodriver.ts @ ff37c5d, v0.7.0).
 */
#include "sim/autodriver.h"

#include <algorithm>
#include <cmath>

#include "sim/signals.h"
#include "sim/vehicle/vehicle.h"

namespace fs {

namespace {
/** The point the driver steers at, `d` metres ahead of a sampled place. */
PolySample gSmp;

/** Find the connector of `route` that leaves `lane`, if the way is known. */
Connector* routeFrom(const std::vector<Connector*>& route, const Lane* lane) {
  for (Connector* c : route)
    if (c->from == lane) return c;
  return nullptr;
}

/** The connector to take out of `lane`: the route's, else the only one out. */
Connector* onwardOf(const std::vector<Connector*>& route, const Lane* lane) {
  Connector* c = routeFrom(route, lane);
  if (c) return c;
  if (lane->out.size() == 1) return lane->out[0];
  return nullptr;
}
}  // namespace

AutoDriver::AutoDriver(World& worldIn, SignalSystem& signalsIn, RuleMonitor& rulesIn,
                       Traffic* trafficIn, const AutoDriverOptions& optsIn)
    : world(worldIn), signals(signalsIn), rules(rulesIn), traffic(trafficIn), opts(optsIn) {
  // speed in a turn at a junction (a truck turns right at walking pace in town, § 9 Abs. 6 StVO)
  turnSpeed_ = opts.truck ? 2.7 : 4.5;
  // the deceleration the driver plans with when stopping at a line (15 t take longer)
  decel_ = opts.truck ? 1.3 : 2.2;
  me_.length = opts.spec && opts.spec->length > 0 ? opts.spec->length : 4.28;
  me_.width = opts.spec && opts.spec->width > 0 ? opts.spec->width : 1.79;
}

void AutoDriver::reset() {
  stoppedFor_ = 0;
  yieldWait_ = 0;
  changeT_ = 0;
  netWait_ = 0;
  lastTurn_ = 0;
  lastNext_ = nullptr;
  haltLane_ = nullptr;
  bendTick_ = 0;
  bendLimit_ = 0;
  cmd.arrived = false;
  cmd.turnRound = nullptr;
}

double AutoDriver::bendCap(const Lane* lane, double from, const Connector* next,
                           const std::vector<Connector*>& route, double speed) {
  const double STEP = 6;
  /** the turn is measured over 2 × SPAN steps: a kink in the line of a road is a gentle bend, a hairpin is not */
  const int SPAN = 2;
  const double pull = opts.truck ? 1.5 : 2.2;
  const double ease = opts.truck ? 1.1 : 1.7;
  const double reach = std::min(240.0, (speed * speed) / (2 * ease) + 45);
  const int n = std::min(48, static_cast<int>(std::ceil(reach / STEP)) + 2 * SPAN + 1);
  // along the lane the car is on, the path through the junction behind it, the lane after that, …
  const Lane* curLane = lane;
  const Connector* curConn = lane ? next : next;
  if (lane && !curConn) curConn = nullptr;
  const Connector* after = lane ? next : nullptr;
  double at = std::max(0.0, from);
  int count = 0;
  PolySample sm;
  while (count < n && (curLane || curConn)) {
    if (curLane) {
      double len = curLane->len;
      if (at > len) {
        // on to the path through the junction behind this lane
        at -= len;
        const Connector* c = after;
        if (!c) {
          for (Connector* r : route)
            if (r->from == curLane) { c = r; break; }
          if (!c && curLane->out.size() == 1) c = curLane->out[0];
        }
        after = nullptr;
        curConn = c;
        curLane = nullptr;
        continue;
      }
      sm = lanePoint(*curLane, at, sm);
    } else {
      double len = curConn->len;
      if (at > len) {
        // and on to the lane the path leads onto
        at -= len;
        curLane = curConn->to;
        curConn = nullptr;
        continue;
      }
      sm = samplePolyline(curConn->pl, at, sm);
    }
    bendDir_[count * 2] = sm.tx;
    bendDir_[count * 2 + 1] = sm.tz;
    count++;
    at += STEP;
  }
  double cap = std::numeric_limits<double>::infinity();
  for (int i = SPAN; i + SPAN < count; i++) {
    int a = (i - SPAN) * 2;
    int b = (i + SPAN) * 2;
    double turn = std::acos(std::max(-1.0, std::min(1.0, bendDir_[a] * bendDir_[b] + bendDir_[a + 1] * bendDir_[b + 1])));
    if (turn < 0.05) continue;
    double bend = std::sqrt((pull * 2 * SPAN * STEP) / turn);
    // (the bend begins where the stretch it was measured over begins)
    double d = std::max(0.0, (i - SPAN) * STEP - 4.0);
    cap = std::min(cap, std::sqrt(bend * bend + 2 * ease * d));
  }
  return cap;
}

AutoCommand AutoDriver::step(double dt, const Vehicle& v, const double mpos[3],
                             const std::vector<Connector*>& route) {
  const VehicleSpec& spec = *opts.spec;
  const double FRONT = opts.front;
  const bool TRUCK = opts.truck;
  const bool obey = opts.obey;
  const double TURN_SPEED = turnSpeed_;
  const double DECEL = decel_;
  SurfaceHit& hit = hit_;
  PolyProjection& proj = proj_;
  Lane* lane = rules.currentLane();
  double laneS = rules.currentLaneS();
  AutoRemark remark;

  // --- path: current lane, then the route's connectors ---
  Connector* next = nullptr;
  /** the lane the route goes on from is taken: wait for a gap before moving over */
  bool laneBlocked = false;
  /** moving over to another lane of the carriageway: -1 to the left, 1 to the right */
  double change = 0;
  // the lane to follow: the one the monitor has us in – or its neighbour, if the route goes on from that one
  Lane* path = lane;
  if (lane) {
    next = routeFrom(route, lane);
    if (!next) {
      Connector* rc = nullptr;
      for (Connector* c : route)
        if (c->from->edge == lane->edge && c->from->dir == lane->dir) rc = c;
      if (rc) {
        next = rc;
        path = rc->from;  // get in lane
        // to which side: indicated, and looked over the shoulder, before the car moves over
        change = (path->off - lane->off) * lane->dir > 0 ? -1 : 1;
        // … and only where there is room: nobody beside, nobody coming up from behind
        if (traffic && !traffic->roomOn(*path, laneS - FRONT, std::max(0.0, v.speed), me_.length)) {
          path = lane;
          laneBlocked = true;
        } else if (traffic && changeT_ < 1) {
          path = lane;
        }
      } else {
        next = lane->out.size() == 1 ? lane->out[0] : nullptr;
      }
    }
  }
  if (next) lastNext_ = next;
  else if (!lane) next = lastNext_;
  // pure pursuit target point: lookahead along lane → connector → next lane
  double speed = std::max(0.0, v.speed);
  // look less far ahead in tight turns so the corner is not cut
  bool tight = next != nullptr && next->turn != TURN_STRAIGHT && next->len < 25;
  double la = tight ? std::min(2.6 + speed * 0.45, 1.5 + next->len * 0.22) : 5 + speed * 0.9;
  double tx = mpos[0] - std::sin(v.yaw) * 5;
  double tz = mpos[2] - std::cos(v.yaw) * 5;
  double limit = 8;
  double distToLine = INFINITY;
  bool mustStop = false;
  bool crossStop = false;
  double turn = 0;
  /** distance to a junction behind joints of the road, where the turn to announce is */
  double ahead = INFINITY;
  /**
   * How fast the car may still be `d` metres before a turn at a junction: one
   * steady easing off from far away down to the speed of the turn – no step in
   * it anywhere, or whoever follows would have to brake hard.
   */
  auto turnCap = [&](double d) {
    double near = std::min(45.0, std::max(0.0, d));
    return TURN_SPEED + near * (TRUCK ? 0.08 : 0.12) +
           std::max(0.0, d - 45) * (TRUCK ? 0.045 : 0.065);
  };
  /** how fast a lane may be driven: its limit, walking pace in a traffic-calmed area, slowly on yards and car parks */
  auto allowed = [&](const Lane& l) {
    double a = std::isfinite(l.limit) ? l.limit : 36;
    if (TRUCK)
      a = std::min(a, l.edge->cls <= MOTORWAY_LINK ? 80 / 3.6
                                                   : ((l.edge->flags & EF_URBAN) ? a : 60 / 3.6));
    if (l.edge->zone == 7) a = 1.7;
    if (l.edge->cls >= 13) a = std::min(a, 6.0);
    return a;
  };
  /** a stop on the way ahead (a light, a stop sign) is what holds the speed down: braked for firmly */
  bool stopAhead = false;
  auto preview = [&](Connector* first, double dist, double cap) {
    Connector* c = first;
    double d = dist;
    double out = cap;
    for (int hop = 0; c && hop < 10 && d < 360; hop++) {
      if (c->turn != TURN_STRAIGHT) out = std::min(out, turnCap(d - 2));
      const Lane& l2 = *c->to;
      // a lower limit on the road ahead (a sign, a zone, a car park) is reached at that speed,
      // not braked down to behind it – whoever follows would have to brake hard, too
      double v2 = allowed(l2);
      if (v2 < out)
        out = std::min(out, std::sqrt(v2 * v2 + 2 * (TRUCK ? 1.0 : 1.4) * std::max(0.0, d + c->len - 2)));
      // a red light or a stop sign at the end of the lane behind it
      const Arm* a2 = l2.armTo;
      double dStop = d + c->len + l2.stopAt;
      int sg2 = (a2 && a2->sg >= 0 && l2.to->sc >= 0) ? signals.get(l2.to->sc, a2->sg) : SIG_GREEN;
      bool red = sg2 == SIG_RED || sg2 == SIG_RED_AMBER ||
                 (sg2 == SIG_AMBER && (speed * speed) / (2 * std::max(dStop - 2.5, 0.5)) < 3);
      // (a line the front of the car has only just reached still counts: the car is not in the junction yet)
      if (obey && dStop > -0.6 && a2 && l2.to->arms.size() > 2 && (red || a2->role == ROLE_STOP)) {
        double room = dStop - 2.5;
        // (A light right behind a junction: wait before it, do not creep up to the line.
        // A stop sign there is crept up to: the car has to get to its line to stop at it.)
        double cap2 = red && room < 0.3 ? 0 : std::sqrt(2 * (DECEL - 0.2) * std::max(room, 0.3));
        if (cap2 < out) {
          out = cap2;
          stopAhead = true;
        }
        if (red && room < 25 && remark.empty()) remark = "red";
      }
      d += c->len + l2.len;
      const Lane* from = &l2;
      c = onwardOf(route, from);
    }
    return out;
  };
  // inside a junction the monitor's lane position is frozen: measure along the connector instead
  bool inJunction = world.surfaceAt(mpos[0], mpos[2], mpos[1] + 0.6, hit) && hit.node != nullptr &&
                    hit.node->arms.size() > 1;
  // the monitor may already count us on the lane we are heading for (wide junction areas):
  // then that lane is the path, not the connector at its far end
  bool onExitLane = lane != nullptr && laneS < std::min(lane->stopAt, lane->len) - 0.5;
  /** crossings of lane `l`, whose start is `offset` metres ahead of the front bumper */
  auto scanCrossings = [&](const Lane& l, double offset) {
    const auto& xs = l.edge->xings;
    for (const auto& c : xs) {
      double pos = l.dir > 0 ? c.s - l.s0 : l.s0 - c.s;
      double d = offset + pos - c.w / 2;
      if (d < -0.5 || d > 70) continue;
      bool stop = false;
      if (c.sc >= 0) {
        int st = signals.get(c.sc, c.g >= 0 ? c.g : 0);
        stop = st == SIG_RED || st == SIG_RED_AMBER ||
               (st == SIG_AMBER && (speed * speed) / (2 * std::max(d, 0.5)) < 3);
        if (stop) remark = "red";
      }
      // people: whoever is on a crossing is let across, whatever the light says by now – and at
      // a zebra crossing whoever stands at it as well
      Pedestrian* walker = (!stop && traffic) ? traffic->pedestrianAt(c) : nullptr;
      if (walker && (walker->state == "cross" || c.k == XING_ZEBRA)) {
        stop = true;
        remark = "zebra";
      }
      if (stop) {
        double dd = std::max(0.0, d - 1.5);
        limit = std::min(limit, std::sqrt(2 * DECEL * dd));
        if (dd < 0.6) crossStop = true;
      }
    }
  };
  /**
   * The point to steer at: `la` metres ahead along the way – nearer in a bend,
   * so that the chord to it does not cut the bend, and with it the kerb on the
   * inside or the side of the road where others come the other way.
   */
  auto aim = [&](const std::function<void(double)>& sample) {
    sample(la);
    if (tight) return;
    double bx = gSmp.tx;
    double bz = gSmp.tz;
    sample(0);
    double bend = std::acos(std::max(-1.0, std::min(1.0, bx * gSmp.tx + bz * gSmp.tz)));
    sample(bend > 0.06 ? std::max(3.5, std::min(la, std::sqrt((2.4 * la) / bend))) : la);
  };
  SelfDriving& me = me_;
  me.v = speed;
  me.x = mpos[0];
  me.y = mpos[1];
  me.z = mpos[2];
  me.yaw = v.yaw;
  me.lane = nullptr;
  me.conn = nullptr;
  me.next = nullptr;
  me.route = &route;
  bool halting = false;
  /** at the place to halt at (not merely standing near it, behind somebody) */
  bool atHalt = false;
  Connector* turnRound = nullptr;
  /** how far along the path through the junction the car is (when it is not on a lane) */
  double connS = 0;
  if (((inJunction && !onExitLane) || !lane) && next) {
    proj = projectOnPolyline(next->pl, mpos[0], mpos[2], proj);
    const Connector* on = next;
    double at = proj.s;
    connS = at;
    aim([&](double d) {
      double sj = at + d;
      if (sj <= on->len)
        gSmp = samplePolyline(on->pl, sj, gSmp);
      else
        gSmp = lanePoint(*on->to, std::min(on->to->len, sj - on->len), gSmp);
    });
    tx = gSmp.x;
    tz = gSmp.z;
    limit = next->turn == TURN_STRAIGHT
                ? std::min(std::isfinite(next->to->limit) ? next->to->limit : 36.0, 14.0)
                : TURN_SPEED;
    // (measured as for the car, whose front is 2.14 m ahead of its middle)
    limit = preview(next, -proj.s - (FRONT - 2.14), limit);
    turn = lastTurn_;
    me.conn = next;
    me.s = proj.s;
    if (obey) scanCrossings(*next->to, next->len - proj.s - FRONT);
  } else if (lane) {
    double centre = laneS - FRONT;
    me.lane = lane;
    me.s = centre;
    me.next = next;
    // (a truck with a trailer: 80 km/h on the motorway, 60 km/h on other roads outside built-up areas)
    limit = allowed(*lane);
    const Lane* here = path ? path : lane;
    const Connector* onward = next;
    aim([&](double d) {
      double sl = centre + d;
      if (sl <= lane->len || !onward)
        gSmp = lanePoint(*here, std::min(lane->len, sl), gSmp);
      else if (sl - lane->len <= onward->len)
        gSmp = samplePolyline(onward->pl, sl - lane->len, gSmp);
      else
        gSmp = lanePoint(*onward->to, std::min(onward->to->len, sl - lane->len - onward->len), gSmp);
    });
    if (centre + la <= lane->len || next) {
      tx = gSmp.x;
      tz = gSmp.z;
    }
    limit = preview(next, std::max(0.0, lane->len - laneS), limit);
    distToLine = lane->stopAt - laneS;
    Node* node = lane->to;
    Arm* arm = lane->armTo;
    if (next && node && node->arms.size() > 2) {
      turn = next->turn == TURN_LEFT ? -1 : next->turn == TURN_RIGHT ? 1 : 0;
      // roundabout: indicate right only when leaving
      bool ring = (lane->edge->flags & EF_ROUNDABOUT) != 0;
      if (node->ctrl == CTRL_ROUNDABOUT) {
        turn = ring && !(next->to->edge->flags & EF_ROUNDABOUT) ? 1 : 0;
        if (distToLine < 40) remark = ring ? (turn ? "ringExit" : remark) : "roundabout";
      } else if (node->ctrl == CTRL_RBL && distToLine < 40) {
        remark = "rbl";
      }
      if (obey && arm && arm->sg >= 0 && node->sc >= 0) {
        int st = signals.get(node->sc, arm->sg);
        if (st == SIG_RED || st == SIG_RED_AMBER) mustStop = true;
        if (st == SIG_AMBER && (speed * speed) / (2 * std::max(distToLine, 0.5)) < 3)
          mustStop = distToLine > 0;
        if (mustStop) remark = "red";
      } else if (obey && arm && arm->role == ROLE_STOP && stoppedFor_ < 1) {
        mustStop = true;
        remark = "stop";
      }
      // slow down for the turn – and towards a junction where others may have the right of way
      if (next->turn != TURN_STRAIGHT) limit = std::min(limit, turnCap(distToLine));
      else if (arm && arm->role != ROLE_PRIORITY)
        limit = std::min(limit, 7 + std::max(0.0, distToLine - 30) * 0.1);
      // A red light a few metres behind this junction – the two belong together, as where a
      // road crosses a dual carriageway: wait here, in between there is no room to stand.
      const Lane* l2 = next->to;
      const Arm* a2 = l2->armTo;
      if (obey && !mustStop && l2->len < 9 && a2 && a2->sg >= 0 && l2->to->sc >= 0) {
        int st2 = signals.get(l2->to->sc, a2->sg);
        if (st2 == SIG_RED || st2 == SIG_RED_AMBER || (st2 == SIG_AMBER && distToLine > 2)) {
          mustStop = true;
          remark = "red";
        }
      }
    } else if (next) {
      // A mere joint in the road (the way was mapped in pieces): the junction behind it decides
      // what to indicate and where to look – a learner sees it coming, however short the pieces are.
      const Lane* l2 = next->to;
      Connector* c2 = onwardOf(route, l2);
      double d = std::max(0.0, lane->len - laneS) + next->len + l2->len;
      for (int hop = 0; c2 && hop < 6 && d < 45 && l2->to->arms.size() <= 2; hop++) {
        l2 = c2->to;
        d += c2->len + l2->len;
        c2 = onwardOf(route, l2);
      }
      if (c2 && d < 45 && l2->to->arms.size() > 2 && l2->to->ctrl != CTRL_ROUNDABOUT) {
        turn = c2->turn == TURN_LEFT ? -1 : c2->turn == TURN_RIGHT ? 1 : 0;
        ahead = d;
      }
    }
    // --- the others at the junction: give way, and never drive into a car that is crossing ---
    bool yielding = false;
    if (traffic && obey && next && !mustStop && node &&
        (node->arms.size() > 2 || !next->conflicts.empty()) && distToLine > -1.5 && distToLine < 28) {
      bool ok = traffic->mayPass(me, next, std::max(distToLine, 0.0));
      // everybody waits for somebody: after a while, with the junction empty, take a tighter gap
      if (!ok && yieldWait_ > 12 && traffic->clearOf(next) &&
          traffic->mayPass(me, next, std::max(distToLine, 0.0), true))
        ok = true;
      if (!ok) {
        mustStop = true;
        yielding = true;
        remark = "yield";
      }
    }
    if (yielding && speed < 0.5) yieldWait_ += dt;
    else if (!yielding) yieldWait_ = 0;
    // --- crossings on the way: a red pedestrian light, people at a zebra crossing ---
    // (on this lane and on the ones the way goes on to: a crossing may lie right behind a turn)
    if (obey) {
      scanCrossings(*lane, -laneS);
      double off = lane->len - laneS;
      const Connector* c = next;
      for (int hop = 0; c && hop < 4 && off < 60; hop++) {
        const Lane* l2 = c->to;
        off += c->len;
        scanCrossings(*l2, off);
        off += l2->len;
        c = onwardOf(route, l2);
      }
    }
    // the lane to get into is taken: hold back, and wait before the junction rather than push in
    if (laneBlocked) {
      limit = std::min(limit, std::max(3.0, speed - 1));
      if (distToLine < 14) {
        mustStop = true;
        remark = "yield";
      }
    }
    // --- the place to halt at: on this lane, or passed already ---
    if (hasHalt) {
      const Edge* e = lane->edge;
      if (haltLane_ != lane && haltX > e->minX - 8 && haltX < e->maxX + 8 && haltZ > e->minZ - 8 &&
          haltZ < e->maxZ + 8) {
        proj = projectOnPolyline(e->pl, haltX, haltZ, proj);
        if (proj.d2 < (e->width / 2 + 6) * (e->width / 2 + 6)) {
          haltLane_ = lane;
          haltS_ = lane->dir > 0 ? proj.s - lane->s0 : lane->s0 - proj.s;
        }
      }
      if (haltLane_ == lane) {
        // the middle of the car at the place; if it lies behind already, stop where it is safe: here
        double dd = std::max(0.0, std::min(haltS_, lane->len - 3) - centre);
        limit = std::min(limit, std::sqrt(2 * (DECEL * 0.7) * dd));
        halting = dd < 45;
        if (halting) remark = "halt";
        if (dd < 0.6) {
          crossStop = true;
          atHalt = true;
        }
      }
      // --- the end of the street, and the way leads back out of it: stop short of the end ---
      if (next && next->from == lane && next->turn == TURN_UTURN && lane->to->arms.size() == 1) {
        double dd = std::max(0.0, lane->len - 3.5 - centre);
        limit = std::min(limit, std::sqrt(2 * (DECEL * 0.7) * dd));
        if (dd < 45) {
          halting = true;
          remark = "deadEnd";
        }
        if (dd < 0.6) {
          crossStop = true;
          turnRound = const_cast<Connector*>(next);
        }
      }
    }
    if ((distToLine > 45 && ahead > 45) || distToLine < -3) turn = 0;
    lastTurn_ = turn;
  } else {
    // inside a junction or off the lane: keep following the last planned connector
    limit = 5;
  }
  // --- crossing the way of others: turning left, or through a junction made of several nodes ---
  if (traffic && obey && next && (me.lane || me.conn)) {
    bool left = next->turn == TURN_LEFT || next->turn == TURN_UTURN;
    bool cluster = (lane != nullptr && lane->len < 9) || next->to->len < 9;
    if ((left || cluster) && (me.conn == next || distToLine < 18)) {
      double xd = traffic->crossingAhead(me, 26);
      // (a car's length into their way already: on, and out of it)
      if (xd < INFINITY && xd > FRONT + 0.8) {
        if (me.lane && distToLine > 0.5) {
          // still before the junction: wait at the line, not somewhere inside in somebody's way
          mustStop = true;
        } else {
          double room = xd - FRONT - 3.2;
          limit = std::min(limit, std::sqrt(2 * DECEL * std::max(0.0, room)));
          if (room < 0.6) crossStop = true;
        }
        remark = "yield";
      }
    }
  }
  // --- whoever is ahead on the way: two seconds behind, and never faster than could be stopped ---
  double gap = INFINITY;
  if (traffic && (me.lane || me.conn)) {
    Lead lead = traffic->aheadOf(me, std::min(110.0, 18 + speed * 4));
    // somebody standing half in the way who does not move either: after a while, past him at a crawl
    if (lead.loose && lead.v < 0.3 && speed < 0.3 && lead.gap < 6) netWait_ += dt;
    else if (speed > 2 || !lead.loose) netWait_ = 0;
    if (lead.loose && netWait_ > 6) {
      // … with what room there is: at half a metre it is a standstill again
      limit = std::min(limit, 1.2);
      lead.gap = lead.gap < 0.6 ? lead.gap : INFINITY;
    }
    gap = lead.gap;
    if (lead.gap < INFINITY) {
      double room = lead.gap - 3;
      double follow = lead.v + 0.45 * (room - 1.9 * speed);
      double stoppable = std::sqrt(lead.v * lead.v + 2 * (DECEL * 0.85) * std::max(room, 0.0));
      double cap = std::max(0.0, std::min(follow, stoppable));
      if (cap < limit) {
        if (cap < limit - 0.6 && remark.empty()) remark = "follow";
        limit = cap;
      }
      if (lead.gap < 2.2) crossStop = true;
    }
  }
  // --- controls ---
  // (the bends further ahead are come up to at the speed they can be driven at; looked at a few
  //  times a second, the way ahead changes as the car moves)
  if (bendTick_++ % 6 == 0)
    bendLimit_ = (lane || next) ? bendCap(lane, lane ? laneS - FRONT : connS, next, route, speed) : 0;
  const double vBend = std::isfinite(bendLimit_) ? bendLimit_ : 1e300;

  // changing lanes is announced like a turn: indicator on, a look over the shoulder, then over
  changeT_ = change != 0 ? changeT_ + dt : 0;
  double look = turn != 0 && std::min(distToLine, ahead) < 25 ? (turn < 0 ? 1.6 : -1.6) : 0;
  if (turn != 0 && remark.empty()) remark = "turn";
  if (traffic && change != 0 && turn == 0) {
    turn = change;
    if (changeT_ < 1.6) look = change < 0 ? 1.6 : -1.6;
    if (remark.empty()) remark = "lane";
  }
  // --- controls ---
  double fx = -std::sin(v.yaw);
  double fz = -std::cos(v.yaw);
  double dx = tx - mpos[0];
  double dz = tz - mpos[2];
  double lat = dx * std::cos(v.yaw) - dz * std::sin(v.yaw);  // + = target to the right
  double fwd = dx * fx + dz * fz;
  double curv = (2 * lat) / std::max(dx * dx + dz * dz, 1.0);
  double steer = std::max(
      -1.0, std::min(1.0, (std::atan(spec.wheelBase * curv) / spec.maxWheelAngle) * (fwd > 0 ? 1 : 3)));
  // keep the lateral acceleration comfortable
  double vCurve = std::sqrt((TRUCK ? 1.5 : 2.2) / std::max(std::fabs(curv), 0.002));
  double target = std::min(limit * 0.94, vCurve);
  target = std::min(target, vBend);
  double brake = 0;
  if (mustStop && distToLine > -1) {
    double d = std::max(0.0, distToLine - (TRUCK ? 1.8 : 1.2));
    target = std::min(target, std::sqrt(2 * DECEL * d));
    if (d < 0.6) {
      target = 0;
      brake = 0.5;
    }
  }
  if (crossStop) {
    target = 0;
    brake = 0.5;
  }
  if (v.speed < 0.15 && mustStop && distToLine < 4) stoppedFor_ += dt;
  else if (!mustStop && distToLine > 12) stoppedFor_ = 0;
  double throttle = 0;
  double err = target - v.speed;
  if (err > 0) throttle = std::min(TRUCK ? 0.85 : 0.6, err * 0.35);
  else {
    // Braking for a line, for somebody in the way or at the destination is as firm as it takes;
    // easing off for a limit, a bend or a junction ahead stays gentle.
    bool firm = mustStop || crossStop || halting || stopAhead || gap < 40 ||
                vCurve < limit * 0.94 || vBend < v.speed - 3 || err < -4;
    double most = firm ? 0.8 : (TRUCK ? 1.6 : 2.1) / (TRUCK ? 6.5 : 9.5);
    brake = std::max(brake, std::min(most, -err * 0.3));
  }

  // Standing where the way is not free – a red light behind the next joint in the road, somebody
  // crossing: held on the brake.  (Left to itself, the car creeps on at walking pace, over the line.)
  if (target < 0.05 && v.speed < 1.2) brake = std::max(brake, 0.4);

  cmd.steer = steer;
  cmd.throttle = brake > 0 ? 0 : throttle;
  cmd.brake = brake;
  cmd.indicator = turn;
  cmd.look = look;
  cmd.remark = remark;
  cmd.waiting = mustStop || crossStop || (gap < 12 && target < 0.3);
  cmd.arrived = atHalt && std::fabs(v.speed) < 0.1;
  cmd.turnRound = (turnRound && std::fabs(v.speed) < 0.1) ? turnRound : nullptr;
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

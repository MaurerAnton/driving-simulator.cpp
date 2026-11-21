// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Ambient traffic: cars on the lane graph (IDM following, right-of-way from
 * the lane graph's conflicts) and pedestrians at the crossings.
 *
 * Origin: src/sim/traffic.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#include "sim/traffic.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "sim/signals.h"



namespace fs {

namespace {
/** Colours the original picks from. */
const uint32_t COLORS[14] = {0xd9dadc, 0x1c1d20, 0x8d9197, 0x3a3f46, 0xa01c1c, 0x1f3f7a,
                             0xf0efe8, 0x55606a, 0x7a2f2a, 0x2c5a3a, 0xb8b08c, 0x394a66,
                             0xc4c7ca, 0x101113};
constexpr int N_COLORS = 14;

PolySample gTmp, gTmp2;

/** Length of a lane's free stretch for a car of `length`. */
double usableLength(const Lane& lane) { return std::max(0.0, lane.len); }
/** SAT overlap of two oriented boxes on the ground (the original's boxesOverlap). */
bool boxesOverlap(double ax, double az, double ayaw, double ahl, double ahw,
                  double bx, double bz, double byaw, double bhl, double bhw) {
  const double axes[4] = {ayaw, ayaw + PI_ / 2, byaw, byaw + PI_ / 2};
  const double dx = bx - ax;
  const double dz = bz - az;
  for (double ang : axes) {
    const double ux = -std::sin(ang);
    const double uz = -std::cos(ang);
    const double dist_ = std::fabs(dx * ux + dz * uz);
    const double ra = ahl * std::fabs(std::cos(ang - ayaw)) + ahw * std::fabs(std::sin(ang - ayaw));
    const double rb = bhl * std::fabs(std::cos(ang - byaw)) + bhw * std::fabs(std::sin(ang - byaw));
    if (dist_ > ra + rb) return false;
  }
  return true;
}
}  // namespace

std::pair<int, int> Traffic::cellKey(double x, double z) {
  return {static_cast<int>(std::floor(x / 120)) + 512,
          static_cast<int>(std::floor(z / 120)) + 512};
}

Traffic::Traffic(World& world, LaneGraph& graph, SignalSystem& signals)
    : world_(world), graph_(graph), signals_(signals) {
  // register every drivable lane in the cells it passes through (spawn points)
  for (const auto& lane : graph_.lanes) {
    if (!LaneGraph::drivable(*lane.edge, true) || lane.len < 25) continue;
    if (lane.edge->flags & EF_ROUNDABOUT) continue;
    std::set<std::pair<int, int>> seen;
    for (double d = 0; d <= lane.len; d += 40) {
      gTmp = lanePoint(lane, d, gTmp);
      auto key = cellKey(gTmp.x, gTmp.z);
      if (!seen.insert(key).second) continue;
      // main roads carry more traffic: list them more than once
      int copies = lane.edge->tier >= 4 ? 4 : lane.edge->tier >= 3 ? 3 : 1;
      for (int k = 0; k < copies; k++) spawnGrid_[key].push_back(const_cast<Lane*>(&lane));
    }
  }
  for (const auto& c : world_.crossings()) crossingsByEdge_[c.e].push_back(c);
  // nobody: a player far away, so mayPass() checks only the traffic
  nobody_.x = nobody_.y = nobody_.z = 1e9;
}

void Traffic::clear() {
  cars.clear();
  peds.clear();
  onLane_.clear();
  onConn_.clear();
}

Pedestrian* Traffic::pedestrianAt(const CrossingJson& c) {
  for (auto& p : peds) {
    if (p.crossing.e == c.e && p.crossing.s == c.s && p.crossing.k == c.k) {
      if (p.state == "gone") continue;
      return &p;
    }
  }
  return nullptr;
}

void Traffic::spawnCar(const PlayerState& player) {
  // a random spot in the ring around the player
  for (int attempt = 0; attempt < 10; attempt++) {
    double ang = rng_() * PI_ * 2;
    double rad = 160 + rng_() * (spawnRadius - 160);
    auto key = cellKey(player.x + std::cos(ang) * rad, player.z + std::sin(ang) * rad);
    auto it = spawnGrid_.find(key);
    if (it == spawnGrid_.end() || it->second.empty()) continue;
    Lane* lane = it->second[static_cast<size_t>(rng_() * it->second.size()) % it->second.size()];
    double st = 6 + rng_() * std::max(0.0, lane->len - 14);
    gTmp = lanePoint(*lane, st, gTmp);
    double d = std::hypot(gTmp.x - player.x, gTmp.z - player.z);
    if (d < 150 || d > spawnRadius) continue;
    if (std::fabs(gTmp.y - player.y) > 60) continue;
    // not right in front of the player's nose
    double fx = -std::sin(player.yaw);
    double fz = -std::cos(player.yaw);
    double ahead = ((gTmp.x - player.x) * fx + (gTmp.z - player.z) * fz) / d;
    if (ahead > 0.82 && d < 260) continue;
    // free space on the lane
    auto li = onLane_.find(lane->id);
    if (li != onLane_.end()) {
      bool taken = false;
      for (const TrafficCar* o : li->second)
        if (std::fabs(o->s - st) < 14) taken = true;
      if (taken) continue;
    }
    int style = static_cast<int>(rng_() * 3);
    TrafficCar car;
    car.id = nextId_++;
    car.lane = lane;
    car.conn = nullptr;
    car.s = st;
    car.v = std::min(lane->limit, 13.0) * 0.8;
    car.a = 0;
    car.next = chooseNext(*lane);
    car.length = style == 1 ? 4.55 : 4.25;
    car.width = 1.8;
    car.color = COLORS[static_cast<size_t>(rng_() * N_COLORS) % N_COLORS];
    car.style = style;
    car.temper = 0.88 + rng_() * 0.14;
    car.x = gTmp.x;
    car.y = gTmp.y;
    car.z = gTmp.z;
    car.yaw = std::atan2(-gTmp.tx, -gTmp.tz);
    car.pitch = 0;
    car.blinker = 0;
    car.braking = false;
    car.waited = 0;
    car.yieldWait = 0;
    car.stoppedAtLine = false;
    car.stuck = 0;
    car.blockedByPlayer = 0;
    car.playerFirstGap = INFINITY;
    car.playerSeen = 0;
    car.crashed = 0;
    car.wheelSpin = 0;
    cars.push_back(car);
    attach(cars.back());
    return;
  }
}

void Traffic::attach(TrafficCar& car) {
  auto& map = car.conn ? onConn_ : onLane_;
  int key = car.conn ? car.conn->id : (car.lane ? car.lane->id : -1);
  map[key].push_back(&car);
}

void Traffic::detach(TrafficCar& car) {
  auto& map = car.conn ? onConn_ : onLane_;
  int key = car.conn ? car.conn->id : (car.lane ? car.lane->id : -1);
  auto it = map.find(key);
  if (it == map.end()) return;
  auto& list = it->second;
  auto f = std::find(list.begin(), list.end(), &car);
  if (f != list.end()) list.erase(f);
}

Connector* Traffic::chooseNext(Lane& lane) {
  const auto& outs = lane.out;
  if (outs.empty()) return nullptr;
  double total = 0;
  std::vector<double> w;
  for (const Connector* c : outs) {
    double weight = 0;
    if (LaneGraph::drivable(*c->to->edge, true)) {
      weight = c->turn == TURN_STRAIGHT ? 4 : c->turn == TURN_UTURN ? 0.02 : 1.4;
      weight *= 0.5 + c->to->edge->tier * 0.5;
      if (c->to->edge->cls <= MOTORWAY_LINK && lane.edge->cls > MOTORWAY_LINK) weight *= 0.35;
    }
    w.push_back(weight);
    total += weight;
  }
  if (total <= 0) return outs[static_cast<size_t>(rng_() * outs.size()) % outs.size()];
  double r = rng_() * total;
  for (size_t i = 0; i < outs.size(); i++) {
    r -= w[i];
    if (r <= 0) return outs[i];
  }
  return outs.back();
}

void Traffic::update(double dt, const PlayerState& player, std::vector<TrafficEvent>& events) {
  spawnTimer_ -= dt;
  if (spawnTimer_ <= 0 && static_cast<int>(cars.size()) < targetCount) {
    spawnCar(player);
    spawnTimer_ = 0.12;
  }
  for (auto it = cars.begin(); it != cars.end();) {
    TrafficCar& car = *it;
    double d = std::hypot(car.x - player.x, car.z - player.z);
    if (d > spawnRadius + 110 || car.stuck > 45 ||
        (car.crashed > 0 && (car.crashed -= dt) <= 0 && d > 60)) {
      detach(car);
      it = cars.erase(it);
      continue;
    }
    advanceCar(car, dt, player, events);
    ++it;
  }
  updatePeds(dt, player, events);
}

void Traffic::advanceCar(TrafficCar& car, double dt, const PlayerState& player,
                         std::vector<TrafficEvent>& events) {
  Lane* lane = car.lane;
  Connector* conn = car.conn;

  // --- desired speed ---------------------------------------------------------
  double limit = lane ? lane->limit : (conn ? conn->to->limit : 13.9);
  if (!std::isfinite(limit)) limit = 36;  // Richtgeschwindigkeit 130 km/h
  double v0 = limit * car.temper;
  if (lane && lane->edge->zone == 7) v0 = std::min(v0, 2.2);
  if ((lane && (lane->edge->flags & EF_ROUNDABOUT)) ||
      (conn && conn->node->ctrl == CTRL_ROUNDABOUT))
    v0 = std::min(v0, 7.2);
  if (conn) {
    // junction speed from the tightness of the turn
    double turnV = conn->turn == TURN_STRAIGHT ? 14
                   : conn->turn == TURN_UTURN  ? 2.5
                                               : clamp(std::sqrt(1.9 * std::max(conn->len, 4.0) * 0.7), 3.0, 9.0);
    v0 = std::min(v0, turnV);
  }
  const double A = 1.7;
  const double B = 2.4;
  double acc = A * (1 - std::pow(car.v / std::max(v0, 0.5), 4));

  auto brakeFor = [&](double gap, double vLead, double s0) {
    double dv = car.v - vLead;
    double sStar = s0 + std::max(0.0, car.v * 1.25 + (car.v * dv) / (2 * std::sqrt(A * B)));
    double g = std::max(gap, 0.1);
    acc = std::min(acc, A * (1 - std::pow(car.v / std::max(v0, 0.5), 4) - (sStar / g) * (sStar / g)));
  };

  // --- car ahead -------------------------------------------------------------
  Lead lead = leader(car, 90);
  if (lead.gap < INFINITY) brakeFor(lead.gap, lead.v, 2.3);

  // --- the player's car -------------------------------------------------------
  double pGap = playerAhead(car, player, clamp(car.v * 3.5 + 12, 14.0, 80.0));
  bool playerBrake = false;
  /** how hard the player's car alone makes this one brake */
  double playerAcc = 0;
  if (pGap < INFINITY) {
    double before = acc;
    double along = -std::sin(player.yaw) * -std::sin(car.yaw) +
                   -std::cos(player.yaw) * -std::cos(car.yaw);
    brakeFor(pGap, std::max(0.0, player.speed * along), 2.6);
    playerBrake = acc < before - 0.3;
    playerAcc = acc;
    if (car.playerSeen <= 0) car.playerFirstGap = pGap / std::max(car.v, 1.0);
    car.playerSeen = 1.5;
  } else if (car.playerSeen > 0) {
    car.playerSeen -= dt;
    if (car.playerSeen <= 0) car.playerFirstGap = INFINITY;
  }

  // --- whatever else is on the road: trams and trains -------------------------
  for (const auto& o : obstacles) {
    if (std::fabs(o.x - car.x) > 110 || std::fabs(o.z - car.z) > 110) continue;
    double gap = playerAhead(car, o, clamp(car.v * 3.5 + 12, 14.0, 80.0));
    if (gap == INFINITY) continue;
    double along = -std::sin(o.yaw) * -std::sin(car.yaw) - std::cos(o.yaw) * -std::cos(car.yaw);
    brakeFor(gap, std::max(0.0, o.speed * along), 2.6);
  }

  // --- junction ahead ---------------------------------------------------------
  double stopDist = INFINITY;
  if (lane) {
    Connector* next = car.next;
    double distToLine = lane->stopAt - car.s - car.length / 2;
    Node* node = lane->to;
    Arm* arm = lane->armTo;
    // slow down for the turn before reaching it
    if (next && next->turn != TURN_STRAIGHT) {
      double turnV = next->turn == TURN_UTURN ? 2.5
                     : clamp(std::sqrt(1.9 * std::max(next->len, 4.0) * 0.7), 3.0, 9.0);
      double d = std::max(0.0, lane->len - car.s - car.length / 2);
      double need = (car.v * car.v - turnV * turnV) / (2 * std::max(d, 1.0));
      if (need > 0.6) acc = std::min(acc, -need);
      car.blinker = d < 45 ? (next->turn == TURN_RIGHT ? 1 : -1) : 0;
    } else if (next && node->ctrl == CTRL_ROUNDABOUT && (lane->edge->flags & EF_ROUNDABOUT) &&
               !(next->to->edge->flags & EF_ROUNDABOUT)) {
      car.blinker = 1;  // leaving the roundabout
    } else {
      car.blinker = 0;
    }

    bool yieldingNow = false;
    if (next && (node->arms.size() > 2 || !next->conflicts.empty()) && distToLine < 70) {
      bool mustStop = false;
      if (arm && arm->sg >= 0 && node->sc >= 0) {
        int st = signals_.get(node->sc, arm->sg);
        if (st == SIG_RED || st == SIG_RED_AMBER) mustStop = true;
        else if (st == SIG_AMBER) {
          double need = (car.v * car.v) / (2 * std::max(distToLine, 0.5));
          mustStop = distToLine > 0 && need < 3.2;
        }
      }
      if (!mustStop && distToLine > -1.5 && arm) {
        int role = arm->role;
        bool needsStopSign = role == ROLE_STOP && !car.stoppedAtLine;
        bool yielding = false;
        for (const Conflict& k : next->conflicts)
          if (k.yield) yielding = true;
        yielding = yielding || role == ROLE_MINOR || role == ROLE_YIELD || role == ROLE_STOP;
        if (needsStopSign) mustStop = true;
        else if (yielding || node->ctrl == CTRL_RBL) {
          // only decide close to the line; approach at a speed that still allows stopping
          if (distToLine < 28) {
            bool ok = mayEnter(car, next, std::max(distToLine, 0.0), player);
            if (!ok) {
              mustStop = true;
              yieldingNow = true;
              // stalemate (everybody waits for somebody): after a while creep through
              int patience = node->ctrl == CTRL_RBL ? 6 + (car.id % 5) : 10 + (car.id % 7);
              if (car.yieldWait > patience && junctionEmpty(next) &&
                  mayEnter(car, next, std::max(distToLine, 0.0), player, true))
                mustStop = false;
            }
          } else if (role != ROLE_PRIORITY) {
            double vApproach = 7;
            if (car.v > vApproach) acc = std::min(acc, -1.4);
          }
        }
      }
      // whoever has the right of way: never drive into a car that is crossing our path
      if (!mustStop && distToLine > -1.5 && distToLine < 32 && boxBlocked(next)) mustStop = true;
      if (mustStop && distToLine > -1.5) stopDist = std::max(0.0, distToLine);
    }
    if (yieldingNow && car.v < 0.5) car.yieldWait += dt;
    else if (!yieldingNow) car.yieldWait = 0;
    // signalised or zebra crossings on this lane
    auto xs = crossingsByEdge_.find(lane->edge->id);
    if (xs != crossingsByEdge_.end()) {
      for (const auto& x : xs->second) {
        double pos = lane->dir > 0 ? x.s - lane->s0 : lane->s0 - x.s;
        double d = pos - x.w / 2 - 1.2 - car.s - car.length / 2;
        if (d < -2 || d > 60) continue;
        bool stop = false;
        if (x.k == XING_SIGNAL && x.sc >= 0) {
          int st = signals_.get(x.sc, x.g >= 0 ? x.g : 0);
          stop = st == SIG_RED || st == SIG_RED_AMBER ||
                 (st == SIG_AMBER && (car.v * car.v) / (2 * std::max(d, 0.5)) < 3.2 && d > 0);
        } else if (x.k == XING_ZEBRA) {
          for (const auto& pd : peds)
            if (pd.crossing.e == x.e && pd.crossing.s == x.s && pd.crossing.k == x.k &&
                pd.state != "gone" && (pd.state == "cross" || pd.waited > 0.3))
              stop = true;
        }
        if (stop) stopDist = std::min(stopDist, std::max(0.0, d));
      }
    }
  }
  if (conn) {
    for (const Conflict& k : conn->conflicts) {
      double dSelf = k.sSelf - car.s - car.length / 2;
      if (dSelf < -1) continue;  // already past the conflict point
      auto it = onConn_.find(k.other->id);
      if (it == onConn_.end()) continue;
      for (const TrafficCar* o : it->second) {
        double dOther = k.sOther - o->s;
        if (dOther < -(o->length / 2 + 1.5)) continue;  // it has cleared the point
        bool wait;
        if (k.yield) wait = true;
        else {
          const Conflict* back = nullptr;
          for (const Conflict& q : k.other->conflicts)
            if (q.other == conn) back = &q;
          if (back && back->yield) wait = false;
          else {
            double tSelf = std::max(dSelf, 0.0) / (car.v + 0.5);
            double tOther = std::max(dOther - o->length / 2, 0.0) / (o->v + 0.5);
            wait = tSelf > tOther + 0.3 || (std::fabs(tSelf - tOther) <= 0.3 && car.id > o->id);
          }
        }
        if (wait) stopDist = std::min(stopDist, std::max(0.0, dSelf - (k.merge ? 1.0 : 3.0)));
      }
    }
  }
  if (stopDist < INFINITY) {
    brakeFor(stopDist + 0.4, 0, 0.6);
    if (stopDist < 1.2 && car.v < 0.4) {
      car.waited += dt;
      if (car.waited > 0.9) car.stoppedAtLine = true;
    }
  } else if (car.v > 1) {
    car.waited = 0;
  }

  if (car.crashed > 0) acc = -6;
  acc = clamp(acc, -7.5, A);
  // report hard braking caused by the player (the rule monitor decides who was at fault)
  if (playerBrake && playerAcc < -2.6 && car.v > 2) {
    car.blockedByPlayer += dt;
    if (car.blockedByPlayer > 0.12) {
      events.push_back(ForcedBrakeEvent{&car, -acc});
      car.blockedByPlayer = -3;  // one report per incident
    }
  } else if (car.blockedByPlayer > 0) {
    car.blockedByPlayer = 0;
  } else if (car.blockedByPlayer < 0) {
    car.blockedByPlayer = std::min(0.0, car.blockedByPlayer + dt);
  }

  car.a = acc;
  car.braking = acc < -0.5 || (car.v < 0.2 && stopDist < 3);
  car.v = std::max(0.0, car.v + acc * dt);
  if (car.v < 0.05 && acc <= 0) car.v = 0;
  car.stuck = car.v < 0.1 ? car.stuck + dt : 0;
  double ds = car.v * dt;
  car.wheelSpin += ds / 0.31;

  // --- advance along the path -------------------------------------------------
  detach(car);
  car.s += ds;
  for (int guard = 0; guard < 4; guard++) {
    if (car.conn) {
      if (car.s < car.conn->len) break;
      car.s -= car.conn->len;
      car.lane = car.conn->to;
      car.conn = nullptr;
      car.next = chooseNext(*car.lane);
      car.stoppedAtLine = false;
      car.waited = 0;
      car.yieldWait = 0;
    } else if (car.lane) {
      if (car.s < car.lane->len) break;
      Connector* next = car.next ? car.next : chooseNext(*car.lane);
      if (!next) {
        // dead end of the map: leave the simulation
        car.s = car.lane->len;
        car.stuck = 100;
        break;
      }
      car.s -= car.lane->len;
      car.conn = next;
      car.lane = nullptr;
    }
  }
  attach(car);

  // --- pose -------------------------------------------------------------------
  if (car.conn) {
    gTmp = samplePolyline(car.conn->pl, car.s, gTmp);
  } else if (car.lane) {
    gTmp = lanePoint(*car.lane, car.s, gTmp);
    gTmp.y = world_.edgeY(*car.lane->edge, car.lane->s0 + car.lane->dir * car.s, gTmp.x, gTmp.z);
  } else {
    return;
  }
  double yaw = std::atan2(-gTmp.tx, -gTmp.tz);
  // slope from the height change since the last frame
  if (ds > 0.01) car.pitch += (std::atan2(gTmp.y - car.y, ds) - car.pitch) * std::min(1.0, dt * 6);
  car.x = gTmp.x;
  car.y = gTmp.y;
  car.z = gTmp.z;
  car.yaw = yaw;

  // --- contact with the player -------------------------------------------------
  double dx = player.x - car.x;
  double dz = player.z - car.z;
  if (dx * dx + dz * dz < 9 && std::fabs(player.y - car.y) < 2.5) {
    if (boxesOverlap(car.x, car.z, car.yaw, car.length / 2, car.width / 2, player.x, player.z,
                     player.yaw, player.length / 2, player.width / 2)) {
      if (car.crashed <= 0)
        events.push_back(CollisionEvent{&car, std::fabs(player.speed) + car.v});
      car.crashed = 6;
      car.v = 0;
    }
  }
}


// --- what a vehicle that drives itself asks -----------------------------------

TrafficCar* Traffic::aim(const SelfDriving& me) {
  TrafficCar& p = probe_;
  p.lane = me.conn ? nullptr : me.lane;
  p.conn = me.conn;
  p.s = me.s;
  p.v = me.v;
  p.next = me.next;
  p.length = me.length;
  p.width = me.width;
  p.x = me.x;
  p.y = me.y;
  p.z = me.z;
  p.yaw = me.yaw;
  return &p;
}

/**
 * The nearest vehicle ahead on the path of `car`: a car of the traffic, or a
 * tram or train in its way (the obstacles).  Hops lane by lane, connector by
 * connector, out to `horizon`.
 */
Lead Traffic::leader(TrafficCar& car, double horizon) {
  Lead out;
  double best = INFINITY, bestV = 0;
  auto scan = [&](const std::vector<TrafficCar*>* list, double offset) {
    if (!list) return;
    for (const TrafficCar* o : *list) {
      if (o == &car) continue;
      double d = offset + o->s;
      if (d > 0 && d < best) {
        best = d - o->length / 2 - car.length / 2;
        bestV = o->v;
        if (best < 0) best = 0;
      }
    }
  };
  double offset = -car.s;
  Lane* lane = car.lane;
  Connector* conn = car.conn;
  Connector* next = car.next;
  // cars that left our lane on another connector still share the first metres with us
  auto siblings = [&](const Connector* c, double off, double behindS) {
    for (const Sibling& sib : c->siblings) {
      auto it = onConn_.find(sib.other->id);
      if (it == onConn_.end()) continue;
      for (const TrafficCar* o : it->second)
        if (o != &car && o->s - o->length / 2 < sib.untilOther && o->s > behindS) {
          std::vector<TrafficCar*> one{const_cast<TrafficCar*>(o)};
          scan(&one, off);
        }
    }
  };
  // Cars on other connectors into the same lane: whoever is nearer to the end of
  // its connector is "ahead"; side by side, the one that has to give way (or,
  // failing that, the higher id) drops back.
  auto merging = [&](const Connector* c, double off) {
    for (const Conflict& k : c->conflicts) {
      if (!k.merge) continue;
      const Connector* other = k.other;
      auto tie = [&](const TrafficCar* o) {
        bool iYield = k.yield;
        if (!iYield) {
          const Conflict* back = nullptr;
          for (const Conflict& q : other->conflicts)
            if (q.other == c) back = &q;
          iYield = (!back || !back->yield) && car.id > o->id;
        }
        if (iYield && best > 0.3) {
          best = 0.3;
          bestV = std::min(o->v, car.v) * 0.5;
        }
      };
      // already on the other connector: compare progress relative to where the paths meet
      auto it = onConn_.find(other->id);
      if (it != onConn_.end()) {
        for (const TrafficCar* o : it->second) {
          double d = off + k.sSelf + (o->s - k.sOther);
          if (d > 0.6) {
            std::vector<TrafficCar*> one{const_cast<TrafficCar*>(o)};
            scan(&one, off + k.sSelf - k.sOther);
          } else if (d > -0.6) tie(o);
        }
      }
      // still on the neighbouring lane of the same carriageway (zip merging)
      if (other->from->armTo == c->from->armTo) {
        auto l3 = onLane_.find(other->from->id);
        if (l3 != onLane_.end()) {
          for (const TrafficCar* o : l3->second) {
            if (o->next != other) continue;
            double ahead = off - (other->from->len - o->s);
            if (ahead > 0.8) {
              double gap = ahead - o->length / 2 - car.length / 2;
              if (gap < best) {
                best = std::max(0.0, gap);
                bestV = o->v;
              }
            } else if (ahead > -0.8) tie(o);
          }
        }
      }
    }
  };
  for (int hop = 0; hop < 6 && offset < horizon; hop++) {
    if (conn) {
      auto it = onConn_.find(conn->id);
      if (it != onConn_.end()) scan(&it->second, offset);
      siblings(conn, offset, hop == 0 ? car.s : -1);
      merging(conn, offset);
      offset += conn->len;
      lane = conn->to;
      conn = nullptr;
      next = nullptr;
    } else if (lane) {
      auto it = onLane_.find(lane->id);
      if (it != onLane_.end()) scan(&it->second, offset);
      offset += lane->len;
      conn = next ? next : (lane->out.size() == 1 ? lane->out[0] : nullptr);
      if (conn && hop == 0) merging(conn, offset);
      if (!conn) {
        // route beyond this lane not chosen yet: anything just past its end may be in our way
        for (const Connector* c : lane->out) {
          auto l2 = onConn_.find(c->id);
          if (l2 != onConn_.end())
            for (const TrafficCar* o : l2->second)
              if (o->s < 14) {
                std::vector<TrafficCar*> one{const_cast<TrafficCar*>(o)};
                scan(&one, offset);
              }
        }
        break;
      }
      lane = nullptr;
    }
  }
  out.gap = best;
  out.v = bestV;
  return out;
}

/**
 * How far along `car`'s way the player's box lies (or INFINITY): walk the way
 * and test each point against the player's oriented box, in the player's frame.
 */
double Traffic::playerAhead(TrafficCar& car, const PlayerState& player, double horizon,
                            const std::vector<Connector>* route) {
  double dx = player.x - car.x;
  double dz = player.z - car.z;
  if (dx * dx + dz * dz > (horizon + 8) * (horizon + 8)) return INFINITY;
  if (std::fabs(player.y - car.y) > 4) return INFINITY;
  double c = std::cos(player.yaw);
  double sn = std::sin(player.yaw);
  double hl = player.length / 2 + 0.3;
  double hw = player.width / 2 + car.width / 2 + 0.25;
  double dist = 0;
  Lane* lane = car.lane;
  Connector* conn = car.conn;
  Connector* next = car.next;
  double at = car.s;
  const double step = 1.5;
  for (int n = 0; n < 60 && dist < horizon; n++) {
    at += step;
    dist += step;
    if (conn) {
      if (at > conn->len) {
        at -= conn->len;
        lane = conn->to;
        conn = nullptr;
        next = nullptr;
      }
    } else if (lane) {
      // undecided beyond this lane: assume straight on (unless the way is known)
      Connector* nx = next;
      if (!nx && route)
        for (const Connector& c2 : *route)
          if (c2.from == lane) nx = const_cast<Connector*>(&c2);
      if (!nx)
        for (Connector* q : lane->out)
          if (q->turn == TURN_STRAIGHT) {
            nx = q;
            break;
          }
      if (!nx && !lane->out.empty()) nx = lane->out[0];
      if (!nx) return INFINITY;
      at -= lane->len;
      conn = nx;
      lane = nullptr;
    }
    if (conn) {
      gTmp2 = samplePolyline(conn->pl, at, gTmp2);
    } else if (lane) {
      gTmp2 = lanePoint(*lane, at, gTmp2);
    } else {
      return INFINITY;
    }
    // into the player's frame: forward = (-s, -c), right = (c, -s)
    double px = gTmp2.x - player.x;
    double pz = gTmp2.z - player.z;
    double lf = -px * sn - pz * c;
    double lr = px * c - pz * sn;
    // Passing the other's nose or tail crosswise takes half this car's width more
    // than following it does: `across` is how much the way runs across the other.
    double along = -sn * gTmp2.tx - c * gTmp2.tz;
    double across = std::sqrt(std::max(0.0, 1 - along * along));
    if (std::fabs(lf) < hl + across * (car.width / 2 + 0.2) && std::fabs(lr) < hw)
      return std::max(0.0, dist - car.length / 2);
  }
  return INFINITY;
}

/**
 * Walk along the way of a vehicle – its lane or connector, then the next
 * connector, then on (as `route` says, or straight) – and call `visit` every
 * `step` metres with the point, the direction of travel there and the distance
 * from the start.  Ends after `reach` metres, at the end of the way, or when
 * `visit` returns true.
 */
void Traffic::walk(Lane* lane, Connector* conn, double s, Connector* next,
                   const std::vector<Connector>* route, double step, double reach,
                   const std::function<bool(double, double, double, double, double)>& visit) {
  double at = s;
  for (double dist = step; dist <= reach; dist += step) {
    at += step;
    if (conn) {
      if (at > conn->len) {
        at -= conn->len;
        lane = conn->to;
        conn = nullptr;
        next = nullptr;
      }
    } else if (lane && at > lane->len) {
      Connector* nx = next;
      if (!nx && route)
        for (const Connector& c : *route)
          if (c.from == lane) nx = const_cast<Connector*>(&c);
      if (!nx)
        for (Connector* q : lane->out)
          if (q->turn == TURN_STRAIGHT) {
            nx = q;
            break;
          }
      if (!nx && !lane->out.empty()) nx = lane->out[0];
      if (!nx) return;
      at -= lane->len;
      conn = nx;
      lane = nullptr;
    }
    if (conn) {
      gTmp2 = samplePolyline(conn->pl, at, gTmp2);
    } else if (lane) {
      gTmp2 = lanePoint(*lane, at, gTmp2);
    } else {
      return;
    }
    if (visit(gTmp2.x, gTmp2.z, gTmp2.tx, gTmp2.tz, dist)) return;
  }
}

double Traffic::playerOnPath(const PlayerState& player, const Connector* c) {
  const Edge* e = c->from->edge;
  const double pad = 8;
  if (player.x < e->minX - pad || player.x > e->maxX + pad || player.z < e->minZ - pad ||
      player.z > e->maxZ + pad)
    return -1;  // null: not on it
  if (std::fabs(player.y - c->node->y) > 8) return -1;
  // on the connector?
  double best = INFINITY, bestS = 0;
  size_t n = c->pl.cum.size();
  for (size_t i = 0; i < n; i++) {
    double d = std::hypot(c->pl.pts[i * 3] - player.x, c->pl.pts[i * 3 + 2] - player.z);
    if (d < best) {
      best = d;
      bestS = c->pl.cum[i];
    }
  }
  if (best < 2.2) return c->len * 0.5 - bestS;
  // on the feeding lane, heading for the junction?
  const Lane* from = c->from;
  int steps = std::min(40, static_cast<int>(std::ceil(from->len / 3)));
  for (int i = 0; i <= steps; i++) {
    double d = from->len - i * 3.0;
    if (d < 0) break;
    gTmp2 = lanePoint(*from, d, gTmp2);
    if (std::hypot(gTmp2.x - player.x, gTmp2.z - player.z) < 2.4) {
      double heading = -std::sin(player.yaw) * gTmp2.tx - std::cos(player.yaw) * gTmp2.tz;
      if (heading < 0.3) return -1;  // going the other way
      return i * 3.0 + c->len * 0.5;
    }
  }
  return -1;
}

/** Is a car inside the junction on a path that crosses or joins `c` ahead of its conflict point? */
bool Traffic::boxBlocked(const Connector* c) {
  for (const Conflict& k : c->conflicts) {
    auto it = onConn_.find(k.other->id);
    if (it == onConn_.end()) continue;
    for (const TrafficCar* o : it->second)
      if (k.merge || o->s - o->length / 2 < k.sOther + 2.5) return true;
  }
  return false;
}

bool Traffic::junctionEmpty(const Connector* c) {
  for (const Conflict& k : c->conflicts) {
    auto it = onConn_.find(k.other->id);
    if (it != onConn_.end() && !it->second.empty()) return false;
  }
  return true;
}

/**
 * May `car` drive into the junction through `c` – room behind it, no vehicle
 * already inside on a crossing path, and enough gap to the traffic that has
 * the right of way (including the player, on the lane feeding the conflict).
 */
bool Traffic::mayEnter(TrafficCar& car, Connector* c, double distToLine, const PlayerState& player,
                       bool impatient, double caution) {
  // room behind the junction, so we never stop inside it
  auto exit = onLane_.find(c->to->id);
  if (exit != onLane_.end())
    for (const TrafficCar* o : exit->second)
      if (o->s < car.length + 3 && o->v < 1.5) return false;
  double tEnter = car.v > 1 ? distToLine / car.v
                            : std::sqrt((2 * std::max(distToLine, 0.5)) / 1.5);
  for (const Conflict& k : c->conflicts) {
    // vehicles already inside the junction on a crossing path: never drive into them
    auto inside = onConn_.find(k.other->id);
    if (inside != onConn_.end()) {
      for (const TrafficCar* o : inside->second) {
        if (k.merge || o->s - o->length / 2 < k.sOther + 2.5) return false;
      }
    }
    if (!k.yield) continue;
    // (`caution`: seconds of gap on top – for a driver who is to make nobody brake)
    double myTime = tEnter + (k.sSelf + car.length) / std::max(car.v, 3.5) + 1.2 + caution;
    // traffic approaching the conflicting movement – also from further upstream,
    // because the lane feeding it can be just a few metres long (roundabouts)
    const Lane* from = k.other->from;
    bool zipper = from->armTo == c->from->armTo;
    auto threat = [&](const TrafficCar* o, double d) -> bool {
      // lanes of one carriageway running together: only whoever is level or ahead goes first
      if (zipper) return d - k.sOther < distToLine + car.length + 1.5;
      double t = d / std::max(o->v, 2.5);
      // after a long wait accept a tighter (but still safe) gap
      return impatient ? t < 2.6 + caution && d < 60 + caution * 12
                       : t < myTime + 1.5 && d < 110 + caution * 20;
    };
    auto approaching = onLane_.find(from->id);
    if (approaching != onLane_.end()) {
      for (const TrafficCar* o : approaching->second) {
        if (o->next && o->next != k.other) continue;
        if (threat(o, from->len - o->s + k.sOther)) return false;
      }
    }
    if (from->len < 70) {
      for (const Connector* cin : from->in) {
        auto l1 = onConn_.find(cin->id);
        if (l1 != onConn_.end())
          for (const TrafficCar* o : l1->second)
            if (threat(o, cin->len - o->s + from->len + k.sOther)) return false;
        const Lane* up = cin->from;
        if (cin->len + from->len > 70) continue;
        auto l2 = onLane_.find(up->id);
        if (l2 != onLane_.end())
          for (const TrafficCar* o : l2->second)
            if (o->next == cin && threat(o, up->len - o->s + cin->len + from->len + k.sOther))
              return false;
      }
    }
    // the player on that lane or in the junction
    double pd = playerOnPath(player, k.other);
    if (pd >= 0) {
      double d = pd;
      if (d < 0) continue;  // already past the conflict point
      double t = d / std::max(player.speed, 2.0);
      if (player.speed > 0.8 && t < myTime + 2 && d < 120) return false;
      if (d < 8) return false;
    }
  }
  return true;
}

// --- the public queries (0.7.0) ----------------------------------------------

Lead Traffic::aheadOf(const SelfDriving& me, double horizon) {
  TrafficCar* p = aim(me);
  Lead lead = leader(*p, horizon);
  lead.loose = false;
  for (const auto& o : obstacles) {
    if (std::fabs(o.x - p->x) > horizon + 30 || std::fabs(o.z - p->z) > horizon + 30) continue;
    double gap = playerAhead(*p, o, horizon);
    if (gap >= lead.gap) continue;
    double along = -std::sin(o.yaw) * -std::sin(p->yaw) - std::cos(o.yaw) * -std::cos(p->yaw);
    lead.gap = gap;
    lead.v = std::max(0.0, o.speed * along);
  }
  // Whoever stands or drives in the way without being "ahead on the lane": a car
  // coming the other way on the wrong side of a narrow street, one crossing, one
  // standing half in the road.  (`loose`: found by where it is.)
  double near = std::min(horizon, 8 + p->v * 2.2);
  PlayerState& box = box_;
  std::vector<Connector>* route = me.route;
  for (const auto& o : cars) {
    if (std::fabs(o.x - p->x) > near + 8 || std::fabs(o.z - p->z) > near + 8) continue;
    box.x = o.x;
    box.y = o.y;
    box.z = o.z;
    box.yaw = o.yaw;
    box.speed = o.v;
    box.length = o.length;
    box.width = o.width;
    double gap = playerAhead(*p, box, near, route);
    if (gap >= lead.gap - 0.5) continue;
    double along = -std::sin(o.yaw) * -std::sin(p->yaw) - std::cos(o.yaw) * -std::cos(p->yaw);
    lead.gap = gap;
    lead.v = std::max(0.0, o.v * along);
    lead.loose = true;
  }
  return lead;
}

double Traffic::crossingAhead(const SelfDriving& me, double reach) {
  TrafficCar* p = aim(me);
  std::vector<Connector>* route = me.route;
  std::vector<double>& way = way_;
  way.clear();
  walk(p->lane, p->conn, p->s, p->next, route, 1.5, reach,
       [&](double x, double z, double tx, double tz, double dist) {
         way.push_back(x);
         way.push_back(z);
         way.push_back(tx);
         way.push_back(tz);
         way.push_back(dist);
         return false;
       });
  if (way.empty()) return INFINITY;
  double vMe = std::max(p->v, 2.5);
  double best = INFINITY;
  for (const auto& o : cars) {
    if (o.v < 2.5 || o.a < -0.8) continue;
    double dx = o.x - p->x;
    double dz = o.z - p->z;
    if (dx * dx + dz * dz > 85 * 85) continue;
    walk(o.lane, o.conn, o.s, o.next, nullptr, 2, std::min(75.0, o.v * 4.5),
         [&](double qx, double qz, double qtx, double qtz, double qd) {
           double tq = qd / o.v;
           for (size_t i = 0; i + 4 < way.size(); i += 5) {
             double d = way[i + 4];
             if (d >= best) break;
             double ex = way[i] - qx;
             double ez = way[i + 1] - qz;
             if (ex * ex + ez * ez > 2.3 * 2.3) continue;
             // going the same way is no crossing
             if (way[i + 2] * qtx + way[i + 3] * qtz > 0.8) continue;
             if (std::fabs(tq - d / vMe) > 2.8) continue;
             best = d;
             return true;
           }
           return false;
         });
  }
  return best;
}

bool Traffic::mayPass(const SelfDriving& me, Connector* c, double distToLine, bool impatient) {
  TrafficCar* p = aim(me);
  if (boxBlocked(c)) return false;
  // a learner's gap: two seconds more than the traffic's own cars take
  return mayEnter(*p, c, distToLine, nobody_, impatient, 2);
}

bool Traffic::roomOn(const Lane& lane, double s, double v, double length) {
  auto it = onLane_.find(lane.id);
  if (it == onLane_.end()) return true;
  for (const TrafficCar* o : it->second) {
    double d = o->s - s;  // + = ahead of us
    if (d >= 0) {
      if (d < length + 4 + std::max(0.0, v - o->v) * 1.5) return false;
    } else if (-d < length + 6 + std::max(0.0, o->v - v) * 3.5) {
      return false;
    }
  }
  return true;
}

bool Traffic::clearOf(const Connector* c) { return junctionEmpty(c); }

void Traffic::updatePeds(double dt, const PlayerState& player, std::vector<TrafficEvent>& events) {
  // spawn a pedestrian at a crossing now and then
  if (peds.size() < 6 && !world_.crossings().empty()) {
    spawnTimer_ -= dt;
    if (spawnTimer_ <= 0) {
      spawnTimer_ = 3 + rng_() * 5;
      const auto& c = world_.crossings()[static_cast<size_t>(rng_() * world_.crossings().size()) %
                                         world_.crossings().size()];
      Edge* e = c.e >= 0 && static_cast<size_t>(c.e) < world_.edges().size()
                    ? &world_.edges()[static_cast<size_t>(c.e)]
                    : nullptr;
      if (e && e->pl.cum.size() >= 2) {
        Pedestrian p;
        p.id = static_cast<int>(peds.size());
        p.crossing = c;
        p.t = -1;
        p.dir = 1;
        p.state = "wait";
        p.waited = 0;
        p.half = e->width / 2 + std::max(e->swL, e->swR);
        p.speed = 1.2 + rng_() * 0.6;
        p.phase = rng_() * 6.28;
        p.colour = COLORS[static_cast<size_t>(rng_() * N_COLORS) % N_COLORS];
        peds.push_back(p);
      }
    }
  }

  for (auto& p : peds) {
    Edge* e = p.crossing.e >= 0 && static_cast<size_t>(p.crossing.e) < world_.edges().size()
                  ? &world_.edges()[static_cast<size_t>(p.crossing.e)]
                  : nullptr;
    if (!e) continue;
    if (p.state == "gone") continue;
    gTmp2 = samplePolyline(e->pl, p.crossing.s, gTmp2);
    // across the road: the road's left normal is (tz, -tx)
    double nx = gTmp2.tz, nz = -gTmp2.tx;
    double across = p.t * p.half;
    p.x = gTmp2.x + nx * across;
    p.y = gTmp2.y;
    p.z = gTmp2.z + nz * across;
    p.yaw = std::atan2(-nx * p.dir, -nz * p.dir);

    if (p.state == "wait") {
      p.waited += dt;
      // cross when the road is clear (a zebra has priority, so only cars matter)
      bool clear = true;
      for (const auto& car : cars) {
        if (std::hypot(car.x - p.x, car.z - p.z) < 12 && car.v > 1) {
          clear = false;
          break;
        }
      }
      if (clear && p.waited > 0.6) p.state = "cross";
    } else if (p.state == "cross") {
      p.t += p.dir * p.speed * dt / std::max(p.half, 0.5);
      // endangered by a car that is too close and moving
      for (const auto& car : cars) {
        if (car.v < 1.5) continue;
        double d = std::hypot(car.x - p.x, car.z - p.z);
        if (d < 4.5) {
          events.push_back(PedestrianEndangeredEvent{&p});
          break;
        }
      }
      if (std::fabs(p.t) >= 1) {
        p.state = "gone";
      }
    }
  }

  // the player may endanger a pedestrian on the crossing
  for (auto& p : peds) {
    if (p.state != "cross") continue;
    double d = std::hypot(player.x - p.x, player.z - p.z);
    if (d < 4.5 && player.speed > 1.5) events.push_back(PedestrianEndangeredEvent{&p});
  }
}

}  // namespace fs



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
}  // namespace

Traffic::Traffic(World& world, LaneGraph& graph, SignalSystem& signals)
    : world_(world), graph_(graph), signals_(signals) {}

void Traffic::clear() {
  cars.clear();
  peds.clear();
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

void Traffic::spawnCar() {
  if (graph_.lanes.empty()) return;
  // pick a random lane that can actually be driven on
  for (int attempt = 0; attempt < 8; attempt++) {
    double r = rng_();
    Lane& lane = graph_.lanes[static_cast<size_t>(r * graph_.lanes.size()) %
                              graph_.lanes.size()];
    if (!LaneGraph::drivable(*lane.edge, true)) continue;
    if (lane.len < 30) continue;
    // keep clear of the player's car
    bool tooClose = false;
    for (const auto& c : cars) {
      if (c.lane == &lane && std::fabs(c.s - lane.len * 0.5) < 40) tooClose = true;
    }
    if (tooClose) continue;

    TrafficCar car;
    car.id = static_cast<int>(cars.size());
    car.lane = &lane;
    car.conn = nullptr;
    car.s = 10 + rng_() * std::max(1.0, lane.len - 40);
    car.v = 0;
    car.a = 0;
    car.length = 4.1 + rng_() * 1.2;
    car.width = 1.75 + rng_() * 0.25;
    car.color = COLORS[static_cast<size_t>(rng_() * N_COLORS) % N_COLORS];
    car.style = static_cast<int>(rng_() * 3);
    car.temper = 0.85 + rng_() * 0.3;
    car.waited = 0;
    car.yieldWait = 0;
    car.stoppedAtLine = false;
    car.stuck = 0;
    car.blockedByPlayer = 0;
    car.crashed = 0;
    car.wheelSpin = 0;
    car.next = nullptr;
    // where does it want to go? any connector out of this lane
    if (!lane.out.empty()) car.next = lane.out[static_cast<size_t>(rng_() * lane.out.size()) % lane.out.size()];
    PolySample s = lanePoint(lane, car.s, gTmp);
    car.x = s.x;
    car.y = s.y;
    car.z = s.z;
    car.yaw = std::atan2(-s.tx, -s.tz);
    car.pitch = 0;
    cars.push_back(car);
    return;
  }
}

void Traffic::update(double dt, const PlayerState& player, std::vector<TrafficEvent>& events) {
  // keep the population near the requested density (the original counts the
  // lanes, so a denser world simply carries more cars)
  double want = density * graph_.lanes.size() * 0.08;
  spawnTimer_ -= dt;
  if (cars.size() < static_cast<size_t>(want) && spawnTimer_ <= 0) {
    spawnCar();
    spawnTimer_ = 0.35;
  }

  for (auto& car : cars) advanceCar(car, dt, player, events);
  updatePeds(dt, player, events);

  // drop the cars that have gone stale (crashed out or stuck too long)
  cars.erase(std::remove_if(cars.begin(), cars.end(),
                            [](const TrafficCar& c) { return c.crashed > 6 || c.stuck > 45; }),
             cars.end());
}

void Traffic::advanceCar(TrafficCar& car, double dt, const PlayerState& player,
                         std::vector<TrafficEvent>& events) {
  Lane* lane = car.lane;
  if (!lane) return;

  // --- where do we want to be? ---------------------------------------------
  double limit = lane->limit;
  if (!std::isfinite(limit) || limit <= 0) limit = 13.9;  // 50 km/h default
  double target = limit * car.temper;
  // slow down for the road ahead: the next connector's turn and the limit
  // beyond it
  if (car.next) {
    double nextLimit = car.next->to->limit;
    if (!std::isfinite(nextLimit) || nextLimit <= 0) nextLimit = 13.9;
    target = std::min(target, nextLimit * car.temper + 2);
    if (car.next->turn == TURN_LEFT) target = std::min(target, 8.0);
    if (car.next->turn == TURN_UTURN) target = std::min(target, 5.0);
  }

  // --- the car ahead (IDM) ---------------------------------------------------
  double gap = 1e9;
  double leadV = 0;
  for (const auto& other : cars) {
    if (&other == &car) continue;
    bool sameLane = other.lane == lane && !other.conn && !car.conn;
    bool sameConn = other.conn == car.conn && car.conn != nullptr;
    if (!sameLane && !sameConn) continue;
    double ds = other.s - car.s;
    if (ds > 0 && ds < gap) {
      gap = ds - (other.length + car.length) / 2;
      leadV = other.v;
    }
  }
  // the player's car counts as an obstacle when it is on our stretch
  if (player.length > 0) {
    double dx = player.x - car.x;
    double dz = player.z - car.z;
    double fx = std::sin(car.yaw), fz = std::cos(car.yaw);
    double ahead = dx * fx + dz * fz;
    double lateral = std::fabs(dx * fz - dz * fx);
    if (ahead > 0 && ahead < gap + 30 && lateral < 2.2) {
      gap = std::min(gap, ahead - (player.length + car.length) / 2);
      leadV = player.speed;
      if (car.v > leadV + 2 && gap < 25) {
        car.blockedByPlayer = std::max(car.blockedByPlayer, 0.5);
        events.push_back(ForcedBrakeEvent{&car, (car.v - leadV) / std::max(dt, 1e-3)});
      }
    }
  }

  // --- Intelligent Driver Model ---------------------------------------------
  const double T = 1.2;      // desired time headway (s)
  const double A_MAX = 2.5;  // max acceleration (m/s²)
  const double B_COMF = 2.0; // comfortable deceleration (m/s²)
  const double S0 = 2.0;     // standstill gap (m)
  double acc = A_MAX * (1 - std::pow(std::max(car.v, 0.0) / std::max(target, 0.1), 4));
  if (gap < 1e8) {
    double sStar = S0 + std::max(0.0, car.v * T + car.v * (car.v - leadV) /
                                                   (2 * std::sqrt(A_MAX * B_COMF)));
    acc -= A_MAX * std::pow(sStar / std::max(gap, 0.5), 2);
  }

  // --- right of way at the junction we are approaching -----------------------
  bool mustStop = false;
  double stopDist = 1e9;
  if (!car.conn && lane->armTo) {
    Node* node = lane->armTo->node;
    double dist = lane->stopAt - car.s;
    if (dist > -1 && dist < 60) {
      stopDist = dist;
      const Arm* arm = lane->armTo;
      bool red = false;
      if (arm->sg >= 0 && node->sc >= 0) {
        int st = signals_.get(node->sc, arm->sg);
        red = st == SIG_RED || st == SIG_RED_AMBER;
        if (st == SIG_AMBER && signals_.age(node->sc, arm->sg) > 1.3 && car.v < 14) red = true;
      } else if (arm->role == ROLE_STOP) {
        red = car.waited < 0.8;  // must stand still at the line
      } else if (arm->role == ROLE_YIELD) {
        red = !car.stoppedAtLine && car.v > 0.5;
      }
      if (red && dist < 40) mustStop = true;
      // the conflict relations decide who waits at an uncontrolled junction
      if (!mustStop && arm->role == ROLE_RBL && dist < 25) {
        for (const Connector* c : lane->out) {
          for (const Conflict& f : c->conflicts) {
            if (!f.yield) continue;
            for (const auto& other : cars) {
              if (other.conn != f.other) continue;
              if (other.s < f.sOther + 12) {
                mustStop = true;
                break;
              }
            }
            if (mustStop) break;
          }
          if (mustStop) break;
        }
      }
    }
  }

  // --- braking to the line ---------------------------------------------------
  if (mustStop && stopDist < 1e8) {
    double vMax = std::sqrt(std::max(0.0, 2 * B_COMF * std::max(stopDist - 1.5, 0.0)));
    if (car.v > vMax) acc = std::min(acc, -(car.v - vMax) / std::max(dt, 1e-3));
    if (stopDist < 1.5 && car.v < 0.4) car.stoppedAtLine = true;
    car.waited += dt;
  } else {
    car.waited = std::max(0.0, car.waited - dt);
    if (car.stoppedAtLine && car.v > 1) car.stoppedAtLine = false;
  }

  // --- integrate --------------------------------------------------------------
  acc = clamp(acc, -8.0, A_MAX);
  car.a = acc;
  car.v = std::max(0.0, car.v + acc * dt);
  car.braking = acc < -0.6;
  if (car.v < 0.05 && mustStop) car.v = 0;
  car.s += car.v * dt;

  // --- advance along the lane / through the junction --------------------------
  if (!car.conn) {
    if (car.s >= usableLength(*lane)) {
      // take the connector we picked (or any legal one)
      Connector* chosen = car.next;
      if (!chosen) {
        for (Connector* c : lane->out) {
          if (!lane->out.empty()) {
            chosen = c;
            break;
          }
        }
      }
      if (!chosen) {
        // no way on: turn round on the spot
        car.s = usableLength(*lane);
        car.v = 0;
      } else {
        car.conn = chosen;
        car.s = 0;
        car.lane = chosen->to;
        car.next = nullptr;
        // pick the next connector after the one we are taking
        for (Connector* c : chosen->to->out) {
          if (c != chosen) {
            car.next = c;
            break;
          }
        }
      }
    }
  } else {
    if (car.s >= car.conn->len) {
      Lane* to = car.conn->to;
      car.conn = nullptr;
      car.lane = to;
      car.s = 0;
      car.next = nullptr;
      for (Connector* c : to->out) {
        car.next = c;
        break;
      }
    }
  }

  // --- place the car in the world --------------------------------------------
  if (car.conn) {
    gTmp = samplePolyline(car.conn->pl, car.s, gTmp);
    car.x = gTmp.x;
    car.y = gTmp.y;
    car.z = gTmp.z;
    car.yaw = std::atan2(-gTmp.tx, -gTmp.tz);
    car.pitch = 0;
    // indicate the turn we are about to make / are making
    car.blinker = car.conn->turn == TURN_LEFT ? -1 : (car.conn->turn == TURN_RIGHT ? 1 : 0);
  } else if (car.lane) {
    PolySample s = lanePoint(*car.lane, car.s, gTmp);
    car.x = s.x;
    car.y = s.y;
    car.z = s.z;
    car.yaw = std::atan2(-s.tx, -s.tz);
    car.blinker = 0;
    if (car.s > car.lane->len - 12 && car.next)
      car.blinker = car.next->turn == TURN_LEFT ? -1 : (car.next->turn == TURN_RIGHT ? 1 : 0);
  }

  // --- stuck / crashed bookkeeping -------------------------------------------
  if (car.v < 0.2) {
    car.stuck += dt;
    if (car.stuck > 8 && car.waited < 0.1) {
      // nothing to wait for and no movement: give up on this car
      car.crashed += dt;
    }
  } else {
    car.stuck = std::max(0.0, car.stuck - dt * 2);
  }
  car.blockedByPlayer = std::max(0.0, car.blockedByPlayer - dt);
  car.wheelSpin = car.v * 0.3;
}

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

// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Ambient traffic.
 *
 * Cars travel on the lane graph: they follow the car ahead (Intelligent
 * Driver Model), keep to speed limits, slow for bends, obey traffic lights
 * and stop signs and give way exactly according to the right-of-way relations
 * of the lane graph – the same relations the rule monitor judges the player by.
 *
 * Origin: src/sim/traffic.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#pragma once

#include <string>
#include <variant>
#include <vector>

#include "core/geom.h"
#include "world/lanes.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

class SignalSystem;

/** One ambient vehicle. */
struct TrafficCar {
  int id = 0;
  Lane* lane = nullptr;
  Connector* conn = nullptr;
  /** distance of the car's centre along the current lane / connector */
  double s = 0;
  double v = 0, a = 0;
  /** next connector to take at the end of the current lane */
  Connector* next = nullptr;
  double length = 4.3, width = 1.8;
  uint32_t color = 0xd9dadc;
  int style = 0;
  /** desired speed factor relative to the limit */
  double temper = 1;
  double x = 0, y = 0, z = 0;
  double yaw = 0, pitch = 0;
  /** -1 left, 0 none, 1 right */
  int blinker = 0;
  bool braking = false;
  /** seconds spent waiting at the current stop/yield line */
  double waited = 0;
  /** seconds spent waiting for a gap (not counting red lights) */
  double yieldWait = 0;
  bool stoppedAtLine = false;
  double stuck = 0;
  /** set while the player's car is the reason for braking */
  double blockedByPlayer = 0;
  double crashed = 0;
  double wheelSpin = 0;
};

/** One pedestrian at a crossing. */
struct Pedestrian {
  int id = 0;
  CrossingJson crossing;
  /** position across the road: -1 kerb A … +1 kerb B */
  double t = 0;
  int dir = 1;
  std::string state = "wait";  // 'wait' | 'cross' | 'gone'
  double waited = 0;
  double x = 0, y = 0, z = 0;
  double yaw = 0;
  double phase = 0;
  double speed = 1.4;
  /** half length of the walk (m): road half width + sidewalk */
  double half = 3;
  uint32_t colour = 0xffffff;
};

/** What the traffic needs to know about the player's car. */
struct PlayerState {
  double x = 0, y = 0, z = 0;
  double yaw = 0;
  double speed = 0;
  double length = 4.3, width = 1.8;
};

/** Events the traffic reports back to the rule monitor. */
struct ForcedBrakeEvent {
  TrafficCar* car = nullptr;
  double decel = 0;
};
struct CollisionEvent {
  TrafficCar* car = nullptr;
  double speed = 0;
};
struct PedestrianEndangeredEvent {
  Pedestrian* ped = nullptr;
};
using TrafficEvent = std::variant<ForcedBrakeEvent, CollisionEvent, PedestrianEndangeredEvent>;

class Traffic {
 public:
  Traffic(World& world, LaneGraph& graph, SignalSystem& signals);

  std::vector<TrafficCar> cars;
  std::vector<Pedestrian> peds;

  void clear();

  /** Advance the simulation by `dt` seconds.  `events` collects what the
   *  rule monitor needs to know (collisions, forced brakes, …). */
  void update(double dt, const PlayerState& player, std::vector<TrafficEvent>& events);

  /** The pedestrian walking on `c`, or nullptr. */
  Pedestrian* pedestrianAt(const CrossingJson& c);

  /** density 0 … 1: how many cars to keep in the world */
  double density = 0.6;
  /** how many cars the world should carry (set from the traffic setting) */
  int targetCount = -1;

 private:
  World& world_;
  LaneGraph& graph_;
  SignalSystem& signals_;
  Rng rng_{0x5eed};
  double spawnTimer_ = 0;

  void spawnCar();
  void advanceCar(TrafficCar& car, double dt, const PlayerState& player,
                  std::vector<TrafficEvent>& events);
  void updatePeds(double dt, const PlayerState& player, std::vector<TrafficEvent>& events);
};

}  // namespace fs

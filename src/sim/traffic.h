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

#include <functional>
#include <map>
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
  /**
   * How the player's car came to be in this one's way: the time gap (s) at
   * which it was first seen there (INFINITY while it is not), and for how
   * long that is remembered once it is out of the way again.
   */
  double playerFirstGap = INFINITY;
  double playerSeen = 0;
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

/**
 * A vehicle that drives itself without being one of the traffic's cars (the
 * player's car on a demonstration drive): it sees what the others see.
 */
struct SelfDriving {
  /** the lane it is on, and the connector it is on while crossing a junction (or null) */
  Lane* lane = nullptr;
  Connector* conn = nullptr;
  /** distance of its middle along that lane or connector */
  double s = 0;
  double v = 0;
  /** the connector it will take at the end of its lane */
  Connector* next = nullptr;
  /** the connectors of its way beyond that, if it knows where it is going */
  std::vector<Connector>* route = nullptr;
  double length = 4.3, width = 1.8;
  double x = 0, y = 0, z = 0;
  double yaw = 0;
};

/** The nearest vehicle ahead on a path: gap in metres, its speed along the path. */
struct Lead {
  double gap = INFINITY, v = 0;
  /** found by where it is, not by the order of the cars on the lanes */
  bool loose = false;
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

  /**
   * The nearest vehicle ahead on the path of `me` – a car of the traffic, or
   * a tram or train in its way: the gap to it (m) and its speed along the
   * path; gap = INFINITY if there is none within `horizon`.
   */
  Lead aheadOf(const SelfDriving& me, double horizon);
  /**
   * For a vehicle that is about to cross the way of others: how far along its
   * way (m, from its middle) the nearest place lies where a car that is under
   * way now will cross at about the time it gets there itself, or INFINITY.
   */
  double crossingAhead(const SelfDriving& me, double reach);
  /**
   * May `me` drive into the junction through `c` – is the way free of traffic
   * it has to give way to, and of cars standing or crossing in it?
   * `impatient`: after a long wait a tighter (still safe) gap is taken.
   */
  bool mayPass(const SelfDriving& me, Connector* c, double distToLine, bool impatient = false);
  /**
   * Is there room on `lane` beside and behind a vehicle whose middle is `s`
   * metres along it (the lanes of a carriageway are measured alike) and that
   * drives at `v` – may it move over without making anybody brake?
   */
  bool roomOn(const Lane& lane, double s, double v, double length);
  /** Is nobody inside the junction on a path that meets `c`? */
  bool clearOf(const Connector* c);

  /** Other things on the road the cars must not run into (trams, trains). */
  std::vector<PlayerState> obstacles;
  /** how far from the player cars are spawned / removed */
  double spawnRadius = 330;

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
  int nextId_ = 0;
  /** where the next spawned car comes from: lane cells around the player */
  std::map<std::pair<int, int>, std::vector<Lane*>> spawnGrid_;
  /** crossings by edge for quick lookup */
  std::map<int, std::vector<CrossingJson>> crossingsByEdge_;
  /** the cars on each lane / connector, kept up to date while driving */
  std::map<int, std::vector<TrafficCar*>> onLane_;
  std::map<int, std::vector<TrafficCar*>> onConn_;
  /** what `aheadOf` asks of a vehicle that is not one of the cars */
  TrafficCar probe_;
  PlayerState box_;
  PlayerState nobody_;
  std::vector<double> way_;

  static std::pair<int, int> cellKey(double x, double z);
  void spawnCar(const PlayerState& player);
  void attach(TrafficCar& car);
  void detach(TrafficCar& car);
  Connector* chooseNext(Lane& lane);
  void advanceCar(TrafficCar& car, double dt, const PlayerState& player,
                  std::vector<TrafficEvent>& events);
  void updatePeds(double dt, const PlayerState& player, std::vector<TrafficEvent>& events);

  // --- what the self-driving vehicles ask (0.7.0) ---
  TrafficCar* aim(const SelfDriving& me);
  Lead leader(TrafficCar& car, double horizon);
  double playerAhead(TrafficCar& car, const PlayerState& player, double horizon,
                     const std::vector<Connector>* route = nullptr);
  void walk(Lane* lane, Connector* conn, double s, Connector* next,
            const std::vector<Connector>* route, double step, double reach,
            const std::function<bool(double, double, double, double, double)>& visit);
  bool mayEnter(TrafficCar& car, Connector* c, double distToLine, const PlayerState& player,
                bool impatient = false, double caution = 0);
  double playerOnPath(const PlayerState& player, const Connector* c);
  bool boxBlocked(const Connector* c);
  bool junctionEmpty(const Connector* c);
};

}  // namespace fs

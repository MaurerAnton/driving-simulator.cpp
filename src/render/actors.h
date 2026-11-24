// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Moving things in the scene: traffic cars, pedestrians, cyclists and traffic-light heads.
 *
 * Origin: src/render/actors.ts @ v1.7.7
 */
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/engine.h"
#include "render/car.h"
#include "render/fleet.h"
#include "render/people.h"
#include "sim/hooks.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "world/world.h"

namespace fs {

// ------------------------------------------------------------------ hooks ----
//
// The cyclists (render/cyclist.ts) and the emergency
// vehicles (render/emergency.ts) are not translated yet.  (The city bus is render/fleet.h's PlayerBus.)  Each stands here as a class of the original's name with just the members
// the traffic's drawing calls, for the missing part to fill in; `create` is its constructor
// (`new CyclistModel(colour)` …) and null until then – what it would draw is left out
// meanwhile.  When a part is translated its declaration here goes and the real class takes its
// place: nothing in actors.cpp changes but the `create` calls.

/** render/cyclist.ts */
class CyclistModel {
 public:
  static inline std::function<std::unique_ptr<CyclistModel>(int colour)> create = nullptr;

  virtual ~CyclistModel() = default;
  const std::shared_ptr<Group> group = make<Group>();
  virtual void setColour(int colour) = 0;
  virtual void pose(double phase, double roll, bool down) = 0;
};

/** render/emergency.ts */
class EmergencyFleet {
 public:
  static inline std::function<std::unique_ptr<EmergencyFleet>()> create = nullptr;

  virtual ~EmergencyFleet() = default;
  const std::shared_ptr<Group> group = make<Group>();
  virtual void update(const std::vector<TrafficCar*>& cars, double time) = 0;
};

// ---------------------------------------------------------------- traffic ----

class TrafficRenderer {
 public:
  const std::shared_ptr<Group> group = make<Group>();
  /** the people on foot (sim/pedestrians.ts), drawn like those at the stops */
  const std::unique_ptr<PeopleRenderer> people;
  /** the camera: those far off, or far behind it, are left out (null: everybody is drawn) */
  Camera* view = nullptr;
  /** is a car's main beam lit (at night on an empty road, or flashing: game/beamplay.ts)?  null: never */
  std::function<bool(int64_t id)> highOf = nullptr;

  TrafficRenderer();
  TrafficRenderer(const TrafficRenderer&) = delete;
  TrafficRenderer& operator=(const TrafficRenderer&) = delete;

  void update(const std::vector<TrafficCar*>& cars, const std::vector<Pedestrian*>& peds, double time, const std::vector<Cyclist*>& cyclists = {});

 private:
  /** `Map<number, T>`: the models by the id of what they show, gone through in the order they came (as JavaScript's) */
  template <class T>
  struct ById {
    std::vector<std::pair<int64_t, std::unique_ptr<T>>> list;
    std::unordered_map<int64_t, T*> at;
    T* get(int64_t id) const {
      const auto it = at.find(id);
      return it == at.end() ? nullptr : it->second;
    }
    T* set(int64_t id, std::unique_ptr<T> model) {
      T* m = model.get();
      at[id] = m;
      list.emplace_back(id, std::move(model));
      return m;
    }
  };
  ById<CarExterior> cars_;
  std::vector<std::unique_ptr<CarExterior>> pool_[3];
  /** (the original keeps the poses in a pool of objects and hands on a list of them: here the list holds them, and keeps its room) */
  std::vector<PersonPose> shown_;
  Vector3 look_;
  ById<CyclistModel> cyclists_;
  std::vector<std::unique_ptr<CyclistModel>> cyclistPool_;
  /** the city buses of the traffic (the bus one learns with, seen from outside) */
  ById<PlayerBus> buses_;
  std::vector<std::unique_ptr<PlayerBus>> busPool_;
  /** the emergency vehicles – police, ambulance, fire engine, the doctor's car (render/emergency.ts) */
  const std::unique_ptr<EmergencyFleet> emergency_;
  CarVisualState busState_;

  /**
   * The people on foot (as the traffic has them, or as a replay recorded
   * them: the look number as their colour, metres walked as the step – −1
   * standing): drawn like those at the stops.  Those more than 220 m off are
   * left out, and those further than 45 m behind the camera (a mirror may
   * show the nearer ones).
   */
  void drawPeople(const std::vector<Pedestrian*>& peds, double time);
};

// ---------------------------------------------------------------- signals ----

/** where a head stands, which way it faces and which light it shows (the timers over them: ui/signaltimers.ts) */
struct HeadSpot {
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  int controller = 0;
  int group = 0;
  /** the green arrow that hangs beside it (null: none) */
  const Arm* arrow = nullptr;
};

/** All traffic-light heads of the map (a few hundred instances at most). */
class SignalRenderer {
 public:
  const std::shared_ptr<Group> group = make<Group>();
  /** positions of the poles for the collision system: x, y, z, radius */
  std::vector<double> poles;
  /** where each head stands, which way it faces and which light it shows (the timers over them: ui/signaltimers.ts) */
  std::vector<HeadSpot> spots;

  explicit SignalRenderer(const World& world, const SignalSystem* signals = nullptr);
  SignalRenderer(const SignalRenderer&) = delete;
  SignalRenderer& operator=(const SignalRenderer&) = delete;

  /**
   * Set the heads up anew: for the signals of the roads that are loaded now
   * (the network comes in pieces).  With `signals`, the green arrows it
   * knows of are hung beside the heads of their approaches.
   */
  void rebuild(const World& world, const SignalSystem* signals = nullptr);

  void update(const SignalSystem& signals);

 private:
  struct Head {
    int controller = 0;
    int group = 0;
    /** index of the first of the three lamp instances */
    int lamp = 0;
    int state = 0;
  };
  /** The green arrow beside a head: an amber and a green arrow lamp. */
  struct ArrowHead {
    const Arm* arm = nullptr;
    /** index of the first of its two lamp instances */
    int lamp = 0;
    int state = 0;
  };
  std::vector<Head> heads_;
  std::shared_ptr<InstancedMesh> lamps_;
  std::shared_ptr<InstancedMesh> housing_;
  /** the green arrows beside some of the heads */
  std::vector<ArrowHead> arrowHeads_;
  std::shared_ptr<InstancedMesh> arrowLamps_;
  std::shared_ptr<InstancedMesh> arrowHousing_;
};

}  // namespace fs

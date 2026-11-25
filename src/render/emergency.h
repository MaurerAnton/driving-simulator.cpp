// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/emergency.ts @ v1.7.7
/**
 * The emergency vehicles as they are seen: the police patrol car, the
 * ambulance (a Rettungswagen: a van with a box body), the fire engine (a
 * rescue pumper, HLF) and the emergency doctor's car (NEF) – each in the
 * livery such vehicles wear in Germany, with the blue lights that flash while
 * it is on a call: double flashes, left and right in turn, the headlights
 * flashing in turn with them.  The police car also has the "STOP POLIZEI"
 * sign that rises from its roof to stop the traffic (a road closure shows it).
 * Simple shapes and pictures drawn on a canvas; no real vehicle and no maker
 * is reproduced.
 *
 * Each is built around the point the traffic places it by – the ground below
 * the middle of its length –, x to the right, y up, the front towards −z.
 * EmergencyFleet draws those of the traffic (src/sim/emergency.ts): which one
 * a car is, and whether its lights are on, it reads from the car's style.
 */
#pragma once

#include <array>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine/engine.h"

namespace fs {

class CarExterior;
struct TrafficCar;

/**
 * The flashing lamps of one vehicle.  The blue lamps belong to one of two
 * groups that give double flashes in turn (the left and the right half of a
 * light bar, …); the headlights flash in turn, left and right.  Lit, a lamp
 * shines brightly and has a glow round it that is seen from afar.
 */
class Flashers {
 public:
  Flashers();
  /** A blue lamp from x0…x1, y0…y1, z0…z1, flashing with group `k`, with a glow `size` metres across. */
  void lamp(Object3D& parent, double x0, double x1, double y0, double y1, double z0, double z1, int k, double size);
  /** What is shown over a headlight while it flashes (k: 0 the left one, 1 the right one). */
  void head(Object3D& parent, const ObjectPtr& flash, int k);
  void update(bool on, double t);

 private:
  // (bright as they are: not toned down with the rest of the picture)
  std::array<MaterialPtr, 2> lens;
  std::array<MaterialPtr, 2> glowMat;
  std::array<std::vector<ObjectPtr>, 2> glows;
  std::array<std::vector<ObjectPtr>, 2> heads;
  /** what is lit now (bits), to change the materials only when that changes */
  int state = -1;
};

/** One emergency vehicle as it is seen. */
class EmergencyModel {
 public:
  /** placed where the vehicle is: the ground below the middle of its length */
  const std::shared_ptr<Group> group = make<Group>();
  /** EM */
  const int kind;

  explicit EmergencyModel(int kind);
  ~EmergencyModel();
  EmergencyModel(const EmergencyModel&) = delete;
  EmergencyModel& operator=(const EmergencyModel&) = delete;

  /** The blue lights – and the headlights flashing in turn – on or off; `time` (s) sets where they are in their rhythm. */
  void setLights(bool on, double time);
  /** The police car's "STOP POLIZEI" sign raised on the roof, or folded away. */
  void setSign(bool on);
  /** The wheels turned by `spin` (rolled spin × 0.31 m, as the traffic counts it), the brake lamps, the indicators as they blink now. */
  void setMotion(double spin, bool braking, bool left, bool right, double steer = 0);

 private:
  Flashers flash;
  /** the car body of the police car and the doctor's car (the others are built here of boxes) */
  std::unique_ptr<CarExterior> car;
  std::vector<ObjectPtr> wheels;
  double wheelRadius = 0.33;
  std::vector<std::shared_ptr<Mesh>> brakes;
  struct Blinker {
    std::shared_ptr<Mesh> mesh;
    int side = 0;
  };
  std::vector<Blinker> blinkers;
  /** the police's "STOP POLIZEI" sign on the roof (raised while shown) */
  ObjectPtr sign;

  void buildCar(bool doctor);
  void buildAmbulance();
  void buildFireEngine();
  void addWheels(Group& g, double r, double wb, double track, double front, double rear);
};

/** A police patrol car as seen – for whatever else shows one (a road closure: lights on, the STOP sign raised). */
std::shared_ptr<EmergencyModel> buildPoliceCar();

/** The emergency vehicles among the traffic: each drawn where it is, its lights as its style says. */
class EmergencyFleet {
 public:
  const std::shared_ptr<Group> group = make<Group>();

  EmergencyFleet();
  EmergencyFleet(const EmergencyFleet&) = delete;
  EmergencyFleet& operator=(const EmergencyFleet&) = delete;

  void update(const std::vector<TrafficCar*>& cars, double time);

 private:
  /** (gone through in the order they came, as JavaScript's Map) */
  std::vector<std::pair<int64_t, std::unique_ptr<EmergencyModel>>> shown;
  std::array<std::vector<std::unique_ptr<EmergencyModel>>, 4> pool;
  std::unordered_set<int64_t> seen;

  void release(size_t i);
};

}  // namespace fs

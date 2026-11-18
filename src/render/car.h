// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The cars you see: the exterior (body, wheels, lamps, plates) and, for the
 * player's car, the cockpit (dashboard, instruments, steering wheel, gear
 * lever, handbrake, pedals, seats) plus the three mirrors.
 *
 * Car frame: origin on the ground below the middle of the wheelbase,
 * +X right, +Y up, -Z forward.  Left-hand drive.
 *
 * Origin: src/render/car.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/scene.h"
#include "render/carBody.h"

namespace fs {

/** Everything the renderer needs to draw one frame of the car. */
struct CarVisualState {
  double speedKmh = 0;
  double rpm = 0;
  int gear = 0;
  /** -1 left, 0 off, 1 right (current lamp phase is in blinkOn) */
  int indicator = 0;
  bool hazard = false;
  bool blinkOn = false;
  bool lowBeam = false;
  bool highBeam = false;
  bool handbrake = true;
  bool ignition = true;
  bool engineRunning = false;
  bool abs = false;
  bool seatbelt = true;
  double fuel = 0.82;
  bool automatic = false;
  /** show the engaged gear (driver aid; a real manual car has no such display) */
  bool showGear = true;
  double odometerKm = 0;
  /** steering wheel angle in rad, + = clockwise / right */
  double steerWheel = 0;
  /** road wheel steer angles (rad, + = right) and spin angles (rad) FL, FR, RL, RR */
  double wheelSteer[4] = {0, 0, 0, 0};
  double wheelSpin[4] = {0, 0, 0, 0};
  /** wheel centre height above the car origin plane, per wheel */
  double wheelY[4] = {0, 0, 0, 0};
  double clutch = 0, brake = 0, throttle = 0, handbrakeLever = 1;
  bool braking = false;
  bool reversing = false;
};

/** One of the cars the traffic draws (no cockpit, no mirrors). */
class CarExterior {
 public:
  CarExterior(uint32_t color, const CarDims& dims = COMPACT_DIMS, const std::string& plate = "");

  Group group;
  CarDims dims;
  CarShell shell;
  /** the four wheel pivots (steer), each holding a spinner (spin) */
  std::vector<Group*> wheels;
  std::vector<Group*> wheelSpinners;

  void setWheels(const double steer[4], const double spin[4], const double y[4]);
  void setLamps(bool low, bool high, bool brake, bool reverse, bool left, bool right);
};

/** The driving-school car with its interior and mirrors. */
class PlayerCar {
 public:
  PlayerCar();

  Group root;
  std::unique_ptr<CarExterior> exterior;
  Group cockpit;
  /** driver's eye point in car coordinates (seat position) */
  Vector3 eye{-0.36, 1.2, 0.2};

  void update(const CarVisualState& s, double dt, double now);
};

}  // namespace fs

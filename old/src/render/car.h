// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The cars you see: the exterior (body, wheels, lamps, plates, lettering,
 * roof sign) and, for the player's car, the cockpit (dashboard, instrument
 * hood, console, doors, seats, steering wheel, gear lever, handbrake, pedals,
 * stalks) plus the three mirrors that work.
 *
 * Car frame: origin on the ground below the middle of the wheelbase,
 * +X right, +Y up, -Z forward.  Left-hand drive.
 *
 * Origin: src/render/car.ts (MaurerAnton/driving-simulator @ ff37c5d, v0.7.0).
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/scene.h"
#include "render/carBody.h"
#include "render/instruments.h"
#include "render/mirrors.h"

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

/** What a caller may ask of the exterior (the original's CarExteriorOptions). */
struct CarExteriorOptions {
  const CarDims* dims = nullptr;
  std::string plate;
  /** see-through cabin glass (the driving-school car), instead of the dark glass */
  bool seeThroughGlass = false;
  /** the roof sign of a driving-school car */
  bool roofSign = false;
  /** the player's car builds its own mirror housings, aligned with the working glass */
  bool noMirrorHousings = false;
  /** lettering on the front and the rear bumper (the traffic cars carry none) */
  std::string lettering;
  /** own copies of the materials all cars share, for a car that changes them (see-through cabin) */
  bool ownMaterials = false;
};

/** Body, wheels and lamps – everything a car shows from outside. */
class CarExterior {
 public:
  CarExterior(uint32_t color, const CarDims& dims = COMPACT_DIMS, const std::string& plate = "");
  CarExterior(uint32_t color, const CarExteriorOptions& opts);

  Group group;
  CarDims dims;
  CarShell shell;
  /** the four wheel pivots (steer), each holding a spinner (spin) */
  std::vector<Group*> wheels;
  std::vector<Group*> wheelSpinners;
  /** the opaque body and what is fixed to it (not wheels, glass, plates, lettering) */
  std::vector<Mesh*> solids;
  /** the lamps, with what they are: 0 head, 1 tail, 2 reverse, 3 blinkL, 4 blinkR */
  struct Lamp {
    Mesh* mesh = nullptr;
    int which = 0;
  };
  std::vector<Lamp> lamps;
  /** no lamp is lit (the colours are only rebuilt when the state changes) */
  bool lampOff_ = false;

  void setWheels(const double steer[4], const double spin[4], const double y[4]);
  void setLamps(bool low, bool high, bool brake, bool reverse, bool left, bool right);

 private:
  void addLettering(const std::string& text, uint32_t paint);
};

/**
 * The driving-school car with its interior, its instruments and its three
 * working mirrors.
 */
class PlayerCar {
 public:
  PlayerCar();

  Group root;
  std::unique_ptr<CarExterior> exterior;
  Group cockpit;
  /** driver's eye point in car coordinates (seat position) */
  Vector3 eye{-0.36, 1.2, 0.2};
  /** the three mirrors: inside, left, right (the game renders them) */
  std::vector<std::unique_ptr<Mirror>> mirrors;

  /**
   * How solid the cabin looks from the driver's seat: 1 = as built, smaller
   * values let the road show through dashboard, pillars, doors, roof, bonnet
   * and steering wheel (0.2 = strongly see-through).  Instruments, displays
   * and mirrors stay solid.  Seen from outside the car is always solid.
   */
  void setCabinOpacity(double opacity);

  void update(const CarVisualState& s, double dt, double now);

 private:
  /** Register a part of the see-through cabin (its twin writes depth only). */
  void ghost(Mesh* mesh, int layer);

  InstrumentPanel panel_;
  Group steering_;
  Group lever_;
  Group handbrake_;
  std::vector<Group*> pedals_;
  Group stalk_;
  double leverX_ = 0, leverZ_ = 0, gate_ = 0;
  /** the cabin parts that turn see-through (see setCabinOpacity) */
  struct Ghost {
    Mesh* mesh = nullptr;
    Mesh* twin = nullptr;
    int layer = 0;
  };
  std::vector<Ghost> ghosts_;
  std::vector<Mesh*> ghostMaterials_;
  double cabinOpacity_ = 1;
};

}  // namespace fs

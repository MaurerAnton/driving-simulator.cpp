// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/vehicle/types.ts @ v1.7.7
/**
 * Public types of the vehicle dynamics model.
 *
 * Conventions
 *  - World: x = east, y = up, z = south (north is -Z), metres, right-handed.
 *  - Body:  +X = right, +Y = up, +Z = backward (forward is -Z); origin at the
 *    centre of mass.  The quaternion [x,y,z,w] maps body → world.
 *  - yaw: rotation about +Y; 0 faces north, positive turns LEFT.
 *  - steer input: +1 = steering wheel fully to the right.
 */
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace fs {

struct GroundSample {
  /** surface height */
  double y = 0;
  /** unit surface normal */
  double nx = 0;
  double ny = 1;
  double nz = 0;
  /** friction multiplier: 1 dry asphalt … 0.4 wet grass */
  double grip = 1;
  /** rolling-resistance multiplier: 1 asphalt … 4 grass */
  double rolling = 1;
};

class Ground {
 public:
  virtual ~Ground() = default;
  /**
   * Surface under world (x, z).  `yRef` is the height of the querying wheel,
   * which lets the world choose between a bridge deck and the road below.
   */
  virtual void sample(double x, double z, double yRef, GroundSample& out) = 0;
};

struct DriverInput {
  /** -1 … +1, desired steering-wheel position (+ = right) */
  double steer = 0;
  double throttle = 0;
  double brake = 0;
  /** clutch pedal, 1 = fully pressed (disengaged) */
  double clutch = 0;
  double handbrake = 0;
  /** manual: -1 R, 0 N, 1…6.  automatic: -2 P, -1 R, 0 N, 1 D */
  int gear = 0;
  bool ignition = true;
  bool starter = false;
};

struct WheelState {
  /** road-wheel steering angle (rad, + = right) */
  double steer = 0;
  /** accumulated rotation for rendering (rad) */
  double spin = 0;
  /** angular speed (rad/s, + = rolling forward) */
  double omega = 0;
  /** suspension compression from full droop (m) */
  double compression = 0;
  bool contact = false;
  /** normal load (N) */
  double load = 0;
  double slipRatio = 0;
  double slipAngle = 0;
  /** 0 … 1: how far the tyre is beyond its grip limit */
  double skid = 0;
  /** world position of the wheel centre */
  double hx = 0;
  double hy = 0;
  double hz = 0;
  /** grip multiplier of the surface under the wheel */
  double grip = 1;
};

struct VehicleSpec {
  std::string name;
  /** total mass incl. driver (kg) */
  double mass = 0;
  /** principal inertias about body X (pitch), Y (yaw), Z (roll) in kg·m² */
  std::array<double, 3> inertia{0, 0, 0};
  double wheelbase = 0;
  double track = 0;
  /** centre of mass: height above ground at rest, distance behind the front axle */
  double cgHeight = 0;
  double cgToFront = 0;
  double wheelRadius = 0;
  double wheelInertia = 0;

  // suspension, per wheel: [front, rear]
  std::array<double, 2> springRate{0, 0};
  std::array<double, 2> damperBump{0, 0};
  std::array<double, 2> damperRebound{0, 0};
  std::array<double, 2> antiRoll{0, 0};
  /** total wheel travel from full droop to the bump stop (m) */
  double travel = 0;

  // tyres
  double muPeak = 0;
  /** slip ratio and slip angle (rad) at peak grip on dry asphalt */
  double slipRatioPeak = 0;
  double slipAnglePeak = 0;
  double loadSensitivity = 0;
  double relaxLong = 0;
  double relaxLat = 0;
  double rollingResistance = 0;

  // engine
  /** [rpm, N·m] full-throttle torque curve */
  std::vector<std::array<double, 2>> torqueCurve;
  double idleRpm = 0;
  double limiterRpm = 0;
  double stallRpm = 0;
  double engineInertia = 0;
  /** friction torque = a + b·rpm (N·m) */
  std::array<double, 2> engineFriction{0, 0};

  // driveline
  /** 'manual' | 'automatic' */
  std::string transmission = "manual";
  /** forward ratios, index 0 = 1st gear */
  std::vector<double> gearRatios;
  double reverseRatio = 0;
  double finalDrive = 0;
  double clutchTorque = 0;
  /** share of the engine torque that reaches the wheels (gearbox + differential losses) */
  double drivelineEfficiency = 0;
  /** operate the clutch for the driver (manual gearbox) */
  bool autoClutch = false;

  // brakes
  /** total brake torque at full pedal (N·m, all four wheels) */
  double brakeTorque = 0;
  double brakeBiasFront = 0;
  double handbrakeTorque = 0;
  bool abs = false;

  // steering
  double steeringRatio = 0;
  double maxWheelAngle = 0;

  // aerodynamics: drag area Cd·A (m²)
  double dragArea = 0;

  // ---- what differs for a truck (a car leaves these out) ----
  /** the rear wheels are driven (default: the front wheels) */
  bool rearDrive = false;
  /** outer length and width of the vehicle (m); default: a compact car */
  std::optional<double> length = std::nullopt;
  std::optional<double> width = std::nullopt;
  /**
   * Corners of the body shell relative to the point on the ground below the
   * middle of the wheelbase (x right, y up, z back) – what touches the ground
   * when the vehicle lies on its side or roof.  Default: a compact car.
   */
  std::optional<std::vector<std::array<double, 3>>> bodyPoints = std::nullopt;
  /**
   * The automatic gearbox: shift points as engine speed (rpm) at no and at
   * full throttle, up to which speed a kick-down shifts back, from which
   * speed of the gearbox input (rad/s) the converter locks up, and the
   * converter's capacity (N·m per (rad/s)²).  Default: a petrol car.
   * (The original's field is `auto`, a word C++ keeps for itself.)
   */
  struct Auto {
    std::array<double, 2> up{0, 0};
    std::array<double, 2> down{0, 0};
    double kickdownRpm = 0;
    double lockOmega = 0;
    double converter = 0;
  };
  std::optional<Auto> auto_ = std::nullopt;
  /**
   * The bump stop at the end of the travel, per wheel: its stiffness (N/m) and
   * its damping (N·s/m, acting both ways while it is pressed).  Default: a
   * car's – hard enough for 1.4 t, far too hard for a light machine, which it
   * would throw into the air off a kerb.
   */
  struct BumpStop {
    double stiffness = 0;
    double damping = 0;
  };
  std::optional<BumpStop> bumpStop = std::nullopt;
};

}  // namespace fs

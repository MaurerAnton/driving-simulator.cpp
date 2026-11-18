// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Vehicle model types: the driver's inputs, the ground the tyres stand on,
 * the vehicle specification and the per-wheel state.
 *
 * Origin: src/sim/vehicle/types.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).
 */
#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace fs {

/** What the driver asks the car to do. */
struct DriverInput {
  double steer = 0;      // -1 … 1 (steering wheel position)
  double throttle = 0;   // 0 … 1
  double brake = 0;      // 0 … 1
  double clutch = 0;     // 0 … 1 (pedal travel)
  double handbrake = 0;  // 0 … 1
  /** manual: -1 R, 0 N, 1…6; automatic: -2 P, -1 R, 0 N, 1 D */
  int gear = 0;
  bool ignition = true;
  bool starter = false;
};

/** What the ground under one wheel looks like. */
struct GroundSample {
  bool hit = false;
  double y = 0;
  double nx = 0, ny = 1, nz = 0;
  double grip = 1;
  double rolling = 1;
};

/** The terrain the vehicle queries (implemented by WorldGround). */
class Ground {
 public:
  virtual ~Ground() = default;
  /**
   * Sample the ground at (x, z).  `yRef` picks the right deck where roads
   * cross at different levels; the result is written into `out`.
   */
  virtual void sample(double x, double z, double yRef, GroundSample& out) = 0;
  /** Surface roughness for the audio (1 asphalt … 3 gravel). */
  virtual double roughnessAt(double x, double z) {
    (void)x;
    (void)z;
    return 1;
  }
};

/** Per-wheel state, refreshed every step. */
struct WheelState {
  double steer = 0;
  double spin = 0;         // radians rolled
  double omega = 0;        // rad/s
  double compression = 0;  // m
  bool contact = false;
  double load = 0;         // N
  double slipRatio = 0;
  double slipAngle = 0;
  double skid = 0;         // 0 … 1
  double hx = 0, hy = 0, hz = 0;  // contact point (world)
  double grip = 1;
};

/** The vehicle's physical constants. */
struct VehicleSpec {
  std::string name;
  double mass = 1375;            // kg
  /** yaw, pitch, roll (kg m²) */
  std::array<double, 3> inertia{1900, 2000, 500};
  double wheelBase = 2.63;       // m
  double track = 1.54;           // m
  double cgHeight = 0.54;        // m
  /** distance from the centre of gravity to the front axle (m) */
  double cgToFront = 1.052;
  double wheelRadius = 0.316;    // m
  double wheelInertia = 1.1;     // kg m²

  // ride frequencies ≈ 1.35 Hz front / 1.5 Hz rear, damping ratio ≈ 0.33
  std::array<double, 2> springRate{29600, 24400};
  std::array<double, 2> damperBump{1700, 1250};
  std::array<double, 2> damperRebound{3000, 2250};
  std::array<double, 2> antiRoll{14000, 7000};
  double travel = 0.24;          // m of suspension travel

  double muPeak = 1.05;
  double slipRatioPeak = 0.11;
  double slipAnglePeak = 0.13;
  double loadSensitivity = 0.1;
  double relaxLong = 0.15;       // m
  double relaxLat = 0.32;        // m
  double rollingResistance = 0.011;

  std::vector<std::pair<double, double>> torqueCurve;  // [rpm, Nm]
  double idleRpm = 800;
  double limiterRpm = 6500;
  double stallRpm = 400;
  double engineInertia = 0.16;   // kg m²
  /** engine friction: [constant Nm, per-rpm Nm/rpm] */
  std::array<double, 2> engineFriction{14, 0.0085};

  std::string transmission = "manual";  // 'manual' | 'automatic'
  std::array<double, 6> gearRatios{3.77, 2.09, 1.32, 0.98, 0.78, 0.64};
  double reverseRatio = 3.6;
  double finalDrive = 3.65;
  double clutchTorque = 420;     // Nm
  double drivelineEfficiency = 0.92;
  bool autoClutch = false;

  double brakeTorque = 5200;     // Nm, total
  double brakeBiasFront = 0.68;
  double handbrakeTorque = 2800; // Nm
  bool abs = true;

  double steeringRatio = 14.5;
  double maxWheelAngle = 0.576;  // rad at the road wheel

  double dragArea = 0.65;        // Cd·A
};

/** One of the built-in vehicles (COMPACT_CAR, …), see spec.h. */
const VehicleSpec& compactCar();
const VehicleSpec& compactCarAutoclutch();
const VehicleSpec& compactCarAutomatic();

}  // namespace fs

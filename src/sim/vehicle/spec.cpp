// SPDX-License-Identifier: AGPL-3.0/null
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The built-in vehicle definitions.
 *
 * Origin: src/sim/vehicle/spec.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).  The numbers are carried over exactly.
 */
#include "sim/vehicle/spec.h"

namespace fs {

namespace {
VehicleSpec makeCompact() {
  VehicleSpec s;
  s.name = "Kompaktwagen 1.5 Benzin, 6-Gang";
  s.mass = 1375;
  s.inertia = {1900, 2000, 500};
  s.wheelBase = 2.63;
  s.track = 1.54;
  s.cgHeight = 0.54;
  s.cgToFront = 1.052;  // 60 % of the weight on the front axle
  s.wheelRadius = 0.316;
  s.wheelInertia = 1.1;

  // ride frequencies ≈ 1.35 Hz front / 1.5 Hz rear, damping ratio ≈ 0.33
  s.springRate = {29600, 24400};
  s.damperBump = {1700, 1250};
  s.damperRebound = {3000, 2250};
  s.antiRoll = {14000, 7000};
  s.travel = 0.24;

  s.muPeak = 1.05;
  s.slipRatioPeak = 0.11;
  s.slipAnglePeak = 0.13;
  s.loadSensitivity = 0.1;
  s.relaxLong = 0.15;
  s.relaxLat = 0.32;
  s.rollingResistance = 0.011;

  s.torqueCurve = {{0, 60},   {500, 110},  {800, 145},  {1000, 172}, {1300, 216},
                   {1500, 250}, {3500, 250}, {4500, 236}, {5000, 216}, {5500, 195},
                   {6000, 176}, {6500, 150}, {7000, 90},  {7500, 0}};
  s.idleRpm = 800;
  s.limiterRpm = 6500;
  s.stallRpm = 400;
  s.engineInertia = 0.16;
  s.engineFriction = {14, 0.0085};

  s.transmission = "manual";
  s.gearRatios = {3.77, 2.09, 1.32, 0.98, 0.78, 0.64};
  s.reverseRatio = 3.6;
  s.finalDrive = 3.65;
  s.clutchTorque = 420;
  s.drivelineEfficiency = 0.92;
  s.autoClutch = false;

  s.brakeTorque = 5200;
  s.brakeBiasFront = 0.68;
  s.handbrakeTorque = 2800;
  s.abs = true;

  s.steeringRatio = 14.5;
  s.maxWheelAngle = 0.576;

  s.dragArea = 0.65;
  return s;
}
}  // namespace

const VehicleSpec& compactCar() { return COMPACT_CAR; }
const VehicleSpec& compactCarAutoclutch() { return COMPACT_CAR_AUTOCLUTCH; }
const VehicleSpec& compactCarAutomatic() { return COMPACT_CAR_AUTOMATIC; }

const VehicleSpec COMPACT_CAR = makeCompact();

const VehicleSpec COMPACT_CAR_AUTOCLUTCH = [] {
  VehicleSpec v = makeCompact();
  v.name = "Kompaktwagen, Kupplungshilfe";
  v.autoClutch = true;
  return v;
}();

const VehicleSpec COMPACT_CAR_AUTOMATIC = [] {
  VehicleSpec v = makeCompact();
  v.name = "Kompaktwagen 1.5 Benzin, Automatik";
  v.transmission = "automatic";
  v.gearRatios = {3.9, 2.3, 1.45, 1.05, 0.82, 0.66};
  // torque converter, pump and planetary sets cost a little more
  v.drivelineEfficiency = 0.87;
  return v;
}();

}  // namespace fs

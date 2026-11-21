// SPDX-License-Identifier: AGPL-3.0/null
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The built-in vehicle definitions.
 *
 * Origin: src/sim/vehicle/spec.ts (MaurerAnton/driving-simulator @ ff37c5d,
 * v0.7.0).  The numbers are carried over exactly.
 */
#include "sim/vehicle/spec.h"

#include <algorithm>
#include <cmath>
#include <string>

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

const double BASE_POWER_PS = 150;

const VehicleSpec TRUCK = [] {
  VehicleSpec s;
  s.name = "Lkw 12 t mit Kofferaufbau, 180 kW, automatisiertes Getriebe";
  s.mass = 9500;
  s.inertia = {61000, 58000, 13000};
  s.wheelBase = 4.7;
  s.track = 2.04;
  s.cgHeight = 1.25;
  s.cgToFront = 2.82;  // 40 % of the weight on the front axle
  s.wheelRadius = 0.49;
  s.wheelInertia = 16;

  // ride frequencies ≈ 1.5 Hz front / 1.6 Hz rear
  s.springRate = {168000, 288000};
  s.damperBump = {9000, 14000};
  s.damperRebound = {16000, 24000};
  s.antiRoll = {120000, 90000};
  s.travel = 0.26;

  s.muPeak = 0.78;
  s.slipRatioPeak = 0.12;
  s.slipAnglePeak = 0.12;
  s.loadSensitivity = 0.08;
  s.relaxLong = 0.3;
  s.relaxLat = 0.6;
  s.rollingResistance = 0.008;

  // a diesel: all its torque between 1100 and 1600 rpm, nothing above 2600
  s.torqueCurve = {{0, 300},   {500, 620},  {600, 760},  {900, 960},  {1100, 1050},
                   {1600, 1050}, {1900, 950}, {2200, 800}, {2400, 600}, {2600, 0}};
  s.idleRpm = 600;
  s.limiterRpm = 2400;
  s.stallRpm = 300;
  s.engineInertia = 1.3;
  s.engineFriction = {70, 0.07};

  s.transmission = "automatic";
  s.gearRatios = {9.3, 6.5, 4.6, 3.3, 2.3, 1.6, 1.15, 0.8};
  s.reverseRatio = 8.9;
  s.finalDrive = 3.7;
  s.clutchTorque = 2600;
  s.drivelineEfficiency = 0.9;
  s.autoClutch = false;

  s.brakeTorque = 40000;
  s.brakeBiasFront = 0.55;
  s.handbrakeTorque = 36000;
  s.abs = true;

  s.steeringRatio = 20;
  s.maxWheelAngle = 0.72;

  s.dragArea = 5.8;

  s.rearDrive = true;
  s.length = 8.2;
  s.width = 2.5;
  s.bodyPoints = {{-1.25, 0.6, -3.75}, {1.25, 0.6, -3.75}, {-1.25, 0.6, 4.45},
                  {1.25, 0.6, 4.45},   {-1.2, 3.0, -3.7},   {1.2, 3.0, -3.7},
                  {-1.25, 3.6, -1.7},  {1.25, 3.6, -1.7},   {-1.25, 3.6, 4.45},
                  {1.25, 3.6, 4.45}};
  // (the kick-down limit lies below the full-throttle shift point, or the
  // gearbox would hunt)
  s.hasAuto = true;
  s.autoBox.up = {1250, 2000};
  s.autoBox.down = {850, 1250};
  s.autoBox.kickdownRpm = 1750;
  s.autoBox.lockOmega = 85;
  s.autoBox.converter = 0.05;
  return s;
}();

const HitchPoint TRUCK_HITCH = {1.7, 0.85};   // behind the rear axle, above the ground

VehicleSpec withPower(const VehicleSpec& spec, double ps) {
  const double k = ps / BASE_POWER_PS;
  if (std::fabs(k - 1) < 0.01) return spec;
  const double low = std::max(k, std::min(1.0, 0.45 + 0.55 * k));
  VehicleSpec out = spec;
  out.name = spec.name + ", " + std::to_string(static_cast<int>(ps)) + " PS";
  out.torqueCurve.clear();
  for (const auto& pt : spec.torqueCurve) {
    const double rpm = pt.first, t = pt.second;
    const double blend = std::min(1.0, std::max(0.0, (rpm - 1000.0) / 900.0));
    const double scaled = t * (low + (k - low) * blend);
    out.torqueCurve.push_back({rpm, std::round(scaled * 10.0) / 10.0});
  }
  // a stronger engine needs a clutch that holds its torque
  out.clutchTorque = std::max(spec.clutchTorque, spec.clutchTorque * k * 1.1);
  return out;
}

}  // namespace fs

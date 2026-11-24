// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/vehicle/spec.ts @ v1.7.7
#pragma once

#include "sim/vehicle/types.h"

namespace fs {

/**
 * A typical driving-school compact hatchback: front-wheel drive, 1.5 l turbo
 * petrol (~110 kW, 250 N·m), 6-speed manual, ~1.3 t.  Generic on purpose –
 * no real model is reproduced.
 */
inline const VehicleSpec COMPACT_CAR = {
    .name = "Kompaktwagen 1.5 Benzin, 6-Gang",
    .mass = 1375,
    .inertia = {1900, 2000, 500},
    .wheelbase = 2.63,
    .track = 1.54,
    .cgHeight = 0.54,
    .cgToFront = 1.052,  // 60 % of the weight on the front axle
    .wheelRadius = 0.316,
    .wheelInertia = 1.1,

    // ride frequencies ≈ 1.35 Hz front / 1.5 Hz rear, damping ratio ≈ 0.33
    .springRate = {29600, 24400},
    .damperBump = {1700, 1250},
    .damperRebound = {3000, 2250},
    .antiRoll = {14000, 7000},
    .travel = 0.24,

    .muPeak = 1.05,
    .slipRatioPeak = 0.11,
    .slipAnglePeak = 0.13,
    .loadSensitivity = 0.1,
    .relaxLong = 0.15,
    .relaxLat = 0.32,
    .rollingResistance = 0.011,

    .torqueCurve =
        {
            {0, 60},
            {500, 110},
            {800, 145},
            {1000, 172},
            {1300, 216},
            {1500, 250},
            {3500, 250},
            {4500, 236},
            {5000, 216},
            {5500, 195},
            {6000, 176},
            {6500, 150},
            {7000, 90},
            {7500, 0},
        },
    .idleRpm = 800,
    .limiterRpm = 6500,
    .stallRpm = 400,
    .engineInertia = 0.16,
    .engineFriction = {14, 0.0085},

    .transmission = "manual",
    .gearRatios = {3.77, 2.09, 1.32, 0.98, 0.78, 0.64},
    .reverseRatio = 3.6,
    .finalDrive = 3.65,
    .clutchTorque = 420,
    .drivelineEfficiency = 0.92,
    .autoClutch = false,

    .brakeTorque = 5200,
    .brakeBiasFront = 0.68,
    .handbrakeTorque = 2800,
    .abs = true,

    .steeringRatio = 14.5,
    .maxWheelAngle = 0.576,

    .dragArea = 0.65,
};

/** Same car with the clutch operated automatically (keyboard-friendly). */
inline const VehicleSpec COMPACT_CAR_AUTOCLUTCH = [] {
  VehicleSpec s = COMPACT_CAR;
  s.name = "Kompaktwagen, Kupplungshilfe";
  s.autoClutch = true;
  return s;
}();

/** Same car with an automatic gearbox (licence code B197 / automatic training). */
inline const VehicleSpec COMPACT_CAR_AUTOMATIC = [] {
  VehicleSpec s = COMPACT_CAR;
  s.name = "Kompaktwagen 1.5 Benzin, Automatik";
  s.transmission = "automatic";
  s.gearRatios = {3.9, 2.3, 1.45, 1.05, 0.82, 0.66};
  // torque converter, pump and planetary sets cost a little more
  s.drivelineEfficiency = 0.87;
  return s;
}();

/** Rated power of the base engine in PS (110 kW). */
inline constexpr double BASE_POWER_PS = 150;

/**
 * The same car with a weaker or stronger engine: the torque curve is scaled
 * to the rated power.  Small engines keep most of their torque just above
 * idle (as small turbo engines do), so that pulling away with the clutch
 * stays as easy as before.
 */
VehicleSpec withPower(const VehicleSpec& spec, double ps);

/** The highest power of an engine along its torque curve (PS). */
double peakPowerPs(const VehicleSpec& spec);

/**
 * Any vehicle with another engine of its kind: the whole torque curve scaled
 * to the power asked for (PS) and the clutch made to hold it.  With an
 * automatic gearbox a weaker engine gets a converter that takes its torque at
 * the same engine speed as before (or it would be held down and stall); a
 * stronger one keeps the converter – it then slips up to a higher speed, but
 * at idle it pulls no harder than before, so that the brake still holds the
 * vehicle at a red light.
 */
VehicleSpec atPower(const VehicleSpec& spec, double ps);

/**
 * A two-axle distribution truck as driving schools use it for the classes C
 * and CE: 12 t gross, 9.5 t as driven, 180 kW diesel, automated gearbox,
 * rear-wheel drive, box body.  With the centre-axle trailer of
 * src/sim/trailer.ts it is a combination of 15.7 m and 15.5 t – what the
 * regulations ask of a test vehicle for class CE (Anlage 7 FeV, 2.2.7 a).
 * Generic on purpose – no real model is reproduced.
 */
inline const VehicleSpec TRUCK = {
    .name = "Lkw 12 t mit Kofferaufbau, 180 kW, automatisiertes Getriebe",
    .mass = 9500,
    .inertia = {61000, 58000, 13000},
    .wheelbase = 4.7,
    .track = 2.04,
    .cgHeight = 1.25,
    .cgToFront = 2.82,  // 40 % of the weight on the front axle
    .wheelRadius = 0.49,
    .wheelInertia = 16,

    // ride frequencies ≈ 1.5 Hz front / 1.6 Hz rear
    .springRate = {168000, 288000},
    .damperBump = {9000, 14000},
    .damperRebound = {16000, 24000},
    .antiRoll = {120000, 90000},
    .travel = 0.26,

    .muPeak = 0.78,
    .slipRatioPeak = 0.12,
    .slipAnglePeak = 0.12,
    .loadSensitivity = 0.08,
    .relaxLong = 0.3,
    .relaxLat = 0.6,
    .rollingResistance = 0.008,

    // a diesel: all its torque between 1100 and 1600 rpm, nothing above 2600
    .torqueCurve =
        {
            {0, 300},
            {500, 620},
            {600, 760},
            {900, 960},
            {1100, 1050},
            {1600, 1050},
            {1900, 950},
            {2200, 800},
            {2400, 600},
            {2600, 0},
        },
    .idleRpm = 600,
    .limiterRpm = 2400,
    .stallRpm = 300,
    .engineInertia = 1.3,
    .engineFriction = {70, 0.07},

    .transmission = "automatic",
    .gearRatios = {9.3, 6.5, 4.6, 3.3, 2.3, 1.6, 1.15, 0.8},
    .reverseRatio = 8.9,
    .finalDrive = 3.7,
    .clutchTorque = 2600,
    .drivelineEfficiency = 0.9,
    .autoClutch = false,

    .brakeTorque = 40000,
    .brakeBiasFront = 0.55,
    .handbrakeTorque = 36000,
    .abs = true,

    .steeringRatio = 20,
    .maxWheelAngle = 0.72,

    .dragArea = 5.8,

    .rearDrive = true,
    .length = 8.2,
    .width = 2.5,
    .bodyPoints = std::vector<std::array<double, 3>>{
        {-1.25, 0.6, -3.75},
        {1.25, 0.6, -3.75},
        {-1.25, 0.6, 4.45},
        {1.25, 0.6, 4.45},
        {-1.2, 3.0, -3.7},
        {1.2, 3.0, -3.7},
        {-1.25, 3.6, -1.7},
        {1.25, 3.6, -1.7},
        {-1.25, 3.6, 4.45},
        {1.25, 3.6, 4.45},
    },
    // (the kick-down limit lies below the full-throttle shift point, or the gearbox would hunt)
    .auto_ = VehicleSpec::Auto{.up = {1250, 2000}, .down = {850, 1250}, .kickdownRpm = 1750, .lockOmega = 85, .converter = 0.05},
};

/** Where the trailer coupling of the truck is: metres behind the rear axle, and above the ground. */
struct TruckHitch {
  double behindRearAxle;
  double height;
};
inline constexpr TruckHitch TRUCK_HITCH = {.behindRearAxle = 1.7, .height = 0.85};

}  // namespace fs

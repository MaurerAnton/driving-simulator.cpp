// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The built-in vehicle definitions.
 *
 * Origin: src/sim/vehicle/spec.ts (MaurerAnton/driving-simulator @ ff37c5d,
 * v0.7.0): the compact car with its variants, the class-CE truck, the engine
 * power choices and TRUCK_HITCH.
 */
#pragma once

#include "sim/vehicle/types.h"

namespace fs {

/** A typical driving-school compact hatchback: front-wheel drive, 1.5 l turbo
 *  petrol (~110 kW, 250 N·m), 6-speed manual, ~1.3 t.  Generic on purpose –
 *  no real model is reproduced. */
extern const VehicleSpec COMPACT_CAR;
/** Same car with the clutch operated automatically (keyboard-friendly). */
extern const VehicleSpec COMPACT_CAR_AUTOCLUTCH;
/** Same car with an automatic gearbox (licence code B197 / automatic training). */
extern const VehicleSpec COMPACT_CAR_AUTOMATIC;

/** Rated power of the base engine in PS (110 kW). */
extern const double BASE_POWER_PS;

/**
 * A two-axle distribution truck as driving schools use it for the classes C
 * and CE: 12 t gross, 9.5 t as driven, 180 kW diesel, automated gearbox,
 * rear-wheel drive, box body.  With the centre-axle trailer of
 * src/sim/trailer.ts it is a combination of 15.7 m and 15.5 t – what the
 * regulations ask of a test vehicle for class CE (Anlage 7 FeV, 2.2.7 a).
 * Generic on purpose – no real model is reproduced.
 */
extern const VehicleSpec TRUCK;

/** Where the trailer coupling of the truck is: metres behind the rear axle, and above the ground. */
struct HitchPoint {
  double behindRearAxle;
  double height;
};
extern const HitchPoint TRUCK_HITCH;

/**
 * The same car with a weaker or stronger engine: the torque curve is scaled
 * to the rated power.  Small engines keep most of their torque just above
 * idle (as small turbo engines do), so that pulling away with the clutch
 * stays as easy as before.
 */
VehicleSpec withPower(const VehicleSpec& spec, double ps);

}  // namespace fs

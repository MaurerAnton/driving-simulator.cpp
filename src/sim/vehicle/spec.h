// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The built-in vehicle definitions.
 *
 * Origin: src/sim/vehicle/spec.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0): COMPACT_CAR and its two variants.
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

}  // namespace fs

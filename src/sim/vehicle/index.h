// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The vehicle models, re-exported for the game.
 *
 * Origin: src/sim/vehicle/index.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0) — four lines in the original: it just re-exports the parts.
 */
#pragma once

#include "sim/vehicle/spec.h"
#include "sim/vehicle/types.h"
#include "sim/vehicle/vehicle.h"

namespace fs {
// COMPACT_CAR, COMPACT_CAR_AUTOCLUTCH and COMPACT_CAR_AUTOMATIC come from
// spec.h (the three built-in vehicles); DriverInput, Ground, GroundSample,
// VehicleSpec and WheelState come from types.h; Vehicle from vehicle.h.
}  // namespace fs

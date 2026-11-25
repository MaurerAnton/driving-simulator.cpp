// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/cyclist.ts @ v1.7.7
/**
 * A person on a bicycle, from a few simple shapes: the wheels turn, the legs
 * pedal, and after a fall bicycle and rider lie on their side.
 *
 * Local frame as for the cars: −z is forwards, y is up, the origin lies on
 * the ground under the middle of the bicycle.
 *
 * (The class CyclistModel of the original is the interface render/actors.h declares – its tests
 * stand in for it; this is the model itself.  `makeCyclistModel` is what the traffic's drawing
 * makes – unless a test sets `CyclistModel::create`.)
 */
#pragma once

#include <memory>

#include "render/actors.h"

namespace fs {

/** `new CyclistModel(colour)` */
std::unique_ptr<CyclistModel> makeCyclistModel(int colour);

}  // namespace fs

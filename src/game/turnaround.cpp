// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/turnaround.ts @ v1.7.7
#include "game/turnaround.h"

#include <cmath>
#include <numbers>

namespace fs {

namespace {
/** How far from where the vehicle stands the lane of the other direction may lie (m). */
const double REACH = 10;
/** The lane must face the other way: the angle between the two ways over 120° (cos below −0.5). */
const double OPPOSITE = -0.5;
}  // namespace

TurnSpot turnedSpot(const TurnSpot& here, const std::optional<TurnSpot>& found) {
  const TurnSpot inPlace = {.x = here.x, .y = here.y, .z = here.z, .yaw = here.yaw + std::numbers::pi};
  if (!found) return inPlace;
  if (std::hypot(found->x - here.x, found->z - here.z) > REACH) return inPlace;
  if (std::cos(found->yaw - here.yaw) > OPPOSITE) return inPlace;
  return *found;
}

}  // namespace fs

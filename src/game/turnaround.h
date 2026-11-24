// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/turnaround.ts @ v1.7.7
/**
 * Turning a vehicle round (game.ts turnAround, input action 'turn'): into the
 * lane of the other direction on a two-way road, or where it stands on a
 * one-way street.  The choice is made here, away from the game, so that it
 * can be tested (tests/game_turnaround_test.cpp).
 */
#pragma once

#include <optional>

namespace fs {

/** Where a vehicle stands or is put: a place on the ground, facing `yaw` (rad). */
struct TurnSpot {
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
};

/**
 * Where the turn-round puts the vehicle: `here` where it stands, `found` the
 * lane found the other way round (freeSpot with yaw + π), or null.
 *
 * The lane is taken when it lies close by and faces the other way – the lane
 * of the other direction on a two-way road.  Otherwise the vehicle turns
 * where it stands, facing the other way: a one-way street, no lane there, or
 * one that is not really the other direction (a lane beside it, one that runs
 * on past it, a road nearby that goes the same way).
 */
TurnSpot turnedSpot(const TurnSpot& here, const std::optional<TurnSpot>& found);

}  // namespace fs

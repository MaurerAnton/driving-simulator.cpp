// LINK: game core platform
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Turning a vehicle round (src/game/turnaround.h): into the lane of the
 * other direction on a two-way road, where it stands on a one-way street.
 *
 * Origin: tests/game/turnaround.test.ts @ v1.7.7
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <numbers>

#include "game/turnaround.h"

using namespace fs;

static const double PI_ = std::numbers::pi;

static bool same(const TurnSpot& a, const TurnSpot& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.yaw == b.yaw; }

int main() {
  /** where the vehicle stands: on the road, facing north (yaw 0 = north) */
  const TurnSpot here = {.x = 100, .y = 50, .z = 200, .yaw = 0};

  // the lane of the other direction is taken
  {
    // the lane beside it, facing south: the other direction of a two-way road
    const TurnSpot other = {.x = 103, .y = 50, .z = 198, .yaw = PI_};
    assert(same(turnedSpot(here, other), other));
    // (also a lane that is turned a little: over 120° apart is the other way round)
    const TurnSpot skewed = {.x = 104, .y = 50, .z = 203, .yaw = PI_ - 0.6};
    assert(same(turnedSpot(here, skewed), skewed));
  }

  // a lane facing the same way, or too far away, means turning in place
  {
    const TurnSpot inPlace = {.x = 100, .y = 50, .z = 200, .yaw = PI_};
    // a lane going the way the vehicle already goes: not the other direction
    assert(same(turnedSpot(here, TurnSpot{.x = 103, .y = 50, .z = 200, .yaw = 0}), inPlace));
    // (a quarter turn away: neither this way nor the other way)
    assert(same(turnedSpot(here, TurnSpot{.x = 103, .y = 50, .z = 200, .yaw = PI_ / 2}), inPlace));
    // the lane of the other direction, but 30 m away (another road): where it stands
    assert(same(turnedSpot(here, TurnSpot{.x = 103, .y = 50, .z = 230, .yaw = PI_}), inPlace));
    // no lane found the other way round (a one-way street): where it stands
    assert(same(turnedSpot(here, std::nullopt), inPlace));
  }

  // turning in place keeps the place and turns the yaw by 180°
  {
    const TurnSpot standing = {.x = -40, .y = 12, .z = 7, .yaw = 1.25};
    const TurnSpot turned = turnedSpot(standing, std::nullopt);
    assert(turned.x == standing.x);
    assert(turned.y == standing.y);
    assert(turned.z == standing.z);
    assert(std::fabs(turned.yaw - (standing.yaw + PI_)) < 1e-9);
    // and the same the other way round: two turn-rounds are back where they were
    const TurnSpot back = turnedSpot(turned, std::nullopt);
    assert(std::fabs(back.yaw - standing.yaw) < 1e-9 || std::fabs(std::fabs(back.yaw - standing.yaw) - 2 * PI_) < 1e-9);
  }

  std::printf("game_turnaround_test: ok\n");
  return 0;
}

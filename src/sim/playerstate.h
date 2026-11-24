// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/traffic.ts @ v1.7.7 (interface PlayerState)
/**
 * PlayerState is the traffic's (sim/traffic.h includes this): it stands in a header of its
 * own because the state of a car going round something (sim/pastblock.h) holds one, and the
 * traffic holds that.
 */
#pragma once

#include <optional>

#include "world/lanes.h"

namespace fs {

/** What the traffic needs to know about the player's car. */
struct PlayerState {
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  double speed = 0;
  double length = 0;
  double width = 0;
  /** the lane the vehicle is on and how far along it its middle is (if the caller knows: the cyclists are sent along by it) */
  Lane* lane = nullptr;
  std::optional<double> s = std::nullopt;
  /** among the obstacles: a part of the player's vehicle (its trailer) */
  bool own = false;
  /** the player is on foot (then x, z are where they stand and yaw where they look) */
  bool onFoot = false;
  /** the horn sounds (the people near it start) */
  bool horn = false;
  /** among the obstacles: a vehicle that stands there for good (parked, broken down) – the traffic goes round it, over a solid line if need be */
  bool parked = false;
};

}  // namespace fs

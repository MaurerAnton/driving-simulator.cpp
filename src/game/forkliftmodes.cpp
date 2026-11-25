// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/forkliftmodes.ts @ v1.7.7
#include "game/forkliftmodes.h"

#include <algorithm>
#include <cmath>

namespace fs {

namespace {
/** how far from the forklift's middle the operator stands after getting off (half its width and a step) */
constexpr double OFF_SIDE = 1.35;
/** within this of its middle the operator can get on again (m) */
constexpr double ON_REACH = 3.2;
}  // namespace

ForkView nextForkView(const ForkView& v) {
  // (indexOf: −1 for a view that is none of them – the first then)
  const auto it = std::find(FORK_VIEWS.begin(), FORK_VIEWS.end(), v);
  const int i = it == FORK_VIEWS.end() ? -1 : int(it - FORK_VIEWS.begin());
  return FORK_VIEWS[size_t((i + 1) % int(FORK_VIEWS.size()))];
}

ForkCamera forkCamera(const ForkView& v) { return v == "side" ? ForkCamera{"chase", FORK_SIDE_YAW} : ForkCamera{v, 0}; }

ForkExit forkliftExit(double x, double z, double yaw, double side) {
  // (to the right is (cos yaw, −sin yaw))
  return {x + std::cos(yaw) * side * OFF_SIDE, z - std::sin(yaw) * side * OFF_SIDE, yaw};
}

bool nearForklift(double fx, double fz, double x, double z) { return std::hypot(x - fx, z - fz) <= ON_REACH; }

}  // namespace fs

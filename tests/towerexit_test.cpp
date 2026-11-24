// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Getting out of the tower crane (src/game/craneplay.h): the operator climbs
 * down and stands beside its cross base – on the left first, else the next
 * free place round it –, and climbs up again from its foot only.
 *
 * Origin: tests/game/towerexit.test.ts @ v1.7.7
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <numbers>

#include "game/craneplay.h"

using namespace fs;

static const double PI_ = std::numbers::pi;

/** the cross base is 5 × 5 m round the axis: nobody stands on it */
static bool offBase(double bx, double bz, double yaw, double x, double z) {
  const double dx = x - bx;
  const double dz = z - bz;
  const double u = dx * std::cos(yaw) - dz * std::sin(yaw);
  const double w = dx * std::sin(yaw) + dz * std::cos(yaw);
  return std::abs(u) > 2.5 || std::abs(w) > 2.5;
}

int main() {
  // down from the tower crane: beside the base on its left, facing it, off the base, at its foot
  for (const double yaw : {0.0, 0.5, -1.9, PI_}) {
    const CranePlace p = towerExit(300, -80, yaw, [](double, double) { return true; });
    assert(offBase(300, -80, yaw, p.x, p.z));
    // left of the crane's heading: to the right is (cos, −sin)
    const double right = (p.x - 300) * std::cos(yaw) - (p.z + 80) * std::sin(yaw);
    assert(right < -4);
    // facing the tower: forwards (−sin, −cos) points at the axis
    const double fx = -std::sin(p.yaw);
    const double fz = -std::cos(p.yaw);
    const double d = std::hypot(300 - p.x, -80 - p.z);
    assert(std::abs(fx * (300 - p.x) + fz * (-80 - p.z) - d) < 1e-6);
    assert(atTowerFoot(300, -80, p.x, p.z));
  }
  // where the left side is blocked: the next free place round the base; none free: the left after all
  {
    const auto blockedLeft = [](double x) { return x > -2; };
    const CranePlace p = towerExit(0, 0, 0, [&](double x, double) { return blockedLeft(x); });
    assert(p.x > -2);
    assert(offBase(0, 0, 0, p.x, p.z));
    const CranePlace q = towerExit(0, 0, 0, [](double, double) { return false; });
    assert(q.x < -4 && std::abs(q.z) < 1e-9);
  }
  // climbing up only from the foot of the crane
  assert(atTowerFoot(0, 0, 4.3, 0) == true);
  assert(atTowerFoot(0, 0, 0, 6.4) == true);
  assert(atTowerFoot(0, 0, 9, 0) == false);
  std::printf("towerexit_test: ok\n");
  return 0;
}

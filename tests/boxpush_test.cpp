// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim game world core platform geo
// Origin: tests/sim/boxpush.test.ts @ v1.7.7
#include <cmath>

#include "sim/traffic.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using namespace fs::harness;

namespace {
// (yaw 0 faces −z: the front of a box at yaw 0 is towards −z)
struct Box {
  double hl;
  double hw;
};
const Box BUS = {6, 1.25};
const Box CAR = {2.2, 0.9};
}  // namespace

int main() {
  std::printf("boxpush_test (tests/sim/boxpush.test.ts)\n");
  {
    test("a car nosing into the rear of a 12 m bus is pushed back out of it (1.7.1: not only near the cabin)");
    // bus at the origin facing −z; its rear end at z = +6; the car behind it, facing −z, its front 0.5 m inside the bus
    const double carZ = 6 + CAR.hl - 0.5;
    ok(boxesOverlap(0, 0, 0, BUS.hl, BUS.hw, 0, carZ, 0, CAR.hl, CAR.hw) == true, "the boxes overlap");
    const std::optional<BoxPush> p = boxPush(0, 0, 0, BUS.hl, BUS.hw, 0, carZ, 0, CAR.hl, CAR.hw);
    ok(p.has_value(), "p");
    ok(std::abs(p->nx) < 1e-9 && std::abs(p->nz - 1) < 1e-9, "out backwards (+z), got " + jsStr(p->nx) + ", " + jsStr(p->nz));
    ok(std::abs(p->depth - 0.5) < 1e-9, "depth 0.5");
  }
  {
    test("a car hitting the side of a bus near its rear is pushed out sideways, not along the bus");
    // car at the bus's right-hand side, 4 m from its middle, turned across it, 0.3 m in
    const double carX = BUS.hw + CAR.hl - 0.3;
    const std::optional<BoxPush> p = boxPush(0, 0, 0, BUS.hl, BUS.hw, carX, 4, PI / 2, CAR.hl, CAR.hw);
    ok(p.has_value(), "p");
    ok(std::abs(std::abs(p->nx) - 1) < 1e-9 && std::abs(p->nz) < 1e-9, "sideways");
    ok(p->nx > 0, "towards the car");
    ok(std::abs(p->depth - 0.3) < 1e-9, "depth 0.3");
  }
  {
    test("boxes apart: nothing to push");
    ok(!boxPush(0, 0, 0, BUS.hl, BUS.hw, 0, 6 + CAR.hl + 0.1, 0, CAR.hl, CAR.hw).has_value(), "null");
  }
  std::printf("boxpush_test: all 3 cases passed\n");
  return 0;
}

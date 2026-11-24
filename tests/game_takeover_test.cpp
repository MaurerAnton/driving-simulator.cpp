// LINK: game sim render i18n audio input engine world core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Any vehicle can be watched and taken over (src/game/takeover.h): what each kind
 * of the traffic is driven as, and which vehicle a tap along a ray meets.
 *
 * Origin: tests/game/takeover.test.ts @ v1.7.7 (the emergency vehicles of the traffic are no
 * part of the port, nor is the test place of game/showdrive.ts: their cases are left out).
 */
#include <cassert>
#include <cstdio>
#include <optional>
#include <vector>

#include "game/takeover.h"
#include "game/vehicles.h"
#include "sim/traffic.h"

using namespace fs;

static PickBox box(double x, double z, int id) {
  PickBox b;
  b.x = x;
  b.y = 0;
  b.z = z;
  b.yaw = 0;
  b.height = 1.5;
  b.front = 2.2;
  b.rear = 2.2;
  b.hw = 0.9;
  b.what = {.kind = "car", .id = id};
  return b;
}

int main() {
  // every kind of the traffic is taken over as one of the game's vehicles
  {
    for (int s : {0, 1, 2}) assert(takeKindOf(s) == "car");
    assert(takeKindOf(BUS_STYLE) == "bus");
    // (and each is a vehicle of the game, driven on the road)
    for (int s = 0; s < 8; s++) assert(VEHICLES.find(takeKindOf(s)));
  }

  // a tap meets the nearest vehicle along its ray
  {
    const std::vector<PickBox> boxes = {box(0, -40, 1), box(0, -20, 2), box(5, -10, 3)};
    // (from 1 m up at the origin, looking north)
    assert((pickAlong(0, 1, 0, 0, 0, -1, boxes) == PickTarget{.kind = "car", .id = 2}));
    // (looking a little east of north: the third, 5 m east and 10 m north)
    assert((pickAlong(0, 1, 0, 0.5, 0, -1, boxes) == PickTarget{.kind = "car", .id = 3}));
    // (over them all, or beyond reach: none)
    assert(!pickAlong(0, 1, 0, 0, 1, -1, boxes));
    assert(!pickAlong(0, 1, 0, 0, 0, -1, boxes, 10));
  }

  std::printf("game_takeover_test: ok\n");
  return 0;
}

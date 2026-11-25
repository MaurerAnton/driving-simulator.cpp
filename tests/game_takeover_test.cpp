// LINK: game sim render i18n audio input engine world core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Any vehicle can be watched and taken over (src/game/takeover.h): what each kind
 * of the traffic is driven as, and which vehicle a tap along a ray meets.
 *
 * Origin: tests/game/takeover.test.ts @ v1.7.7.
 */
#include <cassert>
#include <cstdio>
#include <optional>
#include <vector>

#include "game/showdrive.h"
#include "game/takeover.h"
#include "game/vehicles.h"
#include "sim/emergency.h"
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
    for (const bool lit : {false, true}) {
      assert(takeKindOf(emStyle(EM_POLICE, lit)) == "car");
      assert(takeKindOf(emStyle(EM_DOCTOR, lit)) == "car");
      assert(takeKindOf(emStyle(EM_AMBULANCE, lit)) == "lightTruck");
      assert(takeKindOf(emStyle(EM_FIRE, lit)) == "truckSolo");
    }
    // (and each is a vehicle of the game, driven on the road)
    for (int s = 0; s < 16; s++) assert(VEHICLES.find(takeKindOf(s)));
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

  // a model of the test place got into is hidden while driven; the others are shown
  {
    ShowSpots spots;
    Group ra;
    Group rb;
    spots.add(ShowSpot{.kind = "car", .x = 0, .y = 0, .z = 0, .yaw = 0, .outline = VEHICLES.at("car").outline, .height = 1.6, .roots = {&ra}});
    spots.add(ShowSpot{.kind = "bus", .x = 20, .y = 0, .z = 0, .yaw = 0, .outline = VEHICLES.at("bus").outline, .height = 3.4, .roots = {&rb}});
    assert(spots.boxes().size() == 2);
    // (a drive on the test place from the menu: from the usual place, all shown)
    assert(spots.start() == nullptr);
    assert(spots.taken == -1);
    // into the bus: the drive begins where it stands, it is hidden
    assert(spots.choose(1) == &spots.list[1]);
    assert(spots.start() == &spots.list[1]);
    assert(rb.visible == false);
    assert(ra.visible == true);
    const std::vector<PickBox> left = spots.boxes();
    assert(left.size() == 1 && (left[0].what == PickTarget{.kind = "show", .id = -1, .i = 0}));
    // on foot beside the car: it; beside where the bus stood: nothing (it is driven)
    assert(spots.near(1.5, 0) == 0);
    assert(spots.near(21.5, 0) == -1);
    // then into the car: the bus is shown again
    spots.choose(0);
    spots.start();
    assert(rb.visible == true);
    assert(ra.visible == false);
  }

  std::printf("game_takeover_test: ok\n");
  return 0;
}

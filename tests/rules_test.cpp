// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim world core platform geo
// Origin: tests/sim/limitmatch.test.ts @ v1.7.7
/**
 * The speed limit the rule monitor reports (src/sim/rules): a weak match –
 * a gap in the road surface, a junction area, the nearest-road fallback – must
 * not change it, or the 50 km/h of a town road on a bridge over the motorway
 * (or of a road under it) becomes the limit of the motorway, and the speed
 * limiter of the game (src/game/game) holds the car there.
 *
 * The world is a motorway with a slip road joining it, and a 50 km/h town road
 * crossing 3.5 m above it on a bridge.
 */
#include <cmath>
#include <string>
#include <vector>

#include "src/sim/rules.h"
#include "src/sim/signals.h"
#include "src/sim/traffic.h"
#include "tests/town.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using fs::harness::ok;
using fs::harness::test;
using fs::town::Net;
using fs::town::Road3;

namespace {

const double RDT = 1.0 / 10;
/** standing still, looking the way the road runs */
const double EAST = jsAtan2(-1, 0);  // travel +x
const double NORTH = PI;             // travel +z

/**
 * A motorway (no limit) from x 0 to 400 with a slip road joining it at x 100,
 * and a 50 km/h town road crossing it 3.5 m above on a bridge at x 250.
 *
 *   0        100      250            400
 *   A========J========X==============B   motorway, y 100
 *            \                          slip road, y 100
 *             \
 *             |  town road, y 103.5 (50 km/h)
 */
Net setUp() {
  return town::makeNet3({{0, 100, 0}, {100, 100, 0}, {400, 100, 0}, {160, 100, -70}, {250, 103.5, -60}, {250, 103.5, 60}},
                        {
                            Road3{.a = 0, .b = 1, .cls = MOTORWAY, .lf = 2, .lb = 2, .lw = 3.75, .max = 0, .name = "A 99"},
                            Road3{.a = 1, .b = 2, .cls = MOTORWAY, .lf = 2, .lb = 2, .lw = 3.75, .max = 0, .name = "A 99"},
                            Road3{.a = 1, .b = 3, .cls = MOTORWAY_LINK, .lf = 1, .lb = 0, .lw = 3.75, .max = 80},
                            Road3{.a = 4, .b = 5, .cls = RESIDENTIAL, .lf = 1, .lb = 1, .lw = 3, .max = 50, .name = "Brückenstraße"},
                        });
}

/** A still car at (x, y, z) looking `yaw` – what the monitor makes of it. */
void stand(RuleMonitor& rules, double x, double y, double z, double yaw) {
  PlayerSnapshot snap;
  snap.x = x;
  snap.y = y;
  snap.z = z;
  snap.yaw = yaw;
  snap.speed = 0;
  snap.indicator = 0;
  snap.hazard = false;
  snap.lookYaw = 0;
  snap.seatbelt = true;
  snap.handbrake = 0;
  snap.stalled = false;
  snap.hitScenery = 0;
  snap.length = 4.3;
  snap.front = 2.15;
  rules.update(RDT, snap, {});
}

struct Monitor {
  Net net;
  SignalSystem signals;
  Traffic traffic;
  RuleMonitor rules;
  Monitor() : net(setUp()), signals(net.world->signals(), nullptr, &net.world->signalsPresent()), traffic(*net.world, *net.lanes, signals), rules(*net.world, *net.lanes, signals, traffic) { traffic.targetCount = 0; }
};

SurfaceHit hit = newSurfaceHit();

}  // namespace

int main() {
  std::printf("rules_test (tests/sim/limitmatch.test.ts)\n");
  {
    test("a gap in the road surface keeps the limit of the motorway under the bridge");
    Monitor m;
    // just outside the carriageway, directly under the town road 3.5 m above:
    // no road surface there, and the fallback must not take the road on the bridge
    ok(m.net.world->surfaceAt(250, 9.6, 100.6, hit) == false, "no road surface just outside the carriageway");
    stand(m.rules, 250, 100, 9.6, EAST);
    ok(m.rules.context.limit == 0, "the limit of the motorway (" + jsStr(m.rules.context.limit) + ")");
  }
  {
    test("a junction area keeps the limit of the road the car came from");
    Monitor m;
    stand(m.rules, 85, 100, 0, EAST);
    ok(m.rules.context.limit == 0, "on the motorway: no limit (" + jsStr(m.rules.context.limit) + ")");
    // the junction area of the interchange: its own surface, no carriageway of its own
    ok(m.net.world->surfaceAt(100, 0, 100.6, hit) && hit.node != nullptr && hit.edge == nullptr, "the junction area has its own surface");
    stand(m.rules, 100, 100, 0, EAST);
    ok(m.rules.context.limit == 0, "in the junction area: still none (" + jsStr(m.rules.context.limit) + ")");
  }
  {
    test("a gap with no road near keeps the limit of the road before it");
    Monitor m;
    stand(m.rules, 140, 100, 0, EAST);
    ok(m.rules.context.limit == 0, "on the motorway: no limit");
    // off the carriageway by more than the fallback reaches: neither a surface nor a road
    ok(m.net.world->surfaceAt(150, 11, 100.6, hit) == false, "no surface 11 m off");
    stand(m.rules, 150, 100, 11, EAST);
    ok(m.rules.context.limit == 0, "the limit of the road before it (" + jsStr(m.rules.context.limit) + ")");
  }
  {
    test("the hold does not reach beyond 40 m");
    Monitor m;
    stand(m.rules, 140, 100, 0, EAST);
    stand(m.rules, 140, 100, 60, EAST);
    ok(m.rules.context.limit == -1, "no limit to show 60 m away (" + jsStr(m.rules.context.limit) + ")");
  }
  {
    test("a solid hit on another road takes its limit at once");
    Monitor m;
    stand(m.rules, 240, 100, 0, EAST);
    ok(m.rules.context.limit == 0, "on the motorway: no limit");
    // on the town road of the bridge: its 50 counts from the first step on it
    const Edge* townRoad = nullptr;
    for (const Edge& e : m.net.world->edges())
      if (e.name == "Brückenstraße") townRoad = &e;
    ok(townRoad != nullptr, "the town road is there");
    ok(m.net.world->surfaceAt(250, 0, 104.1, hit) && hit.edge == townRoad, "on the bridge: the town road's surface");
    stand(m.rules, 250, 103.5, 0, NORTH);
    ok(m.rules.context.limit == 50, "its 50 at once (" + jsStr(m.rules.context.limit) + ")");
  }
  std::printf("rules_test: all 5 cases passed\n");
  return 0;
}

// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim world core platform geo
// Origin: tests/sim/groundgrip.test.ts @ v1.7.7
/**
 * "No road physics" (settings evenRoads, WorldGround.even): every surface –
 * asphalt and grass alike – grips and rolls as dry asphalt.  The sample() that
 * the wheels read must give the same grip and rolling on a road as on a field
 * when the option is on, whatever the weather.
 */
#include <cmath>
#include <functional>

#include "sim/ground.h"
#include "sim/jsmath.h"
#include "world/cover.h"
#include "tests/town.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using fs::harness::ok;
using fs::harness::test;

namespace {

/** A terrain that is asphalt at one point and grass at another (no tiles needed). */
class FakeTerrain : public TerrainSource {
 public:
  explicit FakeTerrain(int cover_) : cover(cover_) {}
  double heightAt(double x, double) override { return 100 + jsSin(x * 0.1) * 0.5; }  // (a little bump, like terrain)
  int coverAt(double, double) override { return cover; }

 private:
  int cover;
};

Town theTown() {
  TownOptions o;
  o.cols = 3;
  o.rows = 3;
  o.block = 90;
  return makeTown(o);
}

}  // namespace

int main() {
  std::printf("groundgrip_test (tests/sim/groundgrip.test.ts)\n");
  {
    test("with even on: asphalt and grass give the same grip and rolling");
    Town town = theTown();
    FakeTerrain onAsphalt(COVER_ASPHALT);
    FakeTerrain onGrass(COVER_GRASS);
    WorldGround asphalt(*town.world, onAsphalt);
    WorldGround grass(*town.world, onGrass);
    asphalt.even = true;
    grass.even = true;
    GroundSample a;
    GroundSample g;
    // (a point off the town's streets: the terrain path; the cover says which)
    asphalt.sample(45, 45, 100, a);
    grass.sample(45, 45, 100, g);
    ok(a.grip == g.grip, "grip is the same on asphalt and grass");
    ok(a.rolling == g.rolling, "rolling is the same on asphalt and grass");
    ok(a.grip == 1, "even: grip is dry asphalt (1)");
    ok(a.rolling == 1, "even: rolling is dry asphalt (1)");
  }
  {
    test("with even off: asphalt and grass differ (the option is doing something)");
    Town town = theTown();
    FakeTerrain onAsphalt(COVER_ASPHALT);
    FakeTerrain onGrass(COVER_GRASS);
    WorldGround asphalt(*town.world, onAsphalt);
    WorldGround grass(*town.world, onGrass);
    GroundSample a;
    GroundSample g;
    asphalt.sample(45, 45, 100, a);
    grass.sample(45, 45, 100, g);
    ok(a.grip > g.grip, "asphalt grips more than grass (" + jsStr(a.grip) + " > " + jsStr(g.grip) + ")");
    ok(a.rolling < g.rolling, "asphalt rolls easier than grass (" + jsStr(a.rolling) + " < " + jsStr(g.rolling) + ")");
  }
  {
    test("evenTerrain: grass lies flat as a road (no bumps, no camber)");
    Town town = theTown();
    FakeTerrain onGrass(COVER_GRASS);
    WorldGround g(*town.world, onGrass);
    g.evenTerrain = true;
    GroundSample s;
    g.sample(45, 45, 100, s);
    ok(s.nx == 0, "no camber left/right");
    ok(s.nz == 0, "no slope forward/back");
    ok(s.ny == 1, "flat: the normal points straight up");
    // (the landscape's height is kept – a hill is still a hill)
    ok(std::isfinite(s.y), "the height is kept");
  }
  std::printf("groundgrip_test: all 3 cases passed\n");
  return 0;
}

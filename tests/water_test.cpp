// SPDX-License-Identifier: AGPL-3.0-only
// LINK: game render engine world sim core platform
// Origin: tests/game/water.test.ts @ v1.7.7
/**
 * Driving through water (src/game/wading.ts): the depth from the cover map,
 * the wash, wet brakes, the air intake, the spray – and 1.7.7's bridge over
 * a river, which is not water for the wheels on its deck.  (The original's
 * vehicle is a duck-typed one; here a real Vehicle whose fields are set by hand.)
 */
#include <cmath>
#include <cstdio>
#include <memory>

#include "game/wading.h"
#include "render/water.h"
#include "sim/vehicle/index.h"
#include "tests/vehicle_harness.h"
#include "world/cover.h"

using namespace fs;
using fs::harness::ok;
using fs::harness::test;

namespace {

/** a river 30 m wide along x, from z = 0 to z = 30 */
CoverSource river() {
  return {[](double, double z) { return z >= 0 && z < 30 ? static_cast<int>(COVER_WATER) : static_cast<int>(COVER_MEADOW); }};
}

/** the same river, with the height of the ground: the water's surface at y 0 (the bank's level) */
CoverSource riverAt() {
  CoverSource s = river();
  s.heightAt = [](double, double) { return 0.0; };
  return s;
}

harness::PlaneGround plane;

const VehicleSpec& spec() {
  static const VehicleSpec* s = [] {
    auto* v = new VehicleSpec(COMPACT_CAR);
    v->wheelRadius = 0.3;
    return v;
  }();
  return *s;
}

/** a car standing (or moving) at z with its wheels 1.4 m apart across and 2.6 m along, `hy` the height of the wheel centres */
std::unique_ptr<Vehicle> carAt(double z, double speed, double hy = 0.3) {
  auto v = std::make_unique<Vehicle>(spec(), plane);
  v->pos = {5, 0, z};
  v->speed = speed;
  v->yaw = 0;
  v->engineRunning = true;
  const double xs[4] = {4.3, 5.7, 4.3, 5.7};
  const double zs[4] = {z - 1.3, z - 1.3, z + 1.3, z + 1.3};
  for (size_t i = 0; i < 4; i++) {
    v->wheels[i].hx = xs[i];
    v->wheels[i].hy = hy;
    v->wheels[i].hz = zs[i];
    v->wheels[i].contact = true;
  }
  return v;
}

}  // namespace

int main() {
  test("the bed: the drop at the edge, sloping down, no deeper than DEEP_WATER");
  ok(bedDepth(0) == WATER_DROP, "the drop");
  ok(bedDepth(2) > bedDepth(1), "sloping");
  ok(bedDepth(1000) == DEEP_WATER, "deepest");
  ok(bedDepth(-5) == WATER_DROP, "on land");

  test("the depth of a river: 0 on land, shallow at the edge, deep in the middle, without steps");
  {
    WaterDepth d(river());
    ok(d.at(5, -1) == 0, "land");
    const double edge = d.at(5, 0.5);
    const double mid = d.at(5, 15);
    ok(edge >= WATER_DROP && edge < 0.5, "edge " + jsStr(edge));
    ok(mid == DEEP_WATER, "middle");
    // deeper going in, and no jump from one 10 cm to the next
    double last = d.at(5, 0.05);
    for (double z = 0.15; z < 15; z += 0.1) {
      const double y = d.at(5, z);
      ok(y >= last - 1e-9, "deeper at " + jsStr(z));
      ok(y - last < 0.06, "a step at " + jsStr(z) + ": " + jsStr(last) + " → " + jsStr(y));
      last = y;
    }
    // the same from the other bank
    ok(std::abs(d.at(5, 29.5) - edge) < 0.2, "the other bank");
  }

  test("the wash: more with speed and with depth, none out of the water");
  ok(washRate(10, 0) == 0, "none");
  ok(washRate(0, 0.3) > 0, "standing");
  ok(washRate(10, 0.3) > washRate(3, 0.3), "speed");
  ok(washRate(5, 0.4) > washRate(5, 0.1), "depth");
  // a very dirty car through a ford at 30 km/h: clean within a few seconds
  ok(1 / washRate(8.3, 0.3) < 6, "a ford");

  test("wet brakes: weaker, dried by braking");
  ok(brakeFactor(0) == 1, "dry");
  ok(brakeFactor(1) < 0.7 && brakeFactor(1) > 0.5, "wet");
  ok(dryRate(15, 1) > 10 * dryRate(15, 0), "braking dries");
  ok(wetsBrakes(0.1, 5), "splashing");
  ok(!wetsBrakes(0.1, 0.5), "slow");
  ok(wetsBrakes(0.4, 0), "deep");

  test("the air intake: a car before a truck, a tractor high");
  ok(intakeHeight("car") < intakeHeight("truck"), "car, truck");
  ok(intakeHeight("motorcycle") <= intakeHeight("car"), "motorcycle");
  ok(intakeHeight("tractor") > intakeHeight("car"), "tractor");
  ok(intakeHeight("something new") > 0, "new");

  test("the spray: none standing still, more with speed");
  ok(sprayRate(0, 0.3) == 0, "standing");
  ok(sprayRate(10, 0) == 0, "dry");
  ok(sprayRate(10, 0.3) > sprayRate(2, 0.3), "speed");

  test("wheelDepth: the water reaches up to the ground – on a bridge not at all");
  // in the river: the wheel stands on the bed, the map's depth is the depth
  ok(wheelDepth(DEEP_WATER, 0, -DEEP_WATER) == DEEP_WATER, "on the bed");
  // 5 m above the water: nothing of the river below counts
  ok(wheelDepth(DEEP_WATER, 0, 5) == 0, "above");
  // at the bank's level: a hand's breadth at the surface
  {
    const double atSurface = wheelDepth(DEEP_WATER, 0, 0);
    ok(atSurface > 0 && atSurface < 0.1, jsStr(atSurface));
  }
  // no water there, or no height known (a tile not loaded, a source without heights): as the cover map says
  ok(wheelDepth(0, 0, -1) == 0, "no water");
  ok(wheelDepth(0.4, JS_NAN, 5) == 0.4, "no height");

  test("wading: washed in the shallows, the engine stalls in deep water, the brakes stay wet a while");
  {
    Wading w(river());
    double dirt = 1;
    // the rear wheels in the shallow edge, at speed: washed, wet brakes, spray – the engine runs on
    auto shallow = carAt(-0.8, 6);
    for (int i = 0; i < 30; i++) ok(w.step(0.05, shallow.get(), "car", dirt, 0, true).empty(), "no tip");
    ok(dirt < 0.9, "dirt " + jsStr(dirt));
    ok(shallow->engineRunning, "runs");
    ok(w.wet == 1, "wet");
    ok(w.brakeFactor() < 1, "weaker");
    ok(w.spray.count() > 0, "spray");
    // out of the water, braking: told once, then dried
    auto dry = carAt(-10, 10);
    int told = 0;
    for (int i = 0; i < 200; i++)
      if (w.step(0.05, dry.get(), "car", dirt, 1, true) == "wetBrakes") told++;
    ok(told == 1, "told once");
    ok(w.wet < 0.2, "wet " + jsStr(w.wet));
    // into the middle: the engine stalls, a tip says why
    auto deep = carAt(15, 2);
    ok(w.step(0.05, deep.get(), "car", dirt, 0, true) == "waterStalled", "stalled");
    ok(!deep->engineRunning, "dead");
    // without effects (the low preset): no spray, but the wash all the same
    Wading w2(river());
    double d2 = 1;
    auto c2 = carAt(-0.8, 6);
    w2.step(0.5, c2.get(), "car", d2, 0, false);
    ok(w2.spray.count() == 0, "no spray");
    ok(d2 < 1, "washed");
  }

  test("a bridge over the river: the engine keeps running, no wet brakes, no washing, no spray");
  {
    Wading w(riverAt());
    double dirt = 1;
    // in the middle of the river, but on the deck 5.7 m above the water
    auto bridge = carAt(15, 8, 6);
    for (int i = 0; i < 30; i++) ok(w.step(0.05, bridge.get(), "car", dirt, 0, true).empty(), "no tip");
    ok(w.depthNow == 0, "no depth");
    ok(bridge->engineRunning, "runs");
    ok(w.wet == 0, "dry");
    ok(w.brakeFactor() == 1, "brakes");
    ok(dirt == 1, "not washed");
    ok(w.spray.count() == 0, "no spray");
  }

  test("the same car with its wheels on the bed of the river still stalls");
  {
    Wading w(riverAt());
    double dirt = 1;
    // standing on the bed, 1.8 m down under the surface
    auto ford = carAt(15, 2, -1.5);
    ok(w.step(0.05, ford.get(), "car", dirt, 0, true) == "waterStalled", "stalled");
    ok(!ford->engineRunning, "dead");
    ok(w.depthNow == DEEP_WATER, "deep");
  }
  std::printf("water_test: ok\n");
  return 0;
}

// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim world core platform geo
// Origin: tests/vehicle/fleet.test.ts @ v1.7.7
/**
 * The vehicles of the other licence classes in the physics: they pull away,
 * stop at a line and pull away again without stalling (with the automatic
 * gearbox the demonstration driver uses), the tractor stays at its 40 km/h,
 * and nothing tips over in a tight bend – not even the "motorcycle", which
 * runs on the four-wheel model.
 *
 * The original walks the game's table of vehicles (src/game/vehicles.ts: VEHICLES).  Here are
 * the rows of it that are made of sim/vehicle/spec and sim/vehicle/fleet alone, with their
 * powers and governed speeds as that table has them at v1.7.7 (the rigid three-axle truck,
 * the articulated lorry and the cranes come with their own subsystems; the case about which
 * powers a licence class allows is about that table, not about the physics).
 */
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "src/sim/vehicle/fleet.h"
#include "src/sim/vehicle/index.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using namespace fs::harness;

namespace {

struct Def {
  std::string kind;
  /** its physics with the automatic gearbox, for the engine power (PS) chosen */
  std::function<VehicleSpec(double power)> spec;
  std::vector<double> powers;
  double power;
  /** its top speed by construction or governor (km/h; 0: none) */
  double governed;
};

const double BIG_BIKE_PS = peakPowerPs(MOTORCYCLE);
std::function<VehicleSpec(double)> bike(const std::string& name, double size, double finalDrive) {
  return [=](double power) { return variants(name, power / BIG_BIKE_PS, finalDrive, size).auto_; };
}

std::vector<Def> defs() {
  const auto car = [](double power) { return withPower(COMPACT_CAR_AUTOMATIC, power); };
  const auto lightTruck = [](double power) { return atPower(LIGHT_TRUCK, power); };
  const auto minibus = [](double power) { return atPower(MINIBUS, power); };
  const auto bus = [](double power) { return atPower(BUS, power); };
  return {
      {"car", car, {60, 75, 95, 115, 150, 200}, 75, 0},
      {"carTrailer", car, {60, 75, 95, 115, 150, 200}, 115, 0},
      {"moped", bike("Moped 50 cm³, 45 km/h", 0.12, 24), {3, 4, 5}, 4, 45},
      {"motorcycle125", bike("Leichtkraftrad 125 cm³", 0.3, 7.2), {11, 13, 15}, 15, 0},
      {"motorcycle35", bike("Motorrad bis 35 kW", 0.75, 5.8), {27, 35, 47}, 47, 0},
      {"motorcycle", bike("Motorrad", 1, MOTORCYCLE.finalDrive), {48, 68, 95, 120, 150}, 95, 0},
      {"lightTruck", lightTruck, {136, 170, 190, 218}, 170, 90},
      {"lightTruckTrailer", lightTruck, {136, 170, 190, 218}, 190, 90},
      {"truck", [](double power) { return atPower(TRUCK, power); }, {257, 300, 354, 408, 449}, 354, 90},
      {"minibus", minibus, {116, 129, 150, 177}, 150, 100},
      {"minibusTrailer", minibus, {116, 129, 150, 177}, 177, 100},
      {"bus", bus, {279, 321, 354, 394}, 321, 100},
      {"busTrailer", bus, {279, 321, 354, 394}, 354, 100},
      {"tractorSlow", [](double power) { return atPower(TRACTOR, power); }, {50, 75, 102, 143}, 102, 40},
      {"tractor", [](double power) { return atPower(TRACTOR_FAST, power); }, {143, 187, 231, 272}, 187, 60},
  };
}

}  // namespace

int main() {
  std::printf("fleet_test (tests/vehicle/fleet.test.ts)\n");
  int cases = 0;
  for (const Def& def : defs()) {
    // every engine one may choose: the weakest and the strongest as well as the usual one
    std::vector<double> powers;
    for (const double p : {def.powers.front(), def.power, def.powers.back()})
      if (std::find(powers.begin(), powers.end(), p) == powers.end()) powers.push_back(p);
    for (const double power : powers) {
      test((def.kind + " with " + jsStr(power) + " PS: pulls away, stops, pulls away again – and keeps to its top speed").c_str());
      const VehicleSpec spec = def.spec(power);
      ok(spec.transmission == "automatic", "spec.transmission == automatic");
      PlaneGround g;
      Vehicle v = makeCar(g, spec);
      const auto go = [&](double secs, double throttle, double brake) {
        for (int i = 0; i < secs / DT; i++) v.step(DT, DriverInput{.throttle = throttle, .brake = brake, .gear = 1});
      };
      go(6, 0.6, 0);
      // (a moped built for 45 km/h takes its time, and so does a 125 with its weakest engine – and a 37-tonne crane)
      const double brisk = def.governed > 0 && def.governed < 50 ? 10 : power < 20 || spec.mass > 30000 ? 15 : 20;
      ok(v.speed * 3.6 > brisk, "under way (" + toFixed(v.speed * 3.6, 0) + " km/h)");
      go(6, 0, 0.6);
      ok(std::abs(v.speed) < 0.1, "stopped (" + toFixed(v.speed, 2) + " m/s)");
      go(3, 0, 0.3);
      ok(std::abs(v.speed) < 0.1, "held by the brake, not crept away");
      ok(v.engineRunning == true, "the engine still runs after the stop");
      go(4, 0.5, 0);
      ok(v.speed * 3.6 > brisk / 2 - 2, "and pulls away again (" + toFixed(v.speed * 3.6, 0) + " km/h)");
      if (def.governed > 0 && def.governed < 60) {
        go(40, 1, 0);
        ok(v.speed * 3.6 < def.governed + 2, "not faster than it is built for (" + toFixed(v.speed * 3.6, 1) + " km/h)");
      }
      cases++;
    }

    test((def.kind + ": does not tip over in a tight bend").c_str());
    PlaneGround g;
    Vehicle v = makeCar(g, def.spec(def.power));
    for (int i = 0; i < 25 / DT && v.speed * 3.6 < 28; i++) v.step(DT, DriverInput{.throttle = 0.6, .gear = 1});
    double upright = 1;
    for (int i = 0; i < 6 / DT; i++) {
      v.step(DT, DriverInput{.steer = 0.6, .throttle = v.speed * 3.6 < 28 ? 0.4 : 0.0, .gear = 1});
      upright = jsMin(upright, v.upright);
      ok(std::isfinite(v.speed), "Number.isFinite(v.speed)");
    }
    ok(upright > 0.9, "upright (" + toFixed(upright, 2) + ")");
    cases++;
  }
  std::printf("fleet_test: all %d cases passed\n", cases);
  return 0;
}

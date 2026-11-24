// LINK: game sim render i18n audio input engine world core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Arcade driving and the road physics switched off: D and R by the pedals,
 * the vehicle held at a standstill (on a hill too), the engine started by
 * itself – and "Reset settings" keeping the language.
 *
 * Origin: tests/game/arcadedrive.test.ts @ v1.7.7 (its case about the grip of every surface
 * is sim/ground's).
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>

#include "core/js.h"
#include "game/arcadedrive.h"
#include "game/settings.h"
#include "platform/plat.h"
#include "sim/vehicle/index.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using fs::harness::DT;
using fs::harness::PlaneGround;

/** what arcade driving wrote into the input */
static bool is(const DriverInput& d, double throttle, double brake, int gear) { return d.throttle == throttle && d.brake == brake && d.gear == gear; }

/** A car with the automatic gearbox driven the arcade way for `seconds`, by the pedals `pedals(t)`. */
struct ArcadeRun {
  Vehicle v;
  ArcadeDrive a;
  double z0;
};
static ArcadeRun arcadeRun(PlaneGround& ground, double seconds, const std::function<std::pair<double, double>(double t)>& pedals) {
  ArcadeRun r{harness::makeCar(ground, COMPACT_CAR_AUTOMATIC, false), ArcadeDrive(), 0};
  r.z0 = r.v.pos[2];
  DriverInput d;
  d.ignition = false;
  const int n = static_cast<int>(jsRound(seconds / DT));
  for (int i = 0; i < n; i++) {
    const auto [thr, brk] = pedals(i * DT);
    r.a.step(thr, brk, r.v.speed, DT, d, 1, r.v.engineRunning);
    r.v.step(DT, d);
  }
  return r;
}

int main() {
  // the right pedal drives forwards, the left one brakes
  {
    ArcadeDrive a;
    DriverInput d;
    a.step(0.8, 0, 5, 0.1, d);
    assert(is(d, 0.8, 0, 1) && d.handbrake == 0 && d.clutch == 0 && d.ignition == true);
    a.step(0, 0.6, 5, 0.1, d);
    assert(is(d, 0, 0.6, 1));
    // (the limiter's cap on the right pedal)
    a.step(1, 0, 5, 0.1, d, 0.3);
    assert(d.throttle == 0.3);
  }

  // standing, the left pedal held a moment puts it in reverse – and the right one back into D
  {
    ArcadeDrive a;
    DriverInput d;
    // moving: the left pedal only brakes, however long
    for (double t = 0; t < 2; t += 0.05) a.step(0, 1, 3, 0.05, d);
    assert(a.dir == 1);
    // standing: held for less than the moment – still D, held where it is
    a.step(0, 1, 0, ARCADE_SWITCH / 2, d);
    assert(a.dir == 1);
    assert(d.handbrake == 1 && d.brake == 1);
    a.step(0, 1, 0, ARCADE_SWITCH / 2 + 0.01, d);
    assert(a.dir == -1);
    // reverse: the left pedal drives (backwards), the right one brakes
    assert(is(d, 1, 0, -1) && d.handbrake == 0);
    a.step(0.7, 0, -2, 0.1, d);
    assert(is(d, 0, 0.7, -1));
    // standing again, the right pedal held: forwards
    a.step(1, 0, -ARCADE_STILL / 2, ARCADE_SWITCH + 0.01, d);
    assert(a.dir == 1);
    assert(d.gear == 1 && d.throttle == 1);
    a.reset();
    assert(a.dir == 1);
  }

  // no pedal at a standstill: held; the engine is started when it stands still
  {
    ArcadeDrive a;
    DriverInput d;
    a.step(0, 0, 0, 0.1, d, 1, false);
    assert(d.handbrake == 1);
    assert(d.starter == true);
    a.step(0.5, 0, 0, 0.1, d, 1, true);
    assert(d.handbrake == 0);
    assert(d.starter == false);
    assert(arcadeTransmission(true, "manual") == "automatic");
    assert(arcadeTransmission(false, "manual") == "manual");
  }

  // the car on a 12 % hill stays where it is with no pedal, and starts its engine by itself
  {
    PlaneGround hill(0.12);
    const ArcadeRun r = arcadeRun(hill, 6, [](double) { return std::pair<double, double>(0, 0); });
    assert(r.v.engineRunning == true);
    assert(std::fabs(r.v.pos[2] - r.z0) < 0.15);
    assert(std::fabs(r.v.speed) < 0.05);
  }

  // the car drives off forwards, stops, and backs away with the left pedal held
  {
    // 0–4 s right pedal, 4–8 s left pedal (brakes, then reverses)
    PlaneGround level(0);
    const ArcadeRun r = arcadeRun(level, 9, [](double t) { return t < 4 ? std::pair<double, double>(0.6, 0) : std::pair<double, double>(0, 0.7); });
    assert(r.a.dir == -1);
    assert(r.v.speed < -1);
  }

  // arcade driving is off by default, the even grip on (1.7.1); "Reset settings" keeps the language and the keys
  {
    plat::localStorage().removeItem("fahrsim-baunatal.settings.v1");
    assert(DEFAULT_SETTINGS().arcadeDrive == false);
    assert(DEFAULT_SETTINGS().evenRoads == true);
    plat::localStorage().setItem("fahrsim-baunatal.settings.v2", "{\"version\":" + std::to_string(SETTINGS_VERSION) + ",\"arcadeDrive\":\"yes\",\"evenRoads\":false}");
    Settings s = loadSettings();
    assert(s.arcadeDrive == false);   // implausible: the default
    assert(s.evenRoads == false);     // chosen since 1.7.1: kept
    s.lang = "de";
    s.fuelKey = "key";
    s.limiter = false;
    s.traffic = 0.1;
    s.graphics.water = !DEFAULT_SETTINGS().graphics.water;
    Settings r = resetSettings(s);
    assert(r.lang == "de");
    assert(r.fuelKey == "key");
    assert(r.limiter == DEFAULT_SETTINGS().limiter);
    assert(r.traffic == DEFAULT_SETTINGS().traffic);
    assert(r.evenRoads == DEFAULT_SETTINGS().evenRoads);
    assert(r.graphics.water == DEFAULT_SETTINGS().graphics.water);
    // (the defaults themselves are not touched)
    r.graphics.water = !r.graphics.water;
    assert(r.graphics.water != DEFAULT_SETTINGS().graphics.water);
    plat::localStorage().removeItem("fahrsim-baunatal.settings.v2");
  }

  std::printf("game_arcadedrive_test: ok\n");
  return 0;
}

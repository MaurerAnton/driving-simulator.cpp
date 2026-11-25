// LINK: game core platform
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The wipers and the windscreen: the stalk's places, the motor that parks
 * before it stops, intermittent sweeps and the washer's sweeps after it is
 * let go; rain, snow and dirt building up on the glass, and the blades
 * clearing what they pass over (wet: clean; dry: smeared).
 *
 * Origin: tests/game/wipers.test.ts @ v1.7.7
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>

#include "game/wipers.h"

using namespace fs;

static const ScreenWeather DRY = {.rain = 0, .snow = 0, .temperature = 10, .roadWet = 0, .speed = 0, .crossWind = 0, .heater = false};

static ScreenWeather with(std::function<void(ScreenWeather&)> f) {
  ScreenWeather w = DRY;
  f(w);
  return w;
}

static const ScreenWeather RAIN = with([](ScreenWeather& w) {
  w.rain = 1;
  w.roadWet = 1;
});
static const ScreenWeather SNOW = with([](ScreenWeather& w) {
  w.snow = 1;
  w.temperature = -3;
});

static void run(const std::function<void(double)>& fn, double seconds, double dt = 1.0 / 30) {
  for (double t = 0; t < seconds; t += dt) fn(dt);
}

int main() {
  // the stalk goes round its four places
  {
    assert((WIPER_MODES == std::vector<WiperMode>{"off", "intermittent", "normal", "fast"}));
    assert(nextWiperMode("off") == "intermittent");
    assert(nextWiperMode("fast") == "off");
    Wipers w;
    assert(w.cycle() == "intermittent");
    assert(w.cycle() == "normal");
    assert(w.cycle() == "fast");
    assert(w.cycle() == "off");
  }

  // a sweep goes up to the far end and back
  {
    assert(sweepAt(0) == 0);
    assert(std::abs(sweepAt(0.5) - 1) < 1e-9);
    assert(sweepAt(0.99) < 0.01);
  }

  // off: the blades stay parked; switched off mid-sweep they finish it first
  {
    Wipers w;
    run([&](double dt) { w.step(dt, false); }, 3);
    assert(w.phase == 0);
    assert(w.swept.any == false);
    w.set("normal");
    run([&](double dt) { w.step(dt, false); }, SWEEP_SECONDS.normal * 0.3);
    assert(w.running());
    w.set("off");
    w.step(0.01, false);
    assert(w.running());   // still on its way back
    run([&](double dt) { w.step(dt, false); }, SWEEP_SECONDS.normal);
    assert(w.phase == 0);
    assert(w.running() == false);
  }

  // fast sweeps more often than normal; the far end is reached on each sweep
  {
    const auto count = [](const WiperMode& mode) {
      Wipers w;
      w.set(mode);
      int sweeps = 0;
      double top = 0;
      run(
          [&](double dt) {
            const double before = w.phase;
            w.step(dt, false);
            top = std::max(top, w.swept.hi);
            if (w.phase < before) {
              // a sweep is done: it went all the way up
              assert(top == 1);
              top = 0;
              sweeps++;
            }
          },
          10);
      return sweeps;
    };
    const int n = count("normal");
    const int f = count("fast");
    assert(n >= 7 && n <= 9);
    assert(f > n + 3);
    std::printf("sweeps in 10 s: normal %d, fast %d\n", n, f);
  }

  // intermittent: one sweep, then a pause
  {
    Wipers w;
    w.set("intermittent");
    int starts = 0;
    bool was = false;
    run(
        [&](double dt) {
          w.step(dt, false);
          if (w.running() && !was) starts++;
          was = w.running();
        },
        2 * (SWEEP_SECONDS.normal + INTERVAL_PAUSE) + 0.5);
    assert(starts == 3);
  }

  // the washer runs the wipers while held and for two sweeps after
  {
    Wipers w;
    run([&](double dt) { w.step(dt, true); }, 0.5);
    assert(w.running());
    double parkedAt = -1;
    double t = 0;
    run(
        [&](double dt) {
          t += dt;
          w.step(dt, false);
          if (parkedAt < 0 && !w.running()) parkedAt = t;
        },
        6);
    assert(parkedAt > SWEEP_SECONDS.normal * 1.5 && parkedAt < SWEEP_SECONDS.normal * 3.2);
  }

  // the blades reach the driver's view but not the top corners
  {
    Windscreen s(1.3, 0.85);
    assert(tandemArms(1.3, 0.85).size() == 2);
    assert(s.wiped(s.cellAt(0.4, 0.45)));    // in front of the driver
    assert(s.wiped(s.cellAt(0.95, 0.35)));   // the right side
    assert(!s.wiped(s.cellAt(1.29, 0.84)));  // the top right corner
  }

  // rain builds up into a sheet of water that blurs the view; the wipers keep it clear
  {
    Windscreen s(1.3, 0.85);
    run([&](double dt) { s.step(dt, RAIN); }, 30);
    const size_t k = static_cast<size_t>(s.cellAt(0.4, 0.45));
    assert(s.drops[k] > 0.9);
    assert(s.film[k] > 0.6);
    assert(s.obscured() > 0.5);
    s.wipers.set("normal");
    run([&](double dt) { s.step(dt, RAIN); }, 5);
    // (between two sweeps a little rain lies again)
    assert(s.obscured() < 0.25);
    assert(s.film[k] < 0.2);
  }

  // driving fast catches more rain and makes the drops run
  {
    Windscreen a(1.3, 0.85);
    Windscreen b(1.3, 0.85);
    const ScreenWeather light = with([](ScreenWeather& w) {
      w = RAIN;
      w.rain = 0.3;
    });
    const ScreenWeather fast = with([&](ScreenWeather& w) {
      w = light;
      w.speed = 30;
      w.crossWind = 4;
    });
    run([&](double dt) { a.step(dt, light); }, 5);
    run([&](double dt) { b.step(dt, fast); }, 5);
    assert(b.drops[0] > a.drops[0]);
    assert(a.running == 0);
    assert(b.running > 0.9);
    assert(b.flow.y > 1);
    assert(b.flow.x > 0);
  }

  // snow builds up white unless wiped; it melts above freezing
  {
    Windscreen s(1.3, 0.85);
    run([&](double dt) { s.step(dt, SNOW); }, 90);
    assert(s.obscured() > 0.85);
    const size_t k = static_cast<size_t>(s.cellAt(0.4, 0.45));
    s.wipers.set("fast");
    run([&](double dt) { s.step(dt, SNOW); }, 2);
    assert(s.snow[k] < 0.15);
    // the corner the blades miss stays white
    const size_t c = static_cast<size_t>(s.cellAt(1.29, 0.84));
    assert(s.snow[c] > 0.85);
    s.wipers.set("off");
    const ScreenWeather warm = with([](ScreenWeather& w) { w.temperature = 8; });
    run([&](double dt) { s.step(dt, warm); }, 60);
    assert(s.snow[c] < 0.2);
  }

  // dirt: wiped dry it smears, washed it goes
  {
    Windscreen s(1.3, 0.85);
    const size_t k = static_cast<size_t>(s.cellAt(0.4, 0.45));
    std::fill(s.dirt.begin(), s.dirt.end(), 0.5f);
    s.wipe(0, 1);
    assert(s.smear[k] > 0.1);   // smeared
    assert(s.dirt[k] == 0.5f);
    // the washer held for a second, then its sweeps
    run([&](double dt) { s.step(dt, DRY, true); }, 1);
    run([&](double dt) { s.step(dt, DRY); }, 4);
    assert(s.dirt[k] < 0.1);
    assert(s.smear[k] < 0.05);
    assert(s.film[k] < 0.2);   // and the washer's water wiped off
  }

  // the spray of a wet road dirties the glass slowly
  {
    Windscreen s(1.3, 0.85);
    const ScreenWeather spray = with([](ScreenWeather& w) {
      w.roadWet = 1;
      w.speed = 20;
    });
    run([&](double dt) { s.step(dt, spray); }, 120);
    assert(s.dirt[0] > 0.1 && s.dirt[0] < 0.4);
  }

  std::printf("game_wipers_test: ok\n");
  return 0;
}

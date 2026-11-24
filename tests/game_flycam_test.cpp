// LINK: game core platform
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The flying free camera (game/flycam.h): it flies the way it looks, to the
 * side, up and down; the wheel sets its speed in steps; it keeps above the
 * ground.
 *
 * Origin: tests/game/flycam.test.ts @ v1.7.7
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>

#include "core/js.h"
#include "game/flycam.h"

using namespace fs;

static double flat(double, double) { return 0; }

/** which keys are held (1: held), and whether fast */
struct Held {
  double keyFwd = 0;
  double keyRight = 0;
  double keyUp = 0;
  bool fast = false;
};

/** `secs` seconds with the keys held as `held` asks (key time = dt each picture) */
static void fly(FlyCam& c, double secs, const Held& held, const std::function<double(double, double)>& ground = flat) {
  const double dt = 1.0 / 50;
  for (int i = 0; i < secs * 50; i++) {
    FlyInput t;
    t.keyFwd = held.keyFwd * dt;
    t.keyRight = held.keyRight * dt;
    t.keyUp = held.keyUp * dt;
    t.fast = held.fast;
    c.update(dt, t, ground);
  }
}

int main() {
  // forwards the way it looks: north, and down when it looks down
  {
    FlyCam c;
    c.start(0, 50, 0, 0, 0, -1);
    fly(c, 2, {.keyFwd = 1});
    assert(c.z < -15 && std::fabs(c.x) < 1e-6 && std::fabs(c.y - 50) < 1e-6);
    FlyCam d;
    d.start(0, 50, 0, 0, -1, -1);
    fly(d, 2, {.keyFwd = 1});
    assert(d.y < 40 && d.z < -10);
  }

  // to the side and straight up; shift: four times as fast
  {
    FlyCam c;
    c.start(0, 20, 0, 0, 0, -1);
    fly(c, 2, {.keyRight = 1});
    assert(c.x > 15 && std::fabs(c.z) < 1e-6);
    FlyCam u;
    u.start(0, 20, 0, 0, 0, -1);
    fly(u, 2, {.keyUp = 1});
    assert(u.y > 35);
    FlyCam a;
    FlyCam b;
    a.start(0, 100, 0, 0, 0, -1);
    b.start(0, 100, 0, 0, 0, -1);
    fly(a, 3, {.keyFwd = 1});
    fly(b, 3, {.keyFwd = 1, .fast = true});
    assert(std::fabs(b.z / a.z - 4) < 0.2);
  }

  // the wheel: speed in steps of √2, within its bounds
  {
    FlyCam c;
    const double s0 = c.speed();
    FlyInput t;
    t.speed = 2;
    assert(c.update(0.02, t, flat) == true);
    assert(std::fabs(c.speed() / s0 - 2) < 1e-9);
    assert(c.update(0.02, FlyInput(), flat) == false);
    t.speed = 100;
    c.update(0.02, t, flat);
    assert(c.step == FLY_MAX_STEP);
    t.speed = -100;
    c.update(0.02, t, flat);
    assert(c.speed() >= 1.5 && c.speed() < 1.6);
  }

  // it keeps above the ground – and flies on where nothing is loaded
  {
    FlyCam c;
    c.start(0, 5, 0, 0, -1, 0);
    fly(c, 3, {.keyFwd = 1}, [](double, double) { return 2.0; });
    assert(std::fabs(c.y - 2.5) < 1e-6);
    FlyCam n;
    n.start(0, 5, 0, 0, 0, -1);
    fly(n, 1, {.keyFwd = 1}, [](double, double) { return JS_NAN; });
    assert(n.z < -5 && std::isfinite(n.z));
  }

  std::printf("game_flycam_test: ok\n");
  return 0;
}

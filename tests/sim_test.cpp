// SPDX-License-Identifier: AGPL-3.0-only
/** Traffic signals and scenery collisions. */
#include "../src/sim/collision.h"
#include "../src/sim/signals.h"
#include <cassert>
#include <cstdio>

using namespace fs;

namespace {
SignalControllerJson makeController() {
  SignalControllerJson c;
  c.groups = 2;
  c.off = 0;
  // 30 s cycle: green 15, amber 3, red 6, red+amber 4 (per group, offset)
  SignalPhaseJson p;
  p.d = 15; p.s = "GG";
  c.phases.push_back(p);
  p.d = 3; p.s = "AA";
  c.phases.push_back(p);
  p.d = 6; p.s = "RR";
  c.phases.push_back(p);
  p.d = 4; p.s = "YY";
  c.phases.push_back(p);
  return c;
}

/** Level ground, so the collision maths is isolated from the suspension. */
class Flat : public Ground {
 public:
  void sample(double x, double z, double yRef, GroundSample& out) override {
    (void)x; (void)z; (void)yRef;
    out.hit = true; out.y = 0; out.nx = 0; out.ny = 1; out.nz = 0;
    out.grip = 1; out.rolling = 1;
  }
};
}  // namespace

int main() {
  // --- signals: the German sequence runs in order ---------------------------
  {
    std::vector<SignalControllerJson> cs{makeController()};
    SignalSystem sys(cs);
    assert(sys.get(0, 0) == SIG_GREEN);
    // after 15 s the group turns amber
    for (int i = 0; i < 15 * 60; i++) sys.update(1.0 / 60);
    assert(sys.get(0, 0) == SIG_AMBER);
    // then red
    for (int i = 0; i < 3 * 60; i++) sys.update(1.0 / 60);
    assert(sys.get(0, 0) == SIG_RED);
    // then red+amber
    for (int i = 0; i < 6 * 60; i++) sys.update(1.0 / 60);
    assert(sys.get(0, 0) == SIG_RED_AMBER);
    // then green again (30 s cycle)
    for (int i = 0; i < 4 * 60; i++) sys.update(1.0 / 60);
    assert(sys.get(0, 0) == SIG_GREEN);
    // the age counter resets on every change
    assert(sys.age(0, 0) < 1.0 / 60 + 1e-9);
    // a controller index out of range falls back to green (as the JS does)
    assert(sys.get(99, 0) == SIG_GREEN);
    assert(sys.age(99, 0) == 0);
  }

  // --- collision: the car is pushed out of a wall and loses speed -----------
  {
    Flat g;
    const VehicleSpec& spec = compactCar();
    Vehicle v(spec, g);
    v.teleport(0, 0, 0, 0);  // facing north (-z)
    v.resetDrivetrain(true);
    DriverInput in;
    in.ignition = true;
    in.gear = 1;
    in.clutch = 0;
    in.throttle = 1;
    for (int i = 0; i < 1200; i++) v.step(1.0 / 240, in);
    double before = v.speed;
    assert(before > 4);

    // a wall running east-west 6 m north of the car
    SceneryCollider col;
    double seg[] = {-20, -6, 20, -6, 0};
    CollisionSource src;
    src.key = "wall";
    src.segments = seg;
    src.segmentCount = 1;
    col.add(src);

    double impact = 0;
    for (int i = 0; i < 1200 && v.pos[2] > -5; i++) {
      v.step(1.0 / 240, in);
      impact = std::max(impact, col.resolve(v));
    }
    assert(impact > 1);                       // it hit something
    assert(v.pos[2] > -6.0);                  // never went through the wall
    assert(v.speed < before);                 // and lost speed
    double after = v.speed;

    // removing the tile lets the car drive on
    col.remove("wall");
    for (int i = 0; i < 600; i++) {
      v.step(1.0 / 240, in);
      col.resolve(v);
    }
    assert(v.pos[2] < -6.0);                  // passed the wall's line
    assert(v.speed > after);
  }

  std::printf("sim: all checks passed (signal sequence, scenery collision)\n");
  return 0;
}

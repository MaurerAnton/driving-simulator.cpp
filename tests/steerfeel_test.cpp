// LINK: input sim world core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
// Origin: tests/vehicle/steerfeel.test.ts @ v1.7.7
/**
 * How the keys and the steering bar steer (input/steerfeel.h): a car as
 * before; the truck and the bus with a slow steering wheel and gentle turns
 * at speed – driven with the real vehicles, the turning rate measured.
 *
 * (The original's third case drives the articulated lorry – sim/semitrailer.ts,
 * with TRACTOR_UNIT and SEMI_TRAILER –, which is not part of 2.0.0: it is not
 * here, and the lorries of the second case are the truck and the bus.)
 */
#include <cmath>
#include <string>

#include "input/steerfeel.h"
#include "sim/vehicle/fleet.h"
#include "sim/vehicle/index.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using namespace fs::harness;

namespace {

const double TO_DEG = 180 / std::numbers::pi;

struct Turn {
  /** seconds from the first touch to the lock asked for */
  double toLock = 0;
  /** the fastest the steering wheel turned (°/s) */
  double wheelSpeed = 0;
  /** yaw rate after 6 s (°/s) and lateral acceleration (m/s²) */
  double yawRate = 0;
  double lateral = 0;
  /** the steering input at the end */
  double steer = 0;
};

/** Drive at `speed` (m/s), then hold the bar (or the key) fully left for 6 s. */
Turn turn(const VehicleSpec& spec, double speed, const std::string& how) {
  PlaneGround g;
  Vehicle v = makeCar(g, spec);
  v.vel[2] = -speed;
  const SteerFeel f = steerFeel(spec);
  double steer = 0;
  double toLock = -1;
  double wheelSpeed = 0;
  const int steps = static_cast<int>(jsRound(7 / DT));
  for (int i = 0; i < steps; i++) {
    const double time = i * DT;
    const double before = steer;
    if (time >= 1) {
      steer = how == "touch" ? analogSteer(f, steer, -1, v.speed, DT, 4) : keySteer(f, steer, -1, v.speed, 0, DT);
      const double want = -lockFor(f, how == "touch" ? 7 : 2.4 + 3.4 / (1 + std::pow(v.speed / 15, 2)), std::abs(v.speed));
      if (toLock < 0 && std::abs(steer - want) < 0.01) toLock = time - 1;
    }
    wheelSpeed = jsMax(wheelSpeed, (std::abs(steer - before) / DT) * spec.maxWheelAngle * spec.steeringRatio * TO_DEG);
    DriverInput inp;
    inp.gear = 1;
    inp.steer = steer;
    inp.throttle = jsMin(1, jsMax(0, (speed - v.speed) * 0.5));
    inp.brake = jsMin(1, jsMax(0, (v.speed - speed) * 0.3));
    v.step(DT, inp);
  }
  return Turn{toLock, wheelSpeed, std::abs(v.angVel[1]) * TO_DEG, std::abs(v.angVel[1] * v.speed), steer};
}

}  // namespace

int main() {
  test("a car steers as before: no limit on the steering speed, a car's lateral acceleration");
  {
    const SteerFeel f = steerFeel(COMPACT_CAR_AUTOMATIC);
    FS_OK(f.lateral == 1);
    FS_OK(f.rate == JS_INF);
    // the bar winds the lock on within a quarter of a second, as it always did
    const Turn r = turn(COMPACT_CAR_AUTOMATIC, 3, "touch");
    ok(r.toLock > 0.2 && r.toLock < 0.3, jsStr(r.toLock));
  }

  test("the lorries: a slow steering wheel, gentle turns at speed");
  for (const VehicleSpec* spec : {&TRUCK, &BUS}) {
    const SteerFeel f = steerFeel(*spec);
    ok(std::abs(f.lateral - LORRY_LATERAL) < 1e-9, spec->name);
    // the steering wheel at no more than about 1.4 turns a second
    ok(f.rate * spec->maxWheelAngle * spec->steeringRatio * TO_DEG <= 501, spec->name);
  }

  test("the steering sensitivity: a sharper feel asks for more lock, never past full lock");
  {
    const double speed = 8;   // m/s – a town turn, where the keys wind on meaningful lock
    const double gentle = keySteer(steerFeel(COMPACT_CAR_AUTOMATIC, 2.63, 0.576, 0.5), 0, 1, speed, 0, 0.5);
    const double asBuilt = keySteer(steerFeel(COMPACT_CAR_AUTOMATIC, 2.63, 0.576, 1), 0, 1, speed, 0, 0.5);
    const double sharp = keySteer(steerFeel(COMPACT_CAR_AUTOMATIC, 2.63, 0.576, 2), 0, 1, speed, 0, 0.5);
    // more sensitivity -> more lock for the same input at the same speed
    ok(gentle < asBuilt, jsStr(gentle) + " < " + jsStr(asBuilt));
    ok(asBuilt < sharp, jsStr(asBuilt) + " < " + jsStr(sharp));
    // but never more than full lock, however sharp
    const double full = keySteer(steerFeel(COMPACT_CAR_AUTOMATIC, 2.63, 0.576, 8), 0, 1, speed, 0, 0.5);
    ok(full <= 1, "never past full lock: " + jsStr(full));
    // the default feel (sens 1) is unchanged by building one without a sens
    FS_OK(steerFeel(COMPACT_CAR_AUTOMATIC).sens == 1);
  }

  std::printf("steerfeel_test: ok\n");
  return 0;
}

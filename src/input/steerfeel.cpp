// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/input/steerfeel.ts @ v1.7.7
#include "input/steerfeel.h"

#include <cmath>

namespace fs {

namespace {

/** below this mass (kg) a vehicle steers like a car … */
const double LIGHT = 3000;
/** … from this one on like a lorry */
const double HEAVY = 7500;
/** … and the driver of a vehicle just above LIGHT (rad/s) */
const double LIGHT_WHEEL_SPEED = (1400 * std::numbers::pi) / 180;

double clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
double approach(double cur, double target, double maxStep) { return std::abs(target - cur) <= maxStep ? target : cur + jsSign(target - cur) * maxStep; }

}  // namespace

double heaviness(double mass) {
  const double x = clamp((mass - LIGHT) / (HEAVY - LIGHT), 0, 1);
  return x * x * (3 - 2 * x);
}

SteerFeel steerFeel(const SteerSpec* spec, double wheelbase, double maxWheelAngle, double sens) {
  if (!spec) return SteerFeel{wheelbase, maxWheelAngle, 1, JS_INF, sens};
  const double h = heaviness(spec->mass);
  if (h <= 0) return SteerFeel{spec->wheelbase, spec->maxWheelAngle, 1, JS_INF, sens};
  // the steering wheel's angle from the middle to the stop (rad)
  const double lock = jsMax(0.1, spec->maxWheelAngle * spec->steeringRatio);
  const double wheelSpeed = LIGHT_WHEEL_SPEED + (LORRY_WHEEL_SPEED - LIGHT_WHEEL_SPEED) * h;
  return SteerFeel{spec->wheelbase, spec->maxWheelAngle, 1 + (LORRY_LATERAL - 1) * h, wheelSpeed / lock, sens};
}

double lockFor(const SteerFeel& f, double lateral, double speed) {
  return clamp((lateral * f.lateral * f.wheelbase) / (jsMax(speed * speed, 1) * f.maxWheelAngle), 0.003, 1);
}

double analogSteer(const SteerFeel& f, double cur, double want, double speed, double dt, double quick) {
  const double limit = lockFor(f, ANALOG_LATERAL, std::abs(speed));
  return approach(cur, want * limit, dt * jsMin(jsMax(quick * limit, 0.05), f.rate));
}

double keySteer(const SteerFeel& f, double cur, double target, double speed, double selfAlign, double dt) {
  const double absSpeed = std::abs(speed);
  // (the sensitivity scales how much lock the keys ask for at this speed; the
  //  lock itself is still capped at 1 – a sharper feel never asks past full lock)
  const double limit = clamp(lockFor(f, keyLateral(absSpeed), absSpeed) * f.sens, 0.003, 1);
  if (target != 0) {
    const double rate = jsMin(limit * (1.2 + 1.3 * (1 - limit)), f.rate);
    // counter-steering is quicker than winding on more lock
    if (jsSign(cur) == -target) cur = approach(cur, 0, dt * jsMin(jsMax(1.6 * limit, std::abs(cur) * 4), f.rate * 1.3));
    return approach(cur, target * limit, dt * rate);
  }
  const double centring = clamp(absSpeed / 6, 0, 1) * jsMax((1.4 + std::abs(selfAlign) * 2.5) * jsMax(limit, 0.15), std::abs(cur) * 3);
  return approach(cur, 0, dt * jsMin(centring, f.rate * 1.5));
}

}  // namespace fs

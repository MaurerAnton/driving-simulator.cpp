// SPDX-License-Identifier: AGPL-3.0-only
// Origin: tests/vehicle/harness.ts @ v1.7.7
/** Shared helpers for the vehicle dynamics tests (and what every mirrored test file uses to say what failed). */
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <type_traits>

#include "src/core/geom.h"
#include "src/core/js.h"
#include "src/sim/jsmath.h"
#include "src/sim/vehicle/index.h"

namespace fs::harness {

inline const double DT = 1.0 / 240;

/** assert.ok(cond, message): says what failed, and ends the test program */
inline void ok(bool cond, const std::string& what) {
  if (cond) return;
  std::fprintf(stderr, "FAILED: %s\n", what.c_str());
  std::exit(1);
}
#define FS_OK(cond) ::fs::harness::ok((cond), std::string(#cond) + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ")")

inline void between(double v, double lo, double hi, const std::string& what) {
  ok(v >= lo && v <= hi, what + ": " + toFixed(v, 3) + " not in [" + jsStr(lo) + ", " + jsStr(hi) + "]");
}

/** the name of the case that runs (as node's test runner prints it) */
inline void test(const char* name) { std::printf("  %s\n", name); }

/** Inclined plane rising towards north (-Z) with the given grade (0.15 = 15 %). */
class PlaneGround : public Ground {
 public:
  double grade;
  double grip;
  double rolling;
  /** optional step: ground is `stepHeight` higher for z < stepZ */
  double stepZ = -JS_INF;
  double stepHeight = 0;

  explicit PlaneGround(double grade_ = 0, double grip_ = 1, double rolling_ = 1) : grade(grade_), grip(grip_), rolling(rolling_) {}

  void sample(double, double z, double, GroundSample& out) override {
    const double l = jsHypot(1, grade);
    out.y = -z * grade + (z < stepZ ? stepHeight : 0);
    out.nx = 0;
    out.ny = 1 / l;
    out.nz = grade / l;
    out.grip = grip;
    out.rolling = rolling;
  }
};

/** input({...}): write `DriverInput{.throttle = 1, .gear = 1}` (the fields in their order: steer, throttle, brake, clutch, handbrake, gear, ignition, starter) */
inline DriverInput input() { return DriverInput{}; }

inline Vehicle makeCar(Ground& ground, const VehicleSpec& spec = COMPACT_CAR, bool running = true) {
  Vehicle v(spec, ground);
  v.teleport(0, 0, 0, 0);
  v.resetDrivetrain(running);
  return v;
}

namespace detail {
template <typename D>
DriverInput drive(D& d, double t, Vehicle& v) {
  if constexpr (std::is_invocable<D&, double, Vehicle&>::value) return d(t, v);
  else if constexpr (std::is_invocable<D&, double>::value) return d(t);
  else return d();
}
template <typename E>
void each(E& e, double t, Vehicle& v) {
  if constexpr (std::is_invocable<E&, double, Vehicle&>::value) e(t, v);
  else if constexpr (std::is_invocable<E&, double>::value) e(t);
  else e();
}
}  // namespace detail

/** Run for `seconds`, calling `drive` before every step to obtain the inputs. */
template <typename D>
void run(Vehicle& v, double seconds, D&& drive) {
  const int n = static_cast<int>(jsRound(seconds / DT));
  for (int i = 0; i < n; i++) {
    const double t = i * DT;
    v.step(DT, detail::drive(drive, t, v));
  }
}
template <typename D, typename E>
void run(Vehicle& v, double seconds, D&& drive, E&& each) {
  const int n = static_cast<int>(jsRound(seconds / DT));
  for (int i = 0; i < n; i++) {
    const double t = i * DT;
    v.step(DT, detail::drive(drive, t, v));
    detail::each(each, t + DT, v);
  }
}

inline double kmh(const Vehicle& v) { return v.speed * 3.6; }

/** Idealised test driver: launch, full throttle, shift at the given rpm. */
class sprintDriver {
 public:
  explicit sprintDriver(double shiftRpm_ = 6200) : shiftRpm(shiftRpm_) {}
  DriverInput operator()(double t, Vehicle& v) {
    if (t < 0.25) return DriverInput{.throttle = 0.55, .clutch = 1, .gear = 1};
    if (shiftT >= 0) {
      if (t - shiftT < 0.25) return DriverInput{.throttle = 0, .clutch = 1, .gear = gear};
      shiftT = -1;
    }
    if (v.rpm > shiftRpm && gear < 6) {
      gear++;
      shiftT = t;
      return DriverInput{.throttle = 0, .clutch = 1, .gear = gear};
    }
    // slip the clutch off the line, then full power
    const double launch = t < 1.6 ? jsMax(0, 0.62 - (t - 0.25) * 0.5) : 0;
    return DriverInput{.throttle = 1, .clutch = launch, .gear = gear};
  }

 private:
  double shiftRpm;
  int gear = 1;
  double shiftT = -1;
};

}  // namespace fs::harness

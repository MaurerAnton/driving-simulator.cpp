// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/input/steerfeel.ts @ v1.7.7
/**
 * How the steering devices that are not a real steering wheel – the keys,
 * the steering bar on the screen and a gamepad's stick – move the vehicle's
 * steering wheel (input/input.h).  A key is all or nothing and a finger on
 * the bar moves much faster than hands on a wheel, so the input layer winds
 * the lock on for the driver: no more than the vehicle can sensibly take at
 * this speed (a lateral acceleration, v²/r with r = wheelbase / angle), and
 * no faster than a driver's hands turn its steering wheel.
 *
 * A car is light, sits low and has a quick, light steering: a little over
 * a turn and a half from the middle to the stop, turned as fast as one
 * likes.  A heavy vehicle is different on both counts:
 *
 *  - its steering is slow (ratio ~20:1, two and a quarter turns from the
 *    middle to the stop on a tractor unit) and, even with power steering,
 *    is turned hand over hand at about one and a half turns a second;
 *  - its centre of mass is high – a lorry tips over at 3.5…4 m/s² of lateral
 *    acceleration, an articulated lorry's trailer sooner – so lorry drivers
 *    keep well under half of what a car driver uses: through a junction at
 *    15 km/h on a circle of 10…12 m, round a motorway bend at 1…1.5 m/s².
 *
 * Both are scaled with the vehicle's mass from a car's (unchanged: nothing
 * changes up to 3 t) to a lorry's (from 7.5 t on).  A real steering wheel
 * (a gamepad of kind 'wheel') is not shaped at all: the driver's hands are.
 */
#pragma once

#include <numbers>

#include "core/js.h"

namespace fs {

/** What the shaping needs of a vehicle. */
struct SteerSpec {
  double mass = 0;
  double wheelbase = 0;
  double maxWheelAngle = 0;
  double steeringRatio = 0;

  SteerSpec() = default;
  SteerSpec(double mass_, double wheelbase_, double maxWheelAngle_, double steeringRatio_)
      : mass(mass_), wheelbase(wheelbase_), maxWheelAngle(maxWheelAngle_), steeringRatio(steeringRatio_) {}
  /** (the original's interface is met by any vehicle's spec: a VehicleSpec is handed in as it is) */
  template <class V>
    requires requires(const V& v) {
      v.mass;
      v.wheelbase;
      v.maxWheelAngle;
      v.steeringRatio;
    }
  SteerSpec(const V& v) : mass(v.mass), wheelbase(v.wheelbase), maxWheelAngle(v.maxWheelAngle), steeringRatio(v.steeringRatio) {}   // NOLINT(google-explicit-constructor)
};

/** The steering geometry and its shaping, as the input layer keeps it. */
struct SteerFeel {
  double wheelbase = 0;
  double maxWheelAngle = 0;
  /** what of a car's lateral acceleration the shaping allows (1 a car … LORRY_LATERAL a lorry) */
  double lateral = 1;
  /** the fastest the steering input may move (share of the lock per second; Infinity: no limit) */
  double rate = JS_INF;
  /**
   * how sharply the keys turn for a given speed (settings: steerSensitivity):
   * 1 the feel as built, 2 twice the lock at the same speed (sharper), 0.5 half
   * (gentler).  Scales the lock the keys ask for; the vehicle's own limits and
   * the lateral shaping still hold.
   */
  double sens = 1;
};

/** what of a car's lateral acceleration a lorry is given */
inline constexpr double LORRY_LATERAL = 0.42;
/** how fast the driver of a lorry turns its steering wheel (rad/s: about 1.4 turns a second) … */
inline constexpr double LORRY_WHEEL_SPEED = (500 * std::numbers::pi) / 180;

/** Lateral acceleration (m/s²) the steering keys of a car may ask for: brisk in town, gentle on the motorway. */
inline double keyLateral(double v) { return 2.4 + 3.4 / (1 + (v / 15) * (v / 15)); }
/** … and what full deflection of a stick or of the steering bar asks for: close to what the tyres hold. */
inline constexpr double ANALOG_LATERAL = 7;

/** 0 for a car, 1 for a lorry, smoothly between. */
double heaviness(double mass);

/** The shaping for a vehicle (without one: the compact car's geometry, a car's feel). */
SteerFeel steerFeel(const SteerSpec* spec = nullptr, double wheelbase = 2.63, double maxWheelAngle = 0.576, double sens = 1);
inline SteerFeel steerFeel(const SteerSpec& spec, double wheelbase = 2.63, double maxWheelAngle = 0.576, double sens = 1) {
  return steerFeel(&spec, wheelbase, maxWheelAngle, sens);
}

/**
 * How much of the steering lock an input may ask for at this speed so that
 * the vehicle turns with at most `lateral` m/s² (scaled by the feel).  Full
 * lock up to walking pace.
 */
double lockFor(const SteerFeel& f, double lateral, double speed);

/** The steering bar or a stick: `want` −1 … +1 (already shaped), `quick` how many times the allowed lock per second it moves. */
double analogSteer(const SteerFeel& f, double cur, double want, double speed, double dt, double quick);

/**
 * The keys: `target` −1, 0, +1.  They ask for the lock that turns the vehicle
 * with a comfortable lateral acceleration at this speed – full lock at walking
 * pace, a fraction of a degree on the motorway – and wind it on within about
 * half a second (a lorry's slow steering takes longer).  Let go, the wheel
 * centres itself when the vehicle rolls and stays put when it stands.
 * `selfAlign` (−1 … 1) is the tyres' pull back to the middle.
 */
double keySteer(const SteerFeel& f, double cur, double target, double speed, double selfAlign, double dt);

}  // namespace fs

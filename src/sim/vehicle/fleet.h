// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/vehicle/fleet.ts @ v1.7.7
/**
 * The vehicles of the other licence classes: a city bus (D), a tractor (T)
 * and a motorcycle (A) – generic vehicles of their kind, no real model is
 * reproduced.  (Car B and truck C/CE: spec.h; the trailers: trailer.h.)
 *
 * The motorcycle runs on the same four-wheel model as everything else: two
 * narrow pairs of wheels, its weight low, so that it neither tips over nor
 * needs balancing.  That it leans into a bend is drawn (render/fleet),
 * not simulated.
 */
#pragma once

#include <string>

#include "core/js.h"
#include "sim/vehicle/spec.h"
#include "sim/vehicle/types.h"

namespace fs {

namespace fleet_detail {

/** a box for the body's contact points: from y0 to y1, front to rear (z), half the width */
inline std::vector<std::array<double, 3>> corners(double hw, double y0, double y1, double front, double rear) {
  std::vector<std::array<double, 3>> out;
  for (double y : {y0, y1})
    for (double z : {front, rear})
      for (double x : {-hw, hw}) out.push_back({x, y, z});
  return out;
}

/** a torque curve with every torque passed through `f` (the original's `torqueCurve.map`) */
template <typename F>
inline std::vector<std::array<double, 2>> mapCurve(const std::vector<std::array<double, 2>>& curve, F f) {
  std::vector<std::array<double, 2>> out;
  out.reserve(curve.size());
  for (const auto& [rpm, t] : curve) out.push_back({rpm, f(rpm, t)});
  return out;
}

}  // namespace fleet_detail

/** A low-floor city bus, 12 m, 13.5 t as driven, engine at the back, automatic. */
inline const VehicleSpec BUS = [] {
  VehicleSpec s = TRUCK;
  s.name = "Linienbus 12 m, 220 kW, Automatik";
  s.mass = 13500;
  s.inertia = {175000, 170000, 18000};
  s.wheelbase = 5.9;
  s.track = 2.1;
  s.cgHeight = 1.15;
  s.cgToFront = 3.4;  // 42 % of the weight on the front axle
  s.wheelRadius = 0.5;
  s.wheelInertia = 18;
  s.springRate = {219000, 348000};
  s.damperBump = {12500, 18500};
  s.damperRebound = {21800, 32000};
  s.antiRoll = {160000, 130000};
  s.torqueCurve = fleet_detail::mapCurve(TRUCK.torqueCurve, [](double, double t) { return jsRound(t * 1.25); });
  s.brakeTorque = 52000;
  s.handbrakeTorque = 50000;
  s.maxWheelAngle = 0.82;
  s.dragArea = 7.2;
  s.length = 12.0;
  s.width = 2.55;
  s.bodyPoints = fleet_detail::corners(1.27, 0.35, 3.05, -5.65, 6.35);
  return s;
}();

/** A tractor of 110 kW with a powershift gearbox, built for 40 km/h. */
inline const VehicleSpec TRACTOR = [] {
  VehicleSpec s = TRUCK;
  s.name = "Traktor 110 kW, Lastschaltgetriebe, 40 km/h";
  s.mass = 5600;
  s.inertia = {8500, 9000, 4200};
  s.wheelbase = 2.65;
  s.track = 1.85;
  s.cgHeight = 0.95;
  s.cgToFront = 1.59;  // 40 % on the front axle
  s.wheelRadius = 0.72;
  s.wheelInertia = 26;
  s.springRate = {113000, 170000};
  s.damperBump = {5500, 8000};
  s.damperRebound = {9500, 14000};
  s.antiRoll = {30000, 25000};
  s.travel = 0.12;
  s.muPeak = 0.85;
  s.torqueCurve = {
      {0, 200},
      {600, 380},
      {1000, 560},
      {1400, 620},
      {1800, 560},
      {2100, 470},
      {2300, 300},
      {2400, 0},
  };
  s.idleRpm = 800;
  s.limiterRpm = 2200;
  s.stallRpm = 400;
  s.engineInertia = 0.9;
  s.engineFriction = {40, 0.05};
  s.transmission = "automatic";
  s.gearRatios = {6.2, 4.2, 2.9, 2.05, 1.45};
  s.reverseRatio = 6;
  s.finalDrive = 10.6;
  s.clutchTorque = 2000;
  s.brakeTorque = 32000;
  s.brakeBiasFront = 0.4;
  s.handbrakeTorque = 30000;
  s.steeringRatio = 18;
  s.maxWheelAngle = 0.9;
  s.dragArea = 3.6;
  s.rearDrive = true;
  s.length = 4.5;
  s.width = 2.3;
  s.bodyPoints = fleet_detail::corners(1.0, 0.45, 2.95, -2.4, 2.1);
  // (the coupling takes about a third of the idle torque at a standstill: the tractor creeps, the brakes hold it)
  s.auto_ = VehicleSpec::Auto{.up = {1300, 2000}, .down = {900, 1300}, .kickdownRpm = 1800, .lockOmega = 90, .converter = 0.017};
  return s;
}();

/** A mid-size motorcycle, 650 cm³, with its rider: 300 kg. */
inline const VehicleSpec MOTORCYCLE = [] {
  VehicleSpec s = COMPACT_CAR;
  s.name = "Motorrad 650 cm³, 6-Gang";
  s.mass = 300;
  s.inertia = {150, 130, 40};
  s.wheelbase = 1.45;
  // (wide and low enough that the "motorcycle" slides before it would tip – it is not balanced)
  s.track = 0.9;
  s.cgHeight = 0.25;
  s.cgToFront = 0.72;
  s.wheelRadius = 0.31;
  s.wheelInertia = 0.5;
  s.springRate = {9600, 9600};
  s.damperBump = {600, 600};
  s.damperRebound = {1000, 1000};
  s.antiRoll = {6000, 6000};
  s.travel = 0.14;
  // (a car's bump stop – 260 kN/m, made for 1.4 t – threw this light machine metres into the air off a kerb
  // or a step in the ground, like a spring: a soft, well-damped rubber stop swallows the blow instead)
  s.bumpStop = VehicleSpec::BumpStop{.stiffness = 30000, .damping = 2500};
  s.muPeak = 1.1;
  s.torqueCurve = {
      {0, 20},
      {1000, 40},
      {3000, 55},
      {5000, 62},
      {6500, 64},
      {8000, 60},
      {9000, 52},
      {9500, 30},
      {10000, 0},
  };
  s.idleRpm = 1300;
  s.limiterRpm = 9500;
  s.stallRpm = 700;
  s.engineInertia = 0.05;
  s.engineFriction = {3, 0.002};
  s.gearRatios = {2.6, 1.9, 1.5, 1.27, 1.11, 1.0};
  // (a motorcycle has no reverse gear: it is pushed back – here a crawl backwards)
  s.reverseRatio = 2.6;
  s.finalDrive = 5.6;
  s.clutchTorque = 140;
  s.drivelineEfficiency = 0.9;
  s.brakeTorque = 1200;
  s.brakeBiasFront = 0.7;
  s.handbrakeTorque = 600;
  s.steeringRatio = 1;
  s.maxWheelAngle = 0.6;
  s.dragArea = 0.45;
  s.length = 2.15;
  s.width = 0.85;
  s.bodyPoints = fleet_detail::corners(0.3, 0.3, 1.2, -1.07, 1.08);
  return s;
}();

/** The same with a clutch that works itself (gears by hand), and with an automatic gearbox. */
inline const VehicleSpec MOTORCYCLE_AUTOCLUTCH = [] {
  VehicleSpec s = MOTORCYCLE;
  s.name = "Motorrad, Kupplungshilfe";
  s.autoClutch = true;
  return s;
}();
inline const VehicleSpec MOTORCYCLE_AUTOMATIC = [] {
  VehicleSpec s = MOTORCYCLE;
  s.name = "Motorrad, Automatik";
  s.transmission = "automatic";
  // (a coupling sized for the small engine: at idle it takes less than half of what the engine makes, or it would stall)
  s.auto_ = VehicleSpec::Auto{.up = {4200, 8800}, .down = {2600, 4500}, .kickdownRpm = 7500, .lockOmega = 160, .converter = 0.001};
  return s;
}();

/** A light truck of 7.5 t with a box body (class C1). */
inline const VehicleSpec LIGHT_TRUCK = [] {
  VehicleSpec s = TRUCK;
  s.name = "Lkw 7,5 t mit Kofferaufbau, 130 kW, automatisiertes Getriebe";
  s.mass = 5800;
  s.inertia = {26000, 25000, 6500};
  s.wheelbase = 3.9;
  s.track = 1.9;
  s.cgHeight = 1.1;
  s.cgToFront = 2.15;
  s.wheelRadius = 0.42;
  s.wheelInertia = 9;
  s.springRate = {105000, 170000};
  s.damperBump = {6200, 9500};
  s.damperRebound = {11000, 16500};
  s.antiRoll = {80000, 60000};
  s.torqueCurve = fleet_detail::mapCurve(TRUCK.torqueCurve, [](double, double t) { return jsRound(t * 0.66); });
  s.brakeTorque = 26000;
  s.handbrakeTorque = 24000;
  s.maxWheelAngle = 0.74;
  s.dragArea = 5.0;
  s.length = 7.4;
  s.width = 2.4;
  s.bodyPoints = fleet_detail::corners(1.2, 0.55, 3.3, -3.35, 4.05);
  return s;
}();

/** A minibus for up to 16 passengers (class D1): 5 t, 7.4 m. */
inline const VehicleSpec MINIBUS = [] {
  VehicleSpec s = BUS;
  s.name = "Kleinbus 7,4 m, 120 kW, Automatik";
  s.mass = 4800;
  s.inertia = {21000, 20000, 4500};
  s.wheelbase = 4.3;
  s.track = 1.75;
  s.cgHeight = 1.0;
  s.cgToFront = 2.4;
  s.wheelRadius = 0.38;
  s.wheelInertia = 6;
  s.springRate = {80000, 120000};
  s.damperBump = {4800, 7000};
  s.damperRebound = {8400, 12200};
  s.antiRoll = {45000, 35000};
  s.torqueCurve = fleet_detail::mapCurve(TRUCK.torqueCurve, [](double, double t) { return jsRound(t * 0.5); });
  s.brakeTorque = 17000;
  s.handbrakeTorque = 16000;
  s.maxWheelAngle = 0.7;
  s.dragArea = 4.6;
  s.length = 7.4;
  s.width = 2.2;
  s.bodyPoints = fleet_detail::corners(1.08, 0.35, 2.75, -3.35, 4.05);
  return s;
}();

/** A tractor built for 60 km/h (class T; the 40 km/h tractor is class L). */
inline const VehicleSpec TRACTOR_FAST = [] {
  VehicleSpec s = TRACTOR;
  s.name = "Traktor 150 kW, 60 km/h";
  s.finalDrive = TRACTOR.finalDrive * (40.0 / 60);
  s.torqueCurve = fleet_detail::mapCurve(TRACTOR.torqueCurve, [](double, double t) { return jsRound(t * 1.3); });
  return s;
}();

/**
 * … each with a manual gearbox and an automatic one whose coupling is sized for its engine: a small engine's
 * slips up to a higher speed – as a scooter's belt drive keeps its engine near its power while it pulls away –,
 * and a stronger engine than the big one's keeps the big one's, so that it creeps no harder at a red light.
 * (The original's fields are `manual` and `auto`.)
 */
struct Variants {
  VehicleSpec manual;
  VehicleSpec auto_;
};
Variants variants(const std::string& name, double k, double finalDrive);
Variants variants(const std::string& name, double k, double finalDrive, double size);
// (geared for its 45 km/h: at the rev limit in top gear it runs 46)
inline const Variants MOPED = variants("Moped 50 cm³, 45 km/h", 0.12, 24);
inline const Variants MOTORCYCLE_125 = variants("Leichtkraftrad 125 cm³, 11 kW", 0.3, 7.2);
inline const Variants MOTORCYCLE_35KW = variants("Motorrad 35 kW", 0.75, 5.8);

/**
 * How far a motorcycle leans into a bend (rad, + = to the right): as far as its speed (m/s) and the turn of
 * its heading (rad/s, + = to the left) ask – tan(lean) = v·ω / g, at most 39°.  From the heading rather than
 * from the measured sideways acceleration, which on a bumpy or sloping road takes in the up and down of the
 * suspension too and threw the drawn machine from side to side like a spring.
 */
double bikeLean(double speed, double yawRate);

}  // namespace fs

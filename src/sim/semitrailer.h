// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/semitrailer.ts @ v1.7.7
/**
 * The articulated lorry ("Sattelzug") of class CE: a two-axle tractor unit
 * and a tri-axle semi-trailer of 13.6 m – together 16.5 m, the longest the
 * rules allow (§ 32 Abs. 4 StVZO) – and the three-axle rigid truck that
 * class C is learnt with.  (A tractor unit alone would be class C as well, but
 * nobody learns with one: it has no load to carry and brakes badly empty.)
 * Generic on purpose: no real model is reproduced.
 *
 * The semi-trailer hangs on the fifth wheel by its kingpin.  The fifth wheel
 * sits *over* the tractor's rear axle (a little ahead of it), not behind it as
 * a drawbar coupling does: the joint hardly swings out when the tractor turns,
 * and the trailer's axle group, 7.75 m behind the kingpin, follows on a much
 * tighter circle – it cuts corners more than a drawbar trailer does.  The
 * kinematics are the same two lines as the drawbar trailer's (sim/trailer.ts:
 * the axle group rolls but does not slip sideways) – only the joint is
 * elsewhere.  What is the semi-trailer's own:
 *
 *  - its weight rests partly on the fifth wheel (the kingpin load), and so on
 *    the tractor's rear axle – that is what gives a tractor unit its grip;
 *  - the trailer cannot swing round further than the tractor's cab and frame
 *    let it: at JACKKNIFE_LIMIT it stands against them (jack-knifed);
 *  - its landing gear (the support legs at the front) is wound down to stand
 *    it alone and up to drive; its spring brakes hold it while its parking
 *    brake is set or the supply line (red) is not connected.
 */
#pragma once

#include <array>
#include <memory>
#include <vector>

#include "sim/trailer.h"
#include "sim/vehicle/index.h"

namespace fs {

struct SemiTrailerSpec : TrailerSpec {
  /** the share of its weight that rests on the fifth wheel (the rest on its axles) */
  double kingpinShare = 0;
  /** how many axles its axle group has (axleSpread apart) */
  int axles = 0;
  /** the landing gear: metres behind the kingpin */
  double legsBehind = 0;
};

/** The fifth wheel of the tractor unit: 0.45 m ahead of its rear axle (negative: ahead), 1.15 m above the ground. */
inline constexpr TruckHitch FIFTH_WHEEL = {.behindRearAxle = -0.45, .height = 1.15};

/**
 * A curtain-sider semi-trailer: 13.6 m long, 2.55 m wide, 4 m high, the
 * kingpin 1.6 m behind its front wall, a tri-axle 7.75 m behind the kingpin;
 * 8 t as driven (empty, with some ballast – the combination then has the
 * 15 t a test vehicle of class CE must have, Anlage 7 FeV 2.2.7).
 */
inline const SemiTrailerSpec SEMI_TRAILER = [] {
  SemiTrailerSpec s;
  s.mass = 8000;
  s.hitchToAxle = 7.75;
  // (the front wall stands ahead of the kingpin: a negative distance behind it)
  s.bodyFront = -1.6;
  s.length = 12.0;
  s.width = 2.55;
  s.height = 4.0;
  s.axleSpread = 1.31;
  s.track = 2.04;
  s.wheelRadius = 0.48;
  s.brake = 6.0;
  s.rollingResistance = 0.007;
  s.hitch = FIFTH_WHEEL;
  s.kingpinShare = 0.4;
  s.axles = 3;
  s.legsBehind = 2.3;
  return s;
}();

/** How far the trailer can swing round against the tractor before it stands against its cab and frame (rad, about 83°). */
inline constexpr double JACKKNIFE_LIMIT = 1.45;

/**
 * The tractor unit: a two-axle 18-tonner with a long-distance cab, 330 kW
 * (450 PS) six, automated gearbox, rear-wheel drive; 7.6 t as driven, most
 * of it on the front axle until a trailer rests on the fifth wheel.  Its air
 * springs at the rear are stiff enough to carry the kingpin load.
 */
inline const VehicleSpec TRACTOR_UNIT = [] {
  VehicleSpec s = TRUCK;
  s.name = "Sattelzugmaschine 18 t, 330 kW, automatisiertes Getriebe";
  s.mass = 7600;
  s.inertia = {30000, 28000, 9000};
  s.wheelbase = 3.7;
  s.track = 2.04;
  s.cgHeight = 1.15;
  // 62 % of the weight on the front axle without a trailer
  s.cgToFront = 1.4;
  s.wheelRadius = 0.5;
  s.wheelInertia = 18;
  s.springRate = {230000, 420000};
  s.damperBump = {10000, 16000};
  s.damperRebound = {17000, 26000};
  s.antiRoll = {130000, 110000};
  // a 13 l six: its torque from just above idle, held to 1400 rpm
  s.torqueCurve = {
      {0, 500},
      {500, 1100},
      {600, 1500},
      {900, 2200},
      {1000, 2300},
      {1300, 2300},
      {1600, 2000},
      {1900, 1650},
      {2100, 1300},
      {2300, 0},
  };
  s.idleRpm = 550;
  s.limiterRpm = 2100;
  s.stallRpm = 280;
  s.engineInertia = 1.8;
  s.engineFriction = {90, 0.08};
  s.gearRatios = {12.3, 8.6, 6.1, 4.4, 3.2, 2.3, 1.62, 1.18, 1.0, 0.78};
  s.reverseRatio = 11.5;
  s.finalDrive = 2.85;
  s.clutchTorque = 4200;
  s.brakeTorque = 44000;
  s.brakeBiasFront = 0.6;
  s.handbrakeTorque = 36000;
  s.maxWheelAngle = 0.7;
  s.dragArea = 6.4;
  s.length = 6.0;
  s.width = 2.5;
  s.bodyPoints = std::vector<std::array<double, 3>>{
      {-1.25, 0.6, -3.1},
      {1.25, 0.6, -3.1},
      {-1.25, 0.6, 2.8},
      {1.25, 0.6, 2.8},
      {-1.2, 3.0, -3.05},
      {1.2, 3.0, -3.05},
      {-1.25, 3.85, -2.4},
      {1.25, 3.85, -2.4},
      {-1.25, 3.85, -0.85},
      {1.25, 3.85, -0.85},
  };
  s.auto_ = VehicleSpec::Auto{.up = {1150, 1800}, .down = {800, 1150}, .kickdownRpm = 1600, .lockOmega = 75, .converter = 0.05};
  return s;
}();

/**
 * The three-axle rigid truck of class C (6x2, 26 t gross): 12 t as driven,
 * the tractor's six at less power, a box body.  The physics know two axles:
 * the front one and, behind, one in the middle between the driven axle and
 * the tag axle (the tandem turns about that point).
 */
inline const VehicleSpec HEAVY_RIGID = [] {
  VehicleSpec s = TRACTOR_UNIT;
  s.name = "Lkw 26 t, drei Achsen, mit Kofferaufbau, 265 kW, automatisiertes Getriebe";
  s.mass = 12000;
  s.inertia = {78000, 75000, 16000};
  s.wheelbase = 5.2;
  s.cgHeight = 1.4;
  s.cgToFront = 2.6;
  s.springRate = {260000, 520000};
  s.damperBump = {11000, 19000};
  s.damperRebound = {19000, 31000};
  s.antiRoll = {140000, 120000};
  s.maxWheelAngle = 0.62;
  s.finalDrive = 3.1;
  s.dragArea = 6.6;
  s.length = 8.7;
  s.width = 2.5;
  s.bodyPoints = std::vector<std::array<double, 3>>{
      {-1.25, 0.6, -4.0},
      {1.25, 0.6, -4.0},
      {-1.25, 0.6, 4.7},
      {1.25, 0.6, 4.7},
      {-1.2, 3.3, -3.95},
      {1.2, 3.3, -3.95},
      {-1.25, 3.8, -2.0},
      {1.25, 3.8, -2.0},
      {-1.25, 3.8, 4.7},
      {1.25, 3.8, 4.7},
  };
  return s;
}();

/**
 * The tractor unit's outline from its model origin (the ground point below
 * the middle of its wheelbase): ahead to the bumper, behind to the end of the
 * frame, half its width – and the back wall of its cab, ahead of the origin.
 */
struct TractorOutline {
  double front;
  double rear;
  double hw;
  double cabBack;
};
inline constexpr TractorOutline TRACTOR_OUTLINE = {.front = 3.1, .rear = 2.8, .hw = 1.25, .cabBack = 0.8};
/** the fifth wheel's middle behind the tractor's model origin (m) – TRACTOR_UNIT.wheelbase / 2 + FIFTH_WHEEL.behindRearAxle */
inline constexpr double FIFTH_WHEEL_AT = 3.7 / 2 + FIFTH_WHEEL.behindRearAxle;

/**
 * Is this the spec of a semi-trailer (rather than a drawbar trailer)?  The spec as a semi-trailer's, or null.
 * (The original asks for `'kingpinShare' in t`: here the semi-trailers' specs are the ones of this file.)
 */
const SemiTrailerSpec* isSemiSpec(const TrailerSpec* t);

/**
 * Where the trailer's axle group runs when the joint goes round a circle of
 * radius `hitchRadius` for long enough: on a circle of this radius (the
 * swept path's inner edge comes from it).  The trailer then stands at
 * asin(L / R) to the joint's path.
 */
double steadyAxleRadius(double hitchRadius, double hitchToAxle);

class SemiTrailer : public Trailer {
 public:
  const SemiTrailerSpec semi;
  /** the landing gear: wound down (target) and where it is (0 up … 1 on the ground) */
  bool legsDown = false;
  double legs = 0;
  /** the lines to the tractor: supply (red) and brake (yellow) air, and the electrical cables */
  bool supply = true;
  bool control = true;
  bool cables = true;
  /** its own parking brake (the red knob at its side) is set */
  bool parkBrake = false;
  /** it stands against the tractor's cab and frame */
  bool jackknifed = false;
  /** … and has just come to do so (for one step) */
  bool jackknifeEvent = false;

  SemiTrailer(const SemiTrailerSpec& spec, Ground& ground);

  /** its spring brakes hold: parking brake set, or no supply air */
  bool braked() const { return parkBrake || !supply; }

  /** the weight on the fifth wheel (N), while coupled */
  double kingpinLoad() const;

  void place(double x, double z, double yaw) override;
  void coupleBehind(const Vehicle& truck) override;
  void step(double dt, Vehicle& truck, const DriverInput& input) override;

 private:
  DriverInput held{};
};

/** A trailer for a spec: the semi-trailer for a semi-trailer's, else the drawbar or box trailer. */
std::unique_ptr<Trailer> makeTrailer(const TrailerSpec& spec, Ground& ground);

}  // namespace fs

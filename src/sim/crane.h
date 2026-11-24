// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/crane.ts @ v1.7.7
/**
 * Cranes as the player works them: a tower crane on its cross base, a
 * mobile crane with a telescopic boom on a three-axle carrier, and a loader
 * crane (knuckle boom) behind the cab of a flatbed truck.  This file holds
 * what makes each of them stand and move – pure arithmetic, no drawing
 * (render/crane.ts draws them, game/craneplay.ts runs them):
 *
 *  - their masses and where they sit: carrier, base ballast, mast,
 *    superstructure, counterweight, boom, hook block;
 *  - what they stand on: the four foundation blocks of the tower crane's
 *    cross base, the pads under the outrigger jacks – or the tyres;
 *  - the tipping check: for every line between two neighbouring supports
 *    (a tipping line) the moments that tip the crane over it – the load,
 *    the boom reaching out, the wind and the swing pulling on the rope –
 *    against those that hold it back – carrier, ballast, counterweight;
 *  - the load chart: what may hang on the hook at a radius, a boom length
 *    and an outrigger setting.  For the mobile and the loader crane it is
 *    worked out from that very tipping check, with the classic margins for
 *    mobile cranes (three quarters of the tipping load on outriggers, two
 *    thirds on tyres – and on tyres half of that again, for the give of the
 *    tyres), and capped by what the boom itself may carry; the tower crane's
 *    is that of its jib (its base ballast leaves it far from tipping);
 *  - the load moment limiter (LMI): the load measured on the rope against
 *    the chart – a warning at 90 %, and at 100 % every movement that would
 *    raise the load moment is cut off (hoisting, luffing down, telescoping
 *    or running the trolley out); lowering and coming in always work;
 *  - the drives: each moves at most so fast, gets there with a ramp, stops
 *    at its end stops; outriggers, jacks and the automatic levelling;
 *  - the hook block (with the load slung under it) on its rope: a damped
 *    pendulum on an elastic rope, pushed by the wind, set down on what is
 *    under it, stopped by what it runs into.
 *
 * Frames.  The carrier frame is the vehicle's (render/truck.ts): origin on
 * the ground below the middle of the wheelbase (the tower crane: the middle
 * of its base), x right, y up, z back.  The boom's direction turns with the
 * slewing angle like a yaw: 0 straight ahead (−z), positive to the left.  A
 * part of the superstructure is given along the boom's direction (a, from
 * the slewing axis), to its right (s) and in height (y).  The world: x east,
 * y up, z south; the carrier stands there at `base` turned by `base.yaw`,
 * raised by the jacks (`lift`) and tilted as the ground and the levelling
 * leave it (`tilt`: the rise per metre along its x and z).
 *
 * Units: tonnes (t), metres, seconds; forces in kN, moments in kN·m.
 * Generic cranes of their kinds – no real model is reproduced.
 */
#pragma once

#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/js.h"
#include "engine/math.h"
#include "sim/vehicle/fleet.h"
#include "sim/vehicle/spec.h"
#include "sim/vehicle/types.h"

namespace fs {

/** 'tower' | 'mobile' | 'loader' */
using CraneType = std::string;
/** what the crane stands on, as the load chart knows it: the outrigger beams fully out, half out, in (on the jacks) – or on its tyres; the tower crane on its base ('full' | 'half' | 'zero' | 'tyres' | 'fixed') */
using Setting = std::string;

inline constexpr double G = 9.81;

// ------------------------------------------------------------------------------------ stability

/** A weight (t) and where it acts (any frame with y up). */
struct Weight {
  double t = 0;
  double x = 0;
  double y = 0;
  double z = 0;
};

/** A sideways force (kN) – the wind, the rope pulled aside by a swinging load – and the height it acts at above the supports. */
struct SideForce {
  double fx = 0;
  double fz = 0;
  double y = 0;
};

/** The outcome of a tipping check. */
struct Stability {
  /**
   * the moments that tip the crane over its worst tipping line against
   * those that hold it there: 1 – it tips; Infinity – it stands on fewer
   * than three supports
   */
  double u = 0;
  /** that line: from support `edge` to the next one of `hull` */
  int edge = 0;
  /** where the weights and forces press on the ground (the crane stands while this lies inside the supports) */
  double copX = 0;
  double copZ = 0;
  /** the supports that bound it, in order round it (x, z) */
  std::vector<std::array<double, 2>> hull;
};

/** A tipping line: a point on it and its outward normal (x, z). */
struct TippingLine {
  double px = 0;
  double pz = 0;
  double nx = 0;
  double nz = 0;
};

/** The convex hull of points in the plan (x, z), in order round it; points on its edges dropped. */
std::vector<std::array<double, 2>> convexHull(const std::vector<std::array<double, 2>>& points);

/**
 * The tipping check: every weight presses down where it is, every sideways
 * force acts at its height; about each tipping line (between neighbouring
 * supports) what lies or pushes beyond it tips the crane over it, what lies
 * inside holds it back.  `supports`: where the crane stands (x, z), at the
 * height the forces' heights are measured from.
 */
Stability stability(const std::vector<Weight>& weights, const std::vector<SideForce>& sides, const std::vector<std::array<double, 2>>& supports);

/**
 * The load (t) at (x, z) that just tips the crane over: the least of what
 * each tipping line the load lies beyond can take, the other weights holding
 * the crane back.  0 when it would tip over without a load; Infinity when
 * the load lies within all of them.
 */
double tippingLoad(const std::vector<Weight>& weights, double x, double z, const std::vector<std::array<double, 2>>& supports);

// ------------------------------------------------------------------------------------ the cranes

/** A mass of the superstructure: t, along the boom's direction from the slewing axis (a), to its right (s), height (y). */
struct SlewMass {
  double t = 0;
  double a = 0;
  double s = 0;
  double y = 0;
};

/** A drive: its range, how fast and how briskly it moves. */
struct DriveSpec {
  double min = 0;
  double max = 0;
  /** full speed (per s) */
  double speed = 0;
  /** how quickly it gets there or stops (per s²) */
  double accel = 0;
  /** goes round without end (slewing) */
  bool endless = false;
};

/** An outrigger beam: its place along the carrier and its side (−1 | 1). */
struct OutriggerBeam {
  double z = 0;
  int side = 1;
};

/** The outriggers: a beam each (its place along the carrier and its side), how far out its pad is, the jacks. */
struct OutriggerSpec {
  std::vector<OutriggerBeam> beams;
  /** the pad's distance from the carrier's axis with the beam in and fully out (m) */
  double padIn = 0;
  double padOut = 0;
  /** height of the pad's sole above the ground with the jack drawn up (the carrier on its wheels) */
  double clearance = 0;
  /** the jacks' stroke (m) */
  double stroke = 0;
  /** the beams move out in this many seconds, the jacks at this speed (m/s) */
  double beamTime = 0;
  double jackSpeed = 0;
  /** what the jacks lift the carrier by until its wheels are free (0: the loader crane – its wheels stay on the ground) */
  double freeLift = 0;
};

/** What is the same for every crane: masses, supports, drives, limits. */
struct CraneSpec {
  CraneType type;
  std::string name;
  /** the slewing axis in the carrier frame */
  double slewX = 0;
  double slewZ = 0;
  /** masses that stay with the carrier or the base: t, x, y, z */
  std::vector<Weight> fixed;
  /** masses that turn with the superstructure (the boom's are worked out from its joints) */
  std::vector<SlewMass> slewing;
  /** the hook block (t): part of every load the chart allows */
  double hook = 0;
  /** where the hook hangs, from the rope: the bottom of the hook block below its middle (m) */
  double hookDrop = 0;
  /** what it stands on without outriggers (the foundation blocks, or the tyres: x, z) */
  std::vector<std::array<double, 2>> feet;
  std::optional<OutriggerSpec> outriggers;
  /** the strongest wind it may work in (m/s, gusts at the boom head) */
  double windLimit = 0;
  /** the steepest its carrier may stand at work before the limiter cuts off, and what the levelling brings it within (deg) */
  double levelLimit = 0;
  double levelGood = 0;
  /** the slewing drive */
  DriveSpec slew;
  /** the hoist: rope paid out (m) – null: the hook hangs on a fixed link (the loader crane) */
  std::optional<DriveSpec> hoist;
  /** the rope when there is no hoist (m) */
  double link = 0;
  /** what the hoist lifts at full speed and at its slowest (t: the speed falls linearly between) */
  double hoistLight = 0;
  double hoistHeavy = 0;
};

/** 'slew' | 'trolley' | 'luff' | 'tele' | 'inner' | 'outer' | 'ext' | 'rope' */
using JointKey = std::string;

/** The joints of a crane – each crane uses those it has. */
struct Joints {
  /** turned to the left of straight ahead (rad) */
  double slew = 0;
  /** the tower crane: the trolley's distance from the tower's axis (m) */
  double trolley = 0;
  /** the mobile crane: the boom's angle above the horizontal (rad), its telescopes out (0 … 1) */
  double luff = 0;
  double tele = 0;
  /** the loader crane: the inner boom's angle above the horizontal, the outer boom's against the inner one (rad), the extensions out (0 … 1) */
  double inner = 0;
  double outer = 0;
  double ext = 0;
  /** rope paid out below the boom head or the trolley (m) */
  double rope = 0;

  /** `q[key]` */
  double& at(const JointKey& k);
  double at(const JointKey& k) const;
};

// ---- the tower crane

/** The tower crane's jib: where it is and what it may lift (its chart: 6 t up to 16 m, 1.6 t at the tip). */
struct TowerGeometry {
  /** height of the trolley's sheaves the rope hangs from (m) */
  double sheave = 0;
  /** the trolley: its range (m from the axis) */
  double trolleyMin = 0;
  double trolleyMax = 0;
  double jibLength = 0;
  double counterJib = 0;
  /** the slewing ring's height (the top of the mast) */
  double mastTop = 0;
  /** the mast's width (m) */
  double mastWidth = 0;
  /** the chart: at most maxLoad (t); beyond that moment / r − loss */
  double maxLoad = 0;
  double moment = 0;
  double loss = 0;
  double trolleyMass = 0;
};

inline const TowerGeometry TOWER_GEOMETRY = {
    .sheave = 31.0,
    .trolleyMin = 2.5,
    .trolleyMax = 48.5,
    .jibLength = 50,
    .counterJib = 15,
    .mastTop = 30.0,
    .mastWidth = 1.7,
    .maxLoad = 6,
    // 6 t at 16 m, 1.6 t at 48.5 m
    .moment = 105.06,
    .loss = 0.566,
    .trolleyMass = 0.4,
};

/**
 * A top-slewing tower crane, 50 m jib, 6 t: on a cross base of 5 × 5 m on
 * four foundation blocks, 40 t of ballast blocks on it; the mast of lattice
 * sections 30 m high; on top the slewing platform with the cab, the tower
 * head with the pendant ties, the jib, the counter-jib with its 15 t of
 * ballast blocks and the hoist winch.
 */
inline const CraneSpec TOWER_CRANE = {
    .type = "tower",
    .name = "Turmdrehkran 50 m, 6 t",
    .slewX = 0,
    .slewZ = 0,
    .fixed =
        {
            {6, 0, 0.5, 0},   // the cross base
            {40, 0, 1.4, 0},  // the central ballast
            {18, 0, 15, 0},   // the mast
        },
    .slewing =
        {
            {5, 0, 0, 30.6},       // slewing ring, platform, cab
            {3, 0, 0, 34.5},       // the tower head
            {11, 22, 0, 31.6},     // the jib
            {3.5, -7, 0, 31.2},    // the counter-jib
            {15, -13.5, 0, 31.0},  // its ballast blocks
            {2.5, -9, 0, 31.8},    // the hoist winch
        },
    .hook = 0.35,
    .hookDrop = 1.05,
    .feet = {{-2.5, -2.5}, {2.5, -2.5}, {2.5, 2.5}, {-2.5, 2.5}},
    .outriggers = std::nullopt,
    .windLimit = 20,
    .levelLimit = 90,
    .levelGood = 90,
    // 0.8 rpm
    .slew = {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), 0.084, 0.035, true},
    .hoist = DriveSpec{1.6, 36, 1.0, 0.8},
    .link = 0,
    .hoistLight = 1,
    .hoistHeavy = 6,
};

// ---- the mobile crane

/** The mobile crane's boom: where its foot is, how long it gets, how steep, what it may carry by its length. */
struct MobileGeometry {
  /** the boom's foot pin: behind the slewing axis (a) and its height above the ground with the carrier on its wheels */
  double footA = 0;
  double footY = 0;
  /** the boom fully in and fully out (m), and how far the head's sheaves reach beyond its end */
  double length0 = 0;
  double length1 = 0;
  double head = 0;
  /** the steepest it may be raised (rad) */
  double luffMax = 0;
  /** the luffing cylinder: where it sits on the turntable (a, y) and on the boom (m from the foot) */
  double cylA = 0;
  double cylY = 0;
  double cylOnBoom = 0;
  /** the boom's weight (t) and its centre (share of its length from the foot, plus metres) */
  double boomMass = 0;
  double boomCg = 0;
  double boomCgPlus = 0;
  /** what the boom itself may carry at each length of the chart (t), and the moment its pin and cylinder take (t·m) */
  std::vector<double> lengths;
  std::vector<double> strength;
  double moment = 0;
};

inline const MobileGeometry MOBILE_GEOMETRY = {
    .footA = -0.9,
    .footY = 2.85,
    .length0 = 9.6,
    .length1 = 34,
    .head = 0.45,
    .luffMax = 82 * DEG,
    .cylA = 1.05,
    .cylY = 1.95,
    .cylOnBoom = 3.9,
    .boomMass = 5.6,
    .boomCg = 0.4,
    .boomCgPlus = 0.3,
    .lengths = {9.6, 14.48, 19.36, 24.24, 29.12, 34},
    .strength = {40, 32, 25, 18, 13, 9.5},
    .moment = 130,
};

/** The three axles of the mobile crane (z) and half its track over the tyres' outer flanks. */
inline const std::vector<double> MOBILE_AXLES = {-1.75, 0.95, 2.55};
inline constexpr double MOBILE_TYRE_EDGE = 1.24;

/**
 * A mobile crane of 40 t on a three-axle carrier, 37 t as it drives: the
 * carrier (19.5 t), the superstructure with the crane cab and the hoist
 * (4.5 t), 7 t of counterweight, a boom of four sections from 9.6 to 34 m
 * (5.6 t), a hook block of 0.4 t.  Four outrigger beams – just behind the
 * driver's cab and at the rear – put its pads 6 m apart across, 6.9 m along.
 */
inline const CraneSpec MOBILE_CRANE = [] {
  CraneSpec c;
  c.type = "mobile";
  c.name = "Mobilkran 40 t, 3 Achsen";
  c.slewX = 0;
  c.slewZ = 1.75;
  c.fixed = {{19.5, 0, 1.25, -0.3}};
  c.slewing = {
      {4.5, -0.6, 0, 2.4},  // turntable, superstructure, cab, winch
      {7, -3.3, 0, 2.3},    // the counterweight
  };
  c.hook = 0.4;
  c.hookDrop = 1.15;
  for (const double z : MOBILE_AXLES) {
    c.feet.push_back({-MOBILE_TYRE_EDGE, z});
    c.feet.push_back({MOBILE_TYRE_EDGE, z});
  }
  c.outriggers = OutriggerSpec{
      .beams = {{-2.75, -1}, {-2.75, 1}, {4.15, -1}, {4.15, 1}},
      .padIn = 1.2,
      .padOut = 3.0,
      .clearance = 0.55,
      .stroke = 0.9,
      .beamTime = 14,
      .jackSpeed = 0.07,
      .freeLift = 0.16,
  };
  c.windLimit = 13;
  c.levelLimit = 1;
  c.levelGood = 0.3;
  // 2 rpm
  c.slew = {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), 0.21, 0.12, true};
  c.hoist = DriveSpec{1.6, 42, 0.7, 0.8};
  c.link = 0;
  c.hoistLight = 1;
  c.hoistHeavy = 20;
  return c;
}();

// ---- the loader crane

/** The loader crane's booms: the column's pin, the inner boom, the outer boom with its extensions, the hook's link, what it may lift. */
struct LoaderGeometry {
  /** the inner boom's pin on the column: ahead of the slewing axis (a) and its height */
  double pinA = 0;
  double pinY = 0;
  double inner = 0;
  /** the outer boom in and fully extended (m) */
  double outer0 = 0;
  double outer1 = 0;
  double innerMin = 0;
  double innerMax = 0;
  double outerMin = 0;
  double outerMax = 0;
  /** the folded position it travels in */
  double stowInner = 0;
  double stowOuter = 0;
  /** the lifting moment (t·m) and the most it lifts close in (t) */
  double moment = 0;
  double maxLoad = 0;
  double innerMass = 0;
  double outerMass = 0;
};

inline const LoaderGeometry LOADER_GEOMETRY = {
    .pinA = 0.15,
    .pinY = 1.95,
    .inner = 2.15,
    .outer0 = 2.7,
    .outer1 = 7.95,
    .innerMin = -10 * DEG,
    .innerMax = 80 * DEG,
    .outerMin = -170 * DEG,
    .outerMax = 15 * DEG,
    .stowInner = 78 * DEG,
    .stowOuter = -166 * DEG,
    .moment = 14,
    .maxLoad = 4.5,
    .innerMass = 0.45,
    .outerMass = 0.65,
};

/** The loader truck's axles (z) and half its track over the tyres' outer flanks, front and rear (twin tyres). */
inline const std::vector<double> LOADER_AXLES = {-2.4, 2.4};
inline const std::vector<double> LOADER_TYRE_EDGE = {1.18, 1.25};
/** where the flatbed lies on the loader truck: from z0 to z1, half its width, its floor's height */
struct LoaderBed {
  double z0 = 0;
  double z1 = 0;
  double hw = 0;
  double y = 0;
};
inline constexpr LoaderBed LOADER_BED = {.z0 = -0.85, .z1 = 5.2, .hw = 1.22, .y = 1.28};

/**
 * A loader crane of 14 t·m behind the cab of a two-axle flatbed truck: on
 * its base the outrigger beams left and right, the column turning on it,
 * the inner boom on the lift cylinder, the outer boom on the folding
 * cylinder with three hydraulic extensions, the hook on a short link.
 */
inline const CraneSpec LOADER_CRANE = [] {
  CraneSpec c;
  c.type = "loader";
  c.name = "Lkw-Ladekran 14 tm";
  c.slewX = 0;
  c.slewZ = -1.45;
  c.fixed = {
      {9.6, 0, 1.1, 0.7},    // the truck with its flatbed
      {0.9, 0, 1.3, -1.45},  // the crane's base with its outriggers
  };
  c.slewing = {{0.35, 0, 0, 1.7}};
  c.hook = 0.05;
  c.hookDrop = 0.62;
  c.feet = {
      {-LOADER_TYRE_EDGE[0], LOADER_AXLES[0]},
      {LOADER_TYRE_EDGE[0], LOADER_AXLES[0]},
      {-LOADER_TYRE_EDGE[1], LOADER_AXLES[1]},
      {LOADER_TYRE_EDGE[1], LOADER_AXLES[1]},
  };
  c.outriggers = OutriggerSpec{
      .beams = {{-1.45, -1}, {-1.45, 1}},
      .padIn = 1.2,
      .padOut = 2.6,
      .clearance = 0.5,
      .stroke = 0.7,
      .beamTime = 6,
      .jackSpeed = 0.09,
      .freeLift = 0,
  };
  c.windLimit = 12.5;
  c.levelLimit = 3;
  c.levelGood = 1;
  c.slew = {-200 * DEG, 200 * DEG, 0.25, 0.25};
  c.hoist = std::nullopt;
  c.link = 0.9;
  c.hoistLight = 1;
  c.hoistHeavy = 1;
  return c;
}();

const CraneSpec& specOf(const CraneType& type);

// ---- driving them

/**
 * The mobile crane as it drives: 37 t on three axles (the physics knows two:
 * the first, and one in the middle of the other two), a diesel of 370 kW,
 * an automated gearbox, governed at 80 km/h.
 */
inline const VehicleSpec MOBILE_CRANE_SPEC = [] {
  VehicleSpec s = TRUCK;
  s.name = "Mobilkran 40 t, 3 Achsen, 370 kW";
  s.mass = 37000;
  s.inertia = {235000, 225000, 52000};
  s.wheelbase = 3.5;
  s.track = 2.08;
  s.cgHeight = 1.45;
  // a third of the weight on the front axle
  s.cgToFront = 2.33;
  s.wheelRadius = 0.69;
  s.wheelInertia = 40;
  s.springRate = {640000, 1280000};
  s.damperBump = {34000, 52000};
  s.damperRebound = {60000, 90000};
  s.antiRoll = {420000, 420000};
  s.torqueCurve = fleet_detail::mapCurve(TRUCK.torqueCurve, [](double, double t) { return jsRound(t * 2.1); });
  s.finalDrive = 5.6;
  s.clutchTorque = 6000;
  s.brakeTorque = 165000;
  s.handbrakeTorque = 150000;
  s.maxWheelAngle = 0.62;
  s.dragArea = 7.5;
  s.length = 11.6;
  s.width = 2.55;
  s.bodyPoints = std::vector<std::array<double, 3>>{
      {-1.27, 0.55, -3.95}, {1.27, 0.55, -3.95}, {-1.27, 0.55, 4.6}, {1.27, 0.55, 4.6},
      {-1.27, 3.3, -3.95},  {1.27, 3.3, -3.95},  {-1.27, 3.4, 4.6},  {1.27, 3.4, 4.6},
  };
  return s;
}();

/** The flatbed truck with its loader crane: 13 t, two axles, 210 kW. */
inline const VehicleSpec LOADER_TRUCK_SPEC = [] {
  VehicleSpec s = TRUCK;
  s.name = "Lkw mit Ladekran 18 t, 2 Achsen, 210 kW";
  s.mass = 13000;
  s.inertia = {82000, 79000, 17000};
  s.wheelbase = 4.8;
  s.track = 2.04;
  s.cgHeight = 1.3;
  s.cgToFront = 2.2;
  s.wheelRadius = 0.5;
  s.springRate = {240000, 300000};
  s.damperBump = {12500, 16000};
  s.damperRebound = {22000, 28000};
  s.antiRoll = {150000, 110000};
  s.torqueCurve = fleet_detail::mapCurve(TRUCK.torqueCurve, [](double, double t) { return jsRound(t * 1.18); });
  s.brakeTorque = 56000;
  s.handbrakeTorque = 50000;
  s.length = 9.15;
  s.width = 2.55;
  s.bodyPoints = std::vector<std::array<double, 3>>{
      {-1.25, 0.6, -3.9}, {1.25, 0.6, -3.9}, {-1.25, 0.6, 5.25}, {1.25, 0.6, 5.25},
      {-1.2, 3.1, -3.9},  {1.2, 3.1, -3.9},  {-1.25, 1.9, 5.25}, {1.25, 1.9, 5.25},
  };
  return s;
}();

// ------------------------------------------------------------------------------------ kinematics

/** The direction the boom points in the carrier frame (x, z) for a slewing angle. */
std::array<double, 2> boomDir(double slew);

/** The mobile crane's boom length for how far its telescopes are out. */
double boomLength(double tele);

/** The loader crane's outer boom length for how far its extensions are out. */
double outerLength(double ext);

/** `{ a, y }`: along the boom's direction from the slewing axis, and above the ground. */
struct HeadAt {
  double a = 0;
  double y = 0;
};

/**
 * Where the rope hangs from – the trolley's sheaves, the boom head, the end
 * of the loader crane's outer boom: along the boom's direction from the
 * slewing axis (a, the radius) and above the ground (y), the carrier on its
 * wheels.
 */
HeadAt headOf(const CraneType& type, const Joints& q);

/** The boom's own masses for these joints (the trolley, the boom, the inner and outer boom): t, a, y. */
std::vector<SlewMass> boomMasses(const CraneType& type, const Joints& q);

// ------------------------------------------------------------------------------------ the load charts

/** A load chart: the rated capacity (t, the hook block included) by setting, boom length (columns) and radius (rows). */
struct LoadChart {
  /** the boom lengths of its columns (one column: it does not depend on a length) */
  std::vector<double> lengths;
  std::vector<double> radii;
  std::map<Setting, std::vector<std::vector<double>>> t;
};

/** the margin below the tipping load on outriggers and on tyres – and on tyres half of that again (the tyres give) */
inline constexpr double MARGIN_OUTRIGGERS = 0.75;
inline constexpr double MARGIN_TYRES = 0.667;
inline constexpr double TYRES_DERATE = 0.5;

/** What the crane stands on in a setting: the pads with the beams that far out, or its feet (tyres, foundation blocks). */
std::vector<std::array<double, 2>> supportsFor(const CraneSpec& spec, const Setting& setting);

/** The weights of a crane for its joints, in the carrier frame (without the hook and the load). */
std::vector<Weight> craneWeights(const CraneSpec& spec, const Joints& q, const std::vector<Weight>& extra = {});

/** The tower crane's chart: 6 t up to 16 m, then what its jib may carry at the radius. */
double towerCapacity(double r);

/** The load chart of a crane (worked out the first time it is asked for). */
const LoadChart& chartOf(const CraneType& type);

/**
 * What the chart allows (t) at a radius and boom length in a setting:
 * between its rows and columns straight between the neighbours; closer in
 * than its first radius as at that radius, beyond its last – nothing.
 */
double capacity(const CraneType& type, const Setting& setting, double radius, double length = 0);

// ------------------------------------------------------------------------------------ wind

/** The wind at a height above the ground from the mean wind 10 m up (m/s): it grows with the height (open country). */
double windAtHeight(double wind10, double height);

/**
 * The wind just now against its mean: between the mean (1) and the
 * strongest gusts (`peak`, their ratio to the mean), wandering slowly.
 */
double gust(double t, double peak = 1.4);

// ------------------------------------------------------------------------------------ the world round the hook

/** A solid box in the world (axis-aligned): what the hook and the load must not pass through.  (The original's `Box3`; fs::Box3 is three.js's.) */
struct CraneBox3 {
  double x0 = 0;
  double y0 = 0;
  double z0 = 0;
  double x1 = 0;
  double y1 = 0;
  double z1 = 0;
};

/** the mean wind 10 m above the ground (m/s, the direction it blows to: x, z), and its strongest gusts against it (≥ 1) */
struct CraneWind {
  double x = 0;
  double z = 0;
  double gust = 1;
};

/** What the crane needs to know of its surroundings. */
struct CraneWorld {
  /**
   * The highest surface a body of half-size (hx, hz) at (x, z) can rest on,
   * at most a little above `y` (the ground, a floor slab, a truck's bed,
   * another load) – the world's height.
   */
  std::function<double(double x, double z, double hx, double hz, double y)> floor;
  /** solid boxes the hook and the load stop at */
  std::function<std::vector<CraneBox3>()> boxes;
  /** the ground's height (for the outriggers' pads) */
  std::function<double(double x, double z)> ground;
  /** the mean wind 10 m above the ground (m/s, the direction it blows to: x, z), and its strongest gusts against it (≥ 1) */
  std::function<CraneWind()> wind;
};

/** A load as the crane takes it on the hook: how heavy, how big (half-sizes), the slings' length, how it is turned. */
struct HookLoad {
  std::string id;
  double t = 0;
  double hx = 0;
  double hy = 0;
  double hz = 0;
  double sling = 0;
  double yaw = 0;
  /** its area facing the wind (m²) */
  double area = 0;
};

/** The commands from the controls: the four axes of the two master switches (−1 … 1), and what is held of the set-up. */
struct CraneCommand {
  /** slewing: + to the left */
  double slew = 0;
  /** the left switch's other axis: hoist (+ up) – on the loader crane its inner boom (+ up) */
  double hoist = 0;
  /** the right switch, forward: luffing (+ up), the tower crane's trolley (+ out), the loader crane's outer boom (+ up) */
  double boom = 0;
  /** the right switch, sideways: telescoping (+ out), the loader crane's extensions (+ out) */
  double tele = 0;
  /** creep speed for every drive */
  bool fine = false;
  /** held: outrigger beams out (+1) or in (−1); jacks down (+1) or up (−1); levelling */
  double beams = 0;
  double jacks = 0;
  bool level = false;
};

CraneCommand noCommand();

/** What the load moment limiter shows and decides. */
struct Lmi {
  /** the load on the rope, measured (t: hook block and load) */
  double load = 0;
  /** what the chart allows here and now (t) */
  double capacity = 0;
  /** load / capacity (0 … ) */
  double ratio = 0;
  /** from the slewing axis to the hook (m) */
  double radius = 0;
  /** the boom: its length (m), its angle (deg) – the loader crane's inner boom's */
  double length = 0;
  double angle = 0;
  /** the hook's height above the ground below it (m) */
  double hookHeight = 0;
  Setting setting;
  /** the wind at the boom head (m/s, with its gusts) and what the crane may work in */
  double wind = 0;
  double windLimit = 0;
  /** the carrier's slope (deg) */
  double incline = 0;
  /** the tipping check: 1 – it tips */
  double tipping = 0;
  /** ≥ 90 %: the warning; ≥ 100 %: what raises the load moment is cut off */
  bool warn = false;
  bool stop = false;
  /** the carrier is not level enough to work (steeper than its crane's levelLimit), and what the levelling should bring it within (deg) */
  bool notLevel = false;
  double levelGood = 0;
};

/** what happened in a step, for the game to judge and show */
struct CraneEvents {
  /** the hook or the load ran into something this fast (m/s; 0: nothing) */
  double hit = 0;
  /** the load came down on something this fast (m/s; 0: it did not) */
  double landed = 0;
  /** the load came off what it stood on (it hangs now) */
  bool liftedOff = false;
  /** the limiter cut off a movement */
  bool cut = false;
};

/**
 * A crane at work.  It stands where it was put (`place`), the controls
 * move its drives (`command`, then `step`), and the hook block swings on
 * the rope below them – with a load slung under it once one is attached.
 */
class Crane {
 public:
  /** a body: where it is and how fast it moves */
  struct Body {
    double x = 0;
    double y = 0;
    double z = 0;
    double vx = 0;
    double vy = 0;
    double vz = 0;
  };
  /** a point (x, y, z) */
  struct Point {
    double x = 0;
    double y = 0;
    double z = 0;
  };
  /** where it stands: its origin in the world, turned by yaw */
  struct Base {
    double x = 0;
    double y = 0;
    double z = 0;
    double yaw = 0;
  };
  /** a slope: rise per metre along the carrier's x and z */
  struct Slope {
    double gx = 0;
    double gz = 0;
  };
  /** it has tipped over: the line it went over in the carrier frame and how far it has turned (rad) */
  struct Tipped {
    double px = 0;
    double pz = 0;
    double nx = 0;
    double nz = 0;
    double angle = 0;
    double rate = 0;
  };
  /** the weights and sideways forces on the crane now, and what it stands on */
  struct Loads {
    std::vector<Weight> weights;
    std::vector<SideForce> sides;
    std::vector<std::array<double, 2>> supports;
  };

  const CraneSpec& spec;
  const CraneType type;
  Joints q;
  /** the drives' speeds just now (per s) */
  Joints v{};
  /** where it stands: its origin in the world, turned by yaw */
  Base base;
  /** the ground's slope under the carrier on its wheels (rise per metre along its x and z) */
  Slope terrain;
  /** the outrigger beams out (0 … 1) and the jacks' strokes (m), beam by beam */
  std::vector<double> beams;
  std::vector<double> jacks;
  /** the levelling: what the jacks put right of the slope (rise per metre along x and z) */
  Slope levelFix;
  /** the ground under each pad: its height against the carrier's ground plane on its wheels (m) */
  std::vector<double> padGround;
  /** the carrier lifted off its wheels by the jacks (m) – the middle of the support */
  double lift = 0;
  /** the hook block (with the load) */
  Body hook;
  /** the load on the hook (null: none) */
  std::optional<HookLoad> load;
  /** the load (or the hook block) stands on something */
  bool resting = false;
  /** the rope's pull just now (kN), and as the limiter measures it: smoothed – and quickly while the load still stands, so that an overload is cut off before it leaves the ground */
  double tension = 0;
  Lmi lmi;
  /** what the drives are told */
  CraneCommand cmd = noCommand();
  CraneEvents events;
  /** it has tipped over (and lies there): the line it went over in the carrier frame and how far it has turned (rad) */
  std::optional<Tipped> tipped;
  /** the swing just now: how far the hook is off the plumb line below its head (m) */
  double swing = 0;
  /** the clearance below the hook or its load (m) */
  double clearance = 0;
  /** how much longer the rope is than the way from the head to the hook (m; < 0: it is stretched) */
  double slack = 0;
  double time = 0;
  /** the head (where the rope hangs) in the world, and how fast it moves */
  Body head;
  /** Masses that rest on the carrier and hold it down (the loader crane's cargo), set by the game. */
  std::vector<Weight> extra;

  explicit Crane(const CraneType& type);

  // ---------------------------------------------------------------------------- where it stands

  /**
   * Put the crane where its carrier stands (the vehicle's model origin,
   * heading, and the slope it stands on) and the hook block hanging at rest
   * below the head.
   */
  void place(double x, double y, double z, double yaw, double gx = 0, double gz = 0);

  /** A point of the carrier frame in the world (`out`: anything with x, y, z). */
  template <typename P>
  P& toWorld(double x, double y, double z, P& out) const {
    const Base& b = base;
    const double c = std::cos(b.yaw);
    const double s = std::sin(b.yaw);
    const Slope t = tilt();
    out.x = b.x + x * c + z * s;
    out.z = b.z - x * s + z * c;
    out.y = b.y + lift + y + t.gx * x + t.gz * z;
    return out;
  }

  /** A place of the world in the carrier's plan (x, z). */
  std::array<double, 2> toCarrier(double x, double z) const;

  /** The carrier's slope as it stands now: the ground's, put right by the levelling (rise per metre). */
  Slope tilt() const;

  /** The carrier's slope in degrees. */
  double incline() const;

  /** The head the rope hangs from, in the world (`out`: anything with x, y, z). */
  template <typename P>
  P& headWorld(P& out) const {
    const HeadAt h = headOf(type, q);
    const auto [dx, dz] = boomDir(q.slew);
    return toWorld(spec.slewX + h.a * dx, h.y, spec.slewZ + h.a * dz, out);
  }

  /** The radius: from the slewing axis to the hook (m). */
  double radius() const;

  // ---------------------------------------------------------------------------- the outriggers

  /** Every jack's pad stands on the ground. */
  bool padsDown(const CraneWorld& world);

  /** How far jack `i` must go down until its pad stands on the ground (m), the carrier on its wheels. */
  double gap(int i, const CraneWorld& world);

  /** The beams' least extension: what the chart goes by. */
  double beamSetting() const;

  /** The carrier stands on its pads (and, on the mobile crane, its wheels are free of the ground). */
  bool onOutriggers(const CraneWorld& world);

  /** The setting the chart is read in: by how far the beams are out – or the tyres. */
  Setting setting(const CraneWorld& world);

  /** The boom is down in its rest – folded, for the loader crane – and the crane may drive. */
  bool stowed() const;

  /** Outriggers in and jacks up: ready to drive (with the boom stowed). */
  bool outriggersIn() const;

  // ---------------------------------------------------------------------------- the drives

  /** Does moving `key` this way (sign) raise the load moment?  Hoisting a load up does; so does anything that takes the hook further out. */
  bool momentUp(const JointKey& key, double dir);

  /** Whether the crane may work at all: level enough on what it stands on. */
  bool mayWork(const CraneWorld& world) const;

  // ---------------------------------------------------------------------------- the load

  /** what hangs on the rope: the hook block and the load */
  double mass() const;

  /** How far below the hook's middle the bottom of what hangs there is: the hook block's, or the load's under its slings (they hang from the hook's bowl). */
  double drop() const;

  /** The half-size of what hangs there, in the plan (x, z of the world), turned with the load. */
  std::array<double, 2> footprint() const;

  /**
   * Take a load on the hook: the slinger hooks the slings in, the hook block
   * comes to stand over the load's middle at the slings' length.
   */
  void attach(const HookLoad& load, double x, double top, double z);

  /** Let the load go (it stands where it is): the hook block hangs alone again. */
  std::optional<HookLoad> release();

  // ---------------------------------------------------------------------------- a step

  /** One step of everything: the drives, the head, the hook on its rope, the limiter, the tipping check. */
  void step(double dt, const CraneWorld& world);

  /** The weights and sideways forces on the crane now (carrier frame), and what it stands on – for the tipping check. */
  Loads loads(const CraneWorld& world, const std::vector<Weight>& extra = {});

 private:
  /** how a joint moves the head: along the radius (da) and up (dy) */
  struct Rate {
    double da = 0;
    double dy = 0;
  };
  struct Drive {
    JointKey key;
    double cmd = 0;
    DriveSpec spec;
  };
  double measured_ = 0;
  double quick_ = 0;
  /** how long it has been beyond tipping */
  double overT_ = 0;
  /** the limiter's cut-off is on (it lets go again a little below 100 %) */
  bool cutting_ = false;
  Point flat_;

  Point& toWorldFlat(double x, double z);
  /** Beams, jacks and levelling, while their buttons are held. */
  void stepOutriggers(double dt, const CraneWorld& world);
  /** what levelling asks of jack i beyond the others (m) */
  double levelOffset(int i) const;
  double padX(int i) const;
  /** How far the jacks hold the carrier up: in the middle of the support, the levelling taken out. */
  void updateLift(const CraneWorld& world);
  /** the drives of this crane: which joint each command moves, and its range */
  std::vector<Drive> drives() const;
  /** How a joint moves the head: along the radius and up (per unit of the joint), from the joints as they are. */
  Rate headRate(const JointKey& key);
  void stepDrives(double dt, const CraneWorld& world);
  void stepHook(double dt, const CraneWorld& world);
  /** the pads where they are (and the loader crane's wheels) */
  std::vector<std::array<double, 2>> padSupports() const;
  std::array<double, 2> windInCarrier(double x, double z) const;
  void updateLmi(double dt, const CraneWorld& world);
};

/** The tipping lines of the supports (for drawing which way a crane went over). */
std::vector<TippingLine> linesOf(const std::vector<std::array<double, 2>>& supports);

}  // namespace fs

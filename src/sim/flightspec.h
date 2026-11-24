// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/flightspec.ts @ v1.7.7
/**
 * The light aeroplane of the game: a generic two-seat high-wing trainer of
 * the kind flying schools use – no real type is reproduced.  These are the
 * measures both the flight model (sim/flight.ts) and the 3D model
 * (render/plane.ts) are built from, so that the wheels the physics stands on
 * are the wheels that are drawn.
 *
 * Body frame as for every vehicle of the game: x to the right, y up, z to
 * the back (the nose looks towards -z); the origin is the centre of mass.
 * Metres, kilograms, seconds.
 */
#pragma once

#include <array>
#include <vector>

namespace fs {

struct GearLeg {
  /** wheel centre with the strut fully extended (no load) */
  double x = 0;
  double y = 0;
  double z = 0;
  double radius = 0;
  /** how far the strut can be pushed in (m) */
  double travel = 0;
  /** the nose wheel, steered with the rudder pedals */
  bool steers = false;
};

struct AircraftGeometry {
  /** as flown: two people, half the fuel */
  double mass = 0;
  double length = 0;
  double height = 0;
  double span = 0;
  double wingArea = 0;
  /** mean chord */
  double chord = 0;
  /** height of the wing's lower surface at the root, and where its leading and trailing edges are */
  double wingY = 0;
  double wingLE = 0;
  double wingTE = 0;
  /** degrees */
  double dihedral = 0;
  /** propeller hub and diameter */
  std::array<double, 3> propHub{};
  double propDiameter = 0;
  /** horizontal tail: quarter-chord position, span, chord; fin: position and top */
  double stabZ = 0;
  double stabY = 0;
  double stabSpan = 0;
  double stabChord = 0;
  double finZ = 0;
  double finTop = 0;
  /** nose wheel first, then the main wheels left and right */
  std::vector<GearLeg> gear;
  /** the pilot's eyes in the left seat */
  std::array<double, 3> eye{};
  /** speeds (m/s): stall with full flaps / clean, rotation, best climb, cruise, never exceed */
  double vs0 = 0;
  double vs1 = 0;
  double vr = 0;
  double vy = 0;
  double vcruise = 0;
  double vne = 0;
  /** maximum flap deflection (degrees); the lever has the detents 0, 10, 20 and 30 */
  double flapsMax = 0;
  /** engine (PS) – the power the game offers to choose from, and its default */
  std::vector<double> powers;
  double power = 0;
  /** usable fuel (litres) */
  double fuel = 0;
};

inline const AircraftGeometry TRAINER = [] {
  AircraftGeometry g;
  g.mass = 1000;
  g.length = 8.2;
  g.height = 2.7;
  g.span = 10.9;
  g.wingArea = 16.2;
  g.chord = 1.5;
  g.wingY = 0.95;
  g.wingLE = -0.55;
  g.wingTE = 0.95;
  g.dihedral = 1.5;
  g.propHub = {0, 0.05, -2.45};
  g.propDiameter = 1.9;
  g.stabZ = 4.6;
  g.stabY = 0.15;
  g.stabSpan = 3.4;
  g.stabChord = 0.9;
  g.finZ = 4.7;
  g.finTop = 1.8;
  g.gear = {
      {0, -1.05, -1.5, 0.17, 0.15, true},
      {-1.15, -1.05, 0.35, 0.19, 0.1, false},
      {1.15, -1.05, 0.35, 0.19, 0.1, false},
  };
  g.eye = {-0.3, 0.55, -0.45};
  g.vs0 = 23;
  g.vs1 = 26;
  g.vr = 28;
  g.vy = 38;
  g.vcruise = 56;
  g.vne = 82;
  g.flapsMax = 30;
  g.powers = {100, 116, 160, 180};
  g.power = 160;
  g.fuel = 150;
  return g;
}();

}  // namespace fs

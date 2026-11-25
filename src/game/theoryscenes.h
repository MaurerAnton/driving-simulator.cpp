// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/theoryscenes.ts @ v1.7.7
/**
 * More theory lessons (game/theory.ts runs them like the first thirteen):
 * where you may stop and where you may park, the distance to the car ahead,
 * an obstacle and a cyclist on a country road, a bus leaving its stop, the
 * headlights at night, a wall of fog, an accident ahead, the lorry turning
 * right, a lane that ends, a tunnel, an old lady with a rollator, a deer at
 * dusk, a police officer directing the traffic and a flagman at roadworks.
 *
 * The texts are written from the StVO itself (and § 323c of the criminal
 * code for first aid); the rules of thumb – half the speedometer, the
 * stopping distance, the warning triangle's 100 m – are the ones every
 * driving school teaches.  Every scene keeps to what the tests of
 * tests/game/theory.test.ts ask of all of them: nobody touches anybody,
 * vehicles stay on the asphalt, nobody brakes like a wall.
 *
 * Besides the scripts: what the people at work and the animals of these
 * scenes do – which way somebody faces, a police officer's arms, a
 * flagman's sign, the headlights – as plain functions of the script's time,
 * for render/theory.ts and the tests.  Only types come from game/theory.ts
 * (which lists these lessons after its own).
 */
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "game/theory.h"

namespace fs::theory {

// ------------------------------------------------------------------ timing
/** One leg of a script: on for `dt` seconds to the speed `v` (evenly faster or slower), on to `to` metres arriving at `v`, or standing for `wait` seconds. */
struct Leg {
  std::optional<double> dt{};
  std::optional<double> to{};
  std::optional<double> wait{};
  double v = 0;
};
inline Leg legDt(double dt, double v) { return Leg{.dt = dt, .v = v}; }
inline Leg legTo(double to, double v) { return Leg{.to = to, .v = v}; }
inline Leg legWait(double wait) { return Leg{.wait = wait}; }

/**
 * The keys of a script from where it starts (time, metres, speed) and its
 * legs: each leg speeds up or slows down evenly, so that the curve through
 * the keys (theory.ts progressAt) brakes and pulls away as smoothly as a
 * driver does.
 */
std::vector<Key> plan(double t, double s, double v, const std::vector<Leg>& legs);

// ------------------------------------------------------------- what people do
/** Which way a person faces at time t (a yaw of the stage): as `base` (its path) until the first turn, then turning smoothly. */
double faceAt(const ActorDef& def, double t, double base);

struct GestureNow {
  Gesture sign;
  double since = 0;
  Gesture was;
};

/** A person's sign at time t, and since when it is shown (nullopt: none). */
std::optional<GestureNow> gestureAt(const ActorDef& def, double t);

/** How a person holds the arms at time t (nullopt: as anybody walking or standing). */
std::optional<std::array<double, 4>> armsAt(const ActorDef& def, double t);

/** How far a flagman's sign is turned at time t: 0 its red side to the one he faces, 1 its green side. */
double paddleAt(const ActorDef& def, double t);

/** An actor's headlights at time t: 0 off, 1 dipped, 2 full beam. */
int beamAt(const ActorDef& def, double t);

/** The lessons of this file, in the order of the list (after the first thirteen). */
const std::vector<LessonDef>& MORE_THEORY();

/** For the tests: the ids of the lessons of this file. */
std::vector<std::string> MORE_THEORY_IDS();

}  // namespace fs::theory

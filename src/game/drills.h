// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/drills.ts @ v1.7.7
/**
 * The basic driving tasks of the class B practical test ("Grundfahraufgaben",
 * Anlage 7 FeV, Nr. 2.1.4.2) on the practice yard, with the car.  The test
 * asks three of them: one of group a – reversing round a corner to the right
 * ("Fahren nach rechts rückwärts unter Ausnutzung einer Einmündung, Kreuzung
 * oder Einfahrt") or parallel parking ("Rückwärtsfahren in eine Parklücke,
 * Längsaufstellung") – and two of group b – turning round ("Umkehren"), bay
 * parking ("Einfahren in eine Parklücke, Quer- oder Schrägaufstellung") or
 * the emergency stop ("Abbremsen mit höchstmöglicher Verzögerung"):
 *
 *   – parallel parking into a gap between two parked cars, at most 30 cm
 *     from the kerb,
 *   – bay parking forwards and backwards between lines,
 *   – reversing round a corner to the right close to the kerb – and, as
 *     practice only (no basic task of class B), reversing straight along a
 *     marked lane,
 *   – turning round in a narrow street without touching the kerbs,
 *   – the emergency stop from 30 km/h at the instructor's signal,
 *   – and, as practice for the test drive (Nr. 2.1.5) but no basic task of
 *     class B, the hill start on a ramp of 10 %, with the handbrake, without
 *     rolling back.
 *
 * This file holds the layouts – in the yard's own frame, like those of the
 * class CE tasks (game/ce.h) – and the judge of each task: pure logic, no
 * drawing.  The judge is told where the car stands, how fast it goes and
 * what the driver does with the brakes each moment; it says which step of
 * the guided task is next, what the instructor remarks, the faults, and in
 * the end a score with up to three stars.  The game (game_drills.cpp) runs
 * it, render/drillyard.h draws what the yard of the CE tasks does not have
 * (parked cars, kerb stones, the ramp's sides).
 */
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game/ce.h"
#include "i18n/index.h"
#include "sim/rules.h"

namespace fs {

/** 'parallel' | 'bayFwd' | 'bayBack' | 'backStraight' | 'backCorner' | 'turn' | 'hill' | 'brake' */
using DrillId = std::string;
inline const std::vector<DrillId> DRILL_IDS = {"parallel", "bayFwd", "bayBack", "backStraight", "backCorner", "turn", "hill", "brake"};

/** What the judge needs of the vehicle: its outline and its wheels from the model origin (as in game/vehicles.h). */
struct CarShape {
  double front = 0;
  double rear = 0;
  double hw = 0;
  /** the axles: metres ahead (+) of the origin or behind */
  std::vector<double> axles;
  /** half the track (the middle of the tyres) */
  double track = 0;
};

/** the compact car of class B (render/carBody.h COMPACT_DIMS) */
inline const CarShape CAR_SHAPE = {.front = 2.195, .rear = 2.085, .hw = 0.895, .axles = {1.315, -1.315}, .track = 0.77};

/**
 * A ramp: up from u0 to u1, level to u2, down to u3, `h` high, level
 * across from w0 to w1 and falling away beside that over `side` metres.
 */
struct Hill {
  double u0 = 0;
  double u1 = 0;
  double u2 = 0;
  double u3 = 0;
  double w0 = 0;
  double w1 = 0;
  double h = 0;
  double side = 0;
};

/** How much the ramp raises the ground at a place of the yard (m). */
double hillRise(const Hill& h, double u, double w);

struct DrillLayout : TaskLayout {
  DrillId id;
  /** what bounds the area: kerbs (touching one is a fault, driving over it fails) or painted lines – 'kerb' | 'line' */
  std::string boundary;
  /** kerb stones to draw: u0, w0, u1, w1 */
  std::vector<std::array<double, 4>> kerbs;
  /** the parked cars (yard coordinates; hl along their heading) – touching one fails the task */
  std::vector<Box> parked;
  /** the ramp of the hill start */
  std::optional<Hill> hill;
  /** the guided steps: texts "drill.step.<id>" */
  std::vector<std::string> steps;
  /** moves (forwards or backwards) the task itself takes; more are corrections (-1: not counted) */
  int moves = 0;
  /** seconds an unhurried driver needs */
  double par = 0;
  /** the camera's distance (m; 0: the vehicle's own) */
  double zoom = 0;
};

/** facing along +u, −u, −w, +w of the yard */
inline constexpr double EAST = -3.141592653589793 / 2;
inline constexpr double WEST = 3.141592653589793 / 2;
inline constexpr double NORTH = 0;
inline constexpr double SOUTH = 3.141592653589793;

// (MAX_CORRECTIONS = 2: game/ce.h has it, the same for the tasks of both classes)

/** the parallel parking street: kerb on the right (w), the gap's length for a car */
struct ParallelPlan {
  double kerb, gap, from, to;
};
inline constexpr ParallelPlan PARALLEL = {6, 6.2, -40, 30};
/** the bay parking: the aisle up to w = 7, the bays behind it, 2.75 m wide and 5.5 m deep, the one to park in round u = 0 */
struct BayPlan {
  double aisle, depth, width;
};
inline constexpr BayPlan BAYS = {7, 5.5, 2.75};
/** reversing round the corner to the right: the side road between u = −6 and 0, going off at w = 0 */
struct CornerPlan {
  double u0, u1, r, len;
};
inline constexpr CornerPlan CORNER = {-6, 0, 4, 24};
/** the hill start's line on the ramp (u) */
inline constexpr double HILL_LINE = -2;
/** the emergency stop: at about 30 km/h (m/s), signalled once it has been held a moment */
inline constexpr double BRAKE_SPEED = 30 / 3.6;

DrillLayout drillLayout(const DrillId& id, const CarShape& shape = CAR_SHAPE);

// ---- judging ----------------------------------------------------------------------

/** What the judge is told each moment (the pose in yard coordinates, of the model origin). */
struct DrillInput {
  Pose pose;
  /** along the heading (m/s; negative: reversing) */
  double speed = 0;
  bool handbrake = false;
  /** the brake pedal, 0 … 1 */
  double brake = 0;
  bool engine = true;
};

/** 'minor' | 'major' | 'fail' */
using DrillSeverity = std::string;

struct DrillFault {
  std::string id;
  DrillSeverity severity;
  ViolationData data;
};

struct DrillStatus {
  /** 'run' | 'done' | 'failed' */
  std::string state = "run";
  /** the guided step now (index into the layout's steps) */
  int step = 0;
  /** corrections so far (moves beyond those the task takes) */
  int corrections = 0;
  std::vector<DrillFault> faults;
  /** what the instructor says now (a text key; '' for nothing) and its values */
  std::string remark;
  TrParams remarkData;
  /** the emergency stop: the instructor gives the signal in this step */
  bool signal = false;
  /** the car ran into a parked one: hold it where it is */
  bool stop = false;
  /** the distance to the kerb that counts (m; NaN where none counts) */
  double kerb = NAN;
  /** the hill start: how far it rolled back (m) */
  double rollback = 0;
};

struct DrillResult {
  DrillId id;
  /** its name, a text key */
  std::string text;
  double time = 0;
  double score = 0;
  int stars = 0;
  int minor = 0;
  int major = 0;
  bool failed = false;
  /** what was measured: a text key and its values */
  std::string measure;
  TrParams measureData;
};

/** Do two rectangles given by their corners (x, z pairs) overlap? */
bool rectsOverlap(const std::array<double, 8>& a, const std::array<double, 8>& b);

/** How far a point is from the nearest kerb of a layout (m). */
double kerbDistance(const DrillLayout& lay, double u, double w);

class DrillJudge {
 public:
  const DrillId id;
  const CarShape shape;
  const DrillLayout layout;
  DrillStatus status;
  double time = 0;
  int minor = 0;
  int major = 0;

  explicit DrillJudge(const DrillId& id, const CarShape& shape = CAR_SHAPE, std::function<double()> rand = nullptr);

  /** The car's corners now (front left, front right, rear right, rear left: x, z pairs). */
  std::array<double, 8> carCorners(const Pose& p) const;
  const DrillStatus& update(const DrillInput& s, double dt);
  /** How many moves have been made (forwards and backwards in turn). */
  int moveCount() const { return moves_; }

  struct Why {
    std::string key;
    TrParams data;
  };
  /** Why the car standing here is not done yet (null: it is). */
  std::optional<Why> whyNot(const Pose& p) const { return whyNot(p, carCorners(p)); }
  std::optional<Why> whyNot(const Pose& p, const std::array<double, 8>& tc) const;

  /** The task's score: 100, less for each fault, for a long time, for what was measured; failed: none. */
  DrillResult result() const;

 private:
  std::function<double()> rand_;
  std::vector<std::array<double, 8>> parkedCorners_;
  // the moves: the direction now (1, −1, 0 before the first), how far against it since
  double dir_ = 0;
  double against_ = 0;
  int moves_ = 0;
  // the boundary
  bool onKerb_ = false;
  bool outside_ = false;
  // standing still with the handbrake on, and whether the instructor has said why it is not done yet
  double held_ = 0;
  bool told_ = false;
  // the hill start
  double ref_ = NAN;
  double secured_ = 0;
  bool rollTold_ = false;
  /** the engine ran in the last step (null: not known yet – it may be started on the yard) */
  std::optional<bool> engineWas_;
  bool hbTold_ = false;
  bool pastTold_ = false;
  // the emergency stop
  double atSpeed_ = 0;
  double delay_ = 0;
  double signalT_ = -1;
  double v0_ = 0;
  double run_ = 0;
  double reaction_ = -1;
  double maxBrake_ = 0;
  double stopDist_ = NAN;
  double ideal_ = NAN;

  void fault(const std::string& id, const DrillSeverity& severity, const ViolationData& data = {});
  void say(const std::string& key, const TrParams& data = {});
  void manoeuvre(const DrillInput& s, double dt, const std::array<double, 8>& tc);
  void hill(const DrillInput& s, double dt);
  void brake(const DrillInput& s, double dt);
  void finish();
};

/**
 * The tasks of an exam-like round, chosen at random as the examiner does
 * (Anlage 7 FeV, Nr. 2.1.4.2): one of group a (reversing round the corner
 * or parallel parking), then two different ones of group b (turning round,
 * bay parking forwards or backwards, the emergency stop) – the emergency
 * stop, if chosen, last.
 */
std::vector<DrillId> examDrills(const std::function<double()>& rand = nullptr);

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/ce.ts @ v1.7.7
/**
 * Class CE: what the practical test asks of a truck with trailer besides the
 * drive in traffic (Anlage 7 FeV, 2.1.3 and 2.1.4.5.2 – combinations with a
 * rigid-drawbar trailer):
 *
 *   – coupling and uncoupling ("Verbinden und Trennen"),
 *   – reversing round a corner to the left,
 *   – reversing with an offset to the right up to a loading ramp.
 *
 * This file holds the practice yard's layouts and the judging of the three
 * tasks – pure logic, no drawing.  The yard is laid over an open field of
 * the map while such a lesson runs (see tools/mapgen/yard.ts).
 */
#pragma once

#include <array>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "world/types.h"

namespace fs {

/** A place and a heading (0 = "north" of the frame, positive = to the left). */
struct Pose {
  double x = 0;
  double z = 0;
  double yaw = 0;
};

/** A rectangle on the ground: middle, heading, half length, half width. */
struct Box : Pose {
  double hl = 0;
  double hw = 0;
};

/**
 * The yard's own coordinates: u along its long side, w across, the middle
 * at (0, 0).  A heading in the yard is counted like one on the map with u in
 * the place of east and w in the place of south.
 */
class YardFrame {
 public:
  const YardJson yard;

  explicit YardFrame(const YardJson& yard);

  std::array<double, 2> toWorld(double u, double w) const;
  std::array<double, 2> toLocal(double x, double z) const;
  double yawToWorld(double yaw) const { return yaw + turn_; }
  double yawToLocal(double yaw) const { return yaw - turn_; }
  Pose poseToWorld(const Pose& p) const;
  Pose poseToLocal(const Pose& p) const;

 private:
  double cos_;
  double sin_;
  double turn_;
};

// ---- the vehicles' outlines ---------------------------------------------------

struct RigOutline {
  double front;
  double rear;
  double hw;
  double hitch;
};
/** The truck's outline from its model origin (the ground point below the middle of the wheelbase). */
inline constexpr RigOutline TRUCK_OUTLINE = {.front = 3.75, .rear = 4.45, .hw = 1.25, .hitch = 4.05};
/** The trailer's outline from the middle of its axle group. */
inline constexpr RigOutline TRAILER_OUTLINE = {.front = 3.9, .rear = 2.5, .hw = 1.25, .hitch = 5.4};

/**
 * Where the tyres stand, seen from the same points: metres ahead (+) or
 * behind, and half the width over the outer tyres.  A kerb is driven over
 * with a wheel – the body may swing over it.
 */
struct Axles {
  std::vector<double> at;
  double hw;
};
inline const Axles TRUCK_AXLES = {{2.35, -2.35}, 1.25};
inline const Axles TRAILER_AXLES = {{0.65, -0.65}, 1.25};

/** What the judge needs of a combination: the outlines and the axles of the towing vehicle and of the trailer. */
struct RigShape {
  struct Outline {
    double front;
    double rear;
    double hw;
  };
  Outline truck;
  Outline trailer;
  Axles truckAxles;
  Axles trailerAxles;
};
/** the truck with the drawbar trailer */
inline const RigShape DRAWBAR_RIG = {{TRUCK_OUTLINE.front, TRUCK_OUTLINE.rear, TRUCK_OUTLINE.hw}, {TRAILER_OUTLINE.front, TRAILER_OUTLINE.rear, TRAILER_OUTLINE.hw}, TRUCK_AXLES, TRAILER_AXLES};

/** The outer ends of the axles of a vehicle standing at a pose: x, z pairs (appended to `out`). */
std::vector<double>& wheelPoints(const Pose& p, const Axles& axles, std::vector<double>& out);

/** The four corners of an outline standing at a pose: x, z pairs (front left, front right, rear right, rear left). */
std::array<double, 8> corners(const Pose& p, double front, double rear, double hw);

/** Where the trailer's axle group is when it hangs in line behind a truck standing at `truck`. */
Pose trailerBehind(const Pose& truck);

// ---- layouts --------------------------------------------------------------------

/** 'corner' | 'ramp' | 'couple' */
using TaskId = std::string;

struct Rect {
  double u0 = 0;
  double u1 = 0;
  double w0 = 0;
  double w1 = 0;
};

struct Chamfer {
  double u = 0;
  double w = 0;
  double du = 0;
  double dw = 0;
};

struct RampRect : Rect {
  double h = 0;
};

struct TaskLayout {
  /** where the truck stands at the start (yard coordinates), the trailer in line behind it */
  Pose start;
  /** the paved areas the vehicles must stay on: together they are the "road" */
  std::vector<Rect> allowed;
  /**
   * Cut-off corners of a junction (a kerb is rounded, not square): right
   * triangles with the right angle at (u, w) and legs of du along u and dw
   * along w (signed).  They belong to the paved area.
   */
  std::vector<Chamfer> chamfers;
  /** boundary lines to paint: u0, w0, u1, w1 */
  std::vector<std::array<double, 4>> lines;
  /** cones: u, w */
  std::vector<std::array<double, 2>> cones;
  /** the loading ramp, if the task has one */
  std::optional<RampRect> ramp;
  /** where the combination has to stand in the end */
  Rect goal;
  /** the goal is marked on the ground (not for coupling, which is done anywhere on the pad) */
  bool goalMarked = false;
};

TaskLayout taskLayout(const TaskId& id);

// ---- judging the two manoeuvres --------------------------------------------------

/** What the judge needs to know each moment (poses in yard coordinates). */
struct RigState {
  Pose truck;
  Pose trailer;
  /** speed of the truck along its heading (m/s; negative: reversing) */
  double speed = 0;
  bool handbrake = false;
};

/** 'ceLine' | 'ceCorrections' | 'ceRamp' */
using ManoeuvreFault = std::string;

struct ManoeuvreStatus {
  /** 'run' | 'done' | 'failed' */
  std::string state = "run";
  /** times the driver pulled forward again to correct (two are allowed) */
  int corrections = 0;
  /** times a vehicle left the paved area */
  int excursions = 0;
  /** faults that occurred in this step */
  std::vector<ManoeuvreFault> faults;
  /** how far the rear end is from the ramp (ramp task; NaN otherwise) */
  double gap = NAN;
};

inline constexpr int MAX_CORRECTIONS = 2;

/**
 * How far a point lies off the paved area of a task (m); 0 on it.  `pad`
 * widens the area (the asphalt reaches a little beyond the lines).
 */
double offArea(const TaskLayout& lay, double u, double w, double pad = 0);

class ManoeuvreJudge {
 public:
  /** 'corner' | 'ramp' */
  const TaskId id;
  const TaskLayout layout;
  ManoeuvreStatus status;
  /** the combination judged (the articulated lorry has its own: game/semice.ts) */
  const RigShape rig;

  explicit ManoeuvreJudge(const TaskId& id, const RigShape& rig = DRAWBAR_RIG);

  const ManoeuvreStatus& update(const RigState& rig, double dt);

  /** Does the combination stand where and how the task wants it? */
  bool arrived(const RigState& rig) const;
  bool arrived(const RigState& rig, const std::array<double, 8>& tc, const std::array<double, 8>& rc) const;

 private:
  bool outside_ = false;
  /** metres driven backwards / forwards since the last change of direction */
  double back_ = 0;
  double fwd_ = 0;
  bool reversed_ = false;
  double held_ = 0;
};

// ---- coupling and uncoupling -------------------------------------------------------

/** 'uPark' | 'uBrake' | … | 'cTest' */
using CouplingStepId = std::string;
/** 'ceSecure' | 'ceAir' | 'ceOpen' | 'ceLocked' | 'ceRelease' | 'ceLights' | 'ceHeight' | 'ceFast' */
using CouplingFault = std::string;

/**
 * The steps of uncoupling and coupling.  'do' steps are things done with the
 * truck (the judge sees them happen); the others are things done on foot
 * around the vehicles – the driver picks them from a list, and the order
 * counts.
 */
struct CouplingStep {
  CouplingStepId id;
  /** 'do' | 'act' */
  std::string kind;
  std::vector<CouplingStepId> needs;
  /** the fault of doing it too early ('': none named) */
  CouplingFault why;
};
extern const std::vector<CouplingStep> COUPLING_STEPS;

/** What the procedure needs to know each moment. */
struct CouplingState {
  /** speed of the truck along its heading (m/s) */
  double speed = 0;
  bool handbrake = true;
  /** the trailer hangs on the truck */
  bool coupled = true;
  /** distance from the truck's coupling to the trailer's eye (m) and how far out of line they are (rad) */
  double dist = 0;
  double angle = 0;
};

struct CouplingStatus {
  /** 'run' | 'done' */
  std::string state = "run";
  /** the 'do' step the driver is to carry out now, or null while there are things to do on foot */
  std::optional<CouplingStepId> todo = "uPark";
  /** the trailer is to be let go / to be taken on in this step */
  bool uncouple = false;
  bool couple = false;
  std::vector<CouplingFault> faults;
};

class CouplingProcedure {
 public:
  std::set<CouplingStepId> done;
  CouplingStatus status;
  /** where to stop before taking the trailer up: this far from it at least and at most (m – the articulated lorry's: game/semice.ts) */
  const std::array<double, 2> approach;

  explicit CouplingProcedure(const std::array<double, 2>& approach = {0.25, 1.6});

  /** May the driver get out and do things on foot? (The truck stands, secured.) */
  bool onFoot(const CouplingState& s) const;

  /**
   * The things on foot that can be done now and are not done yet, in the
   * order of the catalogue.  Which of them comes first is for the driver to
   * know.
   */
  std::vector<CouplingStepId> choices() const;

  /**
   * The driver chose a thing to do on foot.  Returns the fault if it is not
   * its turn yet (it is then not done), or null.
   */
  std::optional<CouplingFault> act(const CouplingStepId& id, const CouplingState& s);

  const CouplingStatus& update(const CouplingState& s, double dt);

 private:
  /** metres driven since a mark, for the 'do' steps that are distances */
  double run_ = 0;
  bool braked_ = false;
  bool ready(const CouplingStepId& id) const;
  /** Has everything that is done with the truck before step `id` been done? */
  bool reached(const CouplingStepId& id) const;
};

}  // namespace fs

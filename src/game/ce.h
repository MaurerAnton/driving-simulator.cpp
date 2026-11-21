// SPDX-License-Identifier: AGPL-3.0-only
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
 * the map while such a lesson runs (see the original's tools/mapgen/yard.ts).
 *
 * Origin: src/game/ce.ts (MaurerAnton/driving-simulator @ ff37c5d, v0.7.0).
 */
#pragma once

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "world/types.h"

namespace fs {

/** A place and a heading (0 = "north" of the frame, positive = to the left). */
struct Pose {
  double x = 0, z = 0, yaw = 0;
};

/** A rectangle on the ground: middle, heading, half length, half width. */
struct Box : Pose {
  double hl = 0, hw = 0;
};

/**
 * The yard's own coordinates: u along its long side, w across, the middle
 * at (0, 0).  A heading in the yard is counted like one on the map with u in
 * the place of east and w in the place of south.
 */
class YardFrame {
 public:
  explicit YardFrame(const YardJson& yardIn);

  const YardJson yard;

  std::array<double, 2> toWorld(double u, double w) const {
    return {yard.x + u * cos_ - w * sin_, yard.z + u * sin_ + w * cos_};
  }
  std::array<double, 2> toLocal(double x, double z) const {
    const double dx = x - yard.x;
    const double dz = z - yard.z;
    return {dx * cos_ + dz * sin_, -dx * sin_ + dz * cos_};
  }
  double yawToWorld(double yaw) const { return yaw + turn_; }
  double yawToLocal(double yaw) const { return yaw - turn_; }
  Pose poseToWorld(const Pose& p) const {
    const auto xz = toWorld(p.x, p.z);
    return {xz[0], xz[1], yawToWorld(p.yaw)};
  }
  Pose poseToLocal(const Pose& p) const {
    const auto xz = toLocal(p.x, p.z);
    return {xz[0], xz[1], yawToLocal(p.yaw)};
  }

 private:
  double cos_, sin_, turn_;
};

// ---- the vehicles' outlines ---------------------------------------------------

/** The truck's outline from its model origin (the ground point below the middle of the wheelbase). */
struct Outline {
  double front, rear, hw, hitch;
};
extern const Outline TRUCK_OUTLINE;
extern const Outline TRAILER_OUTLINE;

/**
 * Where the tyres stand, seen from the same points: metres ahead (+) or
 * behind, and half the width over the outer tyres.  A kerb is driven over
 * with a wheel – the body may swing over it.
 */
struct Axles {
  std::vector<double> at;
  double hw;
};
extern const Axles TRUCK_AXLES;
extern const Axles TRAILER_AXLES;

/** The outer ends of the axles of a vehicle standing at a pose: x, z pairs. */
std::vector<double> wheelPoints(const Pose& p, const Axles& axles, std::vector<double> out = {});

/** The four corners of an outline standing at a pose: x, z pairs (front left, front right, rear right, rear left). */
std::vector<double> corners(const Pose& p, double front, double rear, double hw);

/** Where the trailer's axle group is when it hangs in line behind a truck standing at `truck`. */
Pose trailerBehind(const Pose& truck);

// ---- layouts --------------------------------------------------------------------

using TaskId = std::string;   // 'corner' | 'ramp' | 'couple'

struct Rect {
  double u0 = 0, u1 = 0, w0 = 0, w1 = 0;
};

struct Chamfer {
  double u = 0, w = 0, du = 0, dw = 0;
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
  bool hasRamp = false;
  Rect ramp;
  double rampH = 0;
  /** where the combination has to stand in the end */
  Rect goal;
  /** the goal is marked on the ground (not for coupling, which is done anywhere on the pad) */
  bool goalMarked = false;
};

TaskLayout taskLayout(const std::string& id);

/** What the judge needs to know each moment (poses in yard coordinates). */
struct RigState {
  Pose truck;
  Pose trailer;
  /** speed of the truck along its heading (m/s; negative: reversing) */
  double speed = 0;
  bool handbrake = false;
};

struct ManoeuvreStatus {
  std::string state = "run";   // 'run' | 'done' | 'failed'
  /** times the driver pulled forward again to correct (two are allowed) */
  int corrections = 0;
  /** times a vehicle left the paved area */
  int excursions = 0;
  /** faults that occurred in this step */
  std::vector<std::string> faults;
  /** how far the rear end is from the ramp (ramp task; NaN otherwise) */
  double gap = NAN;
};

constexpr int MAX_CORRECTIONS = 2;

/**
 * How far a point lies off the paved area of a task (m); 0 on it.  `pad`
 * widens the area (the asphalt reaches a little beyond the lines).
 */
double offArea(const TaskLayout& lay, double u, double w, double pad = 0);

/** 'corner' or 'ramp': the judge of one basic manoeuvre. */
class ManoeuvreJudge {
 public:
  explicit ManoeuvreJudge(const std::string& id);

  const std::string id;   // 'corner' | 'ramp'
  const TaskLayout layout;
  ManoeuvreStatus status;

  ManoeuvreStatus update(const RigState& rig, double dt);
  /** Does the combination stand where and how the task wants it? */
  bool arrived(const RigState& rig) const;

 private:
  bool outside_ = false;
  /** metres driven backwards / forwards since the last change of direction */
  double back_ = 0, fwd_ = 0;
  bool reversed_ = false;
  double held_ = 0;
};

// ---- coupling and uncoupling -------------------------------------------------------

struct CouplingStep {
  const char* id;
  /** 'do': done with the truck; 'act': done on foot */
  const char* kind;
  std::vector<std::string> needs;
  /** the fault when it is done out of turn (a step without it falls back to ceSecure) */
  const char* why;
};

/** The steps of uncoupling and coupling, in the order of the catalogue. */
extern const std::vector<CouplingStep> COUPLING_STEPS;

/** What the procedure needs to know each moment. */
struct CouplingState {
  /** speed of the truck along its heading (m/s) */
  double speed = 0;
  bool handbrake = false;
  /** the trailer hangs on the truck */
  bool coupled = false;
  /** distance from the truck's coupling to the trailer's eye (m) and how far out of line they are (rad) */
  double dist = 0, angle = 0;
};

struct CouplingStatus {
  std::string state = "run";   // 'run' | 'done'
  /** the 'do' step the driver is to carry out now, or "" while there are things to do on foot */
  std::string todo = "uPark";
  /** the trailer is to be let go / to be taken on in this step */
  bool uncouple = false, couple = false;
  std::vector<std::string> faults;
};

class CouplingProcedure {
 public:
  std::vector<std::string> done;
  CouplingStatus status;

  CouplingProcedure();

  /** May the driver get out and do things on foot? (The truck stands, secured.) */
  bool onFoot(const CouplingState& s) const;
  /**
   * The things on foot that can be done now and are not done yet, in the
   * order of the catalogue.  Which of them comes first is for the driver to
   * know.
   */
  std::vector<std::string> choices() const;
  /**
   * The driver chose a thing to do on foot.  Returns the fault if it is not
   * its turn yet (it is then not done), or "".
   */
  std::string act(const std::string& id, const CouplingState& s);
  CouplingStatus update(const CouplingState& s, double dt);

 private:
  /** metres driven since a mark, for the 'do' steps that are distances */
  double run_ = 0;
  bool braked_ = false;

  const CouplingStep* stepOf(const std::string& id) const;
  bool ready(const std::string& id) const;
  /** Has everything that is done with the truck before step `id` been done? */
  bool reached(const std::string& id) const;
  void finish(const std::string& id);
};

}  // namespace fs

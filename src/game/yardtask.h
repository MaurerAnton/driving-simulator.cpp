// SPDX-License-Identifier: AGPL-3.0-only
/**
 * A task of the truck lessons, run with the real vehicles: it judges a
 * reversing manoeuvre from where truck and trailer stand, or runs the
 * coupling procedure and does to the two what coupling does – the trailer
 * stays behind, is taken up again, and its drawbar stops a truck that
 * backs up beside the eye or too far.
 *
 * What is shown and said about it is the game's business (game.cpp); the
 * rules of the tasks are in ce.cpp.
 *
 * Origin: src/game/yardtask.ts (MaurerAnton/driving-simulator @ ff37c5d, v0.7.0).
 */
#pragma once

#include <string>
#include <vector>

#include "game/ce.h"
#include "sim/trailer.h"

namespace fs {

struct Vehicle;

/** backing into the trailer faster than this is a fault (m/s) */
constexpr double BUMP_SPEED = 0.9;

class YardTask {
 public:
  explicit YardTask(const std::string& idIn);

  const std::string id;   // TaskId
  const TaskLayout layout;
  /**
   * The judge of a reversing manoeuvre, or the coupling procedure – each task
   * has one (the original keeps the other as null; here both exist, and
   * manoeuvre()/couple() each use their own).
   */
  ManoeuvreJudge judge;
  CouplingProcedure coupling;
  /** what the coupling procedure was told in the last step (also what a thing done on foot is judged by) */
  CouplingState state;
  /** the truck ran into the standing trailer in the last step, faster than a crawl */
  bool bumped = false;

  /** One step of a reversing manoeuvre on the yard `frame`. */
  ManoeuvreStatus manoeuvre(double dt, const YardFrame& frame, const Vehicle& truck,
                            const Trailer& trailer, bool handbrake);
  /** One step of uncoupling and coupling: moves on in the procedure and couples or uncouples the trailer. */
  CouplingStatus couple(double dt, const Vehicle& truck, Trailer& trailer, bool handbrake);

 private:
  double mpos_[3] = {0, 0, 0};
};

}  // namespace fs

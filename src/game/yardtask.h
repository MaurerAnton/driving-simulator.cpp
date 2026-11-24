// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/yardtask.ts @ v1.7.7
/**
 * A task of the truck lessons, run with the real vehicles: it judges a
 * reversing manoeuvre from where truck and trailer stand, or runs the
 * coupling procedure and does to the two what coupling does – the trailer
 * stays behind, is taken up again, and its drawbar stops a truck that
 * backs up beside the eye or too far.
 *
 * What is shown and said about it is the game's business (game.ts); the
 * rules of the tasks are in ce.ts.
 */
#pragma once

#include <array>
#include <memory>

#include "game/ce.h"

namespace fs {

class Trailer;
class Vehicle;

/** backing into the trailer faster than this is a fault (m/s) */
inline constexpr double BUMP_SPEED = 0.9;

class YardTask {
 public:
  const TaskId id;
  const TaskLayout layout;
  /** the judge of a reversing manoeuvre, or null for coupling */
  const std::unique_ptr<ManoeuvreJudge> judge;
  /** the coupling procedure, or null for a manoeuvre */
  const std::unique_ptr<CouplingProcedure> coupling;
  /** what the coupling procedure was told in the last step (also what a thing done on foot is judged by) */
  CouplingState state;
  /** the truck ran into the standing trailer in the last step, faster than a crawl */
  bool bumped = false;
  /** done with the articulated lorry (game/semice.ts) rather than the truck with the drawbar trailer */
  const bool semi;

  explicit YardTask(const TaskId& id, bool semi = false);

  /** One step of a reversing manoeuvre on the yard `frame`. */
  const ManoeuvreStatus& manoeuvre(double dt, const YardFrame& frame, const Vehicle& truck, const Trailer& trailer, bool handbrake);

  /** One step of uncoupling and coupling: moves on in the procedure and couples or uncouples the trailer. */
  const CouplingStatus& couple(double dt, Vehicle& truck, Trailer& trailer, bool handbrake);

 private:
  std::array<double, 3> mpos_{0, 0, 0};
};

}  // namespace fs

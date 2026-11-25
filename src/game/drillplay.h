// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/drillplay.ts @ v1.7.7
/**
 * The basic tasks of class B on the practice yard as the game (game.h)
 * runs them – one after the other, as a lesson lists them:
 *
 *  - each task is laid out on the yard (render/yard.h draws its asphalt,
 *    lines and cones, render/drillyard.h its parked cars, kerb stones and
 *    the ramp's sides); the ground under the wheels is asphalt there, and
 *    the ramp of the hill start raises it (`raise`, for sim/ground.h);
 *  - the judge (game/drills.h) is told each moment where the car is; what
 *    to do now goes into the street-name block, the instructor's remarks
 *    into the tip line, the faults to the instructor's log;
 *  - a task that went wrong may be repeated once, as in the test (its
 *    faults that end it count as serious ones the first time);
 *  - each finished task gets a score and up to three stars, shown when it
 *    is done and on the result page.
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "game/ce.h"
#include "game/drills.h"
#include "platform/dom.h"

namespace fs {

class YardScene;
class DrillScene;

/** What the game tells the run each moment (world coordinates; the pose of the model origin). */
struct DrillDrive {
  double x = 0;
  double z = 0;
  double yaw = 0;
  double speed = 0;
  bool handbrake = false;
  double brake = 0;
  bool engine = true;
};

/** What happened in a step, for the game to do. */
struct DrillEvents {
  /** the faults, with the severity to note them with */
  std::vector<DrillFault> faults;
  /** the instructor's remark (already in the player's language; '' for none) */
  std::string remark;
  /** the emergency stop's signal: now */
  bool signal = false;
  /** hold the car where it is (it ran into a parked one) */
  bool stop = false;
  /** 'again': the task once more from its start; 'next': the next task's start; 'over': all done (or failed twice); '' */
  std::string next;
};

class DrillRun {
 public:
  const std::vector<DrillId> ids;
  std::unique_ptr<DrillJudge> judge;
  /** this is the task's second try */
  bool second = false;
  std::vector<DrillResult> results;
  /** a task failed twice */
  bool failed = false;

  DrillRun(std::vector<DrillId> ids, const YardFrame& frame, const CarShape& shape, YardScene& yardScene, DrillScene& scene, std::function<std::string()> lang, std::function<double()> rand = nullptr);

  const DrillLayout& layout() const { return judge->layout; }
  /** Where the car stands at the start of the task now (world). */
  Pose start() const;
  /** the yard is asphalt where the task is (sim/ground.h `paved`) */
  bool paved(double x, double z) const;
  /** how much the ramp raises the ground (sim/ground.h `raise`) */
  double raise(double x, double z) const;
  /** One step of the task: judge it, and say what the game is to do. */
  const DrillEvents& update(double dt, const DrillDrive& d, const std::function<double(double, double)>& heightAt);
  /** What to do now – for the street-name block. */
  std::string line() const;
  /** Below it: the task, its number, the corrections or what is measured. */
  std::string sub() const;
  /** The tasks as they went, for the result page (null: none was finished). */
  std::optional<dom::El> resultNode() const;
  /** The drive ends: the yard goes. */
  void hide();

 private:
  size_t idx_ = 0;
  const YardFrame& frame_;
  const CarShape shape_;
  YardScene& yardScene_;
  DrillScene& scene_;
  std::function<std::string()> lang_;
  std::function<double()> rand_;
  bool laid_ = false;
  double laidTry_ = -10;
  double now_ = 0;
  /** when the run is over (after the last remark has been read) */
  double overAt_ = -1;
  DrillEvents ev_;

  /** Lay the task out on the yard (again, now and then, until the ground under it is loaded). */
  void lay(const std::function<double(double, double)>& heightAt);
  void next(const DrillId& id);
};

}  // namespace fs

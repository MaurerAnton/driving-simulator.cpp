// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/forklift.ts @ v1.7.7
/**
 * The forklift the player drives (sim/forklift.h has its measures), drawn
 * part by part: the chassis with its steps and mudguards, the counterweight
 * at the back, the bonnet with the seat on it, the cowl with the steering
 * column and wheel, the overhead guard on its four posts, the work lamps,
 * the rear lamps and the beacon; the front wheels (driven) and the rear
 * wheels on their swinging axle (steered); the mast – the outer channels
 * on their tilt cylinders, the inner channels that rise at half the speed
 * of the forks, the chains over the sheaves –, the carriage with its load
 * backrest, the side-shift and the two forks; and the operator on the seat.
 *
 * Built like the other vehicles around the point the physics use – the
 * ground below the middle of the wheelbase –, x right, y up, the forks
 * towards −z.  The parts that do not move against each other are merged
 * into one mesh per material; the low graphics preset leaves out the small
 * ones.  A generic forklift: no real make is reproduced.
 */
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "render/fleet.h"
#include "render/people.h"

namespace fs {

class Forklift;

/** The forklift's model: posed each frame from its simulation. */
class ForkliftModel : public VehicleModel {
 public:
  /** `detail`: "full" or "low" */
  explicit ForkliftModel(const std::string& detail = "full");

  void setCabinOpacity(double opacity) override;
  void update(const CarVisualState& s, double dt, double now) override;
  /** The operator shown on the seat (not from the operator's own eyes). */
  void setOperator(bool on);
  /** Pose the model from the forklift: its mast, wheels, steering, its tipping; `now` blinks the beacon. */
  void pose(const Forklift& f, double now);

 private:
  /** tipping: forwards about the front axle, sideways about the line of a front wheel */
  const std::shared_ptr<Group> tipF = make<Group>();
  const std::shared_ptr<Group> tipS = make<Group>();
  const std::shared_ptr<Group> body = make<Group>();
  const std::shared_ptr<Group> mast = make<Group>();
  const std::shared_ptr<Group> inner = make<Group>();
  const std::shared_ptr<Group> carriage = make<Group>();
  const std::shared_ptr<Group> shifter = make<Group>();
  std::vector<std::shared_ptr<Group>> wheelsF;
  std::vector<std::shared_ptr<Group>> wheelsR;
  std::vector<std::shared_ptr<Group>> steerR;
  const std::shared_ptr<Group> steeringWheel = make<Group>();
  std::vector<std::shared_ptr<Mesh>> chains;
  std::vector<std::shared_ptr<Mesh>> cylinders;
  std::shared_ptr<Mesh> beacon;
  PeopleRenderer people{1};
  PersonPose operator_;
  bool operatorShown = true;
  const bool low;
  Vector3 tmpA;
  Vector3 tmpB;

  void poseParts(double lift, double tilt, double side, double steer, double wheel, const std::array<double, 2>& rolled);
};

}  // namespace fs

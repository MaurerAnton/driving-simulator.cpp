// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The truck tasks with the real vehicles (origin: src/game/yardtask.ts
 * @ ff37c5d, v0.7.0).
 */
#include "game/yardtask.h"

#include <cmath>

#include "sim/vehicle/vehicle.h"

namespace fs {

namespace {
/** how far past the coupling the drawbar eye can get before the truck's rear end stands against the drawbar (m) */
constexpr double PAST_THE_EYE = 0.22;
}  // namespace

YardTask::YardTask(const std::string& idIn)
    : id(idIn), layout(taskLayout(idIn)), judge(idIn == "couple" ? "corner" : idIn) {
  // (a coupling task starts with the trailer on the hook and the truck secured)
  if (id == "couple") {
    state.handbrake = true;
    state.coupled = true;
  }
}

ManoeuvreStatus YardTask::manoeuvre(double dt, const YardFrame& frame, const Vehicle& truck,
                                    const Trailer& trailer, bool handbrake) {
  truck.getModelPosition(mpos_);
  RigState rig;
  rig.truck = frame.poseToLocal({mpos_[0], mpos_[2], truck.yaw});
  rig.trailer = frame.poseToLocal({trailer.x, trailer.z, trailer.yaw});
  rig.speed = truck.speed;
  rig.handbrake = handbrake;
  return judge.update(rig, dt);
}

CouplingStatus YardTask::couple(double dt, const Vehicle& truck, Trailer& trailer, bool handbrake) {
  bumped = false;
  if (!trailer.coupled) {
    // The trailer stands alone.  Its drawbar eye goes into the coupling and no further: backed up
    // beside it, or past it, the truck's rear cross-member stops at the drawbar.
    truck.getModelPosition(mpos_);
    const double dx = trailer.hitchX - mpos_[0];
    const double dz = trailer.hitchZ - mpos_[2];
    const double sn = std::sin(truck.yaw);
    const double cs = std::cos(truck.yaw);
    const double behind = dx * sn + dz * cs;
    const double side = dx * cs - dz * sn;
    const double over = TRUCK_OUTLINE.hitch - PAST_THE_EYE - behind;
    if (over > 0 && behind > -2 && std::fabs(side) < TRUCK_OUTLINE.hw) {
      if (truck.speed < -BUMP_SPEED) bumped = true;
      const double push = std::min(over, 0.3);
      const_cast<Vehicle&>(truck).translate(-sn * push, 0, -cs * push);
      if (truck.speed < 0) {
        const_cast<Vehicle&>(truck).vel[0] = 0;
        const_cast<Vehicle&>(truck).vel[2] = 0;
      }
    }
  }
  double dist = 0, angle = 0;
  trailer.offsetFrom(truck, dist, angle);
  state.speed = truck.speed;
  state.handbrake = handbrake;
  state.coupled = trailer.coupled;
  state.dist = trailer.coupled ? 0 : dist;
  state.angle = angle;
  CouplingStatus st = coupling.update(state, dt);
  if (st.uncouple) trailer.uncouple();
  if (st.couple) trailer.couple();
  return st;
}

}  // namespace fs

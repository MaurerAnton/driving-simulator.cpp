// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/yardtask.ts @ v1.7.7
#include "game/yardtask.h"

#include <algorithm>
#include <cmath>

#include "game/semice.h"
#include "sim/jsmath.h"
#include "sim/trailer.h"
#include "sim/vehicle/index.h"

namespace fs {

namespace {
/** how far past the coupling the drawbar eye can get before the truck's rear end stands against the drawbar (m) */
const double PAST_THE_EYE = 0.22;
}  // namespace

YardTask::YardTask(const TaskId& id_, bool semi_)
    : id(id_),
      layout(taskLayout(id_)),
      judge(id_ == "couple" ? nullptr : std::make_unique<ManoeuvreJudge>(id_, semi_ ? SEMI_RIG : DRAWBAR_RIG)),
      coupling(id_ == "couple" ? std::make_unique<CouplingProcedure>(semi_ ? SEMI_APPROACH : std::array<double, 2>{0.25, 1.6}) : nullptr),
      semi(semi_) {}

const ManoeuvreStatus& YardTask::manoeuvre(double dt, const YardFrame& frame, const Vehicle& truck, const Trailer& trailer, bool handbrake) {
  truck.getModelPosition(mpos_.data());
  RigState rs;
  rs.truck = frame.poseToLocal({mpos_[0], mpos_[2], truck.yaw});
  rs.trailer = frame.poseToLocal({trailer.x, trailer.z, trailer.yaw});
  rs.speed = truck.speed;
  rs.handbrake = handbrake;
  return judge->update(rs, dt);
}

const CouplingStatus& YardTask::couple(double dt, Vehicle& truck, Trailer& trailer, bool handbrake) {
  bumped = false;
  if (!trailer.coupled && semi) bumped = semiStop(truck, trailer, mpos_.data());
  else if (!trailer.coupled) {
    // The trailer stands alone.  Its drawbar eye goes into the coupling and no further: backed up
    // beside it, or past it, the truck's rear cross-member stops at the drawbar.
    truck.getModelPosition(mpos_.data());
    const double dx = trailer.hitchX - mpos_[0];
    const double dz = trailer.hitchZ - mpos_[2];
    const double sin = jsSin(truck.yaw);
    const double cos = jsCos(truck.yaw);
    const double behind = dx * sin + dz * cos;
    const double side = dx * cos - dz * sin;
    const double over = TRUCK_OUTLINE.hitch - PAST_THE_EYE - behind;
    if (over > 0 && behind > -2 && std::abs(side) < TRUCK_OUTLINE.hw) {
      if (truck.speed < -BUMP_SPEED) bumped = true;
      const double push = std::min(over, 0.3);
      truck.translate(-sin * push, 0, -cos * push);
      if (truck.speed < 0) {
        truck.vel[0] = 0;
        truck.vel[2] = 0;
      }
    }
  }
  const Trailer::Offset off = trailer.offsetFrom(truck);
  CouplingState& s = state;
  s.speed = truck.speed;
  s.handbrake = handbrake;
  s.coupled = trailer.coupled;
  s.dist = trailer.coupled ? 0 : off.dist;
  s.angle = off.angle;
  const CouplingStatus& st = coupling->update(s, dt);
  if (st.uncouple) trailer.uncouple();
  if (st.couple) trailer.couple();
  if (semi) syncSemi(trailer, *coupling);
  return st;
}

}  // namespace fs

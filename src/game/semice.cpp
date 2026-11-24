// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/semice.ts @ v1.7.7
#include "game/semice.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "core/js.h"
#include "sim/jsmath.h"
#include "sim/semitrailer.h"
#include "sim/trailer.h"
#include "sim/vehicle/index.h"

namespace fs {

namespace {

const TrailerOutline TRAILER = trailerOutline(SEMI_TRAILER);

/** how far the fifth wheel can go past the kingpin before the jaws stop it (m) */
const double PAST_THE_KINGPIN = 0.1;
/** the kingpin finds the jaws within this much to the side (the fifth wheel's guide funnels it in) */
const double JAWS = 0.3;
/** backing into the trailer faster than this is a fault (m/s) */
const double BUMP_SPEED = 0.9;

/** Is the steps' text of the articulated lorry its own (else the drawbar combination's serves)? */
const std::set<std::string>& OWN_TEXT() {
  static const std::set<std::string> s = {"do.uPull", "do.cApproach", "do.cLatch", "step.uLeg", "step.uOpen", "step.cCheck", "step.cHeight", "step.cLocked", "step.cLeg"};
  return s;
}

}  // namespace

const RigShape SEMI_RIG = {
    {TRACTOR_OUTLINE.front, TRACTOR_OUTLINE.rear, TRACTOR_OUTLINE.hw},
    {TRAILER.front, TRAILER.rear, TRAILER.hw},
    {{TRACTOR_UNIT.wheelbase / 2, -TRACTOR_UNIT.wheelbase / 2}, TRACTOR_OUTLINE.hw},
    {{SEMI_TRAILER.axleSpread, 0, -SEMI_TRAILER.axleSpread}, SEMI_TRAILER.width / 2},
};

std::string taskTextKey(bool semi, const std::string& what, const std::string& id) {
  const std::string k = what + "." + id;
  return semi && OWN_TEXT().count(k) ? "semi." + k : "ce." + k;
}

bool semiStop(Vehicle& truck, const Trailer& trailer, double* mpos) {
  truck.getModelPosition(mpos);
  const double dx = trailer.hitchX - mpos[0];
  const double dz = trailer.hitchZ - mpos[2];
  const double sin = jsSin(truck.yaw);
  const double cos = jsCos(truck.yaw);
  // the kingpin: how far behind the tractor's origin, and to its right
  const double behind = dx * sin + dz * cos;
  const double side = dx * cos - dz * sin;
  double a = truck.yaw - trailer.yaw;
  a -= PI * 2 * jsRound(a / (PI * 2));
  double over = 0;
  if (std::abs(side) < JAWS && std::abs(a) < 0.3 && behind > FIFTH_WHEEL_AT - 1.5) {
    over = FIFTH_WHEEL_AT - PAST_THE_KINGPIN - behind;
  } else if (std::abs(side) < SEMI_TRAILER.width && behind > -3) {
    // the trailer's front wall – -bodyFront ahead of the kingpin – against the back of the cab
    over = -TRACTOR_OUTLINE.cabBack + 0.08 - SEMI_TRAILER.bodyFront * jsCos(a) - behind;
  }
  if (over <= 0) return false;
  const bool bumped = truck.speed < -BUMP_SPEED;
  const double push = std::min(over, 0.3);
  truck.translate(-sin * push, 0, -cos * push);
  if (truck.speed < 0) {
    truck.vel[0] = 0;
    truck.vel[2] = 0;
  }
  return bumped;
}

void syncSemi(Trailer& trailer, const CouplingProcedure& proc) {
  auto* semi = dynamic_cast<SemiTrailer*>(&trailer);
  if (!semi) return;
  const std::set<CouplingStepId>& d = proc.done;
  const auto between = [&d](const char* off, const char* on) { return d.count(off) && !d.count(on); };
  semi->parkBrake = between("uBrake", "cBrake");
  semi->legsDown = between("uLeg", "cLeg");
  semi->supply = !between("uRed", "cRed");
  semi->control = !between("uYellow", "cYellow");
  semi->cables = !between("uCable", "cCable");
}

}  // namespace fs

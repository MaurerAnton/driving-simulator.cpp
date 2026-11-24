// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/arcadedrive.ts @ v1.7.7
#include "game/arcadedrive.h"

#include <algorithm>
#include <cmath>

namespace fs {

namespace {
/** a pedal pressed at least this far counts */
const double PRESSED = 0.05;
}  // namespace

void ArcadeDrive::reset() {
  dir = 1;
  held = 0;
  parked = false;
}

void ArcadeDrive::step(double throttle, double brake, double speed, double dt, DriverInput& d, double limit, bool engineRunning) {
  const bool still = std::fabs(speed) < ARCADE_STILL;
  double go = dir > 0 ? throttle : brake;
  double back = dir > 0 ? brake : throttle;
  if (still && back > 0.3 && go < PRESSED) {
    held += dt;
    if (held >= ARCADE_SWITCH) {
      dir = dir > 0 ? -1 : 1;
      held = 0;
      const double t = go;
      go = back;
      back = t;
    }
  } else held = 0;
  d.throttle = dir > 0 ? std::min(go, limit) : go;
  d.brake = back;
  d.gear = dir;
  d.clutch = 0;
  d.ignition = true;
  d.starter = !engineRunning;
  // standing and not asked to go: it stays where it is (the brakes and the parking brake – on a hill too)
  if (go >= PRESSED) parked = false;
  else if (still) parked = true;
  d.handbrake = parked ? 1 : 0;
  if (parked) d.brake = 1;
}

std::string arcadeTransmission(bool arcade, const std::string& chosen) { return arcade ? "automatic" : chosen; }

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/arcadedrive.ts @ v1.7.7
/**
 * Arcade driving: the simulator's chores left out (a setting, off unless
 * chosen).
 *
 * A driving school wants the gear chosen, the handbrake set and released,
 * the engine started and not stalled.  In arcade driving none of that is
 * asked: the vehicle has the automatic gearbox, its engine runs (it starts
 * again by itself), and the two pedals are all there is –
 *
 *  - the right pedal drives the way the vehicle goes (D), the left one brakes;
 *  - standing, the left pedal held a moment puts it in reverse (R): then the
 *    left pedal drives backwards and the right one brakes – and, standing
 *    again, the right pedal held a moment puts it back into D;
 *  - standing with no pedal for going pressed, the vehicle stays where it is
 *    (as if the handbrake were set; on a hill too).
 *
 * The rules judge none of it (no handbrake left on, no stalling).  The other
 * half of the arcade options, the road physics switched off, is
 * WorldGround.even (sim/ground.h): every surface grips as dry asphalt.
 */
#pragma once

#include <string>

#include "sim/vehicle/types.h"

namespace fs {

/** slower than this (m/s) the vehicle counts as standing */
inline constexpr double ARCADE_STILL = 0.4;
/** how long (s) the other pedal is held, standing, to change between D and R */
inline constexpr double ARCADE_SWITCH = 0.35;

/** What arcade driving keeps from one step to the next: the way chosen, and how long the other pedal has been held. */
class ArcadeDrive {
 public:
  /** 1: forwards (D), −1: backwards (R) */
  int dir = 1;

  /** A new drive (or a new vehicle): forwards. */
  void reset();

  /**
   * The pedals as the player presses them (`throttle` the right, `brake` the
   * left one, 0 … 1), the vehicle's speed along its length (m/s, + forwards)
   * and the time step: what goes to the vehicle, written into `d` (an
   * automatic's selector: 1 D, −1 R).  `limit` caps the right pedal driving
   * forwards (the speed limiter's, the governor's – 1: none).
   */
  void step(double throttle, double brake, double speed, double dt, DriverInput& d, double limit = 1, bool engineRunning = true);

 private:
  /** how long the other pedal has been held, standing */
  double held = 0;
  /** held where it stopped: until the pedal for going is pressed (whatever the vehicle does meanwhile – settling on its springs, on a hill) */
  bool parked = false;
};

/** The gearbox a drive is to have: arcade driving always has the automatic one. */
std::string arcadeTransmission(bool arcade, const std::string& chosen);

}  // namespace fs

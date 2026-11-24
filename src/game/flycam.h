// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/flycam.ts @ v1.7.7
/**
 * The free camera that flies (the camera key's third view, from 1.7.0): no
 * longer tied to the vehicle, it goes wherever it is flown, and the world
 * loads round it (the game's focus()).  The vehicle meanwhile waits where it
 * is, braked (input/input.ts `flyCam`).
 *
 *  - Keys: W S (↑ ↓) forwards and back the way the camera looks – up or down
 *    with it –, A D (← →) to the side, E Q (Page Up, Page Down) straight up
 *    and down; Shift: four times as fast.
 *  - The mouse: held and moved, it looks round; the right (or middle) button
 *    held moves the camera over the ground; the wheel sets how fast it
 *    flies, in steps – the game shows the speed for a moment.
 *  - Fingers: the stick on the left flies (ui/touchstick.ts, with buttons
 *    for up and down), one finger on the picture looks round, two fingers
 *    moved up or down lift or lower it, pinching sets the speed.
 *
 * Speeds go in steps of √2 from 1.5 m/s to about 380 m/s.  The camera starts
 * where the camera was, looking the way it looked; it keeps above the ground
 * and below 1500 m over it.  Free of the DOM: tested in
 * tests/game_flycam_test.cpp.
 */
#pragma once

#include <functional>

namespace fs {

/** the speed steps: 1.5 m/s × √2^k */
inline constexpr double FLY_MIN_STEP = 0;
inline constexpr double FLY_MAX_STEP = 16;

/** What was asked of the camera since the last picture (Input.takeCam: key time in seconds, drags in parts of the screen). */
struct FlyInput {
  double yaw = 0;
  double pitch = 0;
  /** dragged with the right or middle button: over the ground, in parts of the screen's height */
  double right = 0;
  double fwd = 0;
  /** seconds of key time (or stick) this picture: forwards, to the right, up */
  double keyFwd = 0;
  double keyRight = 0;
  double keyUp = 0;
  bool fast = false;
  /** two fingers moved up (+) or down, in parts of the screen's height */
  double lift = 0;
  /** speed steps asked for (the wheel, a pinch) */
  double speed = 0;
};

class FlyCam {
 public:
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  double pitch = -0.2;
  /** the speed step (see FLY_MIN_STEP …): continuous, shown rounded */
  double step = 6;
  /** has a place: else it is taken from the camera when flying begins */
  bool ready = false;

  /** metres a second at the step chosen */
  double speed() const;

  /** Begin from where a camera is, looking the way it looks (direction dx, dy, dz). */
  void start(double x, double y, double z, double dx, double dy, double dz);

  /**
   * One picture: turned, flown, kept above the ground (`ground(x, z)`: its
   * height there, NaN where nothing is loaded).  Returns true when the speed
   * step changed (for the game to show it).
   */
  bool update(double dt, const FlyInput& t, const std::function<double(double x, double z)>& ground);

 private:
  double vx = 0;
  double vy = 0;
  double vz = 0;
};

}  // namespace fs

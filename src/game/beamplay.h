// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/beamplay.ts @ v1.7.7
/**
 * Headlights in the game: what ties sim/beams.h (the rules of § 17 StVO,
 * the traffic's own main beam) to render/headlights.h (the light on the
 * road, the lamps of the traffic) and to the game – once a frame, from one
 * short hook in the hub (game_frame.cpp).
 *
 * The dark comes from the sun (the hub's updateSun gives its elevation): by
 * day nothing here costs anything.  The main beam is lit when it is switched
 * on (with the dipped beam), by the assist when the settings ask for it, and
 * while the flasher is held (it works with the lights off, too).
 */
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "engine/engine.h"
#include "render/headlights.h"
#include "sim/beams.h"

namespace fs {

struct TrafficCar;

struct BeamFrame {
  double dt = 0;
  /** the player's vehicle is on the road and driven (not on foot, not flying, not the tram, not a replay) */
  bool driving = false;
  /** its faults count (driving, not a demonstration drive, not a passenger) */
  bool judged = false;
  /** the model to throw the light from (null: none – lights off) */
  Object3D* root = nullptr;
  double front = 0;
  double height = 0;
  double x = 0;
  double z = 0;
  double yaw = 0;
  double speed = 0;
  bool low = false;
  /** the main beam lit (see `high`) */
  bool high = false;
  /** the street is lit throughout (in a town, or a lit road) */
  bool lit = false;
  /** how far one sees in the weather (m) */
  double visibility = 1e9;
  bool cheap = false;
  /** the cars of the traffic (null: none) */
  const std::vector<TrafficCar*>* cars = nullptr;
  std::vector<BeamUser> riders;
  /** the scene the light and the lamps go into (on the first frame) */
  ObjectPtr scene;
  const Camera* camera = nullptr;
  double heightPx = 0;
  std::function<double(double x, double z)> heightAt;
};

struct BeamFrameResult {
  /** '': none */
  BeamTip tip;
  std::vector<BeamFault> faults;
};

class BeamPlay {
 public:
  /** the sun's elevation (degrees), set by the game where it places the sun */
  double sunElevation = 52;
  BeamWatch watch;
  TrafficBeams traffic;
  TrafficGlow glow;

  /** how dark it is for the lamps, 0 … 1 */
  double dark() const { return darkness(sunElevation); }
  /** The main beam lit: switched on with the dipped beam (or put on by the assist), or the flasher held. */
  bool high(bool low, bool high, bool flasher, bool assist) const { return flasher || (low && (assist ? watch.assistHigh : high)); }
  /**
   * One frame: the light, the lamps of the traffic, and what the rules say –
   * a tip to show (only when it is new, or again after half a minute) and the
   * faults found.
   */
  BeamFrameResult frame(const BeamFrame& f);

 private:
  std::unique_ptr<PlayerLights> lights_;
  std::vector<TrafficBeamCar> near_;
  BeamPlayer player_;
  /** the tip given last, and when (s): the same one is not said again for a while */
  BeamTip lastTip_;
  double lastTipAt_ = -1e9;
  double clock_ = 0;

  /** Can one see from one place to the other over the hills (a few points of the ground on the line between, eyes 1.2 m up)? */
  static bool sees(const std::function<double(double, double)>& heightAt, double x0, double z0, double x1, double z1);
};

}  // namespace fs

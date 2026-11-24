// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/walkmodes.ts @ v1.7.7
/**
 * How somebody on foot is moved – three ways to choose from in the settings
 * (Settings.walkControls):
 *
 *  - "classic": as in most games of the kind.  W S (or ↑ ↓) forwards and
 *    back, A D a step to the side, ← → turn; the mouse (held) or a finger
 *    dragged over the picture looks round – and the walker turns with the
 *    look.  On a touch screen a stick on the left moves (forwards, back,
 *    sideways) and the right half of the picture looks.  The default on a
 *    touch screen.
 *  - "tank": as before 1.7.0 – W S forwards and back, A D turn; on a touch
 *    screen the pedals and the steering bar.  The default with keys.
 *  - "tap": a tap (or a click) on the ground and the walker goes there by
 *    themselves – along the pavement, over the road at a crossing near the
 *    way if there is one, else straight across where the place lies.  The
 *    keys and the stick still work, and take over.
 *
 * Turning by keys or the steering bar is eased in and out (no jerk), and
 * running is a key held (Shift) or switched on and off (Caps Lock, or the
 * runner's button beside the stick).
 *
 * Kept free of the scene and the DOM: what is decided here is tested in
 * tests/walkmodes_test; the game feeds it the input and the roads round the walker.
 */
#pragma once

#include <string>
#include <vector>

#include "core/geom.h"
#include "core/js.h"

namespace fs {

/** 'classic' | 'tank' | 'tap' */
using WalkMode = std::string;
/** 'auto' | WalkMode */
using WalkSetting = std::string;
extern const std::vector<WalkSetting> WALK_SETTINGS;

/** The way of walking in use: the setting – or, set to "auto", classic on a touch screen and as before with keys. */
WalkMode walkModeOf(const WalkSetting& setting, bool touch);

/** What the input asks of the walker (see Input.walkInput): forwards (+) and back, a step to the right (+), turning (+ left), running. */
struct WalkIntent {
  double fwd = 0;
  double side = 0;
  double turn = 0;
  bool run = false;
};

/** What the walker is told to do in one step (onfoot WalkMove). */
struct WalkStep {
  double fwd = 0;
  double side = 0;
  double turn = 0;
  bool run = false;
};

/** A road near the walker as the way on foot sees it: its middle line, half its paved width, where its crossings are (metres along it). */
struct FootRoad {
  const Polyline3* pl = nullptr;
  /** from the middle to the kerb (m) */
  double half = 0;
  /** width of the pavement on the left / right of the line's direction (0: none) */
  double swL = 0;
  double swR = 0;
  /** marked crossings and lights (metres along the line) */
  std::vector<double> xings;
};

struct FootPoint {
  double x = 0;
  double z = 0;
};

/**
 * The way from (fx, fz) to (tx, tz) on foot: a list of points to walk to,
 * the last one the place itself.  With a road between the two: along the
 * pavement to a crossing (or to where the place lies opposite), over the
 * road there, along the other pavement.  Along a road on the same side:
 * keeping to the pavement round its bends.  Else straight there.
 */
std::vector<FootPoint> planFootPath(double fx, double fz, double tx, double tz, const std::vector<FootRoad>& roads);

/**
 * The walking of one person: eases the turning, and walks a way chosen by
 * a tap.
 */
class WalkControl {
 public:
  struct At {
    double x = 0;
    double z = 0;
    double yaw = 0;
  };
  /** the way being walked by itself (tap to walk): the points still to reach */
  std::vector<FootPoint> path;
  /** the way could not be walked on: the game says so once */
  bool blocked = false;

  bool walking() const { return !path.empty(); }
  /** Go to these points (planFootPath) by oneself. */
  void goTo(const std::vector<FootPoint>& path);
  void cancel() { path.clear(); }
  /**
   * One step: what the walker is to do, from what the input asks (`want`),
   * where the walker is and faces, and whether the view was turned by hand
   * (`looked`).  Keys or the stick take over from a way being walked.
   */
  WalkStep step(double dt, const WalkIntent& want, const At& at, bool looked);

 private:
  double turnNow = 0;
  /** seconds since the view was last turned by hand (the walker then turns along the way again) */
  double sinceLook = 9;
  /** how far the walker was from the next point a moment ago, and for how long it has not come nearer */
  double lastGap = JS_INF;
  double stuckT = 0;
};

}  // namespace fs

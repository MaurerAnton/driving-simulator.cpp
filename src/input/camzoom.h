// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/input/camzoom.ts @ v1.7.7
/**
 * Zooming the view: a closer look through the eyes on foot and from the
 * driver's seat – at a sign far ahead, a house number, the lights of a
 * junction.
 *
 * On a phone two fingers pinch the view larger or smaller; the zoom stays
 * while they are on the glass.  On a computer the mouse wheel (or a pinch of
 * the touchpad, which the browser sends as a wheel) and the keys + and −
 * zoom; a key held keeps the zoom like a finger.  Once let go the view goes
 * back to its usual width by itself – after a time the settings choose
 * (Settings.zoomRelease): "auto" lets it go at once when the fingers lift and
 * a few seconds after the wheel or a key, or never.
 *
 * It only narrows the camera's angle of view (fov): nothing else is drawn,
 * nothing costs more.  Pure arithmetic, tested in tests/input_test.cpp.
 */
#pragma once

#include <string>
#include <vector>

namespace fs {

/** what the settings may choose for letting the zoom go: 'auto' | 'release' | '3' | '6' | '12' | 'never' */
using ZoomRelease = std::string;
inline const std::vector<ZoomRelease> ZOOM_RELEASES = {"auto", "release", "3", "6", "12", "never"};

/** the closest the view goes: four times */
inline constexpr double ZOOM_MAX = 4;

class CamZoom {
 public:
  /** how many times larger the view is drawn now (1: as usual) */
  double factor = 1;
  /** … and where it is going */
  double target = 1;

  /** Is the zoom held (fingers on the glass, a key down)? */
  bool held() const { return fingers || key; }

  /** Two fingers moved apart by `scale` (new spread / old spread). */
  void pinch(double scale);

  /** The fingers are off the glass (fewer than two left). */
  void fingersOff();

  /** A turn of the wheel: `step` > 0 away (smaller), < 0 closer – as the wheel moves the camera behind the car. */
  void wheel(double step);

  /** A key + (`dir` 1) or − (−1) went down (true) or up (false). */
  void press(double dir, bool down);

  /** Back to the usual view at once (another view, out of the vehicle …). */
  void reset();

  /** How long after being let go the zoom goes back (s; Infinity: never). */
  double delay(const ZoomRelease& release) const;

  /**
   * Each frame: `active` – the view can be zoomed now (else it is reset).
   * Lets the zoom go when its time is up and moves the factor smoothly to
   * where it is going.
   */
  void update(double dt, const ZoomRelease& release, bool active);

  /** The angle of view (degrees) for a camera whose usual one is `base`. */
  double fov(double base) const;

 private:
  bool fingers = false;
  bool key = false;
  /** what zoomed last: the fingers, the wheel or a key ('touch' | 'wheel' | 'key') */
  std::string source = "touch";
  /** seconds since the zoom was last touched or let go */
  double idle = 0;

  void set(double f);
};

}  // namespace fs

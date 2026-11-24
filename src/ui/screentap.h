// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/screentap.ts @ v1.7.7
/**
 * A tap on the vehicle's own screen in the driver's seat (the car's centre
 * display, the screen of the truck, the bus, the tractor, the tram's desk –
 * render/navscreen.h): it opens the big map, or – as the settings choose
 * (Settings.screenTap) – brings the live map back into the corner of the HUD
 * (`peek`), where it is easier to read on a small screen; another tap on the
 * vehicle's screen puts it there again.
 *
 * A tap is a pointer let go soon and near where it came down; it is checked
 * against the screen's picture in the scene with one ray (no cost while
 * nothing is tapped).
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "platform/dom.h"
#include "render/navscreen.h"

namespace fs {

class ScreenTap {
 public:
  /** the corner map is shown instead of the map on the vehicle's screen */
  bool peek = false;

  /**
   * `camera`: the camera the picture is drawn with; `mode`: what a tap does
   * now ('bigmap' | 'minimap'); `openMap`: opens the big map.
   */
  ScreenTap(dom::El canvas, std::shared_ptr<Camera> camera, std::function<std::string()> mode, std::function<void()> openMap);
  // (the page's listeners name this object: it stays where it is)
  ScreenTap(const ScreenTap&) = delete;
  ScreenTap& operator=(const ScreenTap&) = delete;

  /** Would a tap at (x, y) land on the vehicle's screen now (for the other taps on the picture to leave it alone: ui/worldtap.h)? */
  bool covers(double x, double y);

  /** Each frame: the vehicle's screen in view in the driver's seat (null: none – then the corner map is the map anyway). */
  void setScreen(VehicleScreen* screen);

 private:
  /** the screen that can be tapped now (set by the game each frame; null: none) */
  VehicleScreen* screen = nullptr;
  double setAt = 0;
  struct Down {
    int id = -1;
    double x = 0;
    double y = 0;
    double t = 0;
  };
  Down down;
  Raycaster ray;
  Vector2 ndc;
  std::vector<Intersection> hits;
  const dom::El canvas;
  const std::shared_ptr<Camera> camera;

  bool hit(Object3D& mesh, const Camera& camera, const dom::El& canvas, double x, double y);
};

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The HUD of the SDL window (the desktop build without a browser): the game's HUD
 * (ui/hud) is a page of DOM elements, which the window does not show – so the same
 * values are painted with the native 2D canvas into one picture that the window lays
 * over each 3D frame (platform/overlay.h of the SDL host: `sdlOverlayHook`).
 *
 * Painted, where the page has them: the street-name block top left (the arrow, the
 * street, the line under it – or the instructor's notice in its place, red or
 * yellow), the speed limit top right, the speed, the gear and the engine speed in
 * the strip at the bottom, the tip above it, the faults and points, and the corner
 * map bottom right (the canvas the Hud draws it on).  Nothing of this is worked out
 * here: every value is what Hud::update was last given (Hud::lastState) and what it
 * shows (Hud::noticeNow, Hud::cornerMap).
 *
 * The touch controls (the browser's ui/hud_touch.cpp: the steering bar, the accelerator,
 * the brake and – with a manual gearbox – the clutch pedal, the indicators, the gears ▲ ▼,
 * D (1) and R, the handbrake, the starter) are painted over it too while the driver uses
 * fingers (Input::mode 'touch' – from the start where the pointer is coarse: Android, or
 * fahrsim-sdl --touch).  The window hands them its pointers first (platform/overlay.h:
 * `sdlOverlayPointerHook`): a finger or the mouse on one of them is theirs, and they feed
 * Input exactly as the browser's do (Input::touch, touched(), press()).  The tip line is a
 * button there as well ("fasten your seat belt – tap here").
 *
 * Native only (it reads the canvas's pixels): in the browser the page is the HUD.
 */
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "platform/canvas.h"
#include "platform/overlay.h"

namespace fs {

class Hud;

class SdlHud {
 public:
  explicit SdlHud(const Hud& hud);

  /** the picture: RGBA, 8 bits each, premultiplied alpha, the top row first (rgba null: nothing to show) */
  struct Picture {
    const uint8_t* rgba = nullptr;
    int width = 0, height = 0;
    /** changes whenever the pixels do */
    uint64_t version = 0;
  };
  /**
   * paint the HUD for a window of `width` × `height` CSS pixels, `ratio` picture pixels to each
   * (painted again only when something shown changed)
   */
  Picture paint(int width, int height, double ratio = 1);

  /** a pointer on the window (CSS pixels): true – the touch controls take it (and what follows of it) */
  bool pointer(const OverlayPointer& p);

  /** one of the touch controls: where it is (CSS pixels) and what it does */
  struct Control {
    /** "steer", "pedal" (`code`: gas, brk, clutch) or "key" (`code`: the key it stands in for) */
    std::string kind;
    std::string code;
    std::string label;
    double x = 0, y = 0, w = 0, h = 0;
    bool round = false;
  };
  /** the touch controls for a window of that size (CSS pixels) – none while they are not shown */
  std::vector<Control> controls(double width, double height) const;

 private:
  const Hud& hud_;
  Canvas canvas_;
  Ctx2D g_;
  std::vector<uint8_t> pixels_;
  std::string shown_;
  uint64_t mapVersion_ = 0;
  uint64_t version_ = 0;
  bool empty_ = true;
  /** the touch controls are shown (painted into the picture last) */
  bool touch_ = false;
  /** the window's size the controls were laid out for (CSS pixels) */
  double cssW_ = 0, cssH_ = 0;
  /** which control each pointer holds (the pointer's id → the control's code: steer, gas, brk, clutch, a key) */
  std::map<int64_t, std::string> held_;
  /** where the steering bar's knob is (−1 … 1) */
  double knob_ = 0;
  /** the tip line as a button: its box (CSS pixels; empty: none) and the key it stands in for */
  double tipX_ = 0, tipY_ = 0, tipW_ = 0, tipH_ = 0;
  std::string tipCode_;

  bool touchShown() const;
  void draw(double scale, int width, int height);
  void drawControls(double ratio);
  /** a held control follows the pointer (steering bar, pedals) */
  void follow(const std::string& code, double x, double y);
};

/**
 * Let the SDL window show this HUD: sets `sdlOverlayHook` (platform/overlay.h, from the SDL
 * host) to paint it at the window's size (plat::innerWidth/innerHeight).  Where the host has
 * no such hook (no platform/overlay.h in this build) it does nothing.
 */
void installSdlHud(const Hud& hud);

}  // namespace fs

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
 * Native only (it reads the canvas's pixels): in the browser the page is the HUD.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "platform/canvas.h"

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
  /** paint the HUD for a window of `width` × `height` pixels (painted again only when something shown changed) */
  Picture paint(int width, int height);

 private:
  const Hud& hud_;
  Canvas canvas_;
  Ctx2D g_;
  std::vector<uint8_t> pixels_;
  std::string shown_;
  uint64_t mapVersion_ = 0;
  uint64_t version_ = 0;
  bool empty_ = true;

  void draw(double scale, int width, int height);
};

/**
 * Let the SDL window show this HUD: sets `sdlOverlayHook` (platform/overlay.h, from the SDL
 * host) to paint it at the window's size (plat::innerWidth/innerHeight).  Where the host has
 * no such hook (no platform/overlay.h in this build) it does nothing.
 */
void installSdlHud(const Hud& hud);

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
/**
 * A picture over the 3D frame, for a host that has no page to show the interface
 * (the SDL window: platform/sdl).  Whoever paints the interface (the HUD of the native
 * 2D canvas) sets the hook; the window draws what it hands back over each frame,
 * stretched over the whole window.  Unset, or empty: nothing is drawn over the frame.
 */
#pragma once

#include <cstdint>
#include <functional>

namespace fs {

struct OverlayImage {
  /** width × height pixels, RGBA, 8 bits each, premultiplied alpha, the top row first (nullptr: nothing) */
  const uint8_t* rgba = nullptr;
  int width = 0, height = 0;
  /** changes whenever the pixels do (the window uploads them again only then) */
  uint64_t version = 0;
};

/** what draws the picture (empty: no picture) */
inline std::function<OverlayImage()> sdlOverlayHook;

/** the picture to draw over this frame (empty: none) */
inline OverlayImage sdlOverlay() { return sdlOverlayHook ? sdlOverlayHook() : OverlayImage{}; }

}  // namespace fs

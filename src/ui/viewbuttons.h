// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/viewbuttons.ts @ v1.7.7
/**
 * Two small buttons in a corner of the screen: full screen (the browser's
 * Fullscreen API – where a phone's browser offers it) and hiding the whole
 * display for a clean view of the world.  While the display is hidden only
 * a faint button stays to bring it back (also F2, or the button's double
 * meaning: tap it again); menus still open over it.
 */
#pragma once

#include <functional>
#include <string>

#include "platform/dom.h"

namespace fs {

struct ViewButtons {
  dom::El node;
  /** hide or show the display (F2) */
  std::function<void()> toggleHud;
  std::function<void()> toggleFullscreen;
};

ViewButtons viewButtons(const dom::El& root, std::function<std::string(const std::string& k)> t);

}  // namespace fs

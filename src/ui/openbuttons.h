// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/openbuttons.ts @ v1.7.7
/**
 * The buttons for what can be done where one stands beside the vehicle
 * (game/openplay.ts): "Open door" / "Close door" and "Get in" at a door of
 * the car, "Open bonnet", "Open tailgate", "Open rear doors" at the front or
 * the back.  A row of large buttons just above the tip line – for a finger
 * on a phone, and for the mouse; the keys do the same (G and the full stop).
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "platform/dom.h"

namespace fs {

struct OpenButton {
  std::string id;
  std::string label;
};

class OpenButtons {
 public:
  OpenButtons(const dom::El& parent, std::function<void(const std::string& id)> onTap);

  /** These buttons now (none: the row is hidden). */
  void show(const std::vector<OpenButton>& list);

 private:
  const dom::El el = dom::el("div");
  std::string shown;
};

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/touchstick.ts @ v1.7.7
/**
 * A stick on the screen for the left thumb – walking the classic way (and
 * tap to walk: game/walkmodes.ts), and flying the free camera
 * (game/flycam.ts).  It stands in for W A S D: pushed up it goes forwards,
 * to the side it steps or flies sideways, and springs back to the middle
 * when let go.  Beside it: the runner's button (running on or off) when
 * walking, the buttons for up and down when flying.
 *
 * While it is shown, the pedals, the steering bar and the buttons of the
 * gears, the indicators and the parking brake – of no use on foot or to a
 * camera – are hidden (the class `stickmode` on the game's #ui element); the
 * right half of the picture is left to look round with a finger (as
 * before: input/input.h).
 *
 * Also here: a small badge that says something for a moment (the flying
 * camera's speed, when the wheel or a pinch changes it).
 */
#pragma once

#include <functional>
#include <optional>
#include <string>

#include "input/input.h"
#include "platform/dom.h"

namespace fs {

/** what the stick is for now: walking, flying ('walk' | 'fly') – or nothing (hidden) */
using StickUse = std::optional<std::string>;

class TouchStick {
 public:
  /** `root`: the game's #ui element; `t`: the texts */
  TouchStick(dom::El root, Input& input, const std::function<std::string(const std::string& key)>& t);
  // (the page's listeners name this object: it stays where it is)
  TouchStick(const TouchStick&) = delete;
  TouchStick& operator=(const TouchStick&) = delete;

  /** Every picture: what the stick is for now (null: hidden – on keys, or driving). */
  void frame(const StickUse& use);

  /** Say something for a moment (the flying camera's speed). */
  void badge(const std::string& text, double ms = 1400);

 private:
  dom::El root;
  Input& input;
  dom::El pad;
  dom::El knob;
  dom::El run;
  dom::El up;
  dom::El down;
  dom::El badgeEl;
  double badgeUntil = 0;
  StickUse use = std::nullopt;
  int finger = -1;

  void centre();
};

}  // namespace fs

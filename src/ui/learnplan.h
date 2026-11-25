// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/learnplan.ts @ v1.7.7
/**
 * A theory scene seen from above, frozen at one moment – the still of the
 * "spot the danger" items and of the exam questions with a scene
 * (ui/learn.ts).  Drawn on a 2D canvas from the scene's own data
 * (game/theory.ts, theorystage.ts): the road and the pavements, the kerbs,
 * the traffic lights in their colours, the signs, trees and barriers, and
 * every vehicle, cyclist and person where the script has them then – the
 * learner's car white with a yellow edge, as in a theory book.  North is up,
 * the learner comes from below.  Cheap: no 3D, drawn once (and again when a
 * sign's picture has come).
 *
 * The scenes (game/theory.ts, theorystage.ts) are not in this version yet: this
 * file is translated together with them.  Until then its two functions are hooks
 * of their names, empty – and the still stays blank, as the original leaves it for
 * a scene it does not know.  Whoever brings the scenes sets them (or puts the
 * translation of this file in their place).
 */
#pragma once

#include <functional>
#include <optional>
#include <string>

#include "platform/canvas.h"

namespace fs {

struct LearnPlanHooks {
  /** The moment of a scene: the time of its question `step`, moved by `off` seconds (nullopt: no such scene). */
  std::function<std::optional<double>(const std::string& id, int step, double off)> sceneTime = nullptr;
  /**
   * Draw scene `id` at time `t` into the canvas (its size as given by its
   * width and height attributes).  `you`: the label of the learner's car.
   * `signBase`: where the signs' SVG files are (public/signs/).
   */
  std::function<bool(const Canvas& canvas, const std::string& id, double t, const std::string& you, const std::string& signBase)> drawPlan = nullptr;
};

inline LearnPlanHooks learnPlan;

}  // namespace fs

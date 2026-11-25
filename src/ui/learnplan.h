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
 */
#pragma once

#include <optional>
#include <string>

#include "platform/canvas.h"

namespace fs {

/** The moment of a scene: the time of its question `step`, moved by `off` seconds (nullopt: no such scene). */
std::optional<double> sceneTime(const std::string& id, int step, double off = 0);

/**
 * Draw scene `id` at time `t` into the canvas (its size as given by its
 * width and height attributes).  `you`: the label of the learner's car.
 * `signBase`: where the signs' SVG files are (public/signs/) – their
 * pictures are drawn once they have come.
 */
bool drawPlan(const Canvas& canvas, const std::string& id, double t, const std::string& you, const std::string& signBase);

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/animals.ts @ v1.7.7
/**
 * Deer and wild boar, from a few boxes each: a roe deer (brown, the white
 * patch on its rump, big ears) and a wild boar (dark, bristly, a long
 * snout) – or a young boar, smaller and lighter.  The legs swing with the
 * distance run, so the hooves keep step at any speed: a bounding gallop for
 * the deer, a trot for the boar; standing, a deer holds its head up.
 *
 * Used by the theory lessons (render/theory.ts) and by the animals that
 * cross the country roads at dusk (render/wildlife.ts, sim/wildlife.ts).
 * The geometries and the material are shared: a model costs six meshes.
 *
 * Local frame as for the cars: −z is forwards, y is up, the origin lies on
 * the ground under the middle of the body.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/engine.h"

namespace fs {

/** a roe deer, a wild boar, a young boar ('deer' | 'boar' | 'piglet') */
using AnimalKind = std::string;

struct AnimalBuild;

/** One animal: its body and four legs that swing. */
class AnimalModel {
 public:
  const std::shared_ptr<Group> group = make<Group>();
  const AnimalKind kind;

  explicit AnimalModel(const AnimalKind& kind);
  AnimalModel(const AnimalModel&) = delete;
  AnimalModel& operator=(const AnimalModel&) = delete;

  /**
   * The legs and the body for `dist` metres run so far at `speed` m/s:
   * standing still, the legs straight (a deer's head up, watching).
   */
  void pose(double dist, double speed);

 private:
  std::shared_ptr<Mesh> body;
  std::vector<std::shared_ptr<Group>> legs;
  const AnimalBuild& b;
};

}  // namespace fs

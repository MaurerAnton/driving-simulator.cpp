// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/yard.ts @ v1.7.7
/**
 * The practice yard of the truck lessons: asphalt, boundary lines, cones, a
 * loading ramp and the place to end up in – laid over the field the map
 * build chose (tools/mapgen/yard.ts) for as long as such a lesson runs.
 * Nothing of the map is changed: the yard is a few meshes following the
 * ground.
 */
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "engine/engine.h"
#include "game/ce.h"

namespace fs {

class YardScene {
 public:
  const std::shared_ptr<Group> group = make<Group>();

  YardScene();

  /**
   * Lay a task out.  `heightAt` is the ground of the map (NaN where it is
   * not loaded yet – call again when it is).  Returns false in that case.
   */
  bool show(const YardFrame& frame, const TaskLayout& layout, const std::function<double(double x, double z)>& heightAt);

  void hide();

 private:
  std::vector<GeometryPtr> made_;
};

}  // namespace fs

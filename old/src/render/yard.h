// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The practice yard of the truck lessons: asphalt, boundary lines, cones, a
 * loading ramp and the place to end up in – laid over the field the map
 * build chose (the original's tools/mapgen/yard.ts) for as long as such a
 * lesson runs.  Nothing of the map is changed: the yard is a few meshes
 * following the ground.
 *
 * Origin: src/render/yard.ts (MaurerAnton/driving-simulator @ ff37c5d, v0.7.0).
 */
#pragma once

#include <vector>

#include "engine/scene.h"
#include "game/ce.h"

namespace fs {

class YardScene {
 public:
  YardScene();

  Group group;

  /**
   * Lay a task out.  `heightAt` is the ground of the map (NaN where it is
   * not loaded yet – call again when it is).  Returns false in that case.
   */
  bool show(const YardFrame& frame, const TaskLayout& layout,
            const std::function<double(double, double)>& heightAt);

  void hide();

 private:
  /** the meshes of the last show(), taken down by hide() */
  std::vector<std::unique_ptr<Object3D>> made_;
};

}  // namespace fs

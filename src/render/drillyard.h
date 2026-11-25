// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/drillyard.ts @ v1.7.7
/**
 * What the basic tasks of class B (game/drills.h) put on the practice yard
 * besides its asphalt, lines and cones (drawn by render/yard.h): the
 * parked cars of the gap and of the bays, kerb stones along the kerbs, and
 * the sloping sides of the ramp for the hill start.  A few boxes sharing
 * their geometries – cheap on a phone.
 */
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "engine/engine.h"

namespace fs {

class YardFrame;
struct DrillLayout;

class DrillScene {
 public:
  const std::shared_ptr<Group> group = make<Group>();

  DrillScene();
  /** Lay out what a task has; `heightAt`: the ground with the ramp (NaN where not loaded – false then: call again). */
  bool show(const YardFrame& frame, const DrillLayout& layout, const std::function<double(double x, double z)>& heightAt);
  void hide();

 private:
  std::vector<GeometryPtr> made;
};

}  // namespace fs

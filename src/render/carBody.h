// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The player's car body: the shapes that make up the shell, the glass and the
 * wheels, in the car's own frame (x right, y up, z back).
 *
 * Origin: src/render/carBody.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).
 */
#pragma once

#include <memory>
#include <vector>

#include "engine/geometry.h"
#include "engine/scene.h"

namespace fs {

/** What one wheel looks like (radius, width, rim style). */
struct WheelStyle {
  double radius = 0.316;
  double width = 0.22;
  int rim = 0;
};

/** The pieces of the car body, ready to be added to a Group. */
struct CarBody {
  std::unique_ptr<Group> shell;     // painted body panels
  std::unique_ptr<Group> glass;     // windows
  std::unique_ptr<Group> trim;      // bumpers, mirrors, grille
  std::unique_ptr<Group> lights;    // head and tail lights
  std::vector<std::unique_ptr<Mesh>> wheels;  // four, in order FL FR RL RR

  /** Build the body for a given paint colour (sRGB hex) and wheel style. */
  static std::unique_ptr<CarBody> build(uint32_t paint, const WheelStyle& wheels);
};

/** Colour of the standard driving-school car. */
constexpr uint32_t SCHOOL_CAR_PAINT = 0xf2f2f2;

}  // namespace fs

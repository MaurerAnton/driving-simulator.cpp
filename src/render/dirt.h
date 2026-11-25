// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/dirt.ts @ v1.7.7
/**
 * Dirt on the player's vehicle: a cheap blend in the materials of its body.
 *
 * Each material of the vehicle's outside gets a few lines in its shader:
 * where a mask says so, the colour is drawn towards a greyish brown and
 * the shine goes.  The mask is a small tiling noise texture, looked up at
 * two scales where the point lies in the vehicle's own frame (so the dirt
 * stays put on the body as it drives), and it rises towards the bottom of
 * the body – the sills, the wheels and the lower doors get dirty first, the
 * roof last.  `level` (0 clean … 1 filthy) moves the mask's threshold: one
 * uniform for every material, so nothing is rebuilt as the dirt grows.
 *
 * The materials are changed in place, once (their shader is compiled
 * again then).  Materials of lamps, displays, glass and linings are left
 * as they are: MeshBasicMaterial, transparent ones, those seen from inside
 * (BackSide) and those with a picture on them.
 *
 * (The shaders are the original's, as its patch leaves them – captured by tools/glsl/weather.mjs.)
 */
#pragma once

#include <unordered_set>

#include "engine/engine.h"

namespace fs {

/**
 * The dirt on a vehicle: `apply` the materials of its model once, then
 * `update` each frame with the level and where the model stands.
 */
class DirtLook {
 public:
  /** Make the outside of a model able to get dirty (`top`: the model's exterior group where it has one – the car's cockpit stays clean –, else its root). */
  void apply(Object3D& top);
  /** The level (0 … 1), the model's place (its root, whose matrixWorld is current) and how tall the vehicle is. */
  void update(double level, const Object3D& root, double height);

 private:
  std::unordered_set<const Object3D*> done_;
};

}  // namespace fs

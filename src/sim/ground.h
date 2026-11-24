// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/ground.ts @ v1.7.7
/** Ground under the wheels: analytic road surfaces first, terrain tiles otherwise. */
#pragma once

#include <functional>

#include "core/geom.h"
#include "sim/vehicle/types.h"
#include "world/world.h"

namespace fs {

class TerrainSource {
 public:
  virtual ~TerrainSource() = default;
  virtual double heightAt(double x, double z) = 0;
  virtual int coverAt(double x, double z) = 0;
};

class WorldGround : public Ground {
 public:
  /** result of the most recent road lookup (shared scratch object) */
  SurfaceHit hit = newSurfaceHit();
  /** global grip multiplier (rain, ice) */
  double weatherGrip = 1;
  /**
   * The road physics switched off (an arcade option, game/arcadedrive.ts): every road and every
   * piece of land grips and rolls as dry asphalt, whatever the weather – water stays water.
   */
  bool even = false;
  /**
   * The ground flattened (settings evenTerrain): a piece of land lies flat as a
   * road – its bumps and camber gone – so it rides and grips the same.  The
   * landscape's hills stay (one still drives up them); water stays water.
   */
  bool evenTerrain = false;
  /** places that are paved for the time being, whatever the map says (the practice yard laid over a field) */
  std::function<bool(double x, double z)> paved = nullptr;
  /** how much a thing laid on the ground for the time being raises it (m – the ramp of the hill start on the practice yard, game/drillplay.ts) */
  std::function<double(double x, double z)> raise = nullptr;
  /** how deep the water is at a place where the cover is water (m; game/wading.ts) – the wheels go down to its bed */
  std::function<double(double x, double z)> waterDepth = nullptr;

  WorldGround(World& world, TerrainSource& terrain);

  void sample(double x, double z, double yRef, GroundSample& out) override;

 private:
  World& world;
  TerrainSource& terrain;
  PolySample tmp = newPolySample();
  double lastY = 0;
};

}  // namespace fs

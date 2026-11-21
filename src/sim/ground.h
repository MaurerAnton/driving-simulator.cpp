// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Ground under the wheels: analytic road surfaces first, terrain tiles
 * otherwise.
 *
 * Origin: src/sim/ground.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#pragma once

#include <functional>

#include "sim/vehicle/types.h"
#include "world/world.h"

namespace fs {

/** Terrain heights and ground-cover rasters, supplied by the tile manager. */
class TerrainSource {
 public:
  virtual ~TerrainSource() = default;
  /** Terrain height at (x, z); NaN when that tile is not loaded. */
  virtual double heightAt(double x, double z) const = 0;
  /** Ground-cover class at (x, z) (COVER_*). */
  virtual int coverAt(double x, double z) const = 0;
};

class WorldGround : public Ground {
 public:
  WorldGround(World& world, TerrainSource& terrain) : world_(world), terrain_(terrain) {}

  void sample(double x, double z, double yRef, GroundSample& out) override;

  /** result of the most recent road lookup (shared scratch object) */
  SurfaceHit hit;
  /** global grip multiplier (rain, ice) */
  double weatherGrip = 1;
  /** places that are paved for the time being, whatever the map says (the practice yard laid over a field) */
  std::function<bool(double, double)> paved;

 private:
  World& world_;
  TerrainSource& terrain_;
  PolySample tmp_;
  double lastY_ = 0;
};

}  // namespace fs

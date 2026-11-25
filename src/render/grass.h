// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/grass.ts @ v1.7.7
/**
 * Vertical grass on the ground ("No mercy" graphics): blades standing up out
 * of the grass cover of a tile, swaying in the wind (render/grass.ts).
 *
 * The map's cover says where grass is (COVER.GRASS, world/cover.h); a tile's
 * `cover` raster and `heights` give a blade a place to stand and how high the
 * ground is there.  Each blade is a small bent quad, instanced over the grass
 * cells of the tile (one InstancedMesh per tile, like the trees in props.h),
 * and a vertex shader bends them by a wind that travels across the world, so a
 * field ripples as the wind moves over it.
 *
 * It is drawn only on the "No mercy" graphics (the same flag as the water on
 * the roads): there are many blades, and only that preset pays for them.  The
 * blades thin out with the distance and the graphics' tree density, so a phone
 * is not asked to draw a whole meadow.
 */
#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "engine/engine.h"
#include "render/tiles.h"

namespace fs {

/** The world's grass, tile by tile, swaying in the wind. */
class GrassRenderer {
 public:
  /** how much grass is drawn (the graphics' tree density) */
  double density = 1;

  GrassRenderer();
  GrassRenderer(const GrassRenderer&) = delete;
  GrassRenderer& operator=(const GrassRenderer&) = delete;

  /** The grass on (or off): the "No mercy" graphics turn it on. */
  void setEnabled(bool on);
  /** Blades standing up out of a tile's grass cover. */
  void add(LoadedTile& tile);
  void remove(const std::string& key);
  /** Thinning the grass with the distance and the graphics' density, and the wind moving on. */
  void update(double x, double z, double dt);
  void dispose();

  /** the grass of a tile (null: none) – for the tests */
  const InstancedMesh* meshOf(const std::string& key) const;

 private:
  /** The grass of one tile: an InstancedMesh over its grass cells. */
  struct TileGrass {
    std::shared_ptr<InstancedMesh> mesh;
    double cx = 0;
    double cz = 0;
    int total = 0;
  };
  GeometryPtr geo_;
  MaterialPtr mat_;
  UniformPtr uTime_ = uniform(0.0);
  UniformPtr uWind_ = uniform(Vector2(1, 0.3));
  std::unordered_map<std::string, TileGrass> perTile_;
  /** whether it is drawn at all */
  bool enabled_ = false;
  double time_ = 0;
};

}  // namespace fs

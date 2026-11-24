// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The cars parked along the streets (src/world/parked), drawn as instances:
 * one mesh per kind of car and tile.  On the lowest graphics, and far away on
 * the others, a car is two boxes; near by on medium and above it has a
 * shaped body, glass, wheels and lights.  They are drawn on every graphics
 * level – a street lined with cars drives differently from an empty one.
 *
 * Origin: src/render/parked.ts @ v1.7.7
 */
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/engine.h"

namespace fs {

struct LoadedTile;

/** The parked cars of the loaded tiles. */
class ParkedCarRenderer {
 public:
  /** near by in detail (medium graphics and above) */
  bool detail = true;

  ParkedCarRenderer();

  void add(LoadedTile& tile, const std::shared_ptr<Group>& group);
  void remove(const LoadedTile& tile);
  /** Detail by the distance from the viewer. */
  void update(double x, double z);

 private:
  struct TileCars {
    std::vector<std::shared_ptr<InstancedMesh>> meshes;
    /** the kind of car of each mesh */
    std::vector<int> kinds;
    double cx = 0;
    double cz = 0;
  };

  std::vector<GeometryPtr> simple_;
  std::vector<GeometryPtr> detailed_;
  const MaterialPtr material_;
  std::unordered_map<std::string, TileCars> perTile_;
};

}  // namespace fs

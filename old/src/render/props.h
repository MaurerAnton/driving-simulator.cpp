// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Instanced scenery per tile: trees and street lamps.
 *
 * Origin: src/render/props.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).
 */
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/geometry.h"
#include "engine/scene.h"
#include "render/tiles.h"

namespace fs {

class PropRenderer {
 public:
  PropRenderer();

  /** global multiplier for how many trees are drawn (quality setting) */
  double density = 1;

  /** Trees throw shadows (the highest quality only: there are many of them). */
  void setShadows(bool on);

  void add(LoadedTile& tile);
  void remove(const std::string& key);
  /** Choose tree detail and density per tile from the viewer distance. */
  void update(double x, double z);

 private:
  struct TreeSet {
    GeometryPtr hi[4];
    GeometryPtr lo[4];
  };
  struct TileProps {
    Group* group = nullptr;
    std::vector<Mesh*> trees;
    std::vector<int> treeKind;
    std::vector<int> treeTotal;
    double cx = 0, cz = 0;
  };

  bool shadows_ = false;
  TreeSet trees_;
  GeometryPtr lampGeo_;
  MaterialKind kindTree_ = MaterialKind::Struct;
  std::map<std::string, TileProps> perTile_;
};

}  // namespace fs

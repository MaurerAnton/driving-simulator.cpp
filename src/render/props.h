// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Instanced scenery per tile: trees and street lamps.
 *
 * Origin: src/render/props.ts @ v1.7.7
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/engine.h"
#include "render/parked.h"

namespace fs {

struct LoadedTile;

/**
 * How wide a tree is drawn (the scale across of its unit shape) for its
 * height `h` and a random number `r` in [0, 1).  A broadleaf tree's crown is
 * as wide as real ones: about 6 m at 7 m high, 8–9 m at 10 m, 11–13 m at
 * 15 m, at most 14 m (the unit crown is 0.6 across); the other kinds keep
 * their shape's proportions.
 */
double treeWidth(int kind, double h, double r);

/** which of the trees' materials (for what teaches them the season): 'deciduous' | 'evergreen' | 'orchard' */
enum class TreeMaterialKind { Deciduous, Evergreen, Orchard };

class PropRenderer {
 public:
  /** global multiplier for how many trees are drawn (quality setting) */
  double density = 1;
  /** the cars parked along the streets (render/parked): on every graphics level, in detail from medium on */
  ParkedCarRenderer parked;

  PropRenderer();

  /** The season on the trees: broadleaf and the other deciduous kind, conifers, fruit trees (game/outdoors). */
  void setSeason(const std::function<void(Material&, TreeMaterialKind)>& patch);
  /** Trees throw shadows (the highest quality only: there are many of them). */
  void setShadows(bool on);
  void add(LoadedTile& tile);
  void remove(const LoadedTile& tile);
  /** Choose tree detail and density per tile from the viewer distance. */
  void update(double x, double z);

 private:
  struct TreeMesh {
    std::shared_ptr<InstancedMesh> mesh;
    int kind = 0;
    int total = 0;
  };
  struct TileProps {
    std::shared_ptr<Group> group;
    std::vector<TreeMesh> trees;
    double cx = 0;
    double cz = 0;
  };
  /** Unit-height tree shapes (1 = full tree height), detailed and reduced. */
  struct TreeGeometries {
    std::vector<GeometryPtr> hi, lo;
  };
  static TreeGeometries treeGeometries();

  const TreeGeometries geos_;
  const GeometryPtr lampGeo_;
  /** one material for each kind of tree (broadleaf, conifer, …): the season colours and strips them each in its own way */
  std::vector<MaterialPtr> treeMats_;
  const MaterialPtr lampMat_;
  std::unordered_map<std::string, TileProps> perTile_;
  bool shadows_ = false;
};

}  // namespace fs

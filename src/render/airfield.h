// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/airfield.ts @ v1.7.7
/**
 * The airfields on the ground: runways, taxiways and aprons from the map
 * (air.json.gz), laid on the terrain – asphalt or concrete with their white
 * markings (the bars at the thresholds, the dashed middle line, the edges,
 * the number of each end), or a mown strip of grass – and taxiways with
 * their yellow line.  Built when the camera comes within a few kilometres,
 * again when it is close (the ground is known better then).
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "engine/engine.h"
#include "world/types.h"

namespace fs {

class AirfieldRenderer {
 public:
  using Height = std::function<double(double x, double z)>;

  const std::shared_ptr<Group> group = make<Group>();
  /** within this distance of the camera an airfield is drawn (m) */
  double radius = 9000;

  AirfieldRenderer(Height ground, std::function<bool(double x, double z)> detailed);

  void set(std::vector<AirfieldJson> fields);
  void update(double x, double z);

 private:
  struct Shown {
    std::shared_ptr<Group> root;
    int known;
  };
  struct Surfaces {
    std::vector<float> asphalt, concrete, grass, white, yellow;
  };

  std::vector<AirfieldJson> fields;
  std::map<int, Shown> shown;
  Height ground;
  /** whether the ground under a place is known in detail (the tiles there are loaded) */
  std::function<bool(double x, double z)> detailed;

  int known(const AirfieldJson& f) const;
  void drop(const std::shared_ptr<Group>& root);
  std::shared_ptr<Group> build(const AirfieldJson& f);
  void runway(const RunwayJson& r, Surfaces& surf, Group& root);
};

}  // namespace fs

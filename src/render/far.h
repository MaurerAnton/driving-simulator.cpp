// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The ground beyond the tiles: the hills on the horizon, the land below an
 * aeroplane.  Drawn from the coarse grid of the whole region (far.bin.gz, a
 * point every 100 m with its height and cover), in square chunks that are
 * made when the camera comes near and dropped when it leaves – finer near,
 * coarser far away.  Nothing of it is drawn within `inner` metres of the
 * camera: there the tiles show the ground as it is.
 *
 * Origin: src/render/far.ts @ v1.7.7
 */
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/engine.h"
#include "world/far.h"

namespace fs {

class FarTerrain {
 public:
  const std::shared_ptr<Group> group = make<Group>();
  /** how far it reaches, and the radius round the camera that the tiles cover (m) */
  double radius = 12000;
  double inner = 1200;
  /** the grid (none until it is read) – the tour planner asks it what kind of place a street runs through */
  std::optional<FarGround> data;

  FarTerrain();

  /** The season on the far hills (game/outdoors); the material's kind is 'far'. */
  void setSeason(const std::function<void(Material&)>& patch);
  /** The grid to draw from (none: nothing is drawn). */
  void set(std::optional<FarGround> data);
  bool ready() const { return data.has_value(); }
  /** Make the chunks around the camera at (x, z) – a few per call – and drop those out of reach. */
  void update(double x, double z, int budget = 2);

 private:
  struct Chunk {
    std::shared_ptr<Mesh> mesh;
    int step = 0;
  };

  std::unordered_map<std::string, Chunk> chunks_;
  MaterialPtr material_;
  const UniformPtr uInner_ = uniform(1200.0);
  const UniformPtr uCam_ = uniform(Vector2());
  /** the colours of the cover classes, a little greyer than near by (seen through the air) */
  std::vector<std::array<double, 3>> colours_;

  void drop(Chunk& ch);
  /** One chunk: its points every `step` cells, with a skirt round it so that chunks of other steps meet without gaps. */
  std::shared_ptr<Mesh> build(const FarGround& f, int ci, int cj, int step);
};

}  // namespace fs

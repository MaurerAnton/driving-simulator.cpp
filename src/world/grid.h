// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/grid.ts @ v1.7.7
/**
 * The grids the map is cut into – shared by the map build (tools/) and the
 * game.  Squares are numbered from a point far north-west of the world
 * origin, so that their indices are positive all over Hesse and stay the
 * same however far the map grows:
 *
 *   tiles   500 m   terrain, meshes and scenery, streamed around the car
 *   chunks  4 km    the road network, loaded around the car
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace fs {

constexpr double GRID_OFFSET = 200000;
constexpr double TILE = 500;
constexpr double CHUNK = 4000;

/** Index of the grid square of side `size` a coordinate (x or z, world metres) lies in. */
inline int gridIndex(double v, double size) {
  const double i = std::floor((v + GRID_OFFSET) / size);
  // (a place that is no number lies in no square: an index no grid has)
  if (!(i > -2e9 && i < 2e9)) return std::numeric_limits<int>::min();
  return static_cast<int>(i);
}

/** The lower corner (west, or north) of grid square `i`. */
inline double gridStart(int i, double size) { return i * size - GRID_OFFSET; }

/**
 * Which squares of a grid exist: one bit per square of the rectangle
 * (x0, z0) … (x0 + w - 1, z0 + h - 1), row by row.
 */
struct GridBits {
  int x0 = 0, z0 = 0, w = 0, h = 0;
  /** base64 of the bits, least significant bit first */
  std::string bits;
};

/** The base64 of `bytes` (as btoa encodes: standard alphabet, padded). */
std::string base64Encode(const std::vector<uint8_t>& bytes);

/** The bytes of a base64 string (as atob decodes); empty when it is not one. */
std::vector<uint8_t> base64Decode(const std::string& text);

class GridSet {
 public:
  int x0 = 0, z0 = 0, w = 0, h = 0;

  GridSet() = default;
  GridSet(int x0, int z0, int w, int h);
  GridSet(int x0, int z0, int w, int h, std::vector<uint8_t> bytes);

  static GridSet from(const GridBits& json);

  /** The smallest set that holds these squares. */
  static GridSet of(const std::vector<std::pair<int, int>>& squares);

  void add(int ix, int iz);
  bool has(int ix, int iz) const;
  int count() const;

  GridBits toJSON() const;

 private:
  std::vector<uint8_t> bytes_;
};

}  // namespace fs

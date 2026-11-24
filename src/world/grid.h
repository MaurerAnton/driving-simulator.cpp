// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The grids the map is cut into – shared by the map build (tools/) and the
 * game.  Squares are numbered from a point far north-west of the world
 * origin, so that their indices are positive all over Hesse and stay the
 * same however far the map grows:
 *
 *   tiles   500 m   terrain, meshes and scenery, streamed around the car
 *   chunks  4 km    the road network, loaded around the car
 *
 * Origin: src/world/grid.ts (MaurerAnton/driving-simulator @ 3420ad0, v0.8.0).
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fs {

constexpr double GRID_OFFSET = 200000;
constexpr double TILE = 500;
constexpr double CHUNK = 4000;

/** Index of the grid square of side `size` a coordinate (x or z, world metres) lies in. */
inline long gridIndex(double v, double size) {
  return static_cast<long>(std::floor((v + GRID_OFFSET) / size));
}

/** The lower corner (west, or north) of grid square `i`. */
inline double gridStart(long i, double size) { return i * size - GRID_OFFSET; }

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

/** Which squares of a grid exist (a rectangle of bits). */
class GridSet {
 public:
  GridSet() = default;
  GridSet(int x0, int z0, int w, int h);

  int x0 = 0, z0 = 0, w = 0, h = 0;

  static GridSet of(const std::vector<std::pair<int, int>>& squares);

  void add(int x, int z);
  bool has(int x, int z) const;
  int count() const;

  GridBits toJSON() const;
  static GridSet fromJSON(const GridBits& bits);

 private:
  std::vector<uint8_t> bits_;
};

}  // namespace fs

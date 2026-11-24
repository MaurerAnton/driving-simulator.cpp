// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/collision.ts @ v1.7.7
/**
 * Collisions of the player's car with scenery: buildings, walls, guard
 * rails, trees, lamp and sign posts.  The car is approximated by three
 * circles along its axis, the scenery by line segments and discs in a
 * uniform grid that follows the loaded map tiles.
 */
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sim/vehicle/vehicle.h"

namespace fs {

struct CollisionSource {
  std::string key;
  /** building footprints: flat x,z rings and their vertex counts */
  const std::vector<float>* rings = nullptr;
  const std::vector<uint16_t>* ringCounts = nullptr;
  /** sides of the footprints that do not count (they would stand in a lane): one bit per side, in the order of `rings` */
  const std::vector<uint8_t>* ringSkip = nullptr;
  /** x1,z1,x2,z2,y per segment */
  const std::vector<float>* segments = nullptr;
  /** x,y,z,radius per post / trunk */
  std::vector<double> discs;
};

class SceneryCollider {
 public:
  /** Another vehicle: circle centres along its axis (metres from the model origin; - = front) and their radius. */
  void setShape(const std::vector<double>& offsets, double radius);

  void add(const CollisionSource& src);

  void remove(const std::string& key);

  /**
   * Would the vehicle touch anything standing at (x, z) on the ground at
   * height y, heading `yaw`?  `margin` metres of air are asked for on top.
   * For putting the vehicle somewhere: not into a wall or onto a post.
   */
  bool blocked(double x, double y, double z, double yaw, double margin = 0.1) const;

  /** Does a circle of radius `r` at (x, z), on the ground at height y, touch anything?  (Somebody on foot.) */
  bool circleBlocked(double x, double y, double z, double r) const;

  /** the largest impact speed (m/s) into the wall of a building in the last `resolve` */
  double lastBuilding = 0;

  /**
   * Push the car out of anything it penetrates and take the velocity into
   * the obstacle away.  Returns the largest impact speed (m/s) of this call.
   */
  double resolve(Vehicle& v);

 private:
  struct Prim {
    /** 0 = segment (x1,z1,x2,z2), 1 = disc (x1,z1,r) */
    int kind;
    double x1;
    double z1;
    double x2;
    double z2;
    double r;
    /** base height; NaN = applies at any height */
    double y;
  };
  /** what one tile brought: its primitives, and the cells each lies in */
  struct Tile {
    std::deque<Prim> prims;
    std::vector<std::pair<int64_t, Prim*>> cells;
  };

  std::unordered_map<int64_t, std::vector<Prim*>> grid;
  std::unordered_map<std::string, Tile> byTile;
  /** circle centres along the car axis (model frame, metres from the wheelbase centre; - = front) */
  std::vector<double> offsets{-1.28, -0.05, 1.18};
  double radius = 0.93;
  std::array<double, 3> tmp{0, 0, 0};

  static int64_t key(int64_t ix, int64_t iz);
  void insert(Tile& tile, const Prim& p);
  const std::vector<Prim*>* cell(int64_t ix, int64_t iz) const;
};

}  // namespace fs

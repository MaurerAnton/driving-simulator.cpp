// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Collisions of the player's car with scenery: buildings, walls, guard
 * rails, trees, lamp and sign posts.  The car is approximated by three
 * circles along its axis, the scenery by line segments and discs in a
 * uniform grid that follows the loaded map tiles.
 *
 * Origin: src/sim/collision.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).
 */
#pragma once

#include <map>
#include <string>
#include <vector>

#include "sim/vehicle/vehicle.h"

namespace fs {

/** One scenery primitive: a segment or a disc in the ground plane. */
struct Prim {
  /** 0 = segment (x1,z1,x2,z2), 1 = disc (x1,z1,r) */
  int kind = 0;
  double x1 = 0, z1 = 0, x2 = 0, z2 = 0, r = 0;
  /** base height; NaN = applies at any height */
  double y = 0;
  bool yIsNaN = false;
};

/** What one map tile contributes to the collider. */
struct CollisionSource {
  std::string key;
  /** building footprints: flat x,z rings and their vertex counts */
  const double* rings = nullptr;
  const int* ringCounts = nullptr;
  int ringCount = 0;
  /** x1,z1,x2,z2,y per segment */
  const double* segments = nullptr;
  int segmentCount = 0;
  /** x,y,z,radius per post / trunk */
  const double* discs = nullptr;
  int discCount = 0;
};

class SceneryCollider {
 public:
  void add(const CollisionSource& src);
  void remove(const std::string& key);

  /**
   * The shape of the vehicle the scenery is checked against: circle centres
   * along its axis (model frame, metres from the wheelbase centre) and their
   * radius.  A car carries three, a truck with its trailer four (0.7.0).
   */
  void setShape(const double* offsets, int count, double radius);

  /**
   * Push the car out of anything it penetrates and take the velocity into
   * the obstacle away.  Returns the largest impact speed (m/s) of this call.
   */
  double resolve(Vehicle& v);

 private:
  static constexpr double CELL = 16;
  std::map<int64_t, std::vector<Prim>> grid_;
  std::map<std::string, std::vector<Prim>> byTile_;
  /** circle centres along the car axis (model frame, metres from the wheelbase centre; - = front) */
  double offsets_[4] = {-1.28, -0.05, 1.18, 0};
  int circleCount_ = 3;
  double radius_ = 0.93;

  static int64_t key(int ix, int iz);
  void insert(std::vector<Prim>& tile, const Prim& p);
};

}  // namespace fs

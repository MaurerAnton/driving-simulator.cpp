// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/ce.ts @ v1.7.7
/**
 * Class CE: what the practical test asks of a truck with trailer besides the
 * drive in traffic (Anlage 7 FeV, 2.1.3 and 2.1.4.5.2 – combinations with a
 * rigid-drawbar trailer):
 *
 *   – coupling and uncoupling ("Verbinden und Trennen"),
 *   – reversing round a corner to the left,
 *   – reversing with an offset to the right up to a loading ramp.
 *
 * This file holds the practice yard's layouts and the judging of the three
 * tasks – pure logic, no drawing.  The yard is laid over an open field of
 * the map while such a lesson runs (see tools/mapgen/yard.ts).
 *
 * (So far only the yard's frame is translated – what the construction site of the cranes and the
 * forklift's place are laid out in; the tasks of class CE come with their lessons.)
 */
#pragma once

#include <array>
#include <cmath>

#include "engine/math.h"
#include "world/types.h"

namespace fs {

/** A place and a heading (0 = "north" of the frame, positive = to the left). */
struct Pose {
  double x = 0;
  double z = 0;
  double yaw = 0;
};

/**
 * The yard's own coordinates: u along its long side, w across, the middle
 * at (0, 0).  A heading in the yard is counted like one on the map with u in
 * the place of east and w in the place of south.
 */
class YardFrame {
 public:
  const YardJson yard;

  explicit YardFrame(const YardJson& yard) : yard(yard) {
    // rot 1: the long side runs north–south (u points north)
    const double phi = (-PI / 2) * yard.rot;
    cos_ = std::cos(phi);
    sin_ = std::sin(phi);
    turn_ = -phi;
  }

  std::array<double, 2> toWorld(double u, double w) const { return {yard.x + u * cos_ - w * sin_, yard.z + u * sin_ + w * cos_}; }

  std::array<double, 2> toLocal(double x, double z) const {
    const double dx = x - yard.x;
    const double dz = z - yard.z;
    return {dx * cos_ + dz * sin_, -dx * sin_ + dz * cos_};
  }

  double yawToWorld(double yaw) const { return yaw + turn_; }

  double yawToLocal(double yaw) const { return yaw - turn_; }

  Pose poseToWorld(const Pose& p) const {
    const auto [x, z] = toWorld(p.x, p.z);
    return {x, z, yawToWorld(p.yaw)};
  }

  Pose poseToLocal(const Pose& p) const {
    const auto [x, z] = toLocal(p.x, p.z);
    return {x, z, yawToLocal(p.yaw)};
  }

 private:
  double cos_ = 1;
  double sin_ = 0;
  double turn_ = 0;
};

}  // namespace fs

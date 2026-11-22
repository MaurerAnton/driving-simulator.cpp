// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The truck of the class-CE lessons and its centre-axle trailer: a two-axle
 * distribution truck with a box body, 8.2 m long, and a tandem trailer of
 * 7.9 m.  Simple shapes, generic livery – no real vehicle is reproduced.
 *
 * Both are built around the points the physics use: the truck around the
 * ground point below the middle of its wheelbase, the trailer around the
 * middle of its axle group (x right, y up, z back, the front towards -z).
 *
 * Origin: src/render/truck.ts (MaurerAnton/driving-simulator @ ff37c5d, v0.7.0).
 */
#pragma once

#include <vector>

#include "engine/scene.h"
#include "render/car.h"
#include "render/instruments.h"
#include "render/mirrors.h"

namespace fs {

class PlayerTruck {
 public:
  PlayerTruck();

  Group root;
  /** the trailer: a group of its own, placed in the world by the game */
  Group trailer;
  /** the driver's eyes (truck coordinates) */
  Vector3 eye{-0.62, 2.42, -2.55};
  std::vector<std::unique_ptr<Mirror>> mirrors;

  /**
   * How solid the inside of the cab looks from the driver's seat: 1 = solid,
   * less = the road shows through lining, dashboard and steering wheel.
   * (From outside the cab is always solid: its paint is another face.)
   */
  void setCabinOpacity(double opacity);

  /** The truck's moving parts and lamps. */
  void update(const CarVisualState& s, double dt, double now);

  /**
   * Where the trailer is (the middle of its axle group on the ground), and
   * whether it stands on its leg.
   */
  void setTrailer(double x, double y, double z, double yaw, double pitch, double wheelSpin,
                  bool coupled);

 private:
  std::vector<Group*> wheels_;
  std::vector<Group*> trailerWheels_;
  Group* steering_ = nullptr;
  std::vector<Mesh*> brakeLamps_;
  /** the indicator boxes with the side they signal (-1 left, 1 right) */
  std::vector<std::pair<Mesh*, int>> indicators_;
  Mesh* leg_ = nullptr;
  Group* lining_ = nullptr;
  Group* fittings_ = nullptr;
  InstrumentPanel panel_;
};

}  // namespace fs

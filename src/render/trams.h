// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Trams and trains as seen from the road: each car a simple lofted body
 * (skirt, window band, roof), bogies, bellows between the cars, a
 * pantograph on trams.  Generic shapes and liveries – no real vehicle is
 * reproduced.  Where they are and how they move is decided in sim/rail.ts.
 *
 * Origin: src/render/trams.ts (MaurerAnton/driving-simulator @ ff37c5d,
 * v0.7.0).
 */
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/scene.h"
#include "sim/rail.h"

namespace fs {

/**
 * Shows the rail vehicles where the simulation has them.  One geometry per
 * (kind, nose, bellows) is built once and shared by every car that fits it.
 */
class RailActors {
 public:
  RailActors();

  Group group;

  /** Show the vehicles where the simulation has them. */
  void update(const std::vector<RailVehicle>& vehicles);

 private:
  struct Shown {
    std::vector<Mesh*> cars;
  };
  std::map<int, std::unique_ptr<Shown>> shown_;
  std::map<std::string, GeometryPtr> geometries_;

  GeometryPtr carGeometry(int kind, int index, int count);
};

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Moving things in the scene: traffic cars, pedestrians and traffic-light
 * heads.
 *
 * Origin: src/render/actors.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).
 */
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/scene.h"
#include "render/car.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "world/world.h"

namespace fs {

/** Draws the ambient traffic: one pooled exterior per car, a figure per pedestrian. */
class TrafficRenderer {
 public:
  TrafficRenderer();

  Group group;

  void update(const std::vector<TrafficCar>& cars, const std::vector<Pedestrian>& peds,
              double time);

 private:
  struct Entry {
    std::unique_ptr<CarExterior> car;
    int style = 0;
  };
  std::map<int, Entry> cars_;
  std::vector<std::vector<std::unique_ptr<CarExterior>>> pool_;
  std::map<int, std::unique_ptr<Group>> peds_;
  GeometryPtr pedBody_, pedLeg_, pedHead_;
  uint32_t skin_ = 0xd9a989;
  uint32_t trousers_ = 0x2a2d33;
};

/** All traffic-light heads of the map (a few hundred instances at most). */
class SignalRenderer {
 public:
  explicit SignalRenderer(World& world);

  Group group;
  /** positions of the poles for the collision system: x, y, z, radius */
  std::vector<double> poles;

  void update(const SignalSystem& signals);

 private:
  struct Head {
    int controller = 0;
    int group = 0;
    /** index of the first of the three lamp instances */
    int lamp = 0;
    int state = -1;
  };
  std::vector<Head> heads_;
  std::unique_ptr<Mesh> lamps_;
};

}  // namespace fs

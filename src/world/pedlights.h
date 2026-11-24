// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/pedlights.ts @ v1.7.7
/**
 * The pedestrian lights of the signalised crossings: on each kerb a pole
 * with a head that faces the other kerb (one looks across the road at the
 * light one walks towards) and a box with the button and the lamp "Signal
 * kommt", where the people wait.
 *
 * At a crossing of its own the poles are those of the vehicles' lights
 * (render/actors.ts puts them a little beyond the stripes, on the right of
 * each direction); at a junction they stand at the crossing.
 */
#pragma once

#include <vector>

#include "world/types.h"

namespace fs {

class World;

struct PedPole {
  const CrossingJson* crossing = nullptr;
  /** the controller, and the group of the vehicles that cross the stripes (the pedestrians walk while it has red) */
  int controller = 0;
  int group = 0;
  double x = 0;
  double y = 0;
  double z = 0;
  /** the way the head faces (towards the people on the other kerb; 0 = north, + = left) */
  double yaw = 0;
  /** the way the button box faces (towards the stripes, along the kerb) */
  double boxYaw = 0;
};

/** The poles of the pedestrian lights of every signalised crossing that is loaded. */
std::vector<PedPole> pedPoles(const World& world);

/** The pole whose button is within reach of somebody standing at (x, z) – null if there is none. */
const PedPole* poleInReach(const std::vector<PedPole>& poles, double x, double z, double reach = 1.4);

}  // namespace fs

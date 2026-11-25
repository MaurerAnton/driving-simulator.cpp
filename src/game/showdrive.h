// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The test place ("Every model", game.ts buildShowroom): every model of the
 * game stands on the practice yard – and each can be driven.  A tap on one
 * (game/takeover.h), or the door key pressed on foot beside it, gets the
 * player into it: the drive on the test place goes on with that vehicle,
 * from where it stood (the model standing there is hidden meanwhile; the
 * others stay).
 *
 * Here: where each model stands and what it is, which one is being driven,
 * and the boxes a tap is tested against.
 *
 * Origin: src/game/showdrive.ts @ v1.7.7
 */
#pragma once

#include <vector>

#include "engine/engine.h"
#include "game/spawnplace.h"
#include "game/takeover.h"
#include "game/vehicles.h"

namespace fs {

/** A model standing on the test place. */
struct ShowSpot {
  VehicleKind kind;
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  Outline outline;
  double height = 0;
  /** what draws it (the vehicle, its trailer) */
  std::vector<Object3D*> roots;
};

class ShowSpots {
 public:
  std::vector<ShowSpot> list;
  /** the one being driven (−1: none) – hidden while it is */
  int taken = -1;

  void add(ShowSpot s);
  /** Get into model `i`: the next drive on the test place begins where it stands. */
  const ShowSpot* choose(int i);
  /**
   * A drive on the test place begins: where (the model chosen – null: the
   * usual place); the model chosen is hidden, the others are shown again.
   */
  const ShowSpot* start();
  /** The model somebody on foot at (x, z) stands beside (within `reach` metres of its outline) – not the one being driven; −1: none. */
  int near(double x, double z, double reach = 1.6) const;
  /** The boxes of the models a tap may choose (not the one being driven). */
  std::vector<PickBox> boxes();

 private:
  /** the one just chosen: where the next drive on the test place begins (once; −1: none) */
  int next_ = -1;
  std::vector<PickBox> boxList_;
};

}  // namespace fs

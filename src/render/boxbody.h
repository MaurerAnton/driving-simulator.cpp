// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/boxbody.ts @ v1.7.7
/**
 * The box body of a truck with its two rear doors, which open: for the
 * goods to be loaded (render/loading.ts).
 *
 * The box is a shell – paint outside, plywood inside, a floor of planks –
 * open at the back behind a frame, and closed there by two doors hung at
 * the rear corners.  Opened, each door swings out and round until it lies
 * against the side of the box (270°, held there as on a real truck), and
 * the inside of the box is seen.
 *
 * The frame is the truck's: x to the right, y up, z to the back; the box
 * runs from `front` to `rear` along z, from `floor` to `top` in height.
 */
#pragma once

#include <memory>
#include <vector>

#include "engine/engine.h"

namespace fs {

class BoxBody {
 public:
  const std::shared_ptr<Group> group = make<Group>();
  /** the inside: where the goods may stand (truck frame) */
  struct Inside {
    double x0, x1, floor, top, front, rear;
  } inside{};
  /** the plate with the lettering on the doors: hidden while they are open */
  std::shared_ptr<Object3D> rearPlate;

  /** `paint`: the outside of the box */
  BoxBody(double hw, double floor, double top, double front, double rear, const MaterialPtr& paint);
  BoxBody(const BoxBody&) = delete;
  BoxBody& operator=(const BoxBody&) = delete;

  /** How far the doors are open: 0 shut … 1 swung round against the sides. */
  void setDoors(double open);

  double doorsOpen() const { return open_; }

 private:
  std::vector<std::shared_ptr<Group>> doors_;
  double open_ = 0;
};

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/walker.ts @ v1.7.7
/**
 * Somebody on foot: "the vehicle" of the pedestrian, which is none, and the
 * player's own figure.
 *
 * The game asks every vehicle for a model with a root, an eye and mirrors; on
 * foot the camera is the person's eyes – or stands behind them – so the
 * pedestrian's vehicle model is empty.  The figure seen from behind is one of
 * the people of render/people, always the same one, posed each frame in
 * the world's frame (not under a vehicle's root: the game's vehicle is only a
 * stand-in that follows the person).
 */
#pragma once

#include <memory>

#include "render/fleet.h"
#include "render/people.h"

namespace fs {

class WalkerModel : public VehicleModel {
 public:
  WalkerModel() : VehicleModel(Vector3(0, 1.62, 0)) { root->name = "on foot"; }
  /* nothing to see through */
  void setCabinOpacity(double) override {}
  /* nothing moves */
  void update(const CarVisualState&, double, double) override {}
};

/** The look of the player's figure (render/people): a grown-up of 1.74 m in a blue jacket and jeans, hands free. */
inline constexpr int PLAYER_LOOK = 27;

/** The player's own figure, for the times the camera is not their eyes. */
class PlayerFigure {
 public:
  std::shared_ptr<Group> group;

  PlayerFigure() {
    pose_.look = PLAYER_LOOK;
    group = people_.group;
    group->name = "the player on foot";
    people_.update({}, 0);
  }

  /**
   * Standing at (x, y, z), facing `yaw` (0 north, + left); `walked`: the
   * metres walked so far (the gait follows them), `moving`: whether a step is
   * being taken.  `time`: seconds, for the small movements of standing.
   */
  void show(double x, double y, double z, double yaw, double walked, bool moving, double time) {
    PersonPose& p = pose_;
    p.x = x;
    p.y = y;
    p.z = z;
    p.yaw = yaw;
    p.walk = moving ? walked : -1;
    people_.update({p}, time);
    shown_ = true;
  }

  void hide() {
    if (!shown_) return;
    people_.update({}, 0);
    shown_ = false;
  }

 private:
  PeopleRenderer people_{1};
  PersonPose pose_;
  bool shown_ = false;
};

}  // namespace fs

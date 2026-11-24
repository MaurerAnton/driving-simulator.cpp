// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/worldtap.ts @ v1.7.7
/**
 * A tap (or a click) on a place in the 3D picture: a small bar with two
 * buttons shows beside it for a few seconds – "Teleport here" (⤓) puts the
 * player there (game.ts teleportTo: a vehicle into the lane of the nearest
 * road, facing along it; somebody on foot at that very place), "Drive there"
 * (🚗) sends the vehicle off to it by itself (game.ts driveHere).  Each button
 * shows only where it is allowed.  With "tap to walk" (game/walkmodes.ts) the
 * walker also sets off there.
 *
 * Only a tap counts: let go soon and near where it came down, with one
 * finger (or the left mouse button) – a drag that looks round, a pinch, the
 * on-screen controls and the rest of the HUD (which are not the picture)
 * never show it.  Nor does a tap that is somebody else's: on the vehicle's
 * screen, on a parked car, while a place for a car is being picked
 * (`claimed`).  Where the tap meets the ground is found along the ray
 * through the terrain's heights (game/spawnplace.h rayGround); nothing is
 * looked for while nothing is tapped.  The bar follows its place while the
 * camera moves, and goes after a few seconds, at the next tap, or when
 * neither of the two is possible any more.
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "engine/engine.h"
#include "game/spawnplace.h"
#include "platform/dom.h"

namespace fs {

struct WorldTapHost {
  dom::El canvas;
  std::shared_ptr<Camera> camera;
  /** where the button goes (the game's #ui element) */
  dom::El ui;
  /** the terrain's height (NaN: not loaded) */
  std::function<double(double x, double z)> heightAt;
  /** may the player be teleported now? */
  std::function<bool()> allowed;
  /** may the vehicle be sent off to that place now (it drives there by itself)? */
  std::function<bool()> canDrive;
  /** is a tap there somebody else's – checked when the finger comes down (`down`) and when it is let go */
  std::function<bool(double cx, double cy, bool down)> claimed;
  /** a place on the ground was tapped (tap to walk – also where teleporting is not allowed) */
  std::function<void(const GroundPoint& p)> tapped;
  /** teleport there; `yaw`: the way the camera looked (which way a lane is chosen) */
  std::function<void(const GroundPoint& p, double yaw)> teleport;
  /** drive there by itself (game.ts driveHere) */
  std::function<void(const GroundPoint& p)> drive;
  std::function<std::string(const std::string& key)> t;
};

class WorldTap {
 public:
  explicit WorldTap(WorldTapHost host);
  // (the page's listeners name this object: it stays where it is)
  WorldTap(const WorldTap&) = delete;
  WorldTap& operator=(const WorldTap&) = delete;

  void hide();

  /** Every picture: the bar where its place is now on the screen – or gone. */
  void frame();

 private:
  const WorldTapHost host;
  dom::El bar;
  dom::El teleportBtn;
  dom::El driveBtn;
  dom::El ring;
  Raycaster ray;
  Vector2 ndc;
  Vector3 v;
  /** the fingers (or the mouse) down on the picture now, and whether more than one was down in this touch */
  struct Down {
    double x = 0;
    double y = 0;
    double t = 0;
    bool claimed = false;
  };
  std::unordered_map<int, Down> downs;
  bool multi = false;
  /** the place offered, and until when */
  struct At {
    double x = 0;
    double y = 0;
    double z = 0;
    double yaw = 0;
  };
  std::optional<At> at = std::nullopt;
  double until = 0;

  void tap(double cx, double cy);
  /** One button's words: its icon and what it offers, in the language in use now. */
  void label(const dom::El& b);
};

}  // namespace fs

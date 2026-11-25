// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/forkliftmodes.ts @ v1.7.7
/**
 * What the forklift's drive is made of besides the forklift itself (no
 * scene, no page – the game, game/forkliftplay.h, uses these):
 *
 *  - its ways to play: "Forklift work" (`FORK_WORK_MODE`) is the tasks on
 *    the forklift yard; any other way – the free drive, the test place –
 *    begins where the menu put it;
 *  - its cameras, in the order the camera key (V) goes through them: from
 *    the side, where the drive starts – the forks, the load and the mast in
 *    view –, from the operator's seat, from behind, the free camera;
 *  - getting off beside the seat and on again within a few steps of it.
 */
#pragma once

#include <array>
#include <string>

namespace fs {

/** The way to play (the menu's mode) of the work on the forklift yard: its tasks; any other is a free drive. */
inline constexpr const char* FORK_WORK_MODE = "forklift";

/** The forklift's cameras, in the order V goes through them: "side" (the start), "cockpit" (the seat), "chase" (behind), "free". */
using ForkView = std::string;
inline const std::array<ForkView, 4> FORK_VIEWS = {"side", "cockpit", "chase", "free"};
/**
 * The view from the side: the camera circling the forklift rests on its
 * left (the operator's side), a little ahead – the forks, the load and the
 * mast in view (radians, as Input.orbitRest: 0 behind, + to the right).
 */
inline constexpr double FORK_SIDE_YAW = -(3.141592653589793 / 2 + 0.35);

/** The next camera after this one (V). */
ForkView nextForkView(const ForkView& v);

struct ForkCamera {
  /** "cockpit", "chase" or "free" */
  std::string mode;
  double rest;
  bool operator==(const ForkCamera&) const = default;
};
/** What the game's camera is for a view of the forklift: its mode, and where the camera circling it rests. */
ForkCamera forkCamera(const ForkView& v);

struct ForkExit {
  double x;
  double z;
  double yaw;
};
/** Where the operator stands after getting off: beside the seat, on the left (`side` −1) or the right (1), facing the way it faces. */
ForkExit forkliftExit(double x, double z, double yaw, double side = -1);

/** Can somebody standing at (x, z) get on the forklift whose middle is at (fx, fz)? */
bool nearForklift(double fx, double fz, double x, double z);

}  // namespace fs

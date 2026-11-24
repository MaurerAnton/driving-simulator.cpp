// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/lessonstart.ts @ v1.7.7
/**
 * The start of a driving lesson: where it takes place, and whether the
 * player drives it or watches it.  Shown each time a lesson on the road is
 * started – from any of the menus, since they all start the drive the same
 * way (game.ts begin) –, with what was chosen last time already chosen:
 *
 *  - where: at the start place chosen in the menu (as before – the
 *    practice ground, a car park), at real places anywhere in town, or in a
 *    district picked on a list or on a little map of the districts
 *    (game/lessonplaces.ts);
 *  - how: two big buttons, "Drive it myself" and "Watch it" – then the car
 *    drives the lesson by itself at that place and the instructor says what
 *    it does (game/lessonwatch.ts).
 *
 * Built from the classes the other pages use (overlay, panel, btn, row,
 * note), with tap targets of at least 48 px; the map is a canvas drawn once
 * per choice – no tiles, nothing to load.
 */
#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "game/lessonplaces.h"
#include "game/settings.h"
#include "i18n/index.h"
#include "platform/dom.h"
#include "world/types.h"

namespace fs {

/** `(key, vars?) => string`: a text in the player's language (the original's `T`) */
using TrFn = std::function<std::string(const std::string& key, const TrParams& vars)>;

/** How the districts are laid onto the map's picture: x east to the right, z south downwards, the same scale both ways. */
struct DistrictFrame {
  double w = 0;
  double h = 0;
  double cx = 0;
  double cz = 0;
  double k = 1;
  /** picture coordinates of a district's middle */
  std::array<double, 2> toPx(double x, double z) const { return {w / 2 + (x - cx) * k, h / 2 + (z - cz) * k}; }
};

/** The frame for `areas` on a picture `w` × `h` with `pad` pixels round it. */
DistrictFrame districtFrame(const std::vector<LessonArea>& areas, double w, double h, double pad = 28);

/** The district whose dot is nearest to the picture point (px, py), within `reach` pixels – its index, or −1. */
int districtAt(const std::vector<LessonArea>& areas, const DistrictFrame& frame, double px, double py, double reach = 40);

struct LessonStartOptions {
  /** the lesson's name */
  std::string title;
  /** whether the lesson can be put at a real place (not when the drive starts at an address) */
  bool place = false;
  /** whether it can be watched */
  bool watch = false;
};

/**
 * The panel.  `done` once the player chose to drive or to watch (the settings are set – lessonPlace,
 * lessonArea, lessonWatch; the caller saves them and starts the lesson); `back` closes it.
 */
dom::El lessonStartPanel(const TrFn& t, Settings& s, const std::vector<SpawnJson>& spawns, const LessonStartOptions& o, std::function<void()> done, std::function<void()> back);

}  // namespace fs

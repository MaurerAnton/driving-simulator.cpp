// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/clearlanes.ts @ v1.7.7
/**
 * Nothing of the scenery may stand in a lane.
 *
 * The map is put together from sources that do not know of each other: a
 * guard rail of one carriageway can end up in the lane of the one beside it,
 * a sign post on a traffic island the road model has no room for, the corner
 * of a house in a street that is narrower than its lanes.  A human driver
 * steers round such a thing or is stuck in front of it; the demonstration
 * driver is stuck.  So whatever comes closer to the middle of a lane than a
 * vehicle is wide is taken out of the way here: posts are moved beside the
 * lanes (or dropped), walls and footprints lose the pieces that reach in.
 *
 * The map build does this for the scenery it draws (and marks the tile);
 * the game repeats it for the collision shapes of tiles that are not marked.
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "core/js.h"
#include "sim/world_needs.h"
#include "world/lanes.h"
#include "world/tilefmt.h"

namespace fs {

/** how far a post (sign, lamp, trunk) keeps from the middle of a lane */
inline constexpr double POST_CLEAR = 1.5;
/** how far walls, guard rails and buildings keep from it: the half width of the truck and a little */
inline constexpr double WALL_CLEAR = 1.3;

/** Move the sign posts that stand in a lane beside it; those there is no place for are dropped. Returns how many were moved and dropped. */
std::pair<int, int> clearSigns(LaneGraph& lanes, std::vector<SignPost>& signs);

/** Drop the lamps that stand in a lane.  Returns how many. */
int clearObjects(LaneGraph& lanes, std::vector<TileObject>& objects);

/** Is a post at this place in a lane? */
bool postInLane(LaneGraph& lanes, double x, double y, double z);

/**
 * The walls of a tile as the collision test wants them (x1, z1, x2, z2, y per
 * piece; y = NaN: at any height), without the pieces that reach into a lane:
 * the footprints of the buildings (rings) and the free-standing walls.
 */
std::vector<float> clearWalls(LaneGraph& lanes, const std::vector<float>* rings, const std::vector<uint16_t>* ringCounts, const std::vector<float>* segments);

/**
 * The sides of the footprints that would stand in a lane: one bit per side,
 * in the order of the rings (for tiles that keep their footprints whole and
 * carry this beside them).  No value when none does.
 */
template <typename Rings, typename Counts>
std::optional<std::vector<uint8_t>> ringSidesInLanes(LaneGraph& lanes, const Rings& rings, const Counts& ringCounts) {
  std::vector<uint8_t> bits(static_cast<size_t>(std::ceil(static_cast<double>(rings.size()) / 2 / 8)), 0);
  bool any = false;
  size_t o = 0;
  for (size_t r = 0; r < ringCounts.size(); r++) {
    const size_t n = static_cast<size_t>(ringCounts[r]);
    for (size_t i = 0; i < n; i++) {
      const size_t j = (i + 1) % n;
      if (needs::wallInLane(lanes, rings[o + i * 2], rings[o + i * 2 + 1], rings[o + j * 2], rings[o + j * 2 + 1], JS_NAN, WALL_CLEAR)) {
        const size_t side = o / 2 + i;
        bits[side >> 3] |= static_cast<uint8_t>(1 << (side & 7));
        any = true;
      }
    }
    o += n * 2;
  }
  if (!any) return std::nullopt;
  return bits;
}

}  // namespace fs

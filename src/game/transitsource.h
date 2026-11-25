// SPDX-License-Identifier: AGPL-3.0-only
/**
 * transitSource: what the big map needs to know of the trams and buses
 * (src/ui/transitmap.ts), made from what the game has read – positions along
 * their lines, at the game's clock.  (Of src/game/spectate.ts, whose watching
 * is not ported yet, the part that serves the map and the buses of the
 * timetables; lineTakeOver with it.)
 *
 * Origin: src/game/spectate.ts @ v1.7.7 (transitSource, lineTakeOver)
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "ui/transitmap.h"

namespace fs {

class RailTraffic;
class NetIndex;
class World;

/** What the game has read that the trams and buses on the map come from. */
struct TransitGetters {
  std::function<const Timetable*()> timetable;
  std::function<const LinesJson*()> lines;
  std::function<RailTraffic*()> rail;
  std::function<BusLines*()> busLines;
  /** the roads of the whole map (a map in pieces), else null */
  std::function<const NetIndex*()> net;
  World* world = nullptr;
  std::function<Stops*()> stops;
};

/** The trams and buses for the big map: positions along their lines, at the game's clock. */
std::unique_ptr<TransitSource> transitSource(TransitGetters get);

/** A bus taken over on its line: the ways to drive and where its last stop is. */
struct TakeOver {
  std::vector<int> ways;
  std::optional<Spot> end;
};

/**
 * A bus taken over on its line (its number among the bus lines) from the
 * line's way `k` on: the ways from there to the end of its route, and where
 * its last stop is (null: not known).  (Any bus of the traffic on a line,
 * too: game/takeover.ts.)
 */
TakeOver lineTakeOver(TransitSource& src, int index, int k);

}  // namespace fs

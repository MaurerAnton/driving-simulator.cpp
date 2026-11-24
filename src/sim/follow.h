// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/follow.ts @ v1.7.7
/**
 * Keeps what is derived from the road network in step with it where the
 * network comes in pieces (world/loader): the lanes, the roads cars may
 * appear on, the signal controllers and the green arrows beside their
 * lights.  The same for the game and for the tests that drive on the real
 * map.
 *
 * (The map layer this was translated against has neither the loader nor a lane graph that
 * follows a map in pieces – sim/world_needs.h, HAS_LANES_IN_PIECES.  So this is written as
 * the original is, for a loader `map` with `listen({added, removed})` and `world`, and a lane
 * graph with addEdge, completeNode, removeNode and removeEdge: it compiles where it is used
 * with those.)
 */
#pragma once

#include <functional>

#include "sim/arrows.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "world/lanes.h"
#include "world/world.h"

namespace fs {

struct MapFollowers {
  LaneGraph* lanes = nullptr;
  Traffic* traffic = nullptr;
  SignalSystem* signals = nullptr;
  /** called after every change (for what is drawn: the signal heads) */
  std::function<void()> changed = nullptr;
};

/** `who` may be swapped for another set later (its fields are read at each change). */
template <typename MapLoaderT, typename Who = MapFollowers>
void followMap(MapLoaderT& map, Who& who) {
  map.listen({
      .added =
          [&map, &who](const WorldChange& c) {
            for (Edge* e : c.edges) who.lanes->addEdge(*e);
            for (Node* n : c.nodes) who.lanes->completeNode(*n);
            if (who.traffic) who.traffic->addRoads(c.edges);
            if (who.signals) who.signals->sync();
            if (who.signals) markArrows(map.world, *who.lanes, *who.signals, c.nodes);
            if (who.changed) who.changed();
          },
      .removed =
          [&who](const WorldChange& c) {
            if (who.traffic) who.traffic->removeRoads(c.edges);
            if (who.signals) dropArrows(*who.signals, c.nodes);
            for (Node* n : c.nodes) who.lanes->removeNode(*n);
            for (Edge* e : c.edges) who.lanes->removeEdge(*e);
            if (who.changed) who.changed();
          },
  });
}

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/follow.ts @ v1.7.7
/**
 * Keeps what is derived from the road network in step with it where the
 * network comes in pieces (world/loader): the lanes, the roads cars may
 * appear on, the signal controllers and the green arrows beside their
 * lights.  The same for the game and for the tests that drive on the real
 * map.
 */
#pragma once

#include <functional>

#include "sim/arrows.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "world/lanes.h"
#include "world/loader.h"
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
inline void followMap(MapLoader& map, MapFollowers& who) {
  MapLoader* loader = &map;
  MapFollowers* w = &who;
  map.listen({
      .added =
          [loader, w](const WorldChange& c) {
            for (Edge* e : c.edges) w->lanes->addEdge(*e);
            for (Node* n : c.nodes) w->lanes->completeNode(*n);
            if (w->traffic) w->traffic->addRoads(c.edges);
            if (w->signals) w->signals->sync();
            if (w->signals) markArrows(loader->world, *w->lanes, *w->signals, c.nodes);
            if (w->changed) w->changed();
          },
      .removed =
          [w](const WorldChange& c) {
            if (w->traffic) w->traffic->removeRoads(c.edges);
            if (w->signals) dropArrows(*w->signals, c.nodes);
            for (Node* n : c.nodes) w->lanes->removeNode(*n);
            for (Edge* e : c.edges) w->lanes->removeEdge(*e);
            if (w->changed) w->changed();
          },
  });
}

}  // namespace fs

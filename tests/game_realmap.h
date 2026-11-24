// SPDX-License-Identifier: AGPL-3.0-only
// Origin: tests/realmap.ts @ v1.7.7 (what the game's own tests use of it: the map, its lanes, its network)
/**
 * The real map for tests that look things up on it: opened the way the game
 * opens it (src/world/loader.h), from the folder MAP_DIR names – or from the
 * original's public/map beside this repository.  The map is not part of the
 * repository; tests that need it leave their cases on it out without it.
 */
#pragma once

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "game/address.h"
#include "platform/native/native.h"
#include "world/lanes.h"
#include "world/loader.h"
#include "world/mapfiles.h"
#include "world/netindex.h"
#include "world/routegraph.h"
#include "world/world.h"

namespace fs {

/** the folder of the map ('': there is none) */
inline std::string realMapDir() {
  std::vector<std::string> dirs;
  if (const char* dir = std::getenv("MAP_DIR")) dirs.push_back(dir);
  dirs.push_back("public/map");
  dirs.push_back("../driving-simulator/public/map");
  dirs.push_back("/home/user/driving-simulator/public/map");
  for (std::string dir : dirs) {
    while (!dir.empty() && dir.back() == '/') dir.pop_back();
    if (std::ifstream(dir + "/map.json") || std::ifstream(dir + "/world.json")) return dir + "/";
  }
  return "";
}

struct RealMap {
  std::shared_ptr<MapLoader> map;
  World* world = nullptr;
  std::unique_ptr<LaneGraph> lanes;
  /** the roads of the whole map (a map in pieces), for ways and places beyond what is loaded */
  std::shared_ptr<NetIndex> index;
  /** (the connected network of a map in one piece, once it is asked for) */
  std::optional<std::unordered_set<int>> main;

  /** is a lane part of the network one can drive around in? */
  bool usable(const Lane& lane) {
    if (index) return index->main[static_cast<size_t>(wayOf(lane))] == 1;
    if (!main) main = mainNetwork(*world, *lanes);
    return main->count(lane.id) > 0;
  }

  /** `await map.loadAround(x, z, radius)` */
  void loadAround(double x, double z, double radius) { plat::native::wait(map->loadAround(x, z, radius)); }
};

/** The map, opened (null: there is none here). */
inline std::unique_ptr<RealMap> openRealMap() {
  const std::string dir = realMapDir();
  if (dir.empty()) {
    std::printf("  (no map – MAP_DIR, public/map or ../driving-simulator/public/map: the cases on the real map are left out)\n");
    return nullptr;
  }
  auto r = std::make_unique<RealMap>();
  r->map = plat::native::wait(MapLoader::open(diskFiles(dir)));
  r->world = &r->map->world;
  r->lanes = std::make_unique<LaneGraph>(*r->world);
  // what follows the road network as it is loaded (sim/follow.ts): the lanes
  LaneGraph* lanes = r->lanes.get();
  r->map->listen({.added =
                      [lanes](const WorldChange& c) {
                        for (Edge* e : c.edges) lanes->addEdge(*e);
                        for (Node* n : c.nodes) lanes->completeNode(*n);
                      },
                  .removed =
                      [lanes](const WorldChange& c) {
                        for (Node* n : c.nodes) lanes->removeNode(*n);
                        for (Edge* e : c.edges) lanes->removeEdge(*e);
                      }});
  const std::shared_ptr<const NetData> net = plat::native::wait(r->map->net());
  if (net) r->index = std::make_shared<NetIndex>(net);
  return r;
}

}  // namespace fs

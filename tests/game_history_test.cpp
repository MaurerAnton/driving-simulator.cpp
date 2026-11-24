// LINK: game core platform
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The history of the map: the places picked in its search and the ways
 * started, kept on this device – newest first, each once, only so many.
 *
 * Origin: tests/game/history.test.ts @ v1.7.7
 */
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "core/date.h"
#include "game/history.h"
#include "platform/plat.h"

using namespace fs;

/** (the browser's storage with nothing of the history in it) */
static void clearStore() {
  for (const char* key : {"fahrsim.places", "fahrsim.ways", "fahrsim.favourites"}) plat::localStorage().removeItem(key);
}

int main() {
  // places looked for: the newest first, a place picked again moves up, only the last ones are kept
  {
    clearStore();
    savePlace({.name = "Marktplatz 1, Baunatal", .x = 10, .z = 20, .yaw = 0});
    savePlace({.name = "Königsplatz, Kassel", .x = 6700, .z = -7056, .yaw = 1});
    savePlace({.name = "Marktplatz 1, Baunatal", .x = 11, .z = 21, .yaw = 0});
    std::vector<PlaceRecord> places = loadPlaces();
    assert(places.size() == 2 && places[0].name == "Marktplatz 1, Baunatal" && places[1].name == "Königsplatz, Kassel");
    for (int i = 0; i < 20; i++) savePlace({.name = "Place " + std::to_string(i), .x = i * 100.0, .z = 0, .yaw = 0});
    assert(loadPlaces().size() == PLACES_KEPT);
    assert(loadPlaces()[0].name == "Place 19");
  }

  // ways driven: their places in order, the same way again moves up, broken entries are left out
  {
    clearStore();
    const std::vector<NamedPlace> a = {{.name = "A", .x = 0, .z = 0, .yaw = 0}, {.name = "B", .x = 500, .z = 0, .yaw = 0}};
    saveWay(a, false);
    saveWay({{.name = "C", .x = 9, .z = 9, .yaw = 0}}, true);
    saveWay(a, true);
    const std::vector<WayRecord> ways = loadWays();
    assert(ways.size() == 2);
    assert(ways[0].stops.size() == 2 && ways[0].stops[0].name == "A" && ways[0].stops[1].name == "B");
    assert(ways[0].self == true);
    // (a broken one put before them, as the original's test does with what is stored)
    const std::string stored = *plat::localStorage().getItem("fahrsim.ways");
    plat::localStorage().setItem("fahrsim.ways", R"([{"stops":[{"name":"X","x":"no","z":0}],"self":false,"date":""},)" + stored.substr(1));
    assert(loadWays().size() == 2);   // the broken one is not offered
    for (int i = 0; i < 15; i++) saveWay({{.name = "W" + std::to_string(i), .x = static_cast<double>(i), .z = 0, .yaw = 0}}, false);
    assert(loadWays().size() == WAYS_KEPT);
    // (no place, no way: nothing is stored)
    assert(saveWay({}, true).size() == WAYS_KEPT);
  }

  // favourite places: added, a place near one replaces it, taken out again
  {
    clearStore();
    saveFavourite({.name = "Home", .x = 100, .z = 200, .yaw = 0});
    saveFavourite({.name = "Work", .x = 5000, .z = -300, .yaw = 1});
    saveFavourite({.name = "Home (door)", .x = 105, .z = 204, .yaw = 0});
    std::vector<FavouriteRecord> favs = loadFavourites();
    assert(favs.size() == 2 && favs[0].name == "Home (door)" && favs[1].name == "Work");
    removeFavourite(5000, -300);
    favs = loadFavourites();
    assert(favs.size() == 1 && favs[0].name == "Home (door)");
  }

  // what is stored is what the original stores: rounded to a tenth of a metre and a thousandth of a turn, with the moment
  {
    clearStore();
    const double now = utcMs(2026, 10, 24, 8, 5, 9, 42);
    savePlace({.name = "Am \"Stadtpark\" 5", .x = 12.3456, .z = -7.05, .yaw = 1.23456}, now);
    assert(*plat::localStorage().getItem("fahrsim.places") == R"([{"name":"Am \"Stadtpark\" 5","x":12.3,"z":-7,"yaw":1.235,"date":"2026-11-24T08:05:09.042Z"}])");
    saveWay({{.name = "A", .x = 1.26, .z = 2, .yaw = 0}}, true, now);
    assert(*plat::localStorage().getItem("fahrsim.ways") == R"([{"stops":[{"name":"A","x":1.3,"z":2,"yaw":0}],"self":true,"date":"2026-11-24T08:05:09.042Z"}])");
    saveFavourite({.name = "Home", .x = 100.04, .z = 200.05, .yaw = -0.0004});
    assert(*plat::localStorage().getItem("fahrsim.favourites") == R"([{"name":"Home","x":100,"z":200.1,"yaw":0}])");
    clearStore();
  }

  std::printf("game_history_test: ok\n");
  return 0;
}

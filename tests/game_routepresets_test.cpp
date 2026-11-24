// LINK: game sim render i18n audio input engine world core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The set routes (game/routepresets.h): the watch over the route (leaving
 * it, driving it the other way), the mode that starts one from the menu –
 * and, on the real map, every route planned as a round trip of sensible
 * length through what it is about.
 *
 * Origin: tests/game/routepresets.test.ts @ v1.7.7
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "game/routepresets.h"
#include "platform/plat.h"
#include "world/netfile.h"
#include "world/routegraph.h"
#include "world/types.h"

using namespace fs;

/** the network of the whole real map, if the map is here (MAP_DIR, or the original's public/map beside this repository) */
static std::optional<Bytes> realNet() {
  std::vector<std::string> dirs;
  if (const char* dir = std::getenv("MAP_DIR")) dirs.push_back(dir);
  dirs.push_back("../driving-simulator/public/map");
  dirs.push_back("/home/user/driving-simulator/public/map");
  for (const std::string& dir : dirs) {
    std::ifstream f(dir + "/net.bin.gz", std::ios::binary);
    if (!f) continue;
    Bytes gz((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return plat::gunzip(gz);
  }
  return std::nullopt;
}

int main() {
  // the watch: on the route nothing; off it a fault once per leaving; against it a bigger one
  {
    // the route: ways 10 (start), 12, 14, 16 – each road's other direction is way ^ 1
    RouteWatch w(10, {12, 14, 16});
    double odo = 0;
    const auto drive = [&](int way, double metres) {
      std::vector<std::string> out;
      for (double m = 0; m < metres; m += 5) {
        odo += 5;
        const std::optional<RouteFault> f = w.update(way, odo);
        if (f) out.push_back(*f);
      }
      return out;
    };
    const std::vector<std::string> none;
    assert(drive(10, 100) == none);
    assert(drive(12, 100) == none);
    // through a junction (no road) and a few metres onto another road: not yet leaving
    assert(drive(-1, 20) == none);
    assert(drive(40, 30) == none);
    assert(drive(14, 50) == none);   // back on it in time
    // off it for good: one fault, however long
    assert(drive(40, 300) == std::vector<std::string>{"routeLeft"});
    assert(drive(42, 300) == none);   // still off: said once
    assert(drive(14, 30) == none);
    // turned round: along the route the wrong way
    assert(drive(15, 20) == none);   // turning takes a few metres
    assert(drive(15, 200) == std::vector<std::string>{"routeReverse"});
    assert(drive(14, 10) == none);
    assert(drive(50, 100) == std::vector<std::string>{"routeLeft"});   // a new leaving after being back on it
    assert(w.has(12) && !w.has(13));
  }

  // a set route is started from the menu by its mode
  {
    std::set<std::string> ids;
    for (const RoutePreset& p : ROUTE_PRESETS) {
      assert(presetOfMode(routeMode(p.id)) == &p);
      assert(p.points.size() >= 3);   // a round trip through several places
      ids.insert(p.id);
    }
    assert(presetOfMode("free") == nullptr);
    assert(presetOfMode("route:nowhere") == nullptr);
    assert(ids.size() == ROUTE_PRESETS.size() && ROUTE_PRESETS.size() == 6);
  }

  // on the real map: every set route is a round trip of sensible length through what it is about
  if (const std::optional<Bytes> file = realNet()) {
    const NetData net = unpackNet(*file);
    const RouteGraph g = routeGraphOfNet(net);
    const std::vector<uint8_t> main = mainWays(g);
    for (const RoutePreset& p : ROUTE_PRESETS) {
      const std::vector<int> first = snapPoint(g, p.points[0], main);
      assert(!first.empty());   // its first place lies on a road
      const int start = first[0];
      const std::optional<PresetPlan> plan = planPreset(g, start, p, main);
      assert(plan);
      // a drive of a driving test or a bit more: 10 to 35 km
      assert(plan->metres > 10000 && plan->metres < 35000);
      assert(plan->at.size() == p.points.size() + 1);   // every place passed, and the first again
      // the ways follow each other: each one begins where the one before ends
      size_t prev = static_cast<size_t>(start);
      for (int way : plan->ways) {
        const size_t w = static_cast<size_t>(way);
        bool joined = false;
        for (size_t k = g.first[prev]; k < g.first[prev + 1]; k++)
          if (g.to[k] == w) joined = true;
        assert(joined);
        assert((g.kind[w] & RG_NO_ENTRY) == 0);   // no private roads
        prev = w;
      }
      // back to where it began
      assert(plan->ways.back() >> 1 == start >> 1);   // a round trip
      double motorway = 0;
      for (int w : plan->ways)
        if (g.cls[static_cast<size_t>(w)] == MOTORWAY) motorway += static_cast<double>(g.len[static_cast<size_t>(w)]);
      if (p.id == "motorway") assert(motorway > 3000);   // on the A 49
      if (p.id == "baunatal" || p.id == "kasselCentre" || p.id == "hills") assert(motorway == 0);
      const std::vector<float> line = routeLine(g, start, plan->ways);
      assert(line.size() >= 4 * (plan->ways.size() + 1));
      std::printf("  %s: %.0f m, %zu ways\n", p.id.c_str(), plan->metres, plan->ways.size());
    }
  } else std::printf("  (no map – MAP_DIR or ../driving-simulator/public/map: the case on the real map is left out)\n");

  std::printf("game_routepresets_test: ok\n");
  return 0;
}

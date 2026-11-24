// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/routepresets.ts @ v1.7.7
/**
 * Set routes: a handful of drives through Baunatal and Kassel laid out like
 * the routes of a practical driving test – through the districts, over the
 * country roads, into Kassel's centre, onto the A 49 – that the player
 * starts from the menu or the map and then has to drive as laid out.
 *
 * A route is given as places to pass in order (world metres, the middle of a
 * district or a point on a road) and is a round trip: from the first place
 * through the others back to the first.  Each place is put onto the nearest
 * road of the kind it asks for, and the legs between them are the quickest
 * ways on the road network of the whole map (world/routegraph.h) – so the
 * route follows the map as it is and needs no list of streets.
 *
 * While it runs, the game shows the way as on a tour (game/nav.h) and a
 * RouteWatch looks at the road the vehicle is on: leaving the route (more
 * than a junction's length on a road that is not part of it) is a fault, and
 * driving along it the other way – having turned round – a bigger one.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "world/routegraph.h"

namespace fs {

/** A place a set route passes: where, and what kind of road to put it on. */
struct RoutePoint {
  double x = 0;
  double z = 0;
  /** 'street' (default): a road of a town or between villages; 'motorway': the motorway itself; 'rural': a road outside the towns */
  std::string road = "street";
};

/** A set route: its name and description are texts ("routes.<id>.name", "routes.<id>.desc"). */
struct RoutePreset {
  std::string id;
  /** the places in order; the route ends where it began */
  std::vector<RoutePoint> points;
};

/**
 * The set routes.  (Places by the names of the map's districts; the
 * motorway points lie on the A 49 between Baunatal and Kassel.)
 */
inline const std::vector<RoutePreset> ROUTE_PRESETS = {
    // Baunatal's districts: Stadtmitte, Altenbauna, Altenritte, Großenritte, Kirchbauna – the town's junctions, its roundabouts and lights
    {
        .id = "baunatal",
        .points =
            {
                {.x = 744, .z = -272},
                {.x = 1046, .z = -793},
                {.x = -95, .z = -988},
                {.x = -1160, .z = 554},
                {.x = 1717, .z = 817},
            },
    },
    // out of town: Hertingshausen, Guntershausen, Dittershausen, Rengershausen – country roads between villages
    {
        .id = "country",
        .points =
            {
                {.x = 744, .z = -272},
                {.x = 1279, .z = 2512},
                {.x = 4387, .z = 2117},
                {.x = 5123, .z = 41},
                {.x = 3472, .z = -418},
            },
    },
    // into Kassel's south: Rengershausen, Niederzwehren, Oberzwehren, Nordshausen, back through Altenritte
    {
        .id = "kasselSouth",
        .points =
            {
                {.x = 744, .z = -272},
                {.x = 3472, .z = -418},
                {.x = 4568, .z = -3746},
                {.x = 3277, .z = -2382},
                {.x = 2155, .z = -3129},
                {.x = -95, .z = -988},
            },
    },
    // Kassel's centre: Wehlheiden, Vorderer Westen, Mitte, Wesertor, Bettenhausen, Südstadt – lights, tram tracks, several lanes
    {
        .id = "kasselCentre",
        .points =
            {
                {.x = 4265, .z = -5927},
                {.x = 4420, .z = -7169},
                {.x = 6694, .z = -7060},
                {.x = 7947, .z = -7963},
                {.x = 8864, .z = -6025},
                {.x = 5702, .z = -5903},
            },
    },
    // the motorway: from Baunatal onto the A 49 towards Kassel, off into Niederzwehren, back over Rengershausen
    {
        .id = "motorway",
        .points =
            {
                {.x = 744, .z = -272},
                {.x = 2514, .z = -1598, .road = "motorway"},
                {.x = 4568, .z = -3746},
                {.x = 3472, .z = -418},
            },
    },
    // into the hills west of Baunatal: Großenritte, Elgershausen, Hoof, Altenritte
    {
        .id = "hills",
        .points =
            {
                {.x = -1160, .z = 554},
                {.x = -1787, .z = -2501},
                {.x = -3558, .z = -3453},
                {.x = -95, .z = -988},
            },
    },
};

/** The mode a drive is started with to run a set route at once ("route:<id>"). */
inline std::string routeMode(const std::string& id) { return "route:" + id; }

/** The set route a mode asks for (null: the mode is none). */
const RoutePreset* presetOfMode(const std::string& mode);

/** A set route planned on the road network. */
struct PresetPlan {
  /** the ways to drive along, after the one it starts on (as a tour's) */
  std::vector<int> ways;
  /** its length (m) */
  double metres = 0;
  /** the way each place of the route was put on */
  std::vector<int> at;
};

/**
 * The roads a place may be put on: the nearest ones of its kind (both
 * directions of a road where it can be driven both ways), at most `maxDist`
 * from it.  Only ways of the network one can drive around in (`main`).
 */
std::vector<int> snapPoint(const RouteGraph& g, const RoutePoint& p, const std::vector<uint8_t>& main, double maxDist = 600);

/**
 * Plan a set route from the way `start` (the road the vehicle stands on):
 * to its first place, through the others, and back to the first.  Null if
 * a place lies off the roads or a leg cannot be driven.
 * (`main`: the ways of the network one can drive around in – mainWays(g) unless given.)
 */
std::optional<PresetPlan> planPreset(const RouteGraph& g, int start, const RoutePreset& preset);
std::optional<PresetPlan> planPreset(const RouteGraph& g, int start, const RoutePreset& preset, const std::vector<uint8_t>& main);

/**
 * The line of a route for the map: x, z, x, z, … along the ways in order.
 * `course(e)`: the course of road e from its junction a to b (x, z, …) –
 * without it (or where it gives none), each way's ends joined straight.
 */
std::vector<float> routeLine(const RouteGraph& g, int start, const std::vector<int>& ways, const std::function<std::vector<double>(int e)>& course = nullptr);

/** A fault of the set route: off it, or along it the wrong way ('routeLeft' | 'routeReverse'). */
using RouteFault = std::string;

/**
 * Watches whether the set route is kept.  Fed with the way the vehicle is
 * on and its odometer; says a fault once each time the route is left (or
 * driven against), and is ready again once the vehicle is back on it.
 */
class RouteWatch {
 public:
  RouteWatch(int start, const std::vector<int>& ways);

  /** Is this way part of the route (in this direction)? */
  bool has(int way) const { return on.count(way) > 0; }

  /** The way the vehicle is on now (-1: none – in a junction, off the roads) and how far it has driven in all (m). */
  std::optional<RouteFault> update(int way, double odometer);

 private:
  std::unordered_set<int> on;
  double offSince = -1;
  double backSince = -1;
  bool leftSaid = false;
  bool backSaid = false;
};

}  // namespace fs

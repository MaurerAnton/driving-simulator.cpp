// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Where a bus stop's furniture stands (src/render/stopspot.ts).  A player
 * found stops "in the middle of the road": poles mapped on the road's centre
 * line, between the carriageways of a dual road, among cars parked on the
 * pavement.  Here sample stops stand against a handful of streets with the
 * cross-sections of the map (pavements per side, parking strips on the road
 * and on the pavement, verges, cycle tracks, one-way streets, a dual road),
 * and nothing of their furniture – the sign, the shelter, the bench, the
 * board, the people waiting – may stand on a carriageway or a parking strip.
 *
 * Origin: tests/world/stopspot.test.ts @ v1.7.7
 */
// LINK: render engine world core platform geo
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/geom.h"
#include "render/stopspot.h"
#include "world/types.h"
#include "world/world.h"
#include "world/xsection.h"

using namespace fs;

namespace {

constexpr double Y = 100;

struct Street {
  double at = 0;
  bool south = false;
  std::function<void(EdgeJson&)> more;
};

std::shared_ptr<const RoadSideJson> side(std::function<void(RoadSideJson&)> f) {
  auto s = std::make_shared<RoadSideJson>();
  f(*s);
  return s;
}

/** Streets running north (a in the south, b in the north; `south`: the other way), each on its own, 200 m long, at x = `at`. */
std::unique_ptr<World> streets(const std::vector<Street>& list) {
  WorldJson json;
  json.version = 2;
  json.meta.name = "t";
  json.meta.minX = -2000;
  json.meta.maxX = 2000;
  json.meta.minZ = -500;
  json.meta.maxZ = 500;
  json.meta.tileSize = 500;
  for (const Street& s : list) {
    const int a = static_cast<int>(json.nodes.size());
    const double z0 = s.south ? -100 : 100;
    for (double z : {z0, -z0}) {
      NodeJson n;
      n.x = s.at;
      n.y = Y;
      n.z = z;
      n.t = CTRL_NONE;
      ArmJson arm;
      arm.e = static_cast<int>(json.edges.size());
      arm.r = ROLE_NONE;
      n.arms.push_back(arm);
      json.nodes.push_back(n);
    }
    EdgeJson e;
    e.a = a;
    e.b = a + 1;
    e.c = SECONDARY;
    for (int k = 0; k <= 10; k++) e.p.insert(e.p.end(), {s.at, Y, z0 - (z0 / 5) * k});
    e.lf = 1;
    e.lb = 1;
    e.lw = 3.25;
    e.w = 6.5;
    e.mf = 50;
    e.mb = 50;
    e.f = EF_URBAN;
    e.sl = 2.5;
    e.sr = 2.5;
    if (s.more) s.more(e);
    json.edges.push_back(e);
  }
  computeAllCuts(json);
  return std::make_unique<World>(std::move(json));
}

struct Case {
  std::string what;
  double x;
  int flags;
  /** the side it must end up on: +x east or −x west; and whether it may stay where it was mapped */
  bool east;
  bool stays = false;
};

}  // namespace

int main() {
  // (a street running north: left of a→b is west, −x; right is east, +x)
  const std::unique_ptr<World> worldPtr = streets({
      // 0: two-way, pavements of 2.5 m
      {0, false, nullptr},
      // 100: two-way, a parking lane on the road on the right, cars on the pavement on the left
      {100, false, [](EdgeJson& e) {
         e.w = 8.5;
         e.sl = 4.5;
         e.xr = side([](RoadSideJson& r) { r.p = PARK_LANE; r.po = ORIENT_PARALLEL; });
         e.xl = side([](RoadSideJson& r) { r.p = PARK_ON_KERB; r.po = ORIENT_PARALLEL; });
       }},
      // 200: one-way north
      {200, false, [](EdgeJson& e) { e.lf = 1; e.lb = 0; e.w = 4; e.lw = 3.5; }},
      // 300: one-way south
      {300, false, [](EdgeJson& e) { e.lf = 0; e.lb = 1; e.w = 4; e.lw = 3.5; }},
      // 400 and 412: a dual road, the northbound carriageway in the east, the southbound one in the west (each drawn
      // the way it is driven), grass between – on the left of each
      {412, false, [](EdgeJson& e) { e.lf = 2; e.lb = 0; e.w = 7; e.lw = 3.5; e.f = EF_URBAN | EF_DUAL; e.md = MEDIAN_GRASS; e.sl = 0; }},
      {400, true, [](EdgeJson& e) { e.lf = 2; e.lb = 0; e.w = 7; e.lw = 3.5; e.f = EF_URBAN | EF_DUAL; e.md = MEDIAN_GRASS; e.sl = 0; }},
      // 500: a verge and a cycle track on the right before the pavement, half-on-kerb parking on the left
      {500, false, [](EdgeJson& e) {
         e.sr = 5;
         e.xr = side([](RoadSideJson& r) { r.v = 1; r.c = CYCLE_TRACK; r.cw = 1.6; });
         e.w = 7.5;
         e.sl = 3;
         e.xl = side([](RoadSideJson& r) { r.p = PARK_HALF_ON_KERB; r.po = ORIENT_PARALLEL; });
       }},
      // 600: diagonal parking on the road on both sides, a wide road
      {600, false, [](EdgeJson& e) {
         e.w = 17.1;
         e.xl = side([](RoadSideJson& r) { r.p = PARK_LANE; r.po = ORIENT_DIAGONAL; });
         e.xr = side([](RoadSideJson& r) { r.p = PARK_LANE; r.po = ORIENT_DIAGONAL; });
       }},
  });
  World& world = *worldPtr;
  const int BUS = STOP_FLAG_BUS;

  const std::vector<Case> CASES = {
      {"on the centre line of a two-way street (a bus_stop node on the way)", 0, BUS | STOP_FLAG_ON_ROAD, true},
      {"a stop position on the centre line", 0.4, BUS, true},
      {"on the carriageway near the western kerb", -2.6, BUS | STOP_FLAG_SHELTER, false},
      {"at the eastern kerb, on the road", 3.2, BUS | STOP_FLAG_SHELTER | STOP_FLAG_BENCH, true},
      {"on the eastern pavement", 4.5, BUS | STOP_FLAG_SHELTER, true, true},
      {"on the parking lane of the road", 104, BUS | STOP_FLAG_SHELTER, true},
      {"among the cars parked on the western pavement", 95, BUS | STOP_FLAG_SHELTER, false},
      {"on the centre line of a one-way street north", 200, BUS | STOP_FLAG_ON_ROAD, true},
      {"on the centre line of a one-way street south", 300, BUS | STOP_FLAG_ON_ROAD, false},
      {"on the grass between the carriageways of a dual road", 406.5, BUS | STOP_FLAG_SHELTER, true},
      {"at the median kerb of the southbound carriageway", 403.4, BUS | STOP_FLAG_SHELTER, false},
      {"on the centre line of the southbound carriageway", 400, BUS | STOP_FLAG_ON_ROAD, false},
      {"at the kerb before a verge and a cycle track", 504, BUS | STOP_FLAG_SHELTER, true},
      {"among cars parked half on the kerb", 496, BUS | STOP_FLAG_BENCH, false},
      {"in the diagonal parking on a wide road", 607, BUS | STOP_FLAG_SHELTER, true},
  };

  /** the footprint of the furniture as TransitScene lays it out from the spot: sign, shelter, bench, board, bin, people */
  std::vector<std::pair<double, double>> FOOTPRINT;
  for (double along : {-2.6, -1.8, -0.9, 0.0, 0.9, 2.6, 4.4})
    for (double back : {0.0, 0.4, 0.6, 1.2, 1.6, 2.3}) FOOTPRINT.emplace_back(along, back);

  PolyProjection proj;
  for (const Case& c : CASES) {
    // ---- a stop …: its furniture beside the carriageway
    const double z = 13;
    const StopSpot s = stopSpot(world, c.x, z, Y, c.flags);
    if (!s.edge) std::printf("%s: no road\n", c.what.c_str());
    assert(s.edge);   // it finds its road
    if (c.stays) assert(!s.moved && s.x == c.x && s.z == z);   // a pole on the pavement stays where it was mapped
    // facing the road: forward = (−sin yaw, −cos yaw) points west from the east side, east from the west side
    const double fx = -std::sin(s.yaw);
    const double fz = -std::cos(s.yaw);
    assert(std::abs(fz) < 1e-6);   // it faces across the road
    if ((fx < 0) != c.east) std::printf("%s: on the wrong side\n", c.what.c_str());
    assert((fx < 0) == c.east);
    const double ax = std::cos(s.yaw);
    const double az = -std::sin(s.yaw);
    const Edge& e = *s.edge;
    for (const auto& [along, back] : FOOTPRINT) {
      const double px = s.x + ax * along - fx * back;
      const double pz = s.z + az * along - fz * back;
      if (inTheWay(world, px, pz, Y + 2)) std::printf("%s: (%g, %g) at x %.2f stands on the road\n", c.what.c_str(), along, back, px);
      assert(!inTheWay(world, px, pz, Y + 2));
      projectOnPolyline(e.pl, px, pz, proj);
      const int sd = proj.off >= 0 ? 1 : -1;
      assert(std::abs(proj.off) >= clearOf(e, sd) + KERB_GAP - 1e-6);
      // … and not on the other carriageway of a dual road either
      for (size_t i = 0; i < world.edges.size(); i++) {
        const Edge* other = world.edges[static_cast<int64_t>(i)];
        if (!other) continue;
        projectOnPolyline(other->pl, px, pz, proj);
        assert(std::abs(proj.off) > other->width / 2);
      }
    }
  }

  // ---- the clear line: kerb, parked cars on the pavement, a cycle track with a pavement beyond
  {
    const Edge& plain = *world.edges[0];
    const Edge& parked = *world.edges[1];
    const Edge& track = *world.edges[6];
    assert(clearOf(plain, 1) == 3.25);
    assert(clearOf(parked, 1) == 4.25 + 2);   // cars on the pavement: behind them
    assert(clearOf(parked, -1) == 4.25);   // a parking lane is part of the carriageway
    assert(clearOf(track, -1) == 3.75 + 1 + 1.6);   // behind verge and cycle track
    assert(clearOf(track, 1) == 3.75 + 1);   // half on the kerb: behind the cars
  }

  // ---- a pole far from any road keeps its place
  {
    const StopSpot s = stopSpot(world, 50, 0, Y, BUS);
    assert(s.edge == nullptr);
    assert(!s.moved);
  }

  std::printf("stopspot: all checks passed\n");
  return 0;
}

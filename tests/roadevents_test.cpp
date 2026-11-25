// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim world core platform geo game
// Origin: tests/sim/roadevents.test.ts @ v1.7.7
/**
 * Roadworks and demonstrations (src/sim/roadevents): which roads have a
 * work zone on a day and where on them; the temporary light and the signs
 * 208/308 that let both directions through a narrowing in turns; a lane
 * that ends; what the rule monitor makes of the player there – and the
 * marches: when they are, which way they walk, what is closed for them,
 * and that the traffic and the planned ways keep out.
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/date.h"
#include "core/geom.h"
#include "sim/autodriver.h"
#include "sim/ground.h"
#include "sim/kasselclock.h"
#include "sim/rail.h"
#include "sim/roadevents.h"
#include "sim/rules.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "sim/vehicle/index.h"
#include "tests/sim_roads.h"
#include "tests/town.h"
#include "tests/vehicle_harness.h"
#include "world/lanes.h"
#include "world/routegraph.h"

using namespace fs;
using fs::harness::ok;
using fs::harness::test;

namespace {

const double DT = 1.0 / 20;
/** a Wednesday morning, and a Saturday at noon (ms since 1970) */
const double WEDNESDAY = utcMs(2026, 10, 18, 9);
const double SATURDAY = utcMs(2026, 10, 21, 11);

struct RoadSpec {
  int a = 0;
  int b = 0;
  std::optional<int> cls;
  std::optional<int> lf;
  std::optional<int> lb;
  std::optional<double> lw;
  std::optional<bool> urban;
  std::optional<double> max;
  int flags = 0;
  std::string name;
};

struct REvNet {
  WorldJson json;
  std::unique_ptr<World> world;
  std::unique_ptr<LaneGraph> lanes;
};

/** Straight roads between junctions at these points (y = 100), built as the map build hands them to the game. */
REvNet makeNet(const std::vector<std::pair<double, double>>& points, const std::vector<RoadSpec>& roads, const std::vector<CrossingJson>& crossings = {}) {
  std::vector<NodeJson> nodes;
  for (const auto& [x, z] : points) {
    NodeJson n;
    n.x = x;
    n.y = 100;
    n.z = z;
    n.t = CTRL_NONE;
    nodes.push_back(n);
  }
  std::vector<EdgeJson> edges;
  for (size_t i = 0; i < roads.size(); i++) {
    const RoadSpec& r = roads[i];
    EdgeJson e;
    e.a = r.a;
    e.b = r.b;
    e.c = r.cls.value_or(RESIDENTIAL);
    e.n = r.name.empty() ? "Road " + std::to_string(i) : r.name;
    e.p = roads::straightPoints(nodes[r.a], nodes[r.b]);
    e.lf = r.lf.value_or(1);
    e.lb = r.lb.value_or(1);
    const double lw = r.lw.value_or(3);
    e.lw = lw;
    e.w = (e.lf + e.lb) * lw;
    e.mf = r.max.value_or(50);
    e.mb = r.max.value_or(50);
    e.f = (r.urban.value_or(true) ? static_cast<int>(EF_URBAN) : 0) | r.flags;
    e.ca = 0;
    e.cb = 0;
    edges.push_back(e);
  }
  roads::linkArms(nodes, edges, [](size_t arms) { return arms > 2 ? ROLE_RBL : ROLE_NONE; }, true);
  REvNet net;
  WorldJson& json = net.json;
  json.version = 1;
  json.meta.name = "Test roads";
  json.meta.minX = -3000;
  json.meta.maxX = 14000;
  json.meta.minZ = -3000;
  json.meta.maxZ = 14000;
  json.meta.tileSize = 500;
  json.nodes = nodes;
  json.edges = edges;
  json.crossings = crossings;
  computeAllCuts(json);
  net.world = std::make_unique<World>(json);
  net.lanes = std::make_unique<LaneGraph>(*net.world);
  return net;
}

/** Four roads round a block of 600 × 150 m: the traffic goes round and round, both ways.  The first road (z = 0, x 0 → 600) is the one with the work. */
REvNet loop(RoadSpec road = {}) {
  std::vector<RoadSpec> roads;
  for (int i = 0; i < 4; i++) {
    RoadSpec r = road;
    r.a = i;
    r.b = (i + 1) % 4;
    roads.push_back(r);
  }
  return makeNet({{0, 0}, {600, 0}, {600, 150}, {0, 150}}, roads);
}

ZoneSpec spec(int side, const char* control, int lane, double z0, double z1, double taper, double limit, double warn, std::array<double, 2> green, double phase, int seed) {
  ZoneSpec s;
  s.side = side;
  s.control = control;
  s.lane = lane;
  s.z0 = z0;
  s.z1 = z1;
  s.taper = taper;
  s.limit = limit;
  s.warn = warn;
  s.green = green;
  s.phase = phase;
  s.seed = seed;
  return s;
}

const PlayerState WATCHER{.x = 300, .y = 100, .z = 420, .yaw = 0, .speed = 0, .length = 4.3, .width = 1.8};

/** The traffic of a network with road events of the day (a weekday morning: no march) – and the zone to test in place of the day's own. */
struct Setup {
  SignalSystem signals;
  Traffic traffic;
  RoadEvents ev;
  PlayerState player = WATCHER;
  WorkZone* zone = nullptr;
  Setup(REvNet& net, const std::optional<ZoneSpec>& zs, double now = WEDNESDAY)
      : signals(net.world->signals), traffic(*net.world, *net.lanes, signals), ev(*net.world, *net.lanes, signals) {
    traffic.targetCount = 0;
    traffic.spawnRadius = 5000;
    ev.now = [now] { return now; };
    traffic.roadEvents = &ev;
    // (the player stands off the roads, watching)
    ev.step(0, {}, player);
    ev.zones.clear();
    ev.demos.clear();
    if (zs) {
      Edge& e = *net.world->edges[0];
      std::shared_ptr<WorkZone> z = makeZone(e, *net.lanes, *zs);
      zone = z.get();
      ev.zones.set(e.id, z);
    }
  }
};

/** Cars on every lane of the roads but the first (they come round to it). */
void fill(REvNet& net, Traffic& traffic, double every = 50) {
  for (Edge* e : net.world->allEdges) {
    if (e->id == 0) continue;
    const LaneGraph::EdgeLanes& el = *net.lanes->edgeLanes[e->id];
    std::vector<Lane*> all = el.fwd;
    all.insert(all.end(), el.back.begin(), el.back.end());
    for (Lane* lane : all)
      for (double s = 15; s < lane->len - 8; s += every) traffic.addCar(*lane, s);
  }
}

/** A lane's distance of a station. */
double along(const Lane& lane, double s) { return (s - lane.s0) * lane.dir; }

/** Across the road: how far left of a → b the car is drawn, and its station. */
std::pair<double, double> where(const Edge& e, const TrafficCar& car) {
  PolyProjection proj;
  projectOnPolyline(e.pl, car.x, car.z, proj);
  return {proj.s, proj.off};
}

std::vector<WorkZone> zonesOf(World& world, LaneGraph& lanes, int d) {
  std::vector<WorkZone> out;
  for (Edge* e : world.allEdges) {
    std::shared_ptr<WorkZone> z = worksOn(*e, d, lanes);
    if (z) out.push_back(*z);
  }
  return out;
}

// ------------------------------------------------------------------------------------ the choice

void byTheDay() {
  test("roadworks: each road decides by the day – the same zones all day, others on other days, more on the main roads outside towns");
  // a town of streets, and the country roads between villages
  TownOptions to;
  to.cols = 10;
  to.rows = 10;
  to.block = 180;
  Town town = makeTown(to);
  std::vector<std::pair<double, double>> country;
  std::vector<RoadSpec> roads;
  for (int j = 0; j < 6; j++)
    for (int i = 0; i < 6; i++) country.push_back({i * 1000.0, j * 1000.0});
  for (int j = 0; j < 6; j++) {
    for (int i = 0; i < 6; i++) {
      if (i < 5) roads.push_back(RoadSpec{.a = j * 6 + i, .b = j * 6 + i + 1, .cls = SECONDARY, .urban = false, .max = 100});
      if (j < 5) roads.push_back(RoadSpec{.a = j * 6 + i, .b = (j + 1) * 6 + i, .cls = SECONDARY, .urban = false, .max = 100});
    }
  }
  REvNet land = makeNet(country, roads);
  const int day = 2026 * 372 + 10 * 31 + 18;
  // the same day: the very same zones
  const auto a = zonesOf(*land.world, *land.lanes, day);
  const auto b = zonesOf(*land.world, *land.lanes, day);
  FS_OK(a.size() == b.size());
  for (size_t i = 0; i < a.size(); i++) FS_OK(a[i].id == b[i].id && a[i].z0 == b[i].z0 && a[i].z1 == b[i].z1 && a[i].side == b[i].side && a[i].control == b[i].control);
  // over a month: a few each day, other ones each day, more per kilometre outside on the main roads than on the streets in town
  int landCount = 0;
  int townCount = 0;
  int again = 0;
  std::set<int> prev;
  double landKm = 0;
  double townKm = 0;
  for (Edge* e : land.world->allEdges) landKm += (e->len - e->cutA - e->cutB) / 1000;
  for (Edge* e : town.world->allEdges) townKm += (e->len - e->cutA - e->cutB) / 1000;
  for (int d = day; d < day + 30; d++) {
    const auto zl = zonesOf(*land.world, *land.lanes, d);
    landCount += static_cast<int>(zl.size());
    townCount += static_cast<int>(zonesOf(*town.world, *town.lanes, d).size());
    for (const WorkZone& z : zl)
      if (prev.count(z.id)) again++;
    prev.clear();
    for (const WorkZone& z : zl) prev.insert(z.id);
  }
  const double landRate = landCount / 30.0 / landKm;
  const double townRate = townCount / 30.0 / townKm;
  ok(landCount / 30.0 > 5, "several zones a day on 60 km of country roads (" + toFixed(landCount / 30.0, 1) + ")");
  ok(townCount > 3, "now and then one in the streets of a small town (" + std::to_string(townCount) + " in 30 days)");
  ok(landRate > townRate * 2, "outside on the main roads more often (" + toFixed(landRate, 3) + " against " + toFixed(townRate, 3) + " per km)");
  // (each day by itself: a road has a zone again the next day no more often than any road has one at all)
  ok(again < landCount * 0.6, "other roads the next day (" + std::to_string(again) + " of " + std::to_string(landCount) + " again)");
}

void whereTheyAre() {
  test("roadworks: where they are – between the junctions with room for the signs, never on a motorway, a roundabout or over a crossing; the kind fits the lanes");
  std::vector<std::pair<double, double>> pts;
  std::vector<RoadSpec> roads;
  std::vector<CrossingJson> xings;
  const auto add = [&](RoadSpec r, double len, int row, bool crossing = false) {
    const int a = static_cast<int>(pts.size());
    pts.push_back({0, row * 300.0});
    pts.push_back({len, row * 300.0});
    r.a = a;
    r.b = a + 1;
    roads.push_back(r);
    if (crossing) {
      CrossingJson x;
      x.e = static_cast<int>(roads.size()) - 1;
      x.s = len / 2;
      x.k = XING_ZEBRA;
      x.w = 4;
      xings.push_back(x);
    }
  };
  add(RoadSpec{.cls = RESIDENTIAL}, 1000, 0);
  add(RoadSpec{.cls = SECONDARY, .urban = false, .max = 100}, 1500, 1);
  add(RoadSpec{.cls = PRIMARY, .lf = 2, .lb = 2, .lw = 3.25}, 600, 2);
  add(RoadSpec{.cls = TRUNK, .lf = 2, .lb = 0, .lw = 3.5, .urban = false, .max = 0}, 1500, 3);
  add(RoadSpec{.cls = MOTORWAY, .lf = 2, .lb = 0, .lw = 3.75, .urban = false, .max = 0}, 2000, 4);
  add(RoadSpec{.cls = SECONDARY, .urban = false, .max = 100, .flags = EF_ROUNDABOUT}, 800, 5);
  add(RoadSpec{.cls = RESIDENTIAL, .lf = 1, .lb = 0, .lw = 3.5}, 400, 6);
  add(RoadSpec{.cls = SERVICE}, 400, 7);
  add(RoadSpec{.cls = RESIDENTIAL}, 400, 8, true);
  REvNet net = makeNet(pts, roads, xings);
  std::map<int, std::set<std::string>> seen;
  const int day0 = 2026 * 372 + 10 * 31 + 1;
  for (int d = day0; d < day0 + 600; d++) {
    for (Edge* e : net.world->allEdges) {
      std::shared_ptr<WorkZone> zp = worksOn(*e, d, *net.lanes);
      if (!zp) continue;
      const WorkZone& z = *zp;
      seen[e->id].insert(z.control);
      const bool urban = (e->flags & EF_URBAN) != 0;
      const double approach = urban ? 30 : 110;
      // room before and after for the signs and to stop
      ok(z.z0 - z.taper >= e->cutA + approach - 1e-6 && z.z1 + z.taper <= e->len - e->cutB - approach + 1e-6, "room round the zone on road " + std::to_string(e->id));
      FS_OK(z.z1 - z.z0 >= 14);
      // the line to wait at lies on the road, before the zone, for both directions
      for (const int dir : {1, -1}) {
        const double w = waitOf(z, dir);
        FS_OK(w > e->cutA && w < e->len - e->cutB);
        FS_OK((entryOf(z, dir) - w) * dir > 0 && (exitOf(z, dir) - entryOf(z, dir)) * dir > 0);
      }
      // the limit: below the road's own, 30 in town
      if (z.limit > 0) FS_OK(z.limit < std::max(e->maxF, e->maxB) || std::max(e->maxF, e->maxB) == 0);
      if (urban) FS_OK(z.limit == 30);
      FS_OK(z.warn >= (urban ? 30 : 120) && z.warn <= (urban ? 55 : 200));
      // clear of the zebra crossing (at 200 m of road 8)
      if (e->id == 8) ok(z.z1 + z.taper + 10 < 198 || z.z0 - z.taper - 10 > 202, "zone over the crossing");
    }
  }
  FS_OK(worksSite(*net.world->edges[0]) && !worksSite(*net.world->edges[4]) && !worksSite(*net.world->edges[5]) && !worksSite(*net.world->edges[7]));
  ok(!seen.count(4), "never on the motorway");
  ok(!seen.count(5), "never in a roundabout");
  ok(!seen.count(6), "not where a one-way street has but one lane");
  ok(!seen.count(7), "not on a service road");
  // one lane each way: in turns (a light, or signs on a short one in town); more lanes: one of them closed
  for (const int id : {0, 1})
    for (const std::string& k : seen[id]) ok(k == "light" || k == "signs", "road " + std::to_string(id) + ": " + k);
  for (const int id : {2, 3})
    for (const std::string& k : seen[id]) ok(k == "lane", "road " + std::to_string(id));
  ok(seen[0].count("signs") > 0, "a short one in a side street goes by the signs 208/308");
  ok(seen[1].count("light") && !seen[1].count("signs"), "outside towns: the temporary light");
  FS_OK(seen.count(2) && seen.count(3));
}

void theLight() {
  test("the temporary light: the two directions never have green or amber at once, the all-red lasts until the stretch is clear, red-amber before green");
  REvNet net = loop();
  std::shared_ptr<WorkZone> zp = makeZone(*net.world->edges[0], *net.lanes, spec(1, "light", 0, 250, 330, 10, 30, 40, {15, 18}, 0.3, 1));
  const WorkZone& z = *zp;
  const double cycle = z.green[0] + z.green[1] + 2 * (3 + z.clear + 1);
  ok(z.clear >= (80 + 20) / 8.0, "time to clear 100 m at a walking pace of a car");
  double greens[2] = {0, 0};
  double lastGo[2] = {-JS_INF, -JS_INF};
  int lastState[2] = {-1, -1};
  const auto go = [](int s) { return s == SIG_GREEN || s == SIG_AMBER; };
  for (double t = 0; t < cycle * 3; t += 0.1) {
    const WorksLight s0 = lightAt(z, 0, t);
    const WorksLight s1 = lightAt(z, 1, t);
    ok(!(go(s0.state) || s0.state == SIG_RED_AMBER) || !(go(s1.state) || s1.state == SIG_RED_AMBER), "t " + toFixed(t, 1) + ": one at a time");
    const WorksLight both[2] = {s0, s1};
    for (int g = 0; g < 2; g++) {
      const WorksLight& s = both[g];
      if (s.state == SIG_GREEN) greens[g] += 0.1;
      // green comes only after red-amber, and the other side's last moment of amber lies at least the clearing time back
      if (s.state == SIG_GREEN && lastState[g] != SIG_GREEN && lastState[g] >= 0) {
        FS_OK(lastState[g] == SIG_RED_AMBER);
        ok(t - lastGo[1 - g] >= z.clear - 0.25, "cleared before green (" + toFixed(t - lastGo[1 - g], 1) + " s)");
      }
      if (go(s.state)) lastGo[g] = t;
      lastState[g] = s.state;
      FS_OK(s.age >= 0 && s.age <= std::max(z.green[0], z.green[1]) + 2 * (3 + z.clear + 1));
    }
  }
  FS_OK(std::abs(greens[0] - 3 * z.green[0]) < 0.6 && std::abs(greens[1] - 3 * z.green[1]) < 0.6);
}

void noTramZone() {
  test("no work zone where a tram runs along the road: the zones there go once the tracks are known, and none comes there again");
  REvNet net = loop();
  Setup s(net, std::nullopt);
  const auto along0 = [](double z) {
    std::vector<double> p;
    for (double x = 0; x <= 600; x += 5) p.insert(p.end(), {x, 100, z});
    return p;
  };
  for (Edge* e : net.world->allEdges) s.ev.zones.set(e->id, makeZone(*e, *net.lanes, spec(1, "light", 0, 40, 70, 10, 30, 30, {15, 15}, 0, e->id)));
  // a track in the middle of the first road, another one well beside the third (z = 150 + 20)
  RailJson t1;
  t1.k = RAIL_TRAM;
  t1.p = along0(0.8);
  RailJson t2;
  t2.k = RAIL_TRAM;
  t2.p = along0(170);
  RailNetwork rails({t1, t2});
  s.ev.setTracks(&rails);
  ok(!s.ev.zones.has(0), "over the track: gone");
  ok(s.ev.zones.has(2), "twenty metres beside it: stays");
  s.ev.zones.clear();
  s.ev.addRoads(net.world->allEdges);
  for (const auto& [id, z] : s.ev.zones) FS_OK(z->id != 0);
}

void putDownInside() {
  test("a vehicle put down inside a fenced strip (a start, a jump on the map): that work is done for today – it is not boxed in");
  REvNet net = loop();
  Setup s(net, spec(1, "light", 0, 250, 330, 10, 30, 40, {15, 15}, 0, 1));
  const auto at = [&](double x) {
    PlayerState p = s.player;
    p.x = x;
    p.z = 1.5;
    return p;
  };
  // driven up to it: it stays
  s.ev.step(1, {}, at(100));
  s.ev.step(1, {}, at(130));
  FS_OK(s.ev.zones.has(0));
  // put down in the strip (the lane a → b is 1.5 m right of the middle: z = 1.5)
  s.ev.step(1, {}, at(290));
  FS_OK(!s.ev.zones.has(0));
}

// ------------------------------------------------------------------------------------ the traffic at a zone

/** Runs the traffic of a loop round a zone; checks each step; returns how many went through each way. */
std::array<int, 2> through(REvNet& net, Setup& s, double seconds, const std::function<void(const WorkZone&, Traffic&, SignalSystem&)>& each, double every = 50) {
  const WorkZone& z = *s.zone;
  const Edge* e = net.world->edges[0];
  fill(net, s.traffic, every);
  std::array<int, 2> passed{0, 0};
  struct Last {
    const Lane* lane;
    double front;
  };
  std::map<int, Last> last;
  for (double t = 0; t < seconds; t += DT) {
    s.signals.update(DT);
    s.traffic.update(DT, s.player);
    for (const TrafficCar* car : s.traffic.cars) {
      const Lane* lane = car->lane;
      if (!lane || lane->edge != e) continue;
      const double front = car->s + car->length / 2;
      const double out = along(*lane, exitOf(z, lane->dir));
      const auto prev = last.find(car->id);
      if (prev != last.end() && prev->second.lane == lane && prev->second.front < out && front >= out) passed[lane->dir > 0 ? 0 : 1]++;
      last[car->id] = Last{lane, front};
    }
    each(z, s.traffic, s.signals);
  }
  return passed;
}

void inTurnsByLight() {
  test("one side closed, a temporary light: both directions through in turns – never meeting in the narrowing, nobody over the line on red, the side of the work over on the other lane");
  REvNet net = loop();
  Setup s(net, spec(1, "light", 0, 250, 330, 10, 30, 40, {15, 15}, 0, 1));
  const Edge* e = net.world->edges[0];
  std::map<int, double> last;
  int shifted = 0;
  const auto passed = through(net, s, 420, [&](const WorkZone& z, Traffic& tr, SignalSystem& signals) {
    int inside[2] = {0, 0};
    for (const TrafficCar* car : tr.cars) {
      const Lane* lane = car->lane;
      if (!lane || lane->edge != e) continue;
      const int k = lane->dir > 0 ? 0 : 1;
      const double front = car->s + car->length / 2;
      const double wait = along(*lane, waitOf(z, lane->dir));
      const double out = along(*lane, exitOf(z, lane->dir));
      if (front > wait + 0.3 && car->s - car->length / 2 < out) inside[k]++;
      // over the line: only on green (or on amber, too close to stop)
      const auto prev = last.find(car->id * 2 + k);
      if (prev != last.end() && prev->second <= wait && front > wait) {
        const int st = lightAt(z, lane->dir == z.side ? 0 : 1, signals.time).state;
        ok(st == SIG_GREEN || st == SIG_AMBER, "car " + std::to_string(car->id) + " over the line on " + std::to_string(st));
      }
      last[car->id * 2 + k] = front;
      // the fenced strip: nobody drawn in it; the side of the work passes on the other lane
      const auto [as, aoff] = where(*e, *car);
      if (as > z.z0 + 0.5 && as < z.z1 - 0.5) {
        ok(aoff > z.line + 0.5, "car " + std::to_string(car->id) + " in the strip (" + toFixed(aoff, 2) + ")");
        if (lane->dir == z.side && as > z.z0 + 8 && as < z.z1 - 8) {
          ok(std::abs(aoff - 1.5) < 0.3, "over on the oncoming lane (" + toFixed(aoff, 2) + ")");
          shifted++;
        }
      }
    }
    ok(inside[0] == 0 || inside[1] == 0, "both directions in the narrowing (" + std::to_string(inside[0]) + ", " + std::to_string(inside[1]) + ")");
  });
  ok(passed[0] >= 5 && passed[1] >= 5, "through each way (" + std::to_string(passed[0]) + ", " + std::to_string(passed[1]) + ")");
  FS_OK(shifted > 50);
  ok(s.traffic.cars.size() >= 14, "nobody given up on (" + std::to_string(s.traffic.cars.size()) + " cars left)");
}

void inTurnsBySigns() {
  test("one side closed, signs 208 and 308: the oncoming traffic goes first, the narrowing is never shared, both get through");
  REvNet net = loop();
  Setup s(net, spec(-1, "signs", 0, 280, 310, 10, 30, 40, {15, 15}, 0, 2));
  const Edge* e = net.world->edges[0];
  const auto passed = through(
      net, s, 360,
      [&](const WorkZone& z, Traffic& tr, SignalSystem&) {
        int inside[2] = {0, 0};
        for (const TrafficCar* car : tr.cars) {
          const Lane* lane = car->lane;
          if (!lane || lane->edge != e) continue;
          const double front = car->s + car->length / 2;
          if (front > along(*lane, waitOf(z, lane->dir)) + 0.3 && car->s - car->length / 2 < along(*lane, exitOf(z, lane->dir))) inside[lane->dir > 0 ? 0 : 1]++;
          const auto [as, aoff] = where(*e, *car);
          // (the side of the work is b → a here: the strip lies left of a → b, the cars pass right of its beacons)
          if (as > z.z0 + 0.5 && as < z.z1 - 0.5) ok(aoff < z.line - 0.5, "car " + std::to_string(car->id) + " in the strip (" + toFixed(aoff, 2) + ")");
        }
        ok(inside[0] == 0 || inside[1] == 0, "both directions in the narrowing (" + std::to_string(inside[0]) + ", " + std::to_string(inside[1]) + ")");
      },
      60);
  ok(passed[0] >= 5 && passed[1] >= 5, "through each way (" + std::to_string(passed[0]) + ", " + std::to_string(passed[1]) + ")");
  FS_OK(s.traffic.cars.size() >= 10);
}

void laneClosed() {
  test("a lane closed on a road of two lanes each way: the cars move over in good time, one after the other – nobody drives into the strip");
  REvNet net = loop(RoadSpec{.cls = PRIMARY, .lf = 2, .lb = 2, .lw = 3.25});
  Setup s(net, spec(1, "lane", 0, 280, 400, 18, 30, 40, {15, 15}, 0, 3));
  const Edge* e = net.world->edges[0];
  const auto passed = through(
      net, s, 300,
      [&](const WorkZone& z, Traffic& tr, SignalSystem&) {
        for (const TrafficCar* car : tr.cars) {
          const Lane* lane = car->lane;
          if (!lane || lane->edge != e) continue;
          if (lane->dir == z.side && lane->idx == z.lane) {
            const double front = car->s + car->length / 2;
            ok(front < along(*lane, entryOf(z, z.side)) + 0.5 || car->s - car->length / 2 > along(*lane, z.z1), "car " + std::to_string(car->id) + " on the closed lane at " + toFixed(front, 1));
          }
          const auto [as, aoff] = where(*e, *car);
          if (as > z.z0 + 0.5 && as < z.z1 - 0.5) ok(aoff > z.line + 0.3, "car " + std::to_string(car->id) + " in the strip");
        }
      },
      35);
  ok(passed[0] >= 8, "through on the open lane (" + std::to_string(passed[0]) + ")");
  ok(passed[1] >= 8, "the other way untouched (" + std::to_string(passed[1]) + ")");
  ok(s.traffic.cars.size() >= 12, "(" + std::to_string(s.traffic.cars.size()) + " cars left)");
}

// ------------------------------------------------------------------------------------ the player

PlayerSnapshot snapAt(double x, double z, double v) {
  PlayerSnapshot p;
  p.x = x;
  p.y = 100;
  p.z = z;
  p.yaw = -PI / 2;
  p.speed = v;
  p.seatbelt = true;
  p.length = 4.3;
  return p;
}

/**
 * A player's car driven along the first road of the loop, a → b (along x), its middle `off(s)` metres
 * left of the road's middle line at station s, at `v`; `each` looks at every step.
 */
void drive(RoadEvents& ev, SignalSystem& signals, RuleMonitor& rules, double from, double to, double v, const std::function<double(double)>& off, const std::function<void(double)>& each = nullptr) {
  for (double s = from; s < to; s += v * DT) {
    // (left of a → b, the direction +x, is −z)
    const double z = -off(s);
    const PlayerSnapshot p = snapAt(s, z, v);
    signals.update(DT);
    Lane* lane = rules.laneNow();
    PlayerState me{.x = s, .y = 100, .z = z, .yaw = p.yaw, .speed = v, .length = 4.3, .width = 1.8};
    me.lane = lane;
    me.s = lane ? rules.laneFront() - 2.15 : 0;
    ev.step(DT, {}, me);
    rules.update(DT, p, {});
    if (each) each(s);
  }
}

std::string ids(const RuleMonitor& rules) {
  std::string out;
  for (const Violation& v : rules.log) out += (out.empty() ? "" : ", ") + v.id;
  return out;
}

void playerAtZone() {
  test("the player at a zone: the limit from its sign on, a fault on red at the site light and in the closed lane – none passing it right on the other lane on green");
  const ZoneSpec zs = spec(1, "light", 0, 250, 330, 10, 30, 40, {15, 15}, 0, 1);
  // straight through on red, in its own lane, at 50
  {
    REvNet net = loop();
    Setup s(net, zs);
    const WorkZone& z = *s.zone;
    RuleMonitor rules(*net.world, *net.lanes, s.signals, s.traffic);
    // (the light of the side of the work turns red 6 s after the start: the car is at its line some 12 s on)
    double t = 0;
    while (!(lightAt(z, 0, t).state == SIG_RED && lightAt(z, 0, t).age < 0.2)) t += 0.1;
    s.signals.time = t - 6;
    std::map<double, double> limits;
    drive(s.ev, s.signals, rules, 60, 420, 14, [](double) { return -1.5; }, [&](double st) {
      if (rules.context.limit >= 0) limits[jsRound(st)] = rules.context.limit;
    });
    const std::string found = ids(rules);
    ok(found.find("redLight") != std::string::npos, "red at the site light (" + found + ")");
    for (const Violation& v : rules.log)
      if (v.id == "redLight") {
        FS_OK(v.severity == "fail");
        break;
      }
    ok(found.find("worksLane") != std::string::npos, "into the fenced strip");
    ok(found.find("speed") != std::string::npos, "50 where 30 is the limit");
    // the limit: 30 from its sign (with sign 123, 40 m before the stretch) to just behind the zone, 50 before and after
    for (const auto& [st, lim] : limits) {
      const bool inZone = st > entryOf(z, 1) - z.warn + 3 && st < exitOf(z, 1) + 6;
      const bool outside = st < entryOf(z, 1) - z.warn - 3 || st > exitOf(z, 1) + 10;
      if (inZone) ok(lim == 30, "at " + jsStr(st));
      if (outside) ok(lim == 50, "at " + jsStr(st));
    }
  }
  // on green, over on the other lane through the narrowing, at 30
  {
    REvNet net = loop();
    Setup s(net, zs);
    const WorkZone& z = *s.zone;
    RuleMonitor rules(*net.world, *net.lanes, s.signals, s.traffic);
    double t = 0;
    while (!(lightAt(z, 0, t).state == SIG_GREEN && lightAt(z, 0, t).age < 0.2)) t += 0.1;
    // (at 8 m/s the line, 180 m on, is reached in some 22 s: green began 10 s before)
    s.signals.time = t - 12;
    const Lane& fwd = *net.lanes->edgeLanes[0]->fwd[0];
    drive(s.ev, s.signals, rules, 60, 420, 8, [&](double st) { return -1.5 + s.ev.shift(fwd, along(fwd, st)); });
    ok(rules.log.empty(), "no fault (" + ids(rules) + ")");
  }
}

void playerYields() {
  test("the player on the side of the work (Zeichen 208): into the narrowing while an oncoming car is in it is a fault");
  REvNet net = loop();
  Setup s(net, spec(1, "signs", 0, 280, 310, 10, 30, 40, {15, 15}, 0, 2));
  const WorkZone& z = *s.zone;
  RuleMonitor rules(*net.world, *net.lanes, s.signals, s.traffic);
  Lane& back = *net.lanes->edgeLanes[0]->back[0];
  // an oncoming car in the narrowing (it stays there: counted as inside)
  s.traffic.addCar(back, along(back, (z.z0 + z.z1) / 2));
  const Lane& fwd = *net.lanes->edgeLanes[0]->fwd[0];
  const auto shiftOf = [&](double st) { return s.ev.shift(fwd, along(fwd, st)); };
  for (double st = 150; st < 268; st += 8 * DT) {
    const PlayerSnapshot p = snapAt(st, 1.5 - shiftOf(st), 8);
    s.signals.update(DT);
    Lane* lane = rules.laneNow();
    PlayerState me{.x = p.x, .y = 100, .z = p.z, .yaw = p.yaw, .speed = 8, .length = 4.3, .width = 1.8};
    me.lane = lane;
    me.s = lane ? rules.laneFront() - 2.15 : 0;
    s.ev.step(DT, s.traffic.cars, me);
    rules.update(DT, p, {});
  }
  bool found = false;
  for (const Violation& v : rules.log) found = found || (v.id == "worksYield" && v.severity == "major");
  ok(found, "(" + ids(rules) + ")");
}

/** flat land at the height of the roads */
class Flat : public TerrainSource {
 public:
  double heightAt(double, double) override { return 100; }
  int coverAt(double, double) override { return 0; }
};

/**
 * The car of a demonstration drive (the real vehicle model, driven by sim/autodriver) along the first
 * road of the loop from x = 40, `off` metres left of its middle line; `each` looks at every step (the
 * model's position, the vehicle).  Returns the faults the monitor found.
 */
std::vector<std::string> demoDrive(REvNet& net, Setup& s, double off, double until, const std::function<void(const double*, const Vehicle&)>& each) {
  RuleMonitor rules(*net.world, *net.lanes, s.signals, s.traffic);
  Flat terrain;
  WorldGround ground(*net.world, terrain);
  Vehicle v(COMPACT_CAR_AUTOMATIC, ground);
  v.teleport(40, 100.02, -off, -PI / 2);
  v.resetDrivetrain(true);
  // (from the middle of the wheelbase to the front bumper)
  const double FRONT = 2.2;
  AutoDriverOptions opts;
  opts.spec = COMPACT_CAR_AUTOMATIC;
  opts.front = FRONT;
  AutoDriver driver(*net.world, s.signals, rules, &s.traffic, opts);
  double mpos[3] = {0, 0, 0};
  const double H = 1.0 / 120;
  const std::vector<Connector*> noRoute;
  for (double t = 0; t < 240 && mpos[0] < until; t += H) {
    v.getModelPosition(mpos);
    const AutoCommand& cmd = driver.step(H, v, mpos, noRoute);
    v.step(H, DriverInput{.steer = cmd.steer, .throttle = cmd.throttle, .brake = cmd.brake, .clutch = 0, .handbrake = 0, .gear = 1, .ignition = true, .starter = false});
    s.signals.update(H);
    v.getModelPosition(mpos);
    Lane* lane = rules.laneNow();
    PlayerState me{.x = mpos[0], .y = mpos[1], .z = mpos[2], .yaw = v.yaw, .speed = v.speed, .length = 4.28, .width = 1.79};
    me.lane = lane;
    me.s = lane ? rules.laneFront() - 2.14 : 0;
    s.traffic.update(H, me);
    PlayerSnapshot snap;
    snap.x = mpos[0];
    snap.y = mpos[1];
    snap.z = mpos[2];
    snap.yaw = v.yaw;
    snap.speed = v.speed;
    snap.indicator = cmd.indicator;
    snap.lookYaw = cmd.look;
    snap.seatbelt = true;
    snap.length = 4.28;
    snap.front = FRONT;
    rules.update(H, snap, s.traffic.events);
    each(mpos, v);
  }
  ok(mpos[0] >= until, "got through (to x " + toFixed(mpos[0], 0) + ")");
  std::vector<std::string> out;
  for (const Violation& x : rules.log) out.push_back(x.id + " (" + x.severity + ")");
  return out;
}

std::string joined(const std::vector<std::string>& v) {
  std::string s;
  for (const std::string& x : v) s += x + "; ";
  return s;
}

void demoAtLight() {
  test("the demonstration drive at a site light: stops at the line on red, then through the narrowing over on the other lane – without a fault");
  REvNet net = loop();
  Setup s(net, spec(1, "light", 0, 250, 330, 10, 30, 40, {15, 15}, 0, 1));
  const WorkZone& z = *s.zone;
  // (red for the side of the work a few seconds after the start: the car comes to the line on red)
  double t0 = 0;
  while (!(lightAt(z, 0, t0).state == SIG_RED && lightAt(z, 0, t0).age < 0.1)) t0 += 0.05;
  s.signals.time = t0 - 4;
  const double wait = waitOf(z, 1);
  double stood = 0;
  int over = 0;
  const auto faults = demoDrive(net, s, -1.5, 420, [&](const double* m, const Vehicle& v) {
    const double front = m[0] + 2.2;
    if (std::abs(v.speed) < 0.1 && front < wait + 0.5 && front > wait - 6) stood += 1.0 / 120;
    // (left of a → b, the direction +x, is −z: in the narrowing the car keeps left of the beacons)
    if (m[0] > z.z0 && m[0] < z.z1) {
      ok(-m[2] > z.line + 0.9, "in the strip at x " + toFixed(m[0], 1) + " (" + toFixed(-m[2], 2) + ")");
      if (m[0] > z.z0 + 10 && m[0] < z.z1 - 10) over++;
    }
  });
  ok(stood > 3, "waited at the line (" + toFixed(stood, 1) + " s)");
  FS_OK(over > 100);
  ok(faults.empty(), joined(faults));
}

void demoLaneEnds() {
  test("the demonstration drive on a road of two lanes each way: out of the lane that ends before the barrier, past the strip – without a fault");
  REvNet net = loop(RoadSpec{.cls = PRIMARY, .lf = 2, .lb = 2, .lw = 3.25});
  Setup s(net, spec(1, "lane", 0, 280, 400, 18, 30, 40, {15, 15}, 0, 3));
  const WorkZone& z = *s.zone;
  bool moved = false;
  const auto faults = demoDrive(net, s, -4.875, 450, [&](const double* m, const Vehicle&) {
    if (m[0] > entryOf(z, 1) && m[0] < z.z1) ok(-m[2] > z.line + 0.9, "in the strip at x " + toFixed(m[0], 1) + " (" + toFixed(-m[2], 2) + ")");
    if (m[0] < entryOf(z, 1) && -m[2] > -2.2) moved = true;
  });
  ok(moved, "over to the open lane before the taper");
  ok(faults.empty(), joined(faults));
}

// ------------------------------------------------------------------------------------ demonstrations

void demoDays() {
  test("demonstrations: the same date gives the same ones – on most Saturdays around midday, seldom on weekdays, always on the 1st of May");
  {
    const auto a = demosOn(2026, 11, 21);
    const auto b = demosOn(2026, 11, 21);
    FS_OK(a.size() == b.size());
    for (size_t i = 0; i < a.size(); i++) FS_OK(a[i].id == b[i].id && a[i].place == b[i].place && a[i].from == b[i].from && a[i].until == b[i].until && a[i].size == b[i].size && a[i].seed == b[i].seed);
  }
  int saturdays = 0;
  int saturdayDemos = 0;
  int weekdays = 0;
  int weekdayDemos = 0;
  for (double t = utcMs(2027, 0, 1); t < utcMs(2028, 0, 1); t += 86400000) {
    const DateFields d = utcFields(t);
    const int y = d.year;
    const int m = d.month + 1;
    const int day = d.date;
    const auto list = demosOn(y, m, day);
    const int wd = d.day;
    for (const DemoPlan& p : list) {
      const double minutes = (p.until - p.from) / 60000;
      ok(minutes >= 60 && minutes <= 135, jsStr(minutes) + " min");
      FS_OK(p.size >= 30 && p.size <= 220);
      // between the morning and the evening, by the clocks in Kassel
      const double h = kassel::berlinClock(p.from).s / 3600;
      ok(h >= 9.7 && h <= 17.8, "begins at " + toFixed(h, 2) + " h");
    }
    if (m == 5 && day == 1) {
      ok(!list.empty(), "the 1st of May");
      continue;
    }
    if (wd == 6) {
      saturdays++;
      if (!list.empty()) saturdayDemos++;
      // around midday, or in the afternoon
      for (const DemoPlan& p : list) {
        const double h = kassel::berlinClock(p.from).s / 3600;
        ok((h >= 10.7 && h <= 12.3) || (h >= 13.7 && h <= 15.3), "on a Saturday at " + toFixed(h, 2) + " h");
      }
    } else if (wd >= 1 && wd <= 5) {
      weekdays++;
      if (!list.empty()) weekdayDemos++;
    }
  }
  ok(static_cast<double>(saturdayDemos) / saturdays > 0.5 && static_cast<double>(saturdayDemos) / saturdays < 0.9, std::to_string(saturdayDemos) + " of " + std::to_string(saturdays) + " Saturdays");
  ok(static_cast<double>(weekdayDemos) / weekdays > 0.08 && static_cast<double>(weekdayDemos) / weekdays < 0.3, std::to_string(weekdayDemos) + " of " + std::to_string(weekdays) + " weekdays");
}

/** A town with a main street through it, and a march planned near its middle. */
Town marchTown() {
  TownOptions to;
  to.cols = 9;
  to.rows = 7;
  to.block = 120;
  return makeTown(to);
}
// (just south of the street along z = 240: the march takes that one)
const double CX = 370;
const double CZ = 245;

void marchWay() {
  test("the way of a march: along the streets from the place it gathers at, all in one line – its roads and the side roads into it closed");
  Town town = marchTown();
  const std::optional<DemoRoute> route = demoRoute(*town.world, CX, CZ, 500);
  ok(route.has_value(), "a way");
  const DemoRoute& r = *route;
  ok(r.length >= 450, "about as long as wanted (" + toFixed(r.length, 0) + " m)");
  // one after the other: each road begins where the one before ends
  for (size_t i = 0; i < r.edges.size(); i++) {
    const Edge* e = town.world->edges[r.edges[i]];
    const Node* from = r.dirs[i] > 0 ? e->a : e->b;
    const Node* to = r.dirs[i] > 0 ? e->b : e->a;
    FS_OK(from->id == r.nodes[i]);
    FS_OK(to->id == r.nodes[i + 1]);
  }
  ok(std::set<int>(r.edges.begin(), r.edges.end()).size() == r.edges.size(), "no road twice");
  // the same roads: the same way
  FS_OK(demoRoute(*town.world, CX, CZ, 500)->edges == r.edges);
  // closed: its roads both ways, and the side roads where they lead into it
  for (const int id : r.edges) FS_OK(r.ways.count(id * 2) && r.ways.count(id * 2 + 1));
  for (size_t i = 1; i + 1 < r.nodes.size(); i++) {
    const Node* node = town.world->nodes[r.nodes[i]];
    for (const Arm* a : node->arms) {
      if (r.roads.count(a->edge->id)) continue;
      ok(r.ways.count(a->edge->id * 2 + (a->out ? 1 : 0)) > 0, "the side road into it");
      ok(!r.ways.count(a->edge->id * 2 + (a->out ? 0 : 1)), "not the way out of it");
    }
  }
}

Demonstration demo(int id, double from, double until, int size, int seed) {
  Demonstration d;
  d.id = id;
  d.place = "Marktplatz";
  d.town = "Test";
  d.x = CX;
  d.z = CZ;
  d.from = from;
  d.until = until;
  d.size = size;
  d.seed = seed;
  return d;
}

void marchWalks() {
  test("a march: it gathers, then walks its street there and back at a slow pace – the crowd always on its way, never jumping");
  Town town = marchTown();
  const DemoRoute route = *demoRoute(*town.world, CX, CZ, 700);
  Demonstration d = demo(2, SATURDAY, SATURDAY + 7200000, 120, 4);
  d.route = route;
  const double len = crowdShape(d).length;
  int walking = 0;
  int turns = 0;
  struct Last {
    double lo, hi;
    int dir;
  };
  std::optional<Last> last;
  for (double ms = d.from; ms < d.until; ms += 1000) {
    const MarchAt m = marchAt(d, ms);
    // the stretch the crowd covers: from its front back over its length
    const double lo = std::min(m.head, m.head - m.dir * len);
    const double hi = std::max(m.head, m.head - m.dir * len);
    ok(lo >= -0.01 && hi <= route.length + 0.01, "on its way (" + toFixed(lo, 1) + "–" + toFixed(hi, 1) + " of " + toFixed(route.length, 0) + ")");
    if (m.walking) walking++;
    if (last) {
      ok(std::abs(lo - last->lo) < 0.6 && std::abs(hi - last->hi) < 0.6, "a jump at " + jsStr((ms - d.from) / 1000) + " s");
      if (m.dir != last->dir) turns++;
    }
    last = Last{lo, hi, m.dir};
  }
  ok(walking > 0.6 * 7200, "mostly walking (" + std::to_string(walking) + " of 7200 s)");
  ok(turns >= 2, "there and back");
}

void marchUnderWay() {
  test("a march under way: the traffic keeps out of the closed roads, the planned ways go round them, whoever drives in is at fault");
  Town town = marchTown();
  SignalSystem signals(town.world->signals);
  Traffic traffic(*town.world, *town.lanes, signals);
  traffic.targetCount = 26;
  RoadEvents ev(*town.world, *town.lanes, signals);
  double now = SATURDAY;
  ev.now = [&] { return now; };
  traffic.roadEvents = &ev;
  const PlayerState player{.x = CX, .y = 100, .z = CZ + 60, .yaw = 0, .speed = 0, .length = 4.3, .width = 1.8};
  ev.step(0, {}, player);
  ev.zones.clear();
  ev.demos = {demo(1, SATURDAY - 600000, SATURDAY + 3600000, 80, 5)};
  // (the next look at the day finds the march: its way, what is closed)
  ev.step(1, {}, player);
  Demonstration& d = ev.demos[0];
  ok(d.route.has_value(), "its way is known");
  const DemoRoute r = *d.route;
  for (const int id : r.edges) {
    const LaneGraph::EdgeLanes& el = *town.lanes->edgeLanes[id];
    for (const Lane* lane : el.fwd) FS_OK(ev.closed(*lane));
    for (const Lane* lane : el.back) FS_OK(ev.closed(*lane));
  }
  // the traffic: never on a closed road
  size_t cars = 0;
  for (double t = 0; t < 150; t += DT) {
    signals.update(DT);
    traffic.update(DT, player);
    for (const TrafficCar* car : traffic.cars) {
      cars = std::max(cars, traffic.cars.size());
      if (car->lane) ok(!ev.closed(*car->lane), "car " + std::to_string(car->id) + " on a closed road " + std::to_string(car->lane->edge->id));
      if (car->conn) ok(!ev.closed(*car->conn->to), "car " + std::to_string(car->id) + " turning into a closed road " + std::to_string(car->conn->to->edge->id));
    }
  }
  ok(cars > 10, "there was traffic");
  // the barriers: at both ends of the march and across the side roads; what the traffic stops for
  const ClosureLayout& L = ev.closureLayout(d);
  FS_OK(L.barriers.size() >= 2 && L.officers.size() >= 4 && L.signs.size() == L.barriers.size());
  for (const SignPost& sp : L.signs) FS_OK(std::find(sp.s.begin(), sp.s.end(), "250") != sp.s.end());
  // the planned ways: round it – through the town from one side of the march to the other
  town.lanes->wayCost = routeFor.wayCost = [&](int w) { return ev.wayCost(w); };
  {
    for (const int id : r.edges) ok(std::abs(town.world->edges[id]->a->z - 240) < 1 && std::abs(town.world->edges[id]->b->z - 240) < 1, "along the street at z = 240");
    const Edge& start = *town.world->edges[town.street(3, 0, 4, 0)];
    const Edge& goal = *town.world->edges[town.street(3, 4, 4, 4)];
    const Lane& from = *town.lanes->edgeLanes[start.id]->fwd[0];
    const std::vector<Connector*> way = town.lanes->route(from, goal);
    ok(!way.empty(), "a way round");
    for (const Connector* c : way) ok(!ev.closed(*c->to), "not through the march (road " + std::to_string(c->to->edge->id) + ")");
    const RouteGraph g = routeGraphOf(*town.lanes);
    const auto ways = quickest(g, wayOf(from), wayOf(*town.lanes->edgeLanes[goal.id]->fwd[0]));
    ok(ways.has_value(), "a way round for the planner of the whole map");
    for (const int w : *ways) ok(ev.wayCost(w) != JS_INF, "the whole-map planner goes round, too");
  }
  town.lanes->wayCost = routeFor.wayCost = nullptr;
  // the player: inside when it was closed, out of it – no fault; from an open road into it past the barrier – a fault
  RuleMonitor rules(*town.world, *town.lanes, signals, traffic);
  double east = -JS_INF;
  for (const int id : r.nodes) east = std::max(east, town.world->nodes[id]->x);
  const auto westward = [&](double x0, double x1) {
    // (westward, in the lane on the right: north of the middle line)
    for (double x = x0; x > x1; x -= 0.4) {
      PlayerSnapshot p = snapAt(x, 240 - 1.375, 8);
      p.yaw = PI / 2;
      rules.update(DT, p, {});
    }
  };
  westward(east - 30, east - 100);
  bool closedRoad = false;
  for (const Violation& v : rules.log) closedRoad = closedRoad || v.id == "closedRoad";
  ok(!closedRoad, "already inside: no fault");
  westward(east + 70, east - 60);
  for (const Violation& v : rules.log) closedRoad = closedRoad || v.id == "closedRoad";
  ok(closedRoad, "(" + ids(rules) + ")");
  // the trams stop for the crowd on its street
  std::vector<RailObstacle> forTrams;
  ev.railBlocks(forTrams);
  ok(forTrams.size() >= 5, "the crowd is in the trams' way (" + std::to_string(forTrams.size()) + ")");
  // over: nothing closed any more
  now = d.until + 1000;
  ev.step(1, {}, player);
  bool any = false;
  for (const int id : r.edges) any = any || ev.closed(*town.lanes->edgeLanes[id]->fwd[0]);
  FS_OK(!any);
}

void forTheMap() {
  test("the events of the day for the map: the zones with their street, the marches with their times and their way");
  Town town = marchTown();
  SignalSystem signals(town.world->signals);
  RoadEvents ev(*town.world, *town.lanes, signals);
  ev.now = [] { return SATURDAY; };
  const PlayerState player{.x = CX, .y = 100, .z = CZ, .yaw = 0, .speed = 0, .length = 4.3, .width = 1.8};
  ev.step(0, {}, player);
  ev.zones.clear();
  Edge& e = *town.world->edges[0];
  ev.zones.set(e.id, makeZone(e, *town.lanes, spec(1, "signs", 0, 40, 70, 8, 30, 30, {15, 15}, 0, 9)));
  ev.demos = {demo(7, SATURDAY - 60000, SATURDAY + 5400000, 60, 2)};
  ev.step(1, {}, player);
  const auto list = ev.list();
  const RoadEventInfo* works = nullptr;
  const RoadEventInfo* dm = nullptr;
  for (const RoadEventInfo& i : list) {
    if (i.kind == "works" && !works) works = &i;
    if (i.kind == "demo" && !dm) dm = &i;
  }
  FS_OK(works && dm);
  FS_OK(works->street == e.name);
  FS_OK(works->detail == "signs");
  FS_OK(works->from <= SATURDAY && works->until > SATURDAY);
  FS_OK(dm->active == true);
  FS_OK(dm->until == SATURDAY + 5400000);
  ok(dm->line.size() >= 8, "the course of the march");
  FS_OK(!dm->street.empty());
}

}  // namespace

int main() {
  std::printf("roadevents_test (tests/sim/roadevents.test.ts)\n");
  byTheDay();
  whereTheyAre();
  theLight();
  noTramZone();
  putDownInside();
  inTurnsByLight();
  inTurnsBySigns();
  laneClosed();
  playerAtZone();
  playerYields();
  demoAtLight();
  demoLaneEnds();
  demoDays();
  marchWay();
  marchWalks();
  marchUnderWay();
  forTheMap();
  std::printf("ok\n");
  return 0;
}

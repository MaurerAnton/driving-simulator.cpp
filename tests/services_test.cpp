// SPDX-License-Identifier: AGPL-3.0-only
// LINK: game render engine world sim geo core platform i18n
// Origin: tests/game/services.test.ts and tests/world/fuelprices.test.ts @ v1.7.7
/**
 * Fuel stations, car washes and the loading of a truck: the layout of a
 * site (canopy, islands, pumps, the wash's bay), a vehicle at a pump or in
 * a bay, the file read and found by place, filling up, washing, the dirt,
 * the load of a truck, its depot and the script of its loading – and
 * today's fuel prices (world/fuelprices).  (The original's cases of the data
 * build, tools/build-services.ts, are not the game's: the build is not ported.)
 */
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "game/services.h"
#include "render/loading.h"
#include "sim/fuel.h"
#include "tests/vehicle_harness.h"
#include "world/fuelprices.h"
#include "world/services.h"

using namespace fs;
using namespace fs::svc;
using fs::harness::ok;
using fs::harness::test;

namespace {

/** a rectangle as a ring: middle, long side along (ux, uz) */
std::vector<double> box(double cx, double cz, double len, double wid, double ux = 1, double uz = 0) { return rectRing({cx, cz, ux, uz, len, wid}); }

constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

}  // namespace

int main() {
  test("the smallest rectangle round a turned outline, and its frame");
  {
    const double a = 0.6;
    const auto ring = box(100, 50, 24, 10, std::cos(a), std::sin(a));
    const Rect r = minRect(ring);
    ok(std::abs(r.len - 24) < 1e-6 && std::abs(r.wid - 10) < 1e-6, "24 × 10");
    ok(std::abs(std::abs(r.ux * std::cos(a) + r.uz * std::sin(a)) - 1) < 1e-6, "along");
    const auto [x, z] = fromRect(r, 3, -2);
    const auto [u, v] = toRect(r, x, z);
    ok(std::abs(u - 3) < 1e-9 && std::abs(v + 2) < 1e-9, "and back");
  }

  test("under a canopy of 26 × 14 m: four islands, a pump on each, the columns on their ends");
  {
    const CanopyPlan plan = planCanopy(box(0, 0, 26, 14), 200, 205.5);
    ok(plan.islands.size() == 4, "4 islands");
    ok(plan.pumps.size() == 8, "4 pumps");
    ok(plan.columns.size() == 8, "4 columns");
    for (const Island& isl : plan.islands) ok(std::abs(isl.ux) > 0.99, "the islands run along the long side");
    // an L-shaped canopy: no island where there is no roof
    const std::vector<double> L = {-13, -7, 13, -7, 13, 0, 0, 0, 0, 7, -13, 7};
    const CanopyPlan lp = planCanopy(L, NaN, NaN);
    for (const Island& isl : lp.islands) ok(!(isl.x > 1 && isl.z > 1), "island at " + jsStr(isl.x) + ", " + jsStr(isl.z));
  }

  test("a vehicle stands at a pump: alongside it, within the hose's reach, either way round");
  {
    const CanopyPlan plan = planCanopy(box(0, 0, 13, 12), NaN, NaN);
    ok(plan.islands.size() >= 1, "an island");
    const Island& isl = plan.islands[0];
    const double px = plan.pumps[0];
    const double pz = plan.pumps[1];
    // the car to the island's south (+z), heading east (yaw −π/2): the pump on its left
    const Pose car{px + 0.6, pz + 0.65 + 0.9 + 0.5, -PI / 2, 4.3, 1.8};
    ok(std::abs(isl.uz) < 1e-9, "along x");
    const auto at = pumpBeside(plan, car);
    ok(at.has_value(), "beside the pump");
    ok(at->side == -1, "on the left");
    // heading west: the pump on its right
    Pose west = car;
    west.yaw = PI / 2;
    ok(pumpBeside(plan, west)->side == 1, "on the right");
    // across the island, too far, or beyond the pump
    Pose across = car;
    across.yaw = 0;
    ok(!pumpBeside(plan, across), "across");
    Pose far = car;
    far.z = pz + 6;
    ok(!pumpBeside(plan, far), "too far");
    Pose beyond = car;
    beyond.x = px + 6;
    ok(!pumpBeside(plan, beyond), "beyond");
  }

  test("in a car wash's bay: how far to the mark, straight or not");
  {
    const auto ring = box(0, 0, 12, 5);
    const WashPlan plan{ring, minRect(ring), NaN, NaN, true};
    // (heading east, 1.2 m short of the mark in the middle)
    const InBay b = inBay(plan, {-1.2, 0.1, -PI / 2, 4.3, 1.8});
    ok(b.inside && b.straight, "inside, straight");
    ok(std::abs(b.along + 1.2) < 1e-9, "1.2 m short");
    // (heading west at the same place: 1.2 m beyond it)
    const InBay back = inBay(plan, {-1.2, 0, PI / 2, 4.3, 1.8});
    ok(back.dir == -b.dir, "the other way");
    ok(std::abs(back.along - 1.2) < 1e-9, "1.2 m beyond");
    ok(!inBay(plan, {0, 0, 0.4 - PI / 2, 4.3, 1.8}).straight, "not straight");
    ok(!inBay(plan, {0, 5, -PI / 2, 4.3, 1.8}).inside, "beside it");
  }

  test("the file: read and laid out, found by place; prices stay put");
  {
    ServicesJson json;
    json.v = 1;
    json.map = "test";
    {
      ServiceSiteJson s;
      s.k = "fuel", s.osm = "w1", s.name = "Aral", s.brand = "Aral", s.x = 0, s.z = 0, s.area = box(0, 0, 40, 30);
      s.roofs = {{box(0, 0, 26, 14), 200, 205.5, "lod2", std::nullopt, false}};
      json.sites.push_back(s);
    }
    {
      ServiceSiteJson s;
      s.k = "fuel", s.osm = "n2", s.name = "Bft", s.brand = "bft", s.x = 3000, s.z = 0, s.fuels = {"diesel", "hydrogen"};
      s.roofs = {{box(3000, 0, 14, 12.5), std::nullopt, std::nullopt, "gen", std::nullopt, false}};
      json.sites.push_back(s);
    }
    {
      ServiceSiteJson s;
      s.k = "wash", s.osm = "n3", s.x = 50, s.z = 0;
      s.roofs = {{box(50, 0, 12, 5), 200, 204, "lod2", std::nullopt, false}};
      json.sites.push_back(s);
    }
    {
      ServiceSiteJson s;
      s.k = "fuel", s.osm = "n4", s.name = "No room", s.x = 100, s.z = 0;
      json.sites.push_back(s);
    }
    std::vector<ServiceSite> sites = parseServices(json);
    ok(sites.size() == 4, "4 sites");
    ok(sites[0].sells == DEFAULT_FUELS(), "what every station sells");
    ok(sites[1].sells == std::vector<std::string>{"diesel"}, "diesel only");
    ok(sites[0].canopies.size() == 1, "a canopy");
    ok(sites[2].wash.has_value(), "a wash");
    bool threw = false;
    try {
      ServicesJson v2 = json;
      v2.v = 2;
      parseServices(v2);
    } catch (const std::exception&) {
      threw = true;
    }
    ok(threw, "version 2");
    const ServiceIndex ix(std::move(sites));
    std::set<std::string> near;
    for (const ServiceSite* s : ix.near(10, 0, 30)) near.insert(s->osm);
    ok(near.count("w1") && !near.count("n2"), "near");
    ok(ix.nearest(2900, 0, "fuel")->site->osm == "n2", "nearest");
    // (a station without pumps is not where one is sent)
    ok(ix.nearest(110, 0, "fuel")->site->osm == "w1", "with pumps");
    ok(onSite(ix.sites[0], 19, 0, 4) && !onSite(ix.sites[0], 40, 0, 4), "on the site");
    const double p = priceAt(ix.sites[0], "e10");
    ok(p == priceAt(ix.sites[0], "e10"), "the same price");
    ok(std::abs(p - 1.739) <= 0.04 + 1e-9, jsStr(p));
  }

  test("filling up: the nozzle goes in, the fuel runs until the tank is full, the nozzle goes back");
  {
    FuelTank tank(*tankFor("car"), 0.5);
    Refuelling r("e10", 1.739, tank.spec.flow);
    const StandState still{true, false};
    double t = 0;
    while (r.phase == "out") {
      r.step(0.1, tank, still);
      t += 0.1;
    }
    ok(std::abs(t - NOZZLE_TIME) < 0.11, jsStr(t));
    ok(r.nozzle() == 1, "in the filler");
    while (!r.done() && t < 100) {
      r.step(0.1, tank, still);
      t += 0.1;
    }
    ok(r.done(), "done");
    ok(r.reason == "full", "full");
    ok(tank.share() == 1, "a full tank");
    ok(std::abs(r.fill.litres - 25) < 0.05, "25 l");
    ok(r.fill.cost() == jsRound(r.fill.litres * 1.739 * 100) / 100, "cost");
  }

  test("filling up stops at once when the engine is started or the vehicle moves – or when the driver says so");
  {
    FuelTank tank(*tankFor("car"), 0.2);
    const StandState still{true, false};
    Refuelling a("e10", 1.7, 1.6);
    for (int i = 0; i < 40; i++) a.step(0.1, tank, still);
    const double litres = a.fill.litres;
    ok(litres > 0, "some");
    a.step(0.1, tank, {true, true});
    ok(a.phase == "back", "back");
    ok(a.reason == "engine", "engine");
    for (int i = 0; i < 20; i++) a.step(0.1, tank, {false, true});
    ok(a.done(), "done");
    ok(a.fill.litres == litres, "no more");
    Refuelling b("e10", 1.7, 1.6);
    b.step(0.5, tank, {false, false});
    ok(b.reason == "moved", "moved");
    Refuelling c("e10", 1.7, 1.6);
    for (int i = 0; i < 30; i++) c.step(0.1, tank, still);
    c.stop();
    ok(c.reason == "stopped", "stopped");
    ok(c.phase == "back", "back");
  }

  test("the wash: foam, brushes, rinsing, drying – the gantry to and fro, the dirt gone; it stops if the car moves");
  {
    Washing w(0.8);
    const StandState still{true, false};
    ok(w.stepId() == "foam", "foam first");
    ok(w.at() == 1, "the gantry waits");
    std::vector<std::string> seen;
    double lo = JS_INF;
    double t = 0;
    double total = 0;
    for (const WashStep& s : WASH_STEPS) total += s.time;
    while (!w.done() && t < 100) {
      if (std::find(seen.begin(), seen.end(), w.stepId()) == seen.end()) seen.push_back(w.stepId());
      lo = std::min(lo, w.at());
      w.update(0.1, still);
      t += 0.1;
    }
    ok(seen == std::vector<std::string>{"foam", "brush", "rinse", "dry"}, "the steps");
    ok(std::abs(t - total) < 0.2, jsStr(t));
    ok(lo < -0.99, "the gantry goes all the way along the car");
    ok(w.dirt() == 0, "clean");
    ok(w.share() == 1, "all");
    Washing x(0.5);
    x.update(3, still);
    ok(x.dirt() < 0.5 && x.dirt() > 0.3, jsStr(x.dirt()));
    x.update(0.1, {false, false});
    ok(x.aborted && x.done(), "aborted");
  }

  test("the dirt grows with the distance – more on a wet road, most off the road");
  {
    const double dry = dirtGrowth(10000, 0, 0, 0);
    ok(dry > 0.05 && dry < 0.25, jsStr(dry));
    ok(dirtGrowth(10000, 1, 0, 0) > dry * 5, "wet");
    ok(dirtGrowth(300, 0, 1, 0) > 0.4, "off the road");
    ok(dirtGrowth(0, 1, 1, 1) == 0, "standing");
    ok(HELP.litres > 0 && HELP.time > 0, "help");
  }

  test("which way a place lies, seen from the driver's seat");
  // heading north (yaw 0, forward −z): north is ahead, east to the right, south behind, west to the left
  ok(bearingSector(0, 0, 0, 0, -100) == 0, "ahead");
  ok(bearingSector(0, 0, 0, 100, -100) == 1, "ahead right");
  ok(bearingSector(0, 0, 0, 100, 0) == 2, "right");
  ok(bearingSector(0, 0, 0, 0, 100) == 4, "behind");
  ok(bearingSector(0, 0, 0, -100, 0) == 6, "left");
  // heading east (yaw −π/2): south is to the right
  ok(bearingSector(0, 0, -PI / 2, 0, 100) == 2, "east: south right");

  test("a truck is loaded with 4 to 6 pallets, within what it may carry, in the front of its box");
  {
    double seed = 7;
    const auto rand = [&seed]() { return (seed = std::fmod(seed * 16807, 2147483647)) / 2147483647; };
    for (int k = 0; k < 20; k++) {
      const LoadPlan lp = planLoad(3.9, 2.4, 1600, rand);
      ok(lp.pallets.size() >= 4 && lp.pallets.size() <= 6, "4 to 6");
      ok(lp.payload.mass <= 1600 && lp.payload.mass >= 400, jsStr(lp.payload.mass));
      double sum = 0;
      for (const PalletPlan& p : lp.pallets) sum += p.mass;
      ok(lp.payload.mass == sum, "the sum");
      const BoxInside inside = boxInside(3.9, 2.4);
      ok(lp.payload.z > inside.front && lp.payload.z < inside.rear, "in the box");
      ok(lp.payload.y > inside.floor, "on the floor");
    }
    const BoxInside inside = boxInside(4.7, 2.5);
    const auto slots = palletSlots({inside.x0, inside.x1, inside.front, inside.rear}, 6);
    ok(slots.size() == 6, "6 slots");
    for (const auto& [x, z] : slots) ok(std::abs(x) + 0.4 <= inside.x1 && z - 0.6 >= inside.front && z + 0.6 <= inside.rear, "in the box");
  }

  test("the depot: beside the road, the truck facing it, off the road and free; none where there is no room");
  {
    // a road along z, 12 m wide round x = 0; the truck in it heading north
    const auto road = [](double x, double) { return std::abs(x) < 6; };
    const auto flat = [](double, double) { return 200.0; };
    const auto free = findDepot(2, 0, 0, 3.75, 4.45, {road, [](double, double, double) { return false; }, flat});
    ok(free.has_value(), "a place");
    ok(free->x > 6 + 2 + 3.7, "beside the road on the right: " + jsStr(free->x));
    // facing west, to the road: forward (−sin yaw, −cos yaw) = (−1, 0)
    ok(std::abs(-std::sin(free->yaw) + 1) < 1e-6 && std::abs(std::cos(free->yaw)) < 1e-6, "facing the road");
    // the right side built up: the left side then
    const auto left = findDepot(2, 0, 0, 3.75, 4.45, {road, [](double x, double, double) { return x > 0; }, flat});
    ok(left && left->x < -6, "on the left");
    // steep ground, or buildings everywhere: none
    ok(!findDepot(2, 0, 0, 3.75, 4.45, {road, [](double, double, double) { return false; }, [](double x, double) { return 200 + std::abs(x) * 0.3; }}), "steep");
    ok(!findDepot(2, 0, 0, 3.75, 4.45, {road, [](double, double, double) { return true; }, flat}), "built up");
  }

  test("the script of the loading: every pallet fetched, set down, pushed into its place in turn, in 30 to 60 s");
  {
    // (the depot's search leaves the room the scene needs between the truck and the dock)
    ok(DEPOT.gap == DOCK_GAP, "the gap");
    const BoxInside inside = boxInside(4.7, 2.5);
    for (bool dock : {true, false}) {
      for (int n : {4, 6}) {
        const auto slots = palletSlots({inside.x0, inside.x1, inside.front, inside.rear}, n);
        const Script s = schedule(n, slots, inside.rear + 0.08, inside.floor, dock, inside.rear + 0.08 + DOCK_GAP);
        ok(s.end >= 30 - 1e-9 && s.end <= 60 + 1e-9, jsStr(s.end));
        ok(s.closing < s.end, "closing");
        double last = 0;
        for (const Script::Pallet& p : s.pallets) {
          ok(p.pick < p.drop && p.drop < p.take && p.take < p.placed, "in turn");
          ok(p.drop >= last, "one after the other at the door");
          last = p.take;
        }
        for (size_t i = 1; i < s.forklift.size(); i++) ok(s.forklift[i].t >= s.forklift[i - 1].t, "forklift");
        for (size_t i = 1; i < s.jack.size(); i++) ok(s.jack[i].t >= s.jack[i - 1].t, "jack");
        ok(s.pallets[static_cast<size_t>(n - 1)].placed <= s.closing, "all in before the doors close");
      }
    }
  }

  // ---- tests/world/fuelprices.test.ts
  test("the service's prices by the game's fuels (closed or unknown ones left out)");
  {
    Json a;
    Json::parse(R"({"lat":51,"lng":9,"e5":1.799,"e10":1.739,"diesel":1.659})", a);
    ok(pricesOf(a) == std::map<std::string, double>{{"e5", 1.799}, {"e10", 1.739}, {"diesel", 1.659}, {"hgv", 1.659}}, "the prices");
    Json b;
    Json::parse(R"({"lat":51,"lng":9,"e5":false,"e10":null,"diesel":0})", b);
    ok(pricesOf(b).empty(), "none");
  }

  test("a station of the map gets the price of the nearest station of the list – within 150 m only");
  {
    FuelPrices p;
    p.setLive({{100, 100, {{"e10", 1.699}}}, {1000, 0, {{"e10", 1.759}}}});
    ok(p.priceAt(130, 90, "e10") == 1.699, "the near one");
    ok(p.priceAt(1040, 20, "e10") == 1.759, "the other");
    ok(!p.priceAt(500, 500, "e10"), "none near");
    ok(!p.priceAt(100, 100, "lpg"), "a fuel it does not report");
  }

  test("without a key nothing is asked");
  {
    int asked = 0;
    FuelPrices p([&asked](plat::Request) -> Task<plat::Response> {
      asked++;
      plat::Response r;
      r.status = 200;
      r.body = {'{', '}'};
      co_return r;
    });
    p.refresh(0, 0, 1e9);
    ok(asked == 0, "no key");
    p.key = "k";
    p.refresh(0, 0, 1e9);
    p.refresh(10, 0, 1e9 + 1000);
    ok(asked == 1, "once – not again a second later");
  }
  std::printf("services_test: ok\n");
  return 0;
}

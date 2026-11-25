// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The main menu as a stage (src/ui/stage.h): the order the vehicles come in
 * when swiped, the kinds of vehicle, the trailer that goes with a vehicle,
 * what can be done with each, where a drive begins, how the camera frames
 * the vehicle – and that the stage is the menu of new players while a menu
 * stored before stays.
 *
 * Origin: tests/ui/stage.test.ts @ v1.7.7
 */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>
#include <set>
#include <string>
#include <vector>

#include "game/settings.h"
#include "game/vehicles.h"
#include "platform/plat.h"
#include "ui/stage.h"

using namespace fs;

static const char* const KEY = "fahrsim-baunatal.settings.v2";

static std::vector<std::string> ids(const VehicleKind& k, bool yard = true) {
  std::vector<std::string> out;
  for (const StageMode& m : stageModes(k, yard)) out.push_back(m.id);
  return out;
}
static bool has(const std::vector<std::string>& v, const std::string& x) { return std::find(v.begin(), v.end(), x) != v.end(); }

int main() {
  const std::vector<StageGroup> groups = stageGroups();

  // the kinds of vehicle: every vehicle in one of them, in the order of the licence classes
  {
    std::vector<VehicleKind> all;
    for (const StageGroup& g : groups) all.insert(all.end(), g.kinds.begin(), g.kinds.end());
    std::vector<VehicleKind> a = all;
    std::vector<VehicleKind> b;
    for (const VehicleKind& k : VEHICLE_KINDS)
      if (VEHICLES.find(k)) b.push_back(k);
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    assert(a == b);
    assert(std::set<VehicleKind>(all.begin(), all.end()).size() == all.size());
    std::vector<std::string> gids;
    for (const StageGroup& g : groups) gids.push_back(g.id);
    for (const char* id : {"car", "moto", "truck", "bus", "tractor", "tram", "plane", "foot"}) assert(has(gids, id));
    assert((std::vector<std::string>(gids.begin(), gids.begin() + 5) == std::vector<std::string>{"car", "moto", "truck", "bus", "tractor"}));
    const auto kindsOf = [&groups](const std::string& id) {
      for (const StageGroup& g : groups)
        if (g.id == id) return g.kinds;
      return std::vector<VehicleKind>{};
    };
    assert((kindsOf("moto") == std::vector<VehicleKind>{"moped", "motorcycle125", "motorcycle35", "motorcycle"}));
    assert((kindsOf("truck") == std::vector<VehicleKind>{"lightTruck", "lightTruckTrailer", "truckSolo", "truck", "semi"}));
    assert((kindsOf("bus") == std::vector<VehicleKind>{"minibus", "minibusTrailer", "bus", "busTrailer"}));
    // a vehicle of a model the menu does not know is a kind of its own; a crane is known by its name
    assert(groupOf("mobileCrane", "crane") == "crane");
    assert(groupOf("crane", "truck") == "truck");
    assert(groupOf("digger", "excavator") == "excavator");
    KindTable more = vehicleInfo();
    more.push_back({"digger", KindInfo{.kind = "digger", .model = "excavator", .licence = "L", .trailer = false}});
    const std::vector<StageGroup> m = stageGroups({"car", "digger", "carTrailer"}, more);
    assert(m.size() == 2 && m[0].id == "car" && (m[0].kinds == std::vector<VehicleKind>{"car", "carTrailer"}) && m[1].id == "excavator" &&
           (m[1].kinds == std::vector<VehicleKind>{"digger"}));
  }

  // swiping goes through every vehicle kind by kind, and round again
  {
    std::vector<VehicleKind> order;
    for (const StageGroup& g : groups) order.insert(order.end(), g.kinds.begin(), g.kinds.end());
    assert(cycleVehicle(groups, "car", 1) == "carTrailer");
    assert(cycleVehicle(groups, "carTrailer", 1) == "moped");
    assert(cycleVehicle(groups, "car", -1) == order.back());
    assert(cycleVehicle(groups, order.back(), 1) == "car");
    std::set<VehicleKind> seen;
    VehicleKind k = "car";
    for (size_t i = 0; i < order.size(); i++) {
      seen.insert(k);
      k = cycleVehicle(groups, k, 1);
    }
    assert(k == "car");
    assert(seen.size() == order.size());
    assert(cycleVehicle({}, "bus", 1) == "bus");
  }

  // the kinds: the next kind brings the vehicle last chosen of it, else its first
  {
    assert(cycleGroup(groups, "car", 1) == "moped");
    assert(cycleGroup(groups, "carTrailer", 1) == "moped");
    assert(cycleGroup(groups, "car", 1, {{"moto", "motorcycle35"}}) == "motorcycle35");
    assert(cycleGroup(groups, "moped", -1, {{"car", "carTrailer"}}) == "carTrailer");
    assert(cycleGroup(groups, "car", -1) == groups.back().kinds[0]);
    // (a remembered vehicle that is not of that kind does not count)
    assert(cycleGroup(groups, "car", 1, {{"moto", "bus"}}) == "moped");
  }

  // the trailer that goes with a vehicle: its licence class with an E
  {
    for (const auto& [solo, pair] : std::vector<std::pair<VehicleKind, VehicleKind>>{{"car", "carTrailer"}, {"lightTruck", "lightTruckTrailer"}, {"truckSolo", "truck"}, {"minibus", "minibusTrailer"}, {"bus", "busTrailer"}}) {
      assert(trailerPartner(solo) == pair);
      assert(trailerPartner(pair) == solo);
    }
    for (const char* k : {"motorcycle", "moped", "tractor", "tractorSlow", "tram", "plane", "pedestrian"}) assert(!trailerPartner(k));
  }

  // the name beside the badge leaves out the licence class in brackets – in every script
  {
    assert(shortName("Car (class B)", "B") == "Car");
    assert(shortName("Car with trailer (class BE)", "BE") == "Car with trailer");
    assert(shortName("Light motorcycle, 125 cm³ (class A1)", "A1") == "Light motorcycle, 125 cm³");
    assert(shortName("Pkw (Klasse B)", "B") == "Pkw");
    assert(shortName("Light aeroplane (PPL)", "PPL") == "Light aeroplane");
    assert(shortName("小汽车（B 类）", "B") == "小汽车");
    assert(shortName("乗用車（Bクラス）", "B") == "乗用車");
    assert(shortName("سيارة (الفئة B)", "B") == "سيارة");
    assert(shortName("Truck 7.5 t (class C1)", "C1") == "Truck 7.5 t");
    // brackets that say something else, a class that is only part of a word, no class at all: as it is
    assert(shortName("Tram (Kassel)", "BOStrab") == "Tram (Kassel)");
    assert(shortName("Car (class BE)", "B") == "Car (class BE)");
    assert(shortName("On foot", "") == "On foot");
    assert(shortName("(class B)", "B") == "(class B)");
    // every vehicle of the game keeps a name
    for (const VehicleDef& d : VEHICLES.all()) assert(!shortName(d.kind, d.licence).empty());
  }

  // what can be done with a vehicle: as the other menus offer it
  {
    const std::vector<std::string> car = ids("car");
    assert((std::vector<std::string>(car.begin(), car.begin() + 4) == std::vector<std::string>{"free", "demo", "tour", "show"}));
    assert(car.back() == "exam");
    assert(has(car, "basics") && has(car, "rbl") && has(car, "motorway"));
    assert(!has(car, "ceExam") && !has(car, "busLine") && !has(car, "tramLine"));
    assert(!has(ids("car", false), "show"));
    const std::vector<std::string> truck = ids("truck");
    assert(truck[4] == "ceCouple");
    assert(truck.back() == "ceExam");
    assert(!has(truck, "exam"));
    for (const std::string& id : ids("truck", false)) assert(!(id.starts_with("ceC") || id == "ceRamp"));
    assert(has(ids("bus"), "busLine") && has(ids("minibusTrailer"), "busLine"));
    assert((ids("tram") == std::vector<std::string>{"free", "demo", "tramLine"}));
    assert((ids("plane") == std::vector<std::string>{"free", "demo"}));
    assert((ids("pedestrian") == std::vector<std::string>{"free", "show"}));
    assert(!has(ids("tractor"), "motorway") && has(ids("tractor"), "exam"));
    std::vector<std::string> groupsOf;
    for (const StageMode& m : stageModes("car", true))
      if (!has(groupsOf, m.group)) groupsOf.push_back(m.group);
    assert((groupsOf == std::vector<std::string>{"drive", "lesson", "exam"}));
  }

  // the way to play chosen is kept while the vehicle has it
  {
    assert(pickMode(ids("car"), "rbl") == "rbl");
    assert(pickMode(ids("tram"), "rbl") == "free");
    assert(pickMode(ids("tram"), "demo") == "demo");
    assert(pickMode({}, "exam") == "free");
  }

  const std::vector<SpawnJson> spawns = {
      SpawnJson{.name = "Marktplatz", .x = 10, .z = 20, .yaw = 0.5, .kind = "parking", .rough = false, .g = "Baunatal"},
      SpawnJson{.name = "Rathaus", .x = 30, .z = 40, .yaw = 1, .kind = "street", .rough = false, .g = "Baunatal"},
      SpawnJson{.name = "Königsplatz", .x = 6700, .z = -7000, .yaw = 2, .kind = "street", .rough = false, .g = "Kassel"},
  };
  const std::vector<TramStopChoice> stops = {
      {.name = "Rathaus", .town = "Baunatal", .x = 35, .z = 45},
      {.name = "Königsplatz", .town = "Kassel", .x = 6705, .z = -7004},
      {.name = "Wilhelmshöhe", .town = "Kassel", .x = 4000, .z = -6000},
  };
  const std::vector<RunwayChoice> runways = {
      {.field = "Flughafen Kassel (EDVK)", .end = "09", .x = -5000, .z = -20000, .yaw = 1.6},
      {.field = "Flughafen Kassel (EDVK)", .end = "27", .x = -3000, .z = -20000, .yaw = -1.5},
      {.field = "Segelfluggelände", .end = "04", .x = 9000, .z = 3000, .yaw = 0.7},
  };
  const std::vector<TramStopChoice> noStops;
  const auto lastStart = [](int spawn = 0) {
    Settings::LastStart l;
    l.spawn = spawn;
    return l;
  };
  const auto samePreview = [](const std::optional<MenuPreview>& p, const VehicleKind& kind, double x, double z, double yaw) {
    return p && p->kind == kind && p->x == x && p->z == z && p->yaw == yaw;
  };

  // where the drive begins: the start point, an address, a stop, the end of a runway
  {
    const StagePlace road = stagePlace("car", lastStart(1), spawns, nullptr, nullptr);
    assert(road.state == "ok");
    assert(road.request.spawn == 1 && !road.request.address);
    assert(samePreview(road.preview, "car", 30, 40, 1));
    assert(road.town == "Baunatal");
    assert(road.name == "Rathaus");
    assert(stagePlace("bus", lastStart(99), spawns, nullptr, nullptr).request.spawn == 2);
    const AddressHit address{.label = "Am Stadtpark 5, Baunatal", .street = "Am Stadtpark", .x = 55, .z = 66, .exact = true};
    Settings::LastStart withAddress = lastStart(2);
    withAddress.address = address;
    const StagePlace at = stagePlace("motorcycle", withAddress, spawns, nullptr, nullptr);
    assert(at.request.spawn == 2 && at.request.address && at.request.address->label == address.label);
    assert(samePreview(at.preview, "motorcycle", 55, 66, 2));
    assert(at.town.empty());
    assert(at.name == address.label);
    assert(stagePlace("car", lastStart(), {}, nullptr, nullptr).state == "none");

    // the tram: at the stop of the last drive – else in the middle of Kassel; its list read later
    assert(stagePlace("tram", lastStart(), spawns, nullptr, nullptr).state == "loading");
    assert(stagePlace("tram", lastStart(), spawns, &noStops, nullptr).state == "none");
    const StagePlace first = stagePlace("tram", lastStart(), spawns, &stops, nullptr);
    assert(first.name == "Königsplatz");
    assert(samePreview(first.preview, "tram", 6705, -7004, 0) && first.preview->stop && first.preview->stop->name == "Königsplatz");
    Settings::LastStart kept = lastStart();
    kept.tramStop = "Wilhelmshöhe|Kassel";
    const StagePlace k = stagePlace("tram", kept, spawns, &stops, nullptr);
    assert(k.request.stop && k.request.stop->name == "Wilhelmshöhe");
    assert(k.town == "Kassel");

    // the aeroplane: at the end of the runway of the last flight – else the first of the map's
    assert(stagePlace("plane", lastStart(), spawns, &stops, nullptr).state == "loading");
    Settings::LastStart flown = lastStart();
    flown.runway = "Segelfluggelände|04";
    const StagePlace plane = stagePlace("plane", flown, spawns, &stops, &runways);
    assert(plane.request.runway && plane.request.runway->end == "04" && plane.request.runway->field == "Segelfluggelände");
    assert(plane.runway == "04");
    assert(plane.town == "Segelfluggelände");
    assert(samePreview(plane.preview, "plane", 9000, 3000, 0.7));
    Settings::LastStart gone = lastStart();
    gone.runway = "gone|99";
    assert(stagePlace("plane", gone, spawns, &stops, &runways).request.runway->end == "09");
  }

  // a place tapped on the map: there – or the stop, the runway nearest to it
  {
    const SpawnJson spot{.name = "Kasseler Straße", .x = 6600, .z = -6900};
    Settings::LastStart car = lastStart();
    assert(startFromSpot("car", car, spot, &stops, &runways, "Road without a name"));
    assert(car.address && car.address->label == "Kasseler Straße" && car.address->street == "Kasseler Straße" && car.address->x == 6600 && car.address->z == -6900 && !car.address->exact);
    Settings::LastStart nameless = lastStart();
    startFromSpot("pedestrian", nameless, SpawnJson{.name = "", .x = 1, .z = 2}, nullptr, nullptr, "Road without a name");
    assert(nameless.address && nameless.address->label == "Road without a name");
    Settings::LastStart tram = lastStart();
    assert(startFromSpot("tram", tram, spot, &stops, &runways));
    assert(tram.tramStop == "Königsplatz|Kassel");
    assert(!tram.address);
    Settings::LastStart plane = lastStart();
    assert(startFromSpot("plane", plane, SpawnJson{.name = "", .x = -3100, .z = -19900}, &stops, &runways));
    assert(plane.runway == "Flughafen Kassel (EDVK)|27");
    Settings::LastStart none = lastStart();
    assert(!startFromSpot("tram", none, spot, nullptr, &runways));
  }

  // the camera: the vehicle large in the picture, whole, a little above its middle
  {
    const auto half = [](double fov) { return std::tan((fov * std::numbers::pi) / 360); };
    for (const auto& [kind, radius] : std::vector<std::pair<std::string, double>>{{"car", 8}, {"motorcycle", 5.5}, {"bus", 15}, {"truck", 25}, {"carTrailer", 12}, {"pedestrian", 4.2}, {"plane", 15}, {"tram", 23.5}}) {
      for (double aspect : {16.0 / 9, 844.0 / 390, 4.0 / 3, 390.0 / 844}) {
        const StageCam f = frameOf(100, kind, radius, aspect);
        const VehicleDef& d = VEHICLES.at(kind);
        // (how far it reaches from the middle the camera circles: its longer end, its half span – the trailer behind it)
        const double reach = kind == "tram" ? (radius - 7) / 1.1 : std::max({d.outline.front, d.outline.rear, d.outline.hw, d.trailer ? radius * 0.55 : 0.0});
        // seen from the side its longer end stays within the picture …
        assert(reach / f.dist <= half(f.fov) * aspect * 1.0001);
        // … and it fills a good part of it (or of the height, for whoever is taller than long)
        const double share = std::max(reach / f.dist / (half(f.fov) * aspect), 1.6 / f.dist / half(f.fov));
        assert(share > 0.3);
        assert(f.dist > reach);
        assert(f.rise > 0 && f.aim < 0);
      }
    }
    // an upright screen: a wider lens
    assert(frameOf(0, "car", 8, 0.46).fov > frameOf(0, "car", 8, 1.78).fov);
    // it circles …
    assert(frameOf(0, "car", 8, 1.5).angle != frameOf(10, "car", 8, 1.5).angle);
    // … and comes in from further away when the vehicle changed
    const StageCam now = frameOf(50, "car", 8, 1.5, 50);
    const StageCam later = frameOf(52, "car", 8, 1.5, 50);
    assert(now.dist > later.dist * 1.25);
    assert(std::abs(later.dist - frameOf(52, "car", 8, 1.5).dist) < 1e-9);
    // (the frame of the menu: the coming in begins anew with another vehicle)
    stageFrame(10, "car", 8, 1.5);
    const StageCam settled = stageFrame(20, "car", 8, 1.5);
    const StageCam changed = stageFrame(20, "bus", 15, 1.5);
    assert(changed.dist > frameOf(20, "bus", 15, 1.5).dist * 1.3);
    assert(settled.dist == frameOf(20, "car", 8, 1.5).dist);
  }

  // the main menu: the stage for new players, a menu chosen stays – the rows, the default of 1.3.0, become the stage once
  {
    const auto store = [](const char* key, const std::string& text) {
      plat::localStorage().removeItem(KEY);
      plat::localStorage().removeItem("fahrsim-baunatal.settings.v1");
      if (key) plat::localStorage().setItem(key, text);
    };
    store(nullptr, "");
    assert(DEFAULT_SETTINGS().menuStyle == "stage");
    assert(loadSettings().menuStyle == "stage");
    for (const char* style : {"carousel", "classic", "stage"}) {
      // (written by 1.4.0 or later: what was chosen stays)
      store(KEY, std::string(R"({"vehicle":"bus","menuStyle":")") + style + R"(","version":2})");
      const Settings s = loadSettings();
      assert(s.menuStyle == style);
      assert(s.vehicle == "bus");
    }
    // written by 1.3.0, which stored its default: the rows become the stage once, a list chosen stays a list
    store(KEY, R"({"vehicle":"bus","menuStyle":"carousel"})");
    assert(loadSettings().menuStyle == "stage");
    store(KEY, R"({"vehicle":"bus","menuStyle":"classic"})");
    assert(loadSettings().menuStyle == "classic");
    // stored before there was a choice of menus (before 1.3.0), or something unknown: the stage
    store(KEY, R"({"vehicle":"truck"})");
    assert(loadSettings().menuStyle == "stage");
    store(KEY, R"({"menuStyle":"cards"})");
    assert(loadSettings().menuStyle == "stage");
    // the settings of the versions before the playtest
    store("fahrsim-baunatal.settings.v1", R"({"menuStyle":"classic"})");
    assert(loadSettings().menuStyle == "classic");
    store(nullptr, "");
  }

  std::printf("ui_stage_test: ok\n");
  return 0;
}

// LINK: game sim render i18n audio input engine world core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The vehicles one can learn with (src/game/vehicles.h): the table as the original has it at
 * v1.7.7 – every kind with its licence class, its picture, its engines, its size, what the
 * rules ask of it and the physics it is driven with.  (The numbers below are the original's,
 * written out from its table; how the vehicles drive is tests/vehicle/fleet.test.ts's.)
 *
 * With the vehicle case of the original's tests/game/takeover.test.ts: what a vehicle of the
 * traffic is taken over as is one of these.
 */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "game/vehicles.h"
#include "sim/trailer.h"

using namespace fs;

struct Want {
  const char* kind;
  const char* licence;
  const char* name;
  const char* model;
  std::vector<double> powers;
  double power;
  /** the mass of its trailer (-1: none) */
  double trailer;
  double front, rear, hw;
  std::vector<double> axles;
  double track, door, doorSide;
  std::vector<double> circles;
  double radius, zoom, zoomMax;
  /** (-1: no caps) */
  double capRural, capMotorway;
  const char* capFault;
  double governed;
  bool heavy, noMotorway, long_, dash;
  const char* crane;
  /** the names of its physics with the engine it comes with: manual, with clutch help, automatic */
  const char* manual;
  const char* assist;
  const char* automatic;
  bool assistClutch;
  const char* automaticGearbox;
};

static const std::vector<Want> WANT = {
    {"car", "B", "vehCar", "car", {60, 75, 95, 115, 150, 200}, 75, -1, 2.195, 2.0850000000000004, 0.895, {1.315, -1.315}, 0.77, 0.1, -1, {-1.28, -0.05, 1.18}, 0.93, 7, 18, -1, -1, "speed", 0, false, false, false, false, "", "Kompaktwagen 1.5 Benzin, 6-Gang, 75 PS", "Kompaktwagen, Kupplungshilfe, 75 PS", "Kompaktwagen 1.5 Benzin, Automatik, 75 PS", true, "automatic"},
    {"carTrailer", "BE", "vehCarTrailer", "car", {60, 75, 95, 115, 150, 200}, 115, 1400, 2.195, 2.0850000000000004, 0.895, {1.315, -1.315}, 0.77, 0.1, -1, {-1.28, -0.05, 1.18}, 0.93, 10, 26, 80, 80, "classSpeed", 0, false, false, true, false, "", "Kompaktwagen 1.5 Benzin, 6-Gang, 115 PS", "Kompaktwagen, Kupplungshilfe, 115 PS", "Kompaktwagen 1.5 Benzin, Automatik, 115 PS", true, "automatic"},
    {"motorcycle", "A", "vehMotorcycle", "motorcycle", {48, 68, 95, 120, 150}, 95, -1, 1.07, 1.08, 0.42, {0.725, -0.725}, 0.05, 0, 0, {-0.55, 0.55}, 0.45, 5, 14, -1, -1, "speed", 0, false, false, false, true, "", "Motorrad", "Motorrad", "Motorrad, Automatik", true, "automatic"},
    {"truckSolo", "C", "vehTruckSolo", "truckHeavy", {320, 360, 400}, 360, -1, 4, 4.7, 1.25, {2.6, -1.92, -3.28}, 1.1, 3, -1, {-2.8, -0.9, 1, 2.9, 3.45}, 1.25, 15, 34, 60, 80, "classSpeed", 90, true, false, true, true, "", "Lkw 26 t, drei Achsen, mit Kofferaufbau, 265 kW, automatisiertes Getriebe, 360 PS", "Lkw 26 t, drei Achsen, mit Kofferaufbau, 265 kW, automatisiertes Getriebe, 360 PS", "Lkw 26 t, drei Achsen, mit Kofferaufbau, 265 kW, automatisiertes Getriebe, 360 PS", false, "automatic"},
    {"truck", "CE", "vehTruck", "truck", {257, 300, 354, 408, 449}, 354, 6000, 3.75, 4.45, 1.25, {2.35, -2.35}, 1.1, 2.45, -1, {-2.5, -0.6, 1.3, 3.2}, 1.25, 21, 45, 60, 80, "truckSpeed", 90, true, false, true, true, "", "Lkw 12 t mit Kofferaufbau, 180 kW, automatisiertes Getriebe, 354 PS", "Lkw 12 t mit Kofferaufbau, 180 kW, automatisiertes Getriebe, 354 PS", "Lkw 12 t mit Kofferaufbau, 180 kW, automatisiertes Getriebe, 354 PS", false, "automatic"},
    {"semi", "CE", "vehSemi", "semi", {400, 450, 510}, 450, 8000, 3.1, 2.8, 1.25, {1.85, -1.85}, 1.1, 2, -1, {-2, -0.6, 0.8, 2}, 1.25, 22, 48, 60, 80, "truckSpeed", 90, true, false, true, true, "", "Sattelzugmaschine 18 t, 330 kW, automatisiertes Getriebe, 450 PS", "Sattelzugmaschine 18 t, 330 kW, automatisiertes Getriebe, 450 PS", "Sattelzugmaschine 18 t, 330 kW, automatisiertes Getriebe, 450 PS", false, "automatic"},
    {"bus", "D", "vehBus", "bus", {279, 321, 354, 394}, 321, -1, 5.65, 6.35, 1.28, {2.95, -2.95}, 1.05, 4.85, 1, {-4.6, -2.3, 0, 2.3, 4.6}, 1.3, 18, 40, 80, 100, "classSpeed", 100, true, false, true, true, "", "Linienbus 12 m, 220 kW, Automatik", "Linienbus 12 m, 220 kW, Automatik", "Linienbus 12 m, 220 kW, Automatik", false, "automatic"},
    {"tractor", "T", "vehTractor", "tractor", {143, 187, 231, 272}, 187, -1, 2.4, 2.1, 1.15, {1.325, -1.325}, 0.95, -0.2, -1, {-1.4, 0, 1.3}, 1.15, 8, 20, -1, -1, "speed", 60, true, true, false, true, "", "Traktor 150 kW, 60 km/h", "Traktor 150 kW, 60 km/h", "Traktor 150 kW, 60 km/h", false, "automatic"},
    {"tractorSlow", "L", "vehTractorSlow", "tractor", {50, 75, 102, 143}, 102, -1, 2.4, 2.1, 1.15, {1.325, -1.325}, 0.95, -0.2, -1, {-1.4, 0, 1.3}, 1.15, 8, 20, -1, -1, "speed", 40, true, true, false, true, "", "Traktor 110 kW, Lastschaltgetriebe, 40 km/h, 102 PS", "Traktor 110 kW, Lastschaltgetriebe, 40 km/h, 102 PS", "Traktor 110 kW, Lastschaltgetriebe, 40 km/h, 102 PS", false, "automatic"},
    {"moped", "AM", "vehMoped", "motorcycle", {3, 4, 5}, 4, -1, 1.07, 1.08, 0.42, {0.725, -0.725}, 0.05, 0, 0, {-0.55, 0.55}, 0.45, 5, 14, -1, -1, "speed", 45, false, true, false, true, "", "Moped 50 cm³, 45 km/h", "Moped 50 cm³, 45 km/h", "Moped 50 cm³, 45 km/h, Automatik", true, "automatic"},
    {"motorcycle125", "A1", "vehMotorcycle125", "motorcycle", {11, 13, 15}, 15, -1, 1.07, 1.08, 0.42, {0.725, -0.725}, 0.05, 0, 0, {-0.55, 0.55}, 0.45, 5, 14, -1, -1, "speed", 0, false, false, false, true, "", "Leichtkraftrad 125 cm³", "Leichtkraftrad 125 cm³", "Leichtkraftrad 125 cm³, Automatik", true, "automatic"},
    {"motorcycle35", "A2", "vehMotorcycle35", "motorcycle", {27, 35, 47}, 47, -1, 1.07, 1.08, 0.42, {0.725, -0.725}, 0.05, 0, 0, {-0.55, 0.55}, 0.45, 5, 14, -1, -1, "speed", 0, false, false, false, true, "", "Motorrad bis 35 kW", "Motorrad bis 35 kW", "Motorrad bis 35 kW, Automatik", true, "automatic"},
    {"lightTruck", "C1", "vehLightTruck", "truckLight", {136, 170, 190, 218}, 170, -1, 3.35, 4.05, 1.2, {1.95, -1.95}, 0.95, 2.05, -1, {-2.3, -0.6, 1.1, 2.8}, 1.2, 13, 30, 80, 80, "classSpeed", 90, true, false, true, true, "", "Lkw 7,5 t mit Kofferaufbau, 130 kW, automatisiertes Getriebe", "Lkw 7,5 t mit Kofferaufbau, 130 kW, automatisiertes Getriebe", "Lkw 7,5 t mit Kofferaufbau, 130 kW, automatisiertes Getriebe", false, "automatic"},
    {"lightTruckTrailer", "C1E", "vehLightTruckTrailer", "truckLight", {136, 170, 190, 218}, 190, 2600, 3.35, 4.05, 1.2, {1.95, -1.95}, 0.95, 2.05, -1, {-2.3, -0.6, 1.1, 2.8}, 1.2, 16, 36, 60, 80, "classSpeed", 90, true, false, true, true, "", "Lkw 7,5 t mit Kofferaufbau, 130 kW, automatisiertes Getriebe, 190 PS", "Lkw 7,5 t mit Kofferaufbau, 130 kW, automatisiertes Getriebe, 190 PS", "Lkw 7,5 t mit Kofferaufbau, 130 kW, automatisiertes Getriebe, 190 PS", false, "automatic"},
    {"minibus", "D1", "vehMinibus", "minibus", {116, 129, 150, 177}, 150, -1, 3.35, 4.05, 1.1, {2.15, -2.15}, 0.875, 1.25, 1, {-2.3, -0.6, 1.1, 2.8}, 1.1, 12, 28, 80, 100, "classSpeed", 100, true, false, true, true, "", "Kleinbus 7,4 m, 120 kW, Automatik, 150 PS", "Kleinbus 7,4 m, 120 kW, Automatik, 150 PS", "Kleinbus 7,4 m, 120 kW, Automatik, 150 PS", false, "automatic"},
    {"minibusTrailer", "D1E", "vehMinibusTrailer", "minibus", {116, 129, 150, 177}, 177, 1500, 3.35, 4.05, 1.1, {2.15, -2.15}, 0.875, 1.25, 1, {-2.3, -0.6, 1.1, 2.8}, 1.1, 15, 34, 80, 80, "classSpeed", 100, true, false, true, true, "", "Kleinbus 7,4 m, 120 kW, Automatik, 177 PS", "Kleinbus 7,4 m, 120 kW, Automatik, 177 PS", "Kleinbus 7,4 m, 120 kW, Automatik, 177 PS", false, "automatic"},
    {"busTrailer", "DE", "vehBusTrailer", "bus", {279, 321, 354, 394}, 354, 1800, 5.65, 6.35, 1.28, {2.95, -2.95}, 1.05, 4.85, 1, {-4.6, -2.3, 0, 2.3, 4.6}, 1.3, 22, 48, 80, 80, "classSpeed", 100, true, false, true, true, "", "Linienbus 12 m, 220 kW, Automatik, 354 PS", "Linienbus 12 m, 220 kW, Automatik, 354 PS", "Linienbus 12 m, 220 kW, Automatik, 354 PS", false, "automatic"},
    {"tram", "BOStrab", "vehTram", "tram", {}, 0, -1, 4.7, 4.7, 1.2, {2.95, -2.95}, 1.05, 0, 0, {-3, 0, 3}, 1.25, 22, 50, -1, -1, "speed", 70, true, true, true, true, "", "Linienbus 12 m, 220 kW, Automatik", "Linienbus 12 m, 220 kW, Automatik", "Linienbus 12 m, 220 kW, Automatik", false, "automatic"},
    {"plane", "PPL", "vehPlane", "plane", {100, 116, 160, 180}, 160, -1, 2.6, 5.6, 5.45, {1.5, -0.35}, 1.15, -0.4, -1, {-1.5, 0.5, 2.5, 4.5}, 1.1, 15, 80, -1, -1, "speed", 0, false, true, false, true, "", "Kompaktwagen 1.5 Benzin, 6-Gang", "Kompaktwagen 1.5 Benzin, 6-Gang", "Kompaktwagen 1.5 Benzin, 6-Gang", false, "manual"},
    {"mobileCrane", "C", "vehMobileCrane", "mobileCrane", {503}, 503, -1, 6.6, 4.6, 1.28, {1.75, -0.95, -2.55}, 1.04, 3.1, -1, {-5.4, -3.4, -1.3, 0.8, 2.9, 3.35}, 1.28, 16, 40, 60, 80, "classSpeed", 80, true, false, true, true, "mobile", "Mobilkran 40 t, 3 Achsen, 370 kW", "Mobilkran 40 t, 3 Achsen, 370 kW", "Mobilkran 40 t, 3 Achsen, 370 kW", false, "automatic"},
    {"loaderCrane", "C", "vehLoaderCrane", "loaderCrane", {286}, 286, -1, 3.9, 5.25, 1.28, {2.4, -2.4}, 1.1, 3, -1, {-2.9, -1, 1, 2.9, 3.97}, 1.28, 14, 34, 60, 80, "classSpeed", 90, true, false, true, true, "loader", "Lkw mit Ladekran 18 t, 2 Achsen, 210 kW", "Lkw mit Ladekran 18 t, 2 Achsen, 210 kW", "Lkw mit Ladekran 18 t, 2 Achsen, 210 kW", false, "automatic"},
    {"towerCrane", "", "vehTowerCrane", "towerCrane", {}, 0, -1, 50, 15, 2.5, {0, 0}, 2.5, 0, 0, {0}, 2.6, 30, 90, -1, -1, "speed", 0, true, true, false, true, "tower", "Lkw 12 t mit Kofferaufbau, 180 kW, automatisiertes Getriebe", "Lkw 12 t mit Kofferaufbau, 180 kW, automatisiertes Getriebe", "Lkw 12 t mit Kofferaufbau, 180 kW, automatisiertes Getriebe", false, "automatic"},
    {"forklift", "", "vehForklift", "forklift", {}, 0, -1, 2.45, 1.3, 0.6, {0.825, -0.825}, 0.55, 0.2, -1, {-0.7, 0.02, 0.7}, 0.58, 6, 16, -1, -1, "speed", 20, false, true, false, true, "", "Gabelstapler 2,5 t", "Gabelstapler 2,5 t", "Gabelstapler 2,5 t", false, "automatic"},
    {"pedestrian", "", "vehOnFoot", "walker", {}, 0, -1, 0.25, 0.25, 0.3, {0, 0}, 0.2, 0, 0, {0}, 0.3, 3.4, 12, -1, -1, "speed", 0, false, true, false, true, "", "Kompaktwagen 1.5 Benzin, 6-Gang", "Kompaktwagen 1.5 Benzin, 6-Gang", "Kompaktwagen 1.5 Benzin, 6-Gang", false, "manual"},
};

/** the vehicles whose physics are a part of the simulation of their own: left out where that part is not there */
static const char* const MAY_BE_MISSING[] = {"truckSolo", "semi", "plane", "mobileCrane", "loaderCrane", "forklift"};

int main() {
  // the kinds, in the order of the licence classes – and those driven on the road
  assert(VEHICLE_KINDS.size() == 24 && VEHICLE_KINDS.front() == "car" && VEHICLE_KINDS.back() == "pedestrian");
  assert(ROAD_VEHICLE_KINDS.size() == 19);
  for (const char* k : {"tram", "pedestrian", "plane", "towerCrane", "forklift"}) {
    assert(std::find(VEHICLE_KINDS.begin(), VEHICLE_KINDS.end(), k) != VEHICLE_KINDS.end());
    assert(std::find(ROAD_VEHICLE_KINDS.begin(), ROAD_VEHICLE_KINDS.end(), k) == ROAD_VEHICLE_KINDS.end());
  }
  assert(vehicleKind("car") == "car" && vehicleKind("plane") == "plane");
  assert(!vehicleKind("ufo") && !vehicleKind(""));

  // every vehicle as the original's table has it
  assert(WANT.size() == VEHICLE_KINDS.size());
  size_t there = 0;
  std::vector<std::string> order;
  for (const Want& w : WANT) {
    assert(vehicleKind(w.kind));
    const VehicleDef* d = VEHICLES.find(w.kind);
    if (!d) {
      bool may = false;
      for (const char* k : MAY_BE_MISSING) may = may || std::strcmp(k, w.kind) == 0;
      assert(may);
      continue;
    }
    there++;
    order.push_back(w.kind);
    assert(&VEHICLES.at(w.kind) == d);
    assert(d->kind == w.kind && d->licence == w.licence && d->name == w.name && d->model == w.model);
    assert(d->powers == w.powers && d->power == w.power);
    assert((d->trailer ? d->trailer->mass : -1) == w.trailer);
    assert(d->outline.front == w.front && d->outline.rear == w.rear && d->outline.hw == w.hw);
    assert(d->axles == w.axles && d->track == w.track && d->door == w.door && d->doorSide == w.doorSide);
    assert(d->circles == w.circles && d->radius == w.radius && d->zoom == w.zoom && d->zoomMax == w.zoomMax);
    assert((d->caps ? d->caps->rural : -1) == w.capRural && (d->caps ? d->caps->motorway : -1) == w.capMotorway);
    assert(d->capFault == w.capFault && d->governed == w.governed);
    assert(d->heavy == w.heavy && d->noMotorway == w.noMotorway && d->long_ == w.long_ && d->dash == w.dash);
    assert(d->crane.value_or("") == w.crane);
    // its physics for each gearbox, with the engine it comes with
    assert(d->spec("manual", d->power).name == w.manual);
    const VehicleSpec assist = d->spec("assist", d->power);
    assert(assist.name == w.assist && assist.autoClutch == w.assistClutch);
    const VehicleSpec automatic = d->spec("automatic", d->power);
    assert(automatic.name == w.automatic && automatic.transmission == w.automaticGearbox);
    // the engine to drive it with: the one chosen, if it may have it, else the one it comes with
    assert(powerOf(*d, {}) == d->power);
    assert(powerOf(*d, {{d->kind, 1}}) == d->power);
    if (!d->powers.empty()) assert(powerOf(*d, {{d->kind, d->powers.back()}}) == d->powers.back());
    assert(powerOf(*d, {{"ufo", 1}}) == d->power);
  }
  assert(there >= 18 && VEHICLES.all().size() == there);
  // (in the order of the original's table)
  for (size_t i = 0; i < order.size(); i++) assert(VEHICLES.all()[i].kind == order[i]);
  bool thrown = false;
  try {
    VEHICLES.at("ufo");
  } catch (const std::out_of_range&) {
    thrown = true;
  }
  assert(thrown && !VEHICLES.find("ufo"));

  // the trailers' outlines
  assert(!trailerOf(VEHICLES.at("car")));
  {
    const auto t = trailerOf(VEHICLES.at("carTrailer"));
    assert(t && t->front == 1.9500000000000002 && t->rear == 1.7499999999999996 && t->hw == 1.025 && t->hitch == 3.1);
    const auto u = trailerOf(VEHICLES.at("truck"));
    assert(u && u->front == 3.9000000000000004 && u->rear == 2.5 && u->hw == 1.25 && u->hitch == 5.4);
  }

  // an engine of each class within what the class allows: AM at most 4 kW, A1 at most 11 kW, A2 at most 35 kW
  for (const auto& [kind, limitKw] : std::map<std::string, double>{{"moped", 4}, {"motorcycle125", 11}, {"motorcycle35", 35}})
    for (double ps : VEHICLES.at(kind).powers) assert(ps * 0.7355 <= limitKw + 0.05);
  // (and each vehicle comes with one of its engines, listed from the weakest)
  for (const VehicleDef& d : VEHICLES.all()) {
    if (d.powers.empty()) continue;
    assert(std::find(d.powers.begin(), d.powers.end(), d.power) != d.powers.end());
    assert(std::is_sorted(d.powers.begin(), d.powers.end()));
  }

  // tests/game/takeover.test.ts: what a vehicle of the traffic is taken over as is a vehicle of the game, driven on the road
  for (const char* k : {"car", "bus", "lightTruck"}) {
    assert(VEHICLES.find(k));
    assert(std::find(ROAD_VEHICLE_KINDS.begin(), ROAD_VEHICLE_KINDS.end(), k) != ROAD_VEHICLE_KINDS.end());
  }

  std::printf("game_vehicles_test: ok (%zu of %zu vehicles)\n", there, WANT.size());
  return 0;
}

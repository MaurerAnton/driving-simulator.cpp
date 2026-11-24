// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/vehicles.ts @ v1.7.7
#include "game/vehicles.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

#include "render/carBody.h"
#include "sim/trailer.h"
#include "sim/vehicle/fleet.h"
#include "sim/vehicle/index.h"

// A vehicle whose physics come from a part of the simulation that is not in the port is left out of
// the table (see VehicleTable); it is there as soon as that part's header is.
#if __has_include("sim/flightspec.h")
#include "sim/flightspec.h"
#define FS_VEHICLES_PLANE 1
#endif
#if __has_include("sim/crane.h")
#include "sim/crane.h"
#define FS_VEHICLES_CRANES 1
#endif
#if __has_include("sim/forklift.h")
#include "sim/forklift.h"
#define FS_VEHICLES_FORKLIFT 1
#endif
#if __has_include("sim/semitrailer.h")
#include "sim/semitrailer.h"
#define FS_VEHICLES_SEMI 1
#endif

namespace fs {

namespace {

using Spec = std::function<VehicleSpec(const std::string& transmission, double power)>;

/**
 * A motorcycle of a class: the big one with an engine of the power chosen,
 * `size` saying how much smaller the machine is, geared with `finalDrive`.
 */
Spec bike(const std::string& name, double size, double finalDrive) {
  static const double BIG_BIKE_PS = peakPowerPs(MOTORCYCLE);
  return [name, size, finalDrive](const std::string& t, double power) -> VehicleSpec {
    const Variants v = variants(name, power / BIG_BIKE_PS, finalDrive, size);
    if (t == "automatic") return v.auto_;
    if (t == "assist") {
      VehicleSpec s = v.manual;
      s.autoClutch = true;
      return s;
    }
    return v.manual;
  };
}

struct Table {
  std::vector<VehicleDef> all;
  std::unordered_map<std::string, size_t> at;
};

Table build() {
  const double carFront = COMPACT_DIMS.wheelbase / 2 + COMPACT_DIMS.frontOverhang;
  // (what a car has; the kind, the licence, the name, the trailer and the caps are each vehicle's own)
  VehicleDef car;
  car.model = "car";
  car.spec = [](const std::string& t, double power) { return withPower(t == "automatic" ? COMPACT_CAR_AUTOMATIC : t == "assist" ? COMPACT_CAR_AUTOCLUTCH : COMPACT_CAR, power); };
  car.powers = {60, 75, 95, 115, 150, 200};
  car.power = 75;
  car.outline = {.front = carFront, .rear = COMPACT_DIMS.length - carFront, .hw = COMPACT_DIMS.width / 2};
  car.axles = {COMPACT_DIMS.wheelbase / 2, -COMPACT_DIMS.wheelbase / 2};
  car.track = COMPACT_DIMS.track / 2;
  car.door = 0.1;
  car.doorSide = -1;
  car.circles = {-1.28, -0.05, 1.18};
  car.radius = 0.93;
  car.zoom = 7;
  car.zoomMax = 18;
  car.governed = 0;
  car.heavy = false;
  car.noMotorway = false;
  car.long_ = false;
  car.dash = false;

  // (… and a truck; how far the camera stands behind it is each one's own)
  VehicleDef truck;
  truck.model = "truck";
  truck.spec = [](const std::string&, double power) { return atPower(TRUCK, power); };
  truck.powers = {218, 257, 300, 354};
  truck.power = 257;
  truck.outline = {.front = 3.75, .rear = 4.45, .hw = 1.25};
  truck.axles = {2.35, -2.35};
  truck.track = 1.1;
  truck.door = 2.45;
  truck.doorSide = -1;
  truck.circles = {-2.5, -0.6, 1.3, 3.2};
  truck.radius = 1.25;
  truck.governed = 90;
  truck.heavy = true;
  truck.noMotorway = false;
  truck.long_ = true;
  truck.dash = true;

  /** what the motorcycles of every class have in common */
  VehicleDef MOTO_DEF = car;
  MOTO_DEF.kind = "motorcycle";
  MOTO_DEF.licence = "A";
  MOTO_DEF.name = "vehMotorcycle";
  MOTO_DEF.model = "motorcycle";
  MOTO_DEF.spec = bike("Motorrad", 1, MOTORCYCLE.finalDrive);
  MOTO_DEF.powers = {48, 68, 95, 120, 150};
  MOTO_DEF.power = 95;
  MOTO_DEF.trailer = nullptr;
  MOTO_DEF.caps = std::nullopt;
  MOTO_DEF.capFault = "speed";
  MOTO_DEF.outline = {.front = 1.07, .rear = 1.08, .hw = 0.42};
  MOTO_DEF.axles = {0.725, -0.725};
  MOTO_DEF.track = 0.05;
  MOTO_DEF.door = 0;
  MOTO_DEF.doorSide = 0;
  MOTO_DEF.circles = {-0.55, 0.55};
  MOTO_DEF.radius = 0.45;
  MOTO_DEF.zoom = 5;
  MOTO_DEF.zoomMax = 14;
  MOTO_DEF.dash = true;

  /** a 7.5 t truck (class C1) */
  VehicleDef LIGHT_TRUCK_DEF = truck;
  LIGHT_TRUCK_DEF.kind = "lightTruck";
  LIGHT_TRUCK_DEF.licence = "C1";
  LIGHT_TRUCK_DEF.name = "vehLightTruck";
  LIGHT_TRUCK_DEF.model = "truckLight";
  LIGHT_TRUCK_DEF.spec = [](const std::string&, double power) { return atPower(LIGHT_TRUCK, power); };
  LIGHT_TRUCK_DEF.powers = {136, 170, 190, 218};
  LIGHT_TRUCK_DEF.power = 170;
  LIGHT_TRUCK_DEF.trailer = nullptr;
  LIGHT_TRUCK_DEF.caps = VehicleDef::Caps{.rural = 80, .motorway = 80};
  LIGHT_TRUCK_DEF.capFault = "classSpeed";
  LIGHT_TRUCK_DEF.outline = {.front = 3.35, .rear = 4.05, .hw = 1.2};
  LIGHT_TRUCK_DEF.axles = {1.95, -1.95};
  LIGHT_TRUCK_DEF.track = 0.95;
  LIGHT_TRUCK_DEF.door = 2.05;
  LIGHT_TRUCK_DEF.circles = {-2.3, -0.6, 1.1, 2.8};
  LIGHT_TRUCK_DEF.radius = 1.2;
  LIGHT_TRUCK_DEF.zoom = 13;
  LIGHT_TRUCK_DEF.zoomMax = 30;

  /** a minibus (class D1): its door is at the front on the right */
  VehicleDef MINIBUS_DEF = truck;
  MINIBUS_DEF.kind = "minibus";
  MINIBUS_DEF.licence = "D1";
  MINIBUS_DEF.name = "vehMinibus";
  MINIBUS_DEF.model = "minibus";
  MINIBUS_DEF.spec = [](const std::string&, double power) { return atPower(MINIBUS, power); };
  MINIBUS_DEF.powers = {116, 129, 150, 177};
  MINIBUS_DEF.power = 150;
  MINIBUS_DEF.trailer = nullptr;
  MINIBUS_DEF.caps = VehicleDef::Caps{.rural = 80, .motorway = 100};
  MINIBUS_DEF.capFault = "classSpeed";
  MINIBUS_DEF.outline = {.front = 3.35, .rear = 4.05, .hw = 1.1};
  MINIBUS_DEF.axles = {2.15, -2.15};
  MINIBUS_DEF.track = 0.875;
  MINIBUS_DEF.door = 1.25;
  MINIBUS_DEF.doorSide = 1;
  MINIBUS_DEF.circles = {-2.3, -0.6, 1.1, 2.8};
  MINIBUS_DEF.radius = 1.1;
  MINIBUS_DEF.zoom = 12;
  MINIBUS_DEF.zoomMax = 28;
  MINIBUS_DEF.governed = 100;

  VehicleDef BUS_DEF = truck;
  BUS_DEF.kind = "bus";
  BUS_DEF.licence = "D";
  BUS_DEF.name = "vehBus";
  BUS_DEF.model = "bus";
  BUS_DEF.spec = [](const std::string&, double power) { return atPower(BUS, power); };
  BUS_DEF.powers = {279, 321, 354, 394};
  BUS_DEF.power = 321;
  BUS_DEF.trailer = nullptr;
  BUS_DEF.caps = VehicleDef::Caps{.rural = 80, .motorway = 100};
  BUS_DEF.capFault = "classSpeed";
  BUS_DEF.outline = {.front = 5.65, .rear = 6.35, .hw = 1.28};
  BUS_DEF.axles = {2.95, -2.95};
  BUS_DEF.track = 1.05;
  // (the driver of a city bus gets in by the front door, on the right)
  BUS_DEF.door = 4.85;
  BUS_DEF.doorSide = 1;
  BUS_DEF.circles = {-4.6, -2.3, 0, 2.3, 4.6};
  BUS_DEF.radius = 1.3;
  BUS_DEF.zoom = 18;
  BUS_DEF.zoomMax = 40;
  BUS_DEF.governed = 100;

  Table t;
  const auto add = [&t](const VehicleDef& d) {
    t.at[d.kind] = t.all.size();
    t.all.push_back(d);
  };

  {
    VehicleDef d = car;
    d.kind = "car";
    d.licence = "B";
    d.name = "vehCar";
    d.trailer = nullptr;
    d.caps = std::nullopt;
    d.capFault = "speed";
    add(d);
  }
  {
    VehicleDef d = car;
    d.kind = "carTrailer";
    d.licence = "BE";
    d.name = "vehCarTrailer";
    d.trailer = &CAR_TRAILER;
    d.caps = VehicleDef::Caps{.rural = 80, .motorway = 80};
    d.capFault = "classSpeed";
    d.zoom = 10;
    d.zoomMax = 26;
    d.long_ = true;
    // (a car that pulls a trailer comes with a stronger engine)
    d.power = 115;
    add(d);
  }
  add(MOTO_DEF);
#ifdef FS_VEHICLES_SEMI
  // class C: the three-axle rigid truck (render/semitruck.ts) – bigger than the 7.5-tonner of C1 and unlike it
  {
    VehicleDef d = truck;
    d.kind = "truckSolo";
    d.licence = "C";
    d.name = "vehTruckSolo";
    d.model = "truckHeavy";
    d.spec = [](const std::string&, double power) { return atPower(HEAVY_RIGID, power); };
    d.powers = {320, 360, 400};
    d.power = 360;
    d.trailer = nullptr;
    d.caps = VehicleDef::Caps{.rural = 60, .motorway = 80};
    d.capFault = "classSpeed";
    d.outline = {.front = 4.0, .rear = 4.7, .hw = 1.25};
    d.axles = {2.6, -1.92, -3.28};
    d.track = 1.1;
    d.door = 3.0;
    d.circles = {-2.8, -0.9, 1.0, 2.9, 3.45};
    d.radius = 1.25;
    d.zoom = 15;
    d.zoomMax = 34;
    add(d);
  }
#endif
  {
    VehicleDef d = truck;
    d.kind = "truck";
    d.licence = "CE";
    d.name = "vehTruck";
    d.trailer = &TANDEM_TRAILER;
    d.caps = VehicleDef::Caps{.rural = 60, .motorway = 80};
    d.capFault = "truckSpeed";
    d.zoom = 21;
    d.zoomMax = 45;
    d.powers = {257, 300, 354, 408, 449};
    d.power = 354;
    add(d);
  }
#ifdef FS_VEHICLES_SEMI
  // class CE as well: the articulated lorry – tractor unit and semi-trailer (sim/semitrailer.ts)
  {
    VehicleDef d = truck;
    d.kind = "semi";
    d.licence = "CE";
    d.name = "vehSemi";
    d.model = "semi";
    d.spec = [](const std::string&, double power) { return atPower(TRACTOR_UNIT, power); };
    d.powers = {400, 450, 510};
    d.power = 450;
    d.trailer = &SEMI_TRAILER;
    d.caps = VehicleDef::Caps{.rural = 60, .motorway = 80};
    d.capFault = "truckSpeed";
    d.outline = {.front = TRACTOR_OUTLINE.front, .rear = TRACTOR_OUTLINE.rear, .hw = TRACTOR_OUTLINE.hw};
    d.axles = {TRACTOR_UNIT.wheelbase / 2, -TRACTOR_UNIT.wheelbase / 2};
    d.track = 1.1;
    d.door = 2.0;
    d.circles = {-2.0, -0.6, 0.8, 2.0};
    d.radius = 1.25;
    d.zoom = 22;
    d.zoomMax = 48;
    add(d);
  }
#endif
  add(BUS_DEF);
  {
    VehicleDef d = car;
    d.kind = "tractor";
    d.licence = "T";
    d.name = "vehTractor";
    d.model = "tractor";
    d.spec = [](const std::string&, double power) { return atPower(TRACTOR_FAST, power); };
    d.powers = {143, 187, 231, 272};
    d.power = 187;
    d.trailer = nullptr;
    d.caps = std::nullopt;
    d.capFault = "speed";
    d.outline = {.front = 2.4, .rear = 2.1, .hw = 1.15};
    d.axles = {1.325, -1.325};
    d.track = 0.95;
    d.door = -0.2;
    d.circles = {-1.4, 0, 1.3};
    d.radius = 1.15;
    d.zoom = 8;
    d.zoomMax = 20;
    d.governed = 60;
    d.heavy = true;
    d.noMotorway = true;
    d.dash = true;
    add(d);
  }
  {
    VehicleDef d = car;
    d.kind = "tractorSlow";
    d.licence = "L";
    d.name = "vehTractorSlow";
    d.model = "tractor";
    d.spec = [](const std::string&, double power) { return atPower(TRACTOR, power); };
    d.powers = {50, 75, 102, 143};
    d.power = 102;
    d.trailer = nullptr;
    d.caps = std::nullopt;
    d.capFault = "speed";
    d.outline = {.front = 2.4, .rear = 2.1, .hw = 1.15};
    d.axles = {1.325, -1.325};
    d.track = 0.95;
    d.door = -0.2;
    d.circles = {-1.4, 0, 1.3};
    d.radius = 1.15;
    d.zoom = 8;
    d.zoomMax = 20;
    d.governed = 40;
    d.heavy = true;
    d.noMotorway = true;
    d.dash = true;
    add(d);
  }
  {
    VehicleDef d = MOTO_DEF;
    d.kind = "moped";
    d.licence = "AM";
    d.name = "vehMoped";
    // (geared for its 45 km/h: at the rev limit in top gear it runs 46; class AM: at most 4 kW)
    d.spec = bike("Moped 50 cm³, 45 km/h", 0.12, 24);
    d.powers = {3, 4, 5};
    d.power = 4;
    d.governed = 45;
    d.noMotorway = true;
    add(d);
  }
  {
    VehicleDef d = MOTO_DEF;
    d.kind = "motorcycle125";
    d.licence = "A1";
    d.name = "vehMotorcycle125";
    // (class A1: at most 11 kW)
    d.spec = bike("Leichtkraftrad 125 cm³", 0.3, 7.2);
    d.powers = {11, 13, 15};
    d.power = 15;
    add(d);
  }
  {
    VehicleDef d = MOTO_DEF;
    d.kind = "motorcycle35";
    d.licence = "A2";
    d.name = "vehMotorcycle35";
    // (class A2: at most 35 kW)
    d.spec = bike("Motorrad bis 35 kW", 0.75, 5.8);
    d.powers = {27, 35, 47};
    d.power = 47;
    add(d);
  }
  add(LIGHT_TRUCK_DEF);
  {
    VehicleDef d = LIGHT_TRUCK_DEF;
    d.kind = "lightTruckTrailer";
    d.licence = "C1E";
    d.name = "vehLightTruckTrailer";
    d.trailer = &LIGHT_TRUCK_TRAILER;
    d.caps = VehicleDef::Caps{.rural = 60, .motorway = 80};
    d.zoom = 16;
    d.zoomMax = 36;
    d.power = 190;
    add(d);
  }
  add(MINIBUS_DEF);
  {
    VehicleDef d = MINIBUS_DEF;
    d.kind = "minibusTrailer";
    d.licence = "D1E";
    d.name = "vehMinibusTrailer";
    d.trailer = &MINIBUS_TRAILER;
    d.caps = VehicleDef::Caps{.rural = 80, .motorway = 80};
    d.zoom = 15;
    d.zoomMax = 34;
    d.power = 177;
    add(d);
  }
  {
    VehicleDef d = BUS_DEF;
    d.kind = "busTrailer";
    d.licence = "DE";
    d.name = "vehBusTrailer";
    d.trailer = &BUS_TRAILER;
    d.caps = VehicleDef::Caps{.rural = 80, .motorway = 80};
    d.zoom = 22;
    d.zoomMax = 48;
    d.power = 354;
    add(d);
  }
  // The tram runs on the tracks (sim/tramdrive.ts), not on the road physics: what is given here is
  // what the rest of the game needs of a vehicle – where the driver sits, what the camera keeps to –
  // measured from the middle of its first car.
  {
    VehicleDef d = BUS_DEF;
    d.kind = "tram";
    d.licence = "BOStrab";
    d.name = "vehTram";
    d.model = "tram";
    d.spec = [](const std::string&, double) { return BUS; };
    // (electric: its traction is sim/tramdrive.ts's, nothing to choose)
    d.powers = {};
    d.power = 0;
    d.caps = std::nullopt;
    d.capFault = "speed";
    d.outline = {.front = 4.7, .rear = 4.7, .hw = 1.2};
    d.door = 0;
    d.doorSide = 0;
    d.circles = {-3, 0, 3};
    d.radius = 1.25;
    d.zoom = 22;
    d.zoomMax = 50;
    d.governed = 70;
    d.heavy = true;
    d.noMotorway = true;
    d.long_ = true;
    d.dash = true;
    add(d);
  }
#ifdef FS_VEHICLES_PLANE
  // The light aeroplane flies (sim/flight.ts), it is not driven on the road physics: what is given here is what
  // the rest of the game needs of a vehicle – the camera, its size. Its engine powers are those of TRAINER.
  {
    VehicleDef d = car;
    d.kind = "plane";
    d.licence = "PPL";
    d.name = "vehPlane";
    d.model = "plane";
    d.spec = [](const std::string&, double) { return COMPACT_CAR; };
    d.powers.assign(TRAINER.powers.begin(), TRAINER.powers.end());
    d.power = TRAINER.power;
    d.trailer = nullptr;
    d.caps = std::nullopt;
    d.capFault = "speed";
    d.outline = {.front = 2.6, .rear = 5.6, .hw = TRAINER.span / 2};
    d.axles = {1.5, -0.35};
    d.track = 1.15;
    d.door = -0.4;
    d.doorSide = -1;
    d.circles = {-1.5, 0.5, 2.5, 4.5};
    d.radius = 1.1;
    d.zoom = 15;
    d.zoomMax = 80;
    d.governed = 0;
    d.heavy = false;
    d.noMotorway = true;
    d.long_ = false;
    d.dash = true;
    add(d);
  }
#endif
#ifdef FS_VEHICLES_CRANES
  // The cranes (sim/crane.ts, game/craneplay.ts): the mobile crane and the loader crane are trucks of class C,
  // driven like any to their stand on the construction site and worked there; the tower crane stands on its
  // base – nothing to drive (what is given for it is what the rest of the game needs: the camera, its size).
  {
    VehicleDef d = truck;
    d.kind = "mobileCrane";
    d.licence = "C";
    d.name = "vehMobileCrane";
    d.model = "mobileCrane";
    d.spec = [](const std::string&, double) { return MOBILE_CRANE_SPEC; };
    d.powers = {503};
    d.power = 503;
    d.trailer = nullptr;
    d.caps = VehicleDef::Caps{.rural = 60, .motorway = 80};
    d.capFault = "classSpeed";
    // (the boom's head reaches over the front)
    d.outline = {.front = 6.6, .rear = 4.6, .hw = 1.28};
    d.axles = {1.75, -0.95, -2.55};
    d.track = 1.04;
    d.door = 3.1;
    d.doorSide = -1;
    d.circles = {-5.4, -3.4, -1.3, 0.8, 2.9, 3.35};
    d.radius = 1.28;
    d.zoom = 16;
    d.zoomMax = 40;
    d.governed = 80;
    d.crane = "mobile";
    add(d);
  }
  {
    VehicleDef d = truck;
    d.kind = "loaderCrane";
    d.licence = "C";
    d.name = "vehLoaderCrane";
    d.model = "loaderCrane";
    d.spec = [](const std::string&, double) { return LOADER_TRUCK_SPEC; };
    d.powers = {286};
    d.power = 286;
    d.trailer = nullptr;
    d.caps = VehicleDef::Caps{.rural = 60, .motorway = 80};
    d.capFault = "classSpeed";
    d.outline = {.front = 3.9, .rear = 5.25, .hw = 1.28};
    d.axles = {2.4, -2.4};
    d.track = 1.1;
    d.door = 3.0;
    d.doorSide = -1;
    d.circles = {-2.9, -1.0, 1.0, 2.9, 3.97};
    d.radius = 1.28;
    d.zoom = 14;
    d.zoomMax = 34;
    d.crane = "loader";
    add(d);
  }
#endif
  {
    VehicleDef d = car;
    d.kind = "towerCrane";
    d.licence = "";
    d.name = "vehTowerCrane";
    d.model = "towerCrane";
    d.spec = [](const std::string&, double) { return TRUCK; };
    d.powers = {};
    d.power = 0;
    d.trailer = nullptr;
    d.caps = std::nullopt;
    d.capFault = "speed";
    // (its jib and counter-jib, and its base)
    d.outline = {.front = 50, .rear = 15, .hw = 2.5};
    d.axles = {0, 0};
    d.track = 2.5;
    d.door = 0;
    d.doorSide = 0;
    d.circles = {0};
    d.radius = 2.6;
    d.zoom = 30;
    d.zoomMax = 90;
    d.governed = 0;
    d.heavy = true;
    d.noMotorway = true;
    d.long_ = false;
    d.dash = true;
    d.crane = "tower";
    add(d);
  }
#ifdef FS_VEHICLES_FORKLIFT
  // The forklift (sim/forklift.ts, game/forkliftplay.ts): no licence class – the operator's certificate
  // (DGUV Grundsatz 308-001) –, driven on the practice yard by its own simulation; what is given here is what
  // the rest of the game needs of it (its size, the camera, the sound's engine).
  {
    VehicleDef d = car;
    d.kind = "forklift";
    d.licence = "";
    d.name = "vehForklift";
    d.model = "forklift";
    d.spec = [](const std::string&, double) { return FORKLIFT_SPEC; };
    d.powers = {};
    d.power = 0;
    d.trailer = nullptr;
    d.caps = std::nullopt;
    d.capFault = "speed";
    // (the forks reach ahead of the mast)
    d.outline = {.front = 2.45, .rear = 1.3, .hw = 0.6};
    d.axles = {0.825, -0.825};
    d.track = 0.55;
    d.door = 0.2;
    d.doorSide = -1;
    d.circles = {-0.7, 0.02, 0.7};
    d.radius = 0.58;
    d.zoom = 6;
    d.zoomMax = 16;
    d.governed = 20;
    d.heavy = false;
    d.noMotorway = true;
    d.long_ = false;
    d.dash = true;
    add(d);
  }
#endif
  // On foot there is no vehicle at all (game.ts walks with OnFoot, game/onfoot.ts): what is given here is
  // what the rest of the game needs of "the vehicle" – the camera keeps to the person, the motorway is not
  // for them (§ 18 Abs. 9 StVO).
  {
    VehicleDef d = car;
    d.kind = "pedestrian";
    d.licence = "";
    d.name = "vehOnFoot";
    d.model = "walker";
    d.spec = [](const std::string&, double) { return COMPACT_CAR; };
    d.powers = {};
    d.power = 0;
    d.trailer = nullptr;
    d.caps = std::nullopt;
    d.capFault = "speed";
    d.outline = {.front = 0.25, .rear = 0.25, .hw = 0.3};
    d.axles = {0, 0};
    d.track = 0.2;
    d.door = 0;
    d.doorSide = 0;
    d.circles = {0};
    d.radius = 0.3;
    d.zoom = 3.4;
    d.zoomMax = 12;
    d.governed = 0;
    d.heavy = false;
    d.noMotorway = true;
    d.long_ = false;
    d.dash = true;
    add(d);
  }
  return t;
}

/** (made when it is first asked for: the specs it is made of are there by then, whatever the order of the files) */
const Table& table() {
  static const Table t = build();
  return t;
}

}  // namespace

const VehicleDef& VehicleTable::at(const VehicleKind& kind) const {
  const VehicleDef* d = find(kind);
  if (!d) throw std::out_of_range("VEHICLES: no vehicle \"" + kind + "\"");
  return *d;
}

const VehicleDef* VehicleTable::find(const VehicleKind& kind) const {
  const Table& t = table();
  auto it = t.at.find(kind);
  return it == t.at.end() ? nullptr : &t.all[it->second];
}

const std::vector<VehicleDef>& VehicleTable::all() const { return table().all; }

std::optional<TrailerOutline> trailerOf(const VehicleDef& def) {
  if (def.trailer) return trailerOutline(*def.trailer);
  return std::nullopt;
}

double powerOf(const VehicleDef& def, const std::map<std::string, double>& chosen) {
  auto p = chosen.find(def.kind);
  return p != chosen.end() && std::find(def.powers.begin(), def.powers.end(), p->second) != def.powers.end() ? p->second : def.power;
}

std::optional<VehicleKind> vehicleKind(const std::string& v) {
  if (std::find(VEHICLE_KINDS.begin(), VEHICLE_KINDS.end(), v) != VEHICLE_KINDS.end()) return v;
  return std::nullopt;
}

}  // namespace fs

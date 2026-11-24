// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/vehicles.ts @ v1.7.7
/**
 * The vehicles one can learn with, one for each licence class: what each is
 * (its physics, its picture, its trailer), how big it is, and what the
 * rules ask of it beyond the signs –
 *
 *  - outside built-up areas and on the motorway it may be slower than the
 *    signs allow: § 3 Abs. 3 StVO (trucks over 7.5 t and every vehicle with
 *    a trailer but a car's: 60 km/h, buses and cars with a trailer: 80),
 *    § 18 Abs. 5 StVO (trucks, cars with a trailer: 80, coaches: 100);
 *  - over 3.5 t it turns right in town at walking pace (§ 9 Abs. 6 StVO);
 *  - a tractor built for 40 km/h may not use the motorway (§ 18 Abs. 1 StVO).
 */
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "game/spawnplace.h"
#include "sim/vehicle/types.h"

namespace fs {

struct TrailerSpec;
struct TrailerOutline;

/**
 * 'car' | 'carTrailer' | 'moped' | 'motorcycle125' | 'motorcycle35' | 'motorcycle' | 'lightTruck' |
 * 'lightTruckTrailer' | 'truckSolo' | 'truck' | 'semi' | 'minibus' | 'minibusTrailer' | 'bus' | 'busTrailer' |
 * 'tractorSlow' | 'tractor' | 'tram' | 'plane' | 'mobileCrane' | 'loaderCrane' | 'towerCrane' | 'forklift' |
 * 'pedestrian'
 */
using VehicleKind = std::string;
/** in the order of the licence classes: B, BE, AM, A1, A2, A, C1, C1E, C, CE (drawbar, articulated), D1, D1E, D, DE, L, T – the tram, the aeroplane, the cranes, and on foot */
inline const std::vector<VehicleKind> VEHICLE_KINDS = {"car",  "carTrailer",     "moped", "motorcycle125", "motorcycle35", "motorcycle",  "lightTruck", "lightTruckTrailer",
                                                       "truckSolo", "truck",     "semi",  "minibus",       "minibusTrailer", "bus",       "busTrailer", "tractorSlow",
                                                       "tractor",   "tram",      "plane", "mobileCrane",   "loaderCrane",  "towerCrane",  "forklift",   "pedestrian"};

/** the kinds of vehicle that are driven on the road (not the tram on its tracks, not the aeroplane, not the tower crane on its base, not on foot) */
inline const std::vector<VehicleKind> ROAD_VEHICLE_KINDS = [] {
  std::vector<VehicleKind> out;
  for (const VehicleKind& k : VEHICLE_KINDS)
    if (k != "tram" && k != "pedestrian" && k != "plane" && k != "towerCrane" && k != "forklift") out.push_back(k);
  return out;
}();

/**
 * 'car' | 'truck' | 'truckLight' | 'truckHeavy' | 'semi' | 'bus' | 'minibus' | 'tractor' | 'motorcycle' | 'tram' |
 * 'walker' | 'plane' | 'towerCrane' | 'mobileCrane' | 'loaderCrane' | 'forklift'
 */
using ModelKind = std::string;

struct VehicleDef {
  VehicleKind kind;
  /** the licence class it is learnt for */
  std::string licence;
  /** i18n key of its name */
  std::string name;
  ModelKind model;
  /** its physics, for the gearbox ('manual' | 'assist' | 'automatic') and the engine power (PS) the player chose */
  std::function<VehicleSpec(const std::string& transmission, double power)> spec;
  /** the engine powers one may choose for it (PS) – within what its licence class allows – and the one it comes with */
  std::vector<double> powers;
  double power = 0;
  /** (null: it pulls none) */
  const TrailerSpec* trailer = nullptr;
  /** its outline in the plan from the model origin (the ground point below the middle of the wheelbase) */
  Outline outline;
  /** for walking round it: the axles (+ ahead) and half the track over the tyres; the driver's door, ahead of the origin, and its side (−1 left, 1 right; 0: none – a motorcycle) */
  std::vector<double> axles;
  double track = 0;
  double door = 0;
  double doorSide = 0;
  /** circles along its axis that stand for it against walls and posts (metres from the origin, − = ahead), and their radius */
  std::vector<double> circles;
  double radius = 0;
  /** how far behind it the camera stands (and at most) */
  double zoom = 0;
  double zoomMax = 0;
  /** at most this outside built-up areas and on the motorway (km/h), whatever the signs say – none: the signs alone */
  struct Caps {
    double rural = 0;
    double motorway = 0;
  };
  std::optional<Caps> caps = std::nullopt;
  /** the fault when it is faster than that */
  std::string capFault;
  /** its top speed by construction or governor (km/h; 0: none) */
  double governed = 0;
  /** over 3.5 t: walking pace when turning right in town */
  bool heavy = false;
  /** not allowed on the motorway */
  bool noMotorway = false;
  /** long: ways avoid turns that double back, the camera looks from higher up (the original's `long`, a word C++ keeps for itself) */
  bool long_ = false;
  /** the instruments are small or missing in its cockpit: the dashboard strip stays on */
  bool dash = false;
  /** a crane (game/craneplay.ts): its free drive is the work on the construction site ('tower' | 'mobile' | 'loader') */
  std::optional<std::string> crane = std::nullopt;
};

/**
 * `VEHICLES[kind]`: the vehicles by their kind.
 *
 * Not every kind has its vehicle in every build: one whose physics come from a part of the
 * simulation that is not there (the cranes: sim/crane.h; the forklift: sim/forklift.h; the aeroplane: sim/flightspec.h) is left
 * out – `find` then gives null for its kind, `at` throws.
 */
class VehicleTable {
 public:
  constexpr VehicleTable() = default;
  /** `VEHICLES[kind]` (std::out_of_range: no such vehicle) */
  const VehicleDef& at(const VehicleKind& kind) const;
  /** … or null */
  const VehicleDef* find(const VehicleKind& kind) const;
  /** `Object.values(VEHICLES)`: all of them, in the order of the original's table */
  const std::vector<VehicleDef>& all() const;
};
inline constexpr VehicleTable VEHICLES;

/** The outline of the trailer of a vehicle (null: it has none). */
std::optional<TrailerOutline> trailerOf(const VehicleDef& def);

/** The engine power (PS) to drive a vehicle with: the one chosen for it, if it may have it, else the one it comes with. */
double powerOf(const VehicleDef& def, const std::map<std::string, double>& chosen);

/** A vehicle kind from a setting or a link, or null. */
std::optional<VehicleKind> vehicleKind(const std::string& v);

}  // namespace fs

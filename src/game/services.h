// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/services.ts @ v1.7.7
/**
 * What happens at a fuel station, in a car wash and at the depot – the
 * rules of it, without anything drawn (game/serviceworld.h puts it into
 * the game, render/services.h and render/loading.h draw it):
 *
 *  - filling up: the nozzle goes into the filler, the fuel runs and the
 *    litres and the price count up until the tank is full or the driver
 *    stops it, the nozzle goes back.  It stops at once if the engine is
 *    started or the vehicle moves.
 *  - washing: the gantry's program – foam, brushes, rinsing, drying, each a
 *    pass over the car – takes the dirt off; it stops if the car moves.
 *  - roadside help: with the tank empty, a can of fuel is brought.
 *  - the dirt: it grows with the distance driven – faster on a wet road, on
 *    snow and slush, and much faster off the road.
 *  - the load of a truck: how many pallets and how heavy, and where in the
 *    box (for the physics: sim/payload.h).
 *  - the depot: a place beside the road where a warehouse with a dock fits
 *    behind the truck.
 */
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "sim/fuel.h"
#include "sim/payload.h"

namespace fs {

/** The vehicle as the services see it from one frame to the next. */
struct StandState {
  /** standing: below 0.25 m/s */
  bool standing = true;
  bool engineOn = false;
};

// ------------------------------------------------------------------ filling up

/** How long the nozzle takes from its holster into the filler (and back), s. */
inline constexpr double NOZZLE_TIME = 1.4;

/**
 * A fill-up as it goes: 'out' (the nozzle on its way into the filler),
 * 'run' (the fuel running), 'back' (the nozzle on its way back), 'done'.
 * `reason` says why the fuel stopped: 'full', 'stopped' (by the driver),
 * 'moved' or 'engine' (both: at once, the nozzle is pulled out).
 */
class Refuelling {
 public:
  /** 'out' | 'run' | 'back' | 'done' */
  std::string phase = "out";
  /** seconds in the phase */
  double t = 0;
  std::string reason;
  FillUp fill;
  const std::string fuel;

  Refuelling(std::string fuel, double price, double flow) : fill(price, flow), fuel(std::move(fuel)) {}

  /** Where the nozzle is: 0 in its holster … 1 in the filler. */
  double nozzle() const;
  /** The driver stops the pump (or something else does: `reason`). */
  void stop(const std::string& reason = "stopped");
  void step(double dt, FuelTank& tank, const StandState& s);
  bool done() const { return phase == "done"; }
};

// ------------------------------------------------------------------ washing

struct WashStep {
  /** 'foam' | 'brush' | 'rinse' | 'dry' */
  const char* id;
  double time;
  double from;
  double to;
  double clean;
};

/** The program of the gantry: each step a pass from one end of the car to the other (−1 … 1; 1 is where it waits). */
inline constexpr std::array<WashStep, 4> WASH_STEPS = {{
    {"foam", 9, 1, -1, 0.15},
    {"brush", 13, -1, 1, 0.65},
    {"rinse", 8, 1, -1, 0.2},
    {"dry", 8, -1, 1, 0},
}};

/** A wash as it goes, step by step; `aborted`: the car moved (the program stops where it is). */
class Washing {
 public:
  int step = 0;
  double t = 0;
  bool aborted = false;
  /** the dirt when it began */
  const double dirt0;

  explicit Washing(double dirt);

  bool done() const { return aborted || step >= static_cast<int>(WASH_STEPS.size()); }
  /** The step under way ('' when done). */
  std::string stepId() const;
  /** Where the gantry is: −1 … 1 along the car (1 where it waits). */
  double at() const;
  /** How far the whole program is (0 … 1). */
  double share() const;
  /** The dirt left on the car now. */
  double dirt() const;
  void update(double dt, const StandState& s);
};

// ------------------------------------------------------------------ roadside help

/** How long the help takes to come with its can (s), and how much it brings (l). */
struct Help {
  double time, litres;
};
inline constexpr Help HELP = {8, 10};

// ------------------------------------------------------------------ dirt

/**
 * How much dirtier a vehicle gets over `metres` (the level runs from 0 clean
 * to 1 filthy): a little on a dry road (some 40 km to half dirty), much
 * more on a wet one (`wet` 0 … 1: spray), on snow and slush (`snow` 0 … 1),
 * and most off the road (`offRoad`: the share of the wheels on grass or
 * earth – a couple of hundred metres across a field do it).
 */
double dirtGrowth(double metres, double wet, double offRoad, double snow);

// ------------------------------------------------------------------ where something is, seen from the driver's seat

/**
 * Which way a place lies from a vehicle, in eight parts of the circle: 0
 * ahead, 1 ahead on the right, 2 to the right, 3 behind on the right, 4
 * behind, 5 behind on the left, 6 to the left, 7 ahead on the left.
 */
int bearingSector(double x, double z, double yaw, double tx, double tz);

// ------------------------------------------------------------------ the load of a truck

/**
 * The kinds of truck that are loaded at the start of a drive: the 7.5-tonner
 * (C1) and the 12-tonner (C) on their own.  (With a trailer – C1E, CE – the
 * trailer is coupled right behind the box's doors: no forklift or dock gets
 * at them.)
 */
const std::vector<std::string>& LOADED_KINDS();

/** A pallet to be loaded: how tall its goods stand, what they are (0 cartons, 1 sacks, 2 drums, 3 crates), its weight. */
struct PalletPlan {
  double height = 0;
  int kind = 0;
  double mass = 0;
};

struct BoxInside {
  double x0, x1, floor, front, rear;
};
/** The inside of a truck's box as render/truck.h builds it (truck frame: z back from the middle of the wheelbase). */
BoxInside boxInside(double wheelbase, double width);

struct LoadPlan {
  std::vector<PalletPlan> pallets;
  Payload payload;
};
/**
 * What a truck is loaded with: 4 to 6 pallets of 120 to 400 kg, within
 * what it may carry (`maxLoad` kg) – and the load for the physics: its
 * weight, and where it stands (the pallets fill the box from the front, two
 * side by side).  `rand`: numbers 0 … 1.
 */
LoadPlan planLoad(double wheelbase, double width, double maxLoad, const std::function<double()>& rand);

// ------------------------------------------------------------------ the depot

/** What the depot search asks of the world. */
struct DepotProbe {
  /** is a point on a road or a pavement? */
  std::function<bool(double x, double z)> road;
  /** would the truck, standing at (x, z) turned to yaw, touch anything (a building, a tree, a post)? */
  std::function<bool(double x, double z, double yaw)> blocked;
  /** the ground at a point (NaN where nothing is loaded) */
  std::function<double(double x, double z)> ground;
};

/** Where the truck stands at the depot: its model origin and heading – its rear to the warehouse, its front to the road. */
struct DepotSpot {
  double x, z, yaw;
};

/** The warehouse behind the truck: how far its wall stands behind the truck's rear (render/loading.h DOCK_GAP), its width and depth. */
struct DepotSize {
  double gap, wid, dep;
};
inline constexpr DepotSize DEPOT = {1.7, 14, 11};

/**
 * A place for the depot near where a truck stands in its lane (x, z, yaw;
 * `front`/`rear`: its length ahead of and behind its origin): beside the
 * road – on the right first, a little further along the road if need be –
 * the truck standing off the road with its nose 2 m from the road's edge,
 * the warehouse behind it.  The truck's place and the warehouse's must be
 * off the road, free of buildings, trees and posts, and the ground under
 * them no more than 1.1 m above or below the truck's – below the dock's
 * floor – (and not steep under the truck).  Null where there is no such place.
 */
std::optional<DepotSpot> findDepot(double x, double z, double yaw, double front, double rear, const DepotProbe& probe);

}  // namespace fs

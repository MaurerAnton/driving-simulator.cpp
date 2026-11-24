// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/cranetasks.ts @ v1.7.7
/**
 * The construction site and the work done on it with a crane: where
 * everything stands, the loads lying about, the places they are to go –
 * and the work itself: a short guided start, then real tasks one after
 * another ("move this pallet of bricks onto the truck", "bring the steel
 * beam up to the roof", "set the container there"), each judged by how
 * precisely the load was set down, how much it swung, how gently it landed,
 * how long it took and whether anything was hit or overloaded.
 *
 * The site lies on the practice yard of the map (its frame: game/ce.ts,
 * YardFrame – u along the yard, w across it, heights above the site's base
 * height `y0`): a building of three storeys going up, with its scaffolding
 * and a loading platform, the tower crane beside it, storage areas, the
 * site road with a delivery truck, the depot the mobile and the loader
 * crane start from, and the stands marked out for them.
 *
 * Pure logic, no drawing: render/cranesite.ts draws the site, the crane's
 * physics is sim/crane.ts, game/craneplay.ts runs it all.
 */
#pragma once

#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "i18n/index.h"
#include "sim/crane.h"

namespace fs {

// ------------------------------------------------------------------------------------ the loads

/** 'bricks' | 'beam' | 'container' | 'formwork' | 'mesh' */
using LoadKind = std::string;

/** A kind of load: its weight, its half-sizes (x along its length, y up, z across), the slings it hangs on, its area in the wind. */
struct LoadDef {
  LoadKind kind;
  /** i18n key of its name */
  std::string name;
  double t = 0;
  double hx = 0;
  double hy = 0;
  double hz = 0;
  double sling = 0;
  double area = 0;
};

inline const std::map<std::string, LoadDef> LOAD_DEFS = {
    // a pallet of sand-lime bricks: 1.2 × 1.0 m, 1.15 m high, on a four-leg chain sling
    {"bricks", {.kind = "bricks", .name = "craneLoad.bricks", .t = 1.25, .hx = 0.6, .hy = 0.575, .hz = 0.5, .sling = 1.6, .area = 1.4}},
    // a steel beam HEB 300, 8 m: 117 kg per metre, on a two-leg sling
    {"beam", {.kind = "beam", .name = "craneLoad.beam", .t = 0.94, .hx = 4.0, .hy = 0.15, .hz = 0.15, .sling = 2.6, .area = 2.4}},
    // a 20-foot material container
    {"container", {.kind = "container", .name = "craneLoad.container", .t = 2.4, .hx = 3.03, .hy = 1.295, .hz = 1.22, .sling = 3.8, .area = 15.7}},
    // a stack of formwork panels, 2.7 × 2.4 m
    {"formwork", {.kind = "formwork", .name = "craneLoad.formwork", .t = 1.4, .hx = 1.35, .hy = 0.35, .hz = 1.2, .sling = 2.4, .area = 1.9}},
    // a bundle of reinforcing mesh, 5.0 × 2.15 m
    {"mesh", {.kind = "mesh", .name = "craneLoad.mesh", .t = 1.05, .hx = 2.5, .hy = 0.2, .hz = 1.075, .sling = 2.8, .area = 2.0}},
};

// ------------------------------------------------------------------------------------ the site

/** A rectangle of the site (u0 … u1, w0 … w1). */
struct SiteRect {
  double u0 = 0;
  double u1 = 0;
  double w0 = 0;
  double w1 = 0;
};

/** A solid of the site: a rectangle in the plan from y0 to y1 above the base height. */
struct SiteSolid : SiteRect {
  double y0 = 0;
  double y1 = 0;
};

/** A surface something can be set down on: a rectangle at a height above the base height. */
struct SitePlatform : SiteRect {
  double y = 0;
};

/** A pose on the site (u, w; yaw: 0 facing −w, positive to the left, as on the map). */
struct SitePose {
  double u = 0;
  double w = 0;
  double yaw = 0;
};

/** facing along +u (the site road runs that way) */
inline const double SITE_EAST = -M_PI / 2;

/** The building going up: three storeys and a roof, 16 × 12 m. */
struct Building : SiteRect {
  /** the slabs' tops (ground floor on its plinth, first, second floor, roof) */
  std::array<double, 4> floors;
  double slab = 0;
  /** the stair core at the north-west corner, a storey higher than the roof */
  struct Core : SiteRect {
    double top = 0;
  } core;
  /** the loading platform sticking out at the second floor */
  SitePlatform platform;
  /** the scaffolding along the south and the east face, and its top */
  std::vector<SiteRect> scaffold;
  double scaffoldTop = 0;
  /** the guard rails round the roof (where there is no scaffolding), their top */
  std::vector<SiteRect> rails;
  double railTop = 0;
};

inline Building makeBuilding() {
  Building b;
  b.u0 = 8;
  b.u1 = 24;
  b.w0 = -6;
  b.w1 = 6;
  b.floors = {0.3, 3.3, 6.3, 9.3};
  b.slab = 0.25;
  b.core.u0 = 8;
  b.core.u1 = 11.5;
  b.core.w0 = -6;
  b.core.w1 = -2.5;
  b.core.top = 12.3;
  b.platform.u0 = 14;
  b.platform.u1 = 17.2;
  b.platform.w0 = -8.6;
  b.platform.w1 = -6;
  b.platform.y = 6.3;
  b.scaffold = {{.u0 = 8, .u1 = 25.3, .w0 = 6.3, .w1 = 7.3}, {.u0 = 24.3, .u1 = 25.3, .w0 = -6, .w1 = 6.3}};
  b.scaffoldTop = 10.4;
  b.rails = {{.u0 = 11.5, .u1 = 24, .w0 = -6, .w1 = -5.9}, {.u0 = 8, .u1 = 8.1, .w0 = -2.5, .w1 = 6}};
  b.railTop = 10.4;
  return b;
}
inline const Building BUILDING = makeBuilding();

/** The tower crane: the middle of its base, its cross base and ballast, its mast. */
struct TowerSite {
  double u, w, yaw, base, ballastTop, mast, mastTop;
};
inline const TowerSite TOWER_SITE = {.u = -1, .w = -1, .yaw = 0, .base = 2.5, .ballastTop = 2.6, .mast = 0.85, .mastTop = 30};

/** The delivery truck on the site road: its flatbed (a rectangle and its floor's height) and its cab. */
struct Delivery {
  SitePlatform bed;
  SiteSolid cab;
  SiteSolid chassis;
};
inline SitePlatform sitePlatform(double u0, double u1, double w0, double w1, double y) {
  SitePlatform p;
  p.u0 = u0;
  p.u1 = u1;
  p.w0 = w0;
  p.w1 = w1;
  p.y = y;
  return p;
}
inline SiteSolid siteSolid(double u0, double u1, double w0, double w1, double y0, double y1) {
  SiteSolid s;
  s.u0 = u0;
  s.u1 = u1;
  s.w0 = w0;
  s.w1 = w1;
  s.y0 = y0;
  s.y1 = y1;
  return s;
}
inline const Delivery DELIVERY = {.bed = sitePlatform(5, 11.5, -21.7, -19.3, 1.25), .cab = siteSolid(11.8, 14.2, -21.75, -19.25, 0, 3.1), .chassis = siteSolid(4.8, 11.8, -21.4, -19.6, 0.3, 1.25)};

/** The areas of the site laid out on the ground. */
struct Areas {
  SiteRect depot, road, storage, west, site;
};
inline const Areas AREAS = {
    .depot = {.u0 = -74, .u1 = -50, .w0 = -27, .w1 = -8},
    .road = {.u0 = -74, .u1 = 46, .w0 = -24, .w1 = -17},
    .storage = {.u0 = -30, .u1 = -6, .w0 = 3, .w1 = 21},
    .west = {.u0 = -44, .u1 = -20, .w0 = -16.5, .w1 = -9},
    .site = {.u0 = -20, .u1 = 40, .w0 = -16.5, .w1 = 24},
};

/** The stands marked out for the mobile and the loader crane, and where the vehicle is to stand in them. */
struct StandTol {
  double u = 0;
  double w = 0;
  double yaw = 0;
};
struct Stand {
  SiteRect area;
  SitePose pose;
  StandTol tol;
};
/** (keyed 'mobile' | 'loader') */
inline const std::map<std::string, Stand> STANDS = {
    {"mobile", {.area = {.u0 = -15, .u1 = 1, .w0 = -16.5, .w1 = -9.5}, .pose = {.u = -7, .w = -13, .yaw = SITE_EAST}, .tol = {.u = 2.5, .w = 1.5, .yaw = 0.21}}},
    {"loader", {.area = {.u0 = -39, .u1 = -29, .w0 = -22.3, .w1 = -18.7}, .pose = {.u = -34, .w = -20.5, .yaw = SITE_EAST}, .tol = {.u = 2.5, .w = 1.0, .yaw = 0.21}}},
};

/** Where the mobile and the loader crane start: in the depot, on the site road, facing the site. */
inline const SitePose DEPOT_START = {.u = -62, .w = -20.5, .yaw = SITE_EAST};

/** The site office and the store at the depot (containers). */
inline const std::vector<SiteSolid> OFFICES = {
    siteSolid(-69, -63, -13.3, -10.9, 0, 2.6),
    siteSolid(-61, -55, -13.3, -10.9, 0, 2.6),
};

/** a place on the loader crane's flatbed (carrier frame x, z) and a turn against the truck */
struct BedPlace {
  double x = 0;
  double z = 0;
  double yaw = 0;
};
/** a middle on the loader crane's flatbed (carrier frame x, z) */
struct BedPoint {
  double x = 0;
  double z = 0;
};

/** A load lying on the site at the start: its kind, its place (on the ground, on timber bearers, or on the loader crane's own flatbed). */
struct SiteLoad {
  std::string id;
  LoadKind kind;
  double u = 0;
  double w = 0;
  double yaw = 0;
  /** on timber bearers (12 cm up) */
  bool bearers = false;
  /** on the loader crane's flatbed: its place there (carrier frame x, z) and its turn against the truck */
  std::optional<BedPlace> bed = std::nullopt;
};

/** A place a load is to go: on the ground, on a slab, on the truck's bed, on the loader crane's own bed. */
struct SiteZone {
  std::string id;
  /** its middle and how it is turned (the load must lie the same way) */
  double u = 0;
  double w = 0;
  double yaw = 0;
  /** half its size: the load's middle must come to lie within */
  double hu = 0;
  double hw = 0;
  /** the surface it lies on: the ground (nullopt: 'ground'), or a height above the base height */
  std::optional<double> y = std::nullopt;
  /** on the loader crane's own flatbed (its middle in the carrier frame: x, z) */
  std::optional<BedPoint> bed = std::nullopt;
  /** timber bearers lie there for the load to go on */
  bool bearers = false;
};

/** A task: which load goes where, what it is called, how long it should take. */
struct SiteTask {
  std::string load;
  std::string zone;
  /** i18n key of the instruction */
  std::string text;
  /** a good time for it (s) */
  double par = 0;
};

/** What a crane has to do on the site: its loads, the places they go, the tasks in order (the first one is the guided one). */
struct CraneWorkPlan {
  CraneType type;
  std::vector<SiteLoad> loads;
  std::vector<SiteZone> zones;
  std::vector<SiteTask> tasks;
};

/** loads lying about that nobody moves (they make the site, and are in the way) */
extern const std::vector<SiteLoad> SITE_LOADS;

/** (keyed 'tower' | 'mobile' | 'loader') */
extern const std::map<std::string, CraneWorkPlan> PLANS;

/** The timber bearers the mesh is to go on (and the beams lie on): 12 cm. */
inline constexpr double BEARERS = 0.12;

// ------------------------------------------------------------------------------------ in the world

/** How the site lies in the world: its frame (game/ce.ts YardFrame does this) and its base height. */
class SitePlacement {
 public:
  virtual ~SitePlacement() = default;
  virtual std::array<double, 2> toWorld(double u, double w) const = 0;
  virtual std::array<double, 2> toLocal(double x, double z) const = 0;
  virtual double yawToWorld(double yaw) const = 0;
  virtual double yawToLocal(double yaw) const = 0;
};

/** A load in the world: where its bottom's middle is, how it is turned, whether it hangs on the hook or rides on the loader's bed. */
struct WorldLoad {
  std::string id;
  LoadDef def;
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  /** others' loads nobody moves */
  bool fixed = false;
  bool hooked = false;
  /** on the loader crane's own flatbed: its place there (carrier frame) and its turn against the truck – it goes where the truck goes */
  std::optional<BedPlace> bed = std::nullopt;
  /** on timber bearers (drawn under it) */
  bool bearers = false;
};

/** A place a load is to go, in the world: its middle, turn, half-sizes, the height of what it lies on. */
struct WorldZone {
  std::string id;
  double x = 0;
  double z = 0;
  double yaw = 0;
  double hu = 0;
  double hw = 0;
  /** the height of the surface (NaN: the ground, wherever it is) */
  double y = 0;
  std::optional<BedPoint> bed = std::nullopt;
  /** timber bearers lie there (see bearersOf) */
  bool bearers = false;
};

/** `Box3 & { y: number }`: a solid with the height of the surface on it */
struct CraneBoxY : CraneBox3 {
  double y = 0;
};

/**
 * The timber bearers lying at a place (two, across the load's length, as
 * under the loads on bearers), as solids of the world reaching from the
 * ground to their top: (x, z) and the ground's height there.
 */
std::vector<CraneBoxY> bearersOf(const WorldZone& zone, const LoadDef& def, double ground);

/** A rectangle of the site as a world box (axis-aligned: the yard is turned by quarters). */
CraneBox3 worldBox(const SitePlacement& p, const SiteRect& r, double y0, double y1);

/** The half-size in the world's plan (x, z) of a load turned by yaw. */
std::array<double, 2> loadFootprint(const LoadDef& def, double yaw);

/** The fixed geometry of the site in the world: the surfaces loads can be set on and the solids they stop at. */
struct SiteWorld {
  std::vector<CraneBoxY> platforms;
  std::vector<CraneBox3> solids;
};

/**
 * The site's surfaces and solids in the world.  `tower`: the tower crane
 * stands there as part of the site (when it is not the player's own it is
 * in the way all the same – it is always there).
 */
SiteWorld siteWorld(const SitePlacement& p, double y0);

/** The loads a crane finds on the site: its own (to be moved) and the others lying about (fixed). */
std::vector<WorldLoad> siteLoads(const CraneType& type, const SitePlacement& p, const std::function<double(double, double)>& ground);

/** The places of a crane's tasks in the world. */
std::vector<WorldZone> siteZones(const CraneType& type, const SitePlacement& p, double y0);

/** Where a point lies in a zone's own frame (along its u, across its w). */
struct ZonePoint {
  double du = 0;
  double dw = 0;
};
ZonePoint inZone(const WorldZone& zone, double x, double z);

/** The difference between two turns of a load, folded: a load turned round (180°) lies the same way. */
double yawOff(double a, double b);

// ------------------------------------------------------------------------------------ the work

/** The steps of the guided start, in order (the tower crane begins at 'over'):
 *  'drive' | 'park' | 'operate' | 'beams' | 'jacks' | 'level' | 'raise' | 'over' | 'lower' | 'attach' | 'lift' | 'move' | 'setDown' | 'release' */
using GuideStep = std::string;

/** (keyed 'tower' | 'mobile' | 'loader') */
extern const std::map<std::string, std::vector<GuideStep>> GUIDE;

/** a point in the world */
struct CranePoint {
  double x = 0;
  double y = 0;
  double z = 0;
};

/** What the work needs to know of the crane and its vehicle, each step. */
struct CraneSnapshot {
  double time = 0;
  /** the vehicle's origin (world x, z), heading, speed (m/s); the parking brake; the crane being worked */
  double vx = 0;
  double vz = 0;
  double yaw = 0;
  double speed = 0;
  bool handbrake = false;
  bool operating = false;
  /** the outrigger beams' least extension (0 … 1), the pads all down, the carrier lifted off its wheels (the mobile crane) or the pads bearing (the loader crane), its slope (deg), the crane folded for the road */
  double beams = 0;
  bool padsDown = false;
  bool onOutriggers = false;
  double incline = 0;
  bool stowed = false;
  /** the mobile crane's boom angle (deg); the loader crane unfolded */
  double luff = 0;
  bool unfolded = false;
  /** the head the rope hangs from, the hook (world), the load on it */
  CranePoint head;
  CranePoint hook;
  std::optional<std::string> hooked = std::nullopt;
  /** the hook or the load stands on something; the clearance below it; how far it swings off the plumb line (m) */
  bool resting = false;
  double clearance = 0;
  double swing = 0;
  Lmi lmi;
  CraneEvents events;
  /** it tipped over */
  bool tipped = false;
};

/** How one task went. */
struct TaskResult {
  /** i18n key of the task and the load's name */
  std::string text;
  std::string load;
  /** seconds, from its start to the load let go in place */
  double time = 0;
  double par = 0;
  /** how far from the mark the load came to stand (m), and how far turned (deg) */
  double offset = 0;
  /** the most it swung on the way (m), and how fast it came down at the end (m/s) */
  double swing = 0;
  double landing = 0;
  /** the highest the limiter showed while it hung (share of the chart), and whether it cut off */
  double peak = 0;
  bool overload = false;
  /** what the load ran into (times) */
  int hits = 0;
  /** 0 … 100, and 0 … 3 stars */
  double score = 0;
  int stars = 0;
};

/** `Record<string, number | string>` in the order written (the same type as sim/rules.h ViolationData) */
using CraneFaultData = std::vector<std::pair<std::string, std::variant<double, std::string>>>;

/** A fault for the instructor (sim/rules.ts noteAt): its id, how grave ('hint' | 'minor' | 'major' | 'fail'), its numbers. */
struct CraneFault {
  std::string id;
  std::string severity;
  CraneFaultData data;
};

/** what the work says after a step */
struct WorkEvents {
  std::vector<CraneFault> faults;
  /** a step of the guide is done (the next is in `step`) */
  bool stepDone = false;
  /** a task is done (its result is the last of `results`) */
  bool taskDone = false;
  /** everything is done */
  bool allDone = false;
};

/** `Pick<TaskResult, 'time' | 'par' | 'offset' | 'swing' | 'landing' | 'overload' | 'hits'>` */
struct TaskJudged {
  double time = 0;
  double par = 0;
  double offset = 0;
  double swing = 0;
  double landing = 0;
  bool overload = false;
  int hits = 0;
};
struct TaskScore {
  double score = 0;
  int stars = 0;
};
/** The score of a task from how it went (see TaskResult). */
TaskScore scoreTask(const TaskJudged& r);

/** how close the hook has to be over a load to sling it (horizontally, m), and above its top (m) */
inline constexpr double ATTACH_REACH = 1.0;
inline constexpr double ATTACH_ABOVE = 0.9;

/** what the player is to do now (CraneTasks::instruction) */
struct CraneInstruction {
  std::string key;
  TrParams data;
  std::string hint;
};

/**
 * The work with a crane on the site: the guided start, then the tasks one
 * by one, judged as they go.  The game tells it each step how the crane
 * and its vehicle are (`update`), asks which load the hook can take
 * (`reachable`) and tells it when a load is let go (`placed`).
 */
class CraneTasks {
 public:
  const CraneType type;
  const CraneWorkPlan& plan;
  /** (the loads are changed by the game – where they lie, hooked – the vector itself is not resized) */
  std::vector<WorldLoad> loads;
  const std::vector<WorldZone> zones;
  /** the guided start ('guide'), the tasks, all done ('guide' | 'tasks' | 'done') */
  std::string stage = "guide";
  /** the guide's step now (null once it is done) */
  std::optional<GuideStep> step;
  /** the task now (the guide is the first) */
  int task = 0;
  std::vector<TaskResult> results;
  /** where the stand is (world) for the mobile and the loader crane, and how the vehicle is to stand there */
  struct WorldStand {
    double x = 0;
    double z = 0;
    double yaw = 0;
    StandTol tol;
  };
  const std::optional<WorldStand> stand;

  CraneTasks(const CraneType& type, const SitePlacement& place, const std::function<double(double, double)>& ground, double y0);

  /** The task now (null once all are done). */
  const SiteTask* current() const;

  /** The load of the task now, and the place it goes. */
  struct Target {
    WorldLoad* load;
    const WorldZone* zone;
  };
  std::optional<Target> target();

  /** The load the hook can take: one of this crane's (not fixed), the hook over its middle and a little above it. */
  WorldLoad* reachable(const CranePoint& hook, double hookDrop);

  /** The vehicle stands in its stand as it should (the mobile and the loader crane). (`Pick<CraneSnapshot, 'vx' | 'vz' | 'yaw'>`) */
  bool inStand(const CraneSnapshot& s) const;

  /** The load of the task now lies where it should, let go: its middle within the zone, turned its way. */
  bool inPlace(const WorldLoad& load, const WorldZone& zone) const;

  /** How far a load lies from its mark (m). */
  double offset(const WorldLoad& load, const WorldZone& zone) const;

  /** One step of the work: the guide moves on, the task is watched and judged, faults are told. */
  WorkEvents update(const CraneSnapshot& s);

  /**
   * What the player is to do now: the instruction (i18n key and its
   * values) and, under it, a hint for the task now as it stands.
   */
  CraneInstruction instruction(const CraneSnapshot& s);

 private:
  int stepIndex_ = 0;
  /** how the task now is going */
  struct Cur {
    double start = 0;
    double swing = 0;
    double landing = 0;
    double peak = 0;
    bool overload = false;
    int hits = 0;
    bool lifted = false;
  };
  Cur cur_;
  std::unordered_map<std::string, double> faultT_;

  void fault(std::vector<CraneFault>& out, double time, const std::string& id, const std::string& severity, const CraneFaultData& data = {}, double every = 6);
  /** Is a step of the guide done? */
  bool stepDone(const GuideStep& step, const CraneSnapshot& s, const std::optional<Target>& tg) const;
  /** whether a point is over a zone (its middle within it) */
  bool over(const WorldZone& zone, double x, double z) const;
  /** A hint for the task as it stands (i18n key; '' none). */
  std::string hint(const CraneSnapshot& s, const Target& tg) const;
};

}  // namespace fs

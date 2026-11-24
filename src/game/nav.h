// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/nav.ts @ v1.7.7
/** Route guidance ("in 150 m turn left") and the lesson / exam definitions. */
#pragma once

#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/geom.h"
#include "game/vehicles.h"
#include "world/lanes.h"
#include "world/routegraph.h"
#include "world/world.h"

namespace fs {

struct NavHint {
  /** i18n key of the manoeuvre: navLeft, navRight, navStraight, navUturn, navRoundabout, navFollow, navMotorway, routeDone */
  std::string what = "navFollow";
  /** metres to the manoeuvre (-1 = none) */
  double dist = -1;
  /** roundabout exit number */
  int n = 0;
  /** -1 left, 0 straight/none, 1 right – for the arrow */
  int arrow = 0;
  /** get in lane: -1 = move one lane to the left, 1 = to the right, 0 = this lane is fine */
  int lane = 0;
  bool done = false;
};

/**
 * The paths through the junctions for a list of ways (world/routegraph.h)
 * that begins behind the lane `from` – choosing, where a road has several
 * lanes, the ones from which the way goes on with the fewest changes of
 * lane.  Null when two ways that follow each other are not connected.
 */
std::optional<std::vector<Connector*>> connectorsAlong(const LaneGraph& graph, const std::vector<int>& ways, const Lane& from);

class Navigator {
 public:
  /** the paths through the junctions still to take – null: no way is known (yet) */
  std::optional<std::vector<Connector*>> route;
  Edge* goal = nullptr;
  /**
   * The road network of the whole map (world/routegraph.h) – for ways that
   * lead beyond the part of the map that is loaded.  Set by whoever has it.
   */
  const RouteGraph* net = nullptr;
  NavHint hint;
  /**
   * lanes of the route, for drawing it on the map.  A new list whenever the route changes (never null):
   * whoever draws it keeps the pointer and sees by it that there is another one, as the original
   * sees it by the array being another one.
   */
  std::shared_ptr<const std::vector<Lane*>> routeLanes = std::make_shared<std::vector<Lane*>>();

  explicit Navigator(LaneGraph& graph);

  /** Is there a way to follow (to a destination, or laid out beforehand)? */
  bool busy() const { return goal != nullptr || ways.has_value(); }

  void clear();

  /** how many junctions of the route are still ahead */
  int left() const;

  /**
   * Follow a way that was laid out beforehand – a tour: its paths through the
   * junctions, in order, from the lane `from` on.  Unlike a route to a
   * destination it may lead along the same road more than once, and it is
   * not shortened: whoever leaves it is led back onto it.
   */
  void setPath(const std::vector<Connector*>& path, Lane& from);

  /**
   * Follow a list of ways of the whole map's network (`net` must be set),
   * from the lane `from` on.  `tour`: it was laid out to be driven as it is –
   * whoever leaves it is led back onto it; otherwise it is the quickest way
   * to its last road, and is found anew from wherever the car gets to.
   * The ways may lead through parts of the map that are not loaded yet.
   */
  void setWays(const std::vector<int>& ways, Lane& from, bool tour);

  /** (`from`: the lane the car is on – null: not known, the way is looked for once it is) */
  bool setGoal(Edge* goal, Lane* from);

  /** `lane` and `s` = where the player is (distance of the front bumper along the lane); x,z = position. */
  const NavHint& update(double dt, Lane* lane, double s, double x = std::numeric_limits<double>::quiet_NaN(), double z = std::numeric_limits<double>::quiet_NaN());

 private:
  LaneGraph& graph;
  /** the route was laid out beforehand (a tour): it is followed as it is, and rejoined when it was left */
  bool fixed = false;
  /**
   * A way given as a list of ways of that network: of the part that is
   * loaded, `route` holds the paths through the junctions; the rest follows
   * as the car gets there.  `expanded`: how many of the ways `route` covers.
   */
  std::optional<std::vector<int>> ways;
  size_t expanded = 0;
  bool tour = false;
  Lane* from = nullptr;
  int idx = 0;
  double offRoute = 0;
  PolyProjection proj;
  /** how long the car must be off the way before a new one is looked for (s): longer after each search that found none */
  double retry = 1.5;
  /** counts the routes: another number, another route (the original looks whether `route` is another array) */
  int routes = 0;

  void setRoute(std::optional<std::vector<Connector*>> r);
  void setRouteLanes(std::vector<Lane*> lanes);
  /** Turn more of the ways into lanes and paths through junctions: as far as the map is loaded. */
  void extend();
  /** Off the way that was given as ways: back onto the tour, or the quickest way to the destination from here. */
  void rejoinWays(Lane& lane);
  /** Back onto the tour: by the shortest way to one of its next roads. */
  void rejoin(Lane& lane);
  bool plan(Lane& from);
};

/** 'rbl' | 'signals' | 'roundabout' | 'rural' | 'motorway' | 'priority' | 'home' */
using TargetType = std::string;

struct LessonDef {
  /** title and introduction are texts: "lesson.<id>.title", "lesson.<id>.intro" */
  std::string id;
  /** 'yard': tasks on the practice yard only (class CE) ('free' | 'basics' | 'route' | 'exam' | 'yard' | 'tram' | 'bus') */
  std::string kind;
  std::vector<TargetType> targets;
  /** only for this vehicle (default: all but those in `notFor`) – or for these */
  std::optional<VehicleKind> vehicle = std::nullopt;
  std::optional<std::vector<VehicleKind>> vehicles = std::nullopt;
  std::optional<std::vector<VehicleKind>> notFor = std::nullopt;
  /** tasks on the practice yard, done one after the other before the drive (see game/ce.ts): 'couple' | 'corner' | 'ramp' */
  std::optional<std::vector<std::string>> tasks = std::nullopt;
  /** the basic tasks of class B on the practice yard, one after the other (see game/drills.ts: its DrillId; 'exam': three tasks as in the test – one of group a, two of group b, Anlage 7 FeV Nr. 2.1.4.2) */
  std::optional<std::vector<std::string>> drills = std::nullopt;
};

inline const std::vector<LessonDef> LESSONS = [] {
  using Kinds = std::vector<VehicleKind>;
  using Words = std::vector<std::string>;
  return std::vector<LessonDef>{
      {.id = "basics", .kind = "basics", .targets = {}, .vehicle = "car"},
      {.id = "rbl", .kind = "route", .targets = {"rbl", "rbl", "home"}},
      {.id = "priority", .kind = "route", .targets = {"priority", "priority", "home"}},
      {.id = "signals", .kind = "route", .targets = {"signals", "signals", "home"}},
      {.id = "roundabout", .kind = "route", .targets = {"roundabout", "roundabout", "home"}},
      {.id = "rural", .kind = "route", .targets = {"rural", "home"}},
      {.id = "motorway", .kind = "route", .targets = {"motorway", "home"}, .notFor = Kinds{"tractor"}},
      // (the test drive on the road, for every class but CE – that one has its own, with the yard tasks)
      {.id = "exam", .kind = "exam", .targets = {"rbl", "signals", "roundabout", "priority", "rural", "home"}, .notFor = Kinds{"truck", "semi"}},
      // class CE: truck with trailer (Anlage 7 FeV: coupling, two basic manoeuvres, the drive)
      {.id = "ceCouple", .kind = "yard", .targets = {}, .vehicles = Kinds{"truck", "semi"}, .tasks = Words{"couple"}},
      {.id = "ceCorner", .kind = "yard", .targets = {}, .vehicles = Kinds{"truck", "semi"}, .tasks = Words{"corner"}},
      {.id = "ceRamp", .kind = "yard", .targets = {}, .vehicles = Kinds{"truck", "semi"}, .tasks = Words{"ramp"}},
      // class B: the basic tasks of the practical test on the practice yard (game/drills.ts) – the hill start and the emergency stop with the vans and the car with a trailer too
      {.id = "bParallel", .kind = "yard", .targets = {}, .vehicle = "car", .drills = Words{"parallel"}},
      {.id = "bBay", .kind = "yard", .targets = {}, .vehicle = "car", .drills = Words{"bayFwd", "bayBack"}},
      {.id = "bReverse", .kind = "yard", .targets = {}, .vehicle = "car", .drills = Words{"backStraight", "backCorner"}},
      {.id = "bTurn", .kind = "yard", .targets = {}, .vehicle = "car", .drills = Words{"turn"}},
      {.id = "bHill", .kind = "yard", .targets = {}, .vehicles = Kinds{"car", "carTrailer", "lightTruck", "minibus"}, .drills = Words{"hill"}},
      {.id = "bBrake", .kind = "yard", .targets = {}, .vehicles = Kinds{"car", "carTrailer", "lightTruck", "minibus"}, .drills = Words{"brake"}},
      {.id = "bDrills", .kind = "yard", .targets = {}, .vehicle = "car", .drills = Words{"exam"}},
      // the tram: a line with its stops (sim/tramdrive.ts)
      {.id = "tramLine", .kind = "tram", .targets = {}, .vehicle = "tram"},
      // the bus on a line: five stops on the way, with their doors and passengers
      {.id = "busLine", .kind = "bus", .targets = {}, .vehicles = Kinds{"bus", "busTrailer", "minibus", "minibusTrailer"}},
      {.id = "ceExam", .kind = "exam", .targets = {"signals", "roundabout", "priority", "rural", "home"}, .vehicles = Kinds{"truck", "semi"}, .tasks = Words{"couple", "corner", "ramp"}},
  };
}();

/**
 * Is `e` part of a spur without a way out – a road that only leads to the
 * edge of the map or into a dead end, in either direction of travel?
 * Whoever is sent there has to turn round (or cannot get back at all).
 */
bool deadEndSpur(const LaneGraph& graph, const Edge& e);

/** Is node `n` a junction of this kind (right before left, lights, a roundabout, signs that give priority)? */
bool junctionOfType(const Node& n, const TargetType& type);

/** Pick a destination edge of the wanted kind at a sensible distance from (x, z). */
Edge* findTarget(World& world, LaneGraph& graph, const TargetType& type, double x, double z, const Lane& from, Edge* home, const std::unordered_set<Edge*>& avoid);

}  // namespace fs

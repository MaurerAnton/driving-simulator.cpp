// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/roadevents.ts @ v1.7.7
/**
 * What happens on the roads today: roadworks, and now and then a
 * demonstration in a town centre.
 *
 * Roadworks.  Every road decides each day by itself – by a hash of its
 * number in the map and of the date – whether it has a work zone today: the
 * longer and the busier it is, the likelier.  So there are a few around the
 * player wherever that is, in town and outside, the same ones whichever part
 * of the map is loaded – and other ones tomorrow.  A zone fences off a
 * stretch of one side of the road:
 *
 *  - where that direction has more lanes than one, one of them ('lane'):
 *    the traffic moves over in good time; signs 121 and 222 say to which side;
 *  - on a road with one lane each way, that lane: both directions pass on
 *    the other one in turns – by a temporary traffic light whose red lasts
 *    until the last vehicle is through ('light'), or, where the stretch is
 *    short and can be seen, by the signs 208 and 308: the side of the work
 *    lets the oncoming traffic go first ('signs').
 *
 * Before it stand sign 123 (some 50 m in town, 100–200 m outside), the
 * limit (274) and the narrowing (120/121), at it the barrier (600), along it
 * the beacons (605), behind it the end of the limit (278).  The traffic keeps
 * out of the closed lane and waits at the light or for the oncoming cars;
 * the rule monitor knows the limit, the light and the closed lane; the way
 * planners go through (a little slower).
 *
 * Demonstrations.  On Saturdays around midday – and now and then on other
 * days – people march through the centre of Kassel or of Baunatal: a crowd
 * walks slowly along a street which the police close.  The traffic and the
 * planned ways go round it, and whoever drives up stops at the barrier.
 *
 * Both follow the game's clock (game/clock.ts): whoever sets another day in
 * the arcade mode sees that day's events.
 *
 * Words used below: a station is how far along a road from its junction a
 * (m); an offset how far to the left of the direction a → b; a lane's
 * distance how far along the lane from its start.
 */
#pragma once

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/geom.h"
#include "core/js.h"
#include "sim/insertion_map.h"
#include "sim/playerstate.h"
#include "world/lanes.h"
#include "world/tilefmt.h"
#include "world/world.h"

namespace fs {

class SignalSystem;
class RailNetwork;
struct RailObstacle;
struct TrafficCar;
class RuleMonitor;
struct PlayerSnapshot;
class SceneryCollider;

// ------------------------------------------------------------------------------------ roadworks

/** A barrier (Zeichen 600) across a strip: from (x1, z1) to (x2, z2) on the ground at y, facing `yaw`, with lamps on top. */
struct WorksBarrier {
  double x1 = 0, z1 = 0, x2 = 0, z2 = 0, y = 0, yaw = 0;
  int lamps = 0;
};

/** The parts of a work zone the player meets and the renderer draws (worked out once it is near). */
struct WorksLayout {
  /** sign posts like those of the map, on mobile feet */
  std::vector<SignPost> signs;
  /** barriers (Zeichen 600) across the fenced strip */
  std::vector<WorksBarrier> barriers;
  /** beacons (Zeichen 605): facing `yaw`, the stripes falling towards the side the traffic passes (`pass` 1: its left, -1: its right), some with a lamp */
  struct Beacon {
    double x = 0, y = 0, z = 0, yaw = 0;
    int pass = 0;
    bool lamp = false;
  };
  std::vector<Beacon> beacons;
  /** traffic cones round the pit */
  struct Cone {
    double x = 0, y = 0, z = 0;
  };
  std::vector<Cone> cones;
  /** the fence round the pit: its corners (x, z, …, a closed ring) on the ground at y – empty when there is none */
  std::vector<double> fence;
  double fenceY = 0;
  /** the pit: its middle, which way it runs, how long and wide */
  struct Pit {
    double x = 0, y = 0, z = 0, yaw = 0, len = 0, wid = 0;
  };
  std::optional<Pit> pit;
  /** a mini excavator, a heap of gravel, a site trailer */
  struct Machine {
    /** 'excavator' | 'heap' | 'trailer' */
    std::string kind;
    double x = 0, y = 0, z = 0, yaw = 0;
  };
  std::vector<Machine> machines;
  /** the temporary lights: where they stand, facing the traffic of their group (0: the side of the work) */
  struct Light {
    double x = 0, y = 0, z = 0, yaw = 0;
    int group = 0;
  };
  std::vector<Light> lights;
  /** the workers: each walks to and fro between two points (the same point: stands), facing `yaw` when standing */
  struct Worker {
    double x1 = 0, z1 = 0, x2 = 0, z2 = 0, y = 0, yaw = 0;
    int look = 0;
  };
  std::vector<Worker> workers;
  /** what the player's vehicle runs into: discs (x, y, z, r, …) and pieces of wall (x1, z1, x2, z2, y, …) */
  std::vector<double> discs;
  std::vector<double> segments;
  /** what the traffic stops for (the fenced strip, in pieces) */
  std::vector<PlayerState> boxes;
};

/** A work zone of today on a road that is loaded. */
struct WorkZone {
  /** the road's number in the map */
  int id = 0;
  /** the road (a new object whenever it is loaded again: the zone is made anew with it) */
  Edge* edge = nullptr;
  /** the fenced stretch, stations z0 < z1 */
  double z0 = 0;
  double z1 = 0;
  /** the side closed: 1 the lanes running a → b, -1 those running b → a */
  int side = 1;
  /** 'lane' | 'light' | 'signs' */
  std::string control;
  /** the closed lane: its index among the lanes of that side (0: the right-most) */
  int lane = 0;
  /** the limit through the zone (km/h; 0: none of its own) */
  double limit = 0;
  /** how far before the zone sign 123 stands, how long the taper of beacons is (m) */
  double warn = 0;
  double taper = 0;
  bool urban = false;
  std::string street;
  /** the middle of the zone */
  double x = 0;
  double z = 0;
  /** across the road (offsets): where the fenced strip begins at the edge of the road, and the line of its beacons */
  double outer = 0;
  double line = 0;
  /** how far the vehicles of the side of the work move over to pass it (m; 'lane': 0) */
  double pass = 0;
  /** the light: green for the side of the work and for the other side, all-red to clear the stretch (s), where its cycle begins */
  std::array<double, 2> green{};
  double clear = 0;
  double offset = 0;
  /** settles everything else (where the machines stand, the people) */
  int seed = 0;
  /** vehicles in the stretch now, and coming up to it, per direction (0: a → b, 1: b → a) – counted each step */
  std::array<int, 2> inside{};
  std::array<int, 2> coming{};
  /** how far the nearest of those coming is from its line (m), per direction */
  std::array<double, 2> comingNear{JS_INF, JS_INF};
  /** cars standing at the end of a closed lane to move over: their fronts and lengths, pairwise */
  std::vector<double> mergers;
  /** worked out when first needed */
  std::optional<WorksLayout> layout;
};

/**
 * zones per kilometre of road and day: outside towns on the main roads, outside on the others, in town on
 * main roads, in town on the others – on the map of northern Hesse some three or four within 2 km of
 * wherever one is (a road shorter than a zone with its signs has none: in the centre of Kassel most are)
 */
struct WorksRate {
  double ruralMain = 0.6;
  double ruralMinor = 0.25;
  double urbanMain = 0.4;
  double urbanMinor = 0.13;
};
inline constexpr WorksRate WORKS_RATE{};

/** Where the shared stretch (the taper included) begins for a vehicle going `dir` (station). */
double entryOf(const WorkZone& z, int dir);
/** … and where it ends. */
double exitOf(const WorkZone& z, int dir);
/** Where a vehicle going `dir` waits before the stretch (its front, station). */
double waitOf(const WorkZone& z, int dir);

/** May a road have roadworks: a road of the network (no motorway, slip road or roundabout, no bridge or tunnel)? */
bool worksSite(const Edge& e);

/** A number that stands for a day (as the lights that are out of order count them). */
int dayKey(int y, int m, int d);

/**
 * The work zone a road has on a day (`day`: dayKey), or null.  The same
 * road gives the same zone all day – whenever it is loaded.
 */
std::shared_ptr<WorkZone> worksOn(Edge& e, int day, const LaneGraph& lanes);

/** What settles a work zone (worksOn chooses it by the day; tests give it). */
struct ZoneSpec {
  int side = 1;
  std::string control;
  int lane = 0;
  double z0 = 0;
  double z1 = 0;
  double taper = 0;
  double limit = 0;
  double warn = 0;
  std::array<double, 2> green{};
  /** where the light's cycle begins, 0 … 1 */
  double phase = 0;
  int seed = 0;
};

/** A work zone of these measures on a road whose lanes are known. */
std::shared_ptr<WorkZone> makeZone(Edge& e, const LaneGraph& lanes, const ZoneSpec& spec);

/** What the light of a zone shows to a group (0: the side of the work, 1: the other) at a time (s, the signals' clock) – and since when (s). */
struct WorksLight {
  int state = 0;
  double age = 0;
};
WorksLight lightAt(const WorkZone& z, int group, double t);

// ------------------------------------------------------------------------------------ demonstrations

/** A place where people gather for a demonstration. */
struct DemoPlace {
  std::string name;
  std::string town;
  double lat = 0;
  double lon = 0;
  /** how often it is chosen against the others */
  double weight = 0;
};

/** The centres of Kassel and Baunatal. */
const std::vector<DemoPlace>& DEMO_PLACES();

/** The way of a march along the roads, and what is closed for it. */
struct DemoRoute {
  /** the roads (their numbers in the map) in the order walked, which way each is walked (1: a → b), their lengths, where each begins along the way */
  std::vector<int> edges;
  std::vector<int> dirs;
  std::vector<double> lens;
  std::vector<double> cum;
  double length = 0;
  /** the street most of it follows */
  std::string street;
  /** the junctions along it, from where it begins to where it ends */
  std::vector<int> nodes;
  /** the roads it follows; the ways closed – both ways of those roads, and the side roads where they lead into it */
  std::unordered_set<int> roads;
  std::unordered_set<int> ways;
};

/** What stands at the closure of a march. */
struct ClosureLayout {
  std::vector<SignPost> signs;
  std::vector<WorksBarrier> barriers;
  /** the tape behind the barriers: from (x1, z1) to (x2, z2), y on the ground */
  struct Tape {
    double x1 = 0, z1 = 0, x2 = 0, z2 = 0, y = 0;
  };
  std::vector<Tape> tape;
  struct Stand {
    double x = 0, y = 0, z = 0, yaw = 0;
  };
  /** police officers standing at the barriers */
  std::vector<Stand> officers;
  /** where a police car stands (behind the barriers at the ends) */
  std::vector<Stand> cars;
  std::vector<double> discs;
  std::vector<double> segments;
  std::vector<PlayerState> boxes;
};

/** A demonstration of the day. */
struct Demonstration {
  int id = 0;
  std::string place;
  std::string town;
  /** where it gathers (world) */
  double x = 0;
  double z = 0;
  /** when it begins and ends (ms since 1970, by the game's clock) */
  double from = 0;
  double until = 0;
  /** how many people walk */
  int size = 0;
  int seed = 0;
  /** the march (null: not known yet – the roads there are not loaded) */
  std::optional<DemoRoute> route;
  /** the barriers, the officers – worked out when first needed */
  std::optional<ClosureLayout> layout;
};

/** a demonstration planned for a day, before its way is known */
struct DemoPlan {
  int id = 0;
  const DemoPlace* place = nullptr;
  double from = 0;
  double until = 0;
  int size = 0;
  int seed = 0;
};

/**
 * The demonstrations of a day in Kassel and Baunatal: on Saturdays mostly
 * one around midday (sometimes a second in the afternoon), on weekdays now
 * and then one after work, on Sundays seldom; on the 1st of May always one.
 * The same date gives the same ones.
 */
std::vector<DemoPlan> demosOn(int y, int m, int d);

/**
 * The way of a march that gathers near (x, z): from the street nearest to
 * the place on in both directions, along the same street where it goes on
 * (else the straightest road), until it is about `want` metres long.  None
 * when no street is near.  The same roads give the same way.
 */
std::optional<DemoRoute> demoRoute(const World& world, double x, double z, double want);

/** How many walk side by side, how far apart the rows are, how long the crowd is (m). */
struct CrowdShape {
  int cols = 0;
  int rows = 0;
  double gap = 0;
  double length = 0;
};
CrowdShape crowdShape(const Demonstration& d);

/**
 * Where the march is at a moment: how far along its way the front of the
 * crowd is (m), which way it walks (1: as the way runs, -1: back) – the
 * crowd lies behind its front – and whether it is walking.
 */
struct MarchAt {
  double head = 0;
  int dir = 1;
  bool walking = false;
  double speed = 0;
};
MarchAt marchAt(const Demonstration& d, double ms);

/**
 * A point of the way of a march, `u` metres along it and `side` metres to
 * the left of the walking direction: into `out` (x, z, the walking direction
 * tx, tz) – and the road there (its station into `s`), or null without a way.
 */
Edge* routeAt(const World& world, const DemoRoute& route, double u, double side, PolySample& out, double* s = nullptr);

// ------------------------------------------------------------------------------------ for the map

/** An event of the day, for the map. */
struct RoadEventInfo {
  /** 'works' | 'demo' */
  std::string kind;
  /** where it is (world) */
  double x = 0;
  double z = 0;
  /** the street it is in */
  std::string street;
  /** when it holds: from and until (ms since 1970, by the game's clock) */
  double from = 0;
  double until = 0;
  /** happening now (a demonstration of the day may still lie ahead, or be over) */
  bool active = false;
  /** roadworks: how the traffic passes ('lane', 'light', 'signs'); a demonstration: the place it gathers at */
  std::string detail;
  /** a demonstration: the course of its march (x, z, …; empty while its roads are not loaded) */
  std::vector<double> line;
};

/** What a vehicle on a lane is asked by the roadworks and closures (RoadEvents.gate). */
struct Gate {
  /** stop this far ahead of the front (m; Infinity: no need) */
  double stop = JS_INF;
  /** at most this fast (m/s; Infinity: as the road allows) */
  double cap = JS_INF;
  /** the lane ends ahead (closed): the one to move over to */
  Lane* move = nullptr;
  /** the road the vehicle meant to turn into next is closed: choose another way */
  bool reroute = false;
  /** why it stops: '' | 'light' | 'oncoming' | 'lane' | 'closed' */
  std::string why;
};

/**
 * The events of the day around the player: the work zones of the loaded
 * roads, today's demonstrations; what they ask of the traffic, the rule
 * monitor and the way planners.
 */
class RoadEvents {
 public:
  World& world;
  LaneGraph& lanes;
  SignalSystem& signals;
  /** the moment of the world (ms since 1970): the game's clock */
  std::function<double()> now;
  /** is the road network within r metres of a place loaded (a map in pieces)? – a march's way is looked for once it is */
  std::function<bool(double x, double z, double r)> ready = [](double, double, double) { return true; };
  /** what the player's vehicle runs into (the game's SceneryCollider) */
  SceneryCollider* collider = nullptr;
  /** the work zones of the loaded roads, by the road's number */
  InsertionMap<int, std::shared_ptr<WorkZone>> zones;
  /** today's demonstrations */
  std::vector<Demonstration> demos;
  /** grows whenever the zones or the closures change (for whoever draws them or plans ways) */
  int version = 0;

  RoadEvents(World& world, LaneGraph& lanes, SignalSystem& signals);

  /** Roads that are loaded now: their zones of the day. */
  template <class Edges>
  void addRoads(const Edges& edges) {
    if (day < 0) return;
    bool any = false;
    for (Edge* e : edges) any = addRoad(*e) || any;
    if (any) version++;
  }

  /** For the map loader (MapLoader.listen): zones come and go with their roads. */
  void added(const WorldChange& c) { addRoads(c.edges); }
  void removed(const WorldChange& c) { dropRoads(c.edges); }

  /**
   * The tracks of the trams and trains, once they are read: the zones that
   * lie where a track runs along the road go (the trams cannot pass them).
   */
  void setTracks(const RailNetwork* net);

  /** What the trams stop for (sim/rail.ts): the crowds of the marches under way near the player – added to `out`. */
  void railBlocks(std::vector<RailObstacle>& out);

  /** Roads that are taken away: their zones go. */
  void dropRoads(const std::vector<Edge*>& edges);

  /**
   * Called by the traffic at the beginning of each of its steps: the day
   * and the demonstrations now and then; who is in or before each zone.
   */
  void step(double dt, const std::vector<TrafficCar*>& cars, const PlayerState& player);

  /** The tip waiting to be shown (a text key) – taken once. */
  std::string takeTip();

  /**
   * What the zones and closures ask of a vehicle on `lane` whose front is
   * `front` metres along it, going at `v`, turning into `next` at its end:
   * where to stop, how fast at most, whether to move over or to choose
   * another way.
   */
  Gate gate(const Lane& lane, double front, double v, const Connector* next);

  /** The same for a car of the traffic (null: nothing asked of it; the answer is overwritten by the next call). */
  const Gate* gateCar(TrafficCar& car);

  /** The lane beside a closed one (towards the open side), or null. */
  Lane* openLane(const Lane& lane);

  /** May a vehicle at `front` along `lane` be on it from here on (or move over onto it) – is it not closed ahead within reach? */
  bool laneOpen(const Lane& lane, double front);

  /**
   * How far a vehicle whose middle is `mid` metres along `lane` is drawn to
   * its left: where its side is closed it passes on the other lane (m; 0
   * elsewhere).
   */
  double shift(const Lane& lane, double mid);

  /** Moves a point `d` metres along `lane` (sampled into `p`, the direction of travel tx, tz) to where the vehicles pass a closed side. */
  void offset(const Lane& lane, double d, PolySample& p);

  /** Is a lane closed now (a demonstration)? */
  bool closed(const Lane& lane);

  /** May no car of the traffic appear here: a closed road, beside a closure, in a zone's stretch? */
  bool blocked(const Lane& lane, double s);

  /** What the way planners add for a way (s): Infinity where it is closed, a little where a zone slows it. */
  double wayCost(int way);

  /** The obstacles for the traffic: the game's, and the fenced strips, the barriers and the crowd near the player. */
  const std::vector<PlayerState*>& withBlocks(const std::vector<PlayerState*>& obstacles, const PlayerState& player);

  /** Where the crowd of the demonstration listed at (x, z) is at `ms`: its middle, facing the way it walks (none: none there, or its way not loaded). */
  struct CrowdPlace {
    double x = 0, y = 0, z = 0, yaw = 0;
  };
  std::optional<CrowdPlace> crowdAt(double x, double z, double ms);

  /** The limit at a place of a road for a vehicle going `dir`: the zone's where its signs say so, else `limit` (km/h; 0: none). */
  double limitAt(const Edge& edge, double s, int dir, double limit);

  /** Is the oncoming lane the way here – the stretch of a closed side, for a vehicle going `dir` (no fault to keep left there)? */
  bool passing(const Edge& edge, double s, int dir);

  /**
   * The player's faults at the zones and closures (called by the rule
   * monitor with where the vehicle is: its road, station, offset and
   * direction): over the line on red at the site light, into the stretch
   * without letting the oncoming traffic go first (Zeichen 208), into the
   * fenced strip, into a road closed for a march.
   */
  void judge(RuleMonitor& rules, const PlayerSnapshot& p, const Edge& edge, double s, double off, int dir, double dt);

  /** The events of the day: the zones of the loaded roads and today's demonstrations (for the map). */
  std::vector<RoadEventInfo> list();

  /** Is a demonstration under way now – with its way known? */
  bool marching(const Demonstration& d);

  /** Everything of a work zone: signs, barriers, beacons, the pit, the machines, the light, the workers (worked out once). */
  WorksLayout& worksLayout(WorkZone& z);

  /** Everything of the closure of a march: barriers where it begins and ends and across the side roads, signs, officers. */
  ClosureLayout& closureLayout(Demonstration& d);

 private:
  /** the day the zones are of (dayKey; -1: none yet) */
  int day = -1;
  /** the ways closed now (for the traffic), and those closed or about to be (for the way planners) */
  std::unordered_set<int> shut;
  std::unordered_set<int> shutSoon;
  /** the roads a march walks along now */
  std::unordered_set<int> shutRoads;
  /** the junctions where a closure begins (no car to appear just beside them) */
  std::unordered_set<int> shutNodes;
  double tick = 0;
  SurfaceHit hit;
  PolySample smp;
  Gate g;
  Gate g2;
  /** what is in the collider now: by key */
  std::unordered_set<std::string> inCollider;
  /** the obstacles the traffic gets: the game's and these */
  std::vector<PlayerState*> blockList;
  /** (the boxes of the crowds, made anew each time) */
  std::deque<PlayerState> crowdList;
  /** (what a march without a way has standing: nothing) */
  ClosureLayout noClosure;
  /** the player: where it was last (for the tips, the collider), its front's station on a zone's road last step */
  double px = JS_NAN;
  double pz = JS_NAN;
  struct Front {
    const WorkZone* zone = nullptr;
    int dir = 0;
    double s = 0;
    double t = 0;
  };
  std::optional<Front> prevFront;
  double inStrip = 0;
  /** the player was last on a road that is open (a road closed for a march is entered from there), and where */
  bool wasOpen = false;
  double jx = JS_NAN;
  double jz = JS_NAN;
  /** what was said to the player (zone id * 2 + direction, or -1 - demo id), and the tip waiting to be shown */
  std::unordered_set<int> told;
  std::string tipKey;
  /** the tracks of the trams and trains (sim/rail.ts): no work zone where one runs along the road */
  const RailNetwork* tracks = nullptr;

  bool addRoad(Edge& e);
  bool overTrack(const WorkZone& z);
  WorkZone* zoneOn(const Edge& e);
  void count(const Lane& lane, double mid, double length, double v);
  void refresh(const PlayerState& player);
  void collide(const std::string& key, const std::vector<double>& discs, const std::vector<double>& segments);
  void uncollide(const std::string& key);
  void tips(const PlayerState& p);
  void forget();
  Gate& resetGate(Gate& g);
  Gate& zoneGate(WorkZone& z, const Lane& lane, double front, double v, Gate& out);
  /** How far before the zone the limit begins (m): with sign 123 in town, half-way outside. */
  double limitLead(const WorkZone& z) const;
  void crowdBoxes(const Demonstration& d, double ms);
  struct Spot {
    double x = 0, y = 0, z = 0;
  };
  /** A point of road `e`: station `s`, `o` metres left of a → b, on the ground there (into `out`; returns it). */
  Spot& put(const Edge& e, double s, double o, Spot& out);
};

}  // namespace fs

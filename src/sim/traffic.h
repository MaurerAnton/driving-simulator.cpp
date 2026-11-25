// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/traffic.ts @ v1.7.7
/**
 * Ambient traffic.
 *
 * Cars travel on the lane graph: they follow the car ahead (Intelligent
 * Driver Model), keep to speed limits, slow for bends, obey traffic lights
 * and stop signs and give way exactly according to the right-of-way relations
 * of the lane graph – the same relations the rule monitor judges the player by.
 *
 * Of the parts the original's traffic carries with it, these are not translated yet and hang
 * on hooks (sim/hooks.h; null: not there): the people on foot (`crowd`), the cyclists
 * (`cyclists`), the kerb build-outs (`narrowings`) – the original makes these three itself –,
 * the emergency vehicles (`emergency`), the roadworks (`roadEvents`), the level crossings
 * (`levelCrossings`), the wild animals (`wild`), the lines of the buses (setBusLines) and
 * their timetables (`busSchedule`).
 */
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/geom.h"
#include "core/js.h"
#include "sim/hooks.h"
#include "sim/insertion_map.h"
#include "sim/pastblock.h"
#include "sim/playerstate.h"
#include "sim/signals.h"
#include "sim/smoothpose.h"
#include "world/lanes.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

/**
 * A bus running a line (src/world/buslines): what its destination
 * display shows, and where on the line's ways it is.
 */
struct BusOnLine {
  /** the line's number and where it goes – what the bus shows */
  std::string ref;
  std::string to;
  /** the line (its number in BusLines.lines) and the way of it the bus is on (index into its ways) */
  int index = 0;
  int k = 0;
  /**
   * 0 while it is on its line's way; once it has lost it (its lane did not
   * lead on there, or the map has no road for a piece) how many roads it has
   * driven since – it drives on as the others do and looks for its way again
   */
  int off = 0;
};

struct TrafficCar {
  int64_t id = 0;
  Lane* lane = nullptr;
  Connector* conn = nullptr;
  /** distance of the car's centre along the current lane / connector */
  double s = 0;
  double v = 0;
  double a = 0;
  /** next connector to take at the end of the current lane */
  Connector* next = nullptr;
  double length = 0;
  double width = 0;
  int color = 0;
  int style = 0;
  /** desired speed factor relative to the limit */
  double temper = 0;
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  double pitch = 0;
  /** -1 left, 0 none, 1 right */
  int blinker = 0;
  bool braking = false;
  /** seconds spent waiting at the current stop/yield line */
  double waited = 0;
  /** seconds spent waiting for a gap (not counting red lights) */
  double yieldWait = 0;
  bool stoppedAtLine = false;
  double stuck = 0;
  /** set while the player's car is the reason for braking */
  double blockedByPlayer = 0;
  /**
   * How the player's car came to be in this one's way: the time gap (s) at
   * which it was first seen there (Infinity while it is not), and for how
   * long that is remembered once it is out of the way again.
   */
  double playerFirstGap = JS_INF;
  double playerSeen = 0;
  /**
   * A city bus (style BUS_STYLE): the stop it stands at – seconds left with
   * its doors open –, the pole it served last, and how long it still
   * indicates to pull out again; the line it runs (null: none), and how many
   * roads it has driven since it last had one.
   */
  struct Bus {
    double dwell = 0;
    int served = -1;
    bool doors = false;
    double pullOut = 0;
    std::optional<BusOnLine> line = std::nullopt;
    double free = 0;
    /** the trip of the timetables it runs ('': none) */
    std::string trip;
    bool retired = false;
  };
  std::optional<Bus> bus = std::nullopt;
  /**
   * The lane of the same road it is moving over to (a bus whose line turns
   * where its own lane does not lead) – and, once it has, how far it is
   * still drawn to the left of the middle of its new lane (m) while it
   * slides across.
   */
  Lane* merge = nullptr;
  double slide = 0;
  /** a bus that has lost its line's way: the connectors back onto it, the next first ([]: none found; no value: not looked for) */
  std::optional<std::vector<Connector*>> detour = std::nullopt;
  /** an emergency vehicle – police, ambulance, fire engine, the emergency doctor – and its call (src/sim/emergency) */
  std::shared_ptr<EmergencyCar> em;
  /** how far it keeps left (+) or right (−) of the middle of its lane (m): making way for one of those, or passing the others */
  double side = 0;
  /** going round something that blocks its lane – waiting behind it, out, beside it, back (src/sim/pastblock) */
  std::shared_ptr<PastState> past = nullptr;
  double crashed = 0;
  double wheelSpin = 0;
  /**
   * Where it is drawn, and seen when watched (src/sim/smoothpose): its
   * place and heading eased – turning gradually through the corners of its
   * way – and its front wheels' angle.  (x, z, yaw above are where its way
   * puts it: what the simulation reckons with.)  Unset until it first moved.
   */
  std::optional<EasedPose> drawn = std::nullopt;
  /** the path through the junction it came by (its heading is taken over the end of that while it is near) */
  Connector* lastConn = nullptr;
};

/**
 * The timetables of the buses as the traffic asks them (the game answers
 * from src/game/timedtransit): the buses of the lines that have one are
 * put where and when their trips are, not by chance, and stand at a stop
 * until their time.
 */
class BusSchedule {
 public:
  virtual ~BusSchedule() = default;
  /** does the line (its number in BusLines.lines) run by a timetable? */
  virtual bool timed(int line) = 0;
  /** a step: the buses of the trips under way round the player put on their roads (by addCar), each once */
  virtual void step(double dt, const PlayerState& player) = 0;
  /** the traffic was cleared: the trips are put where they are, also close to the player */
  virtual void reset() = 0;
  /** how long a bus of a trip stands at the stop of pole `pole` it has come to (s; `least`: what it would stand without a timetable) */
  virtual double dwell(TrafficCar& car, int pole, double least) = 0;
};

/**
 * One of the people on foot in the town (src/sim/pedestrians): whoever
 * waits at a crossing or walks over it (`state`, `crossing`, `waited`) is
 * what the cars and the instructor go by.
 */
using Pedestrian = Walker;

/** A vehicle that drives itself: where it is on its path, for asking the traffic what is around it. */
struct SelfDriving {
  /** the lane it is on, and the connector it is on while crossing a junction (or null) */
  Lane* lane = nullptr;
  Connector* conn = nullptr;
  /** distance of its middle along that lane or connector */
  double s = 0;
  double v = 0;
  /** the connector it will take at the end of its lane */
  Connector* next = nullptr;
  /** the connectors of its way beyond that, if it knows where it is going */
  const std::vector<Connector*>* route = nullptr;
  double length = 0;
  double width = 0;
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  /** going round something that blocks its lane (src/sim/pastblock) */
  std::shared_ptr<PastState> past = nullptr;
};

/** the style of a traffic car that is a city bus (12 m, it calls at the stops) */
inline constexpr int BUS_STYLE = 3;

/**
 * A bus stop along a lane: where the front of a bus stands to serve it
 * (metres along the lane) and the pole's number – the stop place's number in
 * the map's stops times 64, plus which of its poles (63: the place as a line
 * of buses gives it, without a pole on this lane).
 */
struct BusStopOnLane {
  double s = 0;
  int id = 0;
};

/** The stop place a pole's number (BusStopOnLane.id) belongs to. */
int stopPlaceOf(int pole);

/**
 * What happened in a step (the original's union, told apart by `type`):
 *  - 'forcedBrake' (car, decel, cutIn) – `cutIn`: the player's car appeared in this car's way
 *    at short notice – moved over or pulled out in front of it – rather than having been seen
 *    ahead from far away (whoever comes up from behind keeps his own distance, § 4 StVO);
 *  - 'collision' (car, speed);
 *  - 'contact' (car, nx, nz, depth) – every picture while the player's vehicle overlaps one of
 *    the traffic: the way out (unit, from it to the player) and how deep;
 *  - 'pedestrianEndangered' (ped);
 *  - 'cyclistEndangered' (cyclist, hit) – a cyclist had to brake hard because the player's
 *    vehicle came into their way – or was knocked over by it.
 */
struct TrafficEvent {
  std::string type;
  TrafficCar* car = nullptr;
  double decel = 0;
  bool cutIn = false;
  double speed = 0;
  double nx = 0;
  double nz = 0;
  double depth = 0;
  Pedestrian* ped = nullptr;
  Cyclist* cyclist = nullptr;
  bool hit = false;
};

class Traffic {
 public:
  World& world;
  LaneGraph& graph;
  SignalSystem& signals;
  /**
   * The cars.  (A car taken out of the traffic stays alive until the next update, so that
   * what was said about it in this step – the events – still points at something.)
   */
  std::vector<TrafficCar*> cars;
  /** the people on foot in the town (src/sim/pedestrians; `crowd.target` says how many) */
  std::unique_ptr<Crowd> crowd;
  /** people on bicycles (src/sim/cyclists) */
  std::unique_ptr<Cyclists> cyclists;
  /**
   * The bus stops along a lane (set by the game from the stops of the map) –
   * with them, now and then a city bus is among the traffic.
   */
  std::function<std::vector<BusStopOnLane>(const Lane& lane)> busStops = nullptr;
  /** the share of the traffic that are buses (where there are stops, or where a line runs) */
  double busShare = 0.08;
  /** the timetables of the buses (src/game/timedtransit – null: none): the buses of their lines come where and when their trips are */
  BusSchedule* busSchedule = nullptr;
  int targetCount = 24;
  double spawnRadius = 330;
  std::vector<TrafficEvent> events;
  /** emergency vehicles on their calls, and how the others make way for them (src/sim/emergency – null: none come) */
  Emergencies* emergency = nullptr;
  /** the roadworks and closures of the day: where cars wait, move over and keep out (src/sim/roadevents – null: none) */
  RoadEvents* roadEvents = nullptr;
  /** getting past what blocks a lane: a car standing behind something that does not move goes round it (src/sim/pastblock) */
  PastBlocks pastBlocks;
  /** the narrowings kerb build-outs make: through the gap, slowly, one direction after the other (src/sim/narrowings) */
  const std::unique_ptr<Narrowings> narrowings;
  /** the level crossings: waiting while their lights are on or a train comes, never stopping on the tracks (src/sim/levelcrossing – null: none) */
  LevelCrossings* levelCrossings = nullptr;
  /** deer and wild boar crossing the country roads at dusk: the cars brake for them (src/sim/wildlife – null: none) */
  Wildlife* wild = nullptr;
  /**
   * Other things on the road the cars must not run into, described like the
   * player's car (set by the game every frame: the cars of trams and trains).
   */
  std::vector<PlayerState*> obstacles;

  Traffic(World& world, LaneGraph& graph, SignalSystem& signals);
  ~Traffic();
  Traffic(const Traffic&) = delete;
  Traffic& operator=(const Traffic&) = delete;

  /** The people on foot (src/sim/pedestrians): those at a crossing are what the cars stop for. */
  const std::vector<Pedestrian*>& peds() const;

  /** Roads that are part of the network now (it comes in pieces): cars may appear on their lanes. */
  template <typename Edges>
  void addRoads(const Edges& edges) {
    for (Edge* e : edges) addRoad(*e);
  }

  /** Roads that are being taken out of the network: no car stays on them, none appears there. */
  template <typename Edges>
  void removeRoads(const Edges& edges) {
    InsertionSet<Edge*> gone;
    for (Edge* e : edges) gone.add(e);
    removeRoadSet(gone);
  }

  /** everything besides the cars in their way this step (for src/sim/pastblock) */
  const std::vector<PlayerState*>& blockList() const { return blocks; }
  /** the cars on a lane now, unsorted */
  const std::vector<TrafficCar*>* carsOn(const Lane& lane) const;
  /** the cars on a path through a junction now, unsorted */
  const std::vector<TrafficCar*>* carsOnConn(const Connector& c) const;
  /** distance along a car's way until it would touch `box` (Infinity: it does not within `horizon`) */
  double gapTo(TrafficCar& car, const PlayerState& box, double horizon);

  /**
   * Put a car of the traffic on a lane, its middle `s` metres along it – a
   * city bus if `bus`, running `line` (not given: one of the lines that run
   * there, if any; no value: none).  The traffic fills itself around the player;
   * this is for putting one at a certain place.
   */
  TrafficCar* addCar(Lane& lane, double s, bool bus = false);
  TrafficCar* addCar(Lane& lane, double s, bool bus, const std::optional<BusOnLine>& line);

  // --- the lines of the city buses ----------------------------------------------------

  /** The lines of the city buses, as setBusLines was given them. */
  BusLines* busLines() const { return lines; }

  /**
   * The lines of the city buses (src/world/buslines – null: none).  A bus
   * that appears where lines run gets one of them: it follows the line's way
   * at the junctions, calls at the line's stops and, at the end of its
   * route, goes on as the line back where that begins there.  The buses
   * already under way are given lines anew.
   */
  void setBusLines(BusLines* lines);

  /** Take a car out of the traffic (a bus of the timetables put where its trip is anew). */
  void removeCar(TrafficCar* car);

  void clear();

  // --- for a vehicle that drives itself without being one of the traffic's cars ------
  // (the player's car on a demonstration drive: it sees what the others see)

  /**
   * For a vehicle that drives itself (the player's autopilot): one step of
   * going round something that stands in its way – as the traffic's cars do
   * (src/sim/pastblock).  `gap`: to what is ahead; `held`: it stands for
   * a reason of the road.  Leaves the state in `me.past`.
   */
  void passBy(SelfDriving& me, double dt, double gap, bool held);

  struct Ahead {
    double gap = JS_INF;
    double v = 0;
    bool loose = false;
  };
  /**
   * The nearest vehicle ahead on the path of `me` – a car of the traffic, or
   * a tram or train in its way: the gap to it (m) and its speed along the
   * path; gap = Infinity if there is none within `horizon`.
   */
  Ahead aheadOf(SelfDriving& me, double horizon);

  /**
   * For a vehicle that is about to cross the way of others – turning left, or
   * on its way through a junction made of several: how far along its way (m,
   * from its middle) the nearest place lies where a car that is under way now
   * will cross at about the time it gets there itself, or Infinity.
   * Cars that stand, crawl or are braking to a stop do not count: those are
   * waiting for their turn.
   */
  double crossingAhead(SelfDriving& me, double reach);

  /**
   * May `me` drive into the junction through `c` – is the way free of
   * traffic it has to give way to, and of cars standing or crossing in it?
   * `impatient`: after a long wait a tighter (still safe) gap is taken.
   */
  bool mayPass(SelfDriving& me, Connector& c, double distToLine, bool impatient = false);

  /**
   * Is there room on `lane` beside and behind a vehicle whose middle is `s`
   * metres along it (the lanes of a carriageway are measured alike) and that
   * drives at `v` – may it move over without making anybody brake?
   */
  bool roomOn(const Lane& lane, double s, double v, double length) const;

  /**
   * Is there room at the beginning of `lane` for a vehicle that needs `need`
   * metres – or does the traffic stand there, so that whoever drove into the
   * junction before it would have to wait in it?  Cars that are moving make
   * room; those still in the junction on their way into the lane will need it.
   */
  bool roomBehind(const Lane& lane, double need) const;

  /** Is nobody inside the junction on a path that meets `c`? */
  bool clearOf(Connector& c);

  /** A pedestrian who is crossing at `c` or stands there waiting to. */
  Pedestrian* crossingAt(const CrossingJson& c) const;

  // --- main update -----------------------------------------------------------------
  void update(double dt, const PlayerState& player);

  /**
   * Is the player on foot at this zebra crossing: on its stripes, or standing
   * at its kerb facing the road (somebody who wants to cross, § 26 Abs. 1 StVO)?
   */
  bool atZebra(const CrossingJson& c, const PlayerState& player);

  /** Is a pedestrian waiting at or walking on the given crossing? */
  Pedestrian* pedestrianAt(const CrossingJson& c) const;

  /** The lanes cars may appear on in the cell of the spawn grid at (x, z) – what the original hands the cyclists as `lanesAt`. */
  const std::vector<Lane*>* lanesAt(double x, double z) const;

 private:
  /** cars currently on each lane / connector, unsorted */
  std::unordered_map<int, std::vector<TrafficCar*>> onLane;
  std::unordered_map<int, std::vector<TrafficCar*>> onConn;
  /** lanes cars may appear on, bucketed by 120 m cells for spawning near the player */
  std::unordered_map<int64_t, std::vector<Lane*>> spawnGrid;
  Rng rand{20261114};
  int64_t nextId = 1;
  double spawnTimer = 0;
  /** the lines of the city buses (setBusLines) */
  BusLines* lines = nullptr;
  /** the obstacles and the cyclists, as the cars see them (made anew in every step) */
  std::vector<PlayerState*> blocks;
  std::deque<PlayerState> cycleBoxes;
  std::vector<int> aheadWays;
  std::vector<BusStopOnLane> stopList;
  std::vector<BusLineStop> nearStops;
  TrafficCar probe;
  PlayerState box;
  std::vector<double> way;
  PolySample tmp = newPolySample();
  PolySample tmp2 = newPolySample();
  /** the cars taken out in this step (see `cars`) */
  std::vector<std::unique_ptr<TrafficCar>> gone;

  void addRoad(Edge& e);
  void removeRoadSet(const InsertionSet<Edge*>& gone);
  /** The cells of the spawn grid a lane passes through – none if no car is to appear on it. */
  InsertionSet<int64_t> spawnCells(const Lane& lane);

  // --- bookkeeping -------------------------------------------------------------
  void attach(TrafficCar* car);
  void detach(TrafficCar* car);
  Connector* chooseNext(const Lane& lane);
  static int64_t cellKey(double x, double z);
  void spawn(const PlayerState& player);

  static BusOnLine onLine(const BusLine& line, int k);
  /** A line for a bus on `lane`: one of those whose route runs along it, by chance (`prefer`: one of that number, if one runs here) – none if none does. */
  std::optional<BusOnLine> pickLine(const Lane& lane, const std::string& prefer = "");
  /** The next `n` ways of a bus's line after the one it is on – on into the line back at its end (fewer where its line ends). */
  const std::vector<int>& ahead(const BusOnLine& on, int n);
  /** Can a vehicle on `lane` follow the ways ws[i], ws[i + 1], … through the next `depth` junctions?  (Beyond what is loaded it is taken that it can.) */
  bool reaches(const Lane& lane, const std::vector<int>& ws, size_t i, int depth) const;
  /** Of the lanes of `lane`'s road in its direction, the one from which a bus can follow its line on – the right-most such. */
  Lane* laneFor(Lane& lane, const BusOnLine& on);
  /**
   * The connector to take at the end of `lane`: for a bus on a line the one
   * into its line's next way – into the lane from which it can go on along
   * it, the right-most such –, for every other car one at random.  Where the
   * bus's lane does not lead there but another lane of the road does, it is
   * to move over (`car.merge`, no connector yet); where none does, it loses
   * its way (`off`).
   */
  Connector* nextFor(BusOnLine* on, Lane& lane, TrafficCar* car = nullptr);
  /**
   * A bus that has lost its line's way (a turn the map forbids, a lane it
   * could not leave): the shortest drive back onto it – into one of its next
   * ways, over the roads that are loaded, within 2.5 km – as the connectors
   * to take, the first from `lane`.  None if there is none.
   */
  std::optional<std::vector<Connector*>> wayBack(Lane& lane, const BusOnLine& on);
  /** Another lane of `lane`'s road in its direction from which the ways `ahead` can be followed – the nearest such (null: none). */
  Lane* laneTowards(Lane& lane, const std::vector<int>& ahead);
  /**
   * A car that is to move over to another lane of its road (`car.merge`)
   * does so when there is room there beside and behind it – it waits for
   * that before the stop line.  One that finds none for a long while gives
   * up: a bus then loses its line's way.  (`listed`: the car is in the lists
   * of the cars on each lane – not while it is being moved along.)
   */
  void moveOver(TrafficCar* car, bool listed = true);
  /**
   * A bus has come onto `lane`: where is that on its line?  Further along
   * its way – or, at the end of its route, at the beginning of the line
   * back, which it then runs; a bus whose line has ended, and one that never
   * had one, takes one that runs here after a few roads.
   */
  void busOnLane(TrafficCar* car, Lane& lane);
  /** A bus of the timetables at the end of its trip (or off its way for good): it calls nowhere any more, and goes once the player is not close by. */
  void retireBus(TrafficCar* car);

  struct StopAhead {
    double d = 0;
    int id = 0;
    bool here = false;
  };
  /**
   * The next stop a bus is to call at, within 70 m: on its lane (`here`) –
   * or just beyond the junction ahead, where it has to begin braking on this
   * side (a stop a few metres into a short road would come too late) – how
   * far its front is from where it is to stand, and the pole.
   */
  std::optional<StopAhead> stopAhead(TrafficCar* car, Lane* lane, Connector* conn);
  /**
   * The stops a bus calls at along `lane`: without a line every pole the map
   * has there; on a line only those of its stops – at their poles where the
   * map has them on this lane, else where the line says (`k`: the line's way
   * the lane belongs to).  A bus that has lost its way calls nowhere until it
   * has found it again.
   */
  std::vector<BusStopOnLane> stopsFor(const Lane& lane, const BusOnLine* on);
  std::vector<BusStopOnLane> stopsFor(const Lane& lane, const BusOnLine* on, int k);

  void remove(TrafficCar* car);

  TrafficCar& aim(SelfDriving& me);

  /**
   * Walk along the way of a vehicle – its lane or connector, then the next
   * connector, then on (as `onward` says, or straight) – and call `visit`
   * every `step` metres with the point, the direction of travel there and
   * the distance from the start.  Ends after `reach` metres, at the end of
   * the way, or when `visit` returns true.
   */
  void walk(Lane* lane0, Connector* conn0, double s0, Connector* next0, const std::function<Connector*(Lane&)>& onward, double step, double reach,
            const std::function<bool(double x, double z, double tx, double tz, double dist)>& visit);

  // --- perception ----------------------------------------------------------------
  struct Leader {
    double gap = JS_INF;
    double v = 0;
  };
  /** Gap (bumper to bumper) and speed of the nearest car ahead on the planned path. */
  Leader leader(TrafficCar& car, double horizon);
  /** Distance along the car's path until it would touch the player's car, or Infinity. */
  double playerAhead(TrafficCar& car, const PlayerState& player, double horizon, const std::function<Connector*(Lane&)>& onward = nullptr);
  /** Is the way through connector `c` free of traffic it has to give way to? */
  bool mayEnter(TrafficCar& car, Connector& c, double distToLine, const PlayerState& player, bool impatient = false, double caution = 0);
  /**
   * Distance of the player from the conflict region of connector `c`
   * (positive = still approaching), or no value if the player is not on the lane
   * feeding it nor on the connector itself.
   */
  std::optional<double> playerOnPath(const PlayerState& player, Connector& c);

  void drive(TrafficCar* car, double dt, const PlayerState& player);
  /**
   * The heading of a car's way where it is: the chord from `h` metres behind
   * it to `h` ahead (as its axles would stand) – on a lane along the middle of
   * its road (whose bends do not shift it sideways); near the ends, over into
   * the path through the junction it came by or takes next.
   */
  double wayYaw(TrafficCar& car, double h);
  /** Is a car inside the junction on a path that crosses or joins connector `c` ahead of its conflict point? */
  bool boxBlocked(Connector& c);
  bool junctionEmpty(Connector& c);

  // --- pedestrians (src/sim/pedestrians) ---------------------------------------
  /**
   * Somebody walking across the road `lane` leads into, on a crossing within
   * 20 m of the junction – one of the traffic's pedestrians, or the player on
   * foot on the road there.
   */
  bool peopleCrossing(Lane& lane, const PlayerState& player);
  /** Is the player standing on the road at this crossing (between the kerbs, on its strip)? */
  bool onCrossing(const CrossingJson& c, const PlayerState& player);
};

/**
 * How to push rectangle b out of rectangle a (oriented, on the ground plane): the unit direction (from a towards b)
 * and the depth of the shortest way out – no value when they do not overlap.  The separating-axis test, keeping the
 * axis of least overlap.
 */
struct BoxPush {
  double nx = 0;
  double nz = 0;
  double depth = 0;
};
std::optional<BoxPush> boxPush(double ax, double az, double ayaw, double ahl, double ahw, double bx, double bz, double byaw, double bhl, double bhw);

/** Separating-axis test for two oriented rectangles on the ground plane. */
bool boxesOverlap(double ax, double az, double ayaw, double ahl, double ahw, double bx, double bz, double byaw, double bhl, double bhw);

}  // namespace fs

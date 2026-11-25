// SPDX-License-Identifier: AGPL-3.0-only
/**
 * HOOKS – what the simulation asks of the parts of the original that are not translated yet.
 *
 * The traffic, the rule monitor and the driver of the autopilot call into the people on foot,
 * the cyclists, the emergency vehicles, the roadworks, the kerb build-outs, the level
 * crossings, the wild animals, the bus lines and the signs with a time on them.  None of
 * those is here yet.  Each stands in this file as a class of the original's name with just
 * the members that are called – all of them for the missing part to fill in – and whoever holds
 * one holds a pointer that is null until the part exists:
 *
 *     RoadEvents* roadEvents = nullptr;   …   if (roadEvents && roadEvents->blocked(lane, s)) …
 *
 * When a part is translated, its declaration here goes and the real class takes its place
 * (the same name, the same members): nothing that calls it changes.  A test can hang a small
 * class of its own on a hook in the meantime.
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/geom.h"
#include "core/js.h"
#include "sim/insertion_map.h"
#include "sim/pedestrians.h"
#include "sim/playerstate.h"
#include "world/lanes.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

struct TrafficCar;
struct TrafficEvent;
struct PlayerSnapshot;
class RuleMonitor;

// ---- src/sim/pedestrians.ts: translated (sim/pedestrians.h) ------------------------------

// ---- src/world/stops.ts -------------------------------------------------------------------

struct StopPlace;

/** A pole of a stop (interface StopPole: what the people read of it). */
struct StopPole {
  StopPlace* place = nullptr;
  double x = 0;
  double z = 0;
  int flags = 0;
};

/** A stop place (interface StopPlace: what the people read of it). */
struct StopPlace {
  int id = 0;
  double x = 0;
  double z = 0;
  std::vector<StopPole> poles;
};

/** The stops of the map (class Stops). */
class Stops {
 public:
  virtual ~Stops() = default;
  /** the places within r metres of (x, z) */
  virtual std::vector<const StopPlace*> near(double x, double z, double r) const = 0;
};

// ---- src/sim/cyclists.ts ------------------------------------------------------------------

/** Somebody on a bicycle (interface Cyclist: what the traffic reads of them). */
struct Cyclist {
  int id = 0;
  /** the lane they ride beside, or the path through a junction they are on */
  Lane* lane = nullptr;
  Connector* conn = nullptr;
  /** how far along it (m) */
  double s = 0;
  double v = 0;
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  /** how far the pedals and the wheels have turned (rad), and whether they brake hard (for the picture) */
  double phase = 0;
  double roll = 0;
  bool braking = false;
  int colour = 0;
  /** knocked over: lies there for these seconds, then is gone */
  double down = 0;
};

inline constexpr double CYCLE_LENGTH = 1.8;
inline constexpr double CYCLE_WIDTH = 0.64;

/** People on bicycles (class Cyclists). */
class Cyclists {
 public:
  virtual ~Cyclists() = default;
  std::vector<Cyclist*> list;
  /** how many ride around near the player by themselves (those sent along for a turn come on top) */
  int target = 0;
  /** the lanes cars may appear on near a place (the traffic's spawn grid: Traffic::lanesAt) */
  std::function<const std::vector<Lane*>*(double x, double z)> lanesAt = nullptr;
  virtual void clear() = 0;
  virtual void update(double dt, const PlayerState& player, const std::vector<PlayerState*>& obstacles, const std::vector<TrafficCar*>& cars,
                      std::vector<TrafficEvent>& events) = 0;
  /** is a cyclist beside or coming up behind a vehicle that is about to turn right off `lane`, its middle at `s`? */
  virtual bool blocksRightTurn(const Lane& lane, double s, double half) = 0;
};

// ---- src/sim/emergency.ts -----------------------------------------------------------------

/** An emergency vehicle and its call (interface EmergencyCar: what the traffic reads of it). */
struct EmergencyCar {
  /** on a call: blue lights and siren on (false: a police car on patrol) */
  bool call = false;
  /** the connectors of its way after the one it takes next, in order (Traffic.nextFor hands them out) */
  std::vector<Connector*> route;
};

struct EmergencyFault {
  std::string id;
  /** 'hint' | 'minor' | 'major' | 'fail' */
  std::string severity;
};

/** Emergency vehicles on their calls, and how the others make way for them (class Emergencies). */
class Emergencies {
 public:
  virtual ~Emergencies() = default;
  struct Lead {
    double gap = JS_INF;
    double v = 0;
  };
  virtual void update(double dt, const PlayerState& player) = 0;
  /** the speed a car wants: faster on a call, slower while making way */
  virtual double desired(TrafficCar& car, double v0, double limit) = 0;
  /** how far ahead a car that makes way is to stop (Infinity: not at all) */
  virtual double stopFor(TrafficCar& car) = 0;
  virtual Lead lead(TrafficCar& car) = 0;
  /** through a junction on a call: the speed to go at most, or < 0: stop */
  virtual double through(TrafficCar& car, const Connector& next, double distToLine, bool mustStop) = 0;
  virtual const std::vector<EmergencyFault>& judge(double dt, const PlayerSnapshot& p) = 0;
};

// ---- src/sim/roadevents.ts ----------------------------------------------------------------

/** What roadworks ask of a vehicle (interface Gate). */
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

/** The roadworks and closures of the day (class RoadEvents). */
class RoadEvents {
 public:
  virtual ~RoadEvents() = default;
  virtual void step(double dt, const std::vector<TrafficCar*>& cars, const PlayerState& player) = 0;
  virtual Gate gate(const Lane& lane, double front, double v, const Connector* next) = 0;
  /** null: nothing ahead of this car */
  virtual const Gate* gateCar(TrafficCar& car) = 0;
  virtual Lane* openLane(const Lane& lane) = 0;
  virtual bool laneOpen(const Lane& lane, double front) = 0;
  /** how far left (+) of its lane a vehicle passes a closed side there (m) */
  virtual double shift(const Lane& lane, double mid) = 0;
  virtual void offset(const Lane& lane, double d, PolySample& p) = 0;
  virtual bool closed(const Lane& lane) = 0;
  virtual bool blocked(const Lane& lane, double s) = 0;
  /** the obstacles with the fences, barriers and crowds of the zones near the player added */
  virtual const std::vector<PlayerState*>& withBlocks(const std::vector<PlayerState*>& obstacles, const PlayerState& player) = 0;
  virtual double limitAt(const Edge& edge, double s, int dir, double limit) = 0;
  virtual bool passing(const Edge& edge, double s, int dir) = 0;
  virtual void judge(RuleMonitor& rules, const PlayerSnapshot& p, const Edge& edge, double s, double off, int dir, double dt) = 0;
};

// ---- src/sim/narrowings.ts ----------------------------------------------------------------

struct NarrowGate {
  double stop = JS_INF;
  double cap = JS_INF;
  /** '' | 'oncoming' */
  std::string why;
};

/** The narrowings kerb build-outs make (class Narrowings). */
class Narrowings {
 public:
  virtual ~Narrowings() = default;
  const PlayerState* player = nullptr;
  virtual double shift(const Lane& lane, double mid, double hw = 0.95, double hl = 2.3) = 0;
  virtual void offset(const Lane& lane, double d, PolySample& p, double hw = 0.95, double hl = 2.3) = 0;
  virtual bool passing(const Edge& edge, double s, int dir) = 0;
  virtual NarrowGate gate(const Lane& lane, double front, double v, double length, const TrafficCar* self) = 0;
  virtual void judge(RuleMonitor& rules, const PlayerSnapshot& p, const Edge& edge, double s, int dir) = 0;
};

// ---- src/sim/levelcrossing.ts -------------------------------------------------------------

struct LxGate {
  double stop = JS_INF;
  double cap = JS_INF;
  /** '' | 'light' | 'barrier' | 'train' | 'queue' */
  std::string why;
};

/** The level crossings (class LevelCrossings). */
class LevelCrossings {
 public:
  virtual ~LevelCrossings() = default;
  const PlayerState* player = nullptr;
  virtual void update(double dt, double x, double z) = 0;
  /** `self`: the car of the traffic that asks – or, with `isPlayer`, the player's vehicle (the original's 'player') */
  virtual LxGate gate(const Lane& lane, double front, double v, double length, const Connector* next, const TrafficCar* self, bool isPlayer = false) = 0;
  virtual void judge(RuleMonitor& rules, const PlayerSnapshot& p, const Edge& edge, double s, int dir, double dt) = 0;
};

// ---- src/sim/wildlife.ts ------------------------------------------------------------------

/** Deer and wild boar crossing the country roads at dusk (class Wildlife). */
class Wildlife {
 public:
  virtual ~Wildlife() = default;
  /** the animals on the road, as the cars see them */
  std::vector<PlayerState*> boxes;
  virtual void clear() = 0;
  virtual void update(double dt, const PlayerState& player) = 0;
};

// ---- src/world/buslines.ts ----------------------------------------------------------------

struct BusLineStop {
  /** the stop place's number in the map's stops */
  int place = 0;
  /** the way of the line it lies on (index into its ways), and how far along that road (m from its node a) */
  int k = 0;
  double m = 0;
};

/** A line of the city buses in one direction (interface BusLine: what the traffic reads of it). */
struct BusLine {
  /** its number in BusLines.lines */
  int index = 0;
  /** the line's number and where it goes */
  std::string ref;
  std::string to;
  /** the ways (road * 2 + direction) of its route in order, and how many of them are the route itself (the rest: the way round to the line back) */
  std::vector<int32_t> ways;
  int end = 0;
  /** the line back, where one begins at this one's end */
  BusLine* back = nullptr;
};

/** The lines of the city buses (class BusLines). */
class BusLines {
 public:
  virtual ~BusLines() = default;
  struct At {
    BusLine* line = nullptr;
    int k = 0;
  };
  std::vector<BusLine*> lines;
  /** the lines that run along way `w`, each with the index of that way in its ways */
  virtual std::vector<At> at(int w) = 0;
  virtual bool has(int w) = 0;
  /** the stops of `line` on its ways k0 … k1 (written into `out`, which is returned) */
  virtual std::vector<BusLineStop>& stopsOn(const BusLine& line, int k0, int k1, std::vector<BusLineStop>& out) = 0;
};

// ---- src/world/extrasigns.ts --------------------------------------------------------------

/** The signs with a time on them: which hold now, by the game's clock (class TimedRules). */
class TimedRules {
 public:
  virtual ~TimedRules() = default;
  struct Limit {
    /** km/h */
    double v = 0;
    /** what the sign's panel says ("22–6 h") */
    std::string when;
  };
  /** the limit of a timed sign on a road in a direction that holds now – none: the road's own */
  virtual std::optional<Limit> limit(int edge, int dir, bool heavy) = 0;
  /** shut now to motor vehicles (or lorries): what the panel says – '' when it is not */
  virtual std::string shut(int edge, int dir, bool heavy) = 0;
};

// --- what other files use of the parts that are not ported ---------------------------

/** the first style of the emergency vehicles (src/sim/emergency.ts EM_STYLE) */
inline constexpr int EM_STYLE = 8;

/** which emergency vehicle a style is (0 …), −1: none (src/sim/emergency.ts emKind) */
inline int emKind(int style) { return style >= EM_STYLE && style < EM_STYLE + 8 ? (style - EM_STYLE) >> 1 : -1; }

// ---- src/game/weather.ts ------------------------------------------------------------------
// (what the cranes ask of the weather: its wind.  Until game/weather is translated the weather shown is
//  always CLEAR_WEATHER – what 1.7.7 shows with the weather switched off – so these give its wind.)

/** the part of a WeatherState the cranes read: mean wind 10 m above the ground (m/s), the strongest gusts (m/s), where it comes from (degrees) */
struct WeatherState {
  double wind = 2.5;
  double gust = 5;
  double windFrom = 250;
  /** how far one sees (m: CLEAR_WEATHER's) – the headlights ask (game/beamplay.ts) */
  double visibility = 30000;
};

/** the weather shown now (game/weather.ts weatherNow: CLEAR_WEATHER until it is translated) */
inline WeatherState weatherNow() { return WeatherState{}; }

/** Where the wind blows to, and how hard (m/s): x east, z south (game/weather.ts windVector). */
template <typename Out>
Out& windVector(const WeatherState& s, Out& out) {
  const double a = (s.windFrom * 3.14159265358979323846) / 180;
  const double v = s.wind > 0 ? s.wind : 0;
  // from the north (0°) it blows to the south, +z
  out.x = -std::sin(a) * v;
  out.z = std::cos(a) * v;
  return out;
}

}  // namespace fs

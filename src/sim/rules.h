// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/rules.ts @ v1.7.7
/**
 * The driving examiner: watches the player and records violations of the
 * German traffic rules (StVO).  It judges by the same right-of-way data the
 * traffic simulation uses, so "who had to wait" is never ambiguous.
 */
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "sim/hooks.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "world/lanes.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

/** 'hint' | 'minor' | 'major' | 'fail' */
using Severity = std::string;

/** values for the message of a violation, e.g. { limit: 30, speed: 47 } – in the order they were given */
using ViolationData = std::vector<std::pair<std::string, std::variant<double, std::string>>>;

struct Violation {
  /** catalogue key, see ui/i18n */
  std::string id;
  Severity severity;
  double time = 0;
  double x = 0;
  double z = 0;
  /** values for the message, e.g. { limit: 30, speed: 47 } */
  ViolationData data;
};

struct PlayerSnapshot {
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  /** signed forward speed, m/s */
  double speed = 0;
  int indicator = 0;
  bool hazard = false;
  /** head turn, rad, + = left */
  double lookYaw = 0;
  bool seatbelt = false;
  double handbrake = 0;
  bool stalled = false;
  /** impact speed of a collision with scenery in this step (0 = none) */
  double hitScenery = 0;
  double length = 0;
  /**
   * From the point given by x, z to the front bumper (m).  Half the length
   * unless said otherwise – a truck is measured from the middle of its
   * wheelbase, which is not the middle of its length.
   */
  std::optional<double> front = std::nullopt;
};

/** What the driver should know right now – shown by the optional HUD assistance. */
struct RuleContext {
  /** speed limit in km/h, 0 = none (Autobahn), -1 = unknown/off-road */
  double limit = -1;
  /** the limit is that of a sign with a time on it: what its panel says ("22–6 h") – '' otherwise */
  std::string limitWhen;
  int zone = 0;
  bool urban = false;
  std::string roadName;
  /** upcoming junction */
  struct Junction {
    double dist = 0;
    /** ROLE of our approach */
    int role = 0;
    int ctrl = 0;
    /** signal state (SIG) or -1 */
    int signal = -1;
    /** the green arrow beside the light: SIG.GREEN or SIG.AMBER, -1 dark, -2 there is none */
    int arrow = -2;
    /** the lights of the junction are out of order: its signs count */
    bool dark = false;
  };
  std::optional<Junction> junction = std::nullopt;
  bool onRoad = false;
  bool wrongWay = false;
};

/** hint 0, minor 1, major 3, fail 10 */
extern const std::map<Severity, int> SEVERITY_POINTS;

/**
 * Where the road surface under the car cannot be found – a gap between the pieces of the
 * map, the wide shoulder, a junction area without an edge – the nearest road within this
 * many metres of its carriageway takes its place (world.locate).
 */
inline constexpr double FALLBACK_MAX_DIST = 2.2;
/**
 * How far up or down that fallback may still match a road (m).  The map has some two
 * hundred bridges with little headroom: the 50 km/h of a town road over the motorway
 * must not become the limit of the motorway under it (tools/dev/limit-sweep.ts walks
 * every motorway and slip road of the map to keep this true).
 */
inline constexpr double FALLBACK_MAX_DY = 1.5;

class RuleMonitor {
 public:
  struct Caps {
    double rural = 0;
    double motorway = 0;
  };

  World& world;
  LaneGraph& graph;
  SignalSystem& signals;
  Traffic& traffic;
  std::vector<Violation> log;
  RuleContext context;
  std::function<void(const Violation& v)> onViolation = nullptr;
  /** exam mode judges more strictly (hints become faults) */
  bool strict = false;
  /** roads the navigation sends us through: "residents only" does not count against the driver there */
  const std::unordered_set<const Edge*>* allowed = nullptr;
  /** the signs with a time on them (world/extrasigns): which hold now, by the game's clock – a hook: null until that is translated */
  TimedRules* timed = nullptr;
  double time = 0;
  /** over 3.5 t: right turns in town at walking pace (§ 9 Abs. 6 StVO) */
  bool heavy = false;
  /** On the practice yard there are no roads and no traffic: nothing here is judged. */
  bool yard = false;

  RuleMonitor(World& world, LaneGraph& graph, SignalSystem& signals, Traffic& traffic);

  /**
   * What the vehicle may do beyond what the signs say: at most `caps` km/h
   * outside built-up areas and on the motorway (the fault `capFault` when it
   * is faster), walking pace when turning right in town (`heavy`), no
   * motorway at all.
   */
  void setVehicle(const std::optional<Caps>& caps, const std::string& capFault, bool heavy, bool noMotorway);

  /** A truck with a trailer (class CE): 60 km/h outside town, 80 on the motorway, walking pace when turning right in town. (The original's property `truck`.) */
  void truck(bool on);
  bool truck() const;

  void reset();

  /**
   * The vehicle was put somewhere else (turned round at the end of a street,
   * put back on the road, set down at a place on the map): where it was, and
   * what it was in the middle of there, no longer counts.  The log stays.
   */
  void placed();

  /** The lane the monitor has the car on (kept while it crosses a junction), or null off the road. */
  Lane* laneNow() const { return lane; }

  /** How far the front bumper is along that lane (m). */
  double laneFront() const { return laneS; }

  int points() const;

  bool failed() const;

  /** A fault seen by the game rather than by the monitor (at a place: getting out, the check before the drive). */
  void noteAt(const std::string& id, const Severity& severity, double x, double z, const ViolationData& data = {}, double cooldown = 6);

  /** A fault found by somebody else (the judge of a manoeuvre): into the log like the others. */
  void note(const std::string& id, const Severity& severity, const ViolationData& data = {}, double cooldown = 3);

  void update(double dt, const PlayerSnapshot& p, const std::vector<TrafficEvent>& events);

 private:
  struct Entry {
    Node* node = nullptr;
    Arm* arm = nullptr;
    Lane* lane = nullptr;
    double time = 0;
    double indicatorL = 0;
    double indicatorR = 0;
    bool lookedL = false;
    bool lookedR = false;
    double yaw = 0;
    bool signalRed = false;
    bool onArrow = false;
    bool nowL = false;
    bool nowR = false;
    double vMax = 0;
  };
  struct RedLine {
    Lane* lane = nullptr;
    double x = 0;
    double z = 0;
    double tol = 0;
    Severity severity;
    bool lineReported = false;
  };
  struct Oncoming {
    Node* node = nullptr;
    double time = 0;
  };
  struct Hold {
    Edge* edge = nullptr;
    double s = 0;
    int dir = 0;
    double x = 0;
    double y = 0;
    double z = 0;
    double limit = 0;
    std::string when;
  };

  SurfaceHit hit = newSurfaceHit();
  SurfaceHit frontHit = newSurfaceHit();
  std::unordered_map<std::string, double> cooldown;
  Lane* lane = nullptr;
  double laneS = 0;
  double prevLaneS = 0;
  bool stoppedNearLine = false;
  std::optional<Entry> entry = std::nullopt;
  /**
   * The vehicle is a truck with a trailer: 60 km/h outside built-up areas
   * and 80 km/h on motorways (§ 3 Abs. 3 StVO), walking pace when turning
   * right in town (§ 9 Abs. 6 StVO).
   */
  std::optional<Caps> caps = std::nullopt;
  std::string capFault = "truckSpeed";
  /** not allowed on the motorway: a tractor built for 40 km/h (§ 18 Abs. 1 StVO) */
  bool noMotorway = false;
  double lastX = 0;
  double lastZ = 0;
  double indL = 0;
  double indR = 0;
  double lookL = 0;
  double lookR = 0;
  double speedOver = 0;
  double wrongSide = 0;
  double wrongWayT = 0;
  double sidewalkT = 0;
  double offRoadT = 0;
  double tailgateT = 0;
  double handbrakeT = 0;
  double lostT = 0;
  /**
   * The stop line was crossed on red.  That alone is "only" a stop-line
   * offence (Zeichen 294); the red-light offence needs the protected area
   * behind it (junction, crossing) – so we wait and see how far the car goes.
   */
  std::optional<RedLine> redLine = std::nullopt;
  double indicatorOnT = 0;
  /** an oncoming car braked hard for us at this junction (judged once it is known which way we went) */
  std::optional<Oncoming> oncoming = std::nullopt;
  Edge* lastEdge = nullptr;
  /**
   * The road the car stood solidly on last, and the limit it set there: a weak
   * match – a gap between the pieces of the map, a junction area, the nearest-road
   * fallback – must not change the limit, or the 50 km/h of a town road on a bridge
   * over the motorway (or of a road under it) becomes the limit of the motorway.
   * Only a solid hit of a road changes it; the hold lasts while the car is still
   * near that place and at its height (tools/dev/limit-sweep.ts).
   */
  std::optional<Hold> hold = std::nullopt;
  std::unordered_map<const CrossingJson*, double> crossingState;

  /**
   * The limit of the road the car stood solidly on last, while it is still within
   * `HOLD_DIST` metres and `HOLD_DY` metres in height of that place – null where the
   * hold says nothing (nothing solid stood under the car yet, or it has moved away).
   */
  const Hold* holdNow(const PlayerSnapshot& p) const;
  void report(const std::string& id, Severity severity, const PlayerSnapshot& p, const ViolationData& data = {}, double cooldown = 12);
  /** Lane of `edge` the offset/direction corresponds to, or null (wrong side / none). */
  Lane* laneFor(const Edge& edge, double s, double off, int dir) const;
  int segIndex(const Edge& edge, double s) const;
  void trackLane(Lane& lane, double s, double dt, const PlayerSnapshot& p, double fx, double fz);
  /** Over the stop line on red: stopped just behind it, or driven on into the junction? */
  void judgeRedLine(const PlayerSnapshot& p);
  /** After a junction: which way did we go, and was it announced? */
  void judgeTurn(Edge& exitEdge, const PlayerSnapshot& p);
  /** An AI car had to brake hard because of us: were we the one who had to wait? */
  void judgePriority(const TrafficCar& car, const PlayerSnapshot& p, bool cutIn = true);
  void checkCrossings(const Edge& edge, double s, int dir, const PlayerSnapshot& p);
};

}  // namespace fs

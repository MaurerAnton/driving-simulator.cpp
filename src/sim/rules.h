// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The driving examiner: watches the player and records violations of the
 * German traffic rules (StVO).  It judges by the same right-of-way data the
 * traffic simulation uses, so "who had to wait" is never ambiguous.
 *
 * Origin: src/sim/rules.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sim/signals.h"
#include "sim/traffic.h"
#include "world/lanes.h"
#include "world/world.h"

namespace fs {

enum class Severity { Hint, Minor, Major, Fail };

/** One recorded offence. */
struct Violation {
  /** catalogue key, see ui/i18n */
  std::string id;
  Severity severity = Severity::Hint;
  double time = 0;
  double x = 0, z = 0;
  /** values for the message, e.g. { limit: 30, speed: 47 } */
  std::vector<std::pair<std::string, std::string>> data;
};

/** What the driver should know right now – shown by the optional HUD assistance. */
struct RuleContext {
  /** speed limit in km/h, 0 = none (Autobahn), -1 = unknown/off-road */
  int limit = -1;
  int zone = 0;
  bool urban = false;
  std::string roadName;
  /** upcoming junction */
  struct Junction {
    bool set = false;
    double dist = 0;
    /** ROLE of our approach */
    int role = 0;
    int ctrl = 0;
    /** signal state (SIG) or -1 */
    int signal = -1;
  } junction;
  bool onRoad = false;
  bool wrongWay = false;
};

/** A snapshot of the player's car for one step. */
struct PlayerSnapshot {
  double x = 0, y = 0, z = 0;
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
  double length = 4.3;
};

/** Points a severity is worth in the exam (hint 0, minor 1, major 3, fail 10). */
int severityPoints(Severity s);

class RuleMonitor {
 public:
  RuleMonitor(World& world, LaneGraph& graph, SignalSystem& signals, Traffic& traffic);

  World& world;
  LaneGraph& graph;
  SignalSystem& signals;
  Traffic& traffic;
  std::vector<Violation> log;
  RuleContext context;
  std::function<void(const Violation&)> onViolation;
  /** exam mode judges more strictly (hints become faults) */
  bool strict = false;
  /** roads the navigation sends us through: "residents only" does not count
   *  against the driver there */
  const std::set<Edge*>* allowed = nullptr;
  double time = 0;

  void reset();
  /** The lane the driver is tracking (the original reads it as `lane`). */
  Lane* currentLane() const { return lane_; }
  /** Distance of the front bumper along that lane (the original's `laneS`). */
  double currentLaneS() const { return laneS_; }
  int points() const;
  bool failed() const;
  void update(double dt, const PlayerSnapshot& p, const std::vector<TrafficEvent>& events);

 private:
  SurfaceHit hit_;
  SurfaceHit frontHit_;
  std::map<std::string, double> cooldown_;
  Lane* lane_ = nullptr;
  double laneS_ = 0;
  double prevLaneS_ = 0;
  bool stoppedNearLine_ = false;

  struct Entry {
    Node* node = nullptr;
    Arm* arm = nullptr;
    Lane* lane = nullptr;
    double time = 0;
    double indicatorL = 0, indicatorR = 0;
    bool lookedL = false, lookedR = false;
    double yaw = 0;
    bool signalRed = false;
    bool nowL = false, nowR = false;
  };
  Entry* entry_ = nullptr;
  Entry entryStore_;
  double indL_ = 0, indR_ = 0;
  double lookL_ = 0, lookR_ = 0;
  double speedOver_ = 0;
  double wrongSide_ = 0;
  double wrongWayT_ = 0;
  double sidewalkT_ = 0;
  double offRoadT_ = 0;
  double tailgateT_ = 0;
  double handbrakeT_ = 0;
  double lostT_ = 0;
  double indicatorOnT_ = 0;
  /**
   * The stop line was crossed on red.  That alone is "only" a stop-line
   * offence (Zeichen 294); the red-light offence needs the protected area
   * behind it (junction, crossing) – so we wait and see how far the car goes.
   */
  struct RedLine {
    Lane* lane = nullptr;
    double x = 0, z = 0, tol = 0;
    Severity severity = Severity::Fail;
    bool lineReported = false;
  };
  RedLine redLine_;
  bool hasRedLine_ = false;
  Edge* lastEdge_ = nullptr;
  std::map<const CrossingJson*, double> crossingState_;

  void report(const std::string& id, Severity severity, const PlayerSnapshot& p,
              const std::vector<std::pair<std::string, std::string>>& data = {},
              double cooldown = 12);
  Lane* laneFor(const Edge& e, double s, double off, int dir);
  int segIndex(const Edge& e, double s) const;
  void trackLane(Lane& lane, double s, double dt, const PlayerSnapshot& p, double fx, double fz);
  void judgeTurn(Edge& exitEdge, const PlayerSnapshot& p);
  void judgePriority(TrafficCar& car, const PlayerSnapshot& p);
  /** Over the stop line on red: stopped just behind it, or driven on into the junction? */
  void judgeRedLine(const PlayerSnapshot& p);
  void checkCrossings(const Edge& edge, double s, int dir, const PlayerSnapshot& p);
};

}  // namespace fs

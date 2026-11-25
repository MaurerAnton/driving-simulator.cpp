// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/narrowings.ts @ v1.7.7
/**
 * Driving through the narrowings kerb build-outs make (src/world/narrowing.ts).
 *
 * A build-out stands in the carriageway: whoever comes to it moves over into
 * the gap it leaves – no further than the car needs to clear its kerb with a
 * hand's breadth to spare – and back into the lane behind it.  Where the gap
 * lets only one vehicle through at a time (a narrow street without lane
 * markings), everybody slows down to about 20 km/h, and whoever has the
 * build-out on their side waits before it while somebody comes the other
 * way (§ 6 StVO).  A choker on both sides gives neither direction the right
 * of way: whoever is there first goes first.  Nobody drives in while
 * somebody coming the other way is in it.
 *
 * The traffic (src/sim/traffic.ts) and the autopilot (src/sim/autodriver.ts)
 * ask `gate` where to stop and how fast to go, and `shift` how far to move
 * across; the rule monitor (src/sim/rules.ts) asks `judge` about the player,
 * who drives in on the side of the build-out while somebody coming the other
 * way is in the gap.  The player's car runs into the beds' kerbs and beacons
 * (`narrowColliders`, for the scenery collider).
 *
 * What the original asks of the traffic (`NarrowRoad`: its lane graph and the
 * cars on a lane) the port asks of the Traffic itself.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/geom.h"
#include "core/js.h"
#include "sim/playerstate.h"
#include "world/lanes.h"
#include "world/narrowing.h"
#include "world/world.h"

namespace fs {

class Traffic;
struct TrafficCar;
class RuleMonitor;
struct PlayerSnapshot;
class SceneryCollider;

/** how fast through a single-lane narrowing (m/s: 20 km/h) */
inline constexpr double NARROW_SPEED = 20 / 3.6;
/** over how many metres a vehicle moves across into the gap, and back */
inline constexpr double NARROW_RAMP = 5;
/** how far before the bed the front of a vehicle that waits stands (m) – before it has moved across */
inline constexpr double NARROW_WAIT = 6;

/** What `gate` asks of a vehicle: where to stop (m ahead of its front; Infinity: nowhere), how fast at most. */
struct NarrowGate {
  double stop = JS_INF;
  double cap = JS_INF;
  /** '' | 'oncoming' */
  std::string why;
};

/** Is the build-out on the right of a vehicle going `dir` along its road (1), on its left (−1) – or on both sides, nobody first (0)? */
int sideFor(const Narrowing& n, int dir);

/** How far across (m, + left of the middle of `lane`) a vehicle of half width `hw` keeps in the gap of `n`. */
double gapShift(const Lane& lane, const Narrowing& n, double hw);

class Narrowings {
 public:
  Traffic& road;
  /** the player's vehicle (set each step by the traffic): the cars wait for it, too */
  const PlayerState* player = nullptr;

  explicit Narrowings(Traffic& road) : road(road) {}

  /**
   * How far a vehicle (half width `hw`, half length `hl`) whose middle is
   * `mid` metres along `lane` is drawn to the left of the middle of its lane
   * to pass a build-out (m; 0 elsewhere).
   */
  double shift(const Lane& lane, double mid, double hw = 0.95, double hl = 2.3);

  /** Moves a point `d` metres along `lane` (sampled into `p`, the direction of travel tx, tz) to where a vehicle passes the build-outs. */
  void offset(const Lane& lane, double d, PolySample& p, double hw = 0.95, double hl = 2.3);

  /** Is a vehicle going `dir` at station `s` of `edge` moved across for a build-out there (no fault to be left of the middle)? */
  bool passing(const Edge& edge, double s, int dir);

  /**
   * What the narrowings ask of a vehicle on `lane` whose front is `front`
   * metres along it, `length` long, going at `v` (`self`: a car of the
   * traffic, or null for the player's own): how fast, and where to wait for
   * the oncoming traffic.
   */
  NarrowGate gate(const Lane& lane, double front, double v, double length, const TrafficCar* self);

  /**
   * The player's fault (called by the rule monitor with where the vehicle
   * is: its road, station and direction): into a narrowing on the side of
   * its build-out while somebody coming the other way is in it, or about to
   * be (§ 6 StVO).
   */
  void judge(RuleMonitor& rules, const PlayerSnapshot& p, const Edge& edge, double s, int dir);

 private:
  /** Somebody coming the other way through a narrowing: its front's distance to its wait line (negative: past it) and to the end. */
  struct Other {
    double wait = 0;
    double rear = 0;
    double exit = 0;
    double v = 0;
    int64_t id = 0;
  };
  std::vector<Other> others;
  PolyProjection proj;
  struct Front {
    const Edge* edge = nullptr;
    int dir = 0;
    double s = 0;
    double t = 0;
  };
  std::optional<Front> prevFront;

  /** Those coming the other way through `n` (from the lane of `lane`'s road going the other way): the traffic, and the player's vehicle. */
  std::vector<Other>& oncoming(const Lane& lane, const Narrowing& n, const TrafficCar* self);
};

/**
 * The beds and beacons of the build-outs of these roads, for the scenery
 * collider: x1, z1, x2, z2, y per kerb segment (the side towards the lanes
 * and both ends), and x, y, z, radius per beacon post.
 */
template <class Edges>
void narrowColliders(const Edges& edges, std::vector<float>& segs, std::vector<double>& discs);
void narrowCollidersOf(const Edge& e, std::vector<float>& segs, std::vector<double>& discs);

/** Puts the build-outs of these roads into the collider (`on`), or takes them out again – one source per road. */
void narrowCollideOne(SceneryCollider& collider, const Edge& e, bool on);
template <class Edges>
void narrowCollide(SceneryCollider& collider, const Edges& edges, bool on) {
  for (const auto& e : edges) narrowCollideOne(collider, *e, on);
}

template <class Edges>
void narrowColliders(const Edges& edges, std::vector<float>& segs, std::vector<double>& discs) {
  for (const auto& e : edges) narrowCollidersOf(*e, segs, discs);
}

}  // namespace fs

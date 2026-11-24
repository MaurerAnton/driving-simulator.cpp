// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/pastblock.ts @ v1.7.7
/**
 * Getting past what blocks the way.
 *
 * A car of the traffic that stands behind something that does not move – a
 * broken-down or crashed car, a bus at its stop, a parked vehicle, the
 * player's car standing in the lane – for more than a few seconds looks for
 * a way past it, as a driver would:
 *
 *  - squeezing by within its own lane, where the thing stands far enough to
 *    the side;
 *  - over into a free lane beside it that goes the same way (the traffic's
 *    own moving over, `TrafficCar.merge`, once there is a gap there);
 *  - on a road with one lane each way, over the oncoming lane – only where
 *    that is clear for long enough to get by and back, never over a solid
 *    centre line unless the thing stands there for good (a parked vehicle);
 *  - or half onto the verge beside the road, slowly, where there is one to
 *    drive on (outside town, no pavement, no other road there).
 *
 * Never through people, cyclists or other cars; never in a junction or just
 * before one (whoever stands near a junction is waiting for it); not in the
 * reach of roadworks, which have ways of their own (src/sim/roadevents).
 * Whatever does not count as a block – a car of the traffic that waits in a
 * queue, somebody on foot – is simply waited behind.
 *
 * A car going round something is drawn off the middle of its lane by
 * `past.off` (m, + to the left) and moves across at most RATE metres for each
 * metre it drives; while it is off its lane it looks for what is in its way
 * by where things are rather than by the order of the cars on the lanes, and
 * the others see it as an obstacle (its `box`).  The player's autopilot
 * (src/sim/autodriver) goes round in the same way.
 */
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/geom.h"
#include "core/js.h"
#include "sim/hooks.h"
#include "sim/playerstate.h"
#include "world/lanes.h"
#include "world/world.h"

namespace fs {

struct TrafficCar;
class Traffic;

/** how it gets by: within its lane, over into the lane beside, over the oncoming lane, half onto the verge – '' | 'squeeze' | 'lane' | 'oncoming' | 'verge' */
using PastKind = std::string;
/** standing behind it (counting the seconds), moving out, beside it, back into its lane – 'wait' | 'out' | 'by' | 'back' */
using PastPhase = std::string;

/** A car's going round something in its way. */
struct PastState {
  PastPhase phase = "wait";
  /** seconds in this phase (standing still, for 'wait') */
  double t = 0;
  /** seconds until it looks for a way past again */
  double look = 0;
  /** how far left (+) or right (−) of the middle of its lane it is now, and where it is going (m) */
  double off = 0;
  double to = 0;
  /** the sideways movement of the last step per metre driven (for turning the car a little) */
  double turn = 0;
  /** where along its lane the middle of the car is once it is past (its rear clear of the thing's front) */
  double end = 0;
  /** at most this fast while it goes round (m/s) */
  double cap = JS_INF;
  PastKind kind;
  /** the lane it goes round on */
  Lane* lane = nullptr;
  /** it waits, half out, for something coming the other way to pass */
  bool hold = false;
  /** how the others see it while it is off its lane */
  PlayerState box;
};

/** most metres sideways for each metre forward */
inline constexpr double RATE = 0.6;
/** seconds standing behind something before looking for a way past */
inline constexpr double PATIENCE = 4;
/** a thing nearer than this ahead of the front (m) is what holds the car up */
inline constexpr double NEAR = 9;

/**
 * (A state is held by the car it belongs to – and, for the autopilot, handed to and fro
 * between the driver and the traffic's probe, as the original hands the object: shared.)
 */
std::shared_ptr<PastState> newPast();

/** How far off the middle of its lane the car's way runs `dist` metres ahead of it (it moves across at RATE per metre). */
double offsetAt(const PastState& p, double dist);

/** What the module needs of the traffic (the original's interface PastRoad; src/sim/traffic implements it). */
using PastRoad = Traffic;

/** What `plan` decides. */
struct PastPlan {
  PastKind kind;
  /** where to go (m left of the middle of the lane; for 'lane' the lane) */
  double to = 0;
  Lane* lane = nullptr;
  double end = 0;
  double cap = 0;
};

class PastBlocks {
 public:
  PastRoad& road;

  struct Lead {
    double gap = JS_INF;
    double v = 0;
  };

  explicit PastBlocks(PastRoad& road);

  /** The cars that are off their lanes now, as obstacles for the others (added to `out`). */
  void boxes(const std::vector<TrafficCar*>& cars, std::vector<PlayerState*>& out);

  /**
   * The nearest car of the traffic in the way of a car that goes round
   * something – found by where the cars are, not by the order on the lanes
   * (gap = Infinity: none within `horizon`).  While it holds for oncoming
   * traffic it has a gap of nothing.
   */
  Lead lead(TrafficCar& car, double horizon);

  /**
   * One step of a car's going round (called after it has decided how hard
   * to brake): counting how long it has stood behind something, looking for
   * a way past, the phases of the manoeuvre and the indicator.  `gap`: to
   * what is ahead of it; `held`: it stands for a reason of the road (a line,
   * a light, roadworks) – not because something is in the way.  `self`: the
   * player's autopilot (it moves over to the lane beside by going round, too).
   */
  void look(TrafficCar& car, double dt, double gap, bool held, const PlayerState* player, bool self = false);

  /**
   * Move the car across towards where it is going, `ds` metres driven this
   * step.  Going back it may also creep across standing (a driver who has
   * stopped half out turns the wheel and rolls back in).
   */
  void slide(TrafficCar& car, double ds, double dt);

  /**
   * The way past what stands in front of a car, if there is one now – or
   * null (nothing to go round, or no way to: wait).
   */
  std::optional<PastPlan> plan(TrafficCar& car, const PlayerState* player, bool self = false);

 private:
  /** One thing near a car, in the frame of its lane: along (a, + ahead of its middle), left (l), half extents, speed along the lane. */
  struct Thing {
    const void* who = nullptr;
    double a = 0;
    double l = 0;
    double hl = 0;
    double hw = 0;
    double v = 0;
    /** may be gone round (it stands, and is a block) */
    bool block = false;
    /** stands there for good */
    bool permanent = false;
    /** somebody on foot or on a bicycle: kept further from */
    bool person = false;
    /** part of what is being gone round */
    bool part = false;
  };

  std::vector<Thing> things;
  PlayerState box;
  PolySample smp = newPolySample();
  SurfaceHit hit = newSurfaceHit();
  /** the states whose boxes are in the others' way this step: kept until the next, whatever their cars do with them */
  std::vector<std::shared_ptr<PastState>> listed;

  /** It has stood half out for too long (whoever is in the way waits for it, too): back, and try again later. */
  void giveUp(PastState& p);
  /** It left the lane it went round on (it should not): straight back to the middle of its way. */
  void drop(TrafficCar& car);
  /** The things near a car, in the frame of its lane at its middle (false: it is not on a lane). */
  bool gather(TrafficCar& car, const PlayerState* player, bool self);
  /**
   * Is the way `to` metres left of the middle of the lane free from the car
   * to `until` metres ahead – nobody in it, nothing coming along it that
   * would get there before the car is through (at about `cap`)?
   */
  bool free(const TrafficCar& car, double to, double until, double cap) const;
  /** Re-check the way out while moving out (things move). */
  bool clearFor(TrafficCar& car, double to, double end, const PlayerState* player, bool self);
  /** Is the verge beside the lane drivable where the car's outer wheels would run (no other road, no pavement, no junction)? */
  bool verge(const TrafficCar& car, double to, double until);
};

}  // namespace fs

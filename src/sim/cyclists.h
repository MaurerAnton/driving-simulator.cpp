// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/cyclists.ts @ v1.7.7
/**
 * People on bicycles.
 *
 * They ride on the pavement side of the rightmost lane of a street in town
 * (the cycle track beside the carriageway), straight on through the
 * junctions, at 14 to 26 km/h.  That puts them exactly where the driver of a
 * vehicle that turns right has to look for them: beside and behind, where
 * the mirrors do not show them (§ 9 Abs. 3 StVO: whoever turns lets the
 * cyclists who ride straight on pass first).
 *
 * Some just ride around near the player.  Others are sent along on purpose,
 * out of sight, when the player's vehicle comes up to a junction where it
 * can turn right:
 *
 *  - one rides ahead on the cycle track; the vehicle passes them before the
 *    junction, and they arrive there a moment after it – beside its rear
 *    when it wants to turn;
 *  - one comes up from far behind while the vehicle waits at the line (a
 *    red light, a give-way sign), and stands or rides beside it when it
 *    sets off.
 *
 * Whoever looks over the shoulder sees them and lets them pass.  Whoever
 * does not is in their way: they brake hard (`cyclistEndangered`), or are
 * knocked over (`hit`).
 *
 * Towards everything else they ride defensively: they stop at red lights
 * and for whatever stands or drives on their line.  The cars of the traffic
 * wait for them before turning right (`blocksRightTurn`) and brake for them
 * like for anything else in their way.
 *
 * (The original's CyclePlayer and CycleObstacle are the traffic's PlayerState – the player's
 * vehicle, a trailer of the player's marked `own`, trams – and its cars; CycleSink is the
 * traffic's list of events.)
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "core/geom.h"
#include "sim/playerstate.h"
#include "world/lanes.h"
#include "world/world.h"

namespace fs {

class SignalSystem;
struct TrafficCar;
struct TrafficEvent;

struct Cyclist {
  int id = 0;
  /** the lane they ride beside, or the path through a junction they are on */
  Lane* lane = nullptr;
  Connector* conn = nullptr;
  /** how far along it (m) */
  double s = 0;
  double v = 0;
  /** the speed they like to ride at (m/s) */
  double cruise = 0;
  /** the path they take at the end of the lane */
  Connector* next = nullptr;
  /** how far to the right of the middle of the lane they ride (m) */
  double out = 0;
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  /** how far the pedals and the wheels have turned (rad), and whether they brake hard (for the picture) */
  double phase = 0;
  double roll = 0;
  bool braking = false;
  int colour = 0;
  /**
   * Sent along for a turn of the player's: the last lane before the junction
   * they are to be at, and how long after the player's vehicle they mean to
   * be there (s) – they ride a little faster or slower for it.
   */
  Lane* staged = nullptr;
  double lag = 0;
  /** knocked over: lies there for these seconds, then is gone */
  double down = 0;
  /** when the player was last blamed for being in the way of this one */
  double told = 0;
  /** the player's vehicle was on their line a moment ago: counts as seen for this many more seconds */
  double seen = 0;
  /** for how long they have been standing because something is in the way (s) */
  double held = 0;
  /** waiting at a red light */
  bool atLight = false;
};

/** a bicycle with its rider, in the plan (m) */
inline constexpr double CYCLE_LENGTH = 1.8;
inline constexpr double CYCLE_WIDTH = 0.64;

/**
 * How far to the right of the middle of `lane` a cyclist rides: on the
 * pavement beside it – clear of the sign posts at the kerb and of the lamps
 * at the back –, or on the verge.
 */
double cycleStrip(const Lane& lane);

/** May cyclists turn up beside this lane?  The rightmost lane of a street in town with a pavement – not of a fast road. */
bool rideable(const Lane& lane);

class Cyclists {
 public:
  std::vector<Cyclist*> list;
  /** how many ride around near the player by themselves (those sent along for a turn come on top) */
  int target = 0;
  /** are cyclists sent along when the player comes up to a right turn?  And how often (0 … 1 of the approaches)? */
  bool staging = true;
  double chance = 0.4;
  /** where cyclists may appear: the lanes around a place (given by the traffic, which keeps such a list) */
  std::function<const std::vector<Lane*>*(double x, double z)> lanesAt = [](double, double) -> const std::vector<Lane*>* { return nullptr; };

  Cyclists(World& world, LaneGraph& graph, SignalSystem& signals);
  ~Cyclists();
  Cyclists(const Cyclists&) = delete;
  Cyclists& operator=(const Cyclists&) = delete;

  void clear();

  /** Put a cyclist beside `lane`, `s` metres along it (for the staging, and for tests). */
  Cyclist* add(Lane& lane, double s, double cruise, bool staged = false, double lag = 1.5);

  /**
   * One step.  `obstacles`: what else is on the roads as boxes (a trailer of
   * the player's – marked `own` –, trams), `cars`: the traffic's cars; what
   * happens to the cyclists because of the player is added to `events`.
   */
  void update(double dt, const PlayerState& player, const std::vector<PlayerState*>& obstacles, const std::vector<TrafficCar*>& cars,
              std::vector<TrafficEvent>& events);

  struct UntilJunction {
    Lane* last = nullptr;
    double dist = 0;
  };
  /**
   * From `s` metres along `lane` on to the next junction with three arms or
   * more – through joints where a street goes on in one piece only: the last
   * lane before it and how far it is to its stop line.  None beyond 320 m.
   */
  std::optional<UntilJunction> untilJunction(Lane& lane, double s);

  /**
   * Must a vehicle that wants to turn right from `lane` wait for a cyclist?
   * `s`: where its middle is along the lane, `half`: half its length.  Yes,
   * while one comes up behind it, rides beside it, or has only just ridden
   * into the junction ahead of it.
   */
  bool blocksRightTurn(const Lane& lane, double s, double half);

  /** The cyclist nearest to a place within `reach` metres, or null. */
  Cyclist* nearest(double x, double z, double reach);

 private:
  World& world;
  LaneGraph& graph;
  SignalSystem& signals;
  Rng rand = rng(90210);
  SurfaceHit hit = newSurfaceHit();
  int nextId = 1;
  double time = 0;
  double spawnT = 1;
  /** when a cyclist may next be sent along */
  double stageAt = 6;
  /** the approach for which it was last decided whether one rides ahead / comes up from behind */
  Lane* decidedAhead = nullptr;
  Lane* decidedBehind = nullptr;
  /** for how long the player's vehicle has been standing at the line (s) */
  double waiting = 0;
  PolySample tmp = newPolySample();
  PolySample tmp2 = newPolySample();
  /** (those taken away in the step before: what was said about them in it still points at something) */
  std::vector<std::unique_ptr<Cyclist>> gone;

  void drop(size_t i);
  Connector* pick(const Lane& lane);
  Lane* rightmost(Lane& lane);
  double ahead(const Cyclist& c, double d, PolySample& out);
  void place(Cyclist& c);
  void ride(Cyclist& c, double dt, const PlayerState& player, const std::vector<PlayerState*>& obstacles, const std::vector<TrafficCar*>& cars,
            std::vector<TrafficEvent>& events);
  void spawn(const PlayerState& player);
  void stage(const PlayerState& player, double dt);
  struct Back {
    Lane* lane = nullptr;
    double s = 0;
  };
  std::optional<Back> backFrom(Lane& lane, double s, double back);
};

}  // namespace fs

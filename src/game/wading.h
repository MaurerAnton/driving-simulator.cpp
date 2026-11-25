// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/wading.ts @ v1.7.7
/**
 * Driving through water: rivers, lakes and ponds are cover WATER on the map
 * (world/cover.h) – and here they get a depth, and what water does to a
 * vehicle driving in.
 *
 *  - The depth (WaterDepth): the farther in from the shore, the deeper –
 *    WATER_DROP at the very edge (the surface lies that far below the bank,
 *    render/water.h), then a bank sloping down to DEEP_WATER.  How far a
 *    point is from the shore is found by looking round it on the cover map,
 *    on rings of growing radius; the answers are kept for the corners of a
 *    2 m grid and blended between them, so the bed has no steps.  WorldGround
 *    (sim/ground.h) puts the wheels on that bed: the vehicle sinks into the
 *    water as far as it is deep.  How deep a wheel really stands in is
 *    worked out again from the height of the ground (wheelDepth): over a
 *    bridge the wheels are metres above the river, and only the water they
 *    reach into counts.
 *  - The car gets washed: its dirt goes down while it drives through water,
 *    faster with speed and with depth (washRate).
 *  - The brakes get wet: for a while after, until braking has dried them,
 *    they brake less (brakeFactor) – a tip says so once.
 *  - Water up to the engine's air intake (intakeHeight): the engine stalls,
 *    and will not run while the water is that deep (a tip says why).
 *  - Splashes and spray from the wheels, more with speed, and the foam of a
 *    wake behind the vehicle (render/water.h's WaterSpray) – not on the low
 *    graphics preset.
 * The pure parts (depth, rates, factors) are exported for the tests.
 */
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "engine/engine.h"
#include "render/water.h"

namespace fs {

class Vehicle;

/** the deepest the water gets (m) – far enough from the shore */
inline constexpr double DEEP_WATER = 1.8;
/** how the bed slopes down from the shore (m of depth per m from the shore) */
inline constexpr double BANK_SLOPE = 0.22;

/** How deep the water is (m) at `shore` metres from the shore (0: at the very edge). */
double bedDepth(double shore);

/**
 * How fast driving through water washes the dirt off (dirt 0 … 1 per
 * second) at `speed` m/s in water `depth` m deep: a little standing in it,
 * much more as the water rushes past – and the deeper, the more of the body
 * it reaches.
 */
double washRate(double speed, double depth);

/** what is left of the brakes' force with wet discs (wet 0 … 1) */
double brakeFactor(double wet);

/** How fast the discs dry (wetness per second): slowly by themselves, quickly when braking while moving. */
double dryRate(double speed, double brake);

/** Does water `depth` m deep wet the brakes, at `speed` m/s?  Up to the hubs – or less, splashing at speed. */
bool wetsBrakes(double depth, double speed);

/** How high the engine draws its air (m above the ground): the water above it stalls the engine. */
double intakeHeight(const std::string& model);

/** How many drops a wheel throws up per second at `speed` m/s in water `depth` m deep. */
double sprayRate(double speed, double depth);

/**
 * what of the terrain is needed: the cover class at a place (render/tiles.h's
 * TileManager fits) – and, if it can, the height of the ground there, so that
 * water under a bridge is not taken for water the wheel stands in.
 */
struct CoverSource {
  std::function<int(double x, double z)> coverAt;
  /**
   * the height of the ground (over water: the water's surface, the bank's level)
   * at a place, NaN where the tile is not loaded.  Sources without it (the tests)
   * leave the depth as the cover map gives it.
   */
  std::function<double(double x, double z)> heightAt = nullptr;
};

/**
 * How deep the water a wheel stands in really is (m): `d0` is what the cover map
 * says at that place, `ground` the height of the water's surface (the bank's
 * level; NaN where it is not known) and `wheelBottom` the height of the wheel's
 * lowest point.  The water reaches up to `ground` and no further: in the river
 * the wheel stands on the bed `d0` down and the depth is the map's, on a bridge
 * the wheel lies high above the river and the river below it counts for nothing,
 * in between (a ramp, an embankment) it grows smoothly.
 */
double wheelDepth(double d0, double ground, double wheelBottom);

/**
 * The water's depth at a place, from the cover map: 0 where it is not water.
 * Shore distances are kept for the corners of a 2 m grid (forgotten now and
 * then – tiles that were not loaded yet may have come since).
 */
class WaterDepth {
 public:
  explicit WaterDepth(CoverSource src) : src_(std::move(src)) {}

  /** depth of the water (m) at x, z – 0 on land */
  double at(double x, double z);
  /** Forget the shore distances (tiles were loaded or dropped). */
  void clear() { cache_.clear(); }

 private:
  CoverSource src_;
  std::unordered_map<double, double> cache_;

  double corner(double i, double j);
  /** how far (m) the shore is from x, z: the first ring that reaches land, less half a step; 0 on land */
  double shore(double x, double z) const;
};

/** what happened in a step that the driver should be told: the i18n key of a tip ('waterStalled' | 'wetBrakes' | '' for none) */
using WadingTip = std::string;

/**
 * The player's vehicle in the water: once a frame, before the physics,
 * step() looks at the water under the wheels, washes, wets the brakes,
 * stalls the engine, throws up the spray.  `group` goes into the scene.
 */
class Wading {
 public:
  WaterDepth depth;
  WaterSpray spray{600};
  const std::shared_ptr<Group> group = make<Group>();
  /** the deepest water under a wheel now (m; 0: not in water) */
  double depthNow = 0;
  /** how wet the brake discs are (0 … 1) */
  double wet = 0;

  explicit Wading(CoverSource src);

  /** what is left of the brakes now (multiply the brake pedal by it) */
  double brakeFactor() const { return fs::brakeFactor(wet); }

  /** A new drive, or the vehicle was put somewhere else: dry, no spray. */
  void reset();

  /**
   * One frame of `dt` seconds: `v` the vehicle (null: none in the water –
   * on foot, a tram, flying: the spray only settles), `model` its kind
   * (vehicles.h's ModelKind), `dirt` what holds its dirt (serviceworld's
   * ServiceWorld::dirt), `brake` the brake pedal (0 … 1), `effects` the spray
   * drawn (not on the low preset), `light` how bright the day is (1: clear
   * noon).  Returns a tip to show, or ''.
   */
  WadingTip step(double dt, Vehicle* v, const std::string& model, double& dirt, double brake, bool effects, double light = 1);

  void dispose() { spray.dispose(); }

 private:
  CoverSource src_;
  bool wetTold_ = false;
  double clearT_ = 0;
  double wakeT_ = 0;
  std::array<double, 4> carry_{0, 0, 0, 0};
  SprayParticle p_;

  /** drops thrown up by wheel i (sideways and up, more and higher with speed) */
  void splash(size_t i, const Vehicle& v, double d, double h);
  /** foam lying on the water behind the vehicle, drifting outwards and spreading */
  void wake(const Vehicle& v, double d, double h);
};

}  // namespace fs

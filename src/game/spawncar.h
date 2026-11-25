// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/spawncar.ts @ v1.7.7
/**
 * "A car here": on foot – or at the controls of a crane – the player can have
 * a car put down beside them, at a place tapped on the 3D picture, or at a
 * place chosen on the large map.  It stands there, engine off, until it is
 * tapped (or the door key is pressed beside it): then the player sits in it,
 * and a free drive with it begins where it stands.  The vehicle driven till
 * then stays where it was left – and can be tapped to be driven again.
 *
 * Only where the world is in memory (the loaded tiles, a dashed line round
 * them on the large map) and only where a car stands free: in the lane of the
 * nearest road if there is one near – or, with "place precisely", at the
 * very place chosen – never in a wall nor inside a building (the reckoning:
 * game/spawnplace.ts).
 *
 * The button: a round one at the left edge, shown whenever a car may be put
 * down.  A tap on it, then a tap on the ground: the car stands there.  A
 * second tap on the button instead: beside the player.  The game (game.ts)
 * tells this what it needs through `SpawnCarHost`, and does the change of
 * vehicle itself (`enter`).
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "game/spawnplace.h"
#include "game/vehicles.h"
#include "platform/dom.h"

namespace fs {

class CarExterior;
class SceneryCollider;

/** A vehicle standing on the map that the player can get into. */
struct Parked {
  VehicleKind kind;
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  Outline outline;
  double height = 0;
  /** what draws it */
  ObjectPtr root;
  /** drawn by a car of this module's own (else by the game's model of that vehicle, left where it stood) */
  bool own = false;
};

/** The vehicle the player leaves behind when getting into a parked one (root null: a car – drawn by one of this module's own). */
struct LeftVehicle {
  VehicleKind kind;
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  Outline outline;
  double height = 0;
  ObjectPtr root;
};

/** a place with a heading (x, z, yaw) */
struct SpawnCarPlace {
  double x = 0;
  double z = 0;
  double yaw = 0;
};

/** a circle that a car must keep clear of */
struct SpawnCarCircle {
  double x = 0;
  double z = 0;
  double r = 0;
};

struct SpawnCarHost {
  std::shared_ptr<Scene> scene;
  std::shared_ptr<Camera> camera;
  dom::El canvas;
  /** where the button goes */
  dom::El ui;
  SceneryCollider* collider = nullptr;
  /** the terrain's height (NaN: not loaded) and the height of what one stands on there (a road, a bridge) */
  std::function<double(double x, double z)> heightAt;
  std::function<double(double x, double z)> ground;
  /** may a car be put down now (on foot, at a crane – in a free drive)? may a parked vehicle be got into now? */
  std::function<bool()> canSpawn;
  std::function<bool()> canEnter;
  /** where the player is (on foot: the person; at a crane: the crane) and which way they face */
  std::function<SpawnCarPlace()> player;
  /** what a car must keep clear of besides the scenery: the player's vehicle and the person on foot (circles x, z, r) */
  std::function<std::vector<SpawnCarCircle>()> others;
  /** a place in a lane of a road near (x, z) – the roads round there are loaded – or null */
  std::function<std::optional<SpawnCarPlace>(double x, double z, double maxDist)> roadSpot;
  /** "place precisely": at the very place chosen, not in the nearest lane */
  std::function<bool()> exact;
  /** the car's paint */
  std::function<int()> paint;
  /** the car being put down: its outline */
  Outline carOutline;
  /** into it: the game begins a free drive with it there */
  std::function<void(Parked& p)> enter;
  std::function<void(const std::string& text)> tip;
  std::function<std::string(const std::string& key)> t;
};

class SpawnCars {
 public:
  LoadedArea area;
  Footprints footprints;
  /** the car put down, waiting; the vehicle left behind (null: none) */
  std::unique_ptr<Parked> car;
  std::unique_ptr<Parked> left;
  /** the button was tapped: the next tap on the ground puts the car there */
  bool picking = false;

  explicit SpawnCars(SpawnCarHost host);
  ~SpawnCars();
  // (the page's listeners name this object: it stays where it is)
  SpawnCars(const SpawnCars&) = delete;
  SpawnCars& operator=(const SpawnCars&) = delete;

  /** A tile of the world came or went: the loaded area, the footprints of its buildings. */
  template <class Pts, class Counts>
  void tile(const std::string& key, const TileBox& box, const Pts* pts, const Counts* counts, bool loaded) {
    area.set(key, box, loaded);
    if (loaded) footprints.set(key, pts, counts);
    else footprints.remove(key);
  }

  /** Every frame: the button shown when a car may be put down. */
  void frame();

  /**
   * Is a car free standing at (x, z), facing yaw: on the loaded area, on
   * known ground, in no wall, inside no building – and (`others`) clear of
   * the player and the player's vehicle.  (`o` null: the car's outline.)
   */
  bool free(double x, double z, double yaw, const Outline* o = nullptr, bool others = true);

  /** May a car be put at (x, z)?  (On the loaded area, a few metres inside its edge.) */
  bool canPlaceAt(double x, double z);

  /** The car, beside the player (in the lane of the road there, if one is near). */
  bool spawnBeside();

  /** The car at a place chosen (tapped on the 3D picture, or on the map). */
  bool spawnAt(double x, double z, std::optional<double> yaw = std::nullopt);

  /** Is this the model of the vehicle left behind (the game keeps it drawn where it stands)? */
  bool keeps(const Object3D* root) const;

  /**
   * The player gets into `p`: it is no longer parked – and the vehicle driven
   * till now (if it is one that can stay) is left standing where it is.
   * (`p` may be one of this module's own: it is gone after this.)
   */
  void swap(const Parked* p, const LeftVehicle* from);

  /** Nothing parked any more (back to the menu). */
  void clear();

  /** The parked vehicle somebody on foot at (x, z) stands beside (within `reach` metres of its outline), or null. */
  Parked* near(double x, double z, double reach = 1.6);

  /** Is a tap at (cx, cy) this module's – a place for a car being picked, or a parked vehicle tapped to get in (for the other taps on the picture: ui/worldtap.ts)? */
  bool claims(double cx, double cy);

  /** A tap on the 3D picture: into a parked vehicle tapped – or, after the button, the car put down where the ground was tapped. */
  void tap(double cx, double cy);

 private:
  void pressed();
  bool put(const std::optional<FreePlace>& at);
  /** A car of this module's own that is not in use, painted as the player's. */
  CarExterior& exterior();
  /** A parked vehicle in its place: drawn (if it is one of ours), and in the way of others. */
  void stand(Parked& p, const std::string& slot);
  /** A parked vehicle gone: out of the way, and ours back into the pool. */
  void drop(Parked& p, const std::string& slot);

  SpawnCarHost host;
  dom::El button;
  bool shown = false;
  /** the cars of this module's own (at most two are needed: the one put down, a car left behind) */
  std::vector<std::unique_ptr<CarExterior>> pool;
  Raycaster ray;
  Vector2 ndc;
  struct Down {
    int id = 0;
    double x = 0;
    double y = 0;
    double t = 0;
  };
  std::optional<Down> down;
  /** the rings of the parked vehicles for the collider (it reads them when they are added) */
  std::vector<float> ringCar, ringLeft;
  std::vector<uint16_t> ringCounts{4};
};

}  // namespace fs

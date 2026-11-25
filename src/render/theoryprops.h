// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/theoryprops.ts @ v1.7.7
/**
 * The things of the later theory lessons (game/theoryscenes.ts) that are
 * neither road nor vehicle, drawn for render/theory.ts:
 *
 *  - the props of a place: trees (a forest), guide posts, a warning
 *    triangle, the barriers and cones of roadworks, a heap of earth, a
 *    tunnel over the road (its walls and roof see-through, so that the view
 *    from above still shows what happens inside);
 *  - what people hold: the rollator an old lady walks with, the stop/go
 *    sign of a flagman (red one side, green the other, turned to show);
 *  - the animals (render/animals.ts), from a pool;
 *  - the headlights in the dark: the light of a vehicle's dipped or full
 *    beam drawn on the road ahead of it – how far it reaches is what the
 *    night lesson is about.
 *
 * Everything is built for one lesson and taken down with it; the shared
 * shapes and materials stay.
 */
#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "engine/engine.h"
#include "game/theory.h"
#include "render/animals.h"

namespace fs {

/** Where the stage lies on the map: its points and headings in the map's (render/theory.ts StageFrame). */
struct StageFrame {
  std::function<std::array<double, 2>(double x, double z)> toWorld;
  std::function<double(double yaw)> yawToWorld;
};

class TheoryExtras {
 public:
  const std::shared_ptr<Group> group = make<Group>();

  TheoryExtras();
  TheoryExtras(const TheoryExtras&) = delete;
  TheoryExtras& operator=(const TheoryExtras&) = delete;

  // ------------------------------------------------------------------ props
  /** The props of a place, on the ground of the map. */
  void build(const theory::Stage& st, const StageFrame& f, const std::function<double(double x, double z)>& height);

  // ---------------------------------------------------------------- animals
  /** A deer or a boar for the lesson (from the pool). */
  AnimalModel* animal(const AnimalKind& kind);

  // -------------------------------------------------------- what people hold
  /**
   * Place what the person of actor `i` holds: a rollator ahead of somebody
   * old, a flagman's sign turned to its side of the moment (`t`: the
   * script's time).  At (x, y, z) facing `yaw` (on the map).
   */
  void hold(int i, const theory::ActorDef& def, double t, double x, double y, double z, double yaw);

  /** Hide what actor `i` holds (not there at this moment). */
  void drop(int i);

  // -------------------------------------------------------------- headlights
  /** The light of actor `i`'s headlights at time t, ahead of its nose (`front` m ahead of (x, y, z)), facing `yaw`. */
  void light(int i, const theory::ActorDef& def, double t, double x, double y, double z, double yaw, double front);

  /** Each frame: the props of the moment shown. */
  void update(double t);

  /** Everything of the lesson gone (the pools kept). */
  void clear();

 private:
  struct Held {
    std::shared_ptr<Group> group;
    std::shared_ptr<Group> turn;
  };
  struct Timed {
    ObjectPtr obj;
    double from;
    double to;
  };

  const std::shared_ptr<Group> place = make<Group>();
  std::vector<GeometryPtr> made;
  std::vector<std::shared_ptr<InstancedMesh>> madeInstanced;
  /** props shown only for a while of the script */
  std::vector<Timed> timed;
  struct Pool {
    std::map<AnimalKind, std::vector<std::unique_ptr<AnimalModel>>> animals;
    std::vector<std::shared_ptr<Group>> rollators;
    std::vector<Held> paddles;
  } pool;
  /** the animals the lesson shown uses (given back to the pool by `clear`) */
  std::vector<std::unique_ptr<AnimalModel>> usedAnimals;
  std::vector<std::function<void()>> used;
  /** what each actor (by its index) holds, and its headlights */
  std::map<int, Held> held;
  std::map<int, std::shared_ptr<Mesh>> beams;

  /** One small prop (local: −z the side it faces). */
  ObjectPtr prop(const theory::PropPlace& p);
  /** A tunnel over the road: see-through walls and roof, the portal at its south end, lamps along the walls. */
  ObjectPtr tunnel(const theory::PropPlace& p, const StageFrame& f, const std::function<std::array<double, 3>(double x, double z)>& groundAt);
};

}  // namespace fs

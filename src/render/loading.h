// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/loading.ts @ v1.7.7
/**
 * A truck being loaded at a depot: the scene a free drive with a truck of
 * class C1 or C begins with (game/serviceworld.h).
 *
 * The truck stands with its rear doors at the loading dock of a warehouse
 * (or, where there is no room for a depot, at the roadside: then the goods
 * come off a stack on the ground).  A forklift takes the pallets one by one
 * from the stack and sets them down at the door – on the dock, or lifted
 * onto the floor of the box –, a worker with a pallet truck pushes each into
 * the box, to the front first, row by row; a third stands by with the
 * papers.  The doors open at the start and close at the end; the camera
 * circles slowly at the side of the truck's rear.  Everything here is
 * scripted: the moves of each actor follow from the places of the pallets
 * (`schedule`), and `update` shows the moment of the script.
 *
 * The frame of the scene is the truck's (x to the right, y up from the
 * ground under the truck, z to the back); its group is placed where the
 * truck stands.  The people are those of render/people.h in work clothes.
 */
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "engine/engine.h"
#include "render/boxbody.h"
#include "render/people.h"

namespace fs {

/** A pallet to be loaded: how tall its goods stand, and what they are (0 cartons, 1 sacks, 2 drums, 3 crates). */
struct PalletLoad {
  double height = 0;
  int kind = 0;
  double mass = 0;
};

/** Euro pallet: 1.2 m along the truck, 0.8 m across, 0.144 m high. */
struct PalletSize {
  double len, wid, base;
};
inline constexpr PalletSize PALLET = {1.2, 0.8, 0.144};

/** The part of a box's inside the slots need (BoxBody::Inside fits). */
struct SlotInside {
  double x0, x1, front, rear;
};

/**
 * Where the pallets go in a box (truck frame: x across, z along): two side
 * by side, row after row from the front.  `inside`: the box's inside
 * (render/boxbody.h).
 */
std::vector<std::array<double, 2>> palletSlots(const SlotInside& inside, int n);

// ------------------------------------------------------------------ the script

/** A moment of an actor's way: where it is (truck frame), which way it faces (yaw), its forks' height, what it carries (−1 nothing). */
struct Key {
  double t = 0;
  double x = 0;
  double z = 0;
  double yaw = 0;
  double h = 0;
  int carry = -1;
};

/** What the script says about the whole scene. */
struct Script {
  std::vector<Key> forklift;
  std::vector<Key> jack;
  /** for each pallet: when the forklift takes it, when it puts it down at the door, when the pallet truck takes it, when it is in its place */
  struct Pallet {
    double pick, drop, take, placed;
  };
  std::vector<Pallet> pallets;
  /** where the pallets wait (truck frame), where they are set down, and their places in the box */
  std::vector<std::array<double, 3>> stack;
  std::array<double, 3> staging{};
  std::vector<std::array<double, 2>> slots;
  /** when the doors begin to close, and when it is all over */
  double closing = 0;
  double end = 0;
};

/** how far the dock's wall stands behind the truck's rear: room for its doors to swing round (a long leveller bridges it) */
inline constexpr double DOCK_GAP = 1.7;

/** The actor's state at time t (keys sorted by time). */
Key& at(const std::vector<Key>& keys, double t, Key& out);

/**
 * The script of the loading: `rear` the end of the truck's box (z), `floor`
 * its floor, `dock` whether a dock stands behind it (its wall at `wallZ`,
 * its floor at the height of the truck's) – else the stack is on the ground
 * beside the truck's rear and the forklift lifts each pallet onto the floor
 * of the box at its end.  The pallet truck takes each pallet from there: on
 * the dock from behind, pushing it into the box; on the ground from inside
 * the box, pulling it to the front.  The forklift sets the next pallet down
 * only once the pallet truck is out of its way.  The times are stretched or
 * squeezed so that it all lasts 30 to 60 seconds.
 */
Script schedule(int n, const std::vector<std::array<double, 2>>& slots, double rear, double floor, bool dock, double wallZ);

/** A pallet with its goods (its middle on the ground at the origin, the long side along z) – also the forklift's (render/forkliftyard.h). */
std::shared_ptr<Group> palletModel(const PalletLoad& p);

// ------------------------------------------------------------------ the scene

struct ScenePose {
  /** the truck: its model origin (the ground below the middle of its wheelbase) and heading */
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  /** a warehouse with a dock stands behind it (else the stack is on the ground) */
  bool dock = false;
};

class LoadingScene {
 public:
  const std::shared_ptr<Group> group = make<Group>();
  Script script;
  const ScenePose pose;
  const std::vector<PalletLoad> loads;
  /** seconds into the scene */
  double t = 0;
  /** where the dock's wall stands (truck frame), and the floor the forklift drives on */
  double wallZ = 0;

  LoadingScene(const ScenePose& pose, BoxBody& body, std::vector<PalletLoad> loads);
  LoadingScene(const LoadingScene&) = delete;
  LoadingScene& operator=(const LoadingScene&) = delete;

  struct Progress {
    int loaded;
    int total;
    double mass;
  };
  /** How many pallets are in the box at the moment, of how many, and their weight. */
  Progress progress() const;

  bool done() const { return t >= script.end; }

  /** Jump to the doors closing. */
  void skip();

  /** Show the moment `t` seconds further on. */
  void update(double dt);

  /**
   * The camera: from the side while the doors open and close; while the
   * goods go in, from inside the warehouse looking out through the dock door
   * into the box (at the roadside: from behind the truck).  It drifts a
   * little all the while.
   */
  void camera(Camera& cam, const std::function<double(double, double)>& ground) const;

  void dispose();

 private:
  BoxBody& body_;
  PeopleRenderer people_{4};
  std::vector<PersonPose> poses_;
  std::vector<std::shared_ptr<Group>> pallets_;
  struct Fork {
    std::shared_ptr<Group> group, carriage;
  } fork_;
  struct Jack {
    std::shared_ptr<Group> group, forks;
  } jack_;
  Key k_;
  Key k2_;
  double base_ = 0;
  std::array<double, 2> walked_{0, 0};
  std::array<std::array<double, 2>, 2> lastPos_{};

  /** The floor the pallet truck and its worker are on: the box's – and the dock's, which has its height. */
  double groundUnder(double z) const;
  /** How far a person has walked (for the gait), or −1 standing. */
  double walkOf(int i, double x, double z);
  void onForks(Group& m, const Key& f) const;
  void onJack(Group& m) const;
};

}  // namespace fs

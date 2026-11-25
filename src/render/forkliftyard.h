// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/forkliftyard.ts @ v1.7.7
/**
 * The forklift's place on the practice yard, drawn (game/forklifttasks.h
 * says what is where): the pallet rack – blue uprights with their bracing,
 * orange beams –, the flatbed truck with its side open, the markings on the
 * floor (the aisle, the places the pallets wait in, the green place of the
 * guided start), the pallets (render/loading.h draws them, as for the
 * truck being loaded at a depot) and the crate of 3.6 t with its weight
 * written on it, and green arrows over what is to be taken now.
 *
 * The fixed parts are built in the yard's frame (u along it, w across,
 * metres above the floor) – a group placed and turned as the yard is –,
 * the pallets in the world, each frame where the simulation has them.
 */
#pragma once

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/engine.h"

namespace fs {

class YardFrame;
struct OBox;
struct Pallet;

/** The pallet on the forks leans with the mast: the forklift's yaw and the mast's tilt. */
struct ForkLean {
  double yaw;
  double tilt;
};

/** What is marked now: arrows over these pallets, the green place, the truck's floor, the slot up in the rack. */
struct ForkYardTargets {
  std::vector<std::string> pallets;
  const OBox* zone = nullptr;
  bool bed = false;
  bool slot = false;
};

class ForkYardScene {
 public:
  const std::shared_ptr<Group> group = make<Group>();

  ForkYardScene();

  /** Lay the place out (once for a yard) on the floor `y0`; `low`: the low graphics preset. */
  void show(const YardFrame& frame, double y0, bool low);
  void hide();
  /**
   * The pallets where they are (the one on the forks leaning with the
   * mast: `fork` its yaw and tilt), the arrows over those to take, the
   * marks of where they go.
   */
  void update(const std::deque<Pallet>& pallets, const std::optional<ForkLean>& fork, const ForkYardTargets& targets, double now);

 private:
  /** the fixed parts, in the yard's frame */
  const std::shared_ptr<Group> fixed = make<Group>();
  const std::shared_ptr<Group> palletGroup = make<Group>();
  std::unordered_map<std::string, std::shared_ptr<Group>> pallets_;
  std::vector<std::shared_ptr<Mesh>> arrows;
  const std::shared_ptr<Group> zoneMark = make<Group>();
  const std::shared_ptr<Group> bedMark = make<Group>();
  const std::shared_ptr<Group> slotMark = make<Group>();
  std::string builtFor;
  Quaternion q;
  Quaternion q2;
  Euler e;

  void clearFixed();
  void build(bool low);
  /** A pallet's drawing (made the first time it is seen). */
  Group& palletOf(const Pallet& p);
};

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/forklifttasks.ts @ v1.7.7
/**
 * The forklift's place on the practice yard and what is done there
 * (game/forkliftplay.h runs it, render/forkliftyard.h draws it):
 *
 *  - the place, in the yard's own frame (u along it, w across, metres): a
 *    pallet rack of three bays with beams at 1.6 and 3.1 m, a flatbed truck
 *    standing with its side open to be loaded from the ground, pallets
 *    waiting on the ground, a crate of 3.6 t (more than the forklift may
 *    lift), a green place to set a pallet down on;
 *  - a short guided start with one pallet – forks in, lift it just clear,
 *    tilt the mast back, drive with the load low, set it down and back out;
 *  - then two tasks: load three pallets onto the truck, and put a pallet on
 *    the top level of the rack – each judged by its time and the faults
 *    the instructor noted meanwhile, with a score and up to three stars.
 */
#pragma once

#include <array>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "game/ce.h"
#include "i18n/index.h"
#include "sim/forklift.h"

namespace fs {

/** Where the forklift starts: facing north (−w), the first pallet ahead of it. */
struct ForkStart {
  double u;
  double w;
  double yaw;
};
inline constexpr ForkStart FORK_START = {-28, 4, 0};

/** The rack: the first upright's u, the bays' width and number, its front and back (w), the beams' tops, the uprights' height. */
struct RackPlan {
  double u0 = -14;
  double bay = 2.3;
  int bays = 3;
  double wFront = -10.0;
  double wBack = -11.1;
  std::array<double, 2> levels = {1.6, 3.1};
  double height = 3.9;
  double beam = 0.12;
  double upright = 0.1;
};
inline constexpr RackPlan RACK{};

/** The flatbed truck: its floor (and its height), the chassis under it, its cab, its headboard, its wheels. */
struct TruckPlan {
  struct Bed {
    double u0, u1, w0, w1, y;
  };
  struct Part {
    double u0, u1, w0, w1, y0, y1;
  };
  Bed bed = {-4, 3.5, 4.0, 6.45, 1.25};
  Part chassis = {-4, 3.5, 4.4, 6.05, 0.25, 1.1};
  Part cab = {3.6, 5.9, 3.95, 6.5, 0.4, 3.0};
  Part head = {3.42, 3.55, 4.0, 6.45, 1.25, 2.6};
  std::array<double, 3> wheels = {-2.6, -1.25, 4.6};
  double wheelR = 0.5;
};
/** (TRUCK in the original: fs::TRUCK is the lorry's spec) */
inline constexpr TruckPlan FORK_TRUCK{};

/** The green place of the guided start (its middle and half its sides). */
struct GuideZone {
  double u, w, hu, hw;
};
inline constexpr GuideZone GUIDE_ZONE = {-22, 6, 0.9, 1.1};

/** The paved floor of the place, for the drawing. */
struct ForkArea {
  double u0, u1, w0, w1;
};
inline constexpr ForkArea FORK_AREA = {-34, 9, -14, 11};

struct SlotPlace {
  double u;
  double w;
};
/** The middle of a slot of the rack: bay, left (0) or right (1). */
SlotPlace slotOf(int bay, int slot);

/** The place in the world: what stands in the forklift's way, what pallets can be set down on, the pallets, the green place, the truck's floor. */
struct ForkSite {
  double y0 = 0;
  std::vector<OBox> obstacles;
  std::vector<Shelf> shelves;
  /** (owned here: the forklift and the tasks point to them) */
  std::deque<Pallet> pallets;
  OBox zone;
  Shelf bed;
  /** the slot of the top level the pallet is to go into */
  OBox topSlot;

  /** the pallets as the forklift's world takes them */
  std::vector<Pallet*> palletPtrs();
};

/**
 * The place laid out in the world from the yard's frame; `y0`: the floor's
 * height (the place is flat), `ground`: the map's ground, for the pallets.
 */
ForkSite buildSite(const YardFrame& frame, double y0, const std::function<double(double, double)>& ground);

/** Is the whole of a pallet's footprint within a box (with a margin)? */
bool footprintIn(const Pallet& p, const OBox& b, double margin);

// ---------------------------------------------------------------------------- the tasks

/** What the tasks see of the forklift now. */
struct ForkSnapshot {
  double time = 0;
  ForkFrame forks;
  double lift = 0;
  double tilt = 0;
  double speed = 0;
  double ground = 0;
  const Pallet* load = nullptr;
  std::vector<const Pallet*> pallets;
  /** the load moment now (1: the rear wheels lift) */
  double moment = 0;
  bool tipped = false;
};

struct ForkResult {
  /** the task's name (a text key) */
  std::string text;
  double time = 0;
  double score = 0;
  int stars = 0;
  int minor = 0;
  int major = 0;
  /** the highest load moment (of the tipping limit) */
  double peak = 0;
};

struct ForkTargets {
  std::vector<std::string> pallets;
  const OBox* zone = nullptr;
  bool bed = false;
};

struct ForkUpdate {
  bool stepDone = false;
  bool taskDone = false;
  bool allDone = false;
};

struct ForkInstruction {
  std::string key;
  TrParams data;
  std::string hint;
};

/** The guided start and the tasks, judged as they go. */
class ForkTasks {
 public:
  ForkSite& site;
  /** "guide", "task" or "done" */
  std::string stage = "guide";
  /** the step of the guided start */
  int step = 0;
  /** the task (of TASKS) */
  int task = 0;
  std::vector<ForkResult> results;

  explicit ForkTasks(ForkSite& site) : site(site) {}

  int taskCount() const;

  /** A fault the instructor noted: it counts against the task going on. */
  void fault(const std::string& severity);
  /** The pallets on the truck's floor, wholly on it. */
  int onTruck() const;
  /** The pallets to be highlighted now, and the place they go to. */
  ForkTargets targets() const;
  /** What happened: a step of the guide done, a task done (its result is the last), all done. */
  ForkUpdate update(const ForkSnapshot& s);
  /** What to do now (text keys and their values) and a hint. */
  ForkInstruction instruction(const ForkSnapshot& s) const;

 private:
  double began_ = 0;
  int minor_ = 0;
  int major_ = 0;
  double peak_ = 0;
  double guideBegan_ = 0;
  bool started_ = false;

  const Pallet& pallet(const std::string& id) const;
  /** The forks are out of every pallet and the forklift stands clear. */
  bool forksOut(const ForkSnapshot& s) const;
  void beginTask(double now);
  /** A task's score: 100, less for each fault, less for a long time beyond its par. */
  ForkResult judge(const std::string& text, double time, double par) const;
};

}  // namespace fs

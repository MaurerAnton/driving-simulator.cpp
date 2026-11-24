// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Traffic-light controllers: fixed-time programmes with the German sequence
 * green → amber → red → red+amber → green.
 *
 * Origin: src/sim/signals.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#pragma once

#include <vector>

#include "world/types.h"

namespace fs {

enum Sig : int { SIG_RED = 0, SIG_RED_AMBER = 1, SIG_GREEN = 2, SIG_AMBER = 3 };

class SignalSystem {
 public:
  explicit SignalSystem(const std::vector<SignalControllerJson>& controllers);

  void update(double dt);

  /**
   * Take up the controllers that were added to the list since (the road
   * network comes in pieces).  Each starts where its programme stands by the
   * clock, so that a junction shows the same lights whenever it is loaded.
   */
  void sync();

  /** Current state of a group (SIG_*). */
  int get(int controller, int group) const;
  /** Seconds the group has been showing its current state. */
  double age(int controller, int group) const;

  double time = 0;
  /** by their number in the map – with gaps where the map is not loaded */
  const std::vector<SignalControllerJson>& controllers;

  /**
   * The controllers that are really there are taken up (a piece of the
   * network arrives): each starts where its programme stands by the clock.
   * `present` says for each number whether its controller has arrived.
   */
  void sync(const std::vector<char>& present);

 private:
  /** the numbers of the controllers that are running */
  std::vector<int> ids_;
  std::vector<double> cycle_;
  std::vector<std::vector<int>> state_;
  std::vector<std::vector<double>> since_;
};

}  // namespace fs

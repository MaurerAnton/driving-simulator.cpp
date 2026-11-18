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

  /** Current state of a group (SIG_*). */
  int get(int controller, int group) const;
  /** Seconds the group has been showing its current state. */
  double age(int controller, int group) const;

  double time = 0;
  const std::vector<SignalControllerJson>& controllers;

 private:
  std::vector<double> cycle_;
  std::vector<std::vector<int>> state_;
  std::vector<std::vector<double>> since_;
};

}  // namespace fs

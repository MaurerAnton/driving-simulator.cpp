// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The path the car has driven, for the map: a line of points a few metres
 * apart, kept so that it is still there in the next session.  A jump
 * (teleport, new start) breaks the line.
 *
 * Origin: src/game/track.ts (MaurerAnton/driving-simulator @ ff37c5d, v0.7.0).
 */
#pragma once

#include <string>
#include <vector>

#include "game/faultlog.h"

namespace fs {

/** a new point when the car has moved this far (m) */
constexpr double PATH_STEP = 8;
/** the oldest points make room: 12 000 points are about 100 km */
constexpr size_t PATH_MAX_POINTS = 12000;

class DrivenPath {
 public:
  /** null: the path lives as long as the page (a browser without storage) */
  explicit DrivenPath(KeyValueStore* store = nullptr);

  /** x, z, x, z …; NaN ends a stretch (the original: null in storage) */
  std::vector<double> points;

  /** The car is at (x, z) now. */
  void add(double x, double z);
  /** The next point does not continue the line (teleport, new drive). */
  void breakLine();
  /** Length of the path in metres. */
  double length() const;
  void clear();
  void save();

 private:
  KeyValueStore* store_;
  int dirty_ = 0;
};

}  // namespace fs

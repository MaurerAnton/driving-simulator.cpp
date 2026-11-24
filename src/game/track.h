// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/track.ts @ v1.7.7
/**
 * The path the car has driven, for the map: a line of points a few metres
 * apart, kept in the browser so that it is still there in the next session.
 * A jump (teleport, new start) breaks the line.
 */
#pragma once

#include <vector>

#include "game/faultlog.h"

namespace fs {

/** the oldest points make room: 12 000 points are about 100 km */
inline constexpr size_t PATH_MAX_POINTS = 12000;

class DrivenPath {
 public:
  /** x, z, x, z …; a pair of NaN ends a stretch */
  std::vector<double> points;

  /** (`store`: where it is kept – null: nowhere, the path lives as long as the page) */
  explicit DrivenPath(KeyValueStore* store = &plat::localStorage());

  /** The car is at (x, z) now. */
  void add(double x, double z);

  /** The next point does not continue the line (teleport, new drive). */
  void breakLine();

  /** Length of the path in metres. */
  double length() const;

  void clear();

  void save();

 private:
  KeyValueStore* store;
  int dirty = 0;
};

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The road network of the whole map (netfile.h) made searchable by place:
 * which roads come into a rectangle – for drawing the map of a region whose
 * roads are mostly not loaded –, and which road is nearest to a point – for
 * putting the car somewhere far away, or driving there.
 *
 * Origin: src/world/netindex.ts (MaurerAnton/driving-simulator @ 3420ad0,
 * v0.8.0).
 */
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "world/netfile.h"
#include "world/routegraph.h"

namespace fs {

/** Where on the network a point lies: the road, and the yaw there. */
struct NetSpot {
  int edge = -1;
  /** the point on the road, and the direction the road runs there from its junction a to b (yaw: 0 = north, positive = to the left) */
  double x = 0, z = 0, yaw = 0;
  /** how far that is from what was asked for */
  double d = 0;
};

class NetIndex {
 public:
  NetIndex(const NetData& net);

  NetData net;
  RouteGraph graph;
  /** the ways one can drive around on (routegraph.h: mainWays) */
  std::vector<uint8_t> main;

  /** Visit the roads that come near the rectangle (each once); `big`: only tertiary roads and above. */
  void edgesInBox(double minX, double minZ, double maxX, double maxZ,
                  const std::function<void(int)>& visit, bool big = false) const;

  /** Draw the course of a road into a path (the callback gets moveTo/lineTo pairs). */
  void trace(int e, const std::function<void(double, double, bool)>& path) const;

  /**
   * The point on a road nearest to (x, z), not further than `maxDist` –
   * among the roads `usable` allows (null when there is none).
   */
  bool nearest(double x, double z, double maxDist, const std::function<bool(int)>& usable,
               NetSpot& out) const;

 private:
  std::map<long long, std::vector<int>> cells_;
  /** the same for the bigger roads only (tertiary and above): what a map of the whole region shows */
  std::map<long long, std::vector<int>> bigCells_;
};

}  // namespace fs

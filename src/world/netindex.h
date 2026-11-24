// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/netindex.ts @ v1.7.7
/**
 * The road network of the whole map (netfile.h) made searchable by place:
 * which roads come into a rectangle – for drawing the map of a region whose
 * roads are mostly not loaded –, and which road is nearest to a point – for
 * putting the car somewhere far away, or driving there.
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/task.h"
#include "world/netfile.h"
#include "world/routegraph.h"

namespace fs {

struct NetSpot {
  int edge = 0;
  /** the point on the road, and the direction the road runs there from its junction a to b (yaw: 0 = north, positive = to the left) */
  double x = 0;
  double z = 0;
  double yaw = 0;
  /** how far that is from what was asked for */
  double d = 0;
};

class NetIndex {
 public:
  /** (the network is shared with whoever else looks at it: it is tens of megabytes) */
  std::shared_ptr<const NetData> net;
  RouteGraph graph;
  /** the ways one can drive around on (routegraph.h: mainWays) */
  std::vector<uint8_t> main;

  /**
   * Make the index in steps, with a breath for the page between them (for
   * the game, where a region's network would otherwise hold a frame up for
   * seconds).
   */
  static Task<std::shared_ptr<NetIndex>> build(std::shared_ptr<const NetData> net);

  /** (`graph`, `main` left out: worked out from the network – routeGraphOfNet(net), mainWays(graph)) */
  explicit NetIndex(std::shared_ptr<const NetData> net);
  NetIndex(std::shared_ptr<const NetData> net, RouteGraph graph);
  NetIndex(std::shared_ptr<const NetData> net, RouteGraph graph, std::vector<uint8_t> main);

  /** Visit the roads that come near the rectangle (each once): visit(int e); `big`: only tertiary roads and above. */
  template <class Visit>
  void edgesInBox(double minX, double minZ, double maxX, double maxZ, Visit&& visit, bool big = false) const {
    const Cells& cells = big ? bigCells_ : cells_;
    const double x0 = std::floor(minX / CELL);
    const double x1 = std::floor(maxX / CELL);
    const double z0 = std::floor(minZ / CELL);
    const double z1 = std::floor(maxZ / CELL);
    const bool single = x0 == x1 && z0 == z1;
    std::unordered_set<int> seen;
    for (double ix = x0; ix <= x1; ix++) {
      for (double iz = z0; iz <= z1; iz++) {
        auto list = cells.find(static_cast<int64_t>((ix + 4096) * 8192 + iz + 4096));
        if (list == cells.end()) continue;
        for (size_t i = 0; i < list->second.size(); i++) {
          const int e = list->second[i];
          if (!single) {
            if (!seen.insert(e).second) continue;
          }
          visit(e);
        }
      }
    }
  }

  /** Draw the course of a road into a path (anything with moveTo and lineTo). */
  template <class Path>
  void trace(int e_, Path& path) const {
    const NetData& net_ = *net;
    const size_t e = static_cast<size_t>(e_);
    double x = net_.nodeX[net_.edgeA[e]];
    double z = net_.nodeZ[net_.edgeA[e]];
    path.moveTo(x, z);
    for (uint32_t k = net_.polyFirst[e]; k < net_.polyFirst[e + 1]; k++) {
      x += net_.poly[static_cast<size_t>(k) * 2] * POLY_UNIT;
      z += net_.poly[static_cast<size_t>(k) * 2 + 1] * POLY_UNIT;
      path.lineTo(x, z);
    }
    path.lineTo(net_.nodeX[net_.edgeB[e]], net_.nodeZ[net_.edgeB[e]]);
  }

  /** The point on a road nearest to (x, z), not further than `maxDist` – among the roads `usable` allows (none given: all). */
  std::optional<NetSpot> nearest(double x, double z, double maxDist, const std::function<bool(int e)>& usable = nullptr) const;

 private:
  static constexpr double CELL = 1000;
  using Cells = std::unordered_map<int64_t, std::vector<int>>;
  Cells cells_;
  /** the same for the bigger roads only (tertiary and above): what a map of the whole region shows */
  Cells bigCells_;

  void fill();
};

}  // namespace fs

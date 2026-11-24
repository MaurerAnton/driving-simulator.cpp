// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The road network of the whole map made searchable by place (see netindex.h).
 *
 * Origin: src/world/netindex.ts (MaurerAnton/driving-simulator @ 3420ad0,
 * v0.8.0).
 */
#include "world/netindex.h"

#include <cmath>
#include <set>

namespace fs {

namespace {
constexpr double CELL = 1000;
inline long long cellKey(long ix, long iz) { return (ix + 4096) * 8192 + iz + 4096; }
}  // namespace

NetIndex::NetIndex(const NetData& netIn) : net(netIn) {
  graph = routeGraphOfNet(net);
  main = mainWays(graph);
  const size_t E = net.edgeA.size();
  for (size_t e = 0; e < E; e++) {
    double x = net.nodeX[net.edgeA[e]];
    double z = net.nodeZ[net.edgeA[e]];
    double minX = std::min(x, net.nodeX[net.edgeB[e]]);
    double maxX = std::max(x, net.nodeX[net.edgeB[e]]);
    double minZ = std::min(z, net.nodeZ[net.edgeB[e]]);
    double maxZ = std::max(z, net.nodeZ[net.edgeB[e]]);
    for (uint32_t k = net.polyFirst[e]; k < net.polyFirst[e + 1]; k++) {
      x += net.poly[static_cast<size_t>(k) * 2] * POLY_UNIT;
      z += net.poly[static_cast<size_t>(k) * 2 + 1] * POLY_UNIT;
      if (x < minX) minX = x;
      if (x > maxX) maxX = x;
      if (z < minZ) minZ = z;
      if (z > maxZ) maxZ = z;
    }
    for (long ix = static_cast<long>(std::floor(minX / CELL));
         ix <= static_cast<long>(std::floor(maxX / CELL)); ix++)
      for (long iz = static_cast<long>(std::floor(minZ / CELL));
           iz <= static_cast<long>(std::floor(maxZ / CELL)); iz++) {
        const long long key = cellKey(ix, iz);
        cells_[key].push_back(static_cast<int>(e));
        if (net.edgeCls[e] <= 9) bigCells_[key].push_back(static_cast<int>(e));
      }
  }
}

void NetIndex::edgesInBox(double minX, double minZ, double maxX, double maxZ,
                          const std::function<void(int)>& visit, bool big) const {
  const std::map<long long, std::vector<int>>& cells = big ? bigCells_ : cells_;
  const long x0 = static_cast<long>(std::floor(minX / CELL));
  const long x1 = static_cast<long>(std::floor(maxX / CELL));
  const long z0 = static_cast<long>(std::floor(minZ / CELL));
  const long z1 = static_cast<long>(std::floor(maxZ / CELL));
  const bool single = x0 == x1 && z0 == z1;
  std::set<int> seen;
  for (long ix = x0; ix <= x1; ix++)
    for (long iz = z0; iz <= z1; iz++) {
      auto it = cells.find(cellKey(ix, iz));
      if (it == cells.end()) continue;
      for (int e : it->second) {
        if (!single && !seen.insert(e).second) continue;
        visit(e);
      }
    }
}

void NetIndex::trace(int e, const std::function<void(double, double, bool)>& path) const {
  double x = net.nodeX[net.edgeA[static_cast<size_t>(e)]];
  double z = net.nodeZ[net.edgeA[static_cast<size_t>(e)]];
  path(x, z, true);  // moveTo
  for (uint32_t k = net.polyFirst[static_cast<size_t>(e)];
       k < net.polyFirst[static_cast<size_t>(e) + 1]; k++) {
    x += net.poly[static_cast<size_t>(k) * 2] * POLY_UNIT;
    z += net.poly[static_cast<size_t>(k) * 2 + 1] * POLY_UNIT;
    path(x, z, false);  // lineTo
  }
  path(net.nodeX[net.edgeB[static_cast<size_t>(e)]], net.nodeZ[net.edgeB[static_cast<size_t>(e)]],
       false);
}

bool NetIndex::nearest(double x, double z, double maxDist,
                       const std::function<bool(int)>& usable, NetSpot& out) const {
  bool found = false;
  double bd = maxDist;
  edgesInBox(x - maxDist, z - maxDist, x + maxDist, z + maxDist, [&](int e) {
    if (usable && !usable(e)) return;
    double ax = net.nodeX[net.edgeA[static_cast<size_t>(e)]];
    double az = net.nodeZ[net.edgeA[static_cast<size_t>(e)]];
    const long n = static_cast<long>(net.polyFirst[static_cast<size_t>(e) + 1]) -
                   static_cast<long>(net.polyFirst[static_cast<size_t>(e)]);
    for (long i = 0; i <= n; i++) {
      double bx, bz;
      if (i < n) {
        const size_t k = net.polyFirst[static_cast<size_t>(e)] + static_cast<size_t>(i);
        bx = ax + net.poly[k * 2] * POLY_UNIT;
        bz = az + net.poly[k * 2 + 1] * POLY_UNIT;
      } else {
        bx = net.nodeX[net.edgeB[static_cast<size_t>(e)]];
        bz = net.nodeZ[net.edgeB[static_cast<size_t>(e)]];
      }
      const double dx = bx - ax;
      const double dz = bz - az;
      const double l2 = dx * dx + dz * dz;
      double t = l2 > 1e-9 ? ((x - ax) * dx + (z - az) * dz) / l2 : 0;
      t = t < 0 ? 0 : t > 1 ? 1 : t;
      const double px = ax + dx * t;
      const double pz = az + dz * t;
      const double d = std::hypot(x - px, z - pz);
      if (d < bd) {
        bd = d;
        out.edge = e;
        out.x = px;
        out.z = pz;
        out.yaw = std::atan2(-dx, -dz);
        out.d = d;
        found = true;
      }
      ax = bx;
      az = bz;
    }
  });
  return found;
}

}  // namespace fs

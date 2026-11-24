// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/netindex.ts @ v1.7.7
/** The road network of the whole map made searchable by place (see netindex.h). */
#include "world/netindex.h"

#include "core/geom.h"
#include "platform/plat.h"

namespace fs {

Task<std::shared_ptr<NetIndex>> NetIndex::build(std::shared_ptr<const NetData> net) {
  // pause: `new Promise((r) => setTimeout(r))`
  RouteGraph graph = routeGraphOfNet(*net);
  co_await plat::sleep(0);
  std::vector<uint8_t> main = mainWays(graph);
  co_await plat::sleep(0);
  co_return std::make_shared<NetIndex>(std::move(net), std::move(graph), std::move(main));
}

NetIndex::NetIndex(std::shared_ptr<const NetData> net_) : net(std::move(net_)), graph(routeGraphOfNet(*net)), main(mainWays(graph)) { fill(); }

NetIndex::NetIndex(std::shared_ptr<const NetData> net_, RouteGraph graph_) : net(std::move(net_)), graph(std::move(graph_)), main(mainWays(graph)) { fill(); }

NetIndex::NetIndex(std::shared_ptr<const NetData> net_, RouteGraph graph_, std::vector<uint8_t> main_) : net(std::move(net_)), graph(std::move(graph_)), main(std::move(main_)) { fill(); }

void NetIndex::fill() {
  const NetData& net = *this->net;
  const size_t E = net.edgeA.size();
  for (size_t e = 0; e < E; e++) {
    double x = net.nodeX[net.edgeA[e]];
    double z = net.nodeZ[net.edgeA[e]];
    double minX = jsMin(x, net.nodeX[net.edgeB[e]]);
    double maxX = jsMax(x, net.nodeX[net.edgeB[e]]);
    double minZ = jsMin(z, net.nodeZ[net.edgeB[e]]);
    double maxZ = jsMax(z, net.nodeZ[net.edgeB[e]]);
    for (uint32_t k = net.polyFirst[e]; k < net.polyFirst[e + 1]; k++) {
      x += net.poly[static_cast<size_t>(k) * 2] * POLY_UNIT;
      z += net.poly[static_cast<size_t>(k) * 2 + 1] * POLY_UNIT;
      if (x < minX) minX = x;
      if (x > maxX) maxX = x;
      if (z < minZ) minZ = z;
      if (z > maxZ) maxZ = z;
    }
    for (double ix = std::floor(minX / CELL); ix <= std::floor(maxX / CELL); ix++) {
      for (double iz = std::floor(minZ / CELL); iz <= std::floor(maxZ / CELL); iz++) {
        const int64_t key = static_cast<int64_t>((ix + 4096) * 8192 + iz + 4096);
        cells_[key].push_back(static_cast<int>(e));
        if (net.edgeCls[e] <= 9) bigCells_[key].push_back(static_cast<int>(e));
      }
    }
  }
}

std::optional<NetSpot> NetIndex::nearest(double x, double z, double maxDist, const std::function<bool(int e)>& usable) const {
  const NetData& net = *this->net;
  std::optional<NetSpot> best;
  double bd = maxDist;
  edgesInBox(x - maxDist, z - maxDist, x + maxDist, z + maxDist, [&](int e_) {
    if (usable && !usable(e_)) return;
    const size_t e = static_cast<size_t>(e_);
    double ax = net.nodeX[net.edgeA[e]];
    double az = net.nodeZ[net.edgeA[e]];
    const uint32_t n = net.polyFirst[e + 1] - net.polyFirst[e];
    for (uint32_t i = 0; i <= n; i++) {
      double bx;
      double bz;
      if (i < n) {
        const size_t k = static_cast<size_t>(net.polyFirst[e]) + i;
        bx = ax + net.poly[k * 2] * POLY_UNIT;
        bz = az + net.poly[k * 2 + 1] * POLY_UNIT;
      } else {
        bx = net.nodeX[net.edgeB[e]];
        bz = net.nodeZ[net.edgeB[e]];
      }
      const double dx = bx - ax;
      const double dz = bz - az;
      const double l2 = dx * dx + dz * dz;
      double t = l2 > 1e-9 ? ((x - ax) * dx + (z - az) * dz) / l2 : 0;
      t = t < 0 ? 0 : t > 1 ? 1 : t;
      const double px = ax + dx * t;
      const double pz = az + dz * t;
      const double d = jsHypot(x - px, z - pz);
      if (d < bd) {
        bd = d;
        NetSpot spot;
        spot.edge = e_;
        spot.x = px;
        spot.z = pz;
        spot.yaw = std::atan2(-dx, -dz);
        spot.d = d;
        best = spot;
      }
      ax = bx;
      az = bz;
    }
  });
  return best;
}

}  // namespace fs

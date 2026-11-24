// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/pedlights.ts @ v1.7.7
#include "world/pedlights.h"

#include <algorithm>
#include <cmath>

#include "core/geom.h"
#include "world/world.h"

namespace fs {

std::vector<PedPole> pedPoles(const World& world) {
  std::vector<PedPole> out;
  PolySample tmp = newPolySample();
  for (const CrossingJson* c : world.crossings) {
    if (c->k != XING_SIGNAL || !c->sc) continue;
    const Edge* e = world.edges[c->e];
    if (!e) continue;
    for (const int side : {1, -1}) {
      // right of the edge's direction (side −1) beyond the stripes, left of it before them
      const double s = std::max(0.2, std::min(e->len - 0.2, c->s - side * (c->w / 2 + 0.8)));
      samplePolyline(e->pl, s, tmp);
      const double sw = side > 0 ? e->swL : e->swR;
      const double out1 = e->width / 2 + (sw > 0 ? 0.55 : 0.9);
      // left of the edge direction is (tz, −tx)
      const double x = tmp.x + tmp.tz * out1 * side;
      const double z = tmp.z - tmp.tx * out1 * side;
      const double y = world.edgeY(*e, s, x, z) + (sw > 0 ? KERB_H : -0.1);
      // the head looks across the road, to the other kerb
      const double fx = -tmp.tz * side;
      const double fz = tmp.tx * side;
      // the box looks along the kerb back to the stripes
      const double bx = tmp.tx * side;
      const double bz = tmp.tz * side;
      PedPole p;
      p.crossing = c;
      p.controller = *c->sc;
      p.group = c->g.value_or(0);
      p.x = x;
      p.y = y;
      p.z = z;
      p.yaw = std::atan2(-fx, -fz);
      p.boxYaw = std::atan2(-bx, -bz);
      out.push_back(p);
    }
  }
  return out;
}

const PedPole* poleInReach(const std::vector<PedPole>& poles, double x, double z, double reach) {
  const PedPole* best = nullptr;
  double bd = reach;
  for (const PedPole& p : poles) {
    const double d = std::hypot(p.x - x, p.z - z);
    if (d < bd) {
      bd = d;
      best = &p;
    }
  }
  return best;
}

}  // namespace fs

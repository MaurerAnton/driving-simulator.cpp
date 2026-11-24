// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/ground.ts @ v1.7.7
#include "sim/ground.h"

#include <cmath>

#include "sim/jsmath.h"
#include "world/cover.h"
#include "world/types.h"

namespace fs {

WorldGround::WorldGround(World& world_, TerrainSource& terrain_) : world(world_), terrain(terrain_) {}

void WorldGround::sample(double x, double z, double yRef, GroundSample& out) {
  SurfaceHit& hit = this->hit;
  if (world.surfaceAt(x, z, yRef, hit)) {
    out.y = hit.y;
    if (hit.node) {
      const double gx = hit.node->gx;
      const double gz = hit.node->gz;
      const double l = jsHypot(gx, 1, gz);
      out.nx = -gx / l;
      out.ny = 1 / l;
      out.nz = -gz / l;
    } else if (hit.edge) {
      // slope of the centreline at this station
      const Polyline3& pl = hit.edge->pl;
      tmp = samplePolyline(pl, hit.s, tmp);
      const int i = tmp.seg;
      const double segLen = pl.cum[i + 1] - pl.cum[i];
      const double slope = segLen > 1e-6 ? (pl.pts[i * 3 + 4] - pl.pts[i * 3 + 1]) / segLen : 0;
      const double nx = -slope * tmp.tx;
      const double nz = -slope * tmp.tz;
      const double l = jsHypot(nx, 1, nz);
      out.nx = nx / l;
      out.ny = 1 / l;
      out.nz = nz / l;
    } else {
      out.nx = 0;
      out.ny = 1;
      out.nz = 0;
    }
    out.grip = even ? 1 : SURF_GRIP[hit.surf] * weatherGrip;
    out.rolling = even ? 1 : SURF_ROLLING[hit.surf];
    lastY = out.y;
    return;
  }
  TerrainSource& t = terrain;
  double y = t.heightAt(x, z);
  if (std::isnan(y)) {
    // tile not loaded (yet): pretend the last known ground continues
    out.y = lastY;
    out.nx = 0;
    out.ny = 1;
    out.nz = 0;
    out.grip = 0.5;
    out.rolling = 3;
    return;
  }
  const double e = 0.7;
  double hx = t.heightAt(x + e, z);
  double hz = t.heightAt(x, z + e);
  if (std::isnan(hx)) hx = y;
  if (std::isnan(hz)) hz = y;
  if (raise) {
    y += raise(x, z);
    hx += raise(x + e, z);
    hz += raise(x, z + e);
  }
  const double nx = -(hx - y) / e;
  const double nz = -(hz - y) / e;
  const double l = jsHypot(nx, 1, nz);
  out.y = y;
  // (flattened: the bumps and camber of the land are gone, as on a road – the height of the
  //  landscape is kept, so a hill is still a hill; water, above, is left as it is)
  if (evenTerrain) {
    out.nx = 0;
    out.ny = 1;
    out.nz = 0;
  } else {
    out.nx = nx / l;
    out.ny = 1 / l;
    out.nz = nz / l;
  }
  const int c = t.coverAt(x, z);
  if (c == COVER_ASPHALT || c == COVER_PAVED || (paved && paved(x, z))) {
    out.grip = 0.95 * weatherGrip;
    out.rolling = 1.1;
  } else if (c == COVER_WATER) {
    const double depth = waterDepth ? waterDepth(x, z) : 0;
    out.y = y - depth;
    out.grip = 0.3;
    out.rolling = 12 + 10 * depth;
    lastY = out.y;
    return;
  } else if (c == COVER_FARMLAND || c == COVER_BARE) {
    out.grip = 0.45 * weatherGrip;
    out.rolling = 5;
  } else {
    out.grip = 0.5 * weatherGrip;
    out.rolling = 3.5;
  }
  if (even) {
    out.grip = 1;
    out.rolling = 1;
  }
  lastY = y;
}

}  // namespace fs

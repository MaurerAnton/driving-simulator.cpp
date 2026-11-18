// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Ground under the wheels: analytic road surfaces first, terrain tiles
 * otherwise.
 *
 * Origin: src/sim/ground.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#include "sim/ground.h"

#include <cmath>

#include "world/cover.h"

namespace fs {

void WorldGround::sample(double x, double z, double yRef, GroundSample& out) {
  SurfaceHit& hit = this->hit;
  if (world_.surfaceAt(x, z, yRef, hit)) {
    out.y = hit.y;
    if (hit.node) {
      double gx = hit.node->gx;
      double gz = hit.node->gz;
      double l = std::hypot(gx, 1.0, gz);
      out.nx = -gx / l;
      out.ny = 1 / l;
      out.nz = -gz / l;
    } else if (hit.edge) {
      // slope of the centreline at this station
      const Polyline3& pl = hit.edge->pl;
      tmp_ = samplePolyline(pl, hit.s, tmp_);
      size_t i = static_cast<size_t>(tmp_.seg);
      double segLen = pl.cum[i + 1] - pl.cum[i];
      double slope = segLen > 1e-6 ? (pl.pts[i * 3 + 4] - pl.pts[i * 3 + 1]) / segLen : 0;
      double nx = -slope * tmp_.tx;
      double nz = -slope * tmp_.tz;
      double l = std::hypot(nx, 1.0, nz);
      out.nx = nx / l;
      out.ny = 1 / l;
      out.nz = nz / l;
    } else {
      out.nx = 0;
      out.ny = 1;
      out.nz = 0;
    }
    out.grip = SURF_GRIP[hit.surf] * weatherGrip;
    out.rolling = SURF_ROLLING[hit.surf];
    lastY_ = out.y;
    return;
  }
  double y = terrain_.heightAt(x, z);
  if (std::isnan(y)) {
    // tile not loaded (yet): pretend the last known ground continues
    out.y = lastY_;
    out.nx = 0;
    out.ny = 1;
    out.nz = 0;
    out.grip = 0.5;
    out.rolling = 3;
    return;
  }
  const double e = 0.7;
  double hx = terrain_.heightAt(x + e, z);
  double hz = terrain_.heightAt(x, z + e);
  if (std::isnan(hx)) hx = y;
  if (std::isnan(hz)) hz = y;
  double nx = -(hx - y) / e;
  double nz = -(hz - y) / e;
  double l = std::hypot(nx, 1.0, nz);
  out.y = y;
  out.nx = nx / l;
  out.ny = 1 / l;
  out.nz = nz / l;
  int c = terrain_.coverAt(x, z);
  if (c == COVER_ASPHALT || c == COVER_PAVED) {
    out.grip = 0.95 * weatherGrip;
    out.rolling = 1.1;
  } else if (c == COVER_WATER) {
    out.grip = 0.3;
    out.rolling = 12;
  } else if (c == COVER_FARMLAND || c == COVER_BARE) {
    out.grip = 0.45 * weatherGrip;
    out.rolling = 5;
  } else {
    out.grip = 0.5 * weatherGrip;
    out.rolling = 3.5;
  }
  lastY_ = y;
}

}  // namespace fs

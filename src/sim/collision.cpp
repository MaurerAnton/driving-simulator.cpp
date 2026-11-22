// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Collisions of the player's car with scenery.
 *
 * Origin: src/sim/collision.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).  The car is three circles along its axis, the scenery segments and
 * discs in a 16 m grid keyed by tile so removal is per tile.
 */
#include "sim/collision.h"

#include <cmath>

namespace fs {

int64_t SceneryCollider::key(int ix, int iz) {
  return (static_cast<int64_t>(ix) + 2048) * 4096 + (static_cast<int64_t>(iz) + 2048);
}

void SceneryCollider::insert(std::vector<Prim>& tile, const Prim& p) {
  double pad = p.kind == 1 ? p.r : 0;
  double minX = p.kind == 0 ? std::min(p.x1, p.x2) : p.x1;
  double maxX = p.kind == 0 ? std::max(p.x1, p.x2) : p.x1;
  double minZ = p.kind == 0 ? std::min(p.z1, p.z2) : p.z1;
  double maxZ = p.kind == 0 ? std::max(p.z1, p.z2) : p.z1;
  long x0 = static_cast<long>(std::floor((minX - pad) / CELL));
  long x1 = static_cast<long>(std::floor((maxX + pad) / CELL));
  long z0 = static_cast<long>(std::floor((minZ - pad) / CELL));
  long z1 = static_cast<long>(std::floor((maxZ + pad) / CELL));
  for (long ix = x0; ix <= x1; ix++)
    for (long iz = z0; iz <= z1; iz++) grid_[key(static_cast<int>(ix), static_cast<int>(iz))].push_back(p);
  tile.push_back(p);
}

void SceneryCollider::add(const CollisionSource& src) {
  std::vector<Prim> tile;
  if (src.rings && src.ringCounts) {
    int o = 0;
    for (int r = 0; r < src.ringCount; r++) {
      int n = src.ringCounts[r];
      for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        Prim p;
        p.kind = 0;
        p.x1 = src.rings[o + i * 2];
        p.z1 = src.rings[o + i * 2 + 1];
        p.x2 = src.rings[o + j * 2];
        p.z2 = src.rings[o + j * 2 + 1];
        p.yIsNaN = true;
        insert(tile, p);
      }
      o += n * 2;
    }
  }
  if (src.segments) {
    const double* s = src.segments;
    for (int i = 0; i < src.segmentCount; i++) {
      Prim p;
      p.kind = 0;
      p.x1 = s[i * 5];
      p.z1 = s[i * 5 + 1];
      p.x2 = s[i * 5 + 2];
      p.z2 = s[i * 5 + 3];
      p.y = s[i * 5 + 4];
      insert(tile, p);
    }
  }
  if (src.discs) {
    const double* d = src.discs;
    for (int i = 0; i < src.discCount; i++) {
      Prim p;
      p.kind = 1;
      p.x1 = d[i * 4];
      p.z1 = d[i * 4 + 2];
      p.y = d[i * 4 + 1];
      p.r = d[i * 4 + 3];
      insert(tile, p);
    }
  }
  byTile_[src.key] = std::move(tile);
}

void SceneryCollider::remove(const std::string& key) {
  auto it = byTile_.find(key);
  if (it == byTile_.end()) return;
  // cells hold copies: drop every primitive of this tile from each cell it
  // touched (a primitive is identified by its geometry)
  for (const Prim& p : it->second) {
    double pad = p.kind == 1 ? p.r : 0;
    double minX = p.kind == 0 ? std::min(p.x1, p.x2) : p.x1;
    double maxX = p.kind == 0 ? std::max(p.x1, p.x2) : p.x1;
    double minZ = p.kind == 0 ? std::min(p.z1, p.z2) : p.z1;
    double maxZ = p.kind == 0 ? std::max(p.z1, p.z2) : p.z1;
    long x0 = static_cast<long>(std::floor((minX - pad) / CELL));
    long x1 = static_cast<long>(std::floor((maxX + pad) / CELL));
    long z0 = static_cast<long>(std::floor((minZ - pad) / CELL));
    long z1 = static_cast<long>(std::floor((maxZ + pad) / CELL));
    for (long ix = x0; ix <= x1; ix++) {
      for (long iz = z0; iz <= z1; iz++) {
        auto cell = grid_.find(SceneryCollider::key(static_cast<int>(ix), static_cast<int>(iz)));
        if (cell == grid_.end()) continue;
        auto& list = cell->second;
        for (size_t i = list.size(); i-- > 0;) {
          const Prim& q = list[i];
          if (q.kind == p.kind && q.x1 == p.x1 && q.z1 == p.z1 && q.x2 == p.x2 && q.z2 == p.z2 &&
              q.r == p.r && q.yIsNaN == p.yIsNaN && (q.yIsNaN || q.y == p.y)) {
            list.erase(list.begin() + static_cast<long>(i));
            break;
          }
        }
        if (list.empty()) grid_.erase(cell);
      }
    }
  }
  byTile_.erase(it);
}

void SceneryCollider::setShape(const double* offsets, int count, double radius) {
  circleCount_ = std::min(4, std::max(1, count));
  for (int i = 0; i < circleCount_; i++) offsets_[i] = offsets[i];
  radius_ = radius;
}

double SceneryCollider::resolve(Vehicle& v) {
  double impact = 0;
  double R = radius_;
  for (int c = 0; c < circleCount_; c++) {
    // circle centre in the world, at centre-of-mass height
    // offsets are measured from the wheelbase centre in the model frame
    double world[3];
    v.toWorld(0, 0, offsets_[c], world);
    double cx = world[0], cy = world[1], cz = world[2];
    long ix0 = static_cast<long>(std::floor((cx - R) / CELL));
    long ix1 = static_cast<long>(std::floor((cx + R) / CELL));
    long iz0 = static_cast<long>(std::floor((cz - R) / CELL));
    long iz1 = static_cast<long>(std::floor((cz + R) / CELL));
    for (long ix = ix0; ix <= ix1; ix++) {
      for (long iz = iz0; iz <= iz1; iz++) {
        auto cell = grid_.find(key(static_cast<int>(ix), static_cast<int>(iz)));
        if (cell == grid_.end()) continue;
        for (const Prim& p : cell->second) {
          if (!p.yIsNaN && std::fabs(cy - 0.5 - p.y) > 2.6) continue;
          double px, pz, rad = R;
          if (p.kind == 0) {
            double dx = p.x2 - p.x1, dz = p.z2 - p.z1;
            double l2 = dx * dx + dz * dz;
            double t = l2 > 1e-9 ? ((cx - p.x1) * dx + (cz - p.z1) * dz) / l2 : 0;
            t = t < 0 ? 0 : (t > 1 ? 1 : t);
            px = p.x1 + dx * t;
            pz = p.z1 + dz * t;
          } else {
            px = p.x1;
            pz = p.z1;
            rad = R + p.r;
          }
          double nx = cx - px, nz = cz - pz;
          double d2 = nx * nx + nz * nz;
          if (d2 >= rad * rad || d2 < 1e-10) continue;
          double d = std::sqrt(d2);
          double ux = nx / d, uz = nz / d;
          double depth = rad - d;
          // velocity of the contact point (ground-plane part)
          double rx = px - v.pos[0], rz = pz - v.pos[2];
          double vx = v.vel[0] - v.angVel[1] * -rz;
          double vz = v.vel[2] - v.angVel[1] * rx;
          double vn = vx * ux + vz * uz;
          v.translate(ux * depth, 0, uz * depth);
          if (vn < 0) {
            impact = std::max(impact, -vn);
            // inelastic hit with a little bounce; applied at centre-of-mass
            // height so the car does not flip
            double m = v.spec.mass * 0.55;
            double j = -(1.15 * vn) * m;
            // scrape friction along the obstacle
            double tx = -uz, tz = ux;
            double vt = vx * tx + vz * tz;
            double jt = -std::max(-0.35 * j, std::min(0.35 * j, vt * m));
            v.applyImpulse(px, v.pos[1], pz, ux * j + tx * jt, 0, uz * j + tz * jt);
          }
        }
      }
    }
  }
  return impact;
}

}  // namespace fs

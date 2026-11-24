// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/collision.ts @ v1.7.7
#include "sim/collision.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "sim/jsmath.h"

namespace fs {

namespace {
const double CELL = 16;
inline int64_t cellOf(double v) { return static_cast<int64_t>(std::floor(v / CELL)); }
}  // namespace

void SceneryCollider::setShape(const std::vector<double>& offsets_, double radius_) {
  offsets = offsets_;
  radius = radius_;
}

int64_t SceneryCollider::key(int64_t ix, int64_t iz) { return (ix + 2048) * 4096 + (iz + 2048); }

const std::vector<SceneryCollider::Prim*>* SceneryCollider::cell(int64_t ix, int64_t iz) const {
  auto it = grid.find(key(ix, iz));
  return it == grid.end() ? nullptr : &it->second;
}

void SceneryCollider::insert(Tile& tile, const Prim& prim) {
  const double pad = prim.kind == 1 ? prim.r : 0;
  const double fx0 = std::floor((jsMin(prim.x1, prim.kind == 0 ? prim.x2 : prim.x1) - pad) / CELL);
  const double fx1 = std::floor((jsMax(prim.x1, prim.kind == 0 ? prim.x2 : prim.x1) + pad) / CELL);
  const double fz0 = std::floor((jsMin(prim.z1, prim.kind == 0 ? prim.z2 : prim.z1) - pad) / CELL);
  const double fz1 = std::floor((jsMax(prim.z1, prim.kind == 0 ? prim.z2 : prim.z1) + pad) / CELL);
  // (a primitive that is not a number lies in no cell, as in the original: its loops do not run)
  if (std::isnan(fx0) || std::isnan(fx1) || std::isnan(fz0) || std::isnan(fz1)) return;
  const int64_t x0 = static_cast<int64_t>(fx0);
  const int64_t x1 = static_cast<int64_t>(fx1);
  const int64_t z0 = static_cast<int64_t>(fz0);
  const int64_t z1 = static_cast<int64_t>(fz1);
  tile.prims.push_back(prim);
  Prim* p = &tile.prims.back();
  for (int64_t ix = x0; ix <= x1; ix++) {
    for (int64_t iz = z0; iz <= z1; iz++) {
      const int64_t k = key(ix, iz);
      grid[k].push_back(p);
      tile.cells.push_back({k, p});
    }
  }
}

void SceneryCollider::add(const CollisionSource& src) {
  // (the same key again: the original's map keeps the new list, the old tile's primitives stay in the grid)
  Tile fresh;
  auto old = byTile.find(src.key);
  if (old != byTile.end()) fresh.prims = std::move(old->second.prims);
  Tile& tile = byTile[src.key];
  tile = std::move(fresh);
  if (src.rings && src.ringCounts) {
    const std::vector<float>& rings = *src.rings;
    size_t o = 0;
    const std::vector<uint8_t>* skip = src.ringSkip;
    for (const uint16_t n : *src.ringCounts) {
      for (size_t i = 0; i < n; i++) {
        const size_t side = o / 2 + i;
        if (skip && (side >> 3) < skip->size() && ((*skip)[side >> 3] & (1 << (side & 7))) != 0) continue;
        const size_t j = (i + 1) % n;
        insert(tile, Prim{0, rings[o + i * 2], rings[o + i * 2 + 1], rings[o + j * 2], rings[o + j * 2 + 1], 0, JS_NAN});
      }
      o += static_cast<size_t>(n) * 2;
    }
  }
  if (src.segments) {
    const std::vector<float>& s = *src.segments;
    for (size_t i = 0; i + 4 < s.size(); i += 5) insert(tile, Prim{0, s[i], s[i + 1], s[i + 2], s[i + 3], 0, s[i + 4]});
  }
  for (size_t i = 0; i + 3 < src.discs.size(); i += 4) {
    insert(tile, Prim{1, src.discs[i], src.discs[i + 2], 0, 0, src.discs[i + 3], src.discs[i + 1]});
  }
}

void SceneryCollider::remove(const std::string& key_) {
  auto it = byTile.find(key_);
  if (it == byTile.end()) return;
  for (const auto& [k, p] : it->second.cells) {
    auto g = grid.find(k);
    if (g == grid.end()) continue;
    std::vector<Prim*>& list = g->second;
    auto at = std::find(list.begin(), list.end(), p);
    if (at != list.end()) {
      *at = list.back();
      list.pop_back();
    }
    if (list.empty()) grid.erase(g);
  }
  byTile.erase(it);
}

bool SceneryCollider::blocked(double x, double y, double z, double yaw, double margin) const {
  const double R = radius + margin;
  const double sin = jsSin(yaw);
  const double cos = jsCos(yaw);
  for (size_t c = 0; c < offsets.size(); c++) {
    // (the model looks along -z: forward = (-sin yaw, -cos yaw), and offsets count backwards)
    const double cx = x + sin * offsets[c];
    const double cz = z + cos * offsets[c];
    const int64_t ix0 = cellOf(cx - R);
    const int64_t ix1 = cellOf(cx + R);
    const int64_t iz0 = cellOf(cz - R);
    const int64_t iz1 = cellOf(cz + R);
    for (int64_t ix = ix0; ix <= ix1; ix++) {
      for (int64_t iz = iz0; iz <= iz1; iz++) {
        const std::vector<Prim*>* list = cell(ix, iz);
        if (!list) continue;
        for (size_t i = 0; i < list->size(); i++) {
          const Prim& p = *(*list)[i];
          if (!std::isnan(p.y) && std::abs(y - p.y) > 2.6) continue;
          double px = p.x1;
          double pz = p.z1;
          if (p.kind == 0) {
            const double dx = p.x2 - p.x1;
            const double dz = p.z2 - p.z1;
            const double l2 = dx * dx + dz * dz;
            double t = l2 > 1e-9 ? ((cx - p.x1) * dx + (cz - p.z1) * dz) / l2 : 0;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            px = p.x1 + dx * t;
            pz = p.z1 + dz * t;
          }
          const double rad = R + (p.kind == 1 ? p.r : 0);
          if ((cx - px) * (cx - px) + (cz - pz) * (cz - pz) < rad * rad) return true;
        }
      }
    }
  }
  return false;
}

bool SceneryCollider::circleBlocked(double x, double y, double z, double r) const {
  const int64_t ix0 = cellOf(x - r);
  const int64_t ix1 = cellOf(x + r);
  const int64_t iz0 = cellOf(z - r);
  const int64_t iz1 = cellOf(z + r);
  for (int64_t ix = ix0; ix <= ix1; ix++) {
    for (int64_t iz = iz0; iz <= iz1; iz++) {
      const std::vector<Prim*>* list = cell(ix, iz);
      if (!list) continue;
      for (const Prim* pp : *list) {
        const Prim& p = *pp;
        if (!std::isnan(p.y) && std::abs(y - p.y) > 2.6) continue;
        double px = p.x1;
        double pz = p.z1;
        if (p.kind == 0) {
          const double dx = p.x2 - p.x1;
          const double dz = p.z2 - p.z1;
          const double l2 = dx * dx + dz * dz;
          double t = l2 > 1e-9 ? ((x - p.x1) * dx + (z - p.z1) * dz) / l2 : 0;
          t = t < 0 ? 0 : t > 1 ? 1 : t;
          px = p.x1 + dx * t;
          pz = p.z1 + dz * t;
        }
        const double rad = r + (p.kind == 1 ? p.r : 0);
        if ((x - px) * (x - px) + (z - pz) * (z - pz) < rad * rad) return true;
      }
    }
  }
  return false;
}

double SceneryCollider::resolve(Vehicle& v) {
  double impact = 0;
  lastBuilding = 0;
  const auto& mo = v.modelOrigin;
  const double R = radius;
  for (size_t c = 0; c < offsets.size(); c++) {
    // circle centre in the world, at centre-of-mass height
    v.toWorld(mo[0], 0, mo[2] + offsets[c], tmp.data());
    const double cx = tmp[0];
    const double cy = tmp[1];
    const double cz = tmp[2];
    const int64_t ix0 = cellOf(cx - R);
    const int64_t ix1 = cellOf(cx + R);
    const int64_t iz0 = cellOf(cz - R);
    const int64_t iz1 = cellOf(cz + R);
    for (int64_t ix = ix0; ix <= ix1; ix++) {
      for (int64_t iz = iz0; iz <= iz1; iz++) {
        const std::vector<Prim*>* list = cell(ix, iz);
        if (!list) continue;
        for (size_t i = 0; i < list->size(); i++) {
          const Prim& p = *(*list)[i];
          if (!std::isnan(p.y) && std::abs(cy - 0.5 - p.y) > 2.6) continue;
          double px;
          double pz;
          double rad = R;
          if (p.kind == 0) {
            const double dx = p.x2 - p.x1;
            const double dz = p.z2 - p.z1;
            const double l2 = dx * dx + dz * dz;
            double t = l2 > 1e-9 ? ((cx - p.x1) * dx + (cz - p.z1) * dz) / l2 : 0;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            px = p.x1 + dx * t;
            pz = p.z1 + dz * t;
          } else {
            px = p.x1;
            pz = p.z1;
            rad = R + p.r;
          }
          const double nx = cx - px;
          const double nz = cz - pz;
          const double d2 = nx * nx + nz * nz;
          if (d2 >= rad * rad || d2 < 1e-10) continue;
          const double d = std::sqrt(d2);
          const double ux = nx / d;
          const double uz = nz / d;
          const double depth = rad - d;
          // velocity of the contact point (ground-plane part)
          const double rx = px - v.pos[0];
          const double rz = pz - v.pos[2];
          const double vx = v.vel[0] - v.angVel[1] * -rz;
          const double vz = v.vel[2] - v.angVel[1] * rx;
          const double vn = vx * ux + vz * uz;
          v.translate(ux * depth, 0, uz * depth);
          if (vn < 0) {
            impact = jsMax(impact, -vn);
            // (into the wall of a building – a footprint, not a fence or a post)
            if (p.kind == 0 && std::isnan(p.y)) lastBuilding = jsMax(lastBuilding, -vn);
            // inelastic hit with a little bounce; applied at centre-of-mass height so the car does not flip
            const double m = v.spec.mass * 0.55;
            const double j = -(1.15 * vn) * m;
            // scrape friction along the obstacle
            const double tx = -uz;
            const double tz = ux;
            const double vt = vx * tx + vz * tz;
            const double jt = -jsMax(-0.35 * j, jsMin(0.35 * j, vt * m));
            v.applyImpulse(px, v.pos[1], pz, ux * j + tx * jt, 0, uz * j + tz * jt);
          }
        }
      }
    }
  }
  return impact;
}

}  // namespace fs

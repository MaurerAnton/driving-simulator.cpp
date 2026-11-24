// SPDX-License-Identifier: AGPL-3.0-only
/**
 * What the faces of a building need for their facades: the four numbers of
 * every corner from the rule that face is stored by (see facade.h).
 *
 * Origin: src/world/facade.ts (MaurerAnton/driving-simulator @ 3420ad0, v0.8.0).
 */
#include "world/facade.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "world/tilefmt.h"

namespace fs {

namespace {
inline int16_t q16(double v) {
  double r = std::round(v / UV_SCALE);
  return static_cast<int16_t>(std::max(-32767.0, std::min(32767.0, r)));
}
}  // namespace

bool deriveFace(const std::vector<double>& pos, int first, int n, int rule,
                std::vector<int16_t>& out) {
  if (n < 3) return false;
  const size_t o = static_cast<size_t>(first) * 3;
  if (rule == FACE_ROOF_FLAT) {
    double uMin = std::numeric_limits<double>::infinity();
    double vMin = std::numeric_limits<double>::infinity();
    for (int i = 0; i < n; i++) {
      uMin = std::min(uMin, pos[o + static_cast<size_t>(i) * 3]);
      vMin = std::min(vMin, pos[o + static_cast<size_t>(i) * 3 + 2]);
    }
    for (int i = 0; i < n; i++) {
      const size_t k = static_cast<size_t>(first + i) * 4;
      out[k] = q16((pos[o + static_cast<size_t>(i) * 3] - uMin) * POS_XZ_SCALE);
      out[k + 1] = q16((pos[o + static_cast<size_t>(i) * 3 + 2] - vMin) * POS_XZ_SCALE);
      out[k + 2] = 0;
      out[k + 3] = 0;
    }
    return true;
  }
  // which way the face looks (Newell): the corners in metres, around the first one
  const double x0 = pos[o];
  const double y0 = pos[o + 1];
  const double z0 = pos[o + 2];
  double nx = 0, ny = 0, nz = 0;
  for (int i = 0; i < n; i++) {
    const size_t a = o + static_cast<size_t>(i) * 3;
    const size_t b = o + static_cast<size_t>((i + 1) % n) * 3;
    const double ax = (pos[a] - x0) * POS_XZ_SCALE;
    const double ay = (pos[a + 1] - y0) * POS_Y_SCALE;
    const double az = (pos[a + 2] - z0) * POS_XZ_SCALE;
    const double bx = (pos[b] - x0) * POS_XZ_SCALE;
    const double by = (pos[b + 1] - y0) * POS_Y_SCALE;
    const double bz = (pos[b + 2] - z0) * POS_XZ_SCALE;
    nx += (ay - by) * (az + bz);
    ny += (az - bz) * (ax + bx);
    nz += (ax - bx) * (ay + by);
  }
  const double hl = std::sqrt(nx * nx + nz * nz);
  if (!(hl > 1e-9)) return false;
  // level, along the face
  const double ux = nz / hl;
  const double uz = -nx / hl;
  double uMin = std::numeric_limits<double>::infinity();
  double uMax = -std::numeric_limits<double>::infinity();
  for (int i = 0; i < n; i++) {
    const double u = (pos[o + static_cast<size_t>(i) * 3] - x0) * POS_XZ_SCALE * ux +
                     (pos[o + static_cast<size_t>(i) * 3 + 2] - z0) * POS_XZ_SCALE * uz;
    uMin = std::min(uMin, u);
    uMax = std::max(uMax, u);
  }
  if (rule == FACE_ROOF_SLOPED) {
    const double nl = std::sqrt(nx * nx + ny * ny + nz * nz);
    const double mx = nx / nl;
    const double my = ny / nl;
    const double mz = nz / nl;
    // up the slope: across the normal and the level direction
    double vx = my * uz;
    double vy = mz * ux - mx * uz;
    double vz = -my * ux;
    if (vy < 0) {
      vx = -vx;
      vy = -vy;
      vz = -vz;
    }
    double vMin = std::numeric_limits<double>::infinity();
    for (int i = 0; i < n; i++) {
      const double v = (pos[o + static_cast<size_t>(i) * 3] - x0) * POS_XZ_SCALE * vx +
                       (pos[o + static_cast<size_t>(i) * 3 + 1] - y0) * POS_Y_SCALE * vy +
                       (pos[o + static_cast<size_t>(i) * 3 + 2] - z0) * POS_XZ_SCALE * vz;
      vMin = std::min(vMin, v);
    }
    for (int i = 0; i < n; i++) {
      const size_t k = static_cast<size_t>(first + i) * 4;
      const double x = (pos[o + static_cast<size_t>(i) * 3] - x0) * POS_XZ_SCALE;
      const double y = (pos[o + static_cast<size_t>(i) * 3 + 1] - y0) * POS_Y_SCALE;
      const double z = (pos[o + static_cast<size_t>(i) * 3 + 2] - z0) * POS_XZ_SCALE;
      out[k] = q16(x * ux + z * uz - uMin);
      out[k + 1] = q16(x * vx + y * vy + z * vz - vMin);
      out[k + 2] = 0;
      out[k + 3] = 0;
    }
    return true;
  }
  if (rule != FACE_WALL && rule != FACE_WALL_SUNK) return false;
  // a wall: heights above its foot (where the foot was before it was sunk)
  double low = std::numeric_limits<double>::infinity();
  for (int i = 0; i < n; i++) low = std::min(low, pos[o + static_cast<size_t>(i) * 3 + 1]);
  const bool sunk = rule == FACE_WALL_SUNK;
  const double lo = low * POS_Y_SCALE + (sunk ? SINK : 0);
  auto height = [&](int i) {
    const double y = pos[o + static_cast<size_t>(i) * 3 + 1] * POS_Y_SCALE;
    return sunk && y < low * POS_Y_SCALE + SINK / 2 ? y + SINK : y;
  };
  double hi = -std::numeric_limits<double>::infinity();
  for (int i = 0; i < n; i++) hi = std::max(hi, height(i));
  double eave = hi;
  for (int i = 0; i < n; i++) {
    const double y = height(i);
    if (y > lo + EAVE_ABOVE && y < eave) eave = y;
  }
  const int L = q16(uMax - uMin);
  const int H = q16(eave - lo);
  for (int i = 0; i < n; i++) {
    const size_t k = static_cast<size_t>(first + i) * 4;
    const double x = (pos[o + static_cast<size_t>(i) * 3] - x0) * POS_XZ_SCALE;
    const double z = (pos[o + static_cast<size_t>(i) * 3 + 2] - z0) * POS_XZ_SCALE;
    out[k] = q16(x * ux + z * uz - uMin);
    out[k + 1] = q16(height(i) - lo);
    out[k + 2] = static_cast<int16_t>(L);
    out[k + 3] = static_cast<int16_t>(H);
  }
  return true;
}

namespace {
/** Which corners belong together (a face is a run of them). */
std::vector<int> faceParents(size_t count, const std::vector<double>& idx) {
  std::vector<int> parent(count);
  for (size_t i = 0; i < count; i++) parent[i] = static_cast<int>(i);
  auto find = [&](int i) {
    while (parent[static_cast<size_t>(i)] != i) {
      parent[static_cast<size_t>(i)] = parent[static_cast<size_t>(parent[static_cast<size_t>(i)])];
      i = parent[static_cast<size_t>(i)];
    }
    return i;
  };
  for (size_t t = 0; t + 2 < idx.size(); t += 3) {
    int a = find(static_cast<int>(idx[t]));
    int b = find(static_cast<int>(idx[t + 1]));
    int c = find(static_cast<int>(idx[t + 2]));
    int m = std::min(a, std::min(b, c));
    parent[static_cast<size_t>(a)] = m;
    parent[static_cast<size_t>(b)] = m;
    parent[static_cast<size_t>(c)] = m;
  }
  for (size_t i = 0; i < count; i++) parent[i] = find(static_cast<int>(i));
  return parent;
}
}  // namespace

PackedFaces packFaces(const std::vector<double>& pos, const std::vector<double>& idx,
                      const std::vector<int16_t>& a16) {
  const size_t count = pos.size() / 3;
  std::vector<int> parent = faceParents(count, idx);
  PackedFaces out;
  std::vector<int16_t> tmp(a16.size(), 0);
  int ruled = 0;
  auto explicitRun = [&](size_t first, size_t n) {
    for (size_t i = first * 4; i < (first + n) * 4; i++) out.uvx.push_back(a16[i]);
    // (runs without a rule that follow each other are one run)
    const long last = static_cast<long>(out.run.size()) - 1;
    if (last >= 0 && (out.run[static_cast<size_t>(last)] & 7) == FACE_EXPLICIT &&
        (out.run[static_cast<size_t>(last)] >> 3) + static_cast<int>(n) <= MAX_RUN)
      out.run[static_cast<size_t>(last)] =
          static_cast<uint16_t>(out.run[static_cast<size_t>(last)] + (n << 3));
    else
      out.run.push_back(static_cast<uint16_t>((n << 3) | FACE_EXPLICIT));
  };
  auto fits = [&](size_t first, size_t n) {
    for (size_t i = first * 4; i < (first + n) * 4; i++)
      if (std::abs(static_cast<int>(tmp[i]) - static_cast<int>(a16[i])) > TOLERANCE) return false;
    return true;
  };
  size_t first = 0;
  while (first < count) {
    int group = parent[first];
    size_t n = 1;
    while (first + n < count && n < static_cast<size_t>(MAX_RUN) &&
           parent[first + n] == group)
      n++;
    int rule = FACE_EXPLICIT;
    for (int r : {FACE_WALL_SUNK, FACE_WALL, FACE_ROOF_SLOPED, FACE_ROOF_FLAT}) {
      if (deriveFace(pos, static_cast<int>(first), static_cast<int>(n), r, tmp) && fits(first, n)) {
        rule = r;
        break;
      }
    }
    if (rule == FACE_EXPLICIT) explicitRun(first, n);
    else {
      out.run.push_back(static_cast<uint16_t>((n << 3) | rule));
      ruled += static_cast<int>(n);
    }
    first += n;
  }
  out.ruled = ruled;
  return out;
}

std::vector<int16_t> unpackFaces(const std::vector<double>& pos, const std::vector<uint16_t>& run,
                                 const std::vector<int16_t>* uvx) {
  const size_t count = pos.size() / 3;
  std::vector<int16_t> out(count * 4, 0);
  size_t first = 0;
  size_t x = 0;
  for (uint16_t r : run) {
    const size_t n = static_cast<size_t>(r >> 3);
    const int rule = r & 7;
    if (rule == FACE_EXPLICIT) {
      if (uvx)
        for (size_t i = first * 4; i < (first + n) * 4 && x < uvx->size(); i++)
          out[i] = (*uvx)[x++];
    } else
      deriveFace(pos, static_cast<int>(first), static_cast<int>(n), rule, out);
    first += n;
  }
  return out;
}

}  // namespace fs

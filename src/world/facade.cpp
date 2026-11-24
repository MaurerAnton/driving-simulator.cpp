// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/facade.ts @ v1.7.7
/** What the faces of a building need for their facades (see facade.h). */
#include "world/facade.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "core/js.h"
#include "world/tilefmt.h"

namespace fs {

namespace {

/** a corner higher than this above the foot of a wall can be its eave */
const double EAVE_ABOVE = 0.6;
/** how far (in units of UV_SCALE) what a rule gives may be from what the map build wanted */
const int TOLERANCE = 2;
/** a run is written as  corners × 8 + rule  in 16 bits */
const size_t MAX_RUN = 8191;

int16_t q16(double v) {
  const double q = jsMax(-32767, jsMin(32767, jsRound(v / UV_SCALE)));
  // (what is no number is 0 in an array of whole numbers)
  return q == q ? static_cast<int16_t>(q) : 0;
}

template <class Index>
PackedFaces packFacesOf(std::span<const int16_t> pos, std::span<const Index> idx, std::span<const int16_t> a16) {
  const size_t count = pos.size() / 3;
  // which corners belong together
  std::vector<int32_t> parent(count);
  for (size_t i = 0; i < count; i++) parent[i] = static_cast<int32_t>(i);
  auto find = [&](size_t i) -> size_t {
    while (static_cast<size_t>(parent[i]) != i) {
      parent[i] = parent[static_cast<size_t>(parent[i])];
      i = static_cast<size_t>(parent[i]);
    }
    return i;
  };
  for (size_t t = 0; t + 2 < idx.size(); t += 3) {
    const size_t a = find(idx[t]);
    const size_t b = find(idx[t + 1]);
    const size_t c = find(idx[t + 2]);
    const size_t m = std::min(a, std::min(b, c));
    parent[a] = static_cast<int32_t>(m);
    parent[b] = static_cast<int32_t>(m);
    parent[c] = static_cast<int32_t>(m);
  }
  PackedFaces packed;
  std::vector<uint16_t>& run = packed.run;
  std::vector<int16_t>& uvx = packed.uvx;
  std::vector<int16_t> tmp(a16.size(), 0);
  auto explicitRun = [&](size_t first, size_t n) {
    for (size_t i = first * 4; i < (first + n) * 4; i++) uvx.push_back(i < a16.size() ? a16[i] : 0);
    // (runs without a rule that follow each other are one run)
    if (!run.empty() && (run.back() & 7) == FACE_EXPLICIT && static_cast<size_t>(run.back() >> 3) + n <= MAX_RUN) run.back() = static_cast<uint16_t>(run.back() + (n << 3));
    else run.push_back(static_cast<uint16_t>((n << 3) | FACE_EXPLICIT));
  };
  auto fits = [&](size_t first, size_t n) {
    for (size_t i = first * 4; i < (first + n) * 4; i++)
      if (std::abs(tmp[i] - (i < a16.size() ? a16[i] : 0)) > TOLERANCE) return false;
    return true;
  };
  size_t first = 0;
  while (first < count) {
    const size_t group = find(first);
    size_t n = 1;
    while (first + n < count && n < MAX_RUN && find(first + n) == group) n++;
    int rule = FACE_EXPLICIT;
    for (int r : {FACE_WALL_SUNK, FACE_WALL, FACE_ROOF_SLOPED, FACE_ROOF_FLAT}) {
      if ((first + n) * 4 <= tmp.size() && deriveFace(pos, first, n, r, tmp) && fits(first, n)) {
        rule = r;
        break;
      }
    }
    if (rule == FACE_EXPLICIT) explicitRun(first, n);
    else {
      run.push_back(static_cast<uint16_t>((n << 3) | static_cast<size_t>(rule)));
      packed.ruled += static_cast<int>(n);
    }
    first += n;
  }
  return packed;
}

}  // namespace

bool deriveFace(std::span<const int16_t> pos, size_t first, size_t n, int rule, std::span<int16_t> out) {
  if (n < 3) return false;
  const size_t o = first * 3;
  // (a run that reaches beyond the corners there are, or beyond `out`: no numbers can be made for it)
  if (o + n * 3 > pos.size() || (first + n) * 4 > out.size()) return false;
  if (rule == FACE_ROOF_FLAT) {
    double uMin = JS_INF;
    double vMin = JS_INF;
    for (size_t i = 0; i < n; i++) {
      uMin = jsMin(uMin, pos[o + i * 3]);
      vMin = jsMin(vMin, pos[o + i * 3 + 2]);
    }
    for (size_t i = 0; i < n; i++) {
      const size_t k = (first + i) * 4;
      out[k] = q16((pos[o + i * 3] - uMin) * POS_XZ_SCALE);
      out[k + 1] = q16((pos[o + i * 3 + 2] - vMin) * POS_XZ_SCALE);
      out[k + 2] = 0;
      out[k + 3] = 0;
    }
    return true;
  }
  // which way the face looks (Newell): the corners in metres, around the first one
  const double x0 = pos[o];
  const double y0 = pos[o + 1];
  const double z0 = pos[o + 2];
  double nx = 0;
  double ny = 0;
  double nz = 0;
  for (size_t i = 0; i < n; i++) {
    const size_t a = o + i * 3;
    const size_t b = o + ((i + 1) % n) * 3;
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
  double uMin = JS_INF;
  double uMax = -JS_INF;
  for (size_t i = 0; i < n; i++) {
    const double u = (pos[o + i * 3] - x0) * POS_XZ_SCALE * ux + (pos[o + i * 3 + 2] - z0) * POS_XZ_SCALE * uz;
    uMin = jsMin(uMin, u);
    uMax = jsMax(uMax, u);
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
    double vMin = JS_INF;
    for (size_t i = 0; i < n; i++) {
      const double v = (pos[o + i * 3] - x0) * POS_XZ_SCALE * vx + (pos[o + i * 3 + 1] - y0) * POS_Y_SCALE * vy + (pos[o + i * 3 + 2] - z0) * POS_XZ_SCALE * vz;
      vMin = jsMin(vMin, v);
    }
    for (size_t i = 0; i < n; i++) {
      const size_t k = (first + i) * 4;
      const double x = (pos[o + i * 3] - x0) * POS_XZ_SCALE;
      const double y = (pos[o + i * 3 + 1] - y0) * POS_Y_SCALE;
      const double z = (pos[o + i * 3 + 2] - z0) * POS_XZ_SCALE;
      out[k] = q16(x * ux + z * uz - uMin);
      out[k + 1] = q16(x * vx + y * vy + z * vz - vMin);
      out[k + 2] = 0;
      out[k + 3] = 0;
    }
    return true;
  }
  if (rule != FACE_WALL && rule != FACE_WALL_SUNK) return false;
  // a wall: heights above its foot (where the foot was before it was sunk)
  double low = JS_INF;
  for (size_t i = 0; i < n; i++) low = jsMin(low, pos[o + i * 3 + 1]);
  const bool sunk = rule == FACE_WALL_SUNK;
  const double lo = low * POS_Y_SCALE + (sunk ? SINK : 0);
  auto height = [&](size_t i) -> double {
    const double y = pos[o + i * 3 + 1] * POS_Y_SCALE;
    return sunk && y < low * POS_Y_SCALE + SINK / 2 ? y + SINK : y;
  };
  double hi = -JS_INF;
  for (size_t i = 0; i < n; i++) hi = jsMax(hi, height(i));
  double eave = hi;
  for (size_t i = 0; i < n; i++) {
    const double y = height(i);
    if (y > lo + EAVE_ABOVE && y < eave) eave = y;
  }
  const int16_t L = q16(uMax - uMin);
  const int16_t H = q16(eave - lo);
  for (size_t i = 0; i < n; i++) {
    const size_t k = (first + i) * 4;
    out[k] = q16((pos[o + i * 3] - x0) * POS_XZ_SCALE * ux + (pos[o + i * 3 + 2] - z0) * POS_XZ_SCALE * uz - uMin);
    out[k + 1] = q16(height(i) - lo);
    out[k + 2] = L;
    out[k + 3] = H;
  }
  return true;
}

PackedFaces packFaces(std::span<const int16_t> pos, std::span<const uint16_t> idx, std::span<const int16_t> a16) { return packFacesOf<uint16_t>(pos, idx, a16); }
PackedFaces packFaces(std::span<const int16_t> pos, std::span<const uint32_t> idx, std::span<const int16_t> a16) { return packFacesOf<uint32_t>(pos, idx, a16); }

std::vector<int16_t> unpackFaces(std::span<const int16_t> pos, std::span<const uint16_t> run, const std::vector<int16_t>* uvx) {
  std::vector<int16_t> out((pos.size() / 3) * 4, 0);
  size_t first = 0;
  size_t x = 0;
  for (size_t r = 0; r < run.size(); r++) {
    const size_t n = run[r] >> 3;
    const int rule = run[r] & 7;
    if (rule == FACE_EXPLICIT) {
      if (uvx) {
        for (size_t i = first * 4; i < (first + n) * 4; i++) {
          // (numbers the tile does not have are 0; corners the mesh does not have take none)
          const int16_t v = x < uvx->size() ? (*uvx)[x] : 0;
          x++;
          if (i < out.size()) out[i] = v;
        }
      }
    } else {
      deriveFace(pos, first, n, rule, out);
    }
    first += n;
  }
  return out;
}

}  // namespace fs

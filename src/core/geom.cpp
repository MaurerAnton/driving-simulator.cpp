// SPDX-License-Identifier: AGPL-3.0-only
/**
 * 2D geometry helpers on the ground plane.
 *
 * Origin: src/core/geom.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * The maths mirrors the JavaScript originals so numbers come out the same.
 */
#include "core/geom.h"

#include <algorithm>
#include <cmath>

namespace fs {

LineHit lineLine(double px, double pz, double dx, double dz,
                 double qx, double qz, double ex, double ez) {
  LineHit r;
  double den = dx * ez - dz * ex;
  if (std::fabs(den) < 1e-9) return r;
  double rx = qx - px;
  double rz = qz - pz;
  r.t = (rx * ez - rz * ex) / den;
  r.u = (rx * dz - rz * dx) / den;
  r.has = true;
  return r;
}

LineHit segSeg(double ax, double az, double bx, double bz,
               double cx, double cz, double dx, double dz) {
  LineHit r = lineLine(ax, az, bx - ax, bz - az, cx, cz, dx - cx, dz - cz);
  if (!r.has) return r;
  if (r.t < 0 || r.t > 1 || r.u < 0 || r.u > 1) r.has = false;
  return r;
}

double ringArea(const std::vector<double>& ring) {
  double a = 0;
  const size_t n = ring.size();
  for (size_t i = 0; i < n; i += 2) {
    size_t j = (i + 2) % n;
    // north-up coordinates are (x, -z)
    a += ring[i] * -ring[j + 1] - ring[j] * -ring[i + 1];
  }
  return a / 2;
}

bool pointInRing(double x, double z, const std::vector<double>& ring) {
  bool inside = false;
  const size_t n = ring.size();
  for (size_t i = 0, j = n - 2; i < n; j = i, i += 2) {
    double xi = ring[i], zi = ring[i + 1];
    double xj = ring[j], zj = ring[j + 1];
    if ((zi > z) != (zj > z) && x < (xj - xi) * (z - zi) / (zj - zi) + xi) inside = !inside;
  }
  return inside;
}

double projectOnSegment(double px, double pz, double ax, double az, double bx, double bz) {
  double dx = bx - ax, dz = bz - az;
  double l2 = dx * dx + dz * dz;
  if (l2 < 1e-12) return 0;
  return clamp(((px - ax) * dx + (pz - az) * dz) / l2, 0, 1);
}

Polyline3 makePolyline(const std::vector<double>& pts) {
  Polyline3 pl;
  pl.pts = pts;
  size_t n = pts.size() / 3;
  pl.cum.assign(n, 0);
  double acc = 0;
  for (size_t i = 1; i < n; i++)
    acc += std::hypot(pts[i * 3] - pts[i * 3 - 3], pts[i * 3 + 2] - pts[i * 3 - 1]);
  if (n > 0) pl.cum[n - 1] = acc;
  for (size_t i = 1; i + 1 < n; i++) {
    double prev = pl.cum[i - 1];
    double cur = 0;
    for (size_t k = 1; k <= i; k++)
      cur += std::hypot(pts[k * 3] - pts[k * 3 - 3], pts[k * 3 + 2] - pts[k * 3 - 1]);
    pl.cum[i] = cur;
    (void)prev;
  }
  // recompute cum cleanly (the loop above was a first pass; do it once, properly)
  pl.cum.assign(n, 0);
  acc = 0;
  for (size_t i = 1; i < n; i++) {
    acc += std::hypot(pts[i * 3] - pts[i * 3 - 3], pts[i * 3 + 2] - pts[i * 3 - 1]);
    pl.cum[i] = acc;
  }
  pl.len = acc;
  return pl;
}

PolySample newPolySample() {
  PolySample s;
  s.x = 0; s.y = 0; s.z = 0; s.tx = 1; s.tz = 0; s.seg = 0;
  return s;
}

PolySample samplePolyline(const Polyline3& pl, double s, PolySample out, int hint) {
  const auto& pts = pl.pts;
  const auto& cum = pl.cum;
  const size_t n = cum.size();
  if (n < 2) {
    out.x = pts[0];
    out.y = pts[1];
    out.z = pts[2];
    out.tx = 1;
    out.tz = 0;
    out.seg = 0;
    return out;
  }
  s = clamp(s, 0, pl.len);
  size_t i = static_cast<size_t>(clamp(static_cast<double>(hint), 0, static_cast<double>(n - 2)));
  if (cum[i] > s || cum[i + 1] < s) {
    size_t lo = 0, hi = n - 2;
    while (lo < hi) {
      size_t mid = (lo + hi + 1) >> 1;
      if (cum[mid] <= s) lo = mid;
      else hi = mid - 1;
    }
    i = lo;
  }
  double segLen = cum[i + 1] - cum[i];
  double t = segLen > 1e-9 ? (s - cum[i]) / segLen : 0;
  size_t a = i * 3;
  double dx = pts[a + 3] - pts[a];
  double dz = pts[a + 5] - pts[a + 2];
  double l = std::hypot(dx, dz);
  if (l == 0) l = 1;
  out.x = pts[a] + dx * t;
  out.y = pts[a + 1] + (pts[a + 4] - pts[a + 1]) * t;
  out.z = pts[a + 2] + dz * t;
  out.tx = dx / l;
  out.tz = dz / l;
  out.seg = static_cast<int>(i);
  return out;
}

PolyProjection projectOnPolyline(const Polyline3& pl, double x, double z, PolyProjection out,
                                 int i0, int i1) {
  const auto& pts = pl.pts;
  const auto& cum = pl.cum;
  if (i1 < 0) i1 = static_cast<int>(cum.size()) - 2;
  double best = 1.0e308;
  double bestS = 0, bestOff = 0;
  int bestSeg = i0;
  for (int i = i0; i <= i1; i++) {
    size_t a = static_cast<size_t>(i) * 3;
    double ax = pts[a], az = pts[a + 2];
    double dx = pts[a + 3] - ax, dz = pts[a + 5] - az;
    double l2 = dx * dx + dz * dz;
    double t = l2 > 1e-12 ? ((x - ax) * dx + (z - az) * dz) / l2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    double cx = ax + dx * t, cz = az + dz * t;
    double ex = x - cx, ez = z - cz;
    double d2 = ex * ex + ez * ez;
    if (d2 < best) {
      best = d2;
      double l = std::sqrt(l2);
      if (l == 0) l = 1;
      bestS = cum[static_cast<size_t>(i)] + l * t;
      // left of direction (dx,dz) is (dz,-dx)
      bestOff = (ex * dz - ez * dx) / l;
      bestSeg = i;
    }
  }
  out.s = bestS;
  out.off = bestOff;
  out.d2 = best;
  out.seg = bestSeg;
  return out;
}

std::vector<double> offsetPolyline(const std::vector<double>& pts, double off, double miterLimit) {
  size_t n = pts.size() / 3;
  std::vector<double> out(pts.size(), 0);
  for (size_t i = 0; i < n; i++) {
    size_t a = i * 3;
    double nx = 0, nz = 0, scale = 1;
    if (i > 0 && i < n - 1) {
      double d1x = pts[a] - pts[a - 3], d1z = pts[a + 2] - pts[a - 1];
      double d2x = pts[a + 3] - pts[a], d2z = pts[a + 5] - pts[a + 2];
      double l1 = std::hypot(d1x, d1z);
      double l2 = std::hypot(d2x, d2z);
      if (l1 == 0) l1 = 1;
      if (l2 == 0) l2 = 1;
      d1x /= l1; d1z /= l1;
      d2x /= l2; d2z /= l2;
      // average of the two left normals
      nx = d1z + d2z;
      nz = -d1x - d2x;
      double nl = std::hypot(nx, nz);
      if (nl < 1e-6) {
        nx = d1z;
        nz = -d1x;
      } else {
        nx /= nl;
        nz /= nl;
        // miter length = 1 / cos(half angle)
        double cosHalf = nx * d1z + nz * -d1x;
        scale = std::min(miterLimit, 1 / std::max(cosHalf, 1e-3));
      }
    } else {
      size_t b = i == 0 ? a : a - 3;
      double dx = pts[b + 3] - pts[b], dz = pts[b + 5] - pts[b + 2];
      double l = std::hypot(dx, dz);
      if (l == 0) l = 1;
      nx = dz / l;
      nz = -dx / l;
    }
    out[a] = pts[a] + nx * off * scale;
    out[a + 1] = pts[a + 1];
    out[a + 2] = pts[a + 2] + nz * off * scale;
  }
  return out;
}

std::vector<double> slicePolyline(const Polyline3& pl, double s0, double s1) {
  std::vector<double> out;
  PolySample tmp = newPolySample();
  s0 = clamp(s0, 0, pl.len);
  s1 = clamp(s1, s0, pl.len);
  tmp = samplePolyline(pl, s0, tmp);
  out.push_back(tmp.x);
  out.push_back(tmp.y);
  out.push_back(tmp.z);
  size_t n = pl.cum.size();
  for (size_t i = static_cast<size_t>(tmp.seg) + 1; i < n; i++) {
    if (pl.cum[i] <= s0 + 1e-6) continue;
    if (pl.cum[i] >= s1 - 1e-6) break;
    out.push_back(pl.pts[i * 3]);
    out.push_back(pl.pts[i * 3 + 1]);
    out.push_back(pl.pts[i * 3 + 2]);
  }
  tmp = samplePolyline(pl, s1, tmp);
  out.push_back(tmp.x);
  out.push_back(tmp.y);
  out.push_back(tmp.z);
  return out;
}

double hash01(int n) {
  uint32_t t = static_cast<uint32_t>(n) ^ 0x9e3779b9u;
  t = (t ^ (t >> 16)) * 0x85ebca6bu;
  t = (t ^ (t >> 13)) * 0xc2b2ae35u;
  t ^= t >> 16;
  return static_cast<double>(t) / 4294967296.0;
}

}  // namespace fs

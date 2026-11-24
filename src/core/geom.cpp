// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/core/geom.ts @ v1.7.7
/**
 * 2D geometry helpers on the ground plane.  The arithmetic follows the original
 * operation for operation, so that the numbers come out the same.
 */
#include "core/geom.h"

namespace fs {

LineHit lineLine(double px, double pz, double dx, double dz, double qx, double qz, double ex, double ez) {
  LineHit r;
  const double den = dx * ez - dz * ex;
  if (std::fabs(den) < 1e-9) return r;
  const double rx = qx - px;
  const double rz = qz - pz;
  r.t = (rx * ez - rz * ex) / den;
  r.u = (rx * dz - rz * dx) / den;
  r.has = true;
  return r;
}

LineHit segSeg(double ax, double az, double bx, double bz, double cx, double cz, double dx, double dz) {
  LineHit r = lineLine(ax, az, bx - ax, bz - az, cx, cz, dx - cx, dz - cz);
  if (!r.has) return r;
  if (r.t < 0 || r.t > 1 || r.u < 0 || r.u > 1) return LineHit();
  return r;
}

double projectOnSegment(double px, double pz, double ax, double az, double bx, double bz) {
  const double dx = bx - ax;
  const double dz = bz - az;
  const double l2 = dx * dx + dz * dz;
  if (l2 < 1e-12) return 0;
  return clamp(((px - ax) * dx + (pz - az) * dz) / l2, 0, 1);
}

Polyline3 makePolyline(std::vector<double> pts) {
  Polyline3 pl;
  const size_t n = pts.size() / 3;
  pl.cum.assign(n, 0);
  double acc = 0;
  for (size_t i = 1; i < n; i++) {
    acc += jsHypot(pts[i * 3] - pts[i * 3 - 3], pts[i * 3 + 2] - pts[i * 3 - 1]);
    pl.cum[i] = acc;
  }
  pl.pts = std::move(pts);
  pl.len = acc;
  return pl;
}

PolySample& samplePolyline(const Polyline3& pl, double s, PolySample& out, int hint) {
  const std::vector<double>& pts = pl.pts;
  const std::vector<double>& cum = pl.cum;
  const int n = static_cast<int>(cum.size());
  if (n < 2) {
    // (a line of one point – or of none, where the original reads `undefined`)
    out.x = pts.size() > 0 ? pts[0] : JS_NAN;
    out.y = pts.size() > 1 ? pts[1] : JS_NAN;
    out.z = pts.size() > 2 ? pts[2] : JS_NAN;
    out.tx = 1;
    out.tz = 0;
    out.seg = 0;
    return out;
  }
  s = clamp(s, 0, pl.len);
  int i = hint < 0 ? 0 : hint > n - 2 ? n - 2 : hint;
  if (cum[i] > s || cum[i + 1] < s) {
    // binary search
    int lo = 0;
    int hi = n - 2;
    while (lo < hi) {
      const int mid = (lo + hi + 1) >> 1;
      if (cum[mid] <= s) lo = mid;
      else hi = mid - 1;
    }
    i = lo;
  }
  const double segLen = cum[i + 1] - cum[i];
  const double t = segLen > 1e-9 ? (s - cum[i]) / segLen : 0;
  const size_t a = static_cast<size_t>(i) * 3;
  const double dx = pts[a + 3] - pts[a];
  const double dz = pts[a + 5] - pts[a + 2];
  double l = jsHypot(dx, dz);
  if (l == 0 || l != l) l = 1;  // `|| 1`: for 0 and for NaN
  out.x = pts[a] + dx * t;
  out.y = pts[a + 1] + (pts[a + 4] - pts[a + 1]) * t;
  out.z = pts[a + 2] + dz * t;
  out.tx = dx / l;
  out.tz = dz / l;
  out.seg = i;
  return out;
}

PolyProjection& projectOnPolyline(const Polyline3& pl, double x, double z, PolyProjection& out, int i0, int i1) {
  const std::vector<double>& pts = pl.pts;
  const std::vector<double>& cum = pl.cum;
  if (i1 == LAST_SEGMENT) i1 = static_cast<int>(cum.size()) - 2;
  double best = JS_INF;
  double bestS = 0;
  double bestOff = 0;
  int bestSeg = i0;
  for (int i = i0; i <= i1; i++) {
    const size_t a = static_cast<size_t>(i) * 3;
    const double ax = pts[a];
    const double az = pts[a + 2];
    const double dx = pts[a + 3] - ax;
    const double dz = pts[a + 5] - az;
    const double l2 = dx * dx + dz * dz;
    double t = l2 > 1e-12 ? ((x - ax) * dx + (z - az) * dz) / l2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    const double cx = ax + dx * t;
    const double cz = az + dz * t;
    const double ex = x - cx;
    const double ez = z - cz;
    const double d2 = ex * ex + ez * ez;
    if (d2 < best) {
      best = d2;
      double l = std::sqrt(l2);
      if (l == 0 || l != l) l = 1;
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
  const size_t n = pts.size() / 3;
  std::vector<double> out(n * 3, 0.0);
  for (size_t i = 0; i < n; i++) {
    const size_t a = i * 3;
    double nx = 0;
    double nz = 0;
    double scale = 1;
    if (i > 0 && i + 1 < n) {
      double d1x = pts[a] - pts[a - 3];
      double d1z = pts[a + 2] - pts[a - 1];
      double d2x = pts[a + 3] - pts[a];
      double d2z = pts[a + 5] - pts[a + 2];
      double l1 = jsHypot(d1x, d1z);
      if (l1 == 0 || l1 != l1) l1 = 1;
      double l2 = jsHypot(d2x, d2z);
      if (l2 == 0 || l2 != l2) l2 = 1;
      d1x /= l1;
      d1z /= l1;
      d2x /= l2;
      d2z /= l2;
      // average of the two left normals
      nx = d1z + d2z;
      nz = -d1x - d2x;
      const double nl = jsHypot(nx, nz);
      if (nl < 1e-6) {
        nx = d1z;
        nz = -d1x;
      } else {
        nx /= nl;
        nz /= nl;
        // miter length = 1 / cos(half angle)
        const double cosHalf = nx * d1z + nz * -d1x;
        scale = jsMin(miterLimit, 1 / jsMax(cosHalf, 1e-3));
      }
    } else {
      // (a line of one point has no direction: the original reads past its end there, NaN)
      const size_t b = i == 0 ? a : a - 3;
      const double dx = (b + 3 < pts.size() ? pts[b + 3] : JS_NAN) - pts[b];
      const double dz = (b + 5 < pts.size() ? pts[b + 5] : JS_NAN) - pts[b + 2];
      double l = jsHypot(dx, dz);
      if (l == 0 || l != l) l = 1;
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
  samplePolyline(pl, s0, tmp);
  out.push_back(tmp.x);
  out.push_back(tmp.y);
  out.push_back(tmp.z);
  const int n = static_cast<int>(pl.cum.size());
  for (int i = tmp.seg + 1; i < n; i++) {
    if (pl.cum[i] <= s0 + 1e-6) continue;
    if (pl.cum[i] >= s1 - 1e-6) break;
    out.push_back(pl.pts[static_cast<size_t>(i) * 3]);
    out.push_back(pl.pts[static_cast<size_t>(i) * 3 + 1]);
    out.push_back(pl.pts[static_cast<size_t>(i) * 3 + 2]);
  }
  samplePolyline(pl, s1, tmp);
  out.push_back(tmp.x);
  out.push_back(tmp.y);
  out.push_back(tmp.z);
  return out;
}

double hash01(double n) {
  uint32_t t = static_cast<uint32_t>(jsInt32(n)) ^ 0x9e3779b9u;
  t = (t ^ (t >> 16)) * 0x85ebca6bu;
  t = (t ^ (t >> 13)) * 0xc2b2ae35u;
  return static_cast<double>(t ^ (t >> 16)) / 4294967296.0;
}

}  // namespace fs

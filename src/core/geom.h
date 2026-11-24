// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/core/geom.ts @ v1.7.7
/**
 * 2D geometry helpers on the ground plane.
 *
 * Ground-plane convention: points are (x, z) with x = east and z = SOUTH.
 * Seen from above this is a left-handed pair, so "left of travel" is spelled
 * out explicitly here instead of relying on the usual maths conventions:
 *
 *   travel direction d = (dx, dz)   ->   left = (dz, -dx),  right = (-dz, dx)
 *
 * `angleOf` returns the mathematical angle with north up (counter-clockwise
 * positive when seen from above), so sorting by it walks around a junction
 * counter-clockwise.
 *
 * Where the original hands in an object to be filled (`samplePolyline(pl, s, out)`),
 * the port takes a reference: the call reads the same, and the result is in `out`.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/js.h"
#include "engine/math.h"

namespace fs {

// PI/TAU/DEG come from engine/math.h (one definition for the whole port);
// PI_ is the alias the first draft of this file started with.
constexpr double PI_ = 3.14159265358979323846;

inline double clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

inline double lerp(double a, double b, double t) { return a + (b - a) * t; }

inline double smoothstep(double e0, double e1, double x) {
  if (e1 == e0) return x < e0 ? 0 : 1;
  double t = clamp((x - e0) / (e1 - e0), 0, 1);
  return t * t * (3 - 2 * t);
}

/** Counter-clockwise (seen from above) angle of a ground direction, 0 = east. */
inline double angleOf(double dx, double dz) { return std::atan2(-dz, dx); }

/** Wrap to (-π, π]. */
inline double wrapAngle(double a) {
  a = std::fmod(a, TAU);
  if (a > PI) a -= TAU;
  else if (a <= -PI) a += TAU;
  return a;
}

/** Wrap to [0, 2π). */
inline double wrap2Pi(double a) {
  a = std::fmod(a, TAU);
  return a < 0 ? a + TAU : a;
}

inline double dist2(double ax, double az, double bx, double bz) {
  double dx = bx - ax;
  double dz = bz - az;
  return dx * dx + dz * dz;
}

/**
 * Math.hypot of two numbers, to the last bit as the original's engine (V8) works it out: both
 * divided by the larger, their squares added, the root multiplied back.  The C library's hypot
 * rounds better and so differs from it in the last digit now and then – and lengths are
 * compared with limits all over the map.
 */
inline double jsHypot(double a, double b) {
  const bool aNaN = a != a;
  const bool bNaN = b != b;
  const double x = std::fabs(a);
  const double y = std::fabs(b);
  double max = 0;
  if (!aNaN && x > max) max = x;
  if (!bNaN && y > max) max = y;
  if (max == JS_INF) return JS_INF;
  if (aNaN || bNaN) return JS_NAN;
  if (max == 0) return 0;
  const double nx = x / max;
  const double ny = y / max;
  const double sx = nx * nx;
  const double sy = ny * ny;
  return std::sqrt(sx + sy) * max;
}

inline double dist(double ax, double az, double bx, double bz) { return jsHypot(bx - ax, bz - az); }

/** What lineLine and segSeg answer: the two parameters ([t, u] of the original), or has = false for its null. */
struct LineHit {
  double t = 0, u = 0;
  bool has = false;
  explicit operator bool() const { return has; }
};

/**
 * Intersection of two infinite lines p + t·d and q + u·e.
 * Returns [t, u] or null (has = false) when (nearly) parallel.
 */
LineHit lineLine(double px, double pz, double dx, double dz, double qx, double qz, double ex, double ez);

/** Segment/segment intersection parameters in [0,1] or null (has = false). */
LineHit segSeg(double ax, double az, double bx, double bz, double cx, double cz, double dx, double dz);

/** Signed area of a flat [x,z,x,z,…] ring (positive = counter-clockwise seen from above). */
template <class Ring>
double ringArea(const Ring& ring) {
  double a = 0;
  const size_t n = ring.size();
  for (size_t i = 0; i < n; i += 2) {
    const size_t j = (i + 2) % n;
    // north-up coordinates are (x, -z)
    a += static_cast<double>(ring[i]) * -static_cast<double>(ring[j + 1]) - static_cast<double>(ring[j]) * -static_cast<double>(ring[i + 1]);
  }
  return a / 2;
}
inline double ringArea(const std::vector<double>& ring) { return ringArea<std::vector<double>>(ring); }

/** Even-odd point-in-ring test for a flat [x,z,…] ring. */
template <class Ring>
bool pointInRing(double x, double z, const Ring& ring) {
  bool inside = false;
  const size_t n = ring.size();
  if (n < 2) return false;
  for (size_t i = 0, j = n - 2; i < n; j = i, i += 2) {
    const double xi = ring[i];
    const double zi = ring[i + 1];
    const double xj = ring[j];
    const double zj = ring[j + 1];
    if ((zi > z) != (zj > z) && x < ((xj - xi) * (z - zi)) / (zj - zi) + xi) inside = !inside;
  }
  return inside;
}
inline bool pointInRing(double x, double z, const std::vector<double>& ring) { return pointInRing<std::vector<double>>(x, z, ring); }

/** Closest point on segment a-b to p. Returns parameter t in [0,1]. */
double projectOnSegment(double px, double pz, double ax, double az, double bx, double bz);

/**
 * A polyline stored as stride-3 [x, y, z] vertices (y = height) with cached
 * cumulative ground-plane arc length.  Used for road centrelines and lanes.
 */
struct Polyline3 {
  std::vector<double> pts;
  std::vector<double> cum;
  double len = 0;
};

Polyline3 makePolyline(std::vector<double> pts);

struct PolySample {
  double x = 0, y = 0, z = 0;
  /** unit ground-plane tangent */
  double tx = 1, tz = 0;
  /** segment index used */
  int seg = 0;
};

inline PolySample newPolySample() { return PolySample(); }

/** Sample position and tangent at arc length `s` (clamped). `hint` = previous segment index. */
PolySample& samplePolyline(const Polyline3& pl, double s, PolySample& out, int hint = 0);

struct PolyProjection {
  /** arc length of the closest point */
  double s = 0;
  /** signed lateral offset, positive = LEFT of the polyline direction */
  double off = 0;
  /** squared distance */
  double d2 = 0;
  int seg = 0;
};

/** (`i1` left out: up to the last segment, pl.cum.length − 2) */
constexpr int LAST_SEGMENT = std::numeric_limits<int>::min();

/** Project a point onto a polyline (closest point over all segments in [i0, i1]). */
PolyProjection& projectOnPolyline(const Polyline3& pl, double x, double z, PolyProjection& out, int i0 = 0, int i1 = LAST_SEGMENT);

/**
 * Offset a stride-3 polyline sideways (positive = left) with mitred joints.
 * Heights are copied.  Returns a new stride-3 array of the same vertex count.
 */
std::vector<double> offsetPolyline(const std::vector<double>& pts, double off, double miterLimit = 2.5);

/** Cut a stride-3 polyline to the arc-length window [s0, s1]. */
std::vector<double> slicePolyline(const Polyline3& pl, double s0, double s1);

/** Uniform spatial hash over the ground plane for coarse neighbourhood queries. */
template <typename T>
class Grid2 {
 public:
  double cell;

  explicit Grid2(double cell_) : cell(cell_) {}

  void insertBox(double minX, double minZ, double maxX, double maxZ, const T& item) {
    const double c = cell;
    const int64_t x0 = static_cast<int64_t>(std::floor(minX / c));
    const int64_t x1 = static_cast<int64_t>(std::floor(maxX / c));
    const int64_t z0 = static_cast<int64_t>(std::floor(minZ / c));
    const int64_t z1 = static_cast<int64_t>(std::floor(maxZ / c));
    for (int64_t ix = x0; ix <= x1; ix++) {
      for (int64_t iz = z0; iz <= z1; iz++) map_[key(ix, iz)].push_back(item);
    }
  }

  void insertPoint(double x, double z, const T& item) { insertBox(x, z, x, z, item); }

  /** Take an item out again – with the box it was put in with. */
  void removeBox(double minX, double minZ, double maxX, double maxZ, const T& item) {
    const double c = cell;
    const int64_t x0 = static_cast<int64_t>(std::floor(minX / c));
    const int64_t x1 = static_cast<int64_t>(std::floor(maxX / c));
    const int64_t z0 = static_cast<int64_t>(std::floor(minZ / c));
    const int64_t z1 = static_cast<int64_t>(std::floor(maxZ / c));
    for (int64_t ix = x0; ix <= x1; ix++) {
      for (int64_t iz = z0; iz <= z1; iz++) {
        auto it = map_.find(key(ix, iz));
        if (it == map_.end()) continue;
        std::vector<T>& arr = it->second;
        auto at = std::find(arr.begin(), arr.end(), item);
        if (at == arr.end()) continue;
        // (the last one takes its place: whoever walks a cell meets the items in the original's order)
        *at = std::move(arr.back());
        arr.pop_back();
        if (arr.empty()) map_.erase(it);
      }
    }
  }

  /** Items whose cells overlap the query cell containing (x,z). */
  const std::vector<T>& at(double x, double z) const {
    static const std::vector<T> EMPTY;
    const double c = cell;
    auto it = map_.find(key(static_cast<int64_t>(std::floor(x / c)), static_cast<int64_t>(std::floor(z / c))));
    return it == map_.end() ? EMPTY : it->second;
  }

  /** Visit every item in cells overlapping the box (items may repeat). */
  template <class Visit>
  void queryBox(double minX, double minZ, double maxX, double maxZ, Visit&& visit) const {
    const double c = cell;
    const int64_t x0 = static_cast<int64_t>(std::floor(minX / c));
    const int64_t x1 = static_cast<int64_t>(std::floor(maxX / c));
    const int64_t z0 = static_cast<int64_t>(std::floor(minZ / c));
    const int64_t z1 = static_cast<int64_t>(std::floor(maxZ / c));
    for (int64_t ix = x0; ix <= x1; ix++) {
      for (int64_t iz = z0; iz <= z1; iz++) {
        auto it = map_.find(key(ix, iz));
        if (it == map_.end()) continue;
        const std::vector<T>& arr = it->second;
        for (size_t i = 0; i < arr.size(); i++) visit(arr[i]);
      }
    }
  }

 private:
  std::unordered_map<int64_t, std::vector<T>> map_;

  static int64_t key(int64_t ix, int64_t iz) { return (ix + 32768) * 65536 + (iz + 32768); }
};

/** Small deterministic PRNG (mulberry32) so generated content is reproducible. */
class Rng {
 public:
  explicit Rng(uint32_t seed) : a_(seed) {}
  double operator()() {
    a_ = a_ + 0x6d2b79f5u;
    uint32_t t = a_;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + ((t ^ (t >> 7)) * (t | 61u));
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
  }

 private:
  uint32_t a_;
};

/** `rng(seed)` of the original: the generator as an object that is called for each draw. */
inline Rng rng(double seed) { return Rng(jsUint32(seed)); }

/** Integer hash → [0,1). */
double hash01(double n);

}  // namespace fs

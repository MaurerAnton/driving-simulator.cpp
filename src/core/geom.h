// SPDX-License-Identifier: AGPL-3.0-only
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
 * Origin: src/core/geom.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#pragma once

#include <cmath>

#include "engine/math.h"
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace fs {

// PI/TAU/DEG come from engine/math.h (one definition for the whole port);
// PI_ is the historical alias this file started with.
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
  if (a > PI_) a -= TAU;
  else if (a <= -PI_) a += TAU;
  return a;
}

/** Wrap to [0, 2π). */
inline double wrap2Pi(double a) {
  a = std::fmod(a, TAU);
  return a < 0 ? a + TAU : a;
}

inline double dist2(double ax, double az, double bx, double bz) {
  double dx = bx - ax, dz = bz - az;
  return dx * dx + dz * dz;
}

inline double dist(double ax, double az, double bx, double bz) {
  return std::hypot(bx - ax, bz - az);
}

/** Result of lineLine/segSeg: the two parameters, or has=false when parallel. */
struct LineHit {
  double t = 0, u = 0;
  bool has = false;
};

/**
 * Intersection of two infinite lines p + t·d and q + u·e.
 * Returns has=false when (nearly) parallel.
 */
LineHit lineLine(double px, double pz, double dx, double dz,
                 double qx, double qz, double ex, double ez);

/** Segment/segment intersection parameters in [0,1] or has=false. */
LineHit segSeg(double ax, double az, double bx, double bz,
               double cx, double cz, double dx, double dz);

/** Signed area of a flat [x,z,x,z,…] ring (positive = counter-clockwise from above). */
double ringArea(const std::vector<double>& ring);

/** Even-odd point-in-ring test for a flat [x,z,…] ring. */
bool pointInRing(double x, double z, const std::vector<double>& ring);

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

Polyline3 makePolyline(const std::vector<double>& pts);

/** Sample position and tangent at arc length `s` (clamped). `hint` = previous segment index. */
struct PolySample {
  double x = 0, y = 0, z = 0;
  /** unit ground-plane tangent */
  double tx = 1, tz = 0;
  /** segment index used */
  int seg = 0;
};

PolySample newPolySample();
PolySample samplePolyline(const Polyline3& pl, double s, PolySample out, int hint = 0);

/** Project a point onto a polyline (closest point over all segments in [i0, i1]). */
struct PolyProjection {
  /** arc length of the closest point */
  double s = 0;
  /** signed lateral offset, positive = LEFT of the polyline direction */
  double off = 0;
  /** squared distance */
  double d2 = 0;
  int seg = 0;
};

PolyProjection projectOnPolyline(const Polyline3& pl, double x, double z, PolyProjection out,
                                 int i0 = 0, int i1 = -1);

/**
 * Offset a stride-3 polyline sideways (positive = left) with mitred joints.
 * Heights are copied.  Returns a new stride-3 array of the same vertex count.
 */
std::vector<double> offsetPolyline(const std::vector<double>& pts, double off, double miterLimit = 2.5);

/** Cut a stride-3 polyline to the arc-length window [s0, s1]. */
std::vector<double> slicePolyline(const Polyline3& pl, double s0, double s1);

/**
 * Uniform spatial hash over the ground plane for coarse neighbourhood queries.
 * Items are pointers so the same T can be used for nodes, edges, cars, …
 */
template <typename T>
class Grid2 {
 public:
  explicit Grid2(double cell) : cell_(cell) {}

  void insertBox(double minX, double minZ, double maxX, double maxZ, T item) {
    double c = cell_;
    long x0 = static_cast<long>(std::floor(minX / c));
    long x1 = static_cast<long>(std::floor(maxX / c));
    long z0 = static_cast<long>(std::floor(minZ / c));
    long z1 = static_cast<long>(std::floor(maxZ / c));
    for (long ix = x0; ix <= x1; ix++)
      for (long iz = z0; iz <= z1; iz++) map_[key(ix, iz)].push_back(item);
  }

  void insertPoint(double x, double z, T item) { insertBox(x, z, x, z, item); }

  /** Items whose cells overlap the query cell containing (x,z). */
  const std::vector<T>& at(double x, double z) const {
    static const std::vector<T> kEmpty;
    auto it = map_.find(key(static_cast<long>(std::floor(x / cell_)),
                            static_cast<long>(std::floor(z / cell_))));
    return it == map_.end() ? kEmpty : it->second;
  }

  /** Visit every item in cells overlapping the box (items may repeat). */
  void queryBox(double minX, double minZ, double maxX, double maxZ,
                const std::function<void(T)>& visit) const {
    double c = cell_;
    long x0 = static_cast<long>(std::floor(minX / c));
    long x1 = static_cast<long>(std::floor(maxX / c));
    long z0 = static_cast<long>(std::floor(minZ / c));
    long z1 = static_cast<long>(std::floor(maxZ / c));
    for (long ix = x0; ix <= x1; ix++)
      for (long iz = z0; iz <= z1; iz++) {
        auto it = map_.find(key(ix, iz));
        if (it == map_.end()) continue;
        for (const auto& item : it->second) visit(item);
      }
  }

  double cell() const { return cell_; }

 private:
  double cell_;
  std::map<int64_t, std::vector<T>> map_;

  static int64_t key(long ix, long iz) {
    return (static_cast<int64_t>(ix) + 32768) * 65536 + (static_cast<int64_t>(iz) + 32768);
  }
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
    return static_cast<double>((t ^ (t >> 14))) / 4294967296.0;
  }

 private:
  uint32_t a_;
};

/** Integer hash → [0,1). */
double hash01(int n);

}  // namespace fs

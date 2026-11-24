// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/plane.ts @ v1.7.7
#include "render/plane.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/js.h"
#include "engine/geometries.h"
#include "render/carBody.h"

namespace fs {

namespace {

const AircraftGeometry& T = TRAINER;

// ------------------------------------------------------------------------------- materials
MaterialPtr lambertOf(unsigned hex) {
  MaterialPtr m = lambert({.color = Color(hex)});
  // (the skin is not everywhere a closed solid – the windows are holes in it)
  m->shadowSide = Side::Double;
  return m;
}

/** the materials every aeroplane shares (made at first use) */
struct Shared {
  MaterialPtr PAINT = lambertOf(0xf1f2ef);
  MaterialPtr STRIPE = lambertOf(0x1b57a3);
  MaterialPtr ACCENT = lambertOf(0xe0a21c);
  MaterialPtr SEAM = lambertOf(0x9aa0a6);
  MaterialPtr RUBBER = lambertOf(0x1c1e21);
  MaterialPtr DARK = lambertOf(0x2b2e32);
  MaterialPtr METAL = lambertOf(0xa9afb5);
  MaterialPtr CHROME = phong({.color = Color(0xbcc1c6), .specular = Color(0xd8d8d8), .shininess = 80});
  MaterialPtr TYRE = lambertOf(0x161718);
  MaterialPtr HUB = lambertOf(0xc3c8cd);
  Shared() {
    // (white paint in the shade still looks light: the sky and the ground light it – more than the scene's
    // hemisphere light alone gives the wing's underside, which is in view from the cockpit all the time)
    PAINT->emissive.setHex(0x4c4e50);
  }
};
const Shared& shared() {
  static const Shared s;
  return s;
}

/**
 * The inside gets little direct light and the scene has no bounce light: a
 * self-lit share keeps it readable (as in render/car.ts).  One set per
 * aeroplane – the cabin can be made see-through.
 */
MaterialPtr cabinMaterial(unsigned hex, Side side = Side::Front) {
  const Color c(hex);
  return lambert({.color = c.clone().multiplyScalar(0.6), .side = side, .emissive = c.clone().multiplyScalar(0.42)});
}

// -------------------------------------------------------------------------- geometry helpers
using P2 = std::array<double, 2>;

Vector3 v3(const Vec3& p) { return Vector3(p[0], p[1], p[2]); }

/** Position and normal only, un-indexed: the form in which parts are merged. */
GeometryPtr bare(const GeometryPtr& g) {
  GeometryPtr n = g->index ? g->toNonIndexed() : g;
  std::vector<std::string> names;
  for (const auto& [name, a] : n->attributes) names.push_back(name);
  for (const std::string& name : names)
    if (name != "position" && name != "normal") n->deleteAttribute(name);
  return n;
}

GeometryPtr merge(const std::vector<GeometryPtr>& parts) {
  std::vector<GeometryPtr> b;
  b.reserve(parts.size());
  for (const GeometryPtr& p : parts) b.push_back(bare(p));
  return mergeGeometries(b, false);
}

/** A box from x0…x1, y0…y1, z0…z1. */
GeometryPtr block(double x0, double x1, double y0, double y1, double z0, double z1) {
  GeometryPtr g = boxGeometry(x1 - x0, y1 - y0, z1 - z0);
  g->translate((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
  return bare(g);
}

Vec3 sub3(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 cross3(const Vec3& a, const Vec3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
double dot3(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 unit3(const Vec3& a) {
  double l = std::hypot(a[0], a[1], a[2]);
  if (!l) l = 1;
  return {a[0] / l, a[1] / l, a[2] / l};
}

/** A round tube along a path, its radius per point (or one for all). */
GeometryPtr pipe(const std::vector<Vec3>& path, const std::vector<double>& radius, int seg = 8, bool caps = true) {
  std::vector<std::vector<Vec3>> rings;
  std::optional<Vector3> n1;
  const int n = static_cast<int>(path.size());
  for (int i = 0; i < n; i++) {
    const Vector3 a = v3(path[std::max(0, i - 1)]);
    Vector3 t = v3(path[std::min(n - 1, i + 1)]);
    t.sub(a).normalize();
    if (!n1) {
      Vector3 s(std::fabs(t.y) < 0.9 ? 0 : 1, std::fabs(t.y) < 0.9 ? 1 : 0, 0);
      s.cross(t).normalize();
      n1 = s;
    } else {
      n1->addScaledVector(t, -n1->dot(t)).normalize();
    }
    Vector3 n2;
    n2.crossVectors(t, *n1);
    const double r = radius.size() == 1 ? radius[0] : radius[i];
    std::vector<Vec3> ring;
    for (int k = 0; k < seg; k++) {
      const double an = (static_cast<double>(k) / seg) * PI * 2;
      const double c = std::cos(an) * r;
      const double s = std::sin(an) * r;
      ring.push_back({path[i][0] + n1->x * c + n2.x * s, path[i][1] + n1->y * c + n2.y * s, path[i][2] + n1->z * c + n2.z * s});
    }
    rings.push_back(ring);
  }
  return Sculpt().tube(rings, caps, caps).build(60);
}
GeometryPtr pipe(const std::vector<Vec3>& path, double radius, int seg = 8, bool caps = true) { return pipe(path, std::vector<double>{radius}, seg, caps); }

/** Outline of a rectangle with rounded corners, counter-clockwise. */
std::vector<P2> outline(double w, double h, double r, int seg = 3) {
  std::vector<P2> pts;
  const std::array<std::array<double, 3>, 4> corners = {{
      {w / 2 - r, -h / 2 + r, -90},
      {w / 2 - r, h / 2 - r, 0},
      {-w / 2 + r, h / 2 - r, 90},
      {-w / 2 + r, -h / 2 + r, 180},
  }};
  for (const auto& [cx, cy, a0] : corners) {
    for (int i = 0; i <= seg; i++) {
      const double a = (a0 + (90.0 * i) / seg) * DEG;
      pts.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r});
    }
  }
  return pts;
}

/** A block with rounded edges: w across (x), h high (y), d deep (z); turned by rx (pitch) and ry (yaw), moved to `at`. */
GeometryPtr slab(double w, double h, double d, double r, const Vec3& at, double rx = 0, double ry = 0) {
  const double b = std::min(r, d / 2) * 0.7;
  const std::array<P2, 4> zs = {{{-d / 2, b}, {-d / 2 + b, 0}, {d / 2 - b, 0}, {d / 2, b}}};
  std::vector<std::vector<Vec3>> rings;
  for (const auto& [z, s] : zs) {
    std::vector<Vec3> ring;
    for (const auto& [x, y] : outline(w - 2 * s, h - 2 * s, std::max(0.002, std::min({r, w / 2 - s, h / 2 - s}) - s * 0.5))) ring.push_back({x, y, z});
    rings.push_back(ring);
  }
  GeometryPtr g = Sculpt().tube(rings, true, true).build(50);
  g->applyMatrix4(Matrix4().makeRotationFromEuler(Euler(rx, ry, 0, Euler::YXZ)));
  g->translate(at[0], at[1], at[2]);
  return g;
}

/** A flat oval facing `facing` (a lamp's lens, an air inlet). */
GeometryPtr oval(const Vec3& centre, const Vec3& right, const Vec3& up, double rx, double ry, const Vec3& facing, int seg = 14) {
  std::vector<Vec3> ring;
  for (int i = 0; i < seg; i++) {
    const double a = (static_cast<double>(i) / seg) * PI * 2;
    const double c = std::cos(a) * rx;
    const double s = std::sin(a) * ry;
    ring.push_back({centre[0] + right[0] * c + up[0] * s, centre[1] + right[1] * c + up[1] * s, centre[2] + right[2] * c + up[2] * s});
  }
  return Sculpt().fan(ring, facing).build();
}

/** Monotone cubic interpolation (no overshoot between the stations). */
std::function<double(double)> pchip(const std::vector<double>& xs, const std::vector<double>& ys) {
  const size_t n = xs.size();
  std::vector<double> h;
  std::vector<double> d;
  for (size_t i = 0; i + 1 < n; i++) {
    h.push_back(xs[i + 1] - xs[i]);
    d.push_back((ys[i + 1] - ys[i]) / h[i]);
  }
  std::vector<double> m(n, 0);
  m[0] = d[0];
  m[n - 1] = d[n - 2];
  for (size_t i = 1; i + 1 < n; i++) {
    if (d[i - 1] * d[i] <= 0) m[i] = 0;
    else {
      const double w1 = 2 * h[i] + h[i - 1];
      const double w2 = h[i] + 2 * h[i - 1];
      m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i]);
    }
  }
  return [xs, ys, h, m, n](double x) {
    const double xx = std::min(xs[n - 1], std::max(xs[0], x));
    size_t i = 0;
    while (i < n - 2 && xx > xs[i + 1]) i++;
    const double t = (xx - xs[i]) / h[i];
    const double t2 = t * t;
    const double t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * ys[i] + (t3 - 2 * t2 + t) * h[i] * m[i] + (-2 * t3 + 3 * t2) * ys[i + 1] + (t3 - t2) * h[i] * m[i + 1];
  };
}

// ------------------------------------------------------------------------------- airfoils
struct Profile {
  /** thickness, camber, place of the camber (NACA four-digit) */
  double t;
  double m;
  double p;
};
const Profile WING = {0.12, 0.02, 0.4};
const Profile TAIL = {0.1, 0, 0.4};

double camber(double x, const Profile& f) {
  if (f.m == 0) return 0;
  return x < f.p ? (f.m / (f.p * f.p)) * (2 * f.p * x - x * x) : (f.m / ((1 - f.p) * (1 - f.p))) * (1 - 2 * f.p + 2 * f.p * x - x * x);
}

double halfThick(double x, double t) {
  const double s = std::min(1.0, std::max(0.0, x));
  return 5 * t * (0.2969 * std::sqrt(s) - 0.126 * s - 0.3516 * s * s + 0.2843 * s * s * s - 0.1036 * s * s * s * s);
}

/** The front of a section, from the leading edge to `to` (parts of the chord), closed by a flat face there: [chordwise, up]. */
std::vector<P2> foil(double to, int n, const Profile& f) {
  std::vector<double> xs;
  for (int i = 0; i <= n; i++) xs.push_back(to * (1 - std::cos(((static_cast<double>(i) / n) * PI) / 2)));
  std::vector<P2> pts;
  for (int i = n; i >= 0; i--) pts.push_back({xs[i], camber(xs[i], f) + halfThick(xs[i], f.t)});
  for (int i = 1; i <= n; i++) pts.push_back({xs[i], camber(xs[i], f) - halfThick(xs[i], f.t)});
  return pts;
}

/**
 * A control surface's section from `from` to `to` (parts of the chord): a
 * round nose that sits in the cove of the surface in front, the hinge in the
 * middle of the nose.
 */
struct AftSection {
  std::vector<P2> ring;
  P2 hinge;
};
AftSection aft(double from, double to, int n, const Profile& f) {
  const double xn = from + 0.012;
  const double r = halfThick(xn, f.t);
  const double yn = camber(xn, f);
  std::vector<P2> pts;
  for (int i = n; i >= 0; i--) {
    const double x = xn + ((to - xn) * i) / n;
    pts.push_back({x, camber(x, f) + halfThick(x, f.t)});
  }
  for (int k = 1; k < 6; k++) {
    const double a = PI / 2 + (PI * k) / 6;
    pts.push_back({xn + r * std::cos(a), yn + r * std::sin(a)});
  }
  for (int i = 0; i <= n; i++) {
    const double x = xn + ((to - xn) * i) / n;
    pts.push_back({x, camber(x, f) - halfThick(x, f.t)});
  }
  return {pts, {xn, yn}};
}

/** A thin strip of a section between `from` and `to`, flat at both ends (a trim tab). */
std::vector<P2> strip(double from, double to, int n, const Profile& f) {
  std::vector<P2> pts;
  for (int i = n; i >= 0; i--) {
    const double x = from + ((to - from) * i) / n;
    pts.push_back({x, camber(x, f) + halfThick(x, f.t)});
  }
  for (int i = 0; i <= n; i++) {
    const double x = from + ((to - from) * i) / n;
    pts.push_back({x, camber(x, f) - halfThick(x, f.t)});
  }
  return pts;
}

/** A section scaled about its thickest point – for the rounded tips. */
std::vector<P2> shrink(const std::vector<P2>& sec, double kc, double kt, const Profile& f) {
  const double yc = camber(0.35, f);
  std::vector<P2> out;
  for (const auto& [c, y] : sec) out.push_back({0.35 + (c - 0.35) * kc, yc + (y - yc) * kt});
  return out;
}

// ------------------------------------------------------------------------------- the fuselage
/**
 * Cross-sections along the fuselage: z, the top, the bottom, the height of
 * its widest line, its half width there, how square it is above and below
 * (2 = an ellipse), and how much narrower it gets towards the top.
 */
const std::vector<std::vector<double>> STATIONS = {
    {-2.36, 0.3, -0.29, 0.02, 0.3, 2.4, 2.4, 0},
    {-2.22, 0.33, -0.36, 0.03, 0.4, 2.6, 2.6, 0},
    {-2.0, 0.36, -0.42, 0.04, 0.47, 2.9, 2.8, 0},
    {-1.75, 0.385, -0.46, 0.06, 0.51, 3.2, 3.0, 0},
    {-1.55, 0.4, -0.48, 0.08, 0.53, 3.4, 3.2, 0.01},
    {-1.28, 0.43, -0.5, 0.11, 0.55, 3.0, 3.4, 0.03},
    {-1.1, 0.62, -0.51, 0.13, 0.56, 3.0, 3.5, 0.04},
    {-0.9, 0.79, -0.52, 0.15, 0.565, 3.4, 3.5, 0.05},
    {-0.7, 0.93, -0.52, 0.15, 0.565, 4.0, 3.5, 0.06},
    {-0.58, 0.985, -0.52, 0.15, 0.565, 4.6, 3.5, 0.06},
    {-0.4, 1.0, -0.52, 0.15, 0.565, 5.0, 3.5, 0.06},
    {0.6, 1.0, -0.51, 0.16, 0.565, 5.0, 3.5, 0.06},
    {0.95, 1.0, -0.48, 0.17, 0.55, 4.6, 3.4, 0.06},
    {1.2, 0.97, -0.44, 0.19, 0.52, 3.8, 3.2, 0.05},
    {1.6, 0.88, -0.36, 0.22, 0.47, 3.0, 3.0, 0.03},
    {2.2, 0.72, -0.24, 0.24, 0.39, 2.6, 2.7, 0.02},
    {3.0, 0.57, -0.1, 0.25, 0.29, 2.4, 2.5, 0},
    {3.8, 0.48, 0.01, 0.26, 0.2, 2.3, 2.4, 0},
    {4.5, 0.43, 0.08, 0.27, 0.13, 2.2, 2.3, 0},
    {5.12, 0.4, 0.16, 0.28, 0.05, 2.2, 2.2, 0},
};

struct Section {
  double top;
  double bot;
  double wide;
  double w;
  double nt;
  double nb;
  double k;
};

/** STEPS points along each half of a quarter of a cross-section */
const int STEPS = 14;

/**
 * A quarter of a superellipse a^n + b^n = 1, from (0, 1) to (1, 0) – sampled
 * along a where the curve runs across and along b where it runs up, so that
 * the points stay evenly spread even where it turns sharply (n > 2).
 */
std::vector<P2> quadrant(double n) {
  const double k = std::pow(2, -1 / n);
  std::vector<P2> pts;
  for (int i = 0; i <= STEPS; i++) {
    const double a = (k * i) / STEPS;
    pts.push_back({a, std::pow(1 - std::pow(a, n), 1 / n)});
  }
  for (int i = STEPS - 1; i >= 0; i--) {
    const double b = (k * i) / STEPS;
    pts.push_back({std::pow(1 - std::pow(b, n), 1 / n), b});
  }
  return pts;
}

/** A cross-section as a closed outline: x, y pairs from the bottom middle round by the right side, and how far round each is (0 … 1). */
struct Ring {
  std::vector<double> pts;
  std::vector<double> at;
  double length;
};

/**
 * The fuselage as a surface over (u, z): u runs once round a cross-section,
 * by its length – 0 at the bottom, 0.5 at the top, the right side first.
 */
class Fuselage {
 public:
  double z0;
  double z1;

  explicit Fuselage(const std::vector<std::vector<double>>& rows) {
    std::vector<double> zs;
    for (const auto& r : rows) zs.push_back(r[0]);
    for (int i = 1; i <= 7; i++) {
      std::vector<double> ys;
      for (const auto& r : rows) ys.push_back(r[i]);
      curves_.push_back(pchip(zs, ys));
    }
    z0 = zs[0];
    z1 = zs[zs.size() - 1];
  }

  Section section(double z) const {
    const auto& c = curves_;
    return {c[0](z), c[1](z), c[2](z), c[3](z), c[4](z), c[5](z), c[6](z)};
  }

  /** The cross-section at z: bottom right, top right, top left, bottom left quarter. */
  const Ring& ring(double z) {
    const long long key = static_cast<long long>(jsRound(z * 1e5));
    const auto have = rings_.find(key);
    if (have != rings_.end()) return have->second;
    const Section s = section(z);
    std::vector<P2> below;
    for (const auto& [a, b] : quadrant(s.nb)) below.push_back({s.w * a, s.wide - (s.wide - s.bot) * b});
    std::vector<P2> above;
    for (const auto& [a, b] : quadrant(s.nt)) above.push_back({s.w * a * (1 - s.k * std::pow(b, s.nt / 2)), s.wide + (s.top - s.wide) * b});
    std::reverse(above.begin(), above.end());
    std::vector<P2> right = below;
    right.insert(right.end(), above.begin() + 1, above.end());
    std::vector<P2> all = right;
    for (int i = static_cast<int>(right.size()) - 2; i >= 0; i--) all.push_back({-right[i][0], right[i][1]});
    Ring r;
    r.pts.assign(all.size() * 2, 0);
    r.at.assign(all.size(), 0);
    for (size_t i = 0; i < all.size(); i++) {
      r.pts[i * 2] = all[i][0];
      r.pts[i * 2 + 1] = all[i][1];
      if (i) r.at[i] = r.at[i - 1] + std::hypot(all[i][0] - all[i - 1][0], all[i][1] - all[i - 1][1]);
    }
    r.length = r.at[all.size() - 1];
    for (double& a : r.at) a /= r.length;
    return rings_.emplace(key, std::move(r)).first->second;
  }

  Vec3 point(double u, double z) {
    const Ring& rg = ring(z);
    const auto& pts = rg.pts;
    const auto& at = rg.at;
    const double v = std::min(1.0, std::max(0.0, u));
    size_t lo = 0;
    size_t hi = at.size() - 1;
    while (hi - lo > 1) {
      const size_t m = (lo + hi) >> 1;
      if (at[m] <= v) lo = m;
      else hi = m;
    }
    const double t = (v - at[lo]) / std::max(1e-12, at[hi] - at[lo]);
    return {pts[lo * 2] + (pts[hi * 2] - pts[lo * 2]) * t, pts[lo * 2 + 1] + (pts[hi * 2 + 1] - pts[lo * 2 + 1]) * t, z};
  }

  double perimeter(double z) { return ring(z).length; }

  /** u of the point at height y on the right side. */
  double uAtY(double z, double y) { return find(z, 0, 4 * STEPS, 1, y); }

  /** u of the point at x on the upper half. */
  double uAtX(double z, double x) { return find(z, 2 * STEPS, 6 * STEPS, 0, x); }

  /** Half the width at height y (the inner side of the skin, for the cabin). */
  double wallX(double z, double y) { return point(uAtY(z, y), z)[0]; }

  /** Height of the skin's upper half above x. */
  double roofY(double z, double x) { return point(uAtX(z, x), z)[1]; }

 private:
  std::vector<std::function<double(double)>> curves_;
  std::unordered_map<long long, Ring> rings_;

  /** u where the outline from `from` to `to` (indices, one way round) first reaches value v of coordinate c (it runs monotonously there). */
  double find(double z, int from, int to, int c, double v) {
    const Ring& rg = ring(z);
    const auto& pts = rg.pts;
    const auto& at = rg.at;
    const double sign = pts[to * 2 + c] > pts[from * 2 + c] ? 1 : -1;
    if (sign * (v - pts[from * 2 + c]) <= 0) return at[from];
    if (sign * (v - pts[to * 2 + c]) >= 0) return at[to];
    int lo = from;
    int hi = to;
    while (hi - lo > 1) {
      const int m = (lo + hi) >> 1;
      if (sign * (pts[m * 2 + c] - v) <= 0) lo = m;
      else hi = m;
    }
    const double t = (v - pts[lo * 2 + c]) / (pts[hi * 2 + c] - pts[lo * 2 + c]);
    return at[lo] + (at[hi] - at[lo]) * t;
  }
};

// ------------------------------------------------------------- the skin and its windows
enum Skin { SkinPaint, SkinStripe, SkinAccent, SkinSeam, SkinSeal, SkinGlass };
const int SKIN_COUNT = 6;

/** A part of the skin – a window, a stripe – as a convex polygon over (u, z), counter-clockwise. */
struct Region {
  Skin skin;
  std::vector<P2> poly;
  double u0;
  double u1;
  double z0;
  double z1;
};

double area2(const std::vector<P2>& p) {
  double s = 0;
  for (size_t i = 0; i < p.size(); i++) {
    const P2& a = p[i];
    const P2& b = p[(i + 1) % p.size()];
    s += a[0] * b[1] - b[0] * a[1];
  }
  return s / 2;
}

double side2(const P2& a, const P2& b, const P2& p) { return (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]); }

/** Convex hull, counter-clockwise (monotone chain). */
std::vector<P2> hull(const std::vector<P2>& points) {
  std::vector<P2> p = points;
  std::stable_sort(p.begin(), p.end(), [](const P2& a, const P2& b) {
    const double d = a[0] - b[0];
    return (d ? d : a[1] - b[1]) < 0;
  });
  std::vector<P2> lower;
  for (const P2& q : p) {
    while (lower.size() >= 2 && side2(lower[lower.size() - 2], lower[lower.size() - 1], q) <= 0) lower.pop_back();
    lower.push_back(q);
  }
  std::vector<P2> upper;
  for (int i = static_cast<int>(p.size()) - 1; i >= 0; i--) {
    const P2& q = p[i];
    while (upper.size() >= 2 && side2(upper[upper.size() - 2], upper[upper.size() - 1], q) <= 0) upper.pop_back();
    upper.push_back(q);
  }
  std::vector<P2> out(lower.begin(), lower.end() - 1);
  out.insert(out.end(), upper.begin(), upper.end() - 1);
  return out;
}

Region region(Skin skin, const std::vector<P2>& pts) {
  std::vector<P2> poly = pts;
  if (area2(poly) < 0) std::reverse(poly.begin(), poly.end());
  // (a polygon that came out slightly hollow after mapping into (u, z): its hull)
  for (size_t i = 0; i < poly.size(); i++) {
    if (side2(poly[i], poly[(i + 1) % poly.size()], poly[(i + 2) % poly.size()]) < -1e-12) {
      poly = hull(poly);
      break;
    }
  }
  Region r{skin, poly, JS_INF, -JS_INF, JS_INF, -JS_INF};
  for (const P2& p : poly) {
    r.u0 = std::min(r.u0, p[0]);
    r.u1 = std::max(r.u1, p[0]);
    r.z0 = std::min(r.z0, p[1]);
    r.z1 = std::max(r.z1, p[1]);
  }
  return r;
}

/** The part of `poly` on the left of a→b (sign 1) or on its right (sign −1): Sutherland–Hodgman. */
std::vector<P2> clipHalf(const std::vector<P2>& poly, const P2& a, const P2& b, double sign) {
  std::vector<P2> out;
  const size_t n = poly.size();
  P2 prev = poly[n - 1];
  double sp = sign * side2(a, b, prev);
  for (size_t i = 0; i < n; i++) {
    const P2 cur = poly[i];
    const double sc = sign * side2(a, b, cur);
    if ((sc >= 0 && sp < 0) || (sc < 0 && sp > 0)) {
      const double t = sp / (sp - sc);
      out.push_back({prev[0] + (cur[0] - prev[0]) * t, prev[1] + (cur[1] - prev[1]) * t});
    }
    if (sc >= 0) out.push_back(cur);
    prev = cur;
    sp = sc;
  }
  return out;
}

/** A convex polygon over (u, z) grown by d metres all round (u measured along the skin). */
std::vector<P2> grow(const std::vector<P2>& poly, double d, double perimeter) {
  std::vector<P2> q;
  for (const auto& [u, z] : poly) q.push_back({u * perimeter, z});
  const size_t n = q.size();
  struct L {
    P2 a;
    P2 dir;
  };
  std::vector<L> lines;
  for (size_t i = 0; i < n; i++) {
    const P2& a = q[i];
    const P2& b = q[(i + 1) % n];
    double l = std::hypot(b[0] - a[0], b[1] - a[1]);
    if (!l) l = 1;
    const double nx = (b[1] - a[1]) / l;
    const double ny = -(b[0] - a[0]) / l;
    lines.push_back({{a[0] + nx * d, a[1] + ny * d}, {(b[0] - a[0]) / l, (b[1] - a[1]) / l}});
  }
  std::vector<P2> out;
  for (size_t i = 0; i < n; i++) {
    const L& L0 = lines[(i + n - 1) % n];
    const L& L1 = lines[i];
    const double den = L0.dir[0] * L1.dir[1] - L0.dir[1] * L1.dir[0];
    if (std::fabs(den) < 1e-9) {
      out.push_back(L1.a);
      continue;
    }
    const double t = ((L1.a[0] - L0.a[0]) * L1.dir[1] - (L1.a[1] - L0.a[1]) * L1.dir[0]) / den;
    out.push_back({L0.a[0] + L0.dir[0] * t, L0.a[1] + L0.dir[1] * t});
  }
  for (P2& p : out) p[0] /= perimeter;
  return out;
}

/** A narrow band along the segment a→b over (u, z), `width` metres wide on the skin. */
std::vector<P2> line(const P2& a, const P2& b, double width, double perimeter) {
  const double ax = a[0] * perimeter;
  const double bx = b[0] * perimeter;
  double l = std::hypot(bx - ax, b[1] - a[1]);
  if (!l) l = 1;
  const double dx = (bx - ax) / l;
  const double dz = (b[1] - a[1]) / l;
  const double h = width / 2;
  std::vector<P2> pts = {
      {ax - dx * h - dz * h, a[1] - dz * h + dx * h},
      {bx + dx * h - dz * h, b[1] + dz * h + dx * h},
      {bx + dx * h + dz * h, b[1] + dz * h - dx * h},
      {ax - dx * h + dz * h, a[1] - dz * h - dx * h},
  };
  for (P2& p : pts) p[0] /= perimeter;
  return pts;
}

/** The heights of the cheat line (and the thin line below it) along the fuselage: z, lower edge, upper edge. */
const std::vector<std::array<double, 3>> CHEAT_LINE = {
    {-2.36, -0.03, 0.05}, {-2.15, 0.0, 0.09}, {-1.85, 0.04, 0.14}, {-1.55, 0.07, 0.18}, {-1.2, 0.1, 0.21}, {-0.2, 0.12, 0.23},
    {0.8, 0.14, 0.25},    {1.6, 0.18, 0.28},  {2.4, 0.22, 0.31},   {3.3, 0.25, 0.33},   {4.2, 0.28, 0.35}, {4.62, 0.322, 0.334},
};

struct SkinRegions {
  std::vector<Region> regions;
  std::vector<Region> windows;
};

/** Where the windows, seals, door joints and stripes lie on the skin. */
SkinRegions skinRegions(Fuselage& f) {
  std::vector<Region> regions;
  std::vector<Region> windows;
  const auto mirror = [](const std::vector<P2>& pts) {
    std::vector<P2> out;
    for (const auto& [u, z] : pts) out.push_back({1 - u, z});
    return out;
  };
  const auto add = [&](Skin skin, const std::vector<P2>& pts, bool both) {
    std::vector<std::vector<P2>> list = {pts};
    if (both) list.push_back(mirror(pts));
    for (const auto& p : list) {
      const Region r = region(skin, p);
      regions.push_back(r);
      if (skin == SkinGlass) windows.push_back(r);
    }
  };
  const auto side = [&f](double z, double y) -> P2 { return {f.uAtY(z, y), z}; };
  const auto top = [&f](double z, double x) -> P2 { return {f.uAtX(z, x), z}; };
  const double P = f.perimeter(-0.4);

  // the windscreen, from the cowling up to the wing's leading edge
  const double zb = -1.28;
  const double zt = -0.62;
  // (its upper corners lie at the cabin's shoulders, its lower ones on the cowling)
  const P2 shoulder = side(zt, 0.93);
  const std::vector<P2> screen = {top(zb, 0.46), shoulder, {1 - shoulder[0], zt}, top(zb, -0.46)};
  // the doors' windows: the front edge runs along the windscreen's post, then straight down to the sill
  const P2 a0 = screen[0];
  const P2 a1 = screen[1];
  const auto postAt = [a0, a1](double z, double off) { return a0[0] + off + ((a1[0] - a0[0]) * (z - a0[1])) / (a1[1] - a0[1]); };
  /** where the line along the post (offset `off` in u) reaches the height y (or, should it not, the post's top) */
  const auto meet = [&](double y, double off) -> P2 {
    double lo = -1.3;
    double hi = -0.2;
    if (postAt(hi, off) - f.uAtY(hi, y) < 0) return {postAt(zt, off), zt};
    for (int i = 0; i < 40; i++) {
      const double m = (lo + hi) / 2;
      if (postAt(m, off) - f.uAtY(m, y) < 0) lo = m;
      else hi = m;
    }
    return {postAt(hi, off), hi};
  };
  const double sill = 0.33;
  const double head = 0.88;
  const double pillar = -0.013;
  const double doorFront = -1.1;
  const double doorRear = -0.06;
  // (the sill in short pieces: the cross-section changes fast along the door's front half)
  std::vector<P2> door;
  for (int k = 0; k <= 10; k++) door.push_back(side(doorFront + ((doorRear - doorFront) * k) / 10, sill));
  door.push_back(side(doorRear, head));
  door.push_back(meet(head, pillar));
  door.push_back({postAt(doorFront, pillar), doorFront});
  // behind the door, a window that runs out under the wing's trailing edge
  const std::vector<P2> rear = {side(0.04, sill), side(0.92, sill), side(1.3, 0.6), side(1.2, 0.82), side(0.04, head)};
  // and the window in the roof behind the wing
  const std::vector<P2> back = {top(1.0, 0.36), top(1.52, 0.25), top(1.52, -0.25), top(1.0, -0.36)};
  add(SkinGlass, screen, false);
  add(SkinGlass, door, true);
  add(SkinGlass, rear, true);
  add(SkinGlass, back, false);
  // a dark rubber seal round each window
  const std::vector<Region> glassOnes = windows;
  for (const Region& w : glassOnes) regions.push_back(region(SkinSeal, grow(w.poly, 0.014, f.perimeter((w.z0 + w.z1) / 2))));

  // the joints round the doors and the one between cowling and cabin
  const std::vector<P2> doorEdge = {meet(0.935, pillar + 0.006), {postAt(-1.14, pillar + 0.006), -1.14}, side(-1.14, -0.36), side(0.0, -0.36), side(0.0, 0.935)};
  for (size_t i = 0; i < doorEdge.size(); i++) add(SkinSeam, line(doorEdge[i], doorEdge[(i + 1) % doorEdge.size()], 0.007, P), true);
  add(SkinSeam, {{0, -1.554}, {1, -1.554}, {1, -1.547}, {0, -1.547}}, false);

  // the cheat line with a thin gold line under it – in short pieces, so that it keeps its height where the
  // cross-section changes fast (round the windscreen)
  const auto band = [](double z) -> P2 {
    size_t i = 0;
    while (i < CHEAT_LINE.size() - 2 && z > CHEAT_LINE[i + 1][0]) i++;
    const auto& [za, la, ha] = CHEAT_LINE[i];
    const auto& [zc, lc, hc] = CHEAT_LINE[i + 1];
    const double t = (z - za) / (zc - za);
    return {la + (lc - la) * t, ha + (hc - ha) * t};
  };
  std::vector<double> knots;
  for (double z = CHEAT_LINE[0][0]; z < CHEAT_LINE[CHEAT_LINE.size() - 1][0] - 1e-6; z += z > -1.7 && z < -0.3 ? 0.09 : 0.3) knots.push_back(z);
  knots.push_back(CHEAT_LINE[CHEAT_LINE.size() - 1][0]);
  for (size_t i = 0; i + 1 < knots.size(); i++) {
    const double za = knots[i];
    const double zc = knots[i + 1];
    const auto [la, ha] = band(za);
    const auto [lc, hc] = band(zc);
    add(SkinAccent, {side(za, la - 0.035), side(zc, lc - 0.035), side(zc, lc - 0.02), side(za, la - 0.02)}, true);
    add(SkinStripe, {side(za, la), side(zc, lc), side(zc, hc), side(za, ha)}, true);
  }
  return {regions, windows};
}

/** The cabin, where the skin is lined inside. */
const double LINING_Z0 = -1.57;
const double LINING_Z1 = 1.62;

struct SkinGeometry {
  std::array<GeometryPtr, SKIN_COUNT> parts;
  GeometryPtr lining;
  GeometryPtr reveals;
};

struct Tri {
  std::vector<double> pos;
  std::vector<double> nor;
};

GeometryPtr triGeometry(const Tri& o) {
  GeometryPtr g = make<BufferGeometry>();
  g->setAttribute("position", attr(std::vector<float>(o.pos.begin(), o.pos.end()), 3));
  g->setAttribute("normal", attr(std::vector<float>(o.nor.begin(), o.nor.end()), 3));
  return g;
}

/**
 * The fuselage's skin: a grid over (u, z) whose cells are cut by the
 * regions.  The surface is evaluated exactly at the grid's corners; inside a
 * cell the corners of the pieces lie between them (bilinear) – so the pieces
 * of a cell and of its neighbours meet without cracks, and the windows' edges
 * run exactly where they are drawn, however coarse the grid.
 */
SkinGeometry buildSkin(Fuselage& f) {
  const SkinRegions sr = skinRegions(f);
  const std::vector<Region>& regions = sr.regions;
  const std::vector<Region>& windows = sr.windows;
  struct V {
    Vec3 p;
    Vec3 n;
  };
  // the grid: fine over the cabin, coarser on the tail cone
  const int NU = 64;
  std::vector<double> raw;
  for (double z = f.z0; z < f.z1 - 1e-6; z += z < -1.6 ? 0.09 : z < 1.7 ? 0.075 : 0.16) raw.push_back(z);
  raw.push_back(f.z1);
  raw.push_back(LINING_Z0);
  raw.push_back(LINING_Z1);
  std::stable_sort(raw.begin(), raw.end());
  std::vector<double> zs;
  for (size_t i = 0; i < raw.size(); i++)
    if (i == 0 || raw[i] - raw[i - 1] > 1e-4) zs.push_back(raw[i]);
  std::vector<std::vector<Vec3>> points;
  for (const double z : zs) {
    std::vector<Vec3> row;
    for (int j = 0; j <= NU; j++) row.push_back(f.point(static_cast<double>(j) / NU, z));
    points.push_back(row);
  }
  // normals from the neighbours round and along
  std::vector<std::vector<V>> lattice;
  for (size_t i = 0; i < points.size(); i++) {
    const auto& row = points[i];
    std::vector<V> out;
    for (int j = 0; j <= NU; j++) {
      const Vec3& p = row[j];
      const int k = j % NU;
      const Vec3 tu = sub3(row[(k + 1) % NU], row[(k + NU - 1) % NU]);
      const Vec3 tz = sub3(points[std::min(points.size() - 1, i + 1)][j], points[i > 0 ? i - 1 : 0][j]);
      Vec3 n = unit3(cross3(tu, tz));
      const double wide = f.section(p[2]).wide;
      if (n[0] * p[0] + n[1] * (p[1] - wide) < 0) n = {-n[0], -n[1], -n[2]};
      out.push_back({p, n});
    }
    lattice.push_back(out);
  }
  std::unordered_map<double, V> cache;
  const auto vertex = [&](double u, double z, size_t i, int j) -> V {
    const double key = jsRound(u * 1e6) * 4e6 + jsRound((z + 10) * 1e5);
    const auto have = cache.find(key);
    if (have != cache.end()) return have->second;
    const double s = std::min(1.0, std::max(0.0, u * NU - j));
    const double t = std::min(1.0, std::max(0.0, (z - zs[i]) / (zs[i + 1] - zs[i])));
    const V& a = lattice[i][j];
    const V& b = lattice[i][j + 1];
    const V& c = lattice[i + 1][j];
    const V& d = lattice[i + 1][j + 1];
    const auto mixP = [&](int m) { return (a.p[m] * (1 - s) + b.p[m] * s) * (1 - t) + (c.p[m] * (1 - s) + d.p[m] * s) * t; };
    const auto mixN = [&](int m) { return (a.n[m] * (1 - s) + b.n[m] * s) * (1 - t) + (c.n[m] * (1 - s) + d.n[m] * s) * t; };
    const V v = {{mixP(0), mixP(1), mixP(2)}, unit3({mixN(0), mixN(1), mixN(2)})};
    cache.emplace(key, v);
    return v;
  };
  /** the cell a point of the skin lies in */
  const auto cell = [&](double u, double z) -> std::pair<size_t, int> {
    int lo = 0;
    int hi = static_cast<int>(zs.size()) - 2;
    while (lo < hi) {
      const int m = (lo + hi + 1) >> 1;
      if (zs[m] <= z) lo = m;
      else hi = m - 1;
    }
    return {static_cast<size_t>(lo), std::min(NU - 1, std::max(0, static_cast<int>(std::floor(u * NU))))};
  };

  std::array<Tri, SKIN_COUNT> out;
  Tri lining;
  const auto push = [](Tri& o, const V& a, const V& b, const V& c) {
    o.pos.insert(o.pos.end(), {a.p[0], a.p[1], a.p[2], b.p[0], b.p[1], b.p[2], c.p[0], c.p[1], c.p[2]});
    o.nor.insert(o.nor.end(), {a.n[0], a.n[1], a.n[2], b.n[0], b.n[1], b.n[2], c.n[0], c.n[1], c.n[2]});
  };
  const auto triangle = [&](Skin skin, const V& a, V b, V c, bool lined) {
    const Vec3 n = cross3(sub3(b.p, a.p), sub3(c.p, a.p));
    if (dot3(n, n) < 1e-18) return;
    // wound so that the face looks outwards
    if (dot3(n, {a.n[0] + b.n[0] + c.n[0], a.n[1] + b.n[1] + c.n[1], a.n[2] + b.n[2] + c.n[2]}) < 0) std::swap(b, c);
    push(out[skin], a, b, c);
    if (lined) push(lining, a, b, c);
  };
  const auto emit = [&](Skin skin, const std::vector<P2>& poly, size_t i, int j) {
    if (poly.size() < 3 || std::fabs(area2(poly)) < 1e-11) return;
    std::vector<V> vs;
    double zc = 0;
    for (const auto& [u, z] : poly) {
      vs.push_back(vertex(u, z, i, j));
      zc += z;
    }
    zc /= poly.size();
    const bool lined = skin != SkinGlass && zc > LINING_Z0 && zc < LINING_Z1;
    for (size_t k = 1; k + 1 < vs.size(); k++) triangle(skin, vs[0], vs[k], vs[k + 1], lined);
  };
  for (size_t i = 0; i + 1 < zs.size(); i++) {
    const bool lined = (zs[i] + zs[i + 1]) / 2 > LINING_Z0 && (zs[i] + zs[i + 1]) / 2 < LINING_Z1;
    std::vector<const Region*> row;
    for (const Region& r : regions)
      if (r.z1 > zs[i] && r.z0 < zs[i + 1]) row.push_back(&r);
    for (int j = 0; j < NU; j++) {
      const double u0 = static_cast<double>(j) / NU;
      const double u1 = static_cast<double>(j + 1) / NU;
      // most cells are plain paint: straight from the lattice
      if (!std::any_of(row.begin(), row.end(), [&](const Region* r) { return r->u1 > u0 && r->u0 < u1; })) {
        const V& a = lattice[i][j];
        const V& c = lattice[i + 1][j + 1];
        triangle(SkinPaint, a, lattice[i][j + 1], c, lined);
        triangle(SkinPaint, a, c, lattice[i + 1][j], lined);
        continue;
      }
      std::vector<std::vector<P2>> pieces = {{{u0, zs[i]}, {u1, zs[i]}, {u1, zs[i + 1]}, {u0, zs[i + 1]}}};
      for (const Region* r : row) {
        if (r->u1 <= u0 || r->u0 >= u1) continue;
        std::vector<std::vector<P2>> next;
        const size_t n = r->poly.size();
        for (const auto& piece : pieces) {
          std::vector<P2> in = piece;
          for (size_t k = 0; k < n && in.size() >= 3; k++) in = clipHalf(in, r->poly[k], r->poly[(k + 1) % n], 1);
          if (in.size() >= 3) emit(r->skin, in, i, j);
          // what is left, in convex pieces: outside the first edge, then inside it and outside the next …
          std::vector<P2> rest = piece;
          for (size_t k = 0; k < n && rest.size() >= 3; k++) {
            const std::vector<P2> outside = clipHalf(rest, r->poly[k], r->poly[(k + 1) % n], -1);
            if (outside.size() >= 3 && std::fabs(area2(outside)) > 1e-11) next.push_back(outside);
            rest = clipHalf(rest, r->poly[k], r->poly[(k + 1) % n], 1);
          }
        }
        pieces = next;
        if (pieces.empty()) break;
      }
      for (const auto& piece : pieces) emit(SkinPaint, piece, i, j);
    }
  }
  SkinGeometry result;
  for (int k = 0; k < SKIN_COUNT; k++) result.parts[k] = triGeometry(out[k]);

  // the window openings' edges, seen from inside: the skin's thickness and the frame (flat faces)
  Tri reveal;
  const auto face = [&](const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
    const Vec3 n = unit3(cross3(sub3(b, a), sub3(d, a)));
    push(reveal, {a, n}, {b, n}, {c, n});
    push(reveal, {a, n}, {c, n}, {d, n});
  };
  for (const Region& w : windows) {
    const double P = f.perimeter((w.z0 + w.z1) / 2);
    std::vector<V> ring;
    for (size_t i = 0; i < w.poly.size(); i++) {
      const P2& a = w.poly[i];
      const P2& b = w.poly[(i + 1) % w.poly.size()];
      const int k = std::max(1, static_cast<int>(std::ceil(std::hypot((b[0] - a[0]) * P, b[1] - a[1]) / 0.05)));
      for (int s = 0; s < k; s++) {
        const double u = a[0] + ((b[0] - a[0]) * s) / k;
        const double z = a[1] + ((b[1] - a[1]) * s) / k;
        const auto [ci, cj] = cell(u, z);
        ring.push_back(vertex(u, z, ci, cj));
      }
    }
    Vec3 centre = {0, 0, 0};
    for (const V& v : ring)
      for (int k = 0; k < 3; k++) centre[k] += v.p[k] / ring.size();
    const double depth = 0.03;
    for (size_t i = 0; i < ring.size(); i++) {
      const V& a = ring[i];
      const V& b = ring[(i + 1) % ring.size()];
      const Vec3 qa = {a.p[0] - a.n[0] * depth, a.p[1] - a.n[1] * depth, a.p[2] - a.n[2] * depth};
      const Vec3 qb = {b.p[0] - b.n[0] * depth, b.p[1] - b.n[1] * depth, b.p[2] - b.n[2] * depth};
      const Vec3 n = cross3(sub3(b.p, a.p), sub3(qa, a.p));
      if (dot3(n, sub3(centre, a.p)) >= 0) face(a.p, b.p, qb, qa);
      else face(a.p, qa, qb, b.p);
    }
  }
  result.lining = triGeometry(lining);
  result.reveals = triGeometry(reveal);
  return result;
}

// --------------------------------------------------------------------------------- the wing
const double CHORD = T.chord;
const double LE = T.wingLE;
const double DIH = T.dihedral * DEG;
/** the wing's halves begin, and with them the dihedral, inside the cabin's sides */
const double ROOT = 0.5;
/** where the flaps and ailerons begin (parts of the chord) */
const double HINGE = 0.72;
/** the height of the chord line: the lowest point of the lower surface at the root is at TRAINER.wingY */
const double Y0 = [] {
  double low = 0;
  for (int i = 0; i <= 400; i++) low = std::min(low, camber(i / 400.0, WING) - halfThick(i / 400.0, WING.t));
  return T.wingY - low * CHORD;
}();
/** span (along the half wing) of its tip, of the flaps and of the ailerons */
const double S_END = (T.span / 2 - ROOT) / std::cos(DIH);
const double S_TIP = S_END - 0.5;
const P2 FLAP_SPAN = {0.08, 2.38};
const P2 AILERON_SPAN = {2.44, S_TIP - 0.01};

/** The frame of one half wing: at the root, tilted by the dihedral. */
Matrix4 panelMatrix(double side) { return Matrix4().makeTranslation(side * ROOT, Y0, 0).multiply(Matrix4().makeRotationZ(side * DIH)); }

/** A section of the wing at span s (half-wing frame), [chordwise, up] in parts of the chord. */
std::vector<Vec3> wingRing(double side, double s, const std::vector<P2>& sec) {
  std::vector<Vec3> out;
  for (const auto& [c, y] : sec) out.push_back({side * s, y * CHORD, LE + c * CHORD});
  return out;
}

struct HalfWing {
  GeometryPtr fixed;
  GeometryPtr flap;
  Vec3 flapPivot;
  GeometryPtr aileron;
  Vec3 aileronHinge;
  /** points in the half wing's frame: the strut's upper end, lamps */
  Vec3 strut;
};

GeometryPtr tube35(const std::vector<std::vector<Vec3>>& rings, bool a, bool b) { return Sculpt().tube(rings, a, b).build(35); }

HalfWing halfWing(double side) {
  const std::vector<P2> main = foil(HINGE, 14, WING);
  const std::vector<P2> full = foil(1, 18, WING);
  const AftSection tail = aft(HINGE, 1, 7, WING);
  std::vector<GeometryPtr> fixed;
  fixed.push_back(tube35({wingRing(side, 0, main), wingRing(side, S_TIP, main)}, false, false));
  // the trailing edge between the cabin and the flap
  fixed.push_back(tube35({wingRing(side, 0, tail.ring), wingRing(side, FLAP_SPAN[0] - 0.005, tail.ring)}, false, true));
  // the tip: the whole section, rounded off
  const std::vector<std::array<double, 3>> tip = {
      {S_TIP, 1, 1}, {S_END - 0.16, 1, 1}, {S_END - 0.09, 0.97, 0.86}, {S_END - 0.05, 0.9, 0.64}, {S_END - 0.02, 0.76, 0.38}, {S_END, 0.52, 0.1},
  };
  std::vector<std::vector<Vec3>> tipRings;
  for (const auto& [s, kc, kt] : tip) tipRings.push_back(wingRing(side, s, shrink(full, kc, kt, WING)));
  fixed.push_back(tube35(tipRings, true, true));
  // filler caps on the top of the wing
  const auto capAt = [side](double s, double c) -> Vec3 { return {side * s, (camber(c, WING) + halfThick(c, WING.t)) * CHORD, LE + c * CHORD}; };
  for (const double s : {0.55}) {
    const Vec3 p = capAt(s, 0.28);
    GeometryPtr g = cylinderGeometry(0.035, 0.035, 0.02, 12);
    g->translate(p[0], p[1] + 0.004, p[2]);
    fixed.push_back(bare(g));
  }
  const std::vector<P2>& flapRing = tail.ring;
  GeometryPtr flap = tube35({wingRing(side, FLAP_SPAN[0], flapRing), wingRing(side, FLAP_SPAN[1], flapRing)}, true, true);
  // the flap turns about a pivot below and ahead of it, and so moves aft as it goes down (a slotted flap)
  const double pc = 0.66;
  const Vec3 flapPivot = {0, (camber(pc, WING) - halfThick(pc, WING.t) - 0.05) * CHORD, LE + pc * CHORD};
  flap->translate(-flapPivot[0], -flapPivot[1], -flapPivot[2]);
  GeometryPtr aileron = tube35({wingRing(side, AILERON_SPAN[0], tail.ring), wingRing(side, AILERON_SPAN[1], tail.ring)}, true, true);
  const Vec3 aileronHinge = {0, tail.hinge[1] * CHORD, LE + tail.hinge[0] * CHORD};
  aileron->translate(-aileronHinge[0], -aileronHinge[1], -aileronHinge[2]);
  const double sc = 0.27;
  const Vec3 strut = {side * 2.06, (camber(sc, WING) - halfThick(sc, WING.t)) * CHORD + 0.01, LE + sc * CHORD};
  return {merge(fixed), flap, flapPivot, aileron, aileronHinge, strut};
}

/** The wing's middle, over the cabin: the whole section, straight. */
GeometryPtr centreWing() {
  const std::vector<P2> full = foil(1, 18, WING);
  const auto ring = [&full](double x) {
    std::vector<Vec3> out;
    for (const auto& [c, y] : full) out.push_back({x, Y0 + y * CHORD, LE + c * CHORD});
    return out;
  };
  return Sculpt().tube({ring(-ROOT - 0.002), ring(ROOT + 0.002)}, false, false).build(35);
}

/** A streamlined strut from a to b (its long axis along the flight). */
GeometryPtr strutGeometry(const Vec3& a, const Vec3& b, double chord, double thick) {
  const Vec3 d = unit3(sub3(b, a));
  const Vec3 e1 = unit3(sub3({0, 0, 1}, {d[0] * d[2], d[1] * d[2], d[2] * d[2]}));
  const Vec3 e2 = cross3(d, e1);
  const auto ring = [&](const Vec3& p, double k) {
    std::vector<Vec3> pts;
    for (int i = 0; i < 12; i++) {
      const double an = (i / 12.0) * PI * 2;
      // a teardrop: rounder in front, pointed behind
      const double c = std::cos(an);
      const double s = std::sin(an) * (c < 0 ? 1 : 1 - 0.55 * c);
      pts.push_back({p[0] + (e1[0] * c * chord + e2[0] * s * thick) * k, p[1] + (e1[1] * c * chord + e2[1] * s * thick) * k,
                     p[2] + (e1[2] * c * chord + e2[2] * s * thick) * k});
    }
    return pts;
  };
  const Vec3 mid = {(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2};
  return Sculpt().tube({ring(a, 1), ring(mid, 1), ring(b, 1)}, true, true).build(70);
}

// --------------------------------------------------------------------------------- the tail
const double CS = T.stabChord;
const double STAB_LE = T.stabZ - 0.25 * CS;
const double STAB_HINGE = 0.62;
/** the rudder's hinge line (vertical) and the fin's outline */
const double RUDDER_Z = 5.09;
double finLE(double y) {
  const double main = 4.12 + ((y - 0.42) * (4.8 - 4.12)) / (T.finTop - 0.42);
  // the dorsal fin sweeps forwards along the top of the tail cone
  const double d = std::max(0.0, 1 - (y - 0.36) / 0.34);
  return main - 0.8 * d * d;
}
double finTE(double y) { return y < 1.6 ? 5.46 + (y - 0.2) * 0.025 : 5.495 - std::pow((y - 1.6) / 0.18, 2) * 0.14; }
/** the fin's thickness: 10 % of its chord, but no fatter than 8.4 cm (the long dorsal part stays thin) */
double finScale(double chord) { return std::min(1.0, 0.042 / (0.05 * chord)); }
double finHalf(double y, double z) {
  const double le = finLE(y);
  const double chord = finTE(y) - le;
  return halfThick((z - le) / chord, TAIL.t) * chord * finScale(chord);
}

std::vector<Vec3> stabRing(double x, const std::vector<P2>& sec) {
  std::vector<Vec3> out;
  for (const auto& [c, y] : sec) out.push_back({x, T.stabY + y * CS, STAB_LE + c * CS});
  return out;
}

struct Stabiliser {
  GeometryPtr fixed;
  GeometryPtr elevator;
  Vec3 hinge;
};

/** The horizontal tail: the fixed stabiliser, and the elevator (both halves and the trim tab) about its hinge. */
Stabiliser stabiliser() {
  const double half = T.stabSpan / 2;
  const double tipFrom = half - 0.12;
  std::vector<GeometryPtr> fixed = {tube35({stabRing(-tipFrom, foil(STAB_HINGE, 12, TAIL)), stabRing(tipFrom, foil(STAB_HINGE, 12, TAIL))}, false, false)};
  const std::vector<P2> full = foil(1, 14, TAIL);
  for (const double side : {-1.0, 1.0}) {
    const std::vector<std::array<double, 3>> tip = {{tipFrom, 1, 1}, {half - 0.06, 0.95, 0.8}, {half - 0.025, 0.85, 0.55}, {half, 0.62, 0.15}};
    std::vector<std::vector<Vec3>> rings;
    for (const auto& [x, kc, kt] : tip) rings.push_back(stabRing(side * x, shrink(full, kc, kt, TAIL)));
    fixed.push_back(tube35(rings, true, true));
  }
  const AftSection el = aft(STAB_HINGE, 1, 6, TAIL);
  const AftSection cut = aft(STAB_HINGE, 0.9, 5, TAIL);
  std::vector<GeometryPtr> parts;
  const double inner = 0.2;
  const double outer = tipFrom - 0.005;
  parts.push_back(tube35({stabRing(-outer, el.ring), stabRing(-inner, el.ring)}, true, true));
  // the right half carries the trim tab in a notch of its trailing edge
  parts.push_back(tube35({stabRing(inner, el.ring), stabRing(0.32, el.ring)}, true, true));
  parts.push_back(tube35({stabRing(0.32, cut.ring), stabRing(0.78, cut.ring)}, true, true));
  parts.push_back(tube35({stabRing(0.78, el.ring), stabRing(outer, el.ring)}, true, true));
  const std::vector<P2> tab = strip(0.906, 1, 2, TAIL);
  parts.push_back(tube35({stabRing(0.326, tab), stabRing(0.774, tab)}, true, true));
  const Vec3 hinge = {0, T.stabY + el.hinge[1] * CS, STAB_LE + el.hinge[0] * CS};
  GeometryPtr elevator = merge(parts);
  elevator->translate(-hinge[0], -hinge[1], -hinge[2]);
  return {merge(fixed), elevator, hinge};
}

struct FinAndRudder {
  GeometryPtr fin;
  GeometryPtr rudder;
  Vec3 hinge;
};

/** The fin (fixed) and the rudder about its hinge line. */
FinAndRudder finAndRudder() {
  const auto finRing = [](double y, double kt) {
    const double le = finLE(y);
    const double chord = finTE(std::min(y, 1.78)) - le;
    const double h = (RUDDER_Z - le) / chord;
    const double k = finScale(chord) * kt;
    const int n = 12;
    std::vector<Vec3> pts;
    std::vector<double> cs;
    for (int i = 0; i <= n; i++) cs.push_back(h * (1 - std::cos(((static_cast<double>(i) / n) * PI) / 2)));
    for (int i = n; i >= 0; i--) pts.push_back({halfThick(cs[i], TAIL.t) * chord * k, y, le + cs[i] * chord});
    for (int i = 1; i <= n; i++) pts.push_back({-halfThick(cs[i], TAIL.t) * chord * k, y, le + cs[i] * chord});
    return pts;
  };
  const std::vector<P2> heights = {{0.34, 1}, {0.4, 1}, {0.47, 1}, {0.55, 1}, {0.65, 1}, {0.8, 1}, {1.05, 1}, {1.35, 1}, {1.62, 1}, {1.74, 1}, {1.785, 0.85}, {T.finTop, 0.45}};
  std::vector<std::vector<Vec3>> finRings;
  for (const auto& [y, k] : heights) finRings.push_back(finRing(y, k));
  GeometryPtr fin = Sculpt().tube(finRings, false, true).build(35);
  const double hingeZ = RUDDER_Z + 0.018;
  const auto rudderRing = [hingeZ](double y, double kt) {
    const double le = std::max(finLE(y), 4.25);
    const double te = finTE(y);
    const double chord = te - le;
    const double k = finScale(chord) * kt;
    const double xn = (hingeZ - le) / chord;
    const double r = halfThick(xn, TAIL.t) * chord * k;
    const int n = 7;
    std::vector<Vec3> pts;
    for (int i = n; i >= 0; i--) {
      const double c = xn + ((1 - xn) * i) / n;
      pts.push_back({halfThick(c, TAIL.t) * chord * k, y, le + c * chord});
    }
    for (int j = 1; j < 6; j++) {
      const double a = (j / 6.0) * PI;
      pts.push_back({r * std::cos(a), y, hingeZ - r * std::sin(a)});
    }
    for (int i = 0; i <= n; i++) {
      const double c = xn + ((1 - xn) * i) / n;
      pts.push_back({-halfThick(c, TAIL.t) * chord * k, y, le + c * chord});
    }
    return pts;
  };
  const std::vector<P2> rh = {{0.2, 0.7}, {0.23, 1}, {0.4, 1}, {0.7, 1}, {1.0, 1}, {1.3, 1}, {1.55, 1}, {1.66, 1}, {1.73, 0.95}, {1.765, 0.8}, {1.78, 0.45}};
  const Vec3 hinge = {0, 0, hingeZ};
  std::vector<std::vector<Vec3>> rudderRings;
  for (const auto& [y, k] : rh) rudderRings.push_back(rudderRing(y, k));
  GeometryPtr rudder = Sculpt().tube(rudderRings, true, true).build(35);
  rudder->translate(0, 0, -hingeZ);
  return {fin, rudder, hinge};
}

// -------------------------------------------------------------------------- gear and propeller
/** A wheel fairing round the wheel's centre: a teardrop seen from above, the wheel showing below it. */
GeometryPtr spat(double front, double back, double hw, double top, double bottom) {
  std::vector<std::vector<Vec3>> rings;
  const int N = 14;
  for (int i = 0; i <= N; i++) {
    const double s = static_cast<double>(i) / N;
    const double z = -front + (front + back) * s;
    const double k = s < 0.34 ? std::sqrt(std::max(0.0, 1 - std::pow((0.34 - s) / 0.34, 2))) : std::pow(std::max(0.0, 1 - std::pow((s - 0.34) / 0.66, 2)), 0.75);
    const double kk = std::max(0.06, k);
    const double yTop = bottom + (top - bottom) * (0.25 + 0.75 * kk);
    const double yBot = bottom + (1 - kk) * 0.05;
    const double yc = (yTop + yBot) / 2;
    const double hh = (yTop - yBot) / 2;
    std::vector<Vec3> ring;
    for (int j = 0; j < 16; j++) {
      const double a = (j / 16.0) * PI * 2;
      const double c = std::cos(a);
      const double sn = std::sin(a);
      ring.push_back({hw * kk * jsSign(c) * std::pow(std::fabs(c), 0.8), yc + hh * jsSign(sn) * std::pow(std::fabs(sn), 0.8), z});
    }
    rings.push_back(ring);
  }
  return Sculpt().tube(rings, true, true).build(50);
}

struct WheelGeo {
  GeometryPtr tyre;
  GeometryPtr hub;
};

/** Tyre and hub of a wheel lying on the x axis, its centre at the origin. */
WheelGeo wheelGeometry(double radius, double width) {
  const double hub = radius * 0.52;
  const double w = width / 2;
  const std::vector<Vector2> profile = {
      {hub, -w * 0.9}, {radius * 0.8, -w}, {radius * 0.95, -w * 0.85}, {radius, -w * 0.45}, {radius, w * 0.45}, {radius * 0.95, w * 0.85}, {radius * 0.8, w}, {hub, w * 0.9},
  };
  GeometryPtr lathe = latheGeometry(profile, 22);
  lathe->rotateZ(-PI / 2);
  GeometryPtr tyre = bare(lathe);
  // a hub with six bolts, so that its turning shows
  GeometryPtr cyl = cylinderGeometry(hub, hub, width * 0.86, 12);
  cyl->rotateZ(PI / 2);
  std::vector<GeometryPtr> parts = {bare(cyl)};
  for (int i = 0; i < 6; i++) {
    const double a = (i / 6.0) * PI * 2;
    for (const double s : {-1.0, 1.0})
      parts.push_back(block(s * width * 0.43 - 0.006, s * width * 0.43 + 0.006, std::cos(a) * hub * 0.6 - 0.01, std::cos(a) * hub * 0.6 + 0.01,
                            std::sin(a) * hub * 0.6 - 0.01, std::sin(a) * hub * 0.6 + 0.01));
  }
  return {tyre, merge(parts)};
}

/** Where a leg's strut is fixed (its upper end) and where its lower end is with the strut extended. */
struct LegShape {
  Vec3 root;
  Vec3 foot;
  std::array<double, 3> outer;
  std::array<double, 2> inner;
};

LegShape legShape(const GearLeg& leg) {
  if (leg.steers) return {{leg.x, -0.38, leg.z - 0.03}, {leg.x, leg.y + 0.21, leg.z}, {0.26, 0.036, 0.04}, {0.34, 0.024}};
  const double s = jsSign(leg.x);
  return {{s * 0.36, -0.4, leg.z - 0.01}, {leg.x - s * 0.11, leg.y + 0.06, leg.z}, {0.66, 0.036, 0.044}, {0.36, 0.027}};
}

/** One propeller blade along +y, its leading edge towards +x (it turns clockwise as the pilot sees it). */
GeometryPtr blade(double r0, double r1) {
  const std::vector<std::array<double, 4>> table = {
      // radius, chord, thickness, pitch angle
      {0.1, 0.06, 0.045, 52}, {0.2, 0.1, 0.03, 40}, {0.35, 0.135, 0.02, 29}, {0.55, 0.14, 0.015, 21},
      {0.75, 0.125, 0.011, 16}, {0.89, 0.1, 0.009, 13.5}, {0.93, 0.085, 0.008, 13}, {0.95, 0.04, 0.006, 12},
  };
  const auto at = [&table](double r) -> std::array<double, 3> {
    size_t i = 0;
    while (i < table.size() - 2 && r > table[i + 1][0]) i++;
    const auto& a = table[i];
    const auto& b = table[i + 1];
    const double t = std::min(1.0, std::max(0.0, (r - a[0]) / (b[0] - a[0])));
    return {a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t, (a[3] + (b[3] - a[3]) * t) * DEG};
  };
  std::vector<std::vector<Vec3>> rings;
  std::vector<double> radii = {r0};
  for (const auto& row : table)
    if (row[0] > r0 + 1e-6 && row[0] < r1 - 1e-6) radii.push_back(row[0]);
  radii.push_back(r1);
  for (const double r : radii) {
    const auto [chord, thick, beta] = at(r);
    // the chord runs from the trailing edge back and to −x, to the leading edge forward and to +x
    const Vec3 cd = {std::cos(beta), 0, -std::sin(beta)};
    const Vec3 td = {std::sin(beta), 0, std::cos(beta)};
    std::vector<Vec3> ring;
    for (int i = 0; i < 10; i++) {
      const double a = (i / 10.0) * PI * 2;
      const double c = (std::cos(a) * chord) / 2;
      const double s = (std::sin(a) * thick) / 2 * (std::sin(a) > 0 ? 1.3 : 0.5);
      ring.push_back({cd[0] * c + td[0] * s, r, cd[2] * c + td[2] * s});
    }
    rings.push_back(ring);
  }
  GeometryPtr one = Sculpt().tube(rings, true, true).build(60);
  GeometryPtr two = one->clone();
  two->rotateZ(PI);
  return merge({one, two});
}

GeometryPtr spinnerGeometry() {
  const std::vector<Vector2> p = {
      {0.19, -0.085}, {0.197, -0.06}, {0.198, -0.01}, {0.188, 0.05}, {0.165, 0.11}, {0.128, 0.17}, {0.08, 0.225}, {0.032, 0.265}, {0.0, 0.28},
  };
  // (the lathe's axis is y: turned so that its tip points forward, to −z)
  GeometryPtr g = latheGeometry(p, 24);
  g->rotateX(-PI / 2);
  return bare(g);
}

// -------------------------------------------------------------------------------- textures
TexturePtr glow() {
  static TexturePtr glowTexture;
  if (glowTexture) return glowTexture;
  const Canvas c(64, 64);
  const Ctx2D x = c.getContext2D();
  const CanvasGradient g = x.createRadialGradient(32, 32, 0, 32, 32, 32);
  g.addColorStop(0, "rgba(255,255,255,1)");
  g.addColorStop(0.18, "rgba(255,255,255,0.75)");
  g.addColorStop(0.45, "rgba(255,255,255,0.18)");
  g.addColorStop(1, "rgba(255,255,255,0)");
  x.fillStyle(g);
  x.fillRect(0, 0, 64, 64);
  glowTexture = canvasTexture(c);
  return glowTexture;
}

/** The turning propeller as the eye sees it: a faint grey disc, the painted tips a brighter ring. */
TexturePtr disc() {
  static TexturePtr discTexture;
  if (discTexture) return discTexture;
  const Canvas c(256, 256);
  const Ctx2D x = c.getContext2D();
  const CanvasGradient g = x.createRadialGradient(128, 128, 0, 128, 128, 128);
  g.addColorStop(0, "rgba(40,42,46,0)");
  g.addColorStop(0.2, "rgba(40,42,46,0.3)");
  g.addColorStop(0.5, "rgba(40,42,46,0.2)");
  g.addColorStop(0.9, "rgba(40,42,46,0.12)");
  g.addColorStop(0.93, "rgba(240,200,40,0.2)");
  g.addColorStop(0.98, "rgba(240,200,40,0.17)");
  g.addColorStop(1, "rgba(240,200,40,0)");
  x.fillStyle(g);
  x.fillRect(0, 0, 256, 256);
  discTexture = canvasTexture(c);
  discTexture->srgb = true;
  return discTexture;
}

TexturePtr lettering() {
  static TexturePtr letteringTexture;
  if (letteringTexture) return letteringTexture;
  const Canvas c(512, 128);
  const Ctx2D x = c.getContext2D();
  x.font("italic 800 104px system-ui, \"Segoe UI\", Roboto, \"Noto Sans\", \"DejaVu Sans\", sans-serif");
  x.textAlign("center");
  x.textBaseline("middle");
  const double w = x.measureText("Fahrsim").width;
  if (w > 488) x.font("italic 800 " + jsStr(std::floor((104 * 488) / w)) + "px system-ui, \"Segoe UI\", Roboto, \"Noto Sans\", \"DejaVu Sans\", sans-serif");
  x.fillStyle("#1b57a3");
  x.fillText("Fahrsim", 256, 68);
  letteringTexture = canvasTexture(c);
  letteringTexture->srgb = true;
  letteringTexture->anisotropy = 8;
  return letteringTexture;
}

/** The lettering, lying on the fin's surface on both sides (each reads from nose to tail as seen from its side… or back). */
GeometryPtr letteringGeometry(double side) {
  const double y0 = 0.94;
  const double y1 = 1.1;
  const double z0 = 4.5;
  const double z1 = 5.04;
  const int nz = 14;
  const int ny = 3;
  std::vector<float> pos;
  std::vector<float> nor;
  std::vector<float> uv;
  std::vector<uint32_t> idx;
  for (int j = 0; j <= ny; j++) {
    const double y = y0 + ((y1 - y0) * j) / ny;
    for (int i = 0; i <= nz; i++) {
      const double z = z0 + ((z1 - z0) * i) / nz;
      pos.insert(pos.end(), {static_cast<float>(side * (finHalf(y, z) + 0.003)), static_cast<float>(y), static_cast<float>(z)});
      nor.insert(nor.end(), {static_cast<float>(side), 0, 0});
      // seen from the right the text runs from the tail to the nose, from the left from the nose to the tail
      uv.insert(uv.end(), {static_cast<float>(side > 0 ? 1 - static_cast<double>(i) / nz : static_cast<double>(i) / nz), static_cast<float>(static_cast<double>(j) / ny)});
    }
  }
  for (int j = 0; j < ny; j++) {
    for (int i = 0; i < nz; i++) {
      const uint32_t a = j * (nz + 1) + i;
      const uint32_t b = a + 1;
      const uint32_t c = a + nz + 1;
      const uint32_t d = c + 1;
      if (side > 0) idx.insert(idx.end(), {a, c, b, b, c, d});
      else idx.insert(idx.end(), {a, b, c, b, d, c});
    }
  }
  GeometryPtr g = make<BufferGeometry>();
  g->setAttribute("position", attr(pos, 3));
  g->setAttribute("normal", attr(nor, 3));
  g->setAttribute("uv", attr(uv, 2));
  g->setIndex(idx);
  return g;
}

// ---------------------------------------------------------------------- the shared geometry
struct Lamp {
  Vec3 at;
  unsigned colour;
};

struct Exterior {
  std::unique_ptr<Fuselage> fuselage;
  GeometryPtr paint;
  GeometryPtr stripe;
  GeometryPtr accent;
  GeometryPtr seam;
  GeometryPtr rubber;
  GeometryPtr dark;
  GeometryPtr metal;
  GeometryPtr glass;
  GeometryPtr lining;
  GeometryPtr reveals;
  std::vector<GeometryPtr> letters;
  std::vector<Matrix4> panels;
  std::vector<HalfWing> wings;
  GeometryPtr elevator;
  Vec3 elevatorHinge;
  GeometryPtr rudder;
  Vec3 rudderHinge;
  GeometryPtr blades;
  GeometryPtr tips;
  GeometryPtr spinner;
  std::vector<LegShape> legs;
  std::vector<GeometryPtr> outer;
  std::vector<GeometryPtr> inner;
  std::vector<GeometryPtr> spats;
  std::vector<GeometryPtr> tyres;
  std::vector<GeometryPtr> hubs;
  GeometryPtr fork;
  /** navigation lights (with their colours), strobes, beacon, landing and taxi light */
  std::vector<Lamp> nav;
  std::vector<Vec3> strobes;
  Vec3 beacon;
  Vec3 landing;
  Vec3 taxi;
  Vec3 tailLight;
};

Exterior buildExterior() {
  auto fp = std::make_unique<Fuselage>(STATIONS);
  Fuselage& f = *fp;
  const SkinGeometry skin = buildSkin(f);
  std::vector<GeometryPtr> paint = {skin.parts[SkinPaint]};
  std::vector<GeometryPtr> dark;
  std::vector<GeometryPtr> metal;

  // the cowling's front round the spinner, its air inlets
  const double hubY = T.propHub[1];
  std::vector<Vec3> outer;
  std::vector<Vec3> inner;
  for (int j = 0; j <= 64; j++) {
    const Vec3 p = f.point(j / 64.0, f.z0);
    const double dx = p[0];
    const double dy = p[1] - hubY;
    double l = std::hypot(dx, dy);
    if (!l) l = 1;
    outer.push_back(p);
    inner.push_back({(dx / l) * 0.206, hubY + (dy / l) * 0.206, f.z0 + 0.014});
  }
  paint.push_back(Sculpt().sheet({outer, inner}, {0, 0, -1}).build(80));
  const Vec3 front = {0, 0, -1};
  for (const double s : {-1.0, 1.0}) dark.push_back(oval({s * 0.252, 0.085, f.z0 - 0.002}, {1, 0, 0}, {0, 1, 0}, 0.036, 0.05, front));
  dark.push_back(oval({0, -0.212, f.z0 - 0.002}, {1, 0, 0}, {0, 1, 0}, 0.075, 0.03, front));
  // the end of the tail cone
  std::vector<Vec3> end;
  for (int j = 0; j < 64; j++) end.push_back(f.point(j / 64.0, f.z1));
  paint.push_back(Sculpt().fan(end, {0, 0, 1}).build());

  // the wing: its middle over the cabin, the halves with their struts
  paint.push_back(centreWing());
  const std::vector<Matrix4> panels = {panelMatrix(-1), panelMatrix(1)};
  const std::vector<HalfWing> wings = {halfWing(-1), halfWing(1)};
  const auto strutFoot = [](double s) -> Vec3 { return {s * 0.47, -0.27, 0.02}; };
  for (size_t i = 0; i < wings.size(); i++) {
    const HalfWing& w = wings[i];
    GeometryPtr fixed = w.fixed->clone();
    fixed->applyMatrix4(panels[i]);
    paint.push_back(fixed);
    Vector3 top = v3(w.strut);
    top.applyMatrix4(panels[i]);
    paint.push_back(strutGeometry(strutFoot(i ? 1 : -1), {top.x, top.y + 0.03, top.z}, 0.05, 0.02));
  }

  // the tail
  const Stabiliser stab = stabiliser();
  const FinAndRudder fin = finAndRudder();
  paint.push_back(stab.fixed);
  paint.push_back(fin.fin);

  // gear: struts fixed to the fuselage, the parts that move with the wheels built round their own origin
  std::vector<LegShape> legs;
  for (const GearLeg& g : T.gear) legs.push_back(legShape(g));
  std::vector<GeometryPtr> outerTubes;
  std::vector<GeometryPtr> innerTubes;
  for (const LegShape& l : legs) {
    GeometryPtr o = cylinderGeometry(l.outer[1], l.outer[2], l.outer[0], 12);
    o->translate(0, -l.outer[0] / 2, 0);
    outerTubes.push_back(bare(o));
    GeometryPtr n = cylinderGeometry(l.inner[1], l.inner[1], l.inner[0], 10);
    n->translate(0, l.inner[0] / 2, 0);
    innerTubes.push_back(bare(n));
  }
  std::vector<GeometryPtr> spats;
  std::vector<WheelGeo> wheels;
  for (const GearLeg& g : T.gear) {
    spats.push_back(g.steers ? spat(0.27, 0.33, 0.085, 0.2, -0.1) : spat(0.36, 0.44, 0.105, 0.23, -0.12));
    wheels.push_back(wheelGeometry(g.radius, g.steers ? 0.11 : 0.13));
  }
  // the nose wheel's fork crown on top of its fairing, and a torque link behind the strut
  GeometryPtr fork = merge({block(-0.075, 0.075, -0.025, 0.02, -0.045, 0.045), block(-0.012, 0.012, 0.0, 0.12, 0.03, 0.05)});

  // the propeller: blades with painted tips, the spinner
  GeometryPtr blades = blade(0.1, 0.89);
  GeometryPtr tips = blade(0.89, 0.95);
  GeometryPtr spinner = spinnerGeometry();

  // small things: exhaust, antennas, pitot tube, door handles
  dark.push_back(pipe({{0.17, -0.36, -1.88}, {0.18, -0.47, -1.84}, {0.19, -0.53, -1.76}}, 0.024, 10));
  dark.push_back(Sculpt()
                     .tube(
                         {
                             {{0.004, 0.8, 1.86}, {0.004, 0.8, 2.02}, {-0.004, 0.8, 2.02}, {-0.004, 0.8, 1.86}},
                             {{0.002, 1.04, 2.0}, {0.002, 1.04, 2.06}, {-0.002, 1.04, 2.06}, {-0.002, 1.04, 2.0}},
                         },
                         true, true)
                     .build(30));
  dark.push_back(pipe({{0, -0.45, 1.2}, {0, -0.62, 1.42}, {0, -0.7, 1.58}}, std::vector<double>{0.006, 0.004, 0.003}, 6));
  Vector3 pitotBase(-1.45, (camber(0.2, WING) - halfThick(0.2, WING.t)) * CHORD + 0.01, LE + 0.2 * CHORD);
  pitotBase.applyMatrix4(panels[0]);
  metal.push_back(pipe({{pitotBase.x, pitotBase.y, pitotBase.z},
                        {pitotBase.x, pitotBase.y - 0.09, pitotBase.z - 0.02},
                        {pitotBase.x, pitotBase.y - 0.1, pitotBase.z - 0.08},
                        {pitotBase.x, pitotBase.y - 0.1, LE - 0.22}},
                       std::vector<double>{0.012, 0.01, 0.008, 0.007}, 8));
  for (const double s : {-1.0, 1.0}) {
    const double x = f.wallX(-0.16, 0.26);
    dark.push_back(block(s * x - 0.012, s * x + 0.012, 0.25, 0.27, -0.24, -0.12));
  }

  // lamps: navigation lights and strobes in the wing tips, the beacon on the fin, landing and taxi light in the left wing
  const auto inPanel = [&panels](int i, const Vec3& p) -> Vec3 {
    Vector3 v = v3(p);
    v.applyMatrix4(panels[i]);
    return {v.x, v.y, v.z};
  };
  const std::vector<Lamp> nav = {
      {inPanel(0, {-(S_END - 0.03), 0.01, LE + 0.2}), 0xff2a1a},
      {inPanel(1, {S_END - 0.03, 0.01, LE + 0.2}), 0x2aff5a},
  };
  const std::vector<Vec3> strobes = {inPanel(0, {-(S_END - 0.01), 0.02, LE + 0.5}), inPanel(1, {S_END - 0.01, 0.02, LE + 0.5})};
  const Vec3 beacon = {0, T.finTop + 0.03, finLE(T.finTop) + 0.14};
  const Vec3 landing = inPanel(0, {-1.75, 0.0, LE - 0.004});
  const Vec3 taxi = inPanel(0, {-1.95, 0.0, LE - 0.004});
  const Vec3 tailLight = {0, 0.3, finTE(0.3) + 0.012};

  Exterior e;
  e.paint = merge(paint);
  e.stripe = skin.parts[SkinStripe];
  e.accent = skin.parts[SkinAccent];
  e.seam = skin.parts[SkinSeam];
  e.rubber = skin.parts[SkinSeal];
  e.dark = merge(dark);
  e.metal = merge(metal);
  e.glass = skin.parts[SkinGlass];
  e.lining = skin.lining;
  e.reveals = skin.reveals;
  e.letters = {letteringGeometry(-1), letteringGeometry(1)};
  e.panels = panels;
  e.wings = wings;
  e.elevator = stab.elevator;
  e.elevatorHinge = stab.hinge;
  e.rudder = fin.rudder;
  e.rudderHinge = fin.hinge;
  e.blades = blades;
  e.tips = tips;
  e.spinner = spinner;
  e.legs = legs;
  e.outer = outerTubes;
  e.inner = innerTubes;
  e.spats = spats;
  for (const WheelGeo& w : wheels) {
    e.tyres.push_back(w.tyre);
    e.hubs.push_back(w.hub);
  }
  e.fork = fork;
  e.nav = nav;
  e.strobes = strobes;
  e.beacon = beacon;
  e.landing = landing;
  e.taxi = taxi;
  e.tailLight = tailLight;
  e.fuselage = std::move(fp);
  return e;
}

/** Builds the outside once; every aeroplane shares it. */
Exterior& exterior() {
  static Exterior EXTERIOR = buildExterior();
  return EXTERIOR;
}

// ------------------------------------------------------------------------------- the cockpit
/** The panel's plane: its lower edge, and how far it leans back. */
const double PANEL_Y = 0.08;
const double PANEL_Z = -0.99;
const double PANEL_TILT = 12 * DEG;
/** the panel's plate reaches below and above the pictures */
const P2 PLATE_V = {-0.1, PANEL_AREA.height + 0.022};

Vec3 onPanel(double x, double v, double w = 0) {
  return {x, PANEL_Y + v * std::cos(PANEL_TILT) + w * std::sin(PANEL_TILT), PANEL_Z - v * std::sin(PANEL_TILT) + w * std::cos(PANEL_TILT)};
}

struct Interior {
  GeometryPtr shield;
  GeometryPtr plate;
  GeometryPtr wall;
  GeometryPtr pale;
  GeometryPtr fabric;
  GeometryPtr carpet;
  GeometryPtr black;
  GeometryPtr chrome;
  GeometryPtr red;
  GeometryPtr yoke;
  GeometryPtr yokeShaft;
  GeometryPtr pedal;
  GeometryPtr knob;
};

Interior buildInterior(Fuselage& f) {
  std::vector<GeometryPtr> shield;
  std::vector<GeometryPtr> wall;
  std::vector<GeometryPtr> black;
  std::vector<GeometryPtr> chrome;
  std::vector<GeometryPtr> fabric;

  // the panel's plate, as wide as the cabin
  std::vector<std::vector<Vec3>> rows;
  for (int i = 0; i <= 8; i++) {
    const double v = PLATE_V[0] + ((PLATE_V[1] - PLATE_V[0]) * i) / 8;
    const Vec3 p = onPanel(0, v);
    const double hw = f.wallX(p[2], p[1]) - 0.012;
    rows.push_back({onPanel(-hw, v), onPanel(hw, v)});
  }
  GeometryPtr plate = Sculpt().sheet(rows, {0, std::sin(PANEL_TILT), std::cos(PANEL_TILT)}).build();

  // the glare shield over it, running forward to the foot of the windscreen
  const Vec3 topEdge = onPanel(0, PLATE_V[1]);
  const std::vector<P2> profile = {{-0.012, 0.012}, {0.0, 0.042}, {0.019, 0.046}, {0.028, 0.022}, {0.026, -0.08}, {0.012, -0.2}, {0.004, -0.27}};
  const int across = 14;
  std::vector<std::vector<Vec3>> shelf;
  for (const auto& [dy, dz] : profile) {
    const double z = topEdge[2] + dz;
    const double y = topEdge[1] + dy;
    const double hw = f.wallX(z, std::min(y, f.section(z).top - 0.05)) - 0.012;
    std::vector<Vec3> row;
    for (int i = 0; i <= across; i++) {
      const double x = -hw + (2 * hw * i) / across;
      row.push_back({x, std::min(y, f.roofY(z, x) - 0.012), z});
    }
    shelf.push_back(row);
  }
  Sculpt glare;
  glare.sheet(shelf, {0, 1, 0.35});
  std::vector<Vec3> firsts;
  std::vector<Vec3> lasts;
  for (const auto& r : shelf) {
    firsts.push_back(r[0]);
    lasts.push_back(r[r.size() - 1]);
  }
  glare.fan(firsts, {-1, 0, 0});
  glare.fan(lasts, {1, 0, 0});
  shield.push_back(glare.build(50));
  // under the panel: its lower side, and the firewall behind the pedals
  const Vec3 low = onPanel(0, PLATE_V[0]);
  std::vector<std::vector<Vec3>> under;
  {
    const std::array<double, 3> zs = {low[2], low[2] - 0.12, low[2] - 0.3};
    for (size_t i = 0; i < zs.size(); i++) {
      const double z = zs[i];
      const double y = low[1] + i * 0.01;
      const double hw = f.wallX(z, y) - 0.012;
      under.push_back({{-hw, y, z}, {hw, y, z}});
    }
  }
  shield.push_back(Sculpt().sheet(under, {0, -1, 0}).build());
  std::vector<Vec3> firewall;
  for (int j = 0; j < 48; j++) firewall.push_back(f.point(j / 48.0, -1.555));
  shield.push_back(Sculpt().fan(firewall, {0, 0, 1}).build());

  // floor, headliner under the wing, the wall behind the seats
  const double floorY = -0.42;
  std::vector<std::vector<Vec3>> floor;
  for (double z = -1.54; z <= 1.55; z += 0.31) {
    const double hw = f.wallX(z, floorY) - 0.006;
    floor.push_back({{-hw, floorY, z}, {hw, floorY, z}});
  }
  std::vector<GeometryPtr> rug = {Sculpt().sheet(floor, {0, 1, 0}).build()};
  // the carpet runs up the walls of the footwells
  for (const double s : {-1.0, 1.0}) {
    std::vector<std::vector<Vec3>> wellRows;
    for (double z = -1.54; z <= 0.7; z += 0.28) {
      std::vector<Vec3> row;
      for (const double y : {floorY, -0.3, -0.2, -0.1}) row.push_back({s * (f.wallX(z, y) - 0.008), y, z});
      wellRows.push_back(row);
    }
    rug.push_back(Sculpt().sheet(wellRows, {-s, 0, 0}).build());
  }
  GeometryPtr carpet = merge(rug);
  const double ceilY = 0.915;
  std::vector<std::vector<Vec3>> ceil;
  for (double z = -0.6; z <= 0.98; z += 0.158) {
    const double hw = f.wallX(z, ceilY) - 0.004;
    ceil.push_back({{-hw, ceilY, z}, {hw, ceilY, z}});
  }
  GeometryPtr pale = Sculpt().sheet(ceil, {0, -1, 0}).build();
  std::vector<Vec3> bulkhead;
  for (int j = 0; j < 48; j++) bulkhead.push_back(f.point(j / 48.0, 1.56));
  wall.push_back(Sculpt().fan(bulkhead, {0, 0, -1}).build());
  // the baggage shelf behind the seats
  wall.push_back(slab(0.86, 0.03, 0.5, 0.01, {0, -0.2, 1.28}));

  // the seats
  const double ex = T.eye[0];
  for (const double x : {ex, -ex}) {
    fabric.push_back(slab(0.46, 0.11, 0.47, 0.045, {x, -0.245, -0.5}, -0.06));
    fabric.push_back(slab(0.44, 0.68, 0.1, 0.045, {x, 0.13, -0.19}, 15 * DEG));
    fabric.push_back(slab(0.26, 0.15, 0.08, 0.035, {x, 0.56, -0.08}, 15 * DEG));
    black.push_back(block(x - 0.135, x - 0.105, floorY, -0.3, -0.7, -0.25));
    black.push_back(block(x + 0.105, x + 0.135, floorY, -0.3, -0.7, -0.25));
  }
  // armrests and handles on the doors
  for (const double s : {-1.0, 1.0}) {
    const double x = f.wallX(-0.55, 0.17) - 0.03;
    wall.push_back(slab(0.045, 0.035, 0.52, 0.012, {s * x, 0.17, -0.55}));
    black.push_back(block(s * (x + 0.005) - 0.01, s * (x + 0.005) + 0.01, 0.24, 0.26, -0.33, -0.22));
  }
  // the mixture knob (red) beside the throttle, the magnetic compass on the glare shield
  GeometryPtr redCyl = cylinderGeometry(0.016, 0.016, 0.03, 12);
  redCyl->rotateX(PI / 2).translate(0.07, 0.0, PANEL_Z + 0.03);
  GeometryPtr red = bare(redCyl);
  GeometryPtr stem = cylinderGeometry(0.0045, 0.0045, 0.03, 8);
  stem->rotateX(PI / 2).translate(0.07, 0.0, PANEL_Z + 0.008);
  chrome.push_back(bare(stem));
  const Vec3 compass = onPanel(0, PLATE_V[1], 0);
  black.push_back(slab(0.055, 0.042, 0.05, 0.01, {compass[0] + 0.05, compass[1] + 0.052, compass[2] - 0.05}));

  // the moving parts: a yoke (ram's horns on a hub, a shaft into the panel), a pedal, the throttle knob
  GeometryPtr yoke = merge({
      pipe({{-0.15, 0.085, 0},
            {-0.152, 0.035, 0},
            {-0.141, -0.008, 0},
            {-0.102, -0.032, 0},
            {-0.04, -0.04, 0},
            {0.04, -0.04, 0},
            {0.102, -0.032, 0},
            {0.141, -0.008, 0},
            {0.152, 0.035, 0},
            {0.15, 0.085, 0}},
           0.0135, 10),
      slab(0.09, 0.065, 0.05, 0.018, {0, -0.03, -0.012}),
  });
  GeometryPtr shaft = cylinderGeometry(0.011, 0.011, 0.42, 10);
  shaft->rotateX(PI / 2).translate(0, -0.03, -0.24);
  GeometryPtr yokeShaft = bare(shaft);
  GeometryPtr pedal = merge({slab(0.075, 0.13, 0.014, 0.012, {0, 0.07, 0}, -0.4), block(-0.008, 0.008, 0.0, 0.2, 0.0, 0.02)});
  GeometryPtr knobCap = cylinderGeometry(0.017, 0.017, 0.032, 14);
  knobCap->rotateX(PI / 2).translate(0, 0, 0.016);
  GeometryPtr knobRod = cylinderGeometry(0.0045, 0.0045, 0.12, 8);
  knobRod->rotateX(PI / 2).translate(0, 0, -0.06);
  GeometryPtr knob = merge({bare(knobCap), bare(knobRod)});

  return {merge(shield), plate, merge(wall), pale, merge(fabric), carpet, merge(black), merge(chrome), merge({red}), yoke, yokeShaft, pedal, knob};
}

const Interior& interior(Fuselage& f) {
  static const Interior INTERIOR = buildInterior(f);
  return INTERIOR;
}

const Vector3 DOWN(0, -1, 0);

}  // namespace

// ---------------------------------------------------------------------------------- the model
PlaneModel::PlaneModel(const PlaneModelOptions& options) {
  const bool withInterior = options.interior;
  const Shared& S = shared();
  Exterior& x = exterior();
  Group& root = *this->root;
  root.name = "aeroplane";
  const ObjectPtr rootPtr = this->root;
  const MeshFn mesh = [rootPtr](const GeometryPtr& geo, const MaterialPtr& mat, const ObjectPtr& parent, bool shadow) {
    auto m = make<Mesh>(geo, mat);
    m->castShadow = shadow;
    (parent ? parent : rootPtr)->add(m);
    return m;
  };

  bladeMat_ = lambert({.color = Color(0x26282b), .transparent = true});
  tipMat_ = lambert({.color = Color(0xe8b81e), .transparent = true});
  discMat_ = basic({.map = disc(), .transparent = true, .opacity = 0, .side = Side::Double, .depthWrite = false});
  glass_ = phong({.color = Color(0x18242e), .transparent = true, .opacity = 0.4, .side = Side::Double, .depthWrite = false, .specular = Color(0x9fb4c4), .shininess = 120});
  navMat_ = basic({.vertexColors = true, .toneMapped = false});
  strobeMat_ = basic({.color = Color(0xc8ccd0), .toneMapped = false});
  beaconMat_ = basic({.color = Color(0x5a1210), .toneMapped = false});
  landingMat_ = basic({.color = Color(0xb8bec4), .toneMapped = false});
  taxiMat_ = basic({.color = Color(0xb8bec4), .toneMapped = false});

  // ---- the outside, fixed
  mesh(x.paint, S.PAINT, nullptr, true);
  mesh(x.stripe, S.STRIPE, nullptr, true);
  mesh(x.accent, S.ACCENT, nullptr, false);
  mesh(x.seam, S.SEAM, nullptr, false);
  mesh(x.rubber, S.RUBBER, nullptr, false);
  mesh(x.dark, S.DARK, nullptr, true);
  mesh(x.metal, S.METAL, nullptr, true);
  MaterialPtr letters = lambert({.map = lettering(), .transparent = true, .depthWrite = false, .polygonOffset = true, .polygonOffsetFactor = -2, .polygonOffsetUnits = -4});
  ownedMaterials_.push_back(letters);
  for (const GeometryPtr& g : x.letters) mesh(g, letters, nullptr, false);
  // the cabin's inside: lining and window frames (see-through on request), then the glass
  lining_ = cabinMaterial(0x8f918d, Side::Back);
  frames_ = cabinMaterial(0x3b3f45);
  mesh(x.lining, lining_, nullptr, false);
  mesh(x.reveals, frames_, nullptr, false);
  auto glass = mesh(x.glass, glass_, nullptr, false);
  glass->renderOrder = 5;
  // (seen from inside, the glass is clear; from outside it is darker – and quite dark on an aeroplane without a cockpit)
  glass_->onBeforeRender = [this, withInterior](Renderer&, Scene&, Camera& camera) { glass_->opacity = inside(camera) ? 0.1 : withInterior ? 0.42 : 0.7; };
  for (const MaterialPtr& m : {glass_, lining_, frames_, bladeMat_, tipMat_, discMat_, navMat_, strobeMat_, beaconMat_, landingMat_, taxiMat_}) ownedMaterials_.push_back(m);
  seeThrough_.push_back(lining_);
  seeThrough_.push_back(frames_);

  // ---- control surfaces on their hinges
  for (size_t i = 0; i < x.wings.size(); i++) {
    const HalfWing& w = x.wings[i];
    auto frame = make<Group>();
    frame->matrixAutoUpdate = false;
    frame->matrix.copy(x.panels[i]);
    root.add(frame);
    auto aileron = make<Group>();
    aileron->position.set(w.aileronHinge[0], w.aileronHinge[1], w.aileronHinge[2]);
    mesh(w.aileron, S.PAINT, aileron, true);
    auto flap = make<Group>();
    flap->position.set(w.flapPivot[0], w.flapPivot[1], w.flapPivot[2]);
    mesh(w.flap, S.PAINT, flap, true);
    frame->add(aileron, flap);
    ailerons_.push_back(aileron);
    flaps_.push_back(flap);
  }
  elevator_->position.set(x.elevatorHinge[0], x.elevatorHinge[1], x.elevatorHinge[2]);
  mesh(x.elevator, S.PAINT, elevator_, true);
  rudder_->position.set(x.rudderHinge[0], x.rudderHinge[1], x.rudderHinge[2]);
  mesh(x.rudder, S.PAINT, rudder_, true);
  root.add(elevator_, rudder_);

  // ---- the propeller
  prop_->position.set(T.propHub[0], T.propHub[1], T.propHub[2]);
  mesh(x.blades, bladeMat_, prop_, false);
  mesh(x.tips, tipMat_, prop_, false);
  mesh(x.spinner, S.STRIPE, prop_, true);
  disc_ = make<Mesh>(ringGeometry(0.2, T.propDiameter / 2, 48, 1), discMat_);
  ownedGeometries_.push_back(disc_->geometry);
  disc_->visible = false;
  prop_->add(disc_);
  root.add(prop_);

  // ---- the gear
  for (size_t i = 0; i < x.legs.size(); i++) {
    const LegShape& shape = x.legs[i];
    const GearLeg& spec = T.gear[i];
    auto outer = make<Group>();
    outer->position.set(shape.root[0], shape.root[1], shape.root[2]);
    mesh(x.outer[i], S.PAINT, outer, true);
    auto inner = make<Group>();
    mesh(x.inner[i], S.CHROME, inner, true);
    auto carriage = make<Group>();
    auto spin = make<Group>();
    mesh(x.tyres[i], S.TYRE, spin, true);
    mesh(x.hubs[i], S.HUB, spin, true);
    if (spec.steers) {
      // turning about the strut: the fork, the fairing and the wheel below it
      mesh(x.fork, S.DARK, carriage, true);
      auto s = mesh(x.spats[i], S.PAINT, carriage, true);
      s->position.set(0, spec.y - shape.foot[1], 0);
      spin->position.set(0, spec.y - shape.foot[1], 0);
    } else {
      mesh(x.spats[i], S.PAINT, carriage, true);
    }
    carriage->add(spin);
    root.add(outer, inner, carriage);
    legs_.push_back({spec, v3(shape.root), v3(shape.foot), outer, inner, carriage, spin});
  }

  // ---- lights
  /** a round lamp; the navigation lights carry their colour in the geometry and share one material */
  const auto lens = [](const Vec3& at, double r, std::optional<unsigned> colour) {
    GeometryPtr s = sphereGeometry(r, 10, 8);
    s->translate(at[0], at[1], at[2]);
    GeometryPtr g = bare(s);
    if (colour) {
      const Color c(*colour);
      const size_t n = g->getAttribute("position")->count();
      std::vector<float> col(n * 3);
      for (size_t k = 0; k < n; k++) {
        col[k * 3] = static_cast<float>(c.r);
        col[k * 3 + 1] = static_cast<float>(c.g);
        col[k * 3 + 2] = static_cast<float>(c.b);
      }
      g->setAttribute("color", attr(col, 3));
    }
    return g;
  };
  const auto own = [this](const GeometryPtr& g) {
    ownedGeometries_.push_back(g);
    return g;
  };
  const auto sprite = [this, rootPtr](const Vec3& at, unsigned colour, double size, const ObjectPtr& parent) {
    MaterialPtr m = spriteMaterial({.color = Color(colour), .map = glow(), .transparent = true, .depthWrite = false, .blending = Blending::Additive});
    ownedMaterials_.push_back(m);
    auto s = make<Sprite>(m);
    s->position.set(at[0], at[1], at[2]);
    s->scale.setScalar(size);
    s->visible = false;
    (parent ? parent : rootPtr)->add(s);
    return s;
  };
  {
    std::vector<GeometryPtr> lenses;
    for (const Lamp& l : x.nav) lenses.push_back(lens(l.at, 0.032, l.colour));
    mesh(own(mergeGeometries(lenses, false)), navMat_, nullptr, false);
  }
  for (const Lamp& l : x.nav) navGlows_.push_back(sprite(l.at, l.colour, 0.32, nullptr));
  const Vec3 tail = {x.tailLight[0] - x.rudderHinge[0], x.tailLight[1] - x.rudderHinge[1], x.tailLight[2] - x.rudderHinge[2]};
  mesh(own(lens(tail, 0.022, 0xffffffu)), navMat_, rudder_, false);
  navGlows_.push_back(sprite(tail, 0xffffff, 0.26, rudder_));
  {
    std::vector<GeometryPtr> lenses;
    for (const Vec3& p : x.strobes) lenses.push_back(lens(p, 0.026, std::nullopt));
    mesh(own(mergeGeometries(lenses, false)), strobeMat_, nullptr, false);
  }
  for (const Vec3& p : x.strobes) strobeGlows_.push_back(sprite(p, 0xffffff, 1.3, nullptr));
  {
    GeometryPtr b = sphereGeometry(0.032, 12, 8);
    b->scale(1, 1.3, 1).translate(x.beacon[0], x.beacon[1], x.beacon[2]);
    mesh(own(bare(b)), beaconMat_, nullptr, false);
  }
  beaconGlow_ = sprite(x.beacon, 0xff2a1a, 0.75, nullptr);
  const Vec3 forward = {0, 0, -1};
  mesh(own(oval(x.landing, {1, 0, 0}, {0, 1, 0}, 0.05, 0.035, forward)), landingMat_, nullptr, false);
  mesh(own(oval(x.taxi, {1, 0, 0}, {0, 1, 0}, 0.04, 0.03, forward)), taxiMat_, nullptr, false);
  const auto beam = [this, &own, &root](const Vec3& at, double size) {
    MaterialPtr m = basic({.color = Color(0xfff6dc), .map = glow(), .transparent = true, .depthWrite = false, .blending = Blending::Additive});
    ownedMaterials_.push_back(m);
    GeometryPtr pg = planeGeometry(size, size);
    pg->rotateY(PI);
    auto g = make<Mesh>(own(pg), m);
    g->position.set(at[0], at[1], at[2] - 0.01);
    g->visible = false;
    root.add(g);
    return g;
  };
  landingGlow_ = beam(x.landing, 0.7);
  taxiGlow_ = beam(x.taxi, 0.5);

  if (withInterior) {
    panel_ = std::make_unique<FlightPanel>();
    buildCockpit(mesh);
  }
}

void PlaneModel::buildCockpit(const MeshFn& mesh) {
  const Interior& g = interior(*exterior().fuselage);
  auto cockpit = make<Group>();
  cockpit->name = "cockpit";
  root->add(cockpit);
  struct M {
    MaterialPtr shield = cabinMaterial(0x202226);
    MaterialPtr plate = cabinMaterial(PANEL_PAINT);
    MaterialPtr wall = cabinMaterial(0x8f918d);
    MaterialPtr pale = cabinMaterial(0xc4c5c1);
    MaterialPtr fabric = cabinMaterial(0x4a5466);
    MaterialPtr carpet = cabinMaterial(0x2c2e32);
    MaterialPtr black = cabinMaterial(0x18191c);
    MaterialPtr chrome = cabinMaterial(0xb9bec4);
    MaterialPtr red = cabinMaterial(0xb3261e);
  } m;
  const std::vector<std::pair<GeometryPtr, MaterialPtr>> parts = {
      {g.shield, m.shield}, {g.plate, m.plate}, {g.wall, m.wall}, {g.pale, m.pale}, {g.fabric, m.fabric},
      {g.carpet, m.carpet}, {g.black, m.black}, {g.chrome, m.chrome}, {g.red, m.red},
  };
  for (const auto& [geo, mat] : parts) {
    seeThrough_.push_back(mat);
    ownedMaterials_.push_back(mat);
  }
  for (const auto& [geo, mat] : parts) mesh(geo, mat, cockpit, false);
  // the pictures on the panel: lit like the panel round them, and a little by themselves
  FlightPanel& panel = *panel_;
  const auto face = [this, &cockpit](const TexturePtr& tex, double x0, double x1) {
    MaterialPtr mat = lambert({.color = Color(0.6, 0.6, 0.6), .map = tex, .emissive = Color(0.42, 0.42, 0.42)});
    mat->emissiveMap = tex;
    ownedMaterials_.push_back(mat);
    auto p = make<Mesh>(planeGeometry(x1 - x0, PANEL_AREA.height), mat);
    ownedGeometries_.push_back(p->geometry);
    const Vec3 c = onPanel((x0 + x1) / 2, PANEL_AREA.height / 2, 0.0015);
    p->position.set(c[0], c[1], c[2]);
    p->rotation.x = -PANEL_TILT;
    cockpit->add(p);
    return p;
  };
  auto live = face(panel.texture, PANEL_AREA.left, PANEL_AREA.split);
  // (redrawn only while somebody looks at it)
  live->onBeforeRender = [this](Renderer&, Scene&, Camera&) { panelSeen_ = true; };
  face(panel.radios, PANEL_AREA.split, PANEL_AREA.right);

  // yokes and their shafts, out of the panel below the instruments
  for (const double x : {T.eye[0], -T.eye[0]}) {
    auto slide = make<Group>();
    slide->position.set(x, 0.035, -0.8);
    auto wheel = make<Group>();
    mesh(g.yoke, m.black, wheel, false);
    slide->add(wheel);
    mesh(g.yokeShaft, m.chrome, slide, false);
    cockpit->add(slide);
    yokes_.push_back({slide, wheel});
  }
  // rudder pedals: left and right for each seat
  for (const double x : {T.eye[0], -T.eye[0]}) {
    for (const double s : {-1.0, 1.0}) {
      auto p = make<Group>();
      p->position.set(x + s * 0.09, -0.37, -1.3);
      mesh(g.pedal, m.black, p, false);
      cockpit->add(p);
      pedals_.push_back(p);
    }
  }
  // the throttle: a black knob on a plunger in the middle of the panel's lower edge
  throttle_->position.set(-0.01, 0.0, PANEL_Z + 0.02);
  mesh(g.knob, m.black, throttle_, false);
  cockpit->add(throttle_);
  // the decision what is see-through is made per picture
  for (const MaterialPtr& mat : seeThrough_) {
    Material* raw = mat.get();
    mat->onBeforeRender = [this, raw](Renderer&, Scene&, Camera& camera) { raw->opacity = cabinOpacity_ < 1 && inside(camera) ? cabinOpacity_ : 1; };
  }
}

bool PlaneModel::inside(const Camera& camera) {
  // (the original keeps the answer for the rest of the picture – it is the same for every material asking in it)
  tmp_.setFromMatrixPosition(camera.matrixWorld);
  const Vector3& p = root->worldToLocal(tmp_);
  return std::fabs(p.x) < 0.58 && p.y > -0.5 && p.y < 1.0 && p.z > -1.3 && p.z < 1.6;
}

void PlaneModel::setCabinOpacity(double opacity) {
  const double o = std::isfinite(opacity) ? std::min(1.0, std::max(0.05, opacity)) : 1;
  const bool on = o < 0.995;
  const bool was = cabinOpacity_ < 1;
  cabinOpacity_ = on ? o : 1;
  if (on == was) return;
  for (const MaterialPtr& m : seeThrough_) {
    m->transparent = on;
    m->depthWrite = !on;
    m->opacity = 1;
    m->needsUpdate();
  }
}

void PlaneModel::update(const PlaneVisualState& s, double /*dt*/, double now) {
  const auto num = [](double v, double d = 0) { return std::isfinite(v) ? v : d; };
  // control surfaces (left aileron down for a roll to the right, right one up)
  ailerons_[0]->rotation.x = num(s.aileron);
  ailerons_[1]->rotation.x = -num(s.aileron);
  const double flap = std::max(0.0, num(s.flapsDeg)) * DEG;
  for (const ObjectPtr& f : flaps_) f->rotation.x = flap;
  elevator_->rotation.x = -num(s.elevator);
  rudder_->rotation.y = num(s.rudder);

  // the propeller turns clockwise as the pilot sees it; fast, it becomes a faint disc
  prop_->rotation.z = -num(s.propAngle);
  const double rpm = num(s.rpm);
  const double k = std::min(1.0, std::max(0.0, (rpm - 380) / 420));
  const double blur = k * k * (3 - 2 * k);
  bladeMat_->opacity = 1 - 0.94 * blur;
  tipMat_->opacity = 1 - 0.9 * blur;
  discMat_->opacity = blur * 0.6;
  disc_->visible = blur > 0.01;

  // the gear: each strut shortens by its compression, the wheel turns (and the nose wheel steers)
  Vector3& dir = tmp_;
  for (size_t i = 0; i < legs_.size(); i++) {
    LegParts& leg = legs_[i];
    const PlaneVisualState::Wheel w = i < s.wheels.size() ? s.wheels[i] : PlaneVisualState::Wheel{JS_NAN, JS_NAN, JS_NAN};
    const double c = num(w.compression);
    leg.carriage->position.set(leg.spec.steers ? leg.foot.x : leg.spec.x, (leg.spec.steers ? leg.foot.y : leg.spec.y) + c, leg.spec.z);
    leg.carriage->rotation.y = leg.spec.steers ? -num(w.steer) : 0;
    leg.spin->rotation.x = -num(w.spin);
    leg.inner->position.set(leg.foot.x, leg.foot.y + c, leg.foot.z);
    dir.subVectors(leg.inner->position, leg.root).normalize();
    leg.outer->quaternion.setFromUnitVectors(DOWN, dir);
    leg.inner->quaternion.copy(leg.outer->quaternion);
  }

  // lights: navigation steady, strobes a double flash, the beacon a red flash
  const PanelReadings::Lights& L = s.lights;
  navMat_->color.setScalar(L.nav ? 1 : 0.32);
  for (const auto& g : navGlows_) g->visible = L.nav;
  const double t = num(now);
  const double sp = std::fmod(std::fmod(t, 1.2) + 1.2, 1.2);
  const bool flash = L.strobe && (sp < 0.05 || (sp > 0.14 && sp < 0.19));
  strobeMat_->color.setHex(flash ? 0xffffff : 0xc8ccd0);
  for (const auto& g : strobeGlows_) g->visible = flash;
  const double bp = std::fmod(std::fmod(t, 1.1) + 1.1, 1.1);
  const bool beacon = L.beacon && bp < 0.16;
  beaconMat_->color.setHex(beacon ? 0xff2a1a : 0x5a1210);
  beaconGlow_->visible = beacon;
  landingMat_->color.setHex(L.landing ? 0xffffff : 0xb8bec4);
  landingGlow_->visible = L.landing;
  taxiMat_->color.setHex(L.taxi ? 0xffffff : 0xb8bec4);
  taxiGlow_->visible = L.taxi;

  // the cockpit's controls
  if (panel_) {
    for (const YokeParts& y : yokes_) {
      y.slide->position.z = -0.8 + std::max(-1.0, std::min(1.0, num(s.yoke.pitch))) * 0.07;
      y.wheel->rotation.z = -std::max(-1.0, std::min(1.0, num(s.yoke.roll))) * 1.2;
    }
    const double p = std::max(-1.0, std::min(1.0, num(s.pedals))) * 0.05;
    for (size_t i = 0; i < pedals_.size(); i++) pedals_[i]->position.z = -1.3 + (i % 2 ? -p : p);
    throttle_->position.z = PANEL_Z + 0.02 + (1 - std::max(0.0, std::min(1.0, num(s.throttle)))) * 0.06;
    if (panelSeen_ && panel_->update(s, t)) panelSeen_ = false;
  }
}

void PlaneModel::dispose() {
  if (panel_) panel_->dispose();
  for (const GeometryPtr& g : ownedGeometries_) g->dispose();
  for (const MaterialPtr& m : ownedMaterials_) m->dispose();
}

}  // namespace fs

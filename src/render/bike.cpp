// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/bike.ts @ v1.7.7
#include "render/bike.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>

#include "core/js.h"
#include "engine/curves.h"
#include "engine/geometries.h"
#include "render/truck.h"
#include "sim/vehicle/fleet.h"

namespace fs {

namespace {

const double DEG = PI / 180;
const Vector3 X(1, 0, 0);
const Vector3 Y(0, 1, 0);
const Vector3 Z(0, 0, 1);
Vector3 V(double x = 0, double y = 0, double z = 0) { return Vector3(x, y, z); }
double clamp(double v, double lo, double hi) { return jsMin(hi, jsMax(lo, v)); }
double sign(double v) { return v > 0 ? 1 : v < 0 ? -1 : v; }

using UV = std::array<double, 2>;
using Outline = std::vector<UV>;
using Rings = std::vector<std::vector<Vector3>>;
using Geos = std::vector<GeometryPtr>;

// ------------------------------------------------------------------------------- geometry
// Every part is made as position + normal without an index, so that the parts of one
// material can be merged into one mesh.

GeometryPtr bare(const GeometryPtr& g) {
  GeometryPtr n = g->index ? g->toNonIndexed() : g;
  std::vector<std::string> names;
  for (const auto& entry : n->attributes) names.push_back(entry.first);
  for (const std::string& name : names)
    if (name != "position" && name != "normal") n->deleteAttribute(name);
  return n;
}

/** A closed outline: an ellipse (p = 1) or, with smaller p, a rounder rectangle; [u, v] pairs counter-clockwise. */
Outline oval(double a, double b, int n = 16, double p = 1) {
  Outline out;
  for (int i = 0; i < n; i++) {
    const double t = (static_cast<double>(i) / n) * PI * 2;
    const double c = std::cos(t);
    const double s = std::sin(t);
    out.push_back({a * sign(c) * std::pow(std::fabs(c), p), b * sign(s) * std::pow(std::fabs(s), p)});
  }
  return out;
}

Outline circle(double r, int n = 12) { return oval(r, r, n); }

/** An outline whose upper half (v > 0) reaches `up`, its lower half `down`: a tank or a seat in section. */
Outline section(double hw, double up, double down, int n = 20, double p = 0.72) {
  Outline out;
  for (int i = 0; i < n; i++) {
    const double t = (static_cast<double>(i) / n) * PI * 2;
    const double c = std::cos(t);
    const double s = std::sin(t);
    out.push_back({hw * sign(c) * std::pow(std::fabs(c), p), (s >= 0 ? up : down) * sign(s) * std::pow(std::fabs(s), p)});
  }
  return out;
}

/** A thin strip in section, half width hw, its edges bent by `curl` towards +v, `thick` thick: a mudguard, a screen. */
Outline strip(double hw, double curl, double thick, int n = 8) {
  Outline pts;
  for (int i = 0; i <= n; i++) {
    const double u = -1 + (2.0 * i) / n;
    pts.push_back({u * hw, curl * u * u - thick / 2});
  }
  for (int i = n; i >= 0; i--) {
    const double u = -1 + (2.0 * i) / n;
    pts.push_back({u * hw, curl * u * u + thick / 2});
  }
  return pts;
}

/** A rectangle w × h with corners of radius r. */
Outline rounded(double w, double h, double r, int seg = 2) {
  const double q = jsMax(0.0004, jsMin(jsMin(r, w / 2 - 1e-4), h / 2 - 1e-4));
  Outline pts;
  const std::array<double, 3> corners[] = {
      {w / 2 - q, -h / 2 + q, -90},
      {w / 2 - q, h / 2 - q, 0},
      {-w / 2 + q, h / 2 - q, 90},
      {-w / 2 + q, -h / 2 + q, 180},
  };
  for (const auto& [cx, cy, a0] : corners) {
    for (int i = 0; i <= seg; i++) {
      const double a = (a0 + (90.0 * i) / seg) * DEG;
      pts.push_back({cx + std::cos(a) * q, cy + std::sin(a) * q});
    }
  }
  return pts;
}

/** A flat outline put into space: centre + u·[0] + v·[1]. */
std::vector<Vector3> ring(const Vector3& c, const Vector3& u, const Vector3& v, const Outline& outline) {
  std::vector<Vector3> out;
  out.reserve(outline.size());
  for (const auto& [a, b] : outline) out.push_back(c.clone().addScaledVector(u, a).addScaledVector(v, b));
  return out;
}

/** Two directions square to `d` and to each other. */
std::pair<Vector3, Vector3> across(const Vector3& d) {
  Vector3 u = (std::fabs(d.x) < 0.9 ? X : Y).clone();
  u.addScaledVector(d, -u.dot(d)).normalize();
  return {u, Vector3().crossVectors(d, u)};
}

Vector3 centroid(const std::vector<Vector3>& r) {
  Vector3 c = V();
  for (const Vector3& p : r) c.add(p);
  return c.multiplyScalar(1.0 / static_cast<double>(r.size()));
}

/** Whether a skin over these rings, in the order given, would look inwards (and must be turned round). */
bool inwards(const Rings& rings) {
  const size_t n = rings[0].size();
  double out = 0;
  Vector3 a_, b_, c_;
  for (size_t i = 0; i + 1 < rings.size(); i++) {
    const Vector3 m = centroid(rings[i]);
    for (size_t j = 0; j < n; j++) {
      const Vector3& a = rings[i][j];
      a_.subVectors(rings[i][(j + 1) % n], a).cross(b_.subVectors(rings[i + 1][(j + 1) % n], a));
      out += a_.dot(c_.subVectors(a, m));
    }
  }
  return out < 0;
}

/**
 * A skin over rings of points (all of the same length), shaded smooth all
 * round; the sides look away from the middle of the rings.  The ends are
 * closed by flat caps (unless `caps` is false, or a ring has shrunk to a point).
 */
GeometryPtr loft(const Rings& rings, bool caps, bool flip) {
  const size_t n = rings[0].size();
  std::vector<float> pos;
  for (const auto& r : rings)
    for (const Vector3& p : r) pos.insert(pos.end(), {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)});
  std::vector<Vector3> mids;
  for (const auto& r : rings) mids.push_back(centroid(r));
  std::vector<uint32_t> idx;
  for (size_t i = 0; i + 1 < rings.size(); i++) {
    for (size_t j = 0; j < n; j++) {
      const size_t k = (j + 1) % n;
      const uint32_t a = static_cast<uint32_t>(i * n + j);
      const uint32_t b = static_cast<uint32_t>(i * n + k);
      const uint32_t c = static_cast<uint32_t>((i + 1) * n + k);
      const uint32_t d = static_cast<uint32_t>((i + 1) * n + j);
      if (flip) idx.insert(idx.end(), {a, c, b, a, d, c});
      else idx.insert(idx.end(), {a, b, c, a, c, d});
    }
  }
  if (caps) {
    Vector3 a_, b_, c_;
    for (const auto& [i, next] : {std::pair<size_t, size_t>{0, 1}, std::pair<size_t, size_t>{rings.size() - 1, rings.size() - 2}}) {
      const std::vector<Vector3>& r = rings[i];
      const Vector3& m = mids[i];
      if (std::all_of(r.begin(), r.end(), [&](const Vector3& p) { return p.distanceToSquared(m) < 1e-10; })) continue;
      // (its own corners, so that the edge stays sharp)
      const uint32_t base = static_cast<uint32_t>(pos.size() / 3);
      pos.insert(pos.end(), {static_cast<float>(m.x), static_cast<float>(m.y), static_cast<float>(m.z)});
      for (const Vector3& p : r) pos.insert(pos.end(), {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)});
      Vector3 nrm = V();
      for (size_t j = 0; j < n; j++) nrm.add(a_.subVectors(r[j], m).cross(b_.subVectors(r[(j + 1) % n], m)));
      const bool away = nrm.dot(c_.subVectors(m, mids[next])) >= 0;
      for (size_t j = 0; j < n; j++) {
        const size_t k = (j + 1) % n;
        if (away) idx.insert(idx.end(), {base, base + 1 + static_cast<uint32_t>(j), base + 1 + static_cast<uint32_t>(k)});
        else idx.insert(idx.end(), {base, base + 1 + static_cast<uint32_t>(k), base + 1 + static_cast<uint32_t>(j)});
      }
    }
  }
  const GeometryPtr g = make<BufferGeometry>();
  g->setAttribute("position", attr(std::move(pos), 3));
  g->setIndex(idx);
  g->computeVertexNormals();
  return bare(g);
}

GeometryPtr loft(const Rings& rings, bool caps = true) { return loft(rings, caps, inwards(rings)); }

/**
 * A bar along a smooth path through `points`, in cross-section `shape(t)`
 * (t = 0 … 1 along the path), its first coordinate towards `side`.
 */
GeometryPtr sweep(const std::vector<Vector3>& points, const std::function<Outline(double t)>& shape, int steps, const Vector3& side = X, bool caps = true) {
  std::unique_ptr<Curve<Vector3>> curve;
  if (points.size() > 2) curve = std::make_unique<CatmullRomCurve3>(points, false, CatmullRomCurve3::Centripetal);
  else curve = std::make_unique<LineCurve3>(points[0], points[1]);
  Rings rings;
  for (int i = 0; i <= steps; i++) {
    const double t = static_cast<double>(i) / steps;
    const Vector3 c = curve->getPointAt(t);
    const Vector3 tan = curve->getTangentAt(t).normalize();
    Vector3 u = side.clone().addScaledVector(tan, -side.dot(tan));
    if (u.lengthSq() < 1e-6) u = across(tan).first;
    u.normalize();
    rings.push_back(ring(c, u, Vector3().crossVectors(tan, u), shape(t)));
  }
  return loft(rings, caps);
}

/** A round pipe through `points`. */
GeometryPtr pipe(const std::vector<Vector3>& points, double r, int steps = 16, int n = 10) {
  return sweep(points, [&](double) { return circle(r, n); }, steps);
}

/** A straight rod from a to b (radius r at a, r2 at b). */
GeometryPtr rod(const Vector3& a, const Vector3& b, double r, int n = 8, std::optional<double> r2 = std::nullopt) {
  Vector3 d = b.clone().sub(a);
  const GeometryPtr g = cylinderGeometry(r2 ? *r2 : r, r, d.length(), n);
  g->applyQuaternion(Quaternion().setFromUnitVectors(Y, d.normalize()));
  g->translate((a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2);
  return bare(g);
}

/**
 * A solid of revolution about `axis` through `centre`: a profile of [radius,
 * position along the axis]; `hard` keeps every corner of the profile sharp
 * (discs, rims, covers) instead of shading over it.
 */
GeometryPtr turned(const Outline& profile, const Vector3& centre, const Vector3& axis, int n = 24, bool hard = false) {
  const Vector3 ax = axis.clone().normalize();
  const auto [u, v] = across(ax);
  Rings rings;
  for (const auto& [r, a] : profile) rings.push_back(ring(centre.clone().addScaledVector(ax, a), u, v, circle(r, n)));
  if (!hard) return loft(rings, false);
  // each step of the profile a skin of its own, all turned the same way round
  const bool flip = inwards(rings);
  Geos steps;
  for (size_t i = 0; i + 1 < rings.size(); i++) steps.push_back(loft({rings[i], rings[i + 1]}, false, flip));
  return mergeGeometries(steps, false);
}

/** An ellipsoid, turned (YXZ) and moved into place. */
GeometryPtr blob(const Vector3& c, double rx, double ry, double rz, const std::optional<Euler>& rot = std::nullopt, int w = 16, int h = 10) {
  const GeometryPtr g = sphereGeometry(1, w, h);
  g->scale(rx, ry, rz);
  if (rot) g->applyMatrix4(Matrix4().makeRotationFromEuler(*rot));
  g->translate(c.x, c.y, c.z);
  return bare(g);
}

/** A box w × h × d with rounded edges (`r`), turned (YXZ) about its middle and moved to `c`. */
GeometryPtr soap(const Vector3& c, double w, double h, double d, std::optional<double> r_ = std::nullopt, const std::optional<Euler>& rot = std::nullopt) {
  const double r = r_ ? *r_ : jsMin(jsMin(w, h), d) * 0.25;
  const double e = jsMin(r * 0.8, w * 0.3);
  Rings rings;
  for (const auto& [x, s] : {UV{-w / 2, e}, UV{-w / 2 + e, 0}, UV{w / 2 - e, 0}, UV{w / 2, e}}) rings.push_back(ring(V(x, 0, 0), Z, Y, rounded(d - 2 * s, h - 2 * s, r - s)));
  const GeometryPtr g = loft(rings);
  if (rot) g->applyMatrix4(Matrix4().makeRotationFromEuler(*rot));
  g->translate(c.x, c.y, c.z);
  return g;
}

/** A limb or a pin: rounded at both ends, its radius from ra at a to rb at b. */
GeometryPtr limb(const Vector3& a, const Vector3& b, double ra, double rb, int n = 14) {
  const Vector3 ax = b.clone().sub(a).normalize();
  const auto [u, v] = across(ax);
  Rings rings;
  const int cap = 4;
  for (int i = 0; i <= cap; i++) {
    const double t = (PI / 2) * (1 - static_cast<double>(i) / cap);
    rings.push_back(ring(a.clone().addScaledVector(ax, -ra * std::sin(t)), u, v, circle(ra * std::cos(t), n)));
  }
  for (int i = 0; i <= cap; i++) {
    const double t = (PI / 2) * (static_cast<double>(i) / cap);
    rings.push_back(ring(b.clone().addScaledVector(ax, rb * std::sin(t)), u, v, circle(rb * std::cos(t), n)));
  }
  return loft(rings, false);
}

/** [z, half width, top, bottom] */
using Station = std::array<double, 4>;

/** Cross-sections along z ([z, half width, top, bottom], front to rear): a tank, a seat, a tail. */
GeometryPtr stations(const std::vector<Station>& list, int n = 22, double p = 0.72) {
  Rings rings;
  for (const auto& [z, hw, top, bottom] : list) rings.push_back(ring(V(0, (top + bottom) / 2, z), X, Y, section(hw, (top - bottom) / 2, (top - bottom) / 2, n, p)));
  return loft(rings);
}

/** One measure (index i of [z, half width, top, bottom]) of a shape at z, between its cross-sections. */
double between(const std::vector<Station>& list, double z, size_t i) {
  if (z <= list[0][0]) return list[0][i];
  for (size_t k = 0; k + 1 < list.size(); k++) {
    const double z0 = list[k][0], z1 = list[k + 1][0];
    if (z <= z1) return list[k][i] + ((list[k + 1][i] - list[k][i]) * (z - z0)) / (z1 - z0);
  }
  return list.back()[i];
}

/** The point where a limb of two parts (l1 from `a`, l2 to `b`) bends, towards `pole`; a straight limb when it cannot reach. */
Vector3& bend(const Vector3& a, const Vector3& b, double l1, double l2, const Vector3& pole, Vector3& out) {
  Vector3 d = Vector3().subVectors(b, a);
  const double dist = d.length();
  d.normalize();
  if (dist >= l1 + l2 - 1e-4) return out.copy(a).addScaledVector(d, (dist * l1) / (l1 + l2));
  const double k = jsMax(dist, std::fabs(l1 - l2) + 1e-4);
  const double x = (l1 * l1 - l2 * l2 + k * k) / (2 * k);
  const double h = std::sqrt(jsMax(0, l1 * l1 - x * x));
  const Vector3 p = pole.clone().addScaledVector(d, -pole.dot(d)).normalize();
  return out.copy(a).addScaledVector(d, x).addScaledVector(p, h);
}

/** Geometry collected per material, merged into one mesh each. */
class Parts {
 public:
  void add(const MaterialPtr& m, const Geos& g) {
    for (auto& [mat, list] : lists_)
      if (mat == m) {
        list.insert(list.end(), g.begin(), g.end());
        return;
      }
    lists_.emplace_back(m, g);
  }
  void add(const MaterialPtr& m, const GeometryPtr& g) { add(m, Geos{g}); }

  /** The meshes, in `group` whose origin lies at `origin` (in the frame the parts were made in). */
  std::vector<std::shared_ptr<Mesh>> into(Object3D& group, const std::optional<Vector3>& origin = std::nullopt, bool shadow = true) {
    std::vector<std::shared_ptr<Mesh>> out;
    for (auto& [m, list] : lists_) {
      const GeometryPtr g = mergeGeometries(list, false);
      if (origin) g->translate(-origin->x, -origin->y, -origin->z);
      const auto mesh = make<Mesh>(g, m);
      mesh->castShadow = shadow;
      group.add(mesh);
      out.push_back(mesh);
    }
    return out;
  }

 private:
  std::vector<std::pair<MaterialPtr, Geos>> lists_;
};

// ------------------------------------------------------------------------------ materials
MaterialPtr lam(int color) { return lambert({.color = Color(color)}); }

struct Mats {
  MaterialPtr TYRE = lam(0x18191b);
  MaterialPtr BLACK = lam(0x1e2023);
  MaterialPtr ENGINE = lam(0x3a3d42);
  MaterialPtr ALU = lam(0x969ba1);
  MaterialPtr SEAT = lam(0x212226);
  MaterialPtr RADIATOR = lam(0x141619);
  /** the few parts that should glint: chrome and polished steel, the gold of a fork, the visor */
  MaterialPtr CHROME = phong({.color = Color(0xc4c8cc), .specular = Color(0x8c8c8c), .shininess = 90});
  MaterialPtr STEEL = phong({.color = Color(0x8e9398), .specular = Color(0x555555), .shininess = 50});
  MaterialPtr GOLD = phong({.color = Color(0xb98d34), .specular = Color(0x6a5a30), .shininess = 60});
  MaterialPtr VISOR = phong({.color = Color(0x0e1215), .specular = Color(0x9aa4ac), .shininess = 110});
  MaterialPtr SMOKE = phong({.color = Color(0x3a4652), .transparent = true, .opacity = 0.3, .side = Side::Double, .depthWrite = false, .specular = Color(0x8a96a0), .shininess = 100});
  /** the indicators while dark (the lamps switch them, see fleet.ts Lamps) */
  MaterialPtr DIM = lam(0x3a2d2a);
  // the rider: a grey textile suit, boots and gloves of black leather, a white helmet
  MaterialPtr JACKET = lam(0x59606a);
  MaterialPtr TROUSERS = lam(0x363b44);
  MaterialPtr BOOTS = lam(0x151619);
  MaterialPtr GLOVES = lam(0x1b1c1f);
  MaterialPtr HELMET = lam(0xe9ebec);
};

const Mats& M() {
  static const Mats m;
  return m;
}

/** How one class's motorcycle is built. */
struct Build {
  /** its physics: wheelbase and wheel radius */
  const VehicleSpec* spec;
  /** the steering axis: its angle from the vertical (rad), how far its foot lies ahead of the tyre's contact (m) */
  double rake;
  double trail;
  /** tyre widths front and rear, radius of the rims */
  std::array<double, 2> tyres;
  double rim;
  /** wire spokes, else cast wheels of `spokes` spokes (each split in two towards the rim) */
  bool wire;
  int spokes;
  bool split;
  /** brake discs at the front (one or two) and their radius; the rear one's (0: a drum) */
  int discs;
  double disc;
  double rearDisc;
  /** the fork: half the distance between its legs, radius of its thin tubes, upside down (the thick tubes on top) */
  double forkGap;
  double forkR;
  bool usd;
  /** height of the top of the steering axis (the upper triple clamp) */
  double clampY;
  /** the grips: half the width of the handlebar, their height, how far behind the top of the axis */
  std::array<double, 3> grip;
  /** headlamp: height of its middle, its radius; a cowl round it that carries the instruments (and a screen) */
  double lampY;
  double lampR;
  bool cowl;
  bool screen;
  /** the rider: height of the seat under him, where his hips are (z), the footpegs [x, y, z], his back's lean forward (rad) */
  double seatY;
  double hipZ;
  std::array<double, 3> peg;
  double lean;
  /** cross-sections [z, half width, top, bottom] of tank, seat and tail, front to rear */
  std::vector<Station> tank;
  std::vector<Station> seat;
  std::vector<Station> tail;
  /** crankcase: middle (y, z), half width, height, length; cylinders: number, lean forward (rad), length, radius; fins (air-cooled) */
  std::array<double, 5> crank;
  std::array<double, 4> cylinders;
  bool fins;
  /** the exhaust: where the silencer begins and ends, its radius */
  Vector3 silencer0;
  Vector3 silencer1;
  double silencerR;
  /** two shock absorbers at the back, else one under the seat */
  bool twinShock;
  /** the frame: steel tubes round the engine, a lattice of thin tubes, or two beams of aluminium ('tube' | 'lattice' | 'beam') */
  std::string frameKind;
  /** colours: paint, frame and swing arm, wheels, springs */
  int paint;
  int frame;
  int wheels;
  int spring;
};

const Build& BUILDS(const BikeVariant& variant) {
  static const std::map<std::string, Build> builds = {
      {"moped",
       {.spec = &MOPED.manual,
        .rake = 27 * DEG,
        .trail = 0.085,
        .tyres = {0.08, 0.095},
        .rim = 0.238,
        .wire = true,
        .spokes = 32,
        .split = false,
        .discs = 1,
        .disc = 0.13,
        .rearDisc = 0,
        .forkGap = 0.085,
        .forkR = 0.017,
        .usd = false,
        .clampY = 0.95,
        .grip = {0.33, 1.06, 0.0},
        .lampY = 0.9,
        .lampR = 0.072,
        .cowl = false,
        .screen = false,
        .seatY = 0.8,
        .hipZ = 0.17,
        .peg = {0.15, 0.32, 0.13},
        .lean = 10 * DEG,
        .tank = {{-0.42, 0.05, 0.92, 0.86}, {-0.39, 0.105, 0.955, 0.81}, {-0.3, 0.13, 0.985, 0.79}, {-0.18, 0.128, 0.99, 0.8}, {-0.08, 0.11, 0.975, 0.82}, {-0.02, 0.08, 0.945, 0.84}, {0.0, 0.04, 0.92, 0.86}},
        .seat = {{-0.03, 0.05, 0.86, 0.82}, {0.0, 0.09, 0.85, 0.79}, {0.08, 0.12, 0.818, 0.76}, {0.2, 0.13, 0.802, 0.75}, {0.36, 0.125, 0.806, 0.755}, {0.52, 0.11, 0.815, 0.765}, {0.62, 0.085, 0.815, 0.775}, {0.66, 0.045, 0.805, 0.785}},
        .tail = {{0.3, 0.12, 0.775, 0.66}, {0.45, 0.12, 0.785, 0.68}, {0.6, 0.1, 0.795, 0.71}, {0.7, 0.07, 0.795, 0.74}, {0.74, 0.04, 0.79, 0.76}},
        .crank = {0.35, 0.03, 0.095, 0.19, 0.25},
        .cylinders = {1, 1.12, 0.17, 0.042},
        .fins = true,
        .silencer0 = V(0.13, 0.42, 0.38),
        .silencer1 = V(0.14, 0.53, 0.7),
        .silencerR = 0.034,
        .twinShock = true,
        .frameKind = "tube",
        .paint = 0xbf242b,
        .frame = 0x26282c,
        .wheels = 0xc4c8cc,
        .spring = 0xb9bdc2}},
      {"light",
       {.spec = &MOTORCYCLE_125.manual,
        .rake = 26 * DEG,
        .trail = 0.092,
        .tyres = {0.1, 0.13},
        .rim = 0.216,
        .wire = false,
        .spokes = 5,
        .split = false,
        .discs = 1,
        .disc = 0.14,
        .rearDisc = 0.1,
        .forkGap = 0.092,
        .forkR = 0.019,
        .usd = false,
        .clampY = 0.97,
        .grip = {0.345, 1.075, 0.015},
        .lampY = 0.9,
        .lampR = 0.078,
        .cowl = false,
        .screen = false,
        .seatY = 0.815,
        .hipZ = 0.18,
        .peg = {0.16, 0.335, 0.15},
        .lean = 12 * DEG,
        .tank = {{-0.43, 0.055, 0.93, 0.86}, {-0.4, 0.12, 0.965, 0.8}, {-0.31, 0.15, 1.0, 0.78}, {-0.19, 0.148, 1.015, 0.785}, {-0.08, 0.125, 0.995, 0.81}, {-0.01, 0.09, 0.96, 0.84}, {0.015, 0.045, 0.93, 0.86}},
        .seat = {{-0.02, 0.05, 0.87, 0.83}, {0.01, 0.1, 0.86, 0.8}, {0.09, 0.135, 0.83, 0.77}, {0.2, 0.145, 0.815, 0.76}, {0.32, 0.14, 0.825, 0.765}, {0.4, 0.125, 0.85, 0.78}, {0.44, 0.115, 0.87, 0.8}, {0.58, 0.105, 0.88, 0.815}, {0.66, 0.08, 0.875, 0.825}, {0.7, 0.04, 0.865, 0.835}},
        .tail = {{0.3, 0.13, 0.8, 0.67}, {0.45, 0.13, 0.84, 0.7}, {0.6, 0.11, 0.86, 0.74}, {0.74, 0.08, 0.865, 0.78}, {0.82, 0.05, 0.86, 0.81}, {0.845, 0.03, 0.85, 0.825}},
        .crank = {0.36, 0.0, 0.115, 0.22, 0.3},
        .cylinders = {1, 0.3, 0.22, 0.05},
        .fins = true,
        .silencer0 = V(0.155, 0.35, 0.3),
        .silencer1 = V(0.17, 0.5, 0.66),
        .silencerR = 0.05,
        .twinShock = true,
        .frameKind = "tube",
        .paint = 0xeceeef,
        .frame = 0x1f5fa8,
        .wheels = 0x24262a,
        .spring = 0x1f5fa8}},
      {"mid",
       {.spec = &MOTORCYCLE_35KW.manual,
        .rake = 25 * DEG,
        .trail = 0.098,
        .tyres = {0.11, 0.16},
        .rim = 0.216,
        .wire = false,
        .spokes = 6,
        .split = false,
        .discs = 2,
        .disc = 0.148,
        .rearDisc = 0.11,
        .forkGap = 0.1,
        .forkR = 0.021,
        .usd = false,
        .clampY = 0.985,
        .grip = {0.35, 1.09, 0.035},
        .lampY = 0.93,
        .lampR = 0.08,
        .cowl = true,
        .screen = false,
        .seatY = 0.826,
        .hipZ = 0.19,
        .peg = {0.165, 0.355, 0.16},
        .lean = 14 * DEG,
        .tank = {{-0.445, 0.06, 0.935, 0.865}, {-0.42, 0.125, 0.97, 0.805}, {-0.34, 0.165, 1.01, 0.775}, {-0.23, 0.17, 1.04, 0.775}, {-0.12, 0.155, 1.04, 0.795}, {-0.03, 0.125, 1.01, 0.825}, {0.02, 0.085, 0.97, 0.855}, {0.035, 0.045, 0.94, 0.875}},
        .seat = {{-0.035, 0.055, 0.875, 0.835}, {-0.005, 0.1, 0.865, 0.8}, {0.07, 0.14, 0.84, 0.775}, {0.18, 0.155, 0.826, 0.765}, {0.29, 0.155, 0.83, 0.77}, {0.37, 0.14, 0.85, 0.78}, {0.41, 0.12, 0.88, 0.8}, {0.46, 0.115, 0.895, 0.825}, {0.58, 0.105, 0.902, 0.84}, {0.67, 0.08, 0.895, 0.845}, {0.71, 0.04, 0.882, 0.85}},
        .tail = {{0.3, 0.13, 0.8, 0.665}, {0.43, 0.14, 0.85, 0.695}, {0.57, 0.125, 0.87, 0.735}, {0.71, 0.09, 0.88, 0.78}, {0.82, 0.06, 0.88, 0.815}, {0.865, 0.035, 0.872, 0.835}},
        .crank = {0.375, -0.02, 0.15, 0.27, 0.36},
        .cylinders = {2, 0.45, 0.25, 0.062},
        .fins = false,
        .silencer0 = V(0.17, 0.28, 0.2),
        .silencer1 = V(0.19, 0.35, 0.46),
        .silencerR = 0.06,
        .twinShock = false,
        .frameKind = "lattice",
        .paint = 0xe3a51a,
        .frame = 0x2a2c30,
        .wheels = 0x2a2c30,
        .spring = 0xc0392b}},
      {"big",
       {.spec = &MOTORCYCLE,
        .rake = 24.5 * DEG,
        .trail = 0.1,
        .tyres = {0.12, 0.18},
        .rim = 0.216,
        .wire = false,
        .spokes = 3,
        .split = true,
        .discs = 2,
        .disc = 0.155,
        .rearDisc = 0.11,
        .forkGap = 0.105,
        .forkR = 0.022,
        .usd = true,
        .clampY = 1.0,
        .grip = {0.355, 1.1, 0.03},
        .lampY = 0.95,
        .lampR = 0.085,
        .cowl = true,
        .screen = true,
        .seatY = 0.835,
        .hipZ = 0.2,
        .peg = {0.17, 0.37, 0.17},
        .lean = 15 * DEG,
        .tank = {{-0.455, 0.06, 0.93, 0.87}, {-0.43, 0.13, 0.975, 0.81}, {-0.36, 0.172, 1.02, 0.775}, {-0.25, 0.178, 1.055, 0.77}, {-0.14, 0.165, 1.06, 0.79}, {-0.05, 0.135, 1.035, 0.82}, {0.01, 0.1, 0.99, 0.85}, {0.03, 0.05, 0.95, 0.88}},
        .seat = {{-0.04, 0.06, 0.875, 0.83}, {-0.01, 0.1, 0.87, 0.8}, {0.06, 0.135, 0.85, 0.78}, {0.16, 0.155, 0.835, 0.77}, {0.28, 0.16, 0.838, 0.775}, {0.36, 0.15, 0.855, 0.785}, {0.41, 0.13, 0.885, 0.8}, {0.45, 0.125, 0.905, 0.83}, {0.58, 0.115, 0.912, 0.845}, {0.68, 0.09, 0.905, 0.85}, {0.72, 0.05, 0.89, 0.855}},
        .tail = {{0.3, 0.13, 0.8, 0.66}, {0.42, 0.14, 0.86, 0.69}, {0.56, 0.125, 0.875, 0.73}, {0.7, 0.095, 0.885, 0.775}, {0.82, 0.06, 0.885, 0.815}, {0.875, 0.035, 0.875, 0.835}},
        .crank = {0.385, -0.03, 0.17, 0.3, 0.4},
        .cylinders = {4, 0.5, 0.27, 0.06},
        .fins = false,
        .silencer0 = V(0.2, 0.3, 0.3),
        .silencer1 = V(0.225, 0.44, 0.6),
        .silencerR = 0.065,
        .twinShock = false,
        .frameKind = "beam",
        .paint = 0x1d5fb0,
        .frame = 0xa4a9af,
        .wheels = 0x26282c,
        .spring = 0xd9a21b}},
  };
  return builds.at(variant);
}

/** The number plate: the usual two lines, but no real registration. */
TexturePtr plateTexture() {
  const Canvas c(256, 192);
  const Ctx2D x = c.getContext2D();
  x.fillStyle("#f2f2ee");
  x.fillRect(0, 0, 256, 192);
  x.strokeStyle("#151515");
  x.lineWidth(6);
  x.strokeRect(3, 3, 250, 186);
  x.fillStyle("#1b3fa0");
  x.fillRect(6, 6, 34, 180);
  x.fillStyle("#151515");
  x.font("700 72px \"DIN Alternate\", \"Bahnschrift\", \"Arial Narrow\", Arial, sans-serif");
  x.textAlign("center");
  x.textBaseline("middle");
  x.fillText("FS", 148, 56, 200);
  x.fillText("2026", 148, 138, 200);
  const TexturePtr t = canvasTexture(c);
  t->srgb = true;
  t->anisotropy = 4;
  return t;
}

/**
 * Keep a part of the rider's head out of any picture taken from inside the
 * head – the rider's own eyes –, while it still throws its shadow (the shadow
 * pass does not call onBeforeRender) and still shows in the mirrors.
 */
void ownHead(const std::shared_ptr<Mesh>& mesh, const Vector3& centre, double reach) {
  Mesh* me = mesh.get();
  const MaterialPtr m = mesh->material;
  struct State {
    Vector3 at;
    Vector3 cam;
    bool hidden = false;
  };
  const auto st = std::make_shared<State>();
  mesh->onBeforeRender = [me, m, st, centre, reach](Renderer&, Scene&, Camera& camera) {
    st->at.copy(centre).applyMatrix4(me->matrixWorld);
    st->hidden = st->cam.setFromMatrixPosition(camera.matrixWorld).distanceToSquared(st->at) < reach * reach;
    if (st->hidden) {
      m->colorWrite = false;
      m->depthWrite = false;
    }
  };
  mesh->onAfterRender = [m, st](Renderer&, Scene&, Camera&) {
    if (!st->hidden) return;
    m->colorWrite = true;
    m->depthWrite = true;
    st->hidden = false;
  };
}

const double UPPER_ARM = 0.29;
const double FOREARM = 0.26;
/** how far an arm reaches, just short of straight (a straight arm would look locked) */
const double ARM_REACH = (UPPER_ARM + FOREARM) * 0.998;
const double THIGH = 0.45;
const double SHIN = 0.44;
/** how high the ankle stands above the boot's sole */
const double ANKLE = 0.088;

// ------------------------------------------------------------------------- the rider's movements
// What the rider does is worked out from a handful of numbers (RiderMotion, plain arithmetic that the
// tests can drive), then put on the model in PlayerBike.update.

/** how far ahead the rider looks through a bend – far, as he is taught: this many seconds of his way, but no nearer and no further than … (m) */
const double LOOK_AHEAD = 3;
const double LOOK_NEAR = 6;
const double LOOK_FAR = 80;
/** the furthest he turns to look through a bend (rad): a turn at full lock at walking pace */
const double LOOK_MAX = 0.9;
/** the shoulder check: how far he looks round, head and shoulders together (rad), and how long it takes (s) */
const double CHECK_TURN = 1.45;
const double CHECK_TIME = 1.2;
/** how far the motorcycle settles on to his foot on the road (rad) */
const double FOOT_TILT = 0.035;

double finite(double x) { return std::isfinite(x) ? x : 0; }
double smoothstep(double e0, double e1, double x) {
  const double t = clamp((x - e0) / (e1 - e0), 0, 1);
  return t * t * (3 - 2 * t);
}
/** x on its way to `to`, at `rate` (1/s) over dt – the same however the frames fall */
double follow(double x, double to, double rate, double dt) { return x + (to - x) * (1 - std::exp(-rate * dt)); }

/** A number 0 … 1 that looks random – always the same for the same n. */
double hash(double n) {
  const double x = std::sin(n * 127.1 + 311.7) * 43758.5453;
  return x - std::floor(x);
}

/**
 * Now and then a glance, worked out from the clock alone (nothing else to remember): time is cut into
 * slots of `slot` s, and in a slot, by the chance `p`, comes one glance of `len` s at a moment of its
 * own.  Its strength −1 … 1, the sign the side; smooth, and 0 between glances.
 */
double glance(double now, double slot, double p, double len, double seed) {
  const double n = std::floor(now / slot);
  if (hash(n + seed) > p) return 0;
  const double u = (now - n * slot - hash(n + seed + 0.37) * (slot - len)) / len;
  if (u <= 0 || u >= 1) return 0;
  const double side = hash(n + seed + 0.71);
  return (side < 0.5 ? -1 : 1) * (0.55 + 0.9 * std::fabs(side - 0.5)) * smoothstep(0, 0.22, u) * (1 - smoothstep(0.68, 1, u));
}

/** Standing upright, nothing turned: the pose the model is built in. */
CarVisualState standingState(double r) {
  CarVisualState s;
  s.speedKmh = 0;
  s.rpm = 0;
  s.gear = 0;
  s.indicator = 0;
  s.hazard = false;
  s.blinkOn = false;
  s.lowBeam = true;
  s.highBeam = false;
  s.handbrake = false;
  s.ignition = false;
  s.engineRunning = false;
  s.abs = false;
  s.seatbelt = true;
  s.fuel = 1;
  s.automatic = false;
  s.showGear = false;
  s.odometerKm = 0;
  s.steerWheel = 0;
  s.wheelSteer = {0, 0, 0, 0};
  s.wheelSpin = {0, 0, 0, 0};
  s.wheelY = {r, r, r, r};
  s.clutch = 0;
  s.brake = 0;
  s.throttle = 0;
  s.handbrakeLever = 0;
  s.braking = false;
  s.reversing = false;
  return s;
}

/** (`a[i] ?? d`) */
double at(const std::vector<double>& a, size_t i, double d) { return i < a.size() ? a[i] : d; }

}  // namespace

// ------------------------------------------------------------------------- RiderMotion

RiderMotion::RiderMotion() : checkT_(CHECK_TIME) { pose.tilt = -FOOT_TILT; }

const RiderPose& RiderMotion::step(const RiderInput& i, double dt) {
  RiderPose& p = pose;
  const double h = std::isfinite(dt) && dt > 0 ? jsMin(dt, 60) : 0;
  const double t = (clock_ += h);
  const double kmh = std::fabs(finite(i.speedKmh));
  const double lean = clamp(finite(i.lean), -0.8, 0.8);
  const double bar = clamp(finite(i.bar), -0.8, 0.8);
  const double steer = clamp(finite(i.steer), -0.7, 0.7);
  if (kmh < 3) stopped_ = true;
  else if (kmh > 6) stopped_ = false;
  still_ = follow(still_, stopped_ ? 1 : 0, 2, h);
  const double s = still_;
  const double moving = smoothstep(1, 6, kmh);

  // where he looks: through the bend to a point of his way ahead (half the turn of the arc up to it)
  double look;
  if (i.reversing) look = steer > 0.05 ? 1.2 : -1.2;   // over the shoulder to where the rear goes
  else {
    const double ahead = clamp((kmh / 3.6) * LOOK_AHEAD, LOOK_NEAR, LOOK_FAR);
    const double curve = std::tan(steer) / jsMax(0.5, finite(i.wheelbase));
    look = clamp(curve * ahead * 0.5, -LOOK_MAX, LOOK_MAX) * (0.5 + 0.5 * moving);
  }
  // now and then a glance: round about while it stands (or down at the instruments; not while he pushes it
  // back, looking over his shoulder), into a mirror riding straight on
  const double round = i.reversing ? 0 : glance(t, 4.5, 0.55, 1.6, 11);
  const double down = i.reversing ? 0 : std::fabs(glance(t, 5.3, 0.3, 1.3, 29)) * (1 - std::fabs(round));
  const double mirror = glance(t, 8, 0.35, 0.9, 47) * (1 - smoothstep(0.08, 0.25, std::fabs(look)));
  through_ = follow(through_, look, 5, h);
  view_ = follow(view_, look + s * 0.6 * round + (1 - s) * 0.38 * mirror, 5, h);
  nod_ = follow(nod_, s * 0.35 * down + (1 - s) * 0.2 * std::fabs(mirror), 5, h);

  // a shoulder check when an indicator comes on (or changes side); c: how far into it, 0 … 1
  const double ind = sign(finite(i.indicator));
  if (ind != 0 && ind != lastIndicator_) {
    checkSide_ = ind;
    checkT_ = 0;
  } else checkT_ = jsMin(CHECK_TIME, checkT_ + h);
  lastIndicator_ = ind;
  const double c = smoothstep(0, 0.3, checkT_) * (1 - smoothstep(0.75, CHECK_TIME, checkT_));

  // hard on the throttle he tucks in (and very fast out of the wind); braking he braces himself on the bar
  const double thr = clamp(finite(i.throttle), 0, 1);
  const double brk = clamp(finite(i.brake), 0, 1);
  const double hard = brk < 0.05 ? smoothstep(0.45, 0.95, thr) : 0;
  tuck_ = follow(tuck_, moving * jsMax(hard, clamp((kmh - 90) / 70, 0, 0.6)), 3, h);
  brace_ = follow(brace_, moving * smoothstep(0.1, 0.7, brk), 5, h);

  // the feet: the left one on the road while it stands (the right stays on the brake pedal), both pushed backwards
  p.footL = follow(p.footL, stopped_ || i.reversing ? 1 : 0, 4, h);
  p.footR = follow(p.footR, i.reversing ? 1 : 0, 4, h);
  p.tilt = FOOT_TILT * (p.footR - p.footL);

  // the body: into the bend a little more than the machine, the hips across the seat, the inside knee out
  const double side = sign(lean);
  const double hang = smoothstep(0.2, 0.6, std::fabs(lean));
  const double open = smoothstep(0.3, 0.62, std::fabs(lean));
  p.hipShift = side * hang * 0.055;
  p.hipSlide = 0.025 * brace_;
  p.kneeL = lean < 0 ? open : 0;
  p.kneeR = lean > 0 ? open : 0;
  // (upright while the machine settles on to his foot; the shoulders follow the bar – reaching for the
  // grip ahead – and a little the look through the bend)
  p.eyeRoll = 0.15 * lean + side * hang * 0.08 - p.tilt;
  p.eyePitch = 0.22 * std::fabs(bar) + 0.13 * tuck_ - 0.1 * brace_;
  p.eyeYaw = 0.42 * bar + 0.12 * through_;
  // (and besides: the shoulders go a little with a glance and with the shoulder check; breathing, and
  // shifting his weight while he waits)
  p.torsoRoll = p.eyeRoll + s * 0.015 * std::sin(t * 0.23 + 2);
  p.torsoPitch = p.eyePitch + 0.008 * std::sin(t * 1.55);
  p.torsoYaw = p.eyeYaw + 0.12 * (view_ * (1 - c) - through_) + 0.15 * checkSide_ * c + s * 0.03 * std::sin(t * 0.31 + 0.7);
  // (half of what riding asks of his body is in the hips – rolling on the seat on to the inside buttock, tipping
  // forwards to tuck in, turning with the bar –, the back bends the rest; his looking round is all in the back)
  p.hipRoll = 0.5 * p.eyeRoll;
  p.hipPitch = 0.5 * p.eyePitch;
  p.hipYaw = 0.5 * p.eyeYaw;
  p.elbows = tuck_ - 0.6 * brace_;

  // the head: where he looks less the turn of his shoulders – the shoulder check takes over while it lasts;
  // half the lean of the shoulders taken back, a little tilt into a turn he looks into; still looking ahead
  // when the upper body bends forwards or back
  const double turn = view_ + (CHECK_TURN * checkSide_ - view_) * c;
  p.headYaw = clamp(turn - p.torsoYaw, -1.35, 1.35);
  p.headRoll = -0.5 * (lean + p.tilt + p.torsoRoll) + 0.12 * view_ * (1 - c) + s * 0.03 * std::sin(t * 0.37 + 1);
  p.headPitch = -0.85 * p.torsoPitch + nod_ + 0.06 * c;
  return p;
}

// ------------------------------------------------------------------------- PlayerBike

PlayerBike::~PlayerBike() = default;

PlayerBike::PlayerBike(const BikeVariant& variant_) : variant(variant_) {
  const Mats& K = M();
  const Build& b = BUILDS(variant);
  Group& rootG = *root;
  rootG.name = "motorcycle";
  const double R = (radius_ = b.spec->wheelRadius);
  const double zF = (zFront_ = -b.spec->wheelbase / 2);
  const double zR = (zRear_ = b.spec->wheelbase / 2);
  input_.wheelbase = b.spec->wheelbase;
  const double rake = (rake_ = b.rake);
  const double tanRake = std::tan(rake);
  const Vector3 axis = (axis_ = V(0, std::cos(rake), std::sin(rake)));
  /** square to the axis, forwards and up */
  const Vector3 ahead = V(0, std::sin(rake), -std::cos(rake));
  const double zFoot = zF - b.trail;
  const auto onAxis = [&](double y, double x = 0) { return V(x, y, zFoot + y * tanRake); };
  // the fork legs run parallel to the axis, through the axle
  const auto onFork = [&](double y, double x = 0) { return V(x, y, zF + (y - R) * tanRake); };
  const Vector3 P = onAxis(b.clampY);
  const Vector3 axle = V(0, R, zF);
  const double forkTop = P.clone().sub(axle).dot(axis);
  const auto along = [&](double s, double x = 0) { return axle.clone().addScaledVector(axis, s).setX(x); };
  const double tf = b.tyres[0], tr = b.tyres[1];
  const MaterialPtr paint = lambert({.color = Color(b.paint)});
  const MaterialPtr frameMat = lam(b.frame);
  const MaterialPtr wheelMat = b.wire ? K.CHROME : lam(b.wheels);
  const MaterialPtr springMat = lam(b.spring);

  // ---------------------------------------------------------------- the rider's posture
  const double leanB = b.lean;
  const Vector3 up = V(0, std::cos(leanB), -std::sin(leanB));
  const Vector3 fwd = V(0, -std::sin(leanB), -std::cos(leanB));
  const Vector3 hip = V(0, b.seatY + 0.095, b.hipZ);
  const Vector3 shoulders = hip.clone().addScaledVector(up, 0.5);
  const Vector3 head = shoulders.clone().add(V(0, 0.205, -0.045));
  eye.copy(shoulders).add(V(0, 0.07, -0.04));

  Parts body;
  Parts front;
  Parts slider;
  Parts wheelF;
  Parts wheelR;
  Parts swing;

  // ---------------------------------------------------------------- wheels
  const auto tyre = [&](double w) {
    // a round crown over ±72°, sidewalls down to the beads on the rim
    const double th = 1.25;
    const double cr = w / (2 * std::sin(th));
    Outline prof = {{b.rim - 0.006, -w * 0.4}};
    prof.push_back({b.rim + 0.012, -w * 0.47});
    prof.push_back({(b.rim + R - cr * (1 - std::cos(th))) / 2 + 0.004, -w * 0.505});
    for (int i = 0; i <= 10; i++) {
      const double t = -th + (2 * th * i) / 10;
      prof.push_back({R - cr + cr * std::cos(t), cr * std::sin(t)});
    }
    prof.push_back({(b.rim + R - cr * (1 - std::cos(th))) / 2 + 0.004, w * 0.505});
    prof.push_back({b.rim + 0.012, w * 0.47});
    prof.push_back({b.rim - 0.006, w * 0.4});
    return turned(prof, V(), X, 40);
  };
  const auto rimHoop = [&](double w) {
    const double a = w * 0.42;
    const double r = b.rim;
    return turned({{r - 0.016, -a}, {r + 0.006, -a}, {r + 0.006, -a + 0.007}, {r - 0.003, -a + 0.012}, {r - 0.003, a - 0.012}, {r + 0.006, a - 0.007}, {r + 0.006, a}, {r - 0.016, a}, {r - 0.016, -a}}, V(), X, 40, true);
  };
  const auto hub = [&](double half, double flange) {
    return turned({{0, -half}, {0.03, -half}, {0.034, -half + 0.008}, {flange, -half + 0.012}, {flange, -half + 0.022}, {0.04, -half + 0.03}, {0.04, half - 0.03}, {flange, half - 0.022}, {flange, half - 0.012}, {0.034, half - 0.008}, {0.03, half}, {0, half}}, V(), X, 20);
  };
  const auto disc = [&](double at_, double r, double carrier) {
    return std::pair<GeometryPtr, GeometryPtr>{
        turned({{r - 0.042, at_ - 0.0028}, {r, at_ - 0.0028}, {r, at_ + 0.0028}, {r - 0.042, at_ + 0.0028}, {r - 0.042, at_ - 0.0028}}, V(), X, 40, true),
        turned({{0.04, at_ - 0.002}, {carrier, at_ - 0.002}, {carrier, at_ + 0.002}, {0.04, at_ + 0.002}, {0.04, at_ - 0.002}}, V(), X, 20, true),
    };
  };
  const auto castSpokes = [&](int n, bool split, double width) {
    Geos out;
    const double r0 = 0.036;
    const double r1 = b.rim - 0.012;
    const auto radial = [](double r, double a) { return V(0, std::cos(a) * r, std::sin(a) * r); };
    for (int i = 0; i < n; i++) {
      const double a = (static_cast<double>(i) / n) * PI * 2;
      // (the first measure of a section is across the wheel, the second round it)
      if (!split) {
        out.push_back(sweep({radial(r0, a), radial(r1, a)}, [&](double t) { return rounded(width, 0.03 - 0.01 * t, 0.007); }, 2, X));
        continue;
      }
      const double rm = r0 + (r1 - r0) * 0.42;
      out.push_back(sweep({radial(r0, a), radial(rm, a)}, [&](double) { return rounded(width, 0.034, 0.008); }, 1, X));
      for (const double da : {-0.17, 0.17}) out.push_back(sweep({radial(rm - 0.01, a), radial(r1, a + da)}, [&](double t) { return rounded(width * 0.85, 0.022 - 0.006 * t, 0.006); }, 2, X));
    }
    return out;
  };
  const auto wireSpokes = [&](int n, double half) {
    Geos out;
    for (int i = 0; i < n; i++) {
      const double a = (static_cast<double>(i) / n) * PI * 2;
      const double s = i % 2 ? 1 : -1;
      const double h = a + (i % 4 < 2 ? 0.42 : -0.42);
      out.push_back(rod(V(s * (half - 0.018), std::cos(h) * 0.045, std::sin(h) * 0.045), V(s * 0.004, std::cos(a) * (b.rim - 0.008), std::sin(a) * (b.rim - 0.008)), 0.0018, 3));
    }
    return out;
  };
  // the front wheel: discs on one or both sides, inside the fork legs
  wheelF.add(K.TYRE, tyre(tf));
  wheelF.add(wheelMat, rimHoop(tf));
  const double halfF = b.forkGap - 0.035;
  wheelF.add(b.wire ? K.ALU : wheelMat, hub(halfF, b.wire ? 0.05 : 0.044));
  wheelF.add(wheelMat, b.wire ? wireSpokes(b.spokes, halfF) : castSpokes(b.spokes, b.split, 0.026));
  const std::vector<double> discSides = b.discs > 1 ? std::vector<double>{-1, 1} : std::vector<double>{-1};
  for (const double s : discSides) {
    const auto [ring_, carrier] = disc(s * (halfF - 0.006), b.disc, b.disc - 0.034);
    wheelF.add(K.STEEL, ring_);
    wheelF.add(b.wire ? K.ALU : K.BLACK, carrier);
  }
  // the rear wheel: the disc on the right, the sprocket on the left (or a drum brake)
  const double halfR = tr / 2 + 0.03;
  wheelR.add(K.TYRE, tyre(tr));
  wheelR.add(wheelMat, rimHoop(tr));
  wheelR.add(b.wire ? K.ALU : wheelMat, hub(halfR, b.wire ? 0.05 : 0.046));
  wheelR.add(wheelMat, b.wire ? wireSpokes(b.spokes, halfR) : castSpokes(b.spokes, b.split, 0.03));
  if (b.rearDisc > 0) {
    const auto [ring_, carrier] = disc(halfR - 0.004, b.rearDisc, b.rearDisc - 0.03);
    wheelR.add(K.STEEL, ring_);
    wheelR.add(K.BLACK, carrier);
  } else {
    wheelR.add(K.ALU, turned({{0.03, halfR - 0.01}, {0.085, halfR - 0.01}, {0.085, halfR + 0.012}, {0.03, halfR + 0.012}, {0.03, halfR - 0.01}}, V(), X, 24, true));
  }
  const double sprocketR = 0.085 + 0.02 * (tr / 0.18);
  const int teeth = 40;
  Outline toothed;
  for (int i = 0; i < teeth * 2; i++) {
    const double a = (static_cast<double>(i) / (teeth * 2)) * PI * 2;
    const double r = i % 2 ? sprocketR : sprocketR - 0.008;
    toothed.push_back({std::cos(a) * r, std::sin(a) * r});
  }
  const double chainX = -(tr / 2 + 0.022);
  wheelR.add(K.STEEL, loft({ring(V(chainX - 0.003, 0, 0), Y, Z, toothed), ring(V(chainX + 0.003, 0, 0), Y, Z, toothed)}));
  wheelR.add(K.BLACK, turned({{0.03, chainX + 0.003}, {0.055, chainX + 0.004}, {0.055, -halfR + 0.004}, {0.03, -halfR + 0.002}}, V(), X, 16));

  // ---------------------------------------------------------------- the front: fork, clamps, bar, lamp, instruments
  const double fr = b.forkR;
  for (const double s : {-1.0, 1.0}) {
    const double x = s * b.forkGap;
    if (b.usd) {
      // thick tubes in the clamps, thin gold ones sliding in them, the axle clamp at their foot
      front.add(K.BLACK, limb(along(0.3, x), along(forkTop + 0.012, x), fr + 0.009, fr + 0.009));
      front.add(K.ALU, limb(along(forkTop - 0.005, x), along(forkTop + 0.02, x), fr + 0.006, fr + 0.004));
      slider.add(K.GOLD, rod(along(0.03, x), along(0.36, x), fr - 0.001, 14));
      slider.add(K.BLACK, limb(along(-0.035, x), along(0.07, x), fr + 0.006, fr + 0.004));
    } else {
      front.add(K.CHROME, rod(along(0.27, x), along(forkTop + 0.008, x), fr, 14));
      front.add(K.BLACK, limb(along(forkTop - 0.004, x), along(forkTop + 0.016, x), fr - 0.002, fr - 0.004));
      slider.add(b.wire ? K.ALU : K.BLACK, limb(along(-0.03, x), along(0.33, x), fr + 0.009, fr + 0.008));
    }
  }
  // the axle and the brake callipers behind the discs
  slider.add(K.STEEL, rod(V(-b.forkGap - 0.012, R, zF), V(b.forkGap + 0.012, R, zF), 0.011, 10));
  for (const double s : discSides) {
    const double a = 0.42;
    const double r = b.disc - 0.024;
    slider.add(b.usd ? K.GOLD : K.ALU, soap(V(s * (halfF - 0.006), R + std::sin(a) * r, zF + std::cos(a) * r), 0.045, 0.085, 0.04, 0.012, Euler(-a, 0, 0)));
  }
  // the mudguard over the front tyre: a strip curved across, its edges drawn down round the tyre
  {
    const double g = R + 0.028;
    std::vector<Vector3> arc;
    for (int i = 0; i <= 8; i++) {
      const double t = -0.55 + (1.7 * i) / 8;
      arc.push_back(V(0, R + std::cos(t) * g, zF - std::sin(t) * g));
    }
    slider.add(paint, sweep(arc, [&](double) { return strip(tf / 2 + 0.016, 0.014, 0.004); }, 12, X));
  }
  // the triple clamps
  const auto clamp_ = [&](double s, double thick) {
    const Vector3 c = along(s).addScaledVector(ahead, -0.012);
    const Outline outline = rounded(2 * (b.forkGap + fr + 0.016), 0.032 + fr * 2 + 0.03, 0.03);
    return loft({ring(c, X, ahead, outline), ring(c.clone().addScaledVector(axis, -thick), X, ahead, outline)});
  };
  front.add(b.usd ? K.ALU : K.BLACK, clamp_(forkTop, 0.03));
  front.add(K.BLACK, clamp_(forkTop - 0.2, 0.04));
  // the handlebar on its clamps, the grips, levers and switches
  const double gx = b.grip[0], gy = b.grip[1], gb = b.grip[2];
  const double gz = P.z + gb;
  const double cy = b.clampY + 0.055;
  for (const double s : {-1.0, 1.0}) front.add(K.BLACK, soap(V(s * 0.04, b.clampY + 0.028, P.z), 0.032, 0.05, 0.05, 0.01));
  std::vector<Vector3> barPath;
  for (const double s : {-1.0, 1.0}) {
    std::vector<Vector3> pts = {V(s * (gx + 0.062), gy + 0.005, gz + 0.01), V(s * (gx - 0.07), gy - 0.003, gz - 0.006), V(s * 0.17, cy + 0.006, P.z - 0.002)};
    if (s > 0) std::reverse(pts.begin(), pts.end());
    barPath.insert(barPath.end(), pts.begin(), pts.end());
  }
  barPath.insert(barPath.begin() + 3, V(0, cy, P.z - 0.004));
  front.add(b.usd ? K.BLACK : K.CHROME, sweep(barPath, [](double) { return circle(0.011, 10); }, 40));
  for (const double s : {-1.0, 1.0}) {
    front.add(K.BLACK, limb(V(s * (gx - 0.066), gy - 0.002, gz - 0.005), V(s * (gx + 0.056), gy + 0.005, gz + 0.009), 0.017, 0.018));
    front.add(K.ALU, limb(V(s * (gx + 0.057), gy + 0.005, gz + 0.01), V(s * (gx + 0.07), gy + 0.006, gz + 0.012), 0.016, 0.016));
    // the switch block inside each grip, the lever on its perch in front of it
    front.add(K.BLACK, soap(V(s * (gx - 0.09), gy - 0.002, gz - 0.006), 0.034, 0.042, 0.048, 0.012));
    front.add(K.BLACK, soap(V(s * (gx - 0.125), gy + 0.004, gz - 0.012), 0.04, 0.03, 0.036, 0.01));
    const std::vector<Vector3> lever = {V(s * (gx - 0.115), gy - 0.004, gz - 0.03), V(s * (gx - 0.07), gy - 0.012, gz - 0.066), V(s * (gx + 0.04), gy - 0.016, gz - 0.078)};
    front.add(K.ALU, sweep(lever, [](double) { return rounded(0.012, 0.007, 0.003); }, 10, Y));
  }
  // the reservoir of the front brake on the right
  front.add(K.BLACK, soap(V(gx - 0.145, gy + 0.032, gz - 0.01), 0.04, 0.026, 0.032, 0.008));

  // headlamp, its brackets or cowl, the instruments, the front indicators
  const Vector3 lampAt = onFork(b.lampY).add(V(0, 0, -0.135));
  const double lr = b.lampR;
  const Vector3 facing = V(0, 0, -1);
  if (b.cowl) {
    // a cowl round the lamp that rises to carry the instruments
    const double z0 = lampAt.z + 0.004;
    const std::vector<Station> st = {
        {z0, lr + 0.018, lampAt.y + lr + 0.03, lampAt.y - lr - 0.018},
        {z0 + 0.04, lr + 0.035, lampAt.y + lr + 0.085, lampAt.y - lr - 0.03},
        {z0 + 0.1, lr + 0.04, lampAt.y + lr + 0.105, lampAt.y - lr - 0.02},
        {z0 + 0.15, lr + 0.025, lampAt.y + lr + 0.09, lampAt.y - lr + 0.01},
    };
    front.add(paint, stations(st, 22, 0.6));
    front.add(K.BLACK, turned({{lr + 0.004, -0.004}, {lr + 0.012, -0.004}, {lr + 0.012, 0.012}, {lr + 0.004, 0.012}, {lr + 0.004, -0.004}}, lampAt, facing, 32, true));
    // the lens: a lit ring round a dark reflector with a lit projector in its middle
    front.add(K.BLACK, turned({{0, 0.006}, {lr * 0.95, 0.004}, {lr + 0.004, -0.002}}, lampAt, facing, 32));
    front.add(LAMP(), turned({{lr * 0.72, 0.009}, {lr * 0.8, 0.011}, {lr * 0.88, 0.009}, {lr * 0.8, 0.006}, {lr * 0.72, 0.009}}, lampAt, facing, 32));
    front.add(LAMP(), turned({{0, 0.016}, {lr * 0.3, 0.013}, {lr * 0.34, 0.006}}, lampAt, facing, 20));
    if (b.screen) {
      // a small tinted screen above it
      const std::vector<Vector3> path = {V(0, lampAt.y + lr + 0.06, z0 + 0.02), V(0, lampAt.y + lr + 0.15, z0 + 0.045), V(0, lampAt.y + lr + 0.21, z0 + 0.075)};
      front.add(K.SMOKE, sweep(path, [](double t) { return strip(0.1 + 0.025 * t, 0.03, 0.004); }, 8, X));
    }
  } else {
    // a round lamp in a chrome bucket on two brackets from the fork
    front.add(K.CHROME, turned({{0, -0.115}, {0.03, -0.11}, {lr * 0.7, -0.09}, {lr * 0.97, -0.04}, {lr + 0.006, 0.0}, {lr + 0.008, 0.012}, {lr - 0.004, 0.016}}, lampAt, facing, 32));
    front.add(LAMP(), turned({{0, 0.03}, {lr * 0.5, 0.026}, {lr * 0.85, 0.018}, {lr - 0.002, 0.012}}, lampAt, facing, 32));
    for (const double s : {-1.0, 1.0})
      front.add(K.BLACK, pipe({onFork(b.lampY + 0.05, s * (b.forkGap + fr * 0.6)), V(s * (lr + 0.022), lampAt.y + 0.01, lampAt.z + 0.08), V(s * (lr + 0.004), lampAt.y, lampAt.z + 0.05)}, 0.008, 8, 8));
  }
  for (const double s : {-1.0, 1.0}) {
    const Vector3 base = V(s * (lr + 0.03), lampAt.y - 0.01, lampAt.z + 0.07);
    const Vector3 tip = V(s * (lr + 0.09), lampAt.y - 0.012, lampAt.z + 0.055);
    front.add(K.BLACK, rod(base, tip, 0.006, 6));
    const auto ind = make<Mesh>(blob(tip.clone().add(V(0, 0, -0.012)).sub(P), 0.017, 0.016, 0.03), K.DIM);
    lamps_.indicators.push_back({ind, static_cast<int>(s)});
    front_->add(ind);
  }
  // the instruments: the game's picture in a housing that faces the rider's eyes
  const double dashW = b.cowl ? 0.22 : 0.18;
  const Vector3 dashAt = b.cowl ? V(0, lampAt.y + lr + 0.115, lampAt.z + 0.1) : V(0, gy + 0.05, P.z - 0.1);
  const Vector3 toEye = eye.clone().sub(dashAt).normalize();
  const double tilt = std::atan2(toEye.y, toEye.z);
  const Euler tiltRot(-tilt, 0, 0, Euler::YXZ);
  front.add(K.BLACK, soap(dashAt.clone().addScaledVector(toEye, -0.022), dashW + 0.026, dashW / 3 + 0.028, 0.04, 0.012, tiltRot));
  if (!b.cowl) {
    // its bracket from the top clamp
    front.add(K.BLACK, pipe({P.clone().add(V(0, 0.01, -0.02)), V(0, (P.y + dashAt.y) / 2, (P.z + dashAt.z) / 2 - 0.01), dashAt.clone().addScaledVector(toEye, -0.04)}, 0.009, 6, 8));
  }
  const auto dash = make<Mesh>(planeGeometry(dashW, dashW / 3), basic({.map = panel_.texture, .toneMapped = false}));
  dash->name = "instruments";
  dash->position.copy(dashAt).addScaledVector(toEye, 0.001).sub(P);
  dash->rotation.x = -tilt;
  front_->add(dash);

  // the mirrors on stalks from the perches near the bar ends
  for (const double s : {-1.0, 1.0}) {
    const Vector3 c = V(s * (gx + 0.01), eye.y - 0.14, P.z - 0.115);
    Vector3 toE = eye.clone().sub(c).normalize();
    const Vector3 look = V(s * 0.1, -0.03, 1).normalize();
    const Vector3 normal = toE.add(look).normalize();
    const double w = 0.12;
    const double h = 0.07;
    auto mirror = std::make_unique<Mirror>(MirrorSpec{.name = s < 0 ? "left" : "right", .centre = c.clone().sub(P), .width = w, .height = h, .normal = normal, .convex = 1.6, .resolution = {192, 112}});
    front_->add(mirror->mesh);
    mirrors.push_back(std::move(mirror));
    // the housing behind the glass: rounded, aligned with it
    const Vector3 mUp = V(0, 1, 0).addScaledVector(normal, -normal.y).normalize();
    const Vector3 mRight = Vector3().crossVectors(mUp, normal).normalize();
    const auto shell = [&](double depth, double grow) { return ring(c.clone().addScaledVector(normal, -depth), mRight, mUp, rounded(w + grow, h + grow, 0.03, 3)); };
    front.add(K.BLACK, loft({shell(0.001, 0.016), shell(0.012, 0.02), shell(0.03, 0.012), shell(0.045, -0.03)}));
    const Vector3 mount = c.clone().addScaledVector(normal, -0.03).addScaledVector(mRight, -s * 0.035).addScaledVector(mUp, -0.03);
    const Vector3 perch = V(s * (gx - 0.125), gy + 0.018, gz - 0.012);
    front.add(K.BLACK, pipe({perch, perch.clone().add(V(s * 0.015, 0.07, -0.025)), mount}, 0.0065, 10, 8));
  }

  // ---------------------------------------------------------------- frame, tank, seat, tail
  const Vector3 pivot = V(0, 0.41, 0.115);
  const Vector3 head0 = onAxis(b.clampY - 0.045);
  const Vector3 head1 = onAxis(b.clampY - 0.185);
  body.add(frameMat, limb(head1, head0, 0.034, 0.034));
  if (b.frameKind == "beam") {
    for (const double s : {-1.0, 1.0}) {
      const std::vector<Vector3> beam = {head0.clone().add(V(s * 0.045, -0.01, 0.025)), V(s * 0.17, 0.8, -0.26), V(s * 0.19, 0.76, -0.1), V(s * 0.17, 0.67, 0.05), V(s * 0.14, 0.52, 0.11)};
      body.add(frameMat, sweep(beam, [](double) { return rounded(0.036, 0.095, 0.014); }, 20));
      body.add(frameMat, soap(V(s * 0.14, 0.45, 0.115), 0.04, 0.2, 0.11, 0.02));
      body.add(frameMat, pipe({head1.clone().add(V(s * 0.03, 0, 0.01)), V(s * 0.13, 0.66, -0.3)}, 0.015, 4, 8));
    }
  } else if (b.frameKind == "lattice") {
    for (const double s : {-1.0, 1.0}) {
      const Vector3 A = head0.clone().add(V(s * 0.04, 0, 0.015));
      const Vector3 B = head1.clone().add(V(s * 0.04, 0, 0.005));
      const Vector3 C = V(s * 0.155, 0.81, -0.2);
      const Vector3 D = V(s * 0.15, 0.62, -0.25);
      const Vector3 E = V(s * 0.15, 0.79, 0.0);
      const Vector3 F = V(s * 0.145, 0.58, -0.03);
      const Vector3 G = V(s * 0.135, 0.52, 0.11);
      const std::pair<const Vector3*, const Vector3*> bars[] = {{&A, &C}, {&B, &D}, {&B, &C}, {&C, &D}, {&C, &E}, {&D, &F}, {&C, &F}, {&E, &F}, {&E, &G}, {&F, &G}};
      for (const auto& [p, q] : bars) body.add(frameMat, limb(*p, *q, 0.012, 0.012, 8));
      body.add(frameMat, soap(V(s * 0.135, 0.45, 0.115), 0.035, 0.16, 0.1, 0.02));
    }
  } else {
    // a backbone under the tank, a down tube and a cradle under the engine
    body.add(frameMat, pipe({head0.clone().add(V(0, 0, 0.02)), V(0, 0.86, -0.18), V(0, 0.8, 0.0), V(0, 0.74, 0.06)}, 0.024, 16, 10));
    body.add(frameMat, pipe({head1.clone().add(V(0, 0, 0.01)), V(0, 0.55, -0.37), V(0, 0.25, -0.27)}, 0.02, 12, 10));
    for (const double s : {-1.0, 1.0}) {
      body.add(frameMat, pipe({V(0, 0.75, 0.05), V(s * 0.08, 0.63, 0.1), V(s * 0.09, 0.44, 0.115)}, 0.018, 10, 8));
      body.add(frameMat, pipe({V(0, 0.25, -0.27), V(s * 0.06, 0.17, -0.18), V(s * 0.07, 0.155, 0.02), V(s * 0.085, 0.31, 0.115)}, 0.016, 14, 8));
    }
  }
  // the subframe: under the seat, then inside the tail
  const double tailEnd = b.tail.back()[0];
  const auto inTail = [&](double z) {
    const double top = between(b.tail, z, 2);
    const double bottom = between(b.tail, z, 3);
    return (top + bottom) / 2;
  };
  for (const double s : {-1.0, 1.0}) {
    body.add(frameMat, pipe({V(s * 0.095, between(b.seat, 0.02, 3) - 0.012, 0.02), V(s * 0.085, inTail(0.4), 0.4), V(s * 0.065, inTail(tailEnd - 0.06), tailEnd - 0.06)}, 0.012, 10, 8));
    body.add(frameMat, pipe({V(s * 0.11, 0.5, 0.15), V(s * 0.088, inTail(0.42), 0.42)}, 0.011, 4, 8));
  }
  // tank, seat, tail with its lamp, the side panels under the seat
  body.add(paint, stations(b.tank, 24, 0.7));
  body.add(K.SEAT, stations(b.seat, 22, 0.6));
  body.add(paint, stations(b.tail, 20, 0.75));
  body.add(K.BLACK, soap(V(0, 0.7, 0.19), 0.2, 0.16, 0.3, 0.04));
  for (const double s : {-1.0, 1.0}) body.add(b.cowl ? K.BLACK : paint, soap(V(s * 0.1, 0.735, 0.22), 0.03, 0.12, 0.24, 0.012, Euler(0.12, 0, 0)));
  const double tz = b.tail.back()[0], ttop = b.tail.back()[2], tbot = b.tail.back()[3];
  const auto tailLamp = make<Mesh>(variant == "moped" ? blob(V(0, (ttop + tbot) / 2, tz + 0.012), 0.035, 0.03, 0.02) : soap(V(0, (ttop + tbot) / 2, tz + 0.006), 0.1, 0.034, 0.02, 0.008), TAIL());
  lamps_.brake.push_back(tailLamp);
  body_->add(tailLamp);
  // the number plate on its hanger, the rear indicators beside it
  const double plateZ = zR + 0.255;
  const double plateY = 0.665;
  body.add(K.BLACK, sweep({V(0, tbot + 0.01, tz - 0.12), V(0, tbot - 0.04, tz - 0.02), V(0, plateY + 0.075, plateZ - 0.02)}, [](double) { return rounded(0.1, 0.012, 0.005); }, 10, X));
  body.add(K.BLACK, soap(V(0, plateY, plateZ - 0.006), 0.19, 0.145, 0.008, 0.004, Euler(-0.25, 0, 0)));
  const auto plate = make<Mesh>(planeGeometry(0.18, 0.135), lambert({.map = plateTexture()}));
  plate->position.set(0, plateY, plateZ - 0.0005);
  plate->rotation.x = -0.25;
  body_->add(plate);
  for (const double s : {-1.0, 1.0}) {
    const Vector3 base = V(s * 0.04, plateY + 0.07, plateZ - 0.04);
    const Vector3 tip = V(s * 0.12, plateY + 0.075, plateZ - 0.045);
    body.add(K.BLACK, rod(base, tip, 0.006, 6));
    const auto ind = make<Mesh>(blob(tip.clone().add(V(0, 0, 0.012)), 0.016, 0.015, 0.028), K.DIM);
    lamps_.indicators.push_back({ind, static_cast<int>(s)});
    body_->add(ind);
  }

  // ---------------------------------------------------------------- the engine
  const double ey = b.crank[0], ez = b.crank[1], eh = b.crank[2], eH = b.crank[3], eD = b.crank[4];
  const double k = eH / 0.3;
  body.add(K.ENGINE, soap(V(0, ey, ez), 2 * eh, eH, eD, 0.05 * k));
  body.add(K.ENGINE, soap(V(0, ey - eH / 2 + 0.02, ez + 0.02), 2 * eh * 0.75, 0.08 * k, eD * 0.7, 0.025));
  // clutch cover on the right, the generator's on the left, the sprocket's behind it
  body.add(K.ALU, turned({{0, 0.0}, {0.085 * k + 0.01, 0.0}, {0.085 * k + 0.01, 0.022}, {0.07 * k, 0.034}, {0, 0.034}}, V(eh - 0.01, ey + 0.015, ez + eD * 0.12), X, 28, true));
  body.add(K.ALU, turned({{0, 0.0}, {0.065 * k + 0.008, 0.0}, {0.065 * k + 0.008, 0.02}, {0.05 * k, 0.03}, {0, 0.03}}, V(-eh + 0.01, ey + 0.03, ez - eD * 0.2), V(-1, 0, 0), 24, true));
  body.add(K.BLACK, soap(V(-eh - 0.004, ey - 0.005, ez + eD / 2 - 0.05), 0.024, 0.11 * k + 0.02, 0.1, 0.02));
  // the cylinders, leaning forwards from the top of the crankcase
  const int nCyl = static_cast<int>(b.cylinders[0]);
  const double cTilt = b.cylinders[1], cLen = b.cylinders[2], cR = b.cylinders[3];
  const Vector3 cDir = V(0, std::cos(cTilt), -std::sin(cTilt));
  const Vector3 cFwd = V(0, -std::sin(cTilt), -std::cos(cTilt));
  const Vector3 cBase = V(0, ey + eH / 2 - 0.03 - 0.05 * std::sin(cTilt), ez - eD / 2 + 0.115 - 0.05 * std::sin(cTilt));
  const Euler cRot(-cTilt, 0, 0, Euler::YXZ);
  const auto cyl = [&](double s) { return cBase.clone().addScaledVector(cDir, s); };
  std::vector<Vector3> ports;
  if (b.fins) {
    // one air-cooled cylinder: a barrel of fins, a finned head
    const double fin = cR + 0.032 * k + 0.012;
    body.add(K.ENGINE, rod(cyl(0), cyl(cLen), cR, 18));
    for (int i = 0; i < 9; i++) {
      const double s = 0.025 + (i * (cLen - 0.04)) / 8;
      body.add(i > 5 ? K.ENGINE : K.ALU, turned({{cR, s - 0.003}, {fin - 0.004 * (i % 2), s - 0.003}, {fin - 0.004 * (i % 2), s + 0.003}, {cR, s + 0.003}}, cBase, cDir, 22, true));
    }
    body.add(K.ALU, soap(cyl(cLen + 0.012), fin * 1.6, 0.03, fin * 1.7, 0.012, cRot));
    ports.push_back(cyl(cLen - 0.06).addScaledVector(cFwd, fin - 0.005));
  } else {
    // a block of cylinders, its head and cam cover
    const double w = nCyl * 0.072 + 0.06;
    body.add(K.ENGINE, soap(cyl(cLen * 0.36), w, cLen * 0.72, 2 * cR + 0.05, 0.02, cRot));
    body.add(K.ENGINE, soap(cyl(cLen * 0.8), w + 0.01, cLen * 0.22, 2 * cR + 0.07, 0.025, cRot));
    body.add(b.frameKind == "beam" ? K.ALU : K.BLACK, soap(cyl(cLen + 0.012), w - 0.02, 0.04, 2 * cR + 0.03, 0.018, cRot));
    for (int i = 0; i < nCyl; i++) {
      const double x = (i - (nCyl - 1) / 2.0) * (w - 0.06) / jsMax(1, nCyl - 1);
      ports.push_back(cyl(cLen * 0.82).addScaledVector(cFwd, cR + 0.03).setX(nCyl == 1 ? 0 : x));
    }
    // the radiator in front of the cylinders
    const Vector3 rc = cyl(cLen * 0.62).addScaledVector(cFwd, cR + 0.13);
    body.add(K.RADIATOR, soap(rc, w + 0.03, 0.24, 0.045, 0.012, Euler(-0.18, 0, 0)));
    body.add(K.BLACK, soap(rc.clone().add(V(0, 0, 0.004)), w + 0.05, 0.26, 0.03, 0.01, Euler(-0.18, 0, 0)));
    for (const double s : {-1.0, 1.0}) {
      // the tank's wings beside the radiator
      const std::vector<Station> wing = {
          {rc.z - 0.07, 0.03, rc.y + 0.12, rc.y + 0.02},
          {rc.z - 0.02, 0.05, rc.y + 0.16, rc.y - 0.06},
          {rc.z + 0.08, 0.05, rc.y + 0.17, rc.y - 0.02},
          {rc.z + 0.14, 0.03, rc.y + 0.16, rc.y + 0.08},
      };
      const GeometryPtr g = stations(wing, 14, 0.7);
      g->translate(s * ((w + 0.05) / 2 + 0.01), 0, 0);
      body.add(paint, g);
    }
  }
  // the exhaust: down in front of the engine, under it and back to the silencer on the right
  const Vector3 s0 = b.silencer0;
  const Vector3 s1 = b.silencer1;
  const double sr = b.silencerR;
  const Vector3 under = V(0.04, jsMax(0.14, ey - eH / 2 - 0.04), ez + 0.02);
  for (const Vector3& p : ports) {
    const Vector3 drop = V(p.x * 0.9, jsMax(under.y + 0.06, p.y - 0.18), jsMin(p.z - 0.02, ez - eD / 2 - 0.03));
    body.add(K.STEEL, pipe({p, p.clone().addScaledVector(cFwd, 0.04).add(V(0, -0.02, 0)), drop, V(p.x * 0.5 + 0.02, under.y + 0.01, ez - eD / 2 + 0.04), under}, 0.019 * jsMin(1, 0.6 + 0.4 * k), 18, 10));
  }
  const bool chamber = variant == "moped";
  body.add(K.STEEL, sweep({under, V(0.07, under.y - 0.005, ez + eD * 0.35), V(s0.x - 0.03, s0.y - 0.06, s0.z - 0.12), s0.clone().addScaledVector(s1.clone().sub(s0).normalize(), 0.02)},
                          [&](double t) { return circle(chamber ? 0.022 + 0.03 * std::sin(PI * jsMin(1, t * 1.3)) : 0.024 * jsMin(1, 0.7 + 0.3 * k), 12); }, 16));
  // (the silencer swells a little behind its inlet)
  const auto can = [&](double t) { return sr * (0.82 + 0.18 * std::sin(PI * jsMin(1, t * 1.6 + 0.1))); };
  body.add(chamber ? K.CHROME : K.ALU, sweep({s0, s1}, [&](double t) { return oval(can(t), can(t), 16, 0.8); }, 6, Y));
  body.add(K.BLACK, turned({{sr * 0.84, 0}, {sr * 0.84, 0.025}, {sr * 0.45, 0.03}, {sr * 0.45, 0.01}, {0, 0.01}}, s1.clone(), s1.clone().sub(s0).normalize(), 18, true));
  // footpegs, with the gear lever on the left and the brake pedal on the right; the pillion's pegs
  const double px = b.peg[0], py = b.peg[1], pz = b.peg[2];
  for (const double s : {-1.0, 1.0}) {
    body.add(K.ALU, soap(V(s * (px - 0.05), py + 0.03, pz + 0.04), 0.012, 0.09, 0.07, 0.006, Euler(0.4, 0, 0)));
    body.add(K.BLACK, limb(V(s * (px - 0.045), py, pz), V(s * (px + 0.05), py, pz), 0.012, 0.012, 8));
    const std::vector<Vector3> lever = {V(s * (px - 0.03), py - 0.01, pz + 0.035), V(s * (px - 0.015), py + 0.01, pz - 0.06), V(s * (px - 0.005), py + 0.03, pz - 0.15)};
    body.add(K.ALU, sweep(lever, [](double) { return rounded(0.008, 0.016, 0.003); }, 6, X));
    body.add(K.BLACK, limb(V(s * (px - 0.015), py + 0.03, pz - 0.15), V(s * (px + 0.03), py + 0.03, pz - 0.15), 0.009, 0.009, 8));
    // (on a hanger from the subframe)
    const Vector3 hp = V(s * (px - 0.02), py + 0.13, pz + 0.34);
    body.add(K.ALU, sweep({V(s * 0.09, 0.79, hp.z - 0.12), V(s * (px - 0.05), hp.y + 0.1, hp.z - 0.05), V(s * (px - 0.045), hp.y, hp.z)}, [](double) { return rounded(0.012, 0.035, 0.005); }, 8, X));
    body.add(K.BLACK, limb(V(s * (px - 0.05), hp.y, hp.z), V(s * (px + 0.03), hp.y, hp.z), 0.011, 0.011, 8));
  }

  // ---------------------------------------------------------------- the swing arm, chain and shocks
  const double armX = tr / 2 + 0.042;
  const Vector3 axleR = V(0, R, zR);
  for (const double s : {-1.0, 1.0}) {
    body.add(frameMat, rod(V(s * (armX + 0.022), pivot.y, pivot.z), V(s * (armX + 0.01), pivot.y, pivot.z), 0.024, 12));
    const bool tube = b.frameKind == "tube";
    swing.add(frameMat, sweep({V(s * armX, pivot.y, pivot.z), V(s * armX, (pivot.y + R) / 2 + 0.01, (pivot.z + zR) / 2), V(s * armX, R, zR + 0.025)},
                              [&](double t) { return tube ? oval(0.016, 0.022 - 0.004 * t, 12) : rounded(0.034, 0.075 - 0.025 * t, 0.012); }, 12, X));
    swing.add(K.BLACK, soap(V(s * (armX + 0.004), R, zR + 0.03), 0.03, 0.03, 0.05, 0.008));
  }
  swing.add(frameMat, rod(V(-armX, pivot.y, pivot.z), V(armX, pivot.y, pivot.z), 0.022, 12));
  swing.add(frameMat, rod(V(-armX, pivot.y + 0.005, pivot.z + 0.15), V(armX, pivot.y + 0.005, pivot.z + 0.15), b.frameKind == "tube" ? 0.016 : 0.022, 10));
  swing.add(K.STEEL, rod(V(-armX - 0.018, R, zR), V(armX + 0.018, R, zR), 0.011, 10));
  // the chain from the engine's sprocket to the wheel's
  {
    const Vector3 f = V(chainX, ey - 0.005, ez + eD / 2 - 0.05);
    const double fr_ = 0.04;
    Vector3 d = axleR.clone().setX(chainX).sub(f);
    const double L = d.length();
    d.normalize();
    const double sa = (sprocketR - fr_) / L;
    for (const double side : {1.0, -1.0}) {
      Vector3 perp = V(0, d.z, -d.y).multiplyScalar(side);   // square to the run in the side plane
      if (perp.y * side < 0) perp.negate();
      const Vector3 n = perp.multiplyScalar(std::sqrt(1 - sa * sa)).addScaledVector(d, -sa);
      const Vector3 a = f.clone().addScaledVector(n, fr_);
      const Vector3 e = axleR.clone().setX(chainX).addScaledVector(n, sprocketR);
      swing.add(K.STEEL, sweep({a, e}, [](double) { return rounded(0.012, 0.008, 0.003); }, 1, X));
    }
    body.add(K.STEEL, turned({{0, -0.004}, {fr_ + 0.004, -0.004}, {fr_ + 0.004, 0.004}, {0, 0.004}}, f, X, 16, true));
  }
  // the rear calliper below the disc, the hugger over the tyre
  if (b.rearDisc > 0) swing.add(K.BLACK, soap(V(halfR - 0.004 + 0.006, R - 0.06, zR - 0.06), 0.04, 0.06, 0.07, 0.012, Euler(0.7, 0, 0)));
  {
    const double g = R + 0.026;
    std::vector<Vector3> arc;
    for (int i = 0; i <= 6; i++) {
      const double t = 0.08 + (0.95 * i) / 6;
      arc.push_back(V(0, R + std::cos(t) * g, zR - std::sin(t) * g));
    }
    swing.add(K.BLACK, sweep(arc, [&](double) { return rounded(tr + 0.02, 0.006, 0.003); }, 8, X));
  }
  // the shocks: one in the middle under the seat, or two outside the swing arm
  std::vector<std::pair<Vector3, Vector3>> shockPairs;
  if (b.twinShock) {
    for (const double s : {-1.0, 1.0}) {
      const Vector3 low = pivot.clone().lerp(axleR, 0.84).add(V(s * (armX + 0.03), 0.03, 0));
      shockPairs.emplace_back(low, V(low.x, inTail(low.z - 0.13) + 0.02, low.z - 0.13));
    }
  } else shockPairs.emplace_back(pivot.clone().lerp(axleR, 0.22).add(V(0, 0.055, 0)), V(0, 0.73, 0.15));
  for (const auto& [low, high] : shockPairs) {
    const double len = low.distanceTo(high);
    const auto g = make<Group>();
    Parts sp;
    sp.add(K.BLACK, limb(V(0, 0, 0), V(0, 0.03, 0), 0.016, 0.016, 10));
    sp.add(K.CHROME, rod(V(0, 0.02, 0), V(0, len * 0.6, 0), 0.008, 10));
    sp.add(variant == "moped" ? K.CHROME : K.BLACK, limb(V(0, len * 0.45, 0), V(0, len, 0), 0.021, 0.019, 12));
    const int coils = 8;
    const double coilR = b.twinShock ? 0.027 : 0.034;
    for (int i = 0; i < coils; i++) {
      const double y = len * 0.12 + (i * len * 0.66) / (coils - 1);
      GeometryPtr coil = torusGeometry(coilR, 0.0055, 6, 16);
      coil->rotateX(PI / 2).rotateZ(0.08).translate(0, y, 0);
      coil = coil->toNonIndexed();
      coil->deleteAttribute("uv");
      sp.add(springMat, coil);
    }
    sp.into(*g);
    shocks_.push_back({g, low.clone().sub(pivot), high, len});
    body_->add(g);
  }

  // ---------------------------------------------------------------- the rider
  // Built in the pose he sits in on a straight road.  Hips, upper body, neck and head are each a group
  // about their joint (moved in update()); arms and legs are bent there towards grips, pegs or the road.
  Parts hips;
  Parts upperBody;
  Parts collar;
  Parts neck;
  Parts helmet;
  const Vector3 waist = hip.clone().addScaledVector(up, 0.14);
  // where the neck bends on the shoulders, where the head turns on the neck
  const Vector3 neckBase = shoulders.clone().addScaledVector(up, -0.03);
  const Vector3 skull = head.clone().add(V(0, -0.07, 0.015));
  const auto hipFrame = [&](double s, double a, double d, double shift) { return ring(hip.clone().addScaledVector(up, s).addScaledVector(fwd, shift), X, fwd, oval(a, d, 18, 0.82)); };
  // the seat of the trousers and the hips, sitting on the seat
  hips.add(K.TROUSERS, loft({hipFrame(-0.105, 0.06, 0.04, -0.01), hipFrame(-0.09, 0.14, 0.1, -0.005), hipFrame(-0.04, 0.17, 0.125, 0.0), hipFrame(0.06, 0.162, 0.112, 0.005), hipFrame(0.13, 0.152, 0.105, 0.01)}));
  // the jacket from the waist to the shoulders, its hem well over the trousers (they turn against each other
  // at the waist: a clean edge, not two skins crossing)
  upperBody.add(K.JACKET, loft({hipFrame(0.05, 0.182, 0.138, 0.007), hipFrame(0.18, 0.163, 0.112, 0.014), hipFrame(0.3, 0.178, 0.122, 0.028), hipFrame(0.4, 0.195, 0.124, 0.03), hipFrame(0.46, 0.2, 0.11, 0.022),
                                hipFrame(0.51, 0.175, 0.088, 0.008), hipFrame(0.545, 0.1, 0.062, -0.004)}));
  // a band of the bike's colour across chest and back, and the shoulders
  upperBody.add(paint, loft({hipFrame(0.31, 0.181, 0.125, 0.028), hipFrame(0.355, 0.189, 0.127, 0.03)}, false));
  const auto shoulderAt = [&](double s) { return hip.clone().addScaledVector(up, 0.475).add(V(s * 0.175, 0, 0)); };
  for (const double s : {-1.0, 1.0}) upperBody.add(K.JACKET, blob(shoulderAt(s).add(V(-s * 0.01, 0.004, 0.005)), 0.058, 0.056, 0.064));
  // neck, collar and helmet: the parts the rider's own eyes are inside of
  neck.add(K.JACKET, limb(neckBase, skull, 0.058, 0.052, 12));
  collar.add(K.JACKET, turned({{0.07, 0.0}, {0.085, 0.012}, {0.082, 0.05}, {0.07, 0.056}}, shoulders.clone().addScaledVector(up, -0.035), up, 18));
  helmet.add(K.HELMET, blob(head, 0.128, 0.142, 0.158, std::nullopt, 24, 16));
  // the chin bar of a full-face helmet
  helmet.add(K.HELMET, blob(head.clone().add(V(0, -0.07, -0.06)), 0.105, 0.072, 0.1, Euler(0.25, 0, 0), 18, 10));
  // the visor over the eyes, a stripe of the bike's colour over the top
  {
    const GeometryPtr visorGeo = sphereGeometry(1, 20, 8, PI * 1.5 - 1.1, 2.2, 1.12, 0.62);
    visorGeo->scale(0.135, 0.149, 0.166).translate(head.x, head.y, head.z);
    helmet.add(K.VISOR, bare(visorGeo));
  }
  for (const auto& [p0, pl] : {UV{PI * 0.5 - 0.16, 0.32}, UV{PI * 1.5 - 0.16, 0.32}}) {
    const GeometryPtr st = sphereGeometry(1, 6, 10, p0, pl, 0.02, p0 > PI ? 1.05 : 1.75);
    st->scale(0.131, 0.145, 0.161).translate(head.x, head.y, head.z);
    helmet.add(paint, bare(st));
  }
  // legs: thighs along the tank, knees bent, boots on the pegs – thigh and shin (with the boot's shaft)
  // are posed in update(), each along +y from its upper joint; the foot is a part of its own about the ankle
  for (const double s : {-1.0, 1.0}) {
    const auto thigh = make<Mesh>(limb(V(), V(0, THIGH, 0), 0.08, 0.062), K.TROUSERS);
    const auto shin = make<Group>();
    shin->add(make<Mesh>(limb(V(), V(0, SHIN, 0), 0.058, 0.046), K.TROUSERS), make<Mesh>(limb(V(0, SHIN - 0.2, 0), V(0, SHIN, 0), 0.056, 0.05), K.BOOTS));
    // (flat on the peg, the sole as low as the peg's top: heel and toe 4 cm below the ankle)
    const Vector3 heel = V(0, -0.04, 0.06);
    const Vector3 toe = V(s * 0.01, -0.04, -0.215);
    Vector3 fd = toe.clone().sub(heel);
    const double fl = fd.length();
    fd.normalize();
    const Vector3 fu = Y.clone().addScaledVector(fd, -fd.y).normalize();
    Rings sole;
    for (const auto& [t, w, h] : {std::array<double, 3>{-0.04, 0.0, 0.0}, std::array<double, 3>{0.0, 0.035, 0.035}, std::array<double, 3>{0.25, 0.045, 0.048}, std::array<double, 3>{0.6, 0.048, 0.04},
                                  std::array<double, 3>{0.85, 0.042, 0.03}, std::array<double, 3>{1.0, 0.03, 0.02}, std::array<double, 3>{1.03, 0.0, 0.0}})
      sole.push_back(ring(heel.clone().addScaledVector(fd, t * fl), X, fu, oval(w, h, 14, 0.8)));
    const auto foot = make<Group>();
    foot->add(make<Mesh>(loft(sole, false), K.BOOTS));
    thigh->castShadow = true;
    for (const ObjectPtr& m : shin->children) m->castShadow = true;
    for (const ObjectPtr& m : foot->children) m->castShadow = true;
    body_->add(thigh, shin, foot);
    rider_.insert(rider_.end(), {thigh, shin, foot});
    legs_.push_back({static_cast<int>(s), V(s * 0.095, 0, 0), V(s * px, py + 0.085, pz + 0.085), thigh, shin, foot});
  }
  // the hands round the grips (they turn with the bar)
  Parts gloves;
  for (const double s : {-1.0, 1.0}) {
    const Vector3 G = V(s * gx, gy, gz);
    Vector3 wrist = G.clone().add(V(-s * 0.012, 0.03, 0.062));
    gloves.add(K.GLOVES, blob(G.clone().add(V(-s * 0.005, 0.004, -0.004)), 0.05, 0.036, 0.04));
    gloves.add(K.GLOVES, limb(wrist, G.clone().add(V(s * 0.004, 0.014, -0.012)), 0.033, 0.03));
    gloves.add(K.GLOVES, limb(wrist.clone().add(V(-s * 0.026, -0.016, -0.012)), G.clone().add(V(-s * 0.045, -0.006, -0.022)), 0.014, 0.012, 8));
    // the forearm and upper arm, posed in update()
    const auto upper = make<Mesh>(limb(V(), V(0, UPPER_ARM, 0), 0.056, 0.046), K.JACKET);
    const auto fore = make<Group>();
    fore->add(make<Mesh>(limb(V(), V(0, FOREARM * 0.8, 0), 0.045, 0.039), K.JACKET), make<Mesh>(limb(V(0, FOREARM * 0.68, 0), V(0, FOREARM, 0), 0.043, 0.035), K.GLOVES));
    upper->castShadow = true;
    for (const ObjectPtr& m : fore->children) m->castShadow = true;
    body_->add(upper, fore);
    rider_.insert(rider_.end(), {upper, fore});
    arms_.push_back({static_cast<int>(s), shoulderAt(s).sub(waist), wrist.sub(P), upper, fore});
  }

  // ---------------------------------------------------------------- assemble
  Group& group = *body_;
  rootG.add(body_);
  body.into(group);
  // the front about the steering axis, the sliding part of the fork with the wheel in it
  front_->position.copy(P);
  group.add(front_);
  front.into(*front_, P);
  front_->add(slider_);
  slider.into(*slider_, P);
  frontWheel_->position.copy(axle).sub(P);
  slider_->add(frontWheel_);
  wheelF.into(*frontWheel_);
  for (const std::shared_ptr<Mesh>& m : gloves.into(*front_, P)) rider_.push_back(m);
  // the swing arm about its pivot, the rear wheel at its end
  swing_->position.copy(pivot);
  group.add(swing_);
  swing.into(*swing_, pivot);
  rearWheel_->position.copy(axleR).sub(pivot);
  swing_->add(rearWheel_);
  wheelR.into(*rearWheel_);
  const Vector3 rel = axleR.clone().sub(pivot);
  armLength_ = rel.length();
  armDrop_ = rel.y;
  // the rider: the hips on the seat, the upper body about the waist, the neck on the shoulders, the head
  // on the neck (neck, collar and helmet keep out of the picture taken from his own eyes)
  hipRest_ = hip.clone();
  pelvis_->position.copy(hip);
  group.add(pelvis_);
  rider_.push_back(pelvis_);
  hips.into(*pelvis_, hip);
  torso_->position.copy(waist).sub(hip);
  pelvis_->add(torso_);
  upperBody.into(*torso_, waist);
  for (const std::shared_ptr<Mesh>& m : collar.into(*torso_, waist)) ownHead(m, head.clone().sub(waist), 0.3);
  neck_->position.copy(neckBase).sub(waist);
  torso_->add(neck_);
  for (const std::shared_ptr<Mesh>& m : neck.into(*neck_, neckBase)) ownHead(m, head.clone().sub(neckBase), 0.3);
  head_->position.copy(skull).sub(neckBase);
  neck_->add(head_);
  for (const std::shared_ptr<Mesh>& m : helmet.into(*head_, skull)) ownHead(m, head.clone().sub(skull), 0.3);
  eyeRest_ = eye.clone().sub(waist);
  for (const Lamps::Indicator& i : lamps_.indicators) i.mesh->castShadow = false;
  for (const std::shared_ptr<Mesh>& m : lamps_.brake) m->castShadow = false;
  update(standingState(R), 1, 0);
}

void PlayerBike::setCabinOpacity(double) {
  /* nothing hides the road on a motorcycle */
}

void PlayerBike::setLean(double angle) { lean_ = angle; }

void PlayerBike::setRider(bool on) {
  riderOn_ = on;
  for (const ObjectPtr& o : rider_) o->visible = on;
  // (without him no foot holds it tilted: it stands upright)
  body_->rotation.z = -(leanDrawn_ + (on ? motion_.pose.tilt : 0));
}

void PlayerBike::update(const CarVisualState& s, double dt, double now) {
  Vector3 ta, tb, tc, td, te;
  const double h = std::isfinite(dt) && dt > 0 ? dt : 0;
  // the lean follows the bend smoothly
  leanDrawn_ += (lean_ - leanDrawn_) * jsMin(1, h * 6);
  // the handlebar turns about the raked axis: for the wheel's angle on the road a little more
  const double steer = (at(s.wheelSteer, 0, 0) + at(s.wheelSteer, 1, 0)) / 2;
  const double bar = clamp(steer / std::cos(rake_), -0.7, 0.7);
  front_->quaternion.setFromAxisAngle(axis_, -bar);
  // how the rider moves, from what the motorcycle does
  RiderInput& inp = input_;
  inp.speedKmh = s.speedKmh;
  inp.reversing = s.reversing;
  inp.steer = steer;
  inp.bar = bar;
  inp.lean = leanDrawn_;
  inp.throttle = s.throttle;
  inp.brake = s.brake;
  inp.indicator = s.hazard ? 0 : s.indicator;
  const RiderPose& p = motion_.step(inp, h);
  // (standing, it settles a little on to his foot on the road – not without him)
  const double roll = -(leanDrawn_ + (riderOn_ ? p.tilt : 0));
  body_->rotation.z = roll;
  // the suspension: the fork slides along the axis, the swing arm swings about its pivot
  const double R = radius_;
  const double dive = clamp((at(s.wheelY, 0, R) + at(s.wheelY, 1, R)) / 2 - R, -0.06, 0.12);
  slider_->position.copy(axis_).multiplyScalar(dive / axis_.y);
  const double squat = clamp((at(s.wheelY, 2, R) + at(s.wheelY, 3, R)) / 2 - R, -0.06, 0.12);
  const double rest = std::asin(clamp(armDrop_ / armLength_, -1, 1));
  swing_->rotation.x = rest - std::asin(clamp((armDrop_ + squat) / armLength_, -1, 1));
  frontWheel_->rotation.x = -(at(s.wheelSpin, 0, 0) + at(s.wheelSpin, 1, 0)) / 2;
  rearWheel_->rotation.x = -(at(s.wheelSpin, 2, 0) + at(s.wheelSpin, 3, 0)) / 2;
  swing_->updateMatrix();
  for (Shock& k : shocks_) {
    const Vector3 low = ta.copy(k.low).applyMatrix4(swing_->matrix);
    Vector3& d = tb.subVectors(k.high, low);
    const double len = d.length();
    k.mesh->position.copy(low);
    k.mesh->quaternion.setFromUnitVectors(Y, d.normalize());
    k.mesh->scale.set(1, len / k.length, 1);
  }
  // the rider: the hips on the seat, the upper body about the waist (each rolled, bent, then twisted), the
  // neck and the head on it (tilted, then turned, then nodding); the eye goes with the upper body as he rides
  const Vector3& hr = hipRest_;
  pelvis_->position.set(hr.x + p.hipShift, hr.y - 0.25 * std::fabs(p.hipShift), hr.z - p.hipSlide);
  pelvis_->rotation.set(-p.hipPitch, -p.hipYaw, -p.hipRoll, Euler::ZXY);
  torso_->rotation.set(p.hipPitch - p.torsoPitch, p.hipYaw - p.torsoYaw, p.hipRoll - p.torsoRoll, Euler::ZXY);
  neck_->rotation.set(-0.4 * p.headPitch, 0, -0.4 * p.headRoll, Euler::ZYX);
  head_->rotation.set(-0.6 * p.headPitch, -p.headYaw, -0.6 * p.headRoll, Euler::ZYX);
  pelvis_->updateMatrix();
  torso_->updateMatrix();
  eyeQ_.setFromEuler(eyeEuler_.set(p.hipPitch - p.eyePitch, p.hipYaw - p.eyeYaw, p.hipRoll - p.eyeRoll, Euler::ZXY));
  eyeM_.compose(torso_->position, eyeQ_, Vector3(1, 1, 1));
  eye.copy(eyeRest_).applyMatrix4(eyeM_).applyMatrix4(pelvis_->matrix);
  // his arms reach for the grips: the elbows out and down, further out when he tucks in, in when he braces himself
  front_->updateMatrix();
  for (Arm& a : arms_) {
    Vector3& sh = ta.copy(a.shoulder).applyMatrix4(torso_->matrix).applyMatrix4(pelvis_->matrix);
    const Vector3& wr = tb.copy(a.wrist).applyMatrix4(front_->matrix);
    // (a hand never lets go: with the grip out of reach – hanging off the inside, braking, looking back over the
    // outside shoulder at once – the shoulder reaches forwards for it, a centimetre or two inside the round of it)
    const double gap = sh.distanceTo(wr) - ARM_REACH;
    if (gap > 0) sh.lerp(wr, gap / (gap + ARM_REACH));
    const Vector3& pole = te.set(a.side * (0.55 + 0.35 * p.elbows), -1, 0.2).normalize();
    const Vector3& el = bend(sh, wr, UPPER_ARM, FOREARM, pole, tc);
    Vector3& d = td.subVectors(el, sh);
    a.upper->position.copy(sh);
    a.upper->scale.set(1, d.length() / UPPER_ARM, 1);
    a.upper->quaternion.setFromUnitVectors(Y, d.normalize());
    d.subVectors(wr, el);
    a.fore->position.copy(el);
    a.fore->scale.set(1, d.length() / FOREARM, 1);
    a.fore->quaternion.setFromUnitVectors(Y, d.normalize());
  }
  // his legs from the hips to the pegs, the knees forwards and a little out – the inside one further
  // out at a strong lean; a foot put down goes (lifted on its way) to the road beside the motorcycle,
  // a little ahead of its peg: found in the upright frame (the wheels' heights give the road), turned
  // into the leaning one
  const double cr = std::cos(roll);
  const double sr = std::sin(roll);
  for (Leg& g : legs_) {
    const int side = g.side;
    const double down = side < 0 ? p.footL : p.footR;
    const double knee = side < 0 ? p.kneeL : p.kneeR;
    const double e = down * down * (3 - 2 * down);
    const Vector3& hj = ta.copy(g.hip).applyMatrix4(pelvis_->matrix);
    Vector3& ank = tb.copy(g.peg);
    if (e > 1e-3) {
      const double gx = side * (std::fabs(g.peg.x) + 0.14);
      const double gz = g.peg.z - 0.055;
      const double gy = dive + ((squat - dive) * (gz - zFront_)) / (zRear_ - zFront_) + ANKLE;
      ank.lerp(tc.set(gx * cr + gy * sr, gy * cr - gx * sr, gz), e);
      ank.y += 0.05 * std::sin(PI * e);
      // (never further than the leg reaches)
      Vector3& d = tc.subVectors(ank, hj);
      const double reach = (THIGH + SHIN) * 0.995;
      if (d.lengthSq() > reach * reach) ank.copy(hj).addScaledVector(d.normalize(), reach);
    }
    const Vector3& pole = te.set(side * (0.45 + 0.5 * knee + 0.2 * e), 0.15, -1).normalize();
    const Vector3& kn = bend(hj, ank, THIGH, SHIN, pole, tc);
    Vector3& d = td.subVectors(kn, hj);
    g.thigh->position.copy(hj);
    g.thigh->scale.set(1, d.length() / THIGH, 1);
    g.thigh->quaternion.setFromUnitVectors(Y, d.normalize());
    d.subVectors(ank, kn);
    g.shin->position.copy(kn);
    g.shin->scale.set(1, d.length() / SHIN, 1);
    g.shin->quaternion.setFromUnitVectors(Y, d.normalize());
    // the foot: on its peg the toe turns out a little as the knee opens; on the road flat on it, toe out
    g.foot->position.copy(ank);
    g.foot->rotation.set(0, -side * (0.3 * e + 0.25 * knee), -roll * e, Euler::ZYX);
  }
  lamps_.update(s);
  panel_.update(s, now);
  // (the mirrors are drawn in the frame of the front before the scene updates its matrices)
  if (body_->parent) body_->updateMatrixWorld(true);
}

double PlayerBike::leaning() const { return -static_cast<double>(body_->rotation.z); }

}  // namespace fs

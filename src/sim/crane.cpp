// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/crane.ts @ v1.7.7
#include "sim/crane.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fs {

namespace {

constexpr double INF = std::numeric_limits<double>::infinity();
inline double clampC(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
inline double wrapC(double a) { return a - TAU * jsRound(a / TAU); }
inline double signC(double v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

/** The tipping lines of a support polygon: a point on each and its outward normal (x, z). */
std::vector<TippingLine> tippingLines(const std::vector<std::array<double, 2>>& hull) {
  double cx = 0;
  double cz = 0;
  for (const auto& [x, z] : hull) {
    cx += x;
    cz += z;
  }
  cx /= static_cast<double>(hull.size());
  cz /= static_cast<double>(hull.size());
  std::vector<TippingLine> out;
  for (size_t i = 0; i < hull.size(); i++) {
    const auto [ax, az] = hull[i];
    const auto [bx, bz] = hull[(i + 1) % hull.size()];
    double l = std::hypot(bx - ax, bz - az);
    if (l == 0 || std::isnan(l)) l = 1;
    double nx = (bz - az) / l;
    double nz = -(bx - ax) / l;
    // (outwards: away from the middle)
    if ((ax - cx) * nx + (az - cz) * nz < 0) {
      nx = -nx;
      nz = -nz;
    }
    out.push_back({ax, az, nx, nz});
  }
  return out;
}

Joints joints() {
  Joints q;
  q.slew = 0;
  q.trolley = TOWER_GEOMETRY.trolleyMin;
  q.luff = 0;
  q.tele = 0;
  q.inner = LOADER_GEOMETRY.stowInner;
  q.outer = LOADER_GEOMETRY.stowOuter;
  q.ext = 0;
  q.rope = 2;
  return q;
}

const std::vector<double> MOBILE_RADII = {3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32};
const std::vector<double> LOADER_RADII = {1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5, 5.5, 6, 6.5, 7, 7.5, 8, 8.5, 9, 9.5, 10, 10.5};
/** slewing directions the 360° charts are checked in */
constexpr int DIRECTIONS = 72;

/**
 * The mobile crane's chart for a setting: at each boom length and radius
 * the tipping load in the worst slewing direction, times the margin, at
 * most what the boom may carry at that length and radius.  Radii it cannot reach at a
 * length (too far, or closer in than its steepest) are 0.
 */
std::vector<std::vector<double>> mobileChart(const Setting& setting) {
  const MobileGeometry& g = MOBILE_GEOMETRY;
  const CraneSpec& sp = MOBILE_CRANE;
  const auto sup = supportsFor(sp, setting);
  const double margin = setting == "tyres" ? MARGIN_TYRES * TYRES_DERATE : MARGIN_OUTRIGGERS;
  std::vector<std::vector<double>> out;
  for (size_t li = 0; li < g.lengths.size(); li++) {
    const double L = g.lengths[li];
    std::vector<double> col;
    for (const double R : MOBILE_RADII) {
      const double l = L + g.head;
      const double c = (R - g.footA) / l;
      if (c > 1 || c < std::cos(g.luffMax)) {
        col.push_back(0);
        continue;
      }
      const double luff = std::acos(c);
      const double tele = (L - g.length0) / (g.length1 - g.length0);
      double tip = INF;
      for (int k = 0; k < DIRECTIONS; k++) {
        Joints q = joints();
        q.slew = (static_cast<double>(k) / DIRECTIONS) * TAU;
        q.luff = luff;
        q.tele = tele;
        const auto [dx, dz] = boomDir(q.slew);
        tip = std::min(tip, tippingLoad(craneWeights(sp, q), sp.slewX + R * dx, sp.slewZ + R * dz, sup));
      }
      col.push_back(std::max(0.0, std::floor(std::min({g.strength[li], g.moment / R, margin * tip}) * 20) / 20));
    }
    out.push_back(std::move(col));
  }
  return out;
}

/**
 * The loader crane's chart for a setting: its lifting moment (at most its
 * greatest load close in), and – in the worst slewing direction, the outer
 * boom out level at the radius – the tipping load times the margin.
 */
std::vector<std::vector<double>> loaderChart(const Setting& setting) {
  const LoaderGeometry& g = LOADER_GEOMETRY;
  const CraneSpec& sp = LOADER_CRANE;
  const auto sup = supportsFor(sp, setting);
  const double margin = setting == "tyres" ? MARGIN_TYRES * TYRES_DERATE : MARGIN_OUTRIGGERS;
  std::vector<double> col;
  for (const double R : LOADER_RADII) {
    // the booms reaching out level at that radius: the inner boom a little up, the outer boom level, out as far as needed
    const double inner = 20 * DEG;
    const double reach = R - g.pinA - g.inner * std::cos(inner);
    const double ext = clampC((reach - g.outer0) / (g.outer1 - g.outer0), 0, 1);
    Joints q = joints();
    q.inner = inner;
    q.outer = -inner;
    q.ext = ext;
    double tip = INF;
    for (int k = 0; k < DIRECTIONS; k++) {
      Joints qq = q;
      qq.slew = (static_cast<double>(k) / DIRECTIONS) * TAU;
      const auto [dx, dz] = boomDir(qq.slew);
      tip = std::min(tip, tippingLoad(craneWeights(sp, qq), sp.slewX + R * dx, sp.slewZ + R * dz, sup));
    }
    const double structural = std::min(g.maxLoad, g.moment / R);
    col.push_back(std::max(0.0, std::floor(std::min(structural, margin * tip) * 20) / 20));
  }
  return {col};
}

std::map<CraneType, LoadChart> charts;

/** The rope's give: how far it stretches under the weight it holds (m), and how much of its spring is damped. */
constexpr double STRETCH = 0.08;
constexpr double ROPE_DAMPING = 0.45;
/** the air's damping of the swing (per s) and the drag on a load (ρ·Cd/2) */
constexpr double AIR = 0.05;
constexpr double DRAG = 0.72;

}  // namespace

// ------------------------------------------------------------------------------------ stability

std::vector<std::array<double, 2>> convexHull(const std::vector<std::array<double, 2>>& points) {
  std::vector<std::array<double, 2>> p = points;
  std::stable_sort(p.begin(), p.end(), [](const auto& a, const auto& b) { return a[0] != b[0] ? a[0] < b[0] : a[1] < b[1]; });
  if (p.size() < 3) return p;
  const auto cross = [](const std::array<double, 2>& o, const std::array<double, 2>& a, const std::array<double, 2>& b) {
    return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
  };
  std::vector<std::array<double, 2>> lower;
  for (const auto& q : p) {
    while (lower.size() >= 2 && cross(lower[lower.size() - 2], lower[lower.size() - 1], q) <= 1e-9) lower.pop_back();
    lower.push_back(q);
  }
  std::vector<std::array<double, 2>> upper;
  for (int i = static_cast<int>(p.size()) - 1; i >= 0; i--) {
    const auto& q = p[i];
    while (upper.size() >= 2 && cross(upper[upper.size() - 2], upper[upper.size() - 1], q) <= 1e-9) upper.pop_back();
    upper.push_back(q);
  }
  lower.pop_back();
  upper.pop_back();
  lower.insert(lower.end(), upper.begin(), upper.end());
  return lower;
}

Stability stability(const std::vector<Weight>& weights, const std::vector<SideForce>& sides, const std::vector<std::array<double, 2>>& supports) {
  const auto hull = convexHull(supports);
  double W = 0;
  double mx = 0;
  double mz = 0;
  for (const Weight& w : weights) {
    W += w.t * G;
    mx += w.t * G * w.x;
    mz += w.t * G * w.z;
  }
  // (a sideways force at a height shifts where the whole presses on the ground)
  for (const SideForce& f : sides) {
    mx += f.fx * f.y;
    mz += f.fz * f.y;
  }
  const double copX = W > 0 ? mx / W : 0;
  const double copZ = W > 0 ? mz / W : 0;
  if (hull.size() < 3) return {INF, -1, copX, copZ, hull};
  double u = 0;
  int edge = 0;
  const auto lines = tippingLines(hull);
  for (size_t i = 0; i < lines.size(); i++) {
    const TippingLine& ln = lines[i];
    double over = 0;
    double hold = 0;
    for (const Weight& w : weights) {
      const double m = w.t * G * ((w.x - ln.px) * ln.nx + (w.z - ln.pz) * ln.nz);
      if (m > 0) over += m;
      else hold -= m;
    }
    for (const SideForce& f : sides) {
      const double m = f.y * (f.fx * ln.nx + f.fz * ln.nz);
      if (m > 0) over += m;
      else hold -= m;
    }
    const double ui = hold > 1e-9 ? over / hold : over > 0 ? INF : 0;
    if (ui > u) {
      u = ui;
      edge = static_cast<int>(i);
    }
  }
  return {u, edge, copX, copZ, hull};
}

double tippingLoad(const std::vector<Weight>& weights, double x, double z, const std::vector<std::array<double, 2>>& supports) {
  const auto hull = convexHull(supports);
  if (hull.size() < 3) return 0;
  double best = INF;
  for (const TippingLine& ln : tippingLines(hull)) {
    const double arm = (x - ln.px) * ln.nx + (z - ln.pz) * ln.nz;
    double m = 0;
    for (const Weight& w : weights) m += w.t * ((w.x - ln.px) * ln.nx + (w.z - ln.pz) * ln.nz);
    if (m >= 0) return 0;
    if (arm <= 1e-6) continue;
    best = std::min(best, -m / arm);
  }
  return best;
}

// ------------------------------------------------------------------------------------ the cranes

double& Joints::at(const JointKey& k) {
  if (k == "slew") return slew;
  if (k == "trolley") return trolley;
  if (k == "luff") return luff;
  if (k == "tele") return tele;
  if (k == "inner") return inner;
  if (k == "outer") return outer;
  if (k == "ext") return ext;
  return rope;
}

double Joints::at(const JointKey& k) const { return const_cast<Joints*>(this)->at(k); }

const CraneSpec& specOf(const CraneType& type) { return type == "tower" ? TOWER_CRANE : type == "mobile" ? MOBILE_CRANE : LOADER_CRANE; }

// ------------------------------------------------------------------------------------ kinematics

std::array<double, 2> boomDir(double slew) { return {-std::sin(slew), -std::cos(slew)}; }

double boomLength(double tele) {
  const MobileGeometry& g = MOBILE_GEOMETRY;
  return g.length0 + clampC(tele, 0, 1) * (g.length1 - g.length0);
}

double outerLength(double ext) {
  const LoaderGeometry& g = LOADER_GEOMETRY;
  return g.outer0 + clampC(ext, 0, 1) * (g.outer1 - g.outer0);
}

HeadAt headOf(const CraneType& type, const Joints& q) {
  if (type == "tower") return {q.trolley, TOWER_GEOMETRY.sheave};
  if (type == "mobile") {
    const MobileGeometry& g = MOBILE_GEOMETRY;
    const double l = boomLength(q.tele) + g.head;
    return {g.footA + l * std::cos(q.luff), g.footY + l * std::sin(q.luff)};
  }
  const LoaderGeometry& g = LOADER_GEOMETRY;
  const double o = q.inner + q.outer;
  const double l2 = outerLength(q.ext);
  return {g.pinA + g.inner * std::cos(q.inner) + l2 * std::cos(o), g.pinY + g.inner * std::sin(q.inner) + l2 * std::sin(o)};
}

std::vector<SlewMass> boomMasses(const CraneType& type, const Joints& q) {
  if (type == "tower") return {{TOWER_GEOMETRY.trolleyMass, q.trolley, 0, TOWER_GEOMETRY.sheave + 0.3}};
  if (type == "mobile") {
    const MobileGeometry& g = MOBILE_GEOMETRY;
    const double d = g.boomCg * boomLength(q.tele) + g.boomCgPlus;
    return {{g.boomMass, g.footA + d * std::cos(q.luff), 0, g.footY + d * std::sin(q.luff)}};
  }
  const LoaderGeometry& g = LOADER_GEOMETRY;
  const double kx = g.pinA + g.inner * std::cos(q.inner);
  const double ky = g.pinY + g.inner * std::sin(q.inner);
  const double o = q.inner + q.outer;
  const double half = outerLength(q.ext) * 0.45;
  return {
      {g.innerMass, g.pinA + (g.inner / 2) * std::cos(q.inner), 0, g.pinY + (g.inner / 2) * std::sin(q.inner)},
      {g.outerMass, kx + half * std::cos(o), 0, ky + half * std::sin(o)},
  };
}

// ------------------------------------------------------------------------------------ the load charts

std::vector<std::array<double, 2>> supportsFor(const CraneSpec& spec, const Setting& setting) {
  const auto& o = spec.outriggers;
  if (!o || setting == "tyres" || setting == "fixed") return spec.feet;
  const double e = setting == "full" ? 1 : setting == "half" ? 0.5 : 0;
  std::vector<std::array<double, 2>> pads;
  for (const OutriggerBeam& b : o->beams) pads.push_back({b.side * (o->padIn + e * (o->padOut - o->padIn)), b.z});
  // (the loader crane's wheels stay on the ground beside its two pads)
  if (spec.type == "loader") pads.insert(pads.end(), spec.feet.begin(), spec.feet.end());
  return pads;
}

std::vector<Weight> craneWeights(const CraneSpec& spec, const Joints& q, const std::vector<Weight>& extra) {
  std::vector<Weight> out = spec.fixed;
  const auto [dx, dz] = boomDir(q.slew);
  const auto add = [&](const SlewMass& m) {
    // (right of the boom's direction (dx, dz) is (−dz, dx))
    out.push_back({m.t, spec.slewX + m.a * dx - m.s * dz, m.y, spec.slewZ + m.a * dz + m.s * dx});
  };
  for (const SlewMass& m : spec.slewing) add(m);
  for (const SlewMass& m : boomMasses(spec.type, q)) add(m);
  out.insert(out.end(), extra.begin(), extra.end());
  return out;
}

double towerCapacity(double r) {
  const TowerGeometry& g = TOWER_GEOMETRY;
  return clampC(std::min(g.maxLoad, g.moment / std::max(r, 1.0) - g.loss), 0, g.maxLoad);
}

const LoadChart& chartOf(const CraneType& type) {
  const auto known = charts.find(type);
  if (known != charts.end()) return known->second;
  LoadChart c;
  if (type == "tower") {
    std::vector<double> radii;
    for (double r = 2.5; r <= 50; r += 2.5) radii.push_back(r);
    std::vector<double> col;
    for (const double r : radii) col.push_back(towerCapacity(r));
    c = {{0}, radii, {{"fixed", {col}}}};
  } else if (type == "mobile") {
    c = {MOBILE_GEOMETRY.lengths, MOBILE_RADII, {{"full", mobileChart("full")}, {"half", mobileChart("half")}, {"zero", mobileChart("zero")}, {"tyres", mobileChart("tyres")}}};
  } else {
    c = {{0}, LOADER_RADII, {{"full", loaderChart("full")}, {"half", loaderChart("half")}, {"zero", loaderChart("zero")}, {"tyres", loaderChart("tyres")}}};
  }
  return charts[type] = std::move(c);
}

double capacity(const CraneType& type, const Setting& setting, double radius, double length) {
  if (type == "tower") return towerCapacity(radius);
  const LoadChart& c = chartOf(type);
  const auto found = c.t.find(setting);
  if (found == c.t.end()) return 0;
  const auto& table = found->second;
  const std::vector<double>& rs = c.radii;
  const auto atLength = [&](const std::vector<double>& col) -> double {
    if (radius <= rs[0]) {
      // (closer in than the first radius: as at the first one it may reach)
      for (const double v : col)
        if (v > 0) return v;
      return 0;
    }
    if (radius > rs[rs.size() - 1]) return 0;
    size_t i = 1;
    while (rs[i] < radius) i++;
    const double a = col[i - 1];
    const double b = col[i];
    // (a radius closer in than it can reach at this length: as at the first it reaches)
    if (a == 0 && b > 0) return b;
    const double k = (radius - rs[i - 1]) / (rs[i] - rs[i - 1]);
    return a + (b - a) * k;
  };
  const std::vector<double>& ls = c.lengths;
  if (ls.size() == 1) return atLength(table[0]);
  if (length <= ls[0]) return atLength(table[0]);
  if (length >= ls[ls.size() - 1]) return atLength(table[ls.size() - 1]);
  size_t j = 1;
  while (ls[j] < length) j++;
  const double k = (length - ls[j - 1]) / (ls[j] - ls[j - 1]);
  const double a = atLength(table[j - 1]);
  const double b = atLength(table[j]);
  // (a radius one length can reach and the next not: what the one allows, nearing nothing)
  return a + (b - a) * k;
}

// ------------------------------------------------------------------------------------ wind

double windAtHeight(double wind10, double height) { return std::max(0.0, wind10) * std::pow(std::max(2.0, height) / 10, 0.16); }

double gust(double t, double peak) {
  const double g = 0.5 + 0.3 * std::sin(t * 0.37) + 0.15 * std::sin(t * 1.13 + 1) + 0.05 * std::sin(t * 2.9 + 2);
  return 1 + (std::max(1.0, peak) - 1) * std::max(0.0, std::min(1.0, g));
}

CraneCommand noCommand() { return CraneCommand{0, 0, 0, 0, false, 0, 0, false}; }

// ------------------------------------------------------------------------------------ the crane

Crane::Crane(const CraneType& type_) : spec(specOf(type_)), type(type_) {
  const auto& o = spec.outriggers;
  const size_t n = o ? o->beams.size() : 0;
  beams.assign(n, 0);
  jacks.assign(n, 0);
  padGround.assign(n, 0);
  q = joints();
  q.rope = type == "loader" ? spec.link : type == "tower" ? 12 : spec.hoist->min;
  if (type == "tower") q.trolley = 14;
  lmi = Lmi{};
  lmi.setting = type == "tower" ? "fixed" : "tyres";
  lmi.windLimit = spec.windLimit;
  lmi.levelGood = spec.levelGood;
}

// ---------------------------------------------------------------------------- where it stands

void Crane::place(double x, double y, double z, double yaw, double gx, double gz) {
  base.x = x;
  base.y = y;
  base.z = z;
  base.yaw = yaw;
  terrain.gx = gx;
  terrain.gz = gz;
  headWorld(head);
  head.vx = head.vy = head.vz = 0;
  Body& h = hook;
  h.x = head.x;
  h.z = head.z;
  h.y = head.y - q.rope;
  h.vx = h.vy = h.vz = 0;
}

std::array<double, 2> Crane::toCarrier(double x, double z) const {
  const Base& b = base;
  const double c = std::cos(b.yaw);
  const double s = std::sin(b.yaw);
  const double dx = x - b.x;
  const double dz = z - b.z;
  return {dx * c - dz * s, dx * s + dz * c};
}

Crane::Slope Crane::tilt() const { return {terrain.gx + levelFix.gx, terrain.gz + levelFix.gz}; }

double Crane::incline() const {
  const Slope t = tilt();
  return std::atan(std::hypot(t.gx, t.gz)) / DEG;
}

double Crane::radius() const {
  const auto [x, z] = toCarrier(hook.x, hook.z);
  return std::hypot(x - spec.slewX, z - spec.slewZ);
}

// ---------------------------------------------------------------------------- the outriggers

bool Crane::padsDown(const CraneWorld& world) {
  if (!spec.outriggers) return true;
  for (size_t i = 0; i < jacks.size(); i++)
    if (!(jacks[i] >= gap(static_cast<int>(i), world) - 0.005)) return false;
  return true;
}

double Crane::gap(int i, const CraneWorld& world) {
  const OutriggerSpec& o = *spec.outriggers;
  const OutriggerBeam& b = o.beams[i];
  const double x = b.side * (o.padIn + beams[i] * (o.padOut - o.padIn));
  const Point& p = toWorldFlat(x, b.z);
  const double ground = world.ground(p.x, p.z);
  const double carrier = base.y + terrain.gx * x + terrain.gz * b.z;
  const double under = std::isfinite(ground) ? clampC(ground - carrier, -o.stroke, o.clearance) : 0;
  padGround[i] = under;
  return clampC(o.clearance - under, 0, o.stroke);
}

Crane::Point& Crane::toWorldFlat(double x, double z) {
  const Base& b = base;
  const double c = std::cos(b.yaw);
  const double s = std::sin(b.yaw);
  flat_.x = b.x + x * c + z * s;
  flat_.z = b.z - x * s + z * c;
  flat_.y = b.y;
  return flat_;
}

double Crane::beamSetting() const { return beams.empty() ? 0 : *std::min_element(beams.begin(), beams.end()); }

bool Crane::onOutriggers(const CraneWorld& world) {
  const auto& o = spec.outriggers;
  if (!o) return false;
  if (!padsDown(world)) return false;
  return o->freeLift <= 0 || lift >= o->freeLift - 0.01;
}

Setting Crane::setting(const CraneWorld& world) {
  if (type == "tower") return "fixed";
  if (!onOutriggers(world)) return "tyres";
  const double e = beamSetting();
  return e >= 0.98 ? "full" : e >= 0.48 ? "half" : "zero";
}

bool Crane::stowed() const {
  if (type == "tower") return false;
  if (type == "mobile") return q.luff < 1.5 * DEG && q.tele < 0.01 && std::abs(wrapC(q.slew)) < 1.5 * DEG;
  const LoaderGeometry& g = LOADER_GEOMETRY;
  return std::abs(q.inner - g.stowInner) < 3 * DEG && std::abs(q.outer - g.stowOuter) < 4 * DEG && q.ext < 0.01 && std::abs(q.slew) < 3 * DEG;
}

bool Crane::outriggersIn() const {
  for (const double b : beams)
    if (!(b < 0.01)) return false;
  for (const double j : jacks)
    if (!(j < 0.01)) return false;
  return true;
}

void Crane::stepOutriggers(double dt, const CraneWorld& world) {
  if (!spec.outriggers) return;
  const OutriggerSpec& o = *spec.outriggers;
  const CraneCommand& c = cmd;
  // (the beams move only while the jacks are up, and in only with the boom in its rest: nothing is taken from under a working crane)
  bool jacksUp = true;
  for (const double j : jacks)
    if (!(j < 0.01)) jacksUp = false;
  if (c.beams != 0 && jacksUp && (c.beams > 0 || stowed())) {
    for (size_t i = 0; i < beams.size(); i++) beams[i] = clampC(beams[i] + (c.beams * dt) / o.beamTime, 0, 1);
  }
  const int n = static_cast<int>(jacks.size());
  std::vector<double> gaps(n);
  for (int i = 0; i < n; i++) gaps[i] = gap(i, world);
  if (c.jacks > 0) {
    // down: each to the ground, then all together until the wheels are free (the loader crane's: until they bear)
    bool down = true;
    for (int i = 0; i < n; i++)
      if (!(jacks[i] >= gaps[i] - 0.002)) down = false;
    const double want = o.freeLift > 0 ? o.freeLift + 0.02 : 0.02;
    for (int i = 0; i < n; i++) {
      const double target = std::min(o.stroke, gaps[i] + want + levelOffset(i));
      const double step = o.jackSpeed * dt;
      if (!down) jacks[i] = std::min(gaps[i], jacks[i] + step);
      else jacks[i] = std::min(target, jacks[i] + step);
    }
  } else if (c.jacks < 0 && stowed()) {
    for (int i = 0; i < n; i++) jacks[i] = std::max(0.0, jacks[i] - o.jackSpeed * dt);
    // (down on its wheels again: the levelling is undone with it)
    bool onWheels = true;
    for (int i = 0; i < n; i++)
      if (!(jacks[i] <= gaps[i])) onWheels = false;
    if (onWheels) {
      levelFix.gx = 0;
      levelFix.gz = 0;
    }
  }
  if (c.level && padsDown(world)) {
    // the automatic levelling: the jacks bring the carrier level, a little at a time
    const double rate = 0.006 * dt;
    const Slope t = tilt();
    const double fx = clampC(-t.gx, -rate, rate);
    const double fz = type == "loader" ? 0 : clampC(-t.gz, -rate, rate);
    levelFix.gx += fx;
    levelFix.gz += fz;
    for (int i = 0; i < n; i++) jacks[i] = clampC(jacks[i] + padX(i) * fx + o.beams[i].z * fz, gaps[i], o.stroke);
  }
  updateLift(world);
}

double Crane::levelOffset(int i) const {
  const OutriggerSpec& o = *spec.outriggers;
  return padX(i) * levelFix.gx + o.beams[i].z * levelFix.gz;
}

double Crane::padX(int i) const {
  const OutriggerSpec& o = *spec.outriggers;
  const OutriggerBeam& b = o.beams[i];
  return b.side * (o.padIn + beams[i] * (o.padOut - o.padIn));
}

void Crane::updateLift(const CraneWorld& world) {
  if (!spec.outriggers) return;
  const OutriggerSpec& o = *spec.outriggers;
  double sum = 0;
  int bearing = 0;
  const int n = static_cast<int>(jacks.size());
  for (int i = 0; i < n; i++) {
    const double up = jacks[i] - gap(i, world) - levelOffset(i);
    if (up > -0.001) {
      sum += std::max(0.0, up);
      bearing++;
    }
  }
  // (lifted only once every pad bears: until then it stands on its wheels)
  lift = bearing == n && o.freeLift > 0 ? sum / bearing : 0;
  bool allUp = true;
  for (const double j : jacks)
    if (!(j < 0.001)) allUp = false;
  if (bearing < n && levelFix.gx != 0 && allUp) {
    levelFix.gx = 0;
    levelFix.gz = 0;
  }
}

// ---------------------------------------------------------------------------- the drives

std::vector<Crane::Drive> Crane::drives() const {
  const CraneCommand& c = cmd;
  const CraneSpec& sp = spec;
  std::vector<Drive> out = {{"slew", c.slew, sp.slew}};
  if (type == "tower") {
    const TowerGeometry& g = TOWER_GEOMETRY;
    out.push_back({"trolley", c.boom, {g.trolleyMin, g.trolleyMax, 1.0, 0.45}});
  } else if (type == "mobile") {
    const MobileGeometry& g = MOBILE_GEOMETRY;
    out.push_back({"luff", c.boom, {0, g.luffMax, 0.035, 0.05}});
    // (24.4 m of telescopes in 70 s)
    out.push_back({"tele", c.tele, {0, 1, 0.0143, 0.03}});
  } else {
    const LoaderGeometry& g = LOADER_GEOMETRY;
    out.push_back({"inner", c.hoist, {g.innerMin, g.innerMax, 0.12, 0.25}});
    out.push_back({"outer", c.boom, {g.outerMin, g.outerMax, 0.15, 0.3}});
    out.push_back({"ext", c.tele, {0, 1, 0.06, 0.15}});
  }
  if (sp.hoist) out.push_back({"rope", -c.hoist, *sp.hoist});
  return out;
}

Crane::Rate Crane::headRate(const JointKey& key) {
  if (key == "rope") return {0, -1};
  if (key == "slew") return {0, 0};
  const double e = key == "trolley" ? 0.01 : key == "tele" || key == "ext" ? 0.001 : 0.0005;
  const HeadAt h0 = headOf(type, q);
  double& j = q.at(key);
  const double was = j;
  j = was + e;
  const HeadAt h1 = headOf(type, q);
  j = was;
  return {(h1.a - h0.a) / e, (h1.y - h0.y) / e};
}

bool Crane::momentUp(const JointKey& key, double dir) {
  if (dir == 0 || key == "slew") return false;
  if (key == "rope") return dir < 0;
  const Rate r = headRate(key);
  // (the mobile crane's telescopes out: its boom weighs further out even where the head comes no further)
  if (key == "tele") return dir > 0;
  return r.da * dir > 1e-4;
}

void Crane::stepDrives(double dt, const CraneWorld& world) {
  const CraneCommand& c = cmd;
  const Lmi& l = lmi;
  const double fine = c.fine ? 0.22 : 1;
  // the hoist slows with the load on it
  const CraneSpec& sp = spec;
  const double heavy = clampC((l.load - sp.hoistLight) / std::max(0.1, sp.hoistHeavy - sp.hoistLight), 0, 1);
  events.cut = false;
  const bool working = mayWork(world);
  for (const Drive& d : drives()) {
    const DriveSpec& s = d.spec;
    double want = clampC(d.cmd, -1, 1) * s.speed * fine;
    double accel = s.accel;
    // (a heavy load is hoisted slower, and started and stopped more gently)
    if (d.key == "rope") {
      want *= 1 - 0.5 * heavy;
      accel *= 1 - 0.6 * heavy;
    }
    const double dir = signC(want);
    // the limiter: past 90 % what raises the moment slows down, at 100 % it stops at once; so does everything but coming in when the carrier is not level
    bool cut = false;
    if (dir != 0 && momentUp(d.key, dir)) {
      if (l.stop || !working) {
        want = 0;
        cut = true;
        events.cut = true;
      } else if (l.warn) want *= 0.5;
    }
    if (want != 0) {
      const Rate r = headRate(d.key);
      // creeping near what the hook or the load comes down on …
      if (r.dy * want < 0 && clearance < 2.5) want *= std::max(0.2, clearance / 2.5);
      // … and while a load still stands, it is taken up slowly once the rope is nearly tight
      if (r.dy * want > 0 && load && resting && slack < 0.8) {
        // (the slack taken in briskly, slower and slower as the rope tightens)
        const double creep = d.key == "rope" ? std::max(0.1, slack * 1.2) : s.speed * 0.3;
        want = signC(want) * std::min(std::abs(want), creep);
        v.at(d.key) = clampC(v.at(d.key), -creep, creep);
      }
    }
    // the mobile crane's boom may come down into its rest only straight ahead
    if (d.key == "luff" && want < 0 && q.luff < 4 * DEG && std::abs(wrapC(q.slew)) > 1.5 * DEG) want = 0;
    if (d.key == "slew" && type == "mobile" && q.luff < 3 * DEG) want = 0;
    const JointKey& k = d.key;
    const double vk = v.at(k);
    const double step = accel * dt;
    v.at(k) = cut && signC(vk) == dir ? 0 : std::abs(want - vk) <= step ? want : vk + signC(want - vk) * step;
    double next = q.at(k) + v.at(k) * dt;
    if (next < s.min || next > s.max) {
      next = clampC(next, s.min, s.max);
      v.at(k) = 0;
    }
    q.at(k) = s.endless ? wrapC(next) : next;
  }
  if (!sp.hoist) q.rope = sp.link;
}

bool Crane::mayWork(const CraneWorld& world) const {
  (void)world;
  return type == "tower" || incline() <= spec.levelLimit;
}

// ---------------------------------------------------------------------------- the load

double Crane::mass() const { return spec.hook + (load ? load->t : 0); }

double Crane::drop() const { return spec.hookDrop + (load ? load->sling + 2 * load->hy : 0); }

std::array<double, 2> Crane::footprint() const {
  if (!load) return {0.3, 0.3};
  const HookLoad& l = *load;
  const double c = std::abs(std::cos(l.yaw));
  const double s = std::abs(std::sin(l.yaw));
  return {l.hx * c + l.hz * s, l.hx * s + l.hz * c};
}

void Crane::attach(const HookLoad& l, double x, double top, double z) {
  load = l;
  Body& h = hook;
  h.x = x;
  h.z = z;
  h.y = top + l.sling + spec.hookDrop;
  h.vx = h.vy = h.vz = 0;
  resting = true;
  // (the rope as long as it now hangs – no jerk on hooking in)
  const double d = std::hypot(head.x - h.x, head.y - h.y, head.z - h.z);
  if (spec.hoist && d > q.rope) q.rope = std::min(spec.hoist->max, d);
}

std::optional<HookLoad> Crane::release() {
  std::optional<HookLoad> l = load;
  load = std::nullopt;
  return l;
}

// ---------------------------------------------------------------------------- a step

void Crane::step(double dt, const CraneWorld& world) {
  time += dt;
  CraneEvents& ev = events;
  ev.hit = 0;
  ev.landed = 0;
  ev.liftedOff = false;
  if (tipped) {
    // over it goes, about its tipping line, faster and faster until it lies
    Tipped& t = *tipped;
    t.rate += 1.8 * dt;
    t.angle = std::min(1.35, t.angle + t.rate * dt);
    return;
  }
  stepOutriggers(dt, world);
  const double px = head.x;
  const double py = head.y;
  const double pz = head.z;
  stepDrives(dt, world);
  headWorld(head);
  head.vx = (head.x - px) / dt;
  head.vy = (head.y - py) / dt;
  head.vz = (head.z - pz) / dt;
  stepHook(dt, world);
  updateLmi(dt, world);
}

void Crane::stepHook(double dt, const CraneWorld& world) {
  Body& h = hook;
  const Body& hd = head;
  const double m = mass();
  const double kg = m * 1000;
  // the rope: an elastic line, pulling only
  const double dx = hd.x - h.x;
  const double dy = hd.y - h.y;
  const double dz = hd.z - h.z;
  double d = std::hypot(dx, dy, dz);
  if (d == 0) d = 1e-6;
  const double nx = dx / d;
  const double ny = dy / d;
  const double nz = dz / d;
  const double k = (kg * G) / STRETCH;
  const double c = 2 * ROPE_DAMPING * std::sqrt(k * kg);
  const double stretch = d - q.rope;
  slack = -stretch;
  // (the damping works on how fast the rope stretches – not on rope paid out or hauled in by the winch)
  const double rate = nx * (hd.vx - h.vx) + ny * (hd.vy - h.vy) + nz * (hd.vz - h.vz) - v.rope;
  const double T = stretch > 0 ? std::max(0.0, k * stretch + c * rate) : 0;
  tension = T / 1000;
  double fx = T * nx;
  double fy = T * ny - kg * G;
  double fz = T * nz;
  // the air: it damps the swing, and the wind pushes on the load
  const CraneWind w = world.wind();
  const double at = windAtHeight(1, h.y - base.y) * gust(time * 0.9 + 3, w.gust);
  const double wx = w.x * at - h.vx;
  const double wz = w.z * at - h.vz;
  const double area = load ? load->area : 0.5;
  const double wv = std::hypot(wx, wz);
  fx += DRAG * area * wv * wx - AIR * kg * h.vx;
  fz += DRAG * area * wv * wz - AIR * kg * h.vz;
  fy -= AIR * kg * h.vy;
  h.vx += (fx / kg) * dt;
  h.vy += (fy / kg) * dt;
  h.vz += (fz / kg) * dt;
  h.x += h.vx * dt;
  h.y += h.vy * dt;
  h.z += h.vz * dt;
  // what it comes down on
  const auto [fx2, fz2] = footprint();
  const double below = drop();
  const double bottom = h.y - below;
  const double floor = world.floor(h.x, h.z, fx2, fz2, bottom + 0.35);
  const bool wasResting = resting;
  if (std::isfinite(floor) && bottom < floor) {
    h.y = floor + below;
    if (h.vy < 0) {
      if (!wasResting && load) events.landed = -h.vy;
      h.vy = 0;
    }
    resting = true;
  } else resting = std::isfinite(floor) && bottom < floor + 0.004 && std::abs(h.vy) <= 0.02;
  if (resting) {
    // standing: it does not slide (what pulls it aside drags it only slowly)
    const double keep = std::max(0.0, 1 - 14 * dt);
    h.vx *= keep;
    h.vz *= keep;
  }
  if (wasResting && !resting && load) events.liftedOff = true;
  clearance = std::isfinite(floor) ? std::max(0.0, h.y - below - floor) : 99;
  // what it runs into
  const double top = h.y + 0.5;
  for (const CraneBox3& b : world.boxes()) {
    if (h.x + fx2 <= b.x0 || h.x - fx2 >= b.x1 || h.z + fz2 <= b.z0 || h.z - fz2 >= b.z1 || top <= b.y0 || h.y - below >= b.y1) continue;
    // out the shortest way
    const std::array<std::array<double, 2>, 5> pushes = {{
        {b.x0 - (h.x + fx2), 0},
        {b.x1 - (h.x - fx2), 1},
        {b.z0 - (h.z + fz2), 2},
        {b.z1 - (h.z - fz2), 3},
        {b.y1 - (h.y - below), 4},
    }};
    std::array<double, 2> best = pushes[0];
    for (const auto& p : pushes)
      if (std::abs(p[0]) < std::abs(best[0])) best = p;
    const double axis = best[1];
    double speed = 0;
    if (axis <= 1) {
      h.x += best[0];
      speed = std::abs(h.vx);
      h.vx *= -0.15;
    } else if (axis <= 3) {
      h.z += best[0];
      speed = std::abs(h.vz);
      h.vz *= -0.15;
    } else {
      h.y += best[0];
      speed = std::max(0.0, -h.vy);
      h.vy = 0;
    }
    events.hit = std::max(events.hit, speed);
  }
  swing = std::hypot(h.x - hd.x, h.z - hd.z);
}

// ---------------------------------------------------------------------------- the limiter and the tipping check

Crane::Loads Crane::loads(const CraneWorld& world, const std::vector<Weight>& extraW) {
  std::vector<Weight> weights = craneWeights(spec, q, extraW);
  // the rope pulls on the head: down by what it holds, aside by how the load hangs off the plumb line
  const double T = tension;
  const HeadAt hd = headOf(type, q);
  const auto [dx, dz] = boomDir(q.slew);
  const double hx = spec.slewX + hd.a * dx;
  const double hz = spec.slewZ + hd.a * dz;
  const Body& h = hook;
  const auto [cx, cz] = toCarrier(h.x, h.z);
  double d = std::hypot(head.x - h.x, head.y - h.y, head.z - h.z);
  if (d == 0) d = 1;
  const double vy = (T * (head.y - h.y)) / d;
  weights.push_back({vy / G, hx, hd.y, hz});
  std::vector<SideForce> sides = {{(T * (cx - hx)) / d, (T * (cz - hz)) / d, hd.y + lift}};
  // the wind on the boom or the jib, at its middle
  const CraneWind wind = world.wind();
  const auto [wx, wz] = windInCarrier(wind.x, wind.z);
  const double area = type == "tower" ? 28 : type == "mobile" ? 6 + boomLength(q.tele) * 0.45 : 3;
  const double mid = hd.y * 0.6;
  const double ww = windAtHeight(1, mid) * gust(time * 0.9 + 3, wind.gust);
  const double ws = std::hypot(wx * ww, wz * ww);
  sides.push_back({(0.61 * area * ws * wx * ww) / 1000, (0.61 * area * ws * wz * ww) / 1000, mid});
  const Setting set = setting(world);
  auto supports = type == "tower" || !spec.outriggers ? supportsFor(spec, "fixed") : set == "tyres" ? supportsFor(spec, "tyres") : padSupports();
  return {std::move(weights), std::move(sides), std::move(supports)};
}

std::vector<std::array<double, 2>> Crane::padSupports() const {
  const OutriggerSpec& o = *spec.outriggers;
  std::vector<std::array<double, 2>> pads;
  for (size_t i = 0; i < o.beams.size(); i++) pads.push_back({padX(static_cast<int>(i)), o.beams[i].z});
  if (type == "loader") pads.insert(pads.end(), spec.feet.begin(), spec.feet.end());
  return pads;
}

std::array<double, 2> Crane::windInCarrier(double x, double z) const {
  const double c = std::cos(base.yaw);
  const double s = std::sin(base.yaw);
  return {x * c - z * s, x * s + z * c};
}

void Crane::updateLmi(double dt, const CraneWorld& world) {
  Lmi& l = lmi;
  // the measured load: the rope's pull, smoothed for the display – and taken quickly for the cut-off
  measured_ += (tension / G - measured_) * (1 - std::exp(-dt / 0.3));
  quick_ += (tension / G - quick_) * (1 - std::exp(-dt / 0.02));
  l.load = std::max(0.0, measured_);
  l.setting = setting(world);
  // (the radius the chart is read at: the head's, where the hook hangs at rest)
  const HeadAt hd = headOf(type, q);
  l.radius = std::max(0.0, hd.a);
  l.length = type == "mobile" ? boomLength(q.tele) : type == "loader" ? LOADER_GEOMETRY.inner + outerLength(q.ext) : TOWER_GEOMETRY.jibLength;
  l.angle = (type == "mobile" ? q.luff : type == "loader" ? q.inner : 0) / DEG;
  l.capacity = capacity(type, l.setting, l.radius, l.length);
  const auto ratio = [&](double t) { return l.capacity > 0.01 ? t / l.capacity : t > 0.05 ? 9.99 : 0; };
  l.ratio = ratio(l.load);
  const double quick = ratio(std::max(0.0, quick_));
  l.incline = incline();
  l.notLevel = type != "tower" && l.incline > spec.levelLimit;
  // the wind at the boom head, with its gusts
  const CraneWind w = world.wind();
  l.wind = windAtHeight(std::hypot(w.x, w.z), head.y - base.y) * gust(time * 0.9 + 3, w.gust);
  l.windLimit = spec.windLimit;
  const double g = hook.y - drop() - world.floor(hook.x, hook.z, 0.2, 0.2, hook.y);
  l.hookHeight = std::isfinite(g) ? std::max(0.0, g) : 0;
  l.warn = l.ratio >= 0.9;
  // (cut off at 100 % – the quick reading while the load stands: before an overload leaves the ground –, let go again below 98 %)
  const bool standing = resting && load.has_value();
  cutting_ = cutting_ ? l.ratio >= 0.98 || quick >= 0.98 : l.ratio >= 1 || (standing && quick >= 1);
  l.stop = cutting_;
  // the tipping check
  const Loads t = loads(world, extra);
  const Stability st = stability(t.weights, t.sides, t.supports);
  l.tipping = st.u;
  overT_ = st.u >= 1 ? overT_ + dt : 0;
  if (overT_ > 0.25 && st.edge >= 0) {
    const auto lines = tippingLines(st.hull);
    const TippingLine& ln = lines[st.edge];
    tipped = Tipped{ln.px, ln.pz, ln.nx, ln.nz, 0, 0.05};
  }
}

std::vector<TippingLine> linesOf(const std::vector<std::array<double, 2>>& supports) { return tippingLines(convexHull(supports)); }

}  // namespace fs

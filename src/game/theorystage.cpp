// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/theorystage.ts @ v1.7.7
#include "game/theorystage.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/js.h"

namespace fs::theory {

namespace {
/** The longest piece of a path (m): short enough for a smooth heading along curves. */
constexpr double STEP = 0.5;
constexpr double PI_ = 3.14159265358979323846;
}  // namespace

PathPoint& pathPoint(const Path& p, double s, PathPoint& out) {
  const std::vector<double>& pts = p.pts;
  const int n = static_cast<int>(p.acc.size());
  if (n < 2) {
    out.x = pts.size() > 0 ? pts[0] : 0;
    out.z = pts.size() > 1 ? pts[1] : 0;
    return out;
  }
  int a;
  if (s <= 0) a = 0;
  else if (s >= p.len) a = n - 2;
  else {
    int lo = 0;
    int hi = n - 1;
    while (hi - lo > 1) {
      const int mid = (lo + hi) >> 1;
      if (p.acc[static_cast<size_t>(mid)] <= s) lo = mid;
      else hi = mid;
    }
    a = lo;
  }
  const int b = a + 1;
  double seg = p.acc[static_cast<size_t>(b)] - p.acc[static_cast<size_t>(a)];
  if (seg == 0) seg = 1e-9;
  const double ax = pts[static_cast<size_t>(a * 2)];
  const double az = pts[static_cast<size_t>(a * 2 + 1)];
  out.dx = (pts[static_cast<size_t>(b * 2)] - ax) / seg;
  out.dz = (pts[static_cast<size_t>(b * 2 + 1)] - az) / seg;
  const double u = s - p.acc[static_cast<size_t>(a)];
  out.x = ax + out.dx * u;
  out.z = az + out.dz * u;
  return out;
}

double yawOf(double dx, double dz) { return std::atan2(-dx, -dz); }

// ---------------------------------------------------------------- Trace

void Trace::push(double x, double z) {
  const size_t n = pts.size();
  if (n > 0) {
    const double d = std::hypot(x - pts[n - 2], z - pts[n - 1]);
    if (d < 1e-6) return;
    len += d;
  }
  pts.push_back(x);
  pts.push_back(z);
  lens.push_back(len);
}

Trace& Trace::to(double x, double z) {
  const size_t n = pts.size();
  if (n == 0) {
    push(x, z);
    return *this;
  }
  const double x0 = pts[n - 2];
  const double z0 = pts[n - 1];
  const int k = std::max(1, static_cast<int>(std::ceil(std::hypot(x - x0, z - z0) / STEP)));
  for (int i = 1; i <= k; i++) push(x0 + ((x - x0) * i) / k, z0 + ((z - z0) * i) / k);
  return *this;
}

Trace& Trace::curve(double c1x, double c1z, double c2x, double c2z, double x, double z) {
  const size_t n = pts.size();
  const double x0 = pts[n - 2];
  const double z0 = pts[n - 1];
  const auto at = [&](double t) -> std::array<double, 2> {
    const double u = 1 - t;
    return {u * u * u * x0 + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * x, u * u * u * z0 + 3 * u * u * t * c1z + 3 * u * t * t * c2z + t * t * t * z};
  };
  // (its length, roughly, settles how many points it gets)
  double rough = 0;
  double px = x0;
  double pz = z0;
  for (int i = 1; i <= 32; i++) {
    const auto [qx, qz] = at(i / 32.0);
    rough += std::hypot(qx - px, qz - pz);
    px = qx;
    pz = qz;
  }
  const int k = std::max(2, static_cast<int>(std::ceil(rough / (STEP * 0.8))));
  for (int i = 1; i <= k; i++) {
    const auto [qx, qz] = at(static_cast<double>(i) / k);
    push(qx, qz);
  }
  return *this;
}

Trace& Trace::arc(double cx, double cz, double r, double a0, double a1) {
  const int k = std::max(2, static_cast<int>(std::ceil((std::abs(a1 - a0) * r) / STEP)));
  for (int i = 0; i <= k; i++) {
    const double a = a0 + ((a1 - a0) * i) / k;
    push(cx + std::cos(a) * r, cz + std::sin(a) * r);
  }
  return *this;
}

Trace& Trace::join(double hx, double hz, double x, double z, double ex, double ez) {
  const size_t n = pts.size();
  const double x0 = pts[n - 2];
  const double z0 = pts[n - 1];
  const double dx = x - x0;
  const double dz = z - z0;
  const double cross = hx * ez - hz * ex;
  const double d = std::hypot(dx, dz);
  if (std::abs(cross) < 1e-3) {
    // in line – or side by side (a change of lane): an S
    if (std::abs(dx * hz - dz * hx) < 0.05) return to(x, z);
    return curve(x0 + (hx * d) / 2.6, z0 + (hz * d) / 2.6, x - (ex * d) / 2.6, z - (ez * d) / 2.6, x, z);
  }
  // where the two lines meet: (x0, z0) + a·h = (x, z) − b·e
  const double a = (dx * ez - ex * dz) / cross;
  const double b = (hx * dz - dx * hz) / cross;
  if (a <= 0 || b <= 0) return curve(x0 + (hx * d) / 3, z0 + (hz * d) / 3, x - (ex * d) / 3, z - (ez * d) / 3, x, z);
  // (0.5523: the control points of a quarter circle drawn as a cubic curve)
  const double k = 0.5523;
  return curve(x0 + hx * a * k, z0 + hz * a * k, x - ex * b * k, z - ez * b * k, x, z);
}

Path Trace::path(const std::map<std::string, double>& marks) const {
  Path p{.pts = pts, .acc = lens, .len = len, .marks = {}};
  p.marks["end"] = len;
  for (const auto& [k, v] : marks) p.marks[k] = v;
  return p;
}

Path polyPath(const std::vector<double>& points, const std::map<std::string, double>& marks) {
  Trace t;
  for (size_t i = 0; i + 1 < points.size(); i += 2) t.to(points[i], points[i + 1]);
  return t.path(marks);
}

// ------------------------------------------------------------------ what is drawn

double LIFT(const SurfaceKind& kind) {
  if (kind == "asphalt") return 0.05;
  if (kind == "walk") return 0.17;
  if (kind == "island") return 0.19;
  if (kind == "apron") return 0.09;
  if (kind == "paint") return 0.065;
  if (kind == "red") return 0.058;
  if (kind == "steel") return 0.068;
  return 0;
}

Stage emptyStage() { return Stage{}; }

Stage& finish(Stage& st) {
  double minX = JS_INF;
  double minZ = JS_INF;
  double maxX = -JS_INF;
  double maxZ = -JS_INF;
  for (const Surface& s : st.surfaces) {
    for (size_t i = 0; i + 1 < s.ring.size(); i += 2) {
      minX = std::min(minX, s.ring[i]);
      maxX = std::max(maxX, s.ring[i]);
      minZ = std::min(minZ, s.ring[i + 1]);
      maxZ = std::max(maxZ, s.ring[i + 1]);
    }
  }
  st.bounds = {minX, minZ, maxX, maxZ};
  return st;
}

Stage finish(Stage&& st) {
  finish(st);
  return std::move(st);
}

bool inRing(const std::vector<double>& ring, double x, double z) {
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

bool covers(const Surface& s, double x, double z) {
  if (!inRing(s.ring, x, z)) return false;
  return !std::any_of(s.holes.begin(), s.holes.end(), [&](const std::vector<double>& h) { return inRing(h, x, z); });
}

double liftAt(const Stage& st, double x, double z) {
  double best = 0;
  for (const Surface& s : st.surfaces) {
    if (s.kind == "paint" || s.kind == "red" || s.kind == "steel") continue;
    if (LIFT(s.kind) > best && covers(s, x, z)) best = LIFT(s.kind);
  }
  return best;
}

// ---------------------------------------------------------------------- paint

Surface bar(double x0, double z0, double x1, double z1, double w, const SurfaceKind& kind) {
  double l = std::hypot(x1 - x0, z1 - z0);
  if (l == 0) l = 1;
  const double nx = (-(z1 - z0) / l) * (w / 2);
  const double nz = ((x1 - x0) / l) * (w / 2);
  return Surface{.kind = kind, .ring = {x0 + nx, z0 + nz, x1 + nx, z1 + nz, x1 - nx, z1 - nz, x0 - nx, z0 - nz}};
}

std::vector<Surface> dashes(double x0, double z0, double x1, double z1, double w, double dash, double gap) {
  const double l = std::hypot(x1 - x0, z1 - z0);
  std::vector<Surface> out;
  if (l < 0.01) return out;
  const double ux = (x1 - x0) / l;
  const double uz = (z1 - z0) / l;
  for (double s = 0; s < l - 0.05; s += dash + gap) {
    const double e = std::min(l, s + dash);
    out.push_back(bar(x0 + ux * s, z0 + uz * s, x0 + ux * e, z0 + uz * e, w));
  }
  return out;
}

Surface laneArrow(double x, double z, double yaw, const std::string& kind) {
  // drawn pointing north (−z) with the tip at the origin, then turned
  static const std::vector<double> STRAIGHT = {0, 0, 0.45, 1.4, 0.12, 1.4, 0.12, 5, -0.12, 5, -0.12, 1.4, -0.45, 1.4};
  static const std::vector<double> BEND = {-1.2, 1.2, -0.25, 0.55, -0.25, 1.0, 0.12, 1.4, 0.12, 5, -0.12, 5, -0.12, 1.6, -0.25, 1.45, -0.25, 1.85};
  const std::vector<double>& shape = kind == "straight" ? STRAIGHT : BEND;
  const double flip = kind == "right" ? -1 : 1;
  const double c = std::cos(yaw);
  const double s = std::sin(yaw);
  std::vector<double> ring;
  for (size_t i = 0; i < shape.size(); i += 2) {
    const double px = shape[i] * flip;
    const double pz = shape[i + 1];
    // turned by yaw: north (0, −1) goes to (−sin yaw, −cos yaw)
    ring.push_back(x + px * c + pz * s);
    ring.push_back(z - px * s + pz * c);
  }
  if (flip < 0) {
    // (mirrored: the outline runs the other way round – reverse it so that it stays a simple outline)
    std::vector<double> r;
    for (int i = static_cast<int>(ring.size()) - 2; i >= 0; i -= 2) {
      r.push_back(ring[static_cast<size_t>(i)]);
      r.push_back(ring[static_cast<size_t>(i + 1)]);
    }
    return Surface{.kind = "paint", .ring = r};
  }
  return Surface{.kind = "paint", .ring = ring};
}

// ------------------------------------------------------------- a crossroads

namespace {
/** the direction of each arm, away from the middle */
std::array<double, 2> OUT(Dir d) {
  switch (d) {
    case 'N': return {0, -1};
    case 'E': return {1, 0};
    case 'S': return {0, 1};
    default: return {-1, 0};
  }
}
const std::vector<Dir> DIRS = {'N', 'E', 'S', 'W'};
/**
 * The arms in the order the outline of a crossroads runs round them: from
 * the right-hand side of the traffic coming in on one arm (the west side of
 * the north arm) to the arm on that side (the west arm), and so on.
 */
const std::vector<Dir> AROUND = {'N', 'W', 'S', 'E'};

double halfOf(const Road& r) { return r.median / 2 + r.lanes * r.laneW + r.bike; }

/** The heading's right-hand side: (−hz, hx). */
std::array<double, 2> rightOf(double hx, double hz) { return {-hz, hx}; }

int dirIndex(Dir d) { return static_cast<int>(std::find(DIRS.begin(), DIRS.end(), d) - DIRS.begin()); }

void append(std::vector<double>& to, std::initializer_list<double> xs) { to.insert(to.end(), xs); }
void append(std::vector<double>& to, const std::vector<double>& xs) { to.insert(to.end(), xs.begin(), xs.end()); }
void append(std::vector<double>& to, const std::array<double, 2>& xs) { to.insert(to.end(), xs.begin(), xs.end()); }
}  // namespace

const std::vector<Dir> ARMS = DIRS;

const Road& Cross::road(Dir d) const { return d == 'N' || d == 'S' ? o.ns : o.ew; }

double Cross::half(Dir d) const { return halfOf(road(d)); }

/** where the box of the junction begins on an arm: half the width of the other road */
double Cross::edge(Dir d) const { return d == 'N' || d == 'S' ? halfOf(o.ew) : halfOf(o.ns); }

std::array<double, 2> Cross::at(Dir d, double along, double side) const {
  const auto [ox, oz] = OUT(d);
  const auto [rx, rz] = rightOf(-ox, -oz);
  return {ox * along + rx * side, oz * along + rz * side};
}

double Cross::xingAt(Dir d) const {
  const std::optional<ArmXing>& x = o.arms[d].xing;
  return x ? edge(d) + x->at : 0;
}

/** where the stop or give-way line lies on an arm (from the middle) */
double Cross::lineAt(Dir d) const {
  const ArmOpts& a = o.arms[d];
  const std::optional<ArmXing>& x = a.xing;
  if (a.line == "stop") return x ? edge(d) + x->at + x->w / 2 + 1.2 : edge(d) + 2.6;
  return x && x->at < 1 ? edge(d) + x->at + x->w / 2 + 0.6 : edge(d) + 0.45;
}

Cross crossroads(const CrossOpts& o) { return Cross(o); }

Cross::Cross(const CrossOpts& opts) : o(opts) {
  Stage& st = stage;
  const double R = o.r;

  // ---- the asphalt: one outline round the four arms and the corners between them
  std::vector<double> ring;
  for (int i = 0; i < 4; i++) {
    const Dir d = AROUND[static_cast<size_t>(i)];
    const Dir next = AROUND[static_cast<size_t>((i + 1) % 4)];
    const auto [ox, oz] = OUT(d);
    const double hw = half(d);
    const double nhw = half(next);
    const double len = o.arms[d].len;
    // across the end of this arm: from the left of the traffic coming in to its right …
    const auto [lx, lz] = at(d, len, -hw);
    const auto [rx, rz] = at(d, len, hw);
    append(ring, {lx, lz, rx, rz});
    // … back along that side to the corner, round the kerb's arc, out along the next arm
    const std::array<double, 2> cornerA = at(d, edge(d) + R, hw);
    const std::array<double, 2> cornerB = at(next, edge(next) + R, -nhw);
    // the corner where the two kerbs would meet, and the middle of the arc round it
    const auto [cx, cz] = at(d, edge(d), hw);
    const auto [nx, nz] = OUT(next);
    const double mx = cx + ox * R + nx * R;
    const double mz = cz + oz * R + nz * R;
    const double a0 = std::atan2(cornerA[1] - mz, cornerA[0] - mx);
    double a1 = std::atan2(cornerB[1] - mz, cornerB[0] - mx);
    // (the short way round)
    while (a1 - a0 > PI_) a1 -= 2 * PI_;
    while (a0 - a1 > PI_) a1 += 2 * PI_;
    const auto arcOf = [&](double r) {
      std::vector<double> out;
      for (int k = 1; k < 8; k++) {
        const double a = a0 + ((a1 - a0) * k) / 8;
        out.push_back(mx + std::cos(a) * r);
        out.push_back(mz + std::sin(a) * r);
      }
      return out;
    };
    const std::vector<double> arc = arcOf(R);
    append(ring, cornerA);
    append(ring, arc);
    append(ring, cornerB);
    // the pavement of this corner: from the end of this arm round to the end of the next, as wide as `walk`
    const double w = std::min(road(d).walk, road(next).walk);
    if (w <= 0) continue;
    const std::array<double, 2> nEnd = at(next, o.arms[next].len, -nhw);
    std::vector<double> kerb = {rx, rz, cornerA[0], cornerA[1]};
    append(kerb, arc);
    append(kerb, {cornerB[0], cornerB[1], nEnd[0], nEnd[1]});
    st.kerbs.push_back(Kerb{.line = kerb, .lo = "asphalt", .hi = "walk"});
    std::vector<double> out;
    append(out, at(d, len, hw + w));
    if (R - w > 0.3) {
      append(out, at(d, edge(d) + R, hw + w));
      append(out, arcOf(R - w));
      append(out, at(next, edge(next) + R, -nhw - w));
    }
    // (a pavement wider than the kerb's radius: a sharp corner behind it)
    else append(out, at(d, edge(d) + w, hw + w));
    append(out, at(next, o.arms[next].len, -nhw - w));
    std::vector<double> walkRing = kerb;
    for (int k = static_cast<int>(out.size()) - 2; k >= 0; k -= 2) append(walkRing, {out[static_cast<size_t>(k)], out[static_cast<size_t>(k + 1)]});
    st.surfaces.push_back(Surface{.kind = "walk", .ring = walkRing});
  }
  st.surfaces.insert(st.surfaces.begin(), Surface{.kind = "asphalt", .ring = ring});

  // ---- the markings of each arm
  for (const Dir d : DIRS) {
    const ArmOpts& a = o.arms[d];
    const Road& r = road(d);
    const double hw = half(d);
    const double e = edge(d);
    const double line = lineAt(d);
    const double med = r.median / 2;
    const double end = a.len;
    // the middle line: up to the line where traffic waits (or to the junction)
    const double stopAt = !a.line.empty() ? line : e;
    if (med == 0 && !r.centre.empty() && r.centre != "none") {
      const auto [x0, z0] = at(d, end, 0);
      const auto [x1, z1] = at(d, stopAt + (a.line == "stop" ? 0.25 : 0.4), 0);
      if (r.centre == "solid") st.surfaces.push_back(bar(x0, z0, x1, z1, 0.12));
      else pushAll(st.surfaces, dashes(x1, z1, x0, z0, 0.12, 3, 6));
      // (a solid stretch just before the junction)
      if (r.centre == "dash" && a.line == "stop") {
        const auto [x2, z2] = at(d, stopAt + 12, 0);
        st.surfaces.push_back(bar(x1, z1, x2, z2, 0.12));
      }
    }
    // lines between the lanes of one direction, and of the cycle lane
    for (const double side : {1.0, -1.0}) {
      for (int j = 1; j < r.lanes; j++) {
        const auto [x0, z0] = at(d, end, side * (med + j * r.laneW));
        const auto [x1, z1] = at(d, (side > 0 ? stopAt : e) + 0.3, side * (med + j * r.laneW));
        pushAll(st.surfaces, dashes(x1, z1, x0, z0, 0.12, 3, 6));
      }
      if (r.bike != 0) {
        const double off = side * (med + r.lanes * r.laneW);
        const auto [x0, z0] = at(d, end, off);
        const auto [x1, z1] = at(d, e + (side > 0 && !a.line.empty() ? line - e : 0) + 0.3, off);
        st.surfaces.push_back(bar(x0, z0, x1, z1, 0.25));
      }
    }
    // where the traffic waits: a stop line (Zeichen 294) or a give-way line (Zeichen 341), across the lanes coming in
    if (!a.line.empty()) {
      const auto [x0, z0] = at(d, line, med);
      const auto [x1, z1] = at(d, line, hw);
      if (a.line == "stop") st.surfaces.push_back(bar(x0, z0, x1, z1, 0.5));
      else pushAll(st.surfaces, dashes(x0, z0, x1, z1, 0.5, 0.5, 0.25));
    }
    // a crossing for people: a zebra (Zeichen 293) or the dashed edges of a crossing with lights
    if (a.xing) {
      const double mid = e + a.xing->at;
      const double w = a.xing->w;
      if (a.xing->kind == "zebra") {
        for (double s = -hw + 0.25; s < hw - 0.2; s += 1.0) {
          const auto [x0, z0] = at(d, mid - w / 2, s + 0.25);
          const auto [x1, z1] = at(d, mid + w / 2, s + 0.25);
          st.surfaces.push_back(bar(x0, z0, x1, z1, 0.5));
        }
      } else {
        for (const double k : {-1.0, 1.0}) {
          const auto [x0, z0] = at(d, mid + (k * w) / 2, -hw);
          const auto [x1, z1] = at(d, mid + (k * w) / 2, hw);
          pushAll(st.surfaces, dashes(x0, z0, x1, z1, 0.4, 0.5, 0.5));
        }
      }
      if (!a.pedLight.empty()) {
        for (const double k : {-1.0, 1.0}) {
          const auto [x, z] = at(d, mid + 0.4 + w / 2, k * (hw + 0.6));
          // (facing across the road: the people waiting on the other side see it)
          const auto [ox, oz] = OUT(d);
          const auto [rx, rz] = rightOf(-ox, -oz);
          st.lights.push_back(LightPlace{.id = a.pedLight, .x = x, .z = z, .yaw = yawOf(-rx * k, -rz * k), .ped = true});
        }
      }
    }
    // tram tracks in the middle: a pair of rails each way (1.435 m apart), on to the far side of the junction
    // (drawn once for the whole road, from the end of the south or east arm to the end of the opposite one)
    if (r.tracks && (d == 'S' || d == 'E')) {
      const Dir opp = d == 'S' ? 'N' : 'W';
      for (const double side : {-1.0, 1.0}) {
        const double c = side * (med / 2);
        for (const double g : {-0.7175, 0.7175}) {
          const auto [x0, z0] = at(d, end, c + g);
          const auto [x1, z1] = at(opp, o.arms[opp].len, -(c + g));
          st.surfaces.push_back(bar(x0, z0, x1, z1, 0.09, "steel"));
        }
      }
    }
    // signs on the right of the traffic coming in, on the pavement
    for (const ArmSign& sg : a.signs) {
      const auto [x, z] = at(d, sg.at, hw + 0.7);
      const auto [ox, oz] = OUT(d);
      st.signs.push_back(SignPlace{.x = x, .z = z, .yaw = yawOf(ox, oz), .codes = sg.codes});
    }
    if (a.light) {
      const auto [x, z] = at(d, std::max(e + 0.6, line - 2.4), hw + 0.6);
      const auto [ox, oz] = OUT(d);
      st.lights.push_back(LightPlace{.id = a.light->id, .x = x, .z = z, .yaw = yawOf(ox, oz), .ped = false, .arrow = a.light->arrow});
    }
  }
  // the cycle lanes across the mouths of the side roads: red, with dashed edges (a cyclist's crossing)
  for (const Dir d : DIRS) {
    const Road& r = road(d);
    if (r.bike == 0 || (d != 'N' && d != 'S')) continue;
    const double med = r.median / 2;
    const double inner = med + r.lanes * r.laneW;
    for (const double side : {1.0, -1.0}) {
      const auto [x0, z0] = at(d, edge(d), side * inner);
      const auto [x1, z1] = at(d, edge(d), side * (inner + r.bike));
      const Dir opp = d == 'S' ? 'N' : 'S';
      const auto [x2, z2] = at(opp, edge(opp), -side * (inner + r.bike));
      const auto [x3, z3] = at(opp, edge(opp), -side * inner);
      if (d == 'S') {
        st.surfaces.push_back(Surface{.kind = "red", .ring = {x0, z0, x1, z1, x2, z2, x3, z3}});
        pushAll(st.surfaces, dashes(x0, z0, x3, z3, 0.25, 0.5, 0.5));
        pushAll(st.surfaces, dashes(x1, z1, x2, z2, 0.25, 0.5, 0.5));
      }
    }
  }
  finish(st);
}

Path Cross::move(Dir from, Dir to, const std::string& lane) const {
  const Road& rin = road(from);
  const Road& rout = road(to);
  const auto [fx, fz] = OUT(from);
  const auto [tx, tz] = OUT(to);
  // the lane: the right-most lane of each direction, the cycle lane, the track in the middle
  const auto offOf = [&](const Road& r) {
    return lane == "bike" ? r.median / 2 + r.lanes * r.laneW + r.bike / 2 : lane == "tram" ? r.median / 4 : r.median / 2 + (r.lanes - 0.5) * r.laneW;
  };
  const double offIn = offOf(rin);
  const double offOut = offOf(rout);
  // turning: right (the next arm clockwise), straight on, left
  const int turn = (dirIndex(to) - dirIndex(from) + 4) % 4;
  Trace t;
  const auto [sx, sz] = at(from, o.arms[from].len, offIn);
  const auto [ex, ez] = at(to, o.arms[to].len, -offOut);
  std::map<std::string, double> marks;
  t.to(sx, sz);
  if (turn == 2) {
    // straight on: through the box of the junction
    const auto [ix, iz] = at(from, edge(from), offIn);
    t.to(ix, iz);
    marks["in"] = t.len;
    const auto [ox, oz] = at(to, edge(to), -offOut);
    t.to(ox, oz);
    marks["out"] = t.len;
  } else {
    // a turn: an arc of a circle that touches both lanes – where the lane in and the lane out would meet,
    // `r` back along the one and `r` on along the other (6.5 m turning left, 5.5 m turning right)
    const double r = turn == 3 ? 5.5 : 6.5;
    const auto [ax, az] = at(from, 0, offIn);
    const auto [bx, bz] = at(to, 0, -offOut);
    // the meeting point: on the lane in (a + λ·(−f)) and on the lane out (b + μ·t), the two at right angles
    const double lambda = (bx - ax) * -fx + (bz - az) * -fz;
    const double mx = ax - fx * lambda;
    const double mz = az - fz * lambda;
    const double ix = mx + fx * r;
    const double iz = mz + fz * r;
    const double ox = mx + tx * r;
    const double oz = mz + tz * r;
    t.to(ix, iz);
    marks["in"] = t.len;
    t.curve(ix - fx * r * 0.5523, iz - fz * r * 0.5523, ox - tx * r * 0.5523, oz - tz * r * 0.5523, ox, oz);
    marks["out"] = t.len;
  }
  t.to(ex, ez);
  Path path = t.path(marks);
  // where the nose waits: before the line – or, where there is none, at the edge of the junction
  const ArmOpts& a = o.arms[from];
  const double waitAt = !a.line.empty() ? lineAt(from) + 0.25 + 0.3 : edge(from) + 0.6;
  // … and before the crossing of the arm it goes into, for the people on it
  const std::optional<ArmXing>& x = o.arms[to].xing;
  const double xingNear = x ? edge(to) + x->at - x->w / 2 - 0.6 : 0;
  // (found along the way itself: the first of its points that far on – it may lie on the bend)
  const std::vector<double>& pts = path.pts;
  for (size_t i = 0; i < path.acc.size(); i++) {
    const double px = pts[i * 2];
    const double pz = pts[i * 2 + 1];
    if (!marks.count("line") && px * fx + pz * fz <= waitAt) marks["line"] = path.acc[i];
    const double in = marks.count("in") ? marks["in"] : 0;
    if (x && !marks.count("xing") && path.acc[i] > in && px * tx + pz * tz >= xingNear) marks["xing"] = path.acc[i];
  }
  if (!marks.count("line")) marks["line"] = 0;
  for (const auto& [k, v] : marks) path.marks[k] = v;
  return path;
}

// -------------------------------------------------------------- a roundabout

namespace {
/** the angle (atan2(z, x)) of each arm */
double ANGLE(Dir d) {
  switch (d) {
    case 'E': return 0;
    case 'S': return PI_ / 2;
    case 'W': return PI_;
    default: return -PI_ / 2;
  }
}

struct Fillet {
  std::array<double, 2> centre;
  std::array<double, 2> onArm;
  std::array<double, 2> onRing;
};
}  // namespace

Round roundabout(const RoundOpts& o) { return Round(o); }

Round::Round(const RoundOpts& opts) : o(opts) {
  Stage& st = stage;
  const double hw = o.laneW;
  const double R = o.r;
  std::vector<Dir> arms;
  for (const Dir d : DIRS)
    if (o.len[d] > 0) arms.push_back(d);
  // ---- the outline: round the ring clockwise as seen on the map (decreasing angle: E, N, W, S …), out and back along each arm
  // where an arm's kerb meets the ring: the middle of the kerb's arc lies hw + R beside the arm's line and outer + R from the middle
  std::vector<double> ring;
  t = std::sqrt(std::pow(o.outer + R, 2) - std::pow(hw + R, 2));
  const double tt = t;
  // the arms in the order of decreasing angle, starting with the one of the largest angle
  std::vector<Dir> order = arms;
  std::stable_sort(order.begin(), order.end(), [](Dir a, Dir b) { return ANGLE(b) - ANGLE(a) < 0; });
  const auto fillet = [&](Dir d, int side) -> Fillet {
    const double a = ANGLE(d);
    const double ux = std::cos(a);
    const double uz = std::sin(a);
    // `side` +1: the side of larger angle
    const double nx = -std::sin(a) * side;
    const double nz = std::cos(a) * side;
    const double cx = ux * tt + nx * (hw + R);
    const double cz = uz * tt + nz * (hw + R);
    const double k = o.outer / (o.outer + R);
    return Fillet{.centre = {cx, cz}, .onArm = {ux * tt + nx * hw, uz * tt + nz * hw}, .onRing = {cx * k, cz * k}};
  };
  const auto arcPts = [](double cx, double cz, double r, const std::array<double, 2>& from, const std::array<double, 2>& to, int n, bool shortWay = true) {
    const double a0 = std::atan2(from[1] - cz, from[0] - cx);
    double a1 = std::atan2(to[1] - cz, to[0] - cx);
    if (shortWay) {
      while (a1 - a0 > PI_) a1 -= 2 * PI_;
      while (a0 - a1 > PI_) a1 += 2 * PI_;
    }
    std::vector<double> out;
    for (int k = 0; k <= n; k++) {
      const double a = a0 + ((a1 - a0) * k) / n;
      out.push_back(cx + std::cos(a) * r);
      out.push_back(cz + std::sin(a) * r);
    }
    return out;
  };
  for (size_t i = 0; i < order.size(); i++) {
    const Dir d = order[i];
    const Dir next = order[(i + 1) % order.size()];
    const double a = ANGLE(d);
    const double ux = std::cos(a);
    const double uz = std::sin(a);
    const double nx = -std::sin(a);
    const double nz = std::cos(a);
    const double len = o.len[d];
    // out along the side of larger angle, across the end, back along the side of smaller angle
    const Fillet f1 = fillet(d, 1);
    const Fillet f2 = fillet(d, -1);
    append(ring, arcPts(f1.centre[0], f1.centre[1], R, f1.onRing, f1.onArm, 6));
    append(ring, {ux * len + nx * hw, uz * len + nz * hw, ux * len - nx * hw, uz * len - nz * hw});
    append(ring, arcPts(f2.centre[0], f2.centre[1], R, f2.onArm, f2.onRing, 6));
    // along the ring's outer edge to the next arm (decreasing angle)
    const Fillet g = fillet(next, 1);
    const double b0 = std::atan2(f2.onRing[1], f2.onRing[0]);
    double b1 = std::atan2(g.onRing[1], g.onRing[0]);
    while (b1 >= b0) b1 -= 2 * PI_;
    const int n = std::max(2, static_cast<int>(std::ceil(((b0 - b1) * o.outer) / 1.5)));
    for (int k = 1; k < n; k++) {
      const double b = b0 + ((b1 - b0) * k) / n;
      append(ring, {std::cos(b) * o.outer, std::sin(b) * o.outer});
    }
  }
  const auto circle = [](double r, int n = 48) {
    std::vector<double> out;
    for (int k = 0; k < n; k++) {
      out.push_back(std::cos((static_cast<double>(k) / n) * PI_ * 2) * r);
      out.push_back(std::sin((static_cast<double>(k) / n) * PI_ * 2) * r);
    }
    return out;
  };
  st.surfaces.push_back(Surface{.kind = "asphalt", .ring = ring, .holes = {circle(o.apron)}});
  st.surfaces.push_back(Surface{.kind = "apron", .ring = circle(o.apron), .holes = {circle(o.island)}});
  st.surfaces.push_back(Surface{.kind = "island", .ring = circle(o.island)});
  {
    std::vector<double> islandLine = circle(o.island);
    append(islandLine, {o.island, 0});
    std::vector<double> apronLine = circle(o.apron);
    append(apronLine, {o.apron, 0});
    st.kerbs.push_back(Kerb{.line = islandLine, .lo = "apron", .hi = "island"});
    st.kerbs.push_back(Kerb{.line = apronLine, .lo = "asphalt", .hi = "apron"});
  }
  // ---- markings of each arm: the middle line, the give-way line, the signs 215 over 205
  // (the give-way line just outside the ring, across the lane coming in)
  const double line = o.outer + 0.9;
  for (const Dir d : arms) {
    const double a = ANGLE(d);
    const double ux = std::cos(a);
    const double uz = std::sin(a);
    // the right of the traffic coming in (heading −u): (−(−uz), −ux) = (uz, −ux)
    const double rx = uz;
    const double rz = -ux;
    const double len = o.len[d];
    pushAll(st.surfaces, dashes(ux * (tt + 1), uz * (tt + 1), ux * len, uz * len, 0.12, 3, 6));
    pushAll(st.surfaces, dashes(ux * line + rx * 0.15, uz * line + rz * 0.15, ux * line + rx * hw, uz * line + rz * hw, 0.5, 0.5, 0.25));
    st.signs.push_back(SignPlace{.x = ux * (tt + 2) + rx * (hw + 1.1), .z = uz * (tt + 2) + rz * (hw + 1.1), .yaw = yawOf(ux, uz), .codes = {"215", "205"}});
  }
  finish(st);
}

Path Round::move(Dir from, Dir to) const {
  const double hw = o.laneW;
  const double rc = (o.apron + o.outer) / 2;
  /** where the curves onto the ring and off it begin and end on an arm (from the middle), and how far round the ring they reach */
  const double bendAt = o.outer + 6.5;
  const double alongRing = 0.6;
  const double line = o.outer + 0.9;
  const double a = ANGLE(from);
  const double ux = std::cos(a);
  const double uz = std::sin(a);
  const double rx = uz;
  const double rz = -ux;
  const double off = hw / 2;
  Trace tr;
  tr.to(ux * o.len[from] + rx * off, uz * o.len[from] + rz * off);
  tr.to(ux * bendAt + rx * off, uz * bendAt + rz * off);
  // onto the ring a little past the arm (the ring is driven with decreasing angle), in one wide curve to the right
  const double aIn = a - alongRing;
  tr.join(-ux, -uz, std::cos(aIn) * rc, std::sin(aIn) * rc, std::sin(aIn), -std::cos(aIn));
  std::map<std::string, double> marks = {{"ring", tr.len}};
  // round to a little before the arm to leave by
  const double b = ANGLE(to);
  double aOut = b + alongRing;
  while (aOut >= aIn) aOut -= 2 * PI_;
  // (each arm passed on the way: the place on the ring beside it)
  std::vector<std::pair<Dir, double>> passed;
  for (const Dir d : DIRS) {
    if (d == from || !(o.len[d] > 0)) continue;
    double x = ANGLE(d);
    while (x >= aIn) x -= 2 * PI_;
    passed.emplace_back(d, x);
  }
  const double startLen = tr.len;
  tr.arc(0, 0, rc, aIn, aOut);
  for (const auto& [d, x] : passed)
    if (x > aOut - 0.7) marks[std::string("pass:") + d] = startLen + (aIn - x) * rc;
  marks["exit"] = tr.len;
  const double bx = std::cos(b);
  const double bz = std::sin(b);
  // out on the right of the traffic leaving (heading +u): (−bz, bx)
  const double ox = -bz * off;
  const double oz = bx * off;
  tr.join(std::sin(aOut), -std::cos(aOut), bx * bendAt + ox, bz * bendAt + oz, bx, bz);
  marks["out"] = tr.len;
  tr.to(bx * o.len[to] + ox, bz * o.len[to] + oz);
  Path path = tr.path(marks);
  // where the nose waits: 0.3 m before the give-way line (the first point of the way that close to the middle)
  const double wait = line + 0.25 + 0.3;
  const std::vector<double>& pts = path.pts;
  double s = 0;
  for (size_t i = 0; i < path.acc.size(); i++) {
    if (pts[i * 2] * ux + pts[i * 2 + 1] * uz <= wait) {
      s = path.acc[i];
      break;
    }
  }
  path.marks["line"] = s;
  return path;
}

// ----------------------------------------------------------- a straight road

double boundAt(const Bound& b, double z) {
  if (!b.taper) return b.x;
  const Taper& t = *b.taper;
  if (z >= t.z0) return b.x;
  if (z <= t.z1) return t.to;
  return b.x + ((t.to - b.x) * (t.z0 - z)) / (t.z0 - t.z1);
}

Stage straight(const StraightOpts& o) {
  Stage st = emptyStage();
  // the z where something changes along the road: its ends and the ends of the tapers
  std::vector<double> zs = {o.z0};
  const auto add = [&zs](double z) {
    if (std::find(zs.begin(), zs.end(), z) == zs.end()) zs.push_back(z);
  };
  add(o.z1);
  for (const Bound& b : o.bounds)
    if (b.taper) {
      add(b.taper->z0);
      add(b.taper->z1);
    }
  std::vector<double> cuts;
  for (const double z : zs)
    if (z <= o.z0 && z >= o.z1) cuts.push_back(z);
  std::stable_sort(cuts.begin(), cuts.end(), [](double a, double b) { return b - a < 0; });
  for (size_t i = 0; i < o.strips.size(); i++) {
    const std::string& kind = o.strips[i];
    if (kind == "grass") continue;
    const Bound& a = o.bounds[i];
    const Bound& b = o.bounds[i + 1];
    std::vector<double> left;
    std::vector<double> right;
    for (const double z : cuts) {
      append(left, {boundAt(a, z), z});
      append(right, {boundAt(b, z), z});
    }
    std::vector<double> ring = left;
    for (int k = static_cast<int>(right.size()) - 2; k >= 0; k -= 2) append(ring, {right[static_cast<size_t>(k)], right[static_cast<size_t>(k + 1)]});
    // (a strip that narrows to nothing still makes a simple outline)
    st.surfaces.push_back(Surface{.kind = kind, .ring = ring});
  }
  for (const int k : o.kerbs) {
    std::vector<double> line;
    for (const double z : cuts) append(line, {boundAt(o.bounds[static_cast<size_t>(k)], z), z});
    st.kerbs.push_back(Kerb{.line = line, .lo = "asphalt", .hi = "walk"});
  }
  for (const double x : o.guards) st.guards.push_back({x, o.z0, x, o.z1});
  return st;
}

std::vector<Surface> along(const Bound& b, double z0, double z1, double w, std::optional<std::array<double, 2>> dash) {
  // (piece by piece: straight, then along the taper, then straight again)
  const double lo = std::min(z0, z1);
  const double hi = std::max(z0, z1);
  std::vector<double> cuts = {z0};
  if (b.taper) {
    for (const double z : {b.taper->z0, b.taper->z1})
      if (z > lo && z < hi) cuts.push_back(z);
  }
  cuts.push_back(z1);
  std::stable_sort(cuts.begin(), cuts.end(), [z0, z1](double a, double c) { return (z0 > z1 ? c - a : a - c) < 0; });
  std::vector<Surface> out;
  for (size_t i = 0; i + 1 < cuts.size(); i++) {
    const double za = cuts[i];
    const double zb = cuts[i + 1];
    if (std::abs(zb - za) < 0.01) continue;
    const double xa = boundAt(b, za);
    const double xb = boundAt(b, zb);
    if (dash) pushAll(out, dashes(xa, za, xb, zb, w, (*dash)[0], (*dash)[1]));
    else out.push_back(bar(xa, za, xb, zb, w));
  }
  return out;
}

Path lanePath(double x, double z0, double z1, const std::map<std::string, double>& marks) { return Trace().to(x, z0).to(x, z1).path(marks); }

}  // namespace fs::theory

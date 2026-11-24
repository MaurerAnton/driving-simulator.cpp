// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The cars you see: the exterior (body, wheels, lamps, plates, lettering,
 * roof sign) and the player's cockpit (dashboard, instrument hood, console,
 * doors, seats, steering wheel, gear lever, handbrake, pedals, stalks) plus
 * the three mirrors.
 *
 * Origin: src/render/car.ts (MaurerAnton/driving-simulator @ ff37c5d, v0.7.0).
 * The proportions, the lamp positions, every cockpit builder and the see-
 * through cabin are carried over.  Material colours become vertex colours (the
 * engine's way); the plate / roof-sign / lettering / screen pictures are the
 * platform's to draw (the texture path lands when the renderer uploads panel
 * images, as the sign atlas does), so the quads are placed exactly and stand
 * ready.
 */
#include "render/car.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/geometry.h"

namespace fs {

namespace {

constexpr double DEG = PI / 180;

/** A box; rotation as yaw (ry), then pitch (rx), then roll (rz) — the original's box(). */
GeometryPtr boxG(double w, double h, double d, double x, double y, double z, double rx = 0,
                 double ry = 0, double rz = 0) {
  GeometryPtr g = makeBoxGeometry(w, h, d);
  if (rx || ry || rz) {
    Euler e(rx, ry, rz, Euler::YXZ);
    Matrix4 rot;
    rot.makeRotationFromEuler(e);
    for (size_t i = 0; i < g->position->count(); i++) {
      Vector3 p(g->position->getX(i), g->position->getY(i), g->position->getZ(i));
      p.applyMatrix4(rot);
      g->position->setXYZ(i, p.x, p.y, p.z);
    }
  }
  g->translate(x, y, z);
  g->deleteAttribute("uv");
  return g;
}

/** A box mesh (the original's box() made a Mesh directly). */
std::unique_ptr<Mesh> box(double w, double h, double d, double x, double y, double z,
                          double rx = 0, double ry = 0, double rz = 0) {
  return std::make_unique<Mesh>(boxG(w, h, d, x, y, z, rx, ry, rz), MaterialKind::Struct);
}

/** Vertex colours on a plain geometry (the shell parts are unlit colours). */
void tint(GeometryPtr g, uint32_t color) {
  if (!g || !g->position) return;
  Color c;
  c.setHex(color);
  std::vector<double> cols(g->position->count() * 3);
  for (size_t i = 0; i < g->position->count(); i++) {
    cols[i * 3] = c.r;
    cols[i * 3 + 1] = c.g;
    cols[i * 3 + 2] = c.b;
  }
  g->color = std::make_shared<BufferAttribute>(cols, 3);
}

/** The geometry of a box as used by the cabin builders (no uv, positioned). */
void tint(GeometryPtr g, const Color& c) {
  if (!g || !g->position) return;
  std::vector<double> cols(g->position->count() * 3);
  for (size_t i = 0; i < g->position->count(); i++) {
    cols[i * 3] = c.r;
    cols[i * 3 + 1] = c.g;
    cols[i * 3 + 2] = c.b;
  }
  g->color = std::make_shared<BufferAttribute>(cols, 3);
}

/** Outline of a rectangle with rounded corners, counter-clockwise: [u, v] pairs. */
std::vector<std::array<double, 2>> roundRect(double w, double h, double r, int seg = 2) {
  std::vector<std::array<double, 2>> pts;
  const double corners[4][3] = {{w / 2 - r, -h / 2 + r, -90},
                                {w / 2 - r, h / 2 - r, 0},
                                {-w / 2 + r, h / 2 - r, 90},
                                {-w / 2 + r, -h / 2 + r, 180}};
  for (auto& c : corners) {
    for (int i = 0; i <= seg; i++) {
      double a = (c[2] + (90.0 * i) / seg) * DEG;
      pts.push_back({c[0] + std::cos(a) * r, c[1] + std::sin(a) * r});
    }
  }
  return pts;
}

/** Outline with soft corners that may be flatter below than above (a superellipse). */
std::vector<std::array<double, 2>> softRing(double halfWidth, double top, double bottom,
                                            int n = 24) {
  std::vector<std::array<double, 2>> pts;
  for (int i = 0; i < n; i++) {
    double phi = (static_cast<double>(i) / n) * PI;
    double c = std::cos(phi * 2);
    double s = std::sin(phi * 2);
    double sgnc = c >= 0 ? 1.0 : -1.0;
    double sgns = s >= 0 ? 1.0 : -1.0;
    pts.push_back({halfWidth * sgnc * std::sqrt(std::fabs(c)),
                   (s >= 0 ? top : bottom) * sgns * std::sqrt(std::fabs(s))});
  }
  return pts;
}

using Ring2 = std::vector<std::array<double, 2>>;
using Ring3 = std::vector<std::array<double, 3>>;

/** Put a flat outline into space: origin + u·right + v·up. */
Ring3 place(const Ring2& outline, const std::array<double, 3>& o,
            const std::array<double, 3>& right, const std::array<double, 3>& up) {
  Ring3 out;
  out.reserve(outline.size());
  for (auto& p : outline)
    out.push_back({o[0] + right[0] * p[0] + up[0] * p[1], o[1] + right[1] * p[0] + up[1] * p[1],
                   o[2] + right[2] * p[0] + up[2] * p[1]});
  return out;
}

std::array<double, 3> along(const std::array<double, 3>& o, const std::array<double, 3>& dir,
                            double s) {
  return {o[0] + dir[0] * s, o[1] + dir[1] * s, o[2] + dir[2] * s};
}

const std::array<double, 3> AX = {1, 0, 0};
const std::array<double, 3> AY = {0, 1, 0};
const std::array<double, 3> AZ = {0, 0, 1};

// ---------------------------------------------------------------------------
// Geometry of the fixed cabin parts, collected per colour (the original's Kit,
// one list per material).
// ---------------------------------------------------------------------------

struct Kit {
  /** padded upper surfaces: dashboard top, door tops, instrument hood */
  std::vector<GeometryPtr> soft;
  /** hard plastic: lower dashboard, console, door panels */
  std::vector<GeometryPtr> hard;
  /** pale lining: sun visors, roof lamp */
  std::vector<GeometryPtr> pale;
  std::vector<GeometryPtr> fabric;
  std::vector<GeometryPtr> accent;
  std::vector<GeometryPtr> carpet;
  std::vector<GeometryPtr> silver;
  std::vector<GeometryPtr> black;
};

/** Where the instrument cluster sits, shared by the hood built around it and the picture itself. */
struct ClusterPlace {
  std::array<double, 3> centre{0, 0, 0};
  /** backward tilt of the face in rad */
  double tilt = 0;
  double width = 0, height = 0;
};

}  // namespace

// ---------------------------------------------------------------------------
// CarExterior
// ---------------------------------------------------------------------------

CarExterior::CarExterior(uint32_t color, const CarDims& dims_, const std::string& plate)
    : CarExterior(color, [&] {
        CarExteriorOptions o;
        o.dims = &dims_;
        o.plate = plate;
        return o;
      }()) {}

CarExterior::CarExterior(uint32_t color, const CarExteriorOptions& opts) {
  dims = opts.dims ? *opts.dims : COMPACT_DIMS;
  shell = buildCarShell(dims);

  // body, glass, trim, pillars — each with its own material kind.  Shadows: the shell is not a
  // closed solid (the windows are openings, the floor is a part of its own), so both of its
  // sides throw shadow – otherwise the roof, which faces the sun, would throw none.
  tint(shell.paint, color);
  auto body = std::make_unique<Mesh>(shell.paint, MaterialKind::Struct);
  body->castShadow = true;
  solids.push_back(body.get());
  group.add(std::unique_ptr<Object3D>(body.release()));

  // the glass colour follows the material (three.js MAT.glassDark, or the
  // pale see-through one with its low opacity: vertex colour + alpha)
  tint(shell.glass, opts.seeThroughGlass ? Color(0.62, 0.75, 0.82) : Color(0.047, 0.078, 0.094));
  auto glassMesh = std::make_unique<Mesh>(shell.glass, MaterialKind::Struct);
  glassMesh->renderOrder = 5;
  if (opts.seeThroughGlass) {
    // three.js: transparent: true, opacity: 0.16, depthWrite: false
    glassMesh->opacity = 0.16;
    glassMesh->depthWrite = false;
  }
  group.add(std::unique_ptr<Object3D>(glassMesh.release()));

  tint(shell.trim, 0x2a2d31);
  auto trimMesh = new Mesh(shell.trim, MaterialKind::Struct);
  trimMesh->castShadow = true;
  solids.push_back(trimMesh);
  group.add(std::unique_ptr<Object3D>(trimMesh));

  tint(shell.pillars, color);
  auto pillarMesh = new Mesh(shell.pillars, MaterialKind::Struct);
  pillarMesh->castShadow = true;
  solids.push_back(pillarMesh);
  group.add(std::unique_ptr<Object3D>(pillarMesh));

  // wheels: a pivot that steers, holding a spinner that rolls
  GeometryPtr tyre, rim;
  {
    std::pair<GeometryPtr, GeometryPtr> parts = buildWheelParts(dims);
    tint(parts.first, 0x14151a);   // tyre
    tint(parts.second, 0xb9bcc2);  // rim
    tyre = parts.first;
    rim = parts.second;
  }
  const double xs[4] = {-dims.track / 2, dims.track / 2, -dims.track / 2, dims.track / 2};
  const double zs[4] = {shell.zAxleF, shell.zAxleF, shell.zAxleR, shell.zAxleR};
  for (int i = 0; i < 4; i++) {
    auto steer = std::make_unique<Group>();
    auto spin = std::make_unique<Group>();
    spin->add(std::unique_ptr<Object3D>(new Mesh(tyre, MaterialKind::Struct)));
    spin->add(std::unique_ptr<Object3D>(new Mesh(rim, MaterialKind::Struct)));
    steer->add(std::unique_ptr<Object3D>(spin.release()));
    steer->position.set(xs[i], dims.wheelRadius, zs[i]);
    wheels.push_back(static_cast<Group*>(group.add(std::unique_ptr<Object3D>(steer.release()))));
    wheelSpinners.push_back(static_cast<Group*>(wheels.back()->children.front().get()));
  }

  // lamps
  double hw = shell.halfWidth;
  std::vector<std::unique_ptr<Mesh>> built;
  std::vector<int> which;
  auto keep = [&](std::unique_ptr<Mesh> m, int w) {
    built.push_back(std::move(m));
    which.push_back(w);
  };
  for (int side : {-1, 1}) {
    keep(box(0.34, 0.1, 0.05, side * hw * 0.62, 0.635, shell.zFront + 0.115, 0, side * -0.35), 0);
    keep(box(0.1, 0.07, 0.05, side * hw * 0.86, 0.6, shell.zFront + 0.2, 0, side * -0.8),
         side < 0 ? 3 : 4);
    keep(box(0.3, 0.13, 0.05, side * hw * 0.7, 0.9, shell.zRear - 0.115, 0, side * 0.3), 1);
    keep(box(0.1, 0.09, 0.05, side * hw * 0.88, 0.9, shell.zRear - 0.19, 0, side * 0.7), 4);
    keep(box(0.12, 0.05, 0.04, side * hw * 0.45, 0.86, shell.zRear - 0.06), 2);
    // mirror housings (the player's car builds its own, aligned with the working glass)
    if (!opts.noMirrorHousings) {
      auto housing = box(0.2, 0.13, 0.09, side * (hw + 0.1), shell.beltY + 0.06,
                         shell.zCowl + 0.4);
      tint(housing->geometry, color);
      group.add(std::unique_ptr<Object3D>(housing.release()));
    }
  }
  for (size_t i = 0; i < built.size(); i++) {
    Mesh* raw = built[i].get();
    solids.push_back(raw);
    group.add(std::unique_ptr<Object3D>(built[i].release()));
    lamps.push_back({raw, which[i]});
  }
  setLamps(false, false, false, false, false, false);

  // grille and number plates
  auto grille = box(0.86, 0.14, 0.04, 0, 0.5, shell.zFront + 0.05);
  tint(grille->geometry, 0x2a2d31);
  solids.push_back(grille.get());
  group.add(std::unique_ptr<Object3D>(grille.release()));
  if (!opts.plate.empty()) {
    // the plate texture is drawn by the platform; here the quads are placed
    // exactly as the original puts them
    auto front = std::make_unique<Mesh>(makePlaneGeometry(0.52, 0.11), MaterialKind::Struct);
    front->position.set(0, 0.36, shell.zFront - 0.012);
    front->quaternion.setFromAxisAngle(Vector3(0, 1, 0), PI);
    front->updateMatrix();
    group.add(std::unique_ptr<Object3D>(front.release()));
    auto rear = std::make_unique<Mesh>(makePlaneGeometry(0.52, 0.11), MaterialKind::Struct);
    rear->position.set(0, 0.66, shell.zRear + 0.012);
    rear->quaternion.setFromAxisAngle(Vector3(0, 1, 0), PI);
    rear->updateMatrix();
    group.add(std::unique_ptr<Object3D>(rear.release()));
  }
  if (!opts.lettering.empty()) addLettering(opts.lettering, color);
  if (opts.roofSign) {
    // the sign picture is drawn by the platform; the box and the two quads are placed
    auto sign = box(0.86, 0.16, 0.2, 0, shell.roofY + 0.085, 0.25);
    tint(sign->geometry, 0xf2f2ee);
    solids.push_back(sign.get());
    group.add(std::unique_ptr<Object3D>(sign.release()));
    auto f = std::make_unique<Mesh>(makePlaneGeometry(0.84, 0.15), MaterialKind::Struct);
    f->position.set(0, shell.roofY + 0.085, 0.25 - 0.101);
    f->quaternion.setFromAxisAngle(Vector3(0, 1, 0), PI);
    f->updateMatrix();
    solids.push_back(f.get());
    group.add(std::unique_ptr<Object3D>(f.release()));
    auto r = std::make_unique<Mesh>(makePlaneGeometry(0.84, 0.15), MaterialKind::Struct);
    r->position.set(0, shell.roofY + 0.085, 0.25 + 0.101);
    solids.push_back(r.get());
    group.add(std::unique_ptr<Object3D>(r.release()));
  }
}

/**
 * Lettering on both bumpers: a strip lying just in front of the flat nose
 * face (above the number plate) and of the tail face (below it), as wide as
 * the face allows.  Each strip is turned to its own side, so the text reads
 * left to right from in front as well as from behind.
 *
 * The picture is drawn by the platform (a mask of white letters on black,
 * used as the strip's alpha); here the quads are placed exactly, so the strip
 * stands ready for it.
 */
void CarExterior::addLettering(const std::string& text, uint32_t paint) {
  (void)text;
  Color c;
  c.setHex(paint);
  // dark letters on light paint, light ones on dark paint
  double lum = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
  uint32_t ink = lum > 0.18 ? 0x14171b : 0xf2f3f0;
  constexpr double LETTERING_ASPECT = 8;  // width : height of the strip
  auto strip = [&](const std::vector<std::vector<double>>& outline, double yLow, double yHigh,
                   double z, int facing) {
    auto halfWidthAt = [&](double y) {
      double w = 1e300;
      for (size_t i = 0; i + 1 < outline.size(); i++) {
        double x0 = outline[i][0], y0 = outline[i][1];
        double x1 = outline[i + 1][0], y1 = outline[i + 1][1];
        if (y0 == y1 || y < std::min(y0, y1) || y > std::max(y0, y1)) continue;
        w = std::min(w, x0 + ((x1 - x0) * (y - y0)) / (y1 - y0));
      }
      return w == 1e300 ? 0.0 : w;
    };
    // as large as fits between yLow and yHigh without leaving the flat face
    double h = yHigh - yLow;
    for (int i = 0; i < 4; i++)
      h = std::min(h, (2 * std::min(halfWidthAt(yLow), halfWidthAt(yLow + h)) - 0.03) /
                          LETTERING_ASPECT);
    auto m = std::make_unique<Mesh>(makePlaneGeometry(h * LETTERING_ASPECT, h), MaterialKind::Struct);
    tint(m->geometry, ink);
    m->position.set(0, (yLow + yHigh) / 2, z + facing * 0.006);
    if (facing < 0) m->quaternion.setFromAxisAngle(Vector3(0, 1, 0), PI);
    m->updateMatrix();
    m->renderOrder = 4;  // after the body, before the glass
    group.add(std::unique_ptr<Object3D>(m.release()));
  };
  strip(shell.noseOutline, 0.455, 0.625, shell.zFront, -1);
  strip(shell.tailOutline, 0.435, 0.595, shell.zRear, 1);
}

void CarExterior::setWheels(const double steer[4], const double spin[4], const double y[4]) {
  for (int i = 0; i < 4; i++) {
    if (i >= static_cast<int>(wheels.size())) break;
    // + steer = right = negative rotation about +Y
    wheels[static_cast<size_t>(i)]->quaternion.setFromAxisAngle(
        Vector3(0, 1, 0), i < 2 ? -steer[i] : 0);
    wheels[static_cast<size_t>(i)]->position.y = y[i];
    wheels[static_cast<size_t>(i)]->matrixAutoUpdate = true;
    // rolling forward (-Z) turns the top of the wheel forward: negative rotation about +X
    wheelSpinners[static_cast<size_t>(i)]->quaternion.setFromAxisAngle(Vector3(1, 0, 0), -spin[i]);
    wheelSpinners[static_cast<size_t>(i)]->matrixAutoUpdate = true;
  }
}

void CarExterior::setLamps(bool low, bool high, bool brake, bool reverse, bool left, bool right) {
  // The lamp colours live on the materials in the original; here they are the
  // vertex colours of the lamp boxes (rebuilt only when the state changes).
  struct Combo { bool low, high, brake, reverse, left, right; };
  static const Combo none{false, false, false, false, false, false};
  Combo now{low, high, brake, reverse, left, right};
  if (memcmp(&now, &none, sizeof now) == 0 && lampOff_) return;
  lampOff_ = memcmp(&now, &none, sizeof now) == 0;
  for (const Lamp& l : lamps) {
    uint32_t c;
    switch (l.which) {
      case 0: c = high ? 0xffffff : (low ? 0xf4f6f0 : 0xcfd6da); break;
      case 1: c = brake ? 0xff2a1e : (low ? 0xa01410 : 0x5a0d0d); break;
      case 2: c = reverse ? 0xffffff : 0x777777; break;
      default: c = (l.which == 3 ? left : right) ? 0xffa010 : 0x6a3a08; break;
    }
    tint(l.mesh->geometry, c);
  }
}

// ---------------------------------------------------------------------------
// cockpit builders (the original's buildDashboard / buildBinnacle /
// buildConsole / buildDoor / buildSeat)
// ---------------------------------------------------------------------------

namespace {

/** A colour: how a cabin material shows in daylight (cabinMaterial). */
Color cabinColor(uint32_t hex, double shine = 0) {
  (void)shine;
  Color c;
  c.setHex(hex);
  c.multiplyScalar(0.6);  // the lit share
  return c;
}

/**
 * Dashboard: a side profile that changes across the car (deeper at the doors,
 * crowned like the scuttle), with a padded top, a bright trim strip under its
 * brow and a hard lower part; then the glove box, the air vents and the centre
 * stack with its display.
 */
struct DashFns {
  std::function<double(double)> browZ;
  std::function<double(double)> browY;
};

DashFns buildDashboard(const CarShell& sh, Kit& kit) {
  double hw = sh.halfWidth;
  double zc = sh.zCowl;
  // just inside the side glass
  double xe = hw * 0.975 - 0.022;
  // lower edge of the windscreen across the car
  auto cowlAt = [&](double x) {
    return sh.cowlY + (sh.cowlCrownY - sh.cowlY) * (1 - std::fabs(x) / (hw * 0.9));
  };
  // the brow: the edge of the top towards the occupants.  It stays below the
  // base of the windscreen as the driver sees it, so the top hides no road.
  auto browZ = [&](double x) { return zc + 0.385 + 0.05 * std::pow(std::fabs(x / xe), 3); };
  auto browY = [&](double x) { return sh.beltY + 0.002 - 0.012 * (x / xe) * (x / xe); };

  std::vector<Ring3> top, band, lower;
  const int n = 14;
  for (int i = 0; i <= n; i++) {
    double x = -xe + (2 * xe * i) / n;
    double zb = browZ(x);
    double yb = browY(x);
    double zf = zc + 0.004;
    double yf = cowlAt(x) - 0.014;
    auto p = [&](double z, double y) -> std::array<double, 3> { return {x, y, z}; };
    Ring3 t;
    const double ss[4] = {0, 0.33, 0.66, 1};
    for (double s : ss)
      t.push_back(p(zf + (zb - 0.032 - zf) * s,
                    yf + (yb - yf) * s + 0.006 * std::sin(PI * s)));
    // rounded brow with a lip, the strip set back under it
    t.push_back(p(zb - 0.012, yb - 0.003));
    t.push_back(p(zb - 0.002, yb - 0.013));
    t.push_back(p(zb, yb - 0.027));
    t.push_back(p(zb - 0.008, yb - 0.04));
    t.push_back(p(zb - 0.02, yb - 0.045));
    top.push_back(t);
    band.push_back({p(zb - 0.02, yb - 0.045), p(zb - 0.018, yb - 0.074)});
    lower.push_back({p(zb - 0.018, yb - 0.074), p(zb - 0.004, yb - 0.082), p(zb + 0.004, yb - 0.1),
                     p(zb + 0.002, yb - 0.2), p(zb - 0.02, yb - 0.265), p(zb - 0.08, yb - 0.31),
                     p(zc + 0.02, yb - 0.33)});
  }
  {
    Sculpt s;
    s.sheet(top, {0, 1, 0.4});
    kit.soft.push_back(s.build(50));
  }
  {
    Sculpt s;
    s.sheet(band, AZ);
    kit.silver.push_back(s.build());
  }
  {
    Sculpt s;
    s.sheet(lower, {0, -0.3, 1});
    // closed ends towards the doors
    for (int i : {0, n}) {
      Ring3 fan = top[i];
      fan.insert(fan.end(), lower[i].begin(), lower[i].end());
      std::array<double, 3> facing = {i == 0 ? -1.0 : 1.0, 0, 0};
      s.fan(fan, facing);
    }
    kit.hard.push_back(s.build(50));
  }

  // glove box lid on the passenger's side, standing a little proud, with its handle
  Ring3 lid;
  for (int i = 0; i <= 4; i++) {
    double x = 0.25 + 0.125 * i;
    double zb = browZ(x);
    double yb = browY(x);
    lid.push_back({x, yb - 0.108, zb + 0.009});
    lid.push_back({x, yb - 0.196, zb + 0.008});
    lid.push_back({x, yb - 0.256, zb - 0.01});
  }
  {
    // the lid is one row of (x, y, z) triples: rebuild it as 5 columns of 3 rows
    std::vector<Ring3> rows(3);
    for (int r = 0; r < 3; r++)
      for (int i = 0; i < 5; i++) rows[r].push_back(lid[i * 3 + r]);
    Sculpt s;
    s.sheet(rows, {0, -0.2, 1});
    kit.soft.push_back(s.build(60));
  }
  auto handle = boxG(0.09, 0.012, 0.01, 0.5, browY(0.5) - 0.122, browZ(0.5) + 0.012);
  kit.silver.push_back(handle);

  // round air vents at both ends, turned a little towards the middle of the cabin
  for (int side : {-1, 1}) {
    double x = side * (xe - 0.115);
    std::array<double, 3> o = {x, browY(x) - 0.066, browZ(x) - 0.008};
    double yaw = -side * 0.3;
    std::array<double, 3> out = {std::sin(yaw), 0, std::cos(yaw)};
    std::array<double, 3> right = {std::cos(yaw), 0, -std::sin(yaw)};
    auto circle = [&](double r, double s) {
      Ring3 ring;
      for (int i = 0; i < 14; i++) {
        double a = (static_cast<double>(i) / 14) * PI * 2;
        auto p = along(along(along(o, out, s), right, std::cos(a) * r), AY, std::sin(a) * r);
        ring.push_back(p);
      }
      return ring;
    };
    {
      Sculpt s;
      s.tube({circle(0.047, -0.01), circle(0.047, 0.016), circle(0.04, 0.019)});
      kit.silver.push_back(s.build(50));
    }
    {
      Sculpt s;
      s.tube({circle(0.04, 0.019), circle(0.038, 0.002)}, false, false, true);
      s.fan(circle(0.038, 0.002), out);
      kit.black.push_back(s.build(50));
    }
    for (double dy : {-0.018, 0.0, 0.018})
      kit.hard.push_back(boxG(std::sqrt(0.037 * 0.037 - dy * dy) * 2, 0.004, 0.012,
                              o[0] + out[0] * 0.01, o[1] + dy, o[2] + out[2] * 0.01, 0.25, yaw));
    kit.silver.push_back(boxG(0.008, 0.012, 0.008, o[0] + out[0] * 0.018, o[1], o[2] + out[2] * 0.018,
                             0, yaw));
  }

  // ---- centre stack: display on top, two vents, heating controls ------------
  double zs = browZ(0);
  double ys = browY(0);
  std::vector<Ring3> stack;
  // rings from the top down; the face leans back, the foot widens into the console
  const double ringSpec[5][4] = {{ys - 0.052, 0.3, zs + 0.012, 0.11},
                                 {ys - 0.075, 0.32, zs + 0.03, 0.14},
                                 {0.72, 0.31, zs + 0.052, 0.17},
                                 {0.6, 0.25, zs + 0.085, 0.2},
                                 {0.5, 0.215, zs + 0.13, 0.24}};
  for (auto& r : ringSpec) {
    stack.push_back(place(roundRect(r[1], r[3], 0.028), {0, r[0], r[2] - r[3] / 2}, AX, {0, 0, -1}));
  }
  {
    Sculpt s;
    s.tube(stack, true);
    kit.hard.push_back(s.build(50));
  }
  // the face of the stack is a plane through its upper rings: things on it share its lean
  double lean = std::atan2(0.052 - 0.03, ys - 0.075 - 0.72);
  auto onFace = [&](double y) { return zs + 0.03 + (ys - 0.075 - y) * std::tan(lean); };
  for (int side : {-1, 1}) {
    double y = ys - 0.118;
    kit.black.push_back(boxG(0.118, 0.04, 0.012, side * 0.072, y, onFace(y) - 0.002, -lean));
    for (double dy : {-0.009, 0.009})
      kit.hard.push_back(boxG(0.11, 0.004, 0.014, side * 0.072, y + dy, onFace(y + dy) + 0.002,
                             -lean - 0.3));
    kit.silver.push_back(boxG(0.01, 0.022, 0.008, side * 0.072, y, onFace(y) + 0.008, -lean));
  }
  double yk = ys - 0.185;
  kit.black.push_back(boxG(0.27, 0.058, 0.01, 0, yk, onFace(yk) - 0.001, -lean));
  for (double kx : {-0.09, 0.0, 0.09}) {
    auto ring = makeCylinderGeometry(0.02, 0.022, 0.022, 14, 1, true);
    ring->rotateX(PI / 2 - lean);
    ring->translate(kx, yk, onFace(yk) + 0.014);
    ring->deleteAttribute("uv");
    kit.silver.push_back(ring);
    auto cap = makeCircleGeometry(0.02, 14);
    cap->rotateX(-lean);
    cap->translate(kx, yk + 0.011 * std::sin(lean), onFace(yk) + 0.014 + 0.011 * std::cos(lean));
    cap->deleteAttribute("uv");
    kit.black.push_back(cap);
  }
  return {browZ, browY};
}

/**
 * Instrument hood: a pod around the cluster with a rounded opening towards the
 * driver, running out forwards into the top of the dashboard.
 */
void buildBinnacle(Kit& kit, const ClusterPlace& k, double dashTopY) {
  std::array<double, 3> up = {0, std::cos(k.tilt), -std::sin(k.tilt)};
  std::array<double, 3> out = {0, std::sin(k.tilt), std::cos(k.tilt)};
  double hw = k.width / 2;
  double hh = k.height / 2;
  // rings around the axis of the instrument face; their widest line lies below its middle
  auto ring = [&](double s, double grow) {
    auto base = along(along(k.centre, out, s), up, -0.02);
    return place(softRing(hw + 0.02 + grow, hh + 0.042 + grow, hh - 0.014 + grow), base, AX, up);
  };
  double depth = 0.085;
  // outside: from inside the dashboard back to the rim, rounded over into the opening
  {
    Sculpt s;
    s.tube({ring(-0.1, 0.026), ring(0, 0.02), ring(depth, 0.014), ring(depth + 0.006, 0.007)});
    kit.soft.push_back(s.build(60));
  }
  // inside: a dark tunnel down to the instrument face
  {
    Sculpt s;
    s.tube({ring(depth + 0.006, 0.007), ring(depth, 0), ring(-0.004, -0.004)}, false, false, true);
    s.fan(ring(-0.004, -0.004), out);
    kit.black.push_back(s.build(60));
  }
  // the hood runs out forwards and downwards into the dashboard top
  auto full = ring(0, 0.02);
  std::vector<Ring3> rows;
  rows.push_back(Ring3(full.begin(), full.begin() + 13));
  const double rowSpec[2][3] = {{0.15, hw + 0.05, 0.03}, {0.3, hw + 0.01, -0.02}};
  for (auto& rs : rowSpec) {
    double dz = rs[0], half = rs[1], rise = rs[2];
    Ring3 row;
    for (int i = 0; i <= 12; i++) {
      double phi = (static_cast<double>(i) / 12) * PI;
      double c = std::cos(phi);
      double sgn = c >= 0 ? 1.0 : -1.0;
      row.push_back({k.centre[0] + half * sgn * std::sqrt(std::fabs(c)),
                     dashTopY - 0.03 + (rise + 0.03) * std::sqrt(std::sin(phi)),
                     k.centre[2] - dz});
    }
    rows.push_back(row);
  }
  {
    Sculpt s;
    s.sheet(rows, {0, 1, 0.2});
    kit.soft.push_back(s.build(60));
  }
}

/** Tunnel console between the seats: tray, gear lever frame, cup holders, padded armrest. */
void buildConsole(Kit& kit, double zStack) {
  // top line from the foot of the centre stack back to between the rear footwells
  const double line[9][3] = {{zStack + 0.04, 0.5, 0.2},  {zStack + 0.2, 0.5, 0.2},
                             {-0.02, 0.505, 0.2},       {0.2, 0.51, 0.2},
                             {0.27, 0.56, 0.21},         {0.33, 0.585, 0.21},
                             {0.7, 0.585, 0.21},         {0.76, 0.52, 0.2},
                             {0.8, 0.3, 0.19}};
  std::vector<Ring3> rings;
  for (auto& l : line) {
    double h = l[1] - 0.22;
    rings.push_back(place(roundRect(l[2], h, 0.03), {0, 0.22 + h / 2, l[0]}, AX, AY));
  }
  {
    Sculpt s;
    s.tube(rings, false, true);
    kit.hard.push_back(s.build(50));
  }
  // gear lever surround and two cup holders
  {
    Sculpt s;
    s.tube({place(roundRect(0.13, 0.16, 0.03), {0, 0.503, -0.12}, AX, {0, 0, -1}),
            place(roundRect(0.12, 0.15, 0.028), {0, 0.509, -0.12}, AX, {0, 0, -1})},
           false, true);
    kit.silver.push_back(s.build(50));
  }
  {
    Sculpt s;
    s.fan(place(roundRect(0.098, 0.128, 0.022), {0, 0.5095, -0.12}, AX, {0, 0, -1}), AY);
    kit.black.push_back(s.build());
  }
  for (double z : {0.06, 0.155}) {
    Ring3 disc, rim;
    for (int i = 0; i < 14; i++) {
      double a = (static_cast<double>(i) / 14) * PI * 2;
      disc.push_back({-0.042 + std::cos(a) * 0.036, 0.5115, z - std::sin(a) * 0.036});
      rim.push_back({-0.042 + std::cos(a) * 0.041, 0.511, z - std::sin(a) * 0.041});
    }
    {
      Sculpt s;
      s.fan(disc, AY);
      kit.black.push_back(s.build());
    }
    {
      Sculpt s;
      s.fan(rim, AY);
      kit.silver.push_back(s.build());
    }
  }
  // padded lid of the armrest
  const double padZ[4] = {0.345, 0.37, 0.67, 0.695};
  std::vector<Ring3> pad;
  for (int i = 0; i < 4; i++) {
    double z = padZ[i];
    double w = (i % 3 == 0) ? 0.18 : 0.2;
    double h = (i % 3 == 0) ? 0.012 : 0.03;
    pad.push_back(place(roundRect(w, h, 0.006, 1), {0, 0.598, z}, AX, AY));
  }
  {
    Sculpt s;
    s.tube(pad, true, true);
    kit.accent.push_back(s.build(60));
  }
}

/**
 * Inner door panel between z0 (front) and z1: padded top roll at the window
 * sill, a fabric insert, an armrest with the door pull, the handle and a
 * loudspeaker grille.  The driver's door also carries the window switches.
 */
void buildDoor(const CarShell& sh, Kit& kit, int side, double z0, double z1, bool front) {
  // stations along the door: the front doors are in view all the time, the rear ones hardly ever
  int n = front ? 8 : 5;
  double xg = sh.halfWidth * 0.975 - 0.006;
  // the sill follows the lower edge of the glass, which rises from the scuttle to the cabin
  auto sill = [&](double z) {
    double u = std::max(0.0, std::min(1.0, (sh.zRoofFront - z) / (sh.zRoofFront - sh.zCowl)));
    return sh.beltY - 0.004 - (sh.beltY - (sh.cowlY - 0.015)) * u;
  };
  std::vector<Ring3> roll, ins, panel;
  double armY = 0.735;
  auto armAt = [&](double z) {
    // the armrest grows out of the panel behind the door's front edge and fades before its rear edge
    double u = (z - z0) / (z1 - z0);
    auto ramp = [&](double a, double b) {
      double t = std::max(0.0, std::min(1.0, (u - a) / (b - a)));
      return t * t * (3 - 2 * t);
    };
    return ramp(0.2, 0.36) * (1 - ramp(0.84, 0.97));
  };
  for (int i = 0; i <= n; i++) {
    double z = z0 + ((z1 - z0) * i) / n;
    double ys = sill(z);
    double a = armAt(z);
    auto p = [&](double inward, double y) -> std::array<double, 3> {
      return {side * (xg - inward), y, z};
    };
    roll.push_back({p(0, ys), p(0.04, ys + 0.004), p(0.068, ys - 0.016), p(0.076, ys - 0.05),
                    p(0.07, ys - 0.074)});
    ins.push_back({p(0.07, ys - 0.074), p(0.07, armY + 0.05)});
    panel.push_back({p(0.07, armY + 0.05), p(0.078 + 0.062 * a, armY + 0.028),
                     p(0.082 + 0.068 * a, armY), p(0.08 + 0.055 * a, armY - 0.045),
                     p(0.078, armY - 0.1), p(0.074, 0.3), p(0.03, 0.24)});
  }
  std::array<double, 3> facing = {-static_cast<double>(side), 0.2, 0};
  {
    Sculpt s;
    s.sheet(roll, {-static_cast<double>(side), 1, 0});
    kit.soft.push_back(s.build(60));
  }
  {
    Sculpt s;
    s.sheet(ins, facing);
    kit.accent.push_back(s.build());
  }
  {
    Sculpt s;
    s.sheet(panel, facing);
    // the panel's front and rear edges
    for (int i : {0, n}) {
      Ring3 fan = roll[i];
      fan.insert(fan.end(), panel[i].begin(), panel[i].end());
      std::array<double, 3> f = {0, 0, i == 0 ? -1.0 : 1.0};
      s.fan(fan, f);
    }
    kit.hard.push_back(s.build(50));
  }

  double len = z1 - z0;
  // release handle in a dark recess, towards the front of the door
  double zh = z0 + len * (front ? 0.3 : 0.26);
  double yh = sill(zh) - 0.11;
  kit.black.push_back(boxG(0.006, 0.05, 0.15, side * (xg - 0.073), yh, zh));
  kit.silver.push_back(boxG(0.014, 0.022, 0.1, side * (xg - 0.083), yh, zh + 0.012));
  // door pull on the armrest
  double zp = z0 + len * 0.6;
  kit.black.push_back(boxG(0.05, 0.008, len * 0.22, side * (xg - 0.118), armY + 0.031, zp, 0, 0,
                          side * 0.12));
  if (front && side < 0) {
    for (double dz : {-0.03, -0.01, 0.01, 0.03})
      kit.silver.push_back(boxG(0.022, 0.006, 0.012, side * (xg - 0.12), armY + 0.037,
                               zp - len * 0.18 + dz, 0, 0, side * 0.12));
  }
  // loudspeaker grille low in the panel
  double zl = z0 + len * (front ? 0.24 : 0.3);
  Ring3 grille;
  for (int i = 0; i < 12; i++) {
    double a = (static_cast<double>(i) / 12) * PI * 2;
    grille.push_back({side * (xg - 0.079), 0.5 + std::sin(a) * 0.075, zl + std::cos(a) * 0.075});
  }
  {
    Sculpt s;
    s.fan(grille, {-static_cast<double>(side), 0, 0});
    kit.black.push_back(s.build());
  }
}

/**
 * A seat: cushion and backrest with raised side bolsters, a head restraint and
 * centre panels in a second fabric.  `width` 0.5 is a single seat, the rear
 * bench is the same shape drawn wide with two head restraints.
 */
void buildSeat(Kit& kit, double x, double zFront, double width, double recline,
               const std::vector<double>& restraints) {
  double w = width / 2;
  double top = 0.462;
  double zRear = zFront + 0.48;
  // cushion: outline across the car, swept from front to rear
  auto cushion = [&](double z, double rise, double scale) {
    const double o[8][2] = {{-w, -0.12}, {w, -0.12},     {w, -0.005},   {w - 0.035, 0.028},
                            {w - 0.095, 0}, {-(w - 0.095), 0}, {-(w - 0.035), 0.028}, {-w, -0.005}};
    Ring3 out;
    for (auto& q : o)
      out.push_back({x + q[0] * scale,
                     top + rise + q[1] * (q[1] > -0.1 ? scale : 1), z});
    return out;
  };
  {
    Sculpt s;
    s.tube({cushion(zFront - 0.02, -0.035, 0.9), cushion(zFront + 0.02, 0.012, 1),
            cushion(zFront + 0.2, 0.004, 1), cushion(zRear, -0.02, 1)},
           true, true);
    kit.fabric.push_back(s.build(55));
  }
  // backrest: outline across the car, swept up along the reclined back
  std::array<double, 3> axis = {0, std::cos(recline), std::sin(recline)};
  std::array<double, 3> fwd = {0, std::sin(recline), -std::cos(recline)};
  std::array<double, 3> foot = {x, top - 0.03, zRear - 0.035};
  auto back = [&](double sc, double scale, double bolster, double thick) {
    double b = w * scale;
    const double o[8][2] = {{-b, -thick}, {b, -thick}, {b, 0},      {b - 0.03, 0.035 * bolster},
                            {b - 0.09, 0}, {-(b - 0.09), 0}, {-(b - 0.03), 0.035 * bolster}, {-b, 0}};
    Ring2 o2;
    for (auto& q : o) o2.push_back({q[0], q[1]});
    return place(o2, along(foot, axis, sc), AX, fwd);
  };
  double height = 0.62;
  {
    Sculpt s;
    s.tube({back(0, 0.96, 1, 0.1), back(0.22, 0.98, 1, 0.1), back(0.46, 0.94, 0.7, 0.085),
            back(height - 0.02, 0.86, 0.2, 0.07), back(height, 0.78, 0, 0.05)},
           false, true);
    kit.fabric.push_back(s.build(55));
  }
  // centre panels, a few millimetres proud of the hollow between the bolsters
  double half = w - 0.105;
  Sculpt panels;
  {
    std::vector<Ring3> rows;
    const double zz[3] = {zFront + 0.05, zFront + 0.2, zRear - 0.05};
    const double off[3] = {0.014, 0.008, -0.006};
    for (int i = 0; i < 3; i++)
      rows.push_back({{x - half, top + off[i], zz[i]}, {x + half, top + off[i], zz[i]}});
    panels.sheet(rows, AY);
  }
  {
    std::vector<Ring3> rows;
    const double ss[3] = {0.07, 0.3, 0.52};
    for (double sc : ss) {
      auto o = along(along(foot, axis, sc), fwd, 0.004);
      double b = w * 0.95 - 0.1;
      rows.push_back({{o[0] - b, o[1], o[2]}, {o[0] + b, o[1], o[2]}});
    }
    panels.sheet(rows, fwd);
  }
  kit.accent.push_back(panels.build());
  // head restraints on the line of the backrest
  for (double rx : restraints) {
    std::vector<Ring3> rings;
    const double spec[4][2] = {{height - 0.005, 0.8}, {height + 0.03, 1},
                               {height + 0.13, 1}, {height + 0.165, 0.8}};
    for (auto& sp : spec) {
      std::array<double, 3> base = {x + rx, foot[1], foot[2]};
      rings.push_back(place(roundRect(0.25 * sp[1], 0.085 * sp[1], 0.03 * sp[1], 1),
                            along(along(base, axis, sp[0]), fwd, -0.03), AX, fwd));
    }
    Sculpt s;
    s.tube(rings, true, true);
    kit.fabric.push_back(s.build(60));
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// PlayerCar
// ---------------------------------------------------------------------------

PlayerCar::PlayerCar() {
  exterior = std::make_unique<CarExterior>(0xc9cdd2, [&] {
    CarExteriorOptions o;
    o.plate = "KS FS 2026";
    o.seeThroughGlass = true;
    o.roofSign = true;
    o.noMirrorHousings = true;
    o.lettering = "maureranton.com";
    o.ownMaterials = true;
    return o;
  }());
  const CarShell& sh = exterior->shell;
  // the exterior's group and the cockpit are owned by this object, so the
  // scene only borrows them (as three.js does with scene.add)
  root.addRef(&exterior->group);
  root.addRef(&cockpit);
  double hw = sh.halfWidth;
  double belt = sh.beltY;
  double zc = sh.zCowl;
  double ex = eye.x;

  // the cabin colours (cabinMaterial: the lit share is a vertex colour)
  struct Mats {
    Color soft, hard, pale, fabric, accent, carpet, silver, black;
  } mats{cabinColor(0x40444b), cabinColor(0x575d65), cabinColor(0xb9bbb8), cabinColor(0x4a5466),
         cabinColor(0x77849a), cabinColor(0x2c2e33), cabinColor(0xb9bec4, 50),
         cabinColor(0x1a1c20)};
  Color leather = cabinColor(0x2a2c31, 18);
  Kit kit;

  // a cabin part: a mesh under the cockpit, registered for the see-through cabin
  auto part = [&](GeometryPtr geo, const Color& col, int layer = 0,
                  Object3D* parent = nullptr) -> Mesh* {
    if (!parent) parent = &cockpit;
    auto m = std::make_unique<Mesh>(geo, MaterialKind::Struct);
    Mesh* raw = m.get();
    tint(raw->geometry, col);
    parent->add(std::unique_ptr<Object3D>(m.release()));
    ghost(raw, layer);
    return raw;
  };

  // ---- shell seen from inside ---------------------------------------------
  part(sh.lining, mats.carpet);
  part(sh.headliner, mats.pale);
  part(sh.pillarTrim, mats.pale);

  // ---- dashboard, instrument hood, console, doors, seats -------------------
  DashFns dash = buildDashboard(sh, kit);
  // The cluster stands where the driver sees it through the upper half of the
  // steering wheel: its top just under the rim, its foot just above the hub.
  ClusterPlace cluster;
  cluster.centre = {ex, 0.905, zc + 0.435};
  cluster.tilt = 0.32;
  cluster.width = 0.36;
  cluster.height = 0.12;
  buildBinnacle(kit, cluster, dash.browY(ex));
  buildConsole(kit, dash.browZ(0));
  buildDoor(sh, kit, -1, zc + 0.1, sh.zPillarB - 0.04, true);
  buildDoor(sh, kit, 1, zc + 0.1, sh.zPillarB - 0.04, true);
  buildDoor(sh, kit, -1, sh.zPillarB + 0.04, sh.zRoofRear - 0.15, false);
  buildDoor(sh, kit, 1, sh.zPillarB + 0.04, sh.zRoofRear - 0.15, false);
  buildSeat(kit, ex, -0.15, 0.5, 0.3, {0});
  buildSeat(kit, -ex, -0.15, 0.5, 0.3, {0});
  buildSeat(kit, 0, 0.86, 1.3, 0.36, {-0.36, 0.36});
  // parcel shelf behind the rear seat
  {
    Sculpt s;
    s.sheet({{{-hw + 0.1, 0.985, 1.49}, {hw - 0.1, 0.985, 1.49}},
             {{-hw + 0.1, 0.975, sh.zTail + 0.02}, {hw - 0.1, 0.975, sh.zTail + 0.02}}},
            AY);
    kit.carpet.push_back(s.build());
  }
  // seat belts hanging on the B pillars with their tongues, footrest beside the clutch
  for (int side : {-1, 1}) {
    // from the turning loop high on the pillar (which leans in with the side glass) to the reel at its foot
    std::array<double, 3> a = {side * (hw * 0.8 - 0.004), 1.22, sh.zPillarB - 0.023};
    std::array<double, 3> b = {side * (hw - 0.065), 0.5, sh.zPillarB - 0.023};
    std::array<double, 3> a2 = {a[0], a[1], a[2] + 0.046};
    std::array<double, 3> b2 = {b[0], b[1], b[2] + 0.046};
    Sculpt s;
    s.quad(a.data(), a2.data(), b2.data(), b.data());
    s.quad(a.data(), b.data(), b2.data(), a2.data());
    kit.black.push_back(s.build());
    kit.silver.push_back(boxG(0.012, 0.03, 0.06, a[0], a[1] + 0.012, sh.zPillarB));
    kit.silver.push_back(boxG(0.01, 0.05, 0.032,
                             a[0] + (b[0] - a[0]) * 0.55 - side * 0.006, 0.824, sh.zPillarB, 0, 0,
                             side * 0.16));
  }
  kit.black.push_back(boxG(0.1, 0.02, 0.24, ex - 0.36, 0.34, zc + 0.19, -0.9));
  // sun visors folded up against the roof lining, and the roof lamp
  auto roofAt = [&](double x) {
    return sh.roofY - 0.004 - 0.051 * (std::fabs(x) / (hw * 0.74)) - 0.025;
  };
  for (int side : {-1, 1})
    kit.pale.push_back(boxG(0.33, 0.014, 0.15, side * 0.465, roofAt(0.465) - 0.008,
                            sh.zRoofFront + 0.105, 0.01, 0, -side * 0.077));
  kit.hard.push_back(boxG(0.15, 0.014, 0.085, 0.03, roofAt(0.03) - 0.006, sh.zRoofFront + 0.2));
  kit.pale.push_back(boxG(0.05, 0.004, 0.06, -0.005, roofAt(0.03) - 0.014, sh.zRoofFront + 0.2));
  kit.pale.push_back(boxG(0.05, 0.004, 0.06, 0.065, roofAt(0.03) - 0.014, sh.zRoofFront + 0.2));

  // one mesh per cabin colour (as the original: for each name, merge the list)
  auto addKit = [&](std::vector<GeometryPtr>& list, const Color& col) {
    if (list.empty()) return;
    GeometryPtr merged = list.size() == 1 ? list[0] : mergeGeometries(list, false);
    part(merged, col);
  };
  addKit(kit.soft, mats.soft);
  addKit(kit.hard, mats.hard);
  addKit(kit.pale, mats.pale);
  addKit(kit.fabric, mats.fabric);
  addKit(kit.accent, mats.accent);
  addKit(kit.carpet, mats.carpet);
  addKit(kit.silver, mats.silver);
  addKit(kit.black, mats.black);

  // ---- instrument cluster and centre display: self-lit pictures ------------
  // (the cluster and display pictures are drawn by the platform; the quads are
  // placed exactly, so they stand ready — the same as the sign atlas)
  {
    auto dial = std::make_unique<Mesh>(makePlaneGeometry(cluster.width, cluster.height),
                                       MaterialKind::Struct);
    dial->name = "cluster";
    dial->position.set(cluster.centre[0], cluster.centre[1], cluster.centre[2]);
    dial->quaternion.setFromAxisAngle(Vector3(1, 0, 0), -cluster.tilt);
    dial->updateMatrix();
    cockpit.add(std::unique_ptr<Object3D>(dial.release()));
  }
  // centre display, standing on the stack and turned a little towards the driver
  {
    Group* display = new Group();
    display->position.set(0, dash.browY(0) - 0.025, dash.browZ(0) + 0.05);
    display->quaternion.setFromEuler(Euler(-0.2, -0.13, 0, Euler::YXZ));
    display->matrixAutoUpdate = true;
    cockpit.addRef(display);
    {
      Sculpt s;
      std::vector<Ring3> rings;
      const double zz[3] = {0, -0.008, -0.016};
      for (int i = 0; i < 3; i++) {
        double w = (i == 2) ? 0.25 : 0.262;
        double h = (i == 2) ? 0.14 : 0.152;
        rings.push_back(place(roundRect(w, h, 0.014, 1), {0, 0, zz[i]}, AX, AY));
      }
      s.tube(rings, true, true);
      part(s.build(50), mats.black, 0, display);
    }
    part(boxG(0.07, 0.07, 0.03, 0, -0.07, -0.028), mats.hard, 0, display);
    auto screen = std::make_unique<Mesh>(makePlaneGeometry(0.244, 0.134), MaterialKind::Struct);
    screen->name = "screen";
    screen->position.z = 0.0012;
    display->add(std::unique_ptr<Object3D>(screen.release()));
  }

  // ---- steering wheel -------------------------------------------------------
  // Rim plane in the wheel's own XY plane, +Z towards the driver; the hub lies
  // deeper than the rim (a dished wheel).
  double R = 0.17;
  {
    auto rim = makeTorusGeometry(R, 0.0155, 8, 44);
    rim->deleteAttribute("uv");
    part(rim, leather, 1, &steering_);
  }
  Ring3 hubRingsSpec[4] = {
      place(softRing(0.074 * 0.8, 0.05 * 0.9, 0.058 * 0.9, 16), {-0.085, -0.004, 0}, AX, AY),
      place(softRing(0.074 * 1.0, 0.05 * 1.0, 0.058 * 1.0, 16), {-0.05, -0.004, 0}, AX, AY),
      place(softRing(0.074 * 1.0, 0.05 * 1.0, 0.058 * 1.0, 16), {-0.03, -0.004, 0}, AX, AY),
      place(softRing(0.074 * 0.86, 0.05 * 0.84, 0.058 * 0.84, 16), {-0.022, -0.004, 0}, AX, AY),
  };
  // (softRing's [u,v] maps to x,y at z: rebuild with z from the spec)
  auto hubRing = [&](double z, double kw, double kh) {
    Ring2 o = softRing(0.074 * kw, 0.05 * kh, 0.058 * kh, 16);
    Ring3 out;
    for (auto& p : o) out.push_back({p[0], p[1] - 0.004, z});
    return out;
  };
  Sculpt wheelHard;
  wheelHard.tube({hubRing(-0.085, 0.8, 0.9), hubRing(-0.05, 1, 1), hubRing(-0.03, 1, 1),
                  hubRing(-0.022, 0.86, 0.84)},
                 true, true);
  // spokes at 9, 3 and 6 o'clock, sloping from the hub out to the rim
  auto spoke = [&](double angle, double wHub, double wRim) {
    double c = std::cos(angle), sn = std::sin(angle);
    const double spec[3][3] = {{0.05, wHub, -0.04},
                               {0.1, (wHub + wRim) / 2, -0.022},
                               {R - 0.006, wRim, -0.004}};
    std::vector<Ring3> rings;
    for (auto& sp : spec) {
      Ring2 o = roundRect(sp[1], 0.016, 0.006, 1);
      Ring3 r;
      for (auto& p : o) r.push_back({c * sp[0] - sn * p[0], sn * sp[0] + c * p[0], sp[2] + p[1]});
      rings.push_back(r);
    }
    wheelHard.tube(rings);
  };
  spoke(PI - 0.07, 0.056, 0.04);
  spoke(0.07, 0.056, 0.04);
  spoke(-PI / 2, 0.05, 0.034);
  part(wheelHard.build(50), mats.soft, 1, &steering_);
  {
    std::vector<Ring3> surround;
    const double zz[2] = {-0.04, -0.031};
    for (double z : zz) {
      Ring2 o = softRing(0.074 * 1.06, 0.05 * 1.07, 0.058 * 1.06, 16);
      Ring3 r;
      for (auto& p : o) r.push_back({p[0], p[1] - 0.004, z});
      surround.push_back(r);
    }
    Sculpt s;
    s.tube(surround, false, true);
    part(s.build(50), mats.silver, 1, &steering_);
  }
  // switch pads on the side spokes, a bright clasp on the lower one
  {
    std::vector<GeometryPtr> pads;
    pads.push_back(boxG(0.04, 0.028, 0.004, -0.098, -0.004, -0.0125, 0, -0.2));
    pads.push_back(boxG(0.04, 0.028, 0.004, 0.098, -0.004, -0.0125, 0, 0.2));
    part(mergeGeometries(pads, false), mats.black, 1, &steering_);
  }
  part(boxG(0.03, 0.05, 0.004, 0, -0.112, -0.0105, 0.2), mats.silver, 1, &steering_);
  // top-centre marker so the wheel position reads at a glance
  {
    auto marker = makeTorusGeometry(R, 0.0165, 8, 2, 0.085);
    marker->rotateZ(PI / 2 - 0.0425);
    marker->deleteAttribute("uv");
    auto m = std::make_unique<Mesh>(marker, MaterialKind::Struct);
    tint(m->geometry, 0xd8dde2);
    steering_.add(std::unique_ptr<Object3D>(m.release()));
  }
  Group* column = new Group();
  // The top of the rim stays just under the line of sight to the base of the windscreen.
  column->position.set(ex, 0.907, -0.27);
  column->quaternion.setFromAxisAngle(Vector3(1, 0, 0), -0.42);  // the wheel's axis tilts up
  column->matrixAutoUpdate = true;
  column->addRef(&steering_);
  cockpit.addRef(column);
  // column shroud from behind the hub into the dashboard
  {
    std::vector<Ring3> shroud;
    const double spec[3][3] = {{-0.05, 0.1, 0.085}, {-0.12, 0.118, 0.1}, {-0.3, 0.14, 0.12}};
    for (auto& sp : spec)
      shroud.push_back(place(roundRect(sp[1], sp[2], 0.03), {0, -0.012, sp[0]}, AX, AY));
    Sculpt s;
    s.tube(shroud, true);
    part(s.build(50), mats.hard, 0, column);
  }
  // indicator stalk (left) and wiper stalk (right) do not turn with the wheel
  auto stalkGeo = [&](int side) {
    std::vector<GeometryPtr> parts;
    auto a = makeCylinderGeometry(0.0055, 0.008, 0.14, 8);
    a->rotateZ(side * PI / 2);
    a->translate(side * 0.125, 0, 0);
    a->deleteAttribute("uv");
    parts.push_back(a);
    auto b = makeCylinderGeometry(0.0105, 0.0105, 0.036, 8);
    b->rotateZ(side * PI / 2);
    b->translate(side * 0.2, 0, 0);
    b->deleteAttribute("uv");
    parts.push_back(b);
    return mergeGeometries(parts, false);
  };
  stalk_.position.set(0, 0.04, -0.085);
  stalk_.matrixAutoUpdate = true;
  part(stalkGeo(-1), mats.black, 1, &stalk_);
  Mesh* wiper = part(stalkGeo(1), mats.black, 1, column);
  wiper->position.set(0, 0.04, -0.085);
  wiper->quaternion.setFromAxisAngle(Vector3(0, 0, 1), -0.06);
  wiper->matrixAutoUpdate = true;
  column->addRef(&stalk_);
  cockpit.addRef(&stalk_);

  // ---- gear lever, handbrake, pedals ---------------------------------------
  lever_.position.set(0, 0.505, -0.12);
  lever_.matrixAutoUpdate = true;
  {
    // (the gaiter's skirt reaches down into the console, so tilting the lever opens no gap)
    std::vector<Ring3> boot;
    const double spec[3][3] = {{-0.022, 0.05, 0.063}, {0.05, 0.03, 0.036}, {0.095, 0.014, 0.016}};
    for (auto& sp : spec) {
      Ring3 ring;
      for (int i = 0; i < 10; i++) {
        double an = (static_cast<double>(i) / 10) * PI * 2;
        // a little pleated, like a leather gaiter
        double k = (i % 2 == 0) ? 1.0 : 0.9;
        ring.push_back({std::cos(an) * sp[1] * k, sp[0], -std::sin(an) * sp[2] * k});
      }
      boot.push_back(ring);
    }
    Sculpt s;
    s.tube(boot);
    part(s.build(35), leather, 1, &lever_);
  }
  {
    auto shaft = makeCylinderGeometry(0.0075, 0.0075, 0.12, 8);
    shaft->translate(0, 0.14, 0);
    shaft->deleteAttribute("uv");
    part(shaft, mats.silver, 1, &lever_);
  }
  {
    auto knob = makeSphereGeometry(0.027, 12, 8);
    knob->scale(1, 0.9, 1.05);
    knob->translate(0, 0.205, 0);
    knob->deleteAttribute("uv");
    part(knob, leather, 1, &lever_);
  }
  cockpit.addRef(&lever_);

  handbrake_.position.set(0.058, 0.525, 0.25);
  handbrake_.matrixAutoUpdate = true;
  {
    std::vector<Ring3> grip;
    const double spec[5][3] = {{0.01, 0.03, 0.026}, {-0.07, 0.03, 0.026}, {-0.1, 0.036, 0.034},
                               {-0.2, 0.036, 0.034}, {-0.212, 0.026, 0.024}};
    for (auto& sp : spec)
      grip.push_back(place(roundRect(sp[1], sp[2], 0.01, 1), {0, 0.016, sp[0]}, AX, AY));
    Sculpt s;
    s.tube(grip, true, true);
    part(s.build(50), leather, 1, &handbrake_);
  }
  {
    auto btn = makeCylinderGeometry(0.008, 0.008, 0.014, 8);
    btn->rotateX(PI / 2);
    btn->translate(0, 0.016, -0.218);
    btn->deleteAttribute("uv");
    part(btn, mats.silver, 1, &handbrake_);
  }
  cockpit.addRef(&handbrake_);

  const double pedalX[3] = {ex - 0.19, ex - 0.05, ex + 0.11};
  for (int i = 0; i < 3; i++) {
    Group* p = new Group();
    p->position.set(pedalX[i], 0.52, zc + 0.15);
    p->quaternion.setFromAxisAngle(Vector3(1, 0, 0), -0.5);
    p->matrixAutoUpdate = true;
    part(boxG(0.016, 0.22, 0.012, 0, -0.11, 0), mats.black, 0, p);
    part(boxG(i == 2 ? 0.046 : 0.07, i == 2 ? 0.12 : 0.06, 0.014, 0, i == 2 ? -0.2 : -0.23, 0.006),
         mats.black, 0, p);
    pedals_.push_back(p);
    cockpit.addRef(p);
  }

  // ---- mirrors ---------------------------------------------------------------
  // Inside mirror: small, curved and tucked up under the roof's front edge.
  // There it stays clear of the stretch of windscreen in which the traffic
  // lights of a junction appear to the car waiting first at the line.
  std::array<double, 3> inner = {ex + 0.172, sh.roofY - 0.077, sh.zRoofFront + 0.044};
  {
    MirrorSpec ms;
    ms.name = "inside";
    ms.centre = Vector3(inner[0], inner[1], inner[2]);
    ms.width = 0.15;
    ms.height = 0.042;
    ms.normal = mirrorNormal(ms.centre, eye, Vector3(0, -0.03, 1));
    ms.convex = 1.5;
    ms.resolution = {512, 160};
    mirrors.push_back(std::make_unique<Mirror>(ms));
  }
  for (int side : {-1, 1}) {
    // at the front corner of the door glass: there the left one is inside the
    // forward view on an ordinary screen (about 36° off the line of sight)
    Vector3 c(side * (hw + 0.1), belt + 0.06, zc + 0.28);
    MirrorSpec ms;
    ms.name = side < 0 ? "left" : "right";
    ms.centre = c;
    ms.width = 0.17;
    ms.height = 0.105;
    ms.normal = mirrorNormal(c, eye, Vector3(side * 0.16, -0.06, 1));
    ms.convex = side < 0 ? 1.25 : 1.6;
    ms.resolution = {320, 200};
    mirrors.push_back(std::make_unique<Mirror>(ms));
  }
  // Housings: a rounded shell behind each glass.  They stay solid when the
  // cabin is see-through – a mirror must always read as a mirror.
  Color shellPaint;
  shellPaint.setHex(0xc9cdd2);
  Color mirrorPlastic = cabinColor(0x24262a);
  for (size_t i = 0; i < mirrors.size(); i++) {
    Mirror& m = *mirrors[i];
    const MirrorSpec& s = m.spec;
    Vector3 n = s.normal;
    n.normalize();
    Vector3 up(0, 1, 0);
    up.addScaledVector(n, -n.y);
    up.normalize();
    Vector3 right;
    right.crossVectors(up, n);
    double depth = (i == 0) ? 0.024 : 0.085;
    std::vector<Ring3> rings;
    const double spec[4][2] = {{0.002, 1}, {0.4, 1}, {0.8, 0.82}, {1, 0.5}};
    for (auto& sp : spec) {
      Vector3 o = s.centre;
      o.addScaledVector(n, -sp[0] * depth);
      std::array<double, 3> o3 = {o.x, o.y, o.z};
      std::array<double, 3> r3 = {right.x, right.y, right.z};
      std::array<double, 3> u3 = {up.x, up.y, up.z};
      rings.push_back(place(roundRect((s.width + 0.022) * sp[1], (s.height + 0.022) * sp[1],
                                     0.018 * sp[1], 2),
                            o3, r3, u3));
    }
    Sculpt s2;
    s2.tube(rings, true, true);
    Mesh* housing = part(s2.build(50), (i == 0) ? mirrorPlastic : shellPaint);
    (void)housing;
    cockpit.addRef(m.mesh.get());
  }
  // stem of the inside mirror up to the roof's front edge, feet of the door mirrors
  std::vector<GeometryPtr> feet;
  {
    std::vector<Ring3> rings;
    std::array<double, 3> o1 = {inner[0] + 0.02, inner[1] + 0.012, inner[2] - 0.02};
    std::array<double, 3> o2 = {inner[0] + 0.035, sh.roofY - 0.036, sh.zRoofFront + 0.012};
    std::array<double, 3> up = {0, 0.5, 0.87};
    rings.push_back(place(roundRect(0.022, 0.022, 0.006, 1), o1, AX, up));
    rings.push_back(place(roundRect(0.04, 0.03, 0.006, 1), o2, AX, up));
    Sculpt s;
    s.tube(rings, true, true);
    feet.push_back(s.build(50));
  }
  for (int side : {-1, 1}) {
    // a short arm from the door, slimmer towards the housing it carries
    const double spec[3][3] = {{hw - 0.035, 0.066, 0.038},
                               {hw + 0.03, 0.056, 0.032},
                               {hw + 0.092, 0.042, 0.022}};
    std::vector<Ring3> arm;
    for (auto& sp : spec)
      arm.push_back(place(roundRect(sp[1], sp[2], 0.009, 1),
                          {side * sp[0], belt - 0.013, zc + 0.264}, AZ, AY));
    Sculpt s;
    s.tube(arm, false, true);
    feet.push_back(s.build(50));
  }
  {
    GeometryPtr merged = mergeGeometries(feet, false);
    auto m = std::make_unique<Mesh>(merged, MaterialKind::Struct);
    tint(m->geometry, mirrorPlastic);
    cockpit.add(std::unique_ptr<Object3D>(m.release()));
  }

  // ---- see-through cabin: the body belongs to it as well --------------------
  for (Mesh* m : exterior->solids) ghost(m, 0);
}

/**
 * Register a part of the see-through cabin.  The twin writes depth only (the
 * engine's way of "colourWrite = false": it is the same geometry, so it can
 * only be a depth pass when the renderer draws it first); the mesh itself is
 * what shows when the cabin is see-through.
 */
void PlayerCar::ghost(Mesh* mesh, int layer) {
  auto twin = std::make_unique<Mesh>(mesh->geometry, MaterialKind::Struct);
  Mesh* raw = twin.get();
  raw->visible = false;
  raw->renderOrder = (layer == 0) ? 1 : 3;
  mesh->add(std::unique_ptr<Object3D>(twin.release()));
  ghosts_.push_back({mesh, raw, layer});
  if (std::find(ghostMaterials_.begin(), ghostMaterials_.end(), mesh) == ghostMaterials_.end())
    ghostMaterials_.push_back(mesh);
}

/**
 * How solid the cabin looks from the driver's seat: 1 = as built, smaller
 * values let the road show through dashboard, pillars, doors, roof, bonnet
 * and steering wheel (0.2 = strongly see-through).  Instruments, displays
 * and mirrors stay solid.  Seen from outside the car is always solid.
 */
void PlayerCar::setCabinOpacity(double opacity) {
  double o = std::isfinite(opacity) ? std::min(1.0, std::max(0.05, opacity)) : 1.0;
  bool on = o < 0.995;
  bool was = cabinOpacity_ < 1;
  cabinOpacity_ = on ? o : 1;
  if (on == was) return;
  for (const Ghost& g : ghosts_) {
    g.twin->visible = on;
    // draw order: depth of the cabin, cabin, depth of the controls, controls (then the glass, 5)
    g.mesh->renderOrder = on ? (g.layer == 0 ? 2 : 4) : 0;
  }
}

void PlayerCar::update(const CarVisualState& s, double dt, double now) {
  // the instrument cluster at the foot of the windscreen
  DashState d;
  d.speedKmh = s.speedKmh;
  d.rpm = s.rpm;
  d.gear = s.gear;
  d.indicator = s.indicator;
  d.hazard = s.hazard;
  d.blinkOn = s.blinkOn;
  d.lowBeam = s.lowBeam;
  d.highBeam = s.highBeam;
  d.handbrake = s.handbrake;
  d.ignition = s.ignition;
  d.engineRunning = s.engineRunning;
  d.abs = s.abs;
  d.seatbelt = s.seatbelt;
  d.fuel = s.fuel;
  d.automatic = s.automatic;
  d.showGear = s.showGear;
  d.odometerKm = s.odometerKm;
  panel_.update(d, now);

  if (exterior) {
    exterior->setWheels(s.wheelSteer, s.wheelSpin, s.wheelY);
    // (the lamps blink: only the lit phase of the indicators is shown)
    exterior->setLamps(s.lowBeam, s.highBeam, s.braking, s.reversing,
                       s.blinkOn && (s.hazard || s.indicator < 0),
                       s.blinkOn && (s.hazard || s.indicator > 0));
  }
  steering_.quaternion.setFromAxisAngle(Vector3(0, 0, 1), -s.steerWheel);
  steering_.matrixAutoUpdate = true;
  stalk_.quaternion.setFromAxisAngle(Vector3(0, 0, 1), s.indicator * 0.16);  // up = right
  stalk_.matrixAutoUpdate = true;

  // H gate: R far left forward, 1/2 left, 3/4 middle, 5/6 right
  double tx = 0, tz = 0;
  int g = s.gear;
  if (!s.automatic) {
    if (g == -1) {
      tx = -1.5;
      tz = -1;
    } else if (g > 0) {
      tx = std::floor(static_cast<double>(g - 1) / 2) - 1;
      tz = (g % 2 == 1) ? -1 : 1;
    }
    if (g != 0) gate_ = tx;
    else tx = gate_ * 0.0;  // neutral springs to the 3/4 plane
  } else {
    tz = g == -2 ? -1.5 : (g == -1 ? -0.75 : (g == 0 ? 0 : 0.75));
  }
  double k = std::min(1.0, std::max(0.0, dt * 14));
  leverX_ += (tx - leverX_) * k;
  leverZ_ += (tz - leverZ_) * k;
  lever_.quaternion.setFromAxisAngle(Vector3(0, 0, 1), -leverX_ * 0.17);
  lever_.matrixAutoUpdate = true;
  handbrake_.quaternion.setFromAxisAngle(Vector3(1, 0, 0), s.handbrakeLever * 0.42);
  handbrake_.matrixAutoUpdate = true;
  // pads hang towards the driver; pressing swings them forward (an automatic has no clutch)
  if (!pedals_.empty()) pedals_[0]->visible = !s.automatic;
  if (pedals_.size() > 0)
    pedals_[0]->quaternion.setFromAxisAngle(Vector3(1, 0, 0), -0.5 + s.clutch * 0.32);
  if (pedals_.size() > 1)
    pedals_[1]->quaternion.setFromAxisAngle(Vector3(1, 0, 0), -0.5 + s.brake * 0.22);
  if (pedals_.size() > 2)
    pedals_[2]->quaternion.setFromAxisAngle(Vector3(1, 0, 0), -0.5 + s.throttle * 0.28);
  for (Group* p : pedals_) p->matrixAutoUpdate = true;
}

}  // namespace fs

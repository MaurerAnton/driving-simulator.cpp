// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/car.ts @ v1.7.7
#include "render/car.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <map>

#include "core/js.h"
#include "engine/geometries.h"

namespace fs {

namespace {

/** a point [u, v] of a flat outline */
using UV = std::array<double, 2>;
/** a row of numbers of a table written out in the code (`for (const [z, w, h] of [[…], […]])`) */
template <size_t N>
using Rows = std::initializer_list<std::array<double, N>>;

/** Position and normal only, un-indexed: the form in which parts are merged. */
GeometryPtr bare(const GeometryPtr& g) {
  GeometryPtr n = g->index ? g->toNonIndexed() : g;
  std::vector<std::string> names;
  for (const auto& entry : n->attributes) names.push_back(entry.first);
  for (const std::string& name : names)
    if (name != "position" && name != "normal") n->deleteAttribute(name);
  return n;
}

/** A box; rotation as yaw (ry), then pitch (rx), then roll (rz). */
GeometryPtr box(double w, double h, double d, double x, double y, double z, double rx = 0, double ry = 0, double rz = 0) {
  GeometryPtr g = boxGeometry(w, h, d);
  if (rx != 0 || ry != 0 || rz != 0) g->applyMatrix4(Matrix4().makeRotationFromEuler(Euler(rx, ry, rz, Euler::YXZ)));
  g->translate(x, y, z);
  return bare(g);
}

GeometryPtr merge(const std::vector<GeometryPtr>& parts) { return mergeGeometries(parts, false); }

/** Outline of a rectangle with rounded corners, counter-clockwise: [u, v] pairs. */
std::vector<UV> roundRect(double w, double h, double r, int seg = 2) {
  std::vector<UV> pts;
  for (const auto& [cx, cy, a0] : Rows<3>{
           {w / 2 - r, -h / 2 + r, -90},
           {w / 2 - r, h / 2 - r, 0},
           {-w / 2 + r, h / 2 - r, 90},
           {-w / 2 + r, -h / 2 + r, 180},
       }) {
    for (int i = 0; i <= seg; i++) {
      const double a = (a0 + (90.0 * i) / seg) * DEG;
      pts.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r});
    }
  }
  return pts;
}

/** Outline with soft corners that may be flatter below than above (a superellipse): [u, v] pairs. */
std::vector<UV> softRing(double halfWidth, double top, double bottom, int n = 24) {
  std::vector<UV> pts;
  for (int i = 0; i < n; i++) {
    const double phi = (static_cast<double>(i) / n) * PI * 2;
    const double c = std::cos(phi);
    const double s = std::sin(phi);
    pts.push_back({halfWidth * jsSign(c) * std::sqrt(std::fabs(c)), (s >= 0 ? top : bottom) * jsSign(s) * std::sqrt(std::fabs(s))});
  }
  return pts;
}

/** Put a flat outline into space: origin + u·right + v·up. */
std::vector<Vec3> place(const std::vector<UV>& outline, const Vec3& o, const Vec3& right, const Vec3& up) {
  std::vector<Vec3> out;
  out.reserve(outline.size());
  for (const auto& [u, v] : outline) out.push_back({o[0] + right[0] * u + up[0] * v, o[1] + right[1] * u + up[1] * v, o[2] + right[2] * u + up[2] * v});
  return out;
}

Vec3 along(const Vec3& o, const Vec3& dir, double s) { return {o[0] + dir[0] * s, o[1] + dir[1] * s, o[2] + dir[2] * s}; }

const Vec3 X = {1, 0, 0};
const Vec3 Y = {0, 1, 0};
const Vec3 Z = {0, 0, 1};

TexturePtr plateTexture(const std::string& text) {
  const Canvas c(520, 110);
  const Ctx2D x = c.getContext2D();
  x.fillStyle("#f4f4f0");
  x.fillRect(0, 0, 520, 110);
  x.strokeStyle("#111");
  x.lineWidth(6);
  x.strokeRect(3, 3, 514, 104);
  x.fillStyle("#1b3fa0");
  x.fillRect(6, 6, 44, 98);
  x.fillStyle("#111");
  x.font("700 78px \"DIN Alternate\", \"Bahnschrift\", \"Arial Narrow\", Arial, sans-serif");
  x.textAlign("center");
  x.textBaseline("middle");
  x.fillText(text, 285, 59, 450);
  TexturePtr t = canvasTexture(c);
  t->srgb = true;
  t->anisotropy = 4;
  return t;
}

TexturePtr roofSignTexture() {
  const Canvas c(512, 96);
  const Ctx2D x = c.getContext2D();
  x.fillStyle("#f7f7f4");
  x.fillRect(0, 0, 512, 96);
  x.fillStyle("#1454b8");
  x.fillRect(0, 0, 512, 12);
  x.fillRect(0, 84, 512, 12);
  x.font("800 54px \"DIN Alternate\", \"Bahnschrift\", \"Arial Narrow\", Arial, sans-serif");
  x.textAlign("center");
  x.textBaseline("middle");
  x.fillText("FAHRSCHULE", 256, 50, 480);
  TexturePtr t = canvasTexture(c);
  t->srgb = true;
  t->anisotropy = 4;
  return t;
}

/** Width : height of the lettering strip (and of its texture). */
const int LETTERING_ASPECT = 8;

/**
 * The lettering as a mask: white letters on black, used as the alpha of a
 * plain-coloured strip.  The letters fill the strip's height; a bold face is
 * narrowed just enough to fit its length, which makes them as tall – and as
 * readable from behind – as the width of a bumper allows.
 */
TexturePtr letteringTexture(const std::string& text) {
  const int height = 256;
  const int width = height * LETTERING_ASPECT;
  const Canvas c(width, height);
  const Ctx2D x = c.getContext2D();
  x.fillStyle("#000");
  x.fillRect(0, 0, width, height);
  const int size = 200;
  x.font("800 " + std::to_string(size) + "px \"Helvetica Neue\", Helvetica, Arial, \"Liberation Sans\", \"Noto Sans\", \"DejaVu Sans\", sans-serif");
  x.textAlign("left");
  x.textBaseline("alphabetic");
  const TextMetrics m = x.measureText(text);
  // the inked box of this very text (no descenders in a web address like ours)
  // (the original falls back to the width and to a share of the size where a browser does not measure the box: here it always is)
  const double left = m.actualBoundingBoxLeft;
  const double right = m.actualBoundingBoxRight;
  const double up = m.actualBoundingBoxAscent;
  const double down = m.actualBoundingBoxDescent;
  const double pad = 14;
  const double sy = (height - 2 * pad) / jsMax(1, up + down);
  // never stretched wider than drawn, only narrowed
  const double sx = jsMin(sy, (width - 2 * pad) / jsMax(1, left + right));
  x.setTransform(sx, 0, 0, sy, (width - (left + right) * sx) / 2 + left * sx, pad + up * sy);
  x.fillStyle("#fff");
  x.fillText(text, 0, 0);
  x.setTransform(1, 0, 0, 1, 0, 0);
  TexturePtr t = canvasTexture(c);
  // mip-maps and anisotropic filtering keep the strokes from breaking up at a distance and at a slant
  t->generateMipmaps = true;
  t->minFilter = Filter::LinearMipmapLinear;
  t->anisotropy = 8;
  return t;
}

/** Picture on the centre display: a stylised map with the route, and a row of tiles. */
TexturePtr screenTexture() {
  const Canvas c(512, 288);
  const Ctx2D x = c.getContext2D();
  x.fillStyle("#0b131c");
  x.fillRect(0, 0, 512, 288);
  x.lineCap("round");
  x.lineJoin("round");
  using Pts = std::vector<UV>;
  const auto path = [&x](const Pts& pts, const std::string& colour, double width) {
    x.strokeStyle(colour);
    x.lineWidth(width);
    x.beginPath();
    for (size_t i = 0; i < pts.size(); i++) {
      if (i == 0) x.moveTo(pts[i][0], pts[i][1]);
      else x.lineTo(pts[i][0], pts[i][1]);
    }
    x.stroke();
  };
  // blocks of a town, streets between them
  x.fillStyle("#121d29");
  x.fillRect(0, 0, 352, 288);
  for (const Pts& s : {
           Pts{{-10, 78}, {150, 64}, {362, 40}},
           Pts{{-10, 196}, {176, 178}, {362, 150}},
           Pts{{58, -10}, {84, 120}, {100, 300}},
           Pts{{236, -10}, {246, 150}, {262, 300}},
           Pts{{150, 64}, {176, 178}},
       })
    path(s, "#2c3d50", 15);
  path(Pts{{92, 300}, {86, 186}, {176, 178}, {242, 168}, {238, 40}, {236, -10}}, "#3d8bff", 8);
  x.fillStyle("#eef2f5");
  x.beginPath();
  x.moveTo(88, 206);
  x.lineTo(104, 244);
  x.lineTo(89, 236);
  x.lineTo(73, 244);
  x.closePath();
  x.fill();
  // tiles at the side
  x.fillStyle("#0e1822");
  x.fillRect(352, 0, 160, 288);
  int i = 0;
  for (const char* colour : {"#3d8bff", "#35c06a", "#ffb020", "#9aa3ab"}) {
    const double tx = 372 + (i % 2) * 64;
    const double ty = 28 + (i / 2) * 64;
    x.fillStyle("#1a2836");
    x.fillRect(tx, ty, 52, 52);
    x.fillStyle(colour);
    x.beginPath();
    x.arc(tx + 26, ty + 26, 11, 0, PI * 2);
    x.fill();
    i++;
  }
  x.fillStyle("#1a2836");
  x.fillRect(372, 172, 116, 22);
  x.fillRect(372, 204, 116, 22);
  x.fillStyle("#8f9aa5");
  x.fillRect(380, 180, 62, 6);
  x.fillRect(380, 212, 84, 6);
  TexturePtr t = canvasTexture(c);
  t->srgb = true;
  t->anisotropy = 4;
  return t;
}

/** the shells that were built, by their measures (they live as long as the program: cars come and go, their shape stays) */
const CarShell& shellFor(const CarDims& d) {
  static std::map<std::string, std::unique_ptr<CarShell>>* SHELL_CACHE = new std::map<std::string, std::unique_ptr<CarShell>>();
  // (as JSON.stringify(d) names them: every measure, in their order)
  std::string key;
  for (const double v : {d.length, d.width, d.height, d.wheelbase, d.track, d.wheelRadius, d.tyreWidth, d.frontOverhang, static_cast<double>(d.tail)}) key += jsStr(v) + ",";
  std::unique_ptr<CarShell>& s = (*SHELL_CACHE)[key];
  if (!s) s = std::make_unique<CarShell>(buildCarShell(d));
  return *s;
}

/** Half width of a bumper face (outline from the shell, bottom → top) at height y. */
double halfWidthAt(const std::vector<UV>& outline, double y) {
  double w = JS_INF;
  for (size_t i = 0; i + 1 < outline.size(); i++) {
    const auto& [x0, y0] = outline[i];
    const auto& [x1, y1] = outline[i + 1];
    if (y0 == y1 || y < jsMin(y0, y1) || y > jsMax(y0, y1)) continue;
    w = jsMin(w, x0 + ((x1 - x0) * (y - y0)) / (y1 - y0));
  }
  return w == JS_INF ? 0 : w;
}

/** the materials all cars share (made with the first car: they live as long as the program) */
struct SharedMaterials {
  MaterialPtr trim;
  MaterialPtr tyre;
  MaterialPtr rim;
  MaterialPtr glassDark;
};
const SharedMaterials& MAT() {
  static const SharedMaterials* m = new SharedMaterials{
      lambert({.color = Color(0x17191b)}),
      lambert({.color = Color(0x151617)}),
      phong({.color = Color(0xb9bdc2), .specular = Color(0x555555), .shininess = 60}),
      phong({.color = Color(0x0c1418), .specular = Color(0x9fb4c4), .shininess = 140}),
  };
  return *m;
}

}  // namespace

CarExterior::CarExterior(int color, const CarExteriorOptions& opts) : dims(opts.dims ? *opts.dims : COMPACT_DIMS), shell(shellFor(dims)) {
  const CarDims& d = dims;
  const CarShell& sh = shell;
  const MaterialPtr trim = opts.ownMaterials ? MAT().trim->clone() : MAT().trim;
  paint = phong({.color = Color(color), .specular = Color(0x777777), .shininess = 90});
  // Shadows: the shell is not a closed solid (the windows are openings, the floor is a part
  // of its own), so both of its sides throw shadow – otherwise the roof, which faces the sun,
  // would throw none.  Nothing on the car receives shadow: at the scale of a shadow map a
  // car shadowing itself, inside or out, only looks blotchy.
  paint->shadowSide = Side::Double;
  glass = opts.seeThroughGlass
              ? phong({.color = Color(0x9fc0d0), .transparent = true, .opacity = 0.16, .depthWrite = false, .specular = Color(0xffffff), .shininess = 160})
              : MAT().glassDark;
  Group& g = *group;
  const auto solid = [this, &g](const GeometryPtr& geo, const MaterialPtr& mat) {
    std::shared_ptr<Mesh> m = make<Mesh>(geo, mat);
    solids.push_back(m);
    g.add(m);
    return m;
  };
  solid(sh.paint, paint)->castShadow = true;
  const std::shared_ptr<Mesh> glassMesh = make<Mesh>(sh.glass, glass);
  glassMesh->renderOrder = 5;
  g.add(glassMesh);
  solid(sh.trim, trim)->castShadow = true;
  solid(sh.pillars, paint)->castShadow = true;

  const WheelGeometry wheelGeo = buildWheel(d);
  const double xs[4] = {-d.track / 2, d.track / 2, -d.track / 2, d.track / 2};
  const double zs[4] = {sh.zAxleF, sh.zAxleF, sh.zAxleR, sh.zAxleR};
  for (int i = 0; i < 4; i++) {
    const std::shared_ptr<Group> steer = make<Group>();
    const std::shared_ptr<Group> spin = make<Group>();
    spin->add(make<Mesh>(wheelGeo.tyre, MAT().tyre), make<Mesh>(wheelGeo.rim, MAT().rim));
    steer->add(spin);
    steer->position.set(xs[i], d.wheelRadius, zs[i]);
    g.add(steer);
    wheels.push_back(steer);
    wheelSpinners.push_back(spin);
  }

  // lamps
  head = basic({.color = Color(0xdfe6ea)});
  tail = basic({.color = Color(0x5a0d0d)});
  reverse = basic({.color = Color(0x777777)});
  blinkL = basic({.color = Color(0x6a3a08)});
  blinkR = basic({.color = Color(0x6a3a08)});
  const double hw = sh.halfWidth;
  std::vector<std::pair<GeometryPtr, MaterialPtr>> lamps;
  for (const double side : {-1.0, 1.0}) {
    lamps.push_back({box(0.34, 0.1, 0.05, side * hw * 0.62, 0.635, sh.zFront + 0.115, 0, side * -0.35), head});
    lamps.push_back({box(0.1, 0.07, 0.05, side * hw * 0.86, 0.6, sh.zFront + 0.2, 0, side * -0.8), side < 0 ? blinkL : blinkR});
    lamps.push_back({box(0.3, 0.13, 0.05, side * hw * 0.7, 0.9, sh.zRear - 0.115, 0, side * 0.3), tail});
    lamps.push_back({box(0.1, 0.09, 0.05, side * hw * 0.88, 0.9, sh.zRear - 0.19, 0, side * 0.7), side < 0 ? blinkL : blinkR});
    lamps.push_back({box(0.12, 0.05, 0.04, side * hw * 0.45, 0.86, sh.zRear - 0.06), reverse});
    // mirror housings (the player's car builds its own, aligned with the working glass)
    if (!opts.noMirrorHousings) g.add(make<Mesh>(box(0.2, 0.13, 0.09, side * (hw + 0.1), sh.beltY + 0.06, sh.zCowl + 0.4), paint));
  }
  for (const auto& [geo, mat] : lamps) lampMeshes.push_back(solid(geo, mat));
  // grille and number plates
  solid(box(0.86, 0.14, 0.04, 0, 0.5, sh.zFront + 0.05), trim);
  if (!opts.plate.empty()) {
    const TexturePtr tex = plateTexture(opts.plate);
    const MaterialPtr pm = basic({.map = tex});
    const std::shared_ptr<Mesh> front = make<Mesh>(planeGeometry(0.52, 0.11), pm);
    front->position.set(0, 0.36, sh.zFront - 0.012);
    front->rotation.y = PI;
    const std::shared_ptr<Mesh> rear = make<Mesh>(planeGeometry(0.52, 0.11), pm);
    rear->position.set(0, 0.66, sh.zRear + 0.012);
    g.add(front, rear);
  }
  if (!opts.lettering.empty()) addLettering(opts.lettering, color);
  if (opts.roofSign) {
    const TexturePtr tex = roofSignTexture();
    const MaterialPtr sm = basic({.map = tex});
    solid(box(0.86, 0.16, 0.2, 0, sh.roofY + 0.085, 0.25), lambert({.color = Color(0xf2f2ee)}));
    const std::shared_ptr<Mesh> f = solid(planeGeometry(0.84, 0.15), sm);
    f->position.set(0, sh.roofY + 0.085, 0.25 - 0.101);
    f->rotation.y = PI;
    const std::shared_ptr<Mesh> r = solid(planeGeometry(0.84, 0.15), sm);
    r->position.set(0, sh.roofY + 0.085, 0.25 + 0.101);
  }
}

void CarExterior::addLettering(const std::string& text, int paint_) {
  const CarShell& sh = shell;
  const Color c(paint_);
  // dark letters on light paint, light ones on dark paint
  const bool light = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b > 0.18;
  const MaterialPtr ink = lambert({
      .color = Color(light ? 0x14171b : 0xf2f3f0),
      .transparent = true,
      .depthWrite = false,
      // the strip lies a few millimetres off the paint; the bias keeps it from flickering far away
      .polygonOffset = true,
      .polygonOffsetFactor = -2,
      .polygonOffsetUnits = -4,
  });
  ink->alphaMap = letteringTexture(text);
  const auto strip = [this, &ink](const std::vector<UV>& outline, double yLow, double yHigh, double z, double facing) {
    // as large as fits between yLow and yHigh without leaving the flat face
    double h = yHigh - yLow;
    for (int i = 0; i < 4; i++) h = jsMin(h, (2 * jsMin(halfWidthAt(outline, yLow), halfWidthAt(outline, yLow + h)) - 0.03) / LETTERING_ASPECT);
    const std::shared_ptr<Mesh> m = make<Mesh>(planeGeometry(h * LETTERING_ASPECT, h), ink);
    m->position.set(0, (yLow + yHigh) / 2, z + facing * 0.006);
    if (facing < 0) m->rotation.y = PI;
    m->name = facing < 0 ? "lettering front" : "lettering rear";
    // after the body (which is drawn late while the cabin is see-through), before the glass
    m->renderOrder = 4;
    group->add(m);
  };
  strip(sh.noseOutline, 0.455, 0.625, sh.zFront, -1);
  strip(sh.tailOutline, 0.435, 0.595, sh.zRear, 1);
}

void CarExterior::setWheels(const std::vector<double>& steer, const std::vector<double>& spin, const std::vector<double>& y) {
  for (size_t i = 0; i < 4; i++) {
    // + steer = right = negative rotation about +Y
    wheels[i]->rotation.y = i < 2 ? -steer[i] : 0;
    wheels[i]->position.y = y[i];
    // rolling forward (-Z) turns the top of the wheel forward: negative rotation about +X
    wheelSpinners[i]->rotation.x = -spin[i];
  }
}

void CarExterior::setLamps(const Lamps& o) {
  head->color.set(o.high ? 0xffffff : o.low ? 0xf4f6f0 : 0xcfd6da);
  tail->color.set(o.brake ? 0xff2a1e : o.low ? 0xa01410 : 0x5a0d0d);
  reverse->color.set(o.reverse ? 0xffffff : 0x777777);
  blinkL->color.set(o.left ? 0xffa010 : 0x6a3a08);
  blinkR->color.set(o.right ? 0xffa010 : 0x6a3a08);
}

// ---------------------------------------------------------------- cockpit ----

MaterialPtr cabinMaterial(int hex, double shine) {
  const Color c(hex);
  const Color color = c.clone().multiplyScalar(0.6);
  const Color emissive = c.clone().multiplyScalar(0.42);
  return shine > 0 ? phong({.color = color, .emissive = emissive, .specular = Color(hex).multiplyScalar(0.35), .shininess = shine})
                   : lambert({.color = color, .emissive = emissive});
}

namespace {

/** Geometry of the fixed cabin parts, collected per material. */
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
/** `Object.keys(kit)`: the kinds in their order */
constexpr std::vector<GeometryPtr> Kit::*KIT_KEYS[] = {&Kit::soft, &Kit::hard, &Kit::pale, &Kit::fabric, &Kit::accent, &Kit::carpet, &Kit::silver, &Kit::black};

/** the cabin's materials, one for each kind of the kit */
struct KitMaterials {
  MaterialPtr soft;
  MaterialPtr hard;
  MaterialPtr pale;
  MaterialPtr fabric;
  MaterialPtr accent;
  MaterialPtr carpet;
  MaterialPtr silver;
  MaterialPtr black;
};
constexpr MaterialPtr KitMaterials::*MATS_KEYS[] = {&KitMaterials::soft,   &KitMaterials::hard,   &KitMaterials::pale,   &KitMaterials::fabric,
                                                    &KitMaterials::accent, &KitMaterials::carpet, &KitMaterials::silver, &KitMaterials::black};

/** Where the instrument cluster sits, shared by the hood built around it and the picture itself. */
struct ClusterPlace {
  Vec3 centre;
  /** backward tilt of the face in rad */
  double tilt;
  double width;
  double height;
};

/** what buildDashboard answers: where the brow of the dashboard runs */
struct Dashboard {
  std::function<double(double x)> browZ;
  std::function<double(double x)> browY;
};

/**
 * Dashboard: a side profile that changes across the car (deeper at the doors,
 * crowned like the scuttle), with a padded top, a bright trim strip under its
 * brow and a hard lower part; then the glove box, the air vents and the centre
 * stack with its display.
 */
Dashboard buildDashboard(const CarShell& sh, Kit& kit) {
  const double hw = sh.halfWidth;
  const double zc = sh.zCowl;
  // just inside the side glass
  const double xe = hw * 0.975 - 0.022;
  // lower edge of the windscreen across the car
  const auto cowlAt = [&sh, hw](double x) { return sh.cowlY + (sh.cowlCrownY - sh.cowlY) * (1 - std::fabs(x) / (hw * 0.9)); };
  // the brow: the edge of the top towards the occupants.  It stays below the
  // base of the windscreen as the driver sees it, so the top hides no road.
  const double beltY = sh.beltY;
  const auto browZ = [zc, xe](double x) { return zc + 0.385 + 0.05 * std::pow(std::fabs(x / xe), 3); };
  const auto browY = [beltY, xe](double x) { return beltY + 0.002 - 0.012 * std::pow(x / xe, 2); };
  std::vector<std::vector<Vec3>> top;
  std::vector<std::vector<Vec3>> band;
  std::vector<std::vector<Vec3>> lower;
  const int n = 14;
  for (int i = 0; i <= n; i++) {
    const double x = -xe + (2 * xe * i) / n;
    const double zb = browZ(x);
    const double yb = browY(x);
    const double zf = zc + 0.004;
    const double yf = cowlAt(x) - 0.014;
    const auto p = [x](double z, double y) -> Vec3 { return {x, y, z}; };
    std::vector<Vec3> t;
    for (const double s : {0.0, 0.33, 0.66, 1.0}) t.push_back(p(zf + (zb - 0.032 - zf) * s, yf + (yb - yf) * s + 0.006 * std::sin(PI * s)));
    // rounded brow with a lip, the strip set back under it
    t.insert(t.end(), {p(zb - 0.012, yb - 0.003), p(zb - 0.002, yb - 0.013), p(zb, yb - 0.027), p(zb - 0.008, yb - 0.04), p(zb - 0.02, yb - 0.045)});
    top.push_back(t);
    band.push_back({p(zb - 0.02, yb - 0.045), p(zb - 0.018, yb - 0.074)});
    lower.push_back({
        p(zb - 0.018, yb - 0.074),
        p(zb - 0.004, yb - 0.082),
        p(zb + 0.004, yb - 0.1),
        p(zb + 0.002, yb - 0.2),
        p(zb - 0.02, yb - 0.265),
        p(zb - 0.08, yb - 0.31),
        p(zc + 0.02, yb - 0.33),
    });
  }
  kit.soft.push_back(Sculpt().sheet(top, {0, 1, 0.4}).build(50));
  kit.silver.push_back(Sculpt().sheet(band, Z).build());
  Sculpt low;
  low.sheet(lower, {0, -0.3, 1});
  // closed ends towards the doors
  for (const int i : {0, n}) {
    std::vector<Vec3> end = top[static_cast<size_t>(i)];
    end.insert(end.end(), lower[static_cast<size_t>(i)].begin(), lower[static_cast<size_t>(i)].end());
    low.fan(end, {i == 0 ? -1.0 : 1.0, 0, 0});
  }
  kit.hard.push_back(low.build(50));

  // glove box lid on the passenger's side, standing a little proud, with its handle
  std::vector<std::vector<Vec3>> lid;
  for (int i = 0; i <= 4; i++) {
    const double x = 0.25 + 0.125 * i;
    const double zb = browZ(x);
    const double yb = browY(x);
    lid.push_back({
        {x, yb - 0.108, zb + 0.009},
        {x, yb - 0.196, zb + 0.008},
        {x, yb - 0.256, zb - 0.01},
    });
  }
  kit.soft.push_back(Sculpt().sheet(lid, {0, -0.2, 1}).build(60));
  kit.silver.push_back(box(0.09, 0.012, 0.01, 0.5, browY(0.5) - 0.122, browZ(0.5) + 0.012));

  // round air vents at both ends, turned a little towards the middle of the cabin
  for (const double side : {-1.0, 1.0}) {
    const double x = side * (xe - 0.115);
    const Vec3 o = {x, browY(x) - 0.066, browZ(x) - 0.008};
    const double yaw = -side * 0.3;
    const Vec3 out = {std::sin(yaw), 0, std::cos(yaw)};
    const Vec3 right = {std::cos(yaw), 0, -std::sin(yaw)};
    const auto circle = [&](double r, double s) {
      std::vector<Vec3> ring;
      for (int i = 0; i < 14; i++) {
        const double a = (i / 14.0) * PI * 2;
        ring.push_back(along(along(along(o, out, s), right, std::cos(a) * r), Y, std::sin(a) * r));
      }
      return ring;
    };
    kit.silver.push_back(Sculpt().tube({circle(0.047, -0.01), circle(0.047, 0.016), circle(0.04, 0.019)}).build(50));
    kit.black.push_back(Sculpt().tube({circle(0.04, 0.019), circle(0.038, 0.002)}, false, false, true).fan(circle(0.038, 0.002), out).build(50));
    for (const double dy : {-0.018, 0.0, 0.018})
      kit.hard.push_back(box(std::sqrt(std::pow(0.037, 2) - std::pow(dy, 2)) * 2, 0.004, 0.012, o[0] + out[0] * 0.01, o[1] + dy, o[2] + out[2] * 0.01, 0.25, yaw));
    kit.silver.push_back(box(0.008, 0.012, 0.008, o[0] + out[0] * 0.018, o[1], o[2] + out[2] * 0.018, 0, yaw));
  }

  // ---- centre stack: display on top, two vents, heating controls ------------
  const double zs = browZ(0);
  const double ys = browY(0);
  std::vector<std::vector<Vec3>> stack;
  // rings from the top down; the face leans back, the foot widens into the console
  for (const auto& [y, w, zFace, depth] : Rows<4>{
           {ys - 0.052, 0.3, zs + 0.012, 0.11},
           {ys - 0.075, 0.32, zs + 0.03, 0.14},
           {0.72, 0.31, zs + 0.052, 0.17},
           {0.6, 0.25, zs + 0.085, 0.2},
           {0.5, 0.215, zs + 0.13, 0.24},
       })
    stack.push_back(place(roundRect(w, depth, 0.028), {0, y, zFace - depth / 2}, X, {0, 0, -1}));
  kit.hard.push_back(Sculpt().tube(stack, true).build(50));
  // the face of the stack is a plane through its upper rings: things on it share its lean
  const double lean = std::atan2(0.052 - 0.03, ys - 0.075 - 0.72);
  const auto onFace = [zs, ys, lean](double y) { return zs + 0.03 + (ys - 0.075 - y) * std::tan(lean); };
  for (const double side : {-1.0, 1.0}) {
    const double y = ys - 0.118;
    kit.black.push_back(box(0.118, 0.04, 0.012, side * 0.072, y, onFace(y) - 0.002, -lean));
    for (const double dy : {-0.009, 0.009}) kit.hard.push_back(box(0.11, 0.004, 0.014, side * 0.072, y + dy, onFace(y + dy) + 0.002, -lean - 0.3));
    kit.silver.push_back(box(0.01, 0.022, 0.008, side * 0.072, y, onFace(y) + 0.008, -lean));
  }
  const double yk = ys - 0.185;
  kit.black.push_back(box(0.27, 0.058, 0.01, 0, yk, onFace(yk) - 0.001, -lean));
  for (const double kx : {-0.09, 0.0, 0.09}) {
    const GeometryPtr ring = cylinderGeometry(0.02, 0.022, 0.022, 14, 1, true);
    ring->rotateX(PI / 2 - lean).translate(kx, yk, onFace(yk) + 0.014);
    kit.silver.push_back(bare(ring));
    const GeometryPtr cap = circleGeometry(0.02, 14);
    cap->rotateX(-lean).translate(kx, yk + 0.011 * std::sin(lean), onFace(yk) + 0.014 + 0.011 * std::cos(lean));
    kit.black.push_back(bare(cap));
  }
  return {browZ, browY};
}

/**
 * Instrument hood: a pod around the cluster with a rounded opening towards the
 * driver, running out forwards into the top of the dashboard.
 */
void buildBinnacle(Kit& kit, const ClusterPlace& k, double dashTopY) {
  const Vec3 up = {0, std::cos(k.tilt), -std::sin(k.tilt)};
  const Vec3 out = {0, std::sin(k.tilt), std::cos(k.tilt)};
  const double hw = k.width / 2;
  const double hh = k.height / 2;
  // rings around the axis of the instrument face; their widest line lies below its middle
  const auto ring = [&](double s, double grow) {
    return place(softRing(hw + 0.02 + grow, hh + 0.042 + grow, hh - 0.014 + grow), along(along(k.centre, out, s), up, -0.02), X, up);
  };
  const double depth = 0.085;
  // outside: from inside the dashboard back to the rim, rounded over into the opening
  kit.soft.push_back(Sculpt().tube({ring(-0.1, 0.026), ring(0, 0.02), ring(depth, 0.014), ring(depth + 0.006, 0.007)}).build(60));
  // inside: a dark tunnel down to the instrument face
  kit.black.push_back(Sculpt().tube({ring(depth + 0.006, 0.007), ring(depth, 0), ring(-0.004, -0.004)}, false, false, true).fan(ring(-0.004, -0.004), out).build(60));
  // the hood runs out forwards and downwards into the dashboard top
  const std::vector<Vec3> full = ring(0, 0.02);
  std::vector<std::vector<Vec3>> rows = {std::vector<Vec3>(full.begin(), full.begin() + 13)};
  for (const auto& [dz, half, rise] : Rows<3>{
           {0.15, hw + 0.05, 0.03},
           {0.3, hw + 0.01, -0.02},
       }) {
    std::vector<Vec3> row;
    for (int i = 0; i <= 12; i++) {
      const double phi = (i / 12.0) * PI;
      const double c = std::cos(phi);
      row.push_back({k.centre[0] + half * jsSign(c) * std::sqrt(std::fabs(c)), dashTopY - 0.03 + (rise + 0.03) * std::sqrt(std::sin(phi)), k.centre[2] - dz});
    }
    rows.push_back(row);
  }
  kit.soft.push_back(Sculpt().sheet(rows, {0, 1, 0.2}).build(60));
}

/** Tunnel console between the seats: tray, gear lever frame, cup holders, padded armrest. */
void buildConsole(Kit& kit, double zStack) {
  // top line from the foot of the centre stack back to between the rear footwells
  std::vector<std::vector<Vec3>> rings;
  for (const auto& [z, top, w] : Rows<3>{
           {zStack + 0.04, 0.5, 0.2},
           {zStack + 0.2, 0.5, 0.2},
           {-0.02, 0.505, 0.2},
           {0.2, 0.51, 0.2},
           {0.27, 0.56, 0.21},
           {0.33, 0.585, 0.21},
           {0.7, 0.585, 0.21},
           {0.76, 0.52, 0.2},
           {0.8, 0.3, 0.19},
       }) {
    const double h = top - 0.22;
    rings.push_back(place(roundRect(w, h, 0.03), {0, 0.22 + h / 2, z}, X, Y));
  }
  kit.hard.push_back(Sculpt().tube(rings, false, true).build(50));
  // gear lever surround and two cup holders
  kit.silver.push_back(
      Sculpt().tube({place(roundRect(0.13, 0.16, 0.03), {0, 0.503, -0.12}, X, {0, 0, -1}), place(roundRect(0.12, 0.15, 0.028), {0, 0.509, -0.12}, X, {0, 0, -1})}, false, true).build(50));
  kit.black.push_back(Sculpt().fan(place(roundRect(0.098, 0.128, 0.022), {0, 0.5095, -0.12}, X, {0, 0, -1}), Y).build());
  for (const double z : {0.06, 0.155}) {
    std::vector<Vec3> disc;
    std::vector<Vec3> rim;
    for (int i = 0; i < 14; i++) {
      const double a = (i / 14.0) * PI * 2;
      disc.push_back({-0.042 + std::cos(a) * 0.036, 0.5115, z - std::sin(a) * 0.036});
      rim.push_back({-0.042 + std::cos(a) * 0.041, 0.511, z - std::sin(a) * 0.041});
    }
    kit.black.push_back(Sculpt().fan(disc, Y).build());
    kit.silver.push_back(Sculpt().fan(rim, Y).build());
  }
  // padded lid of the armrest
  std::vector<std::vector<Vec3>> pad;
  int i = 0;
  for (const double z : {0.345, 0.37, 0.67, 0.695}) {
    pad.push_back(place(roundRect(i % 3 == 0 ? 0.18 : 0.2, i % 3 == 0 ? 0.012 : 0.03, 0.006, 1), {0, 0.598, z}, X, Y));
    i++;
  }
  kit.accent.push_back(Sculpt().tube(pad, true, true).build(60));
}

/**
 * Inner door panel between z0 (front) and z1: padded top roll at the window
 * sill, a fabric insert, an armrest with the door pull, the handle and a
 * loudspeaker grille.  The driver's door also carries the window switches.
 */
void buildDoor(const CarShell& sh, Kit& kit, double side, double z0, double z1, bool front) {
  // stations along the door: the front doors are in view all the time, the rear ones hardly ever
  const int n = front ? 8 : 5;
  const double xg = sh.halfWidth * 0.975 - 0.006;
  // the sill follows the lower edge of the glass, which rises from the scuttle to the cabin
  const auto sill = [&sh](double z) {
    return sh.beltY - 0.004 - (sh.beltY - (sh.cowlY - 0.015)) * jsMax(0, jsMin(1, (sh.zRoofFront - z) / (sh.zRoofFront - sh.zCowl)));
  };
  std::vector<std::vector<Vec3>> roll;
  std::vector<std::vector<Vec3>> insert;
  std::vector<std::vector<Vec3>> panel;
  const double armY = 0.735;
  const auto armAt = [z0, z1](double z) {
    // the armrest grows out of the panel behind the door's front edge and fades before its rear edge
    const double u = (z - z0) / (z1 - z0);
    const auto ramp = [u](double a, double b) {
      const double t = jsMax(0, jsMin(1, (u - a) / (b - a)));
      return t * t * (3 - 2 * t);
    };
    return ramp(0.2, 0.36) * (1 - ramp(0.84, 0.97));
  };
  for (int i = 0; i <= n; i++) {
    const double z = z0 + ((z1 - z0) * i) / n;
    const double ys = sill(z);
    const double a = armAt(z);
    const auto p = [side, xg, z](double inward, double y) -> Vec3 { return {side * (xg - inward), y, z}; };
    roll.push_back({p(0, ys), p(0.04, ys + 0.004), p(0.068, ys - 0.016), p(0.076, ys - 0.05), p(0.07, ys - 0.074)});
    insert.push_back({p(0.07, ys - 0.074), p(0.07, armY + 0.05)});
    panel.push_back({
        p(0.07, armY + 0.05),
        p(0.078 + 0.062 * a, armY + 0.028),
        p(0.082 + 0.068 * a, armY),
        p(0.08 + 0.055 * a, armY - 0.045),
        p(0.078, armY - 0.1),
        p(0.074, 0.3),
        p(0.03, 0.24),
    });
  }
  const Vec3 facing = {-side, 0.2, 0};
  kit.soft.push_back(Sculpt().sheet(roll, {-side, 1, 0}).build(60));
  kit.accent.push_back(Sculpt().sheet(insert, facing).build());
  Sculpt body;
  body.sheet(panel, facing);
  // the panel's front and rear edges
  for (const int i : {0, n}) {
    std::vector<Vec3> edge = roll[static_cast<size_t>(i)];
    edge.insert(edge.end(), panel[static_cast<size_t>(i)].begin(), panel[static_cast<size_t>(i)].end());
    body.fan(edge, {0, 0, i == 0 ? -1.0 : 1.0});
  }
  kit.hard.push_back(body.build(50));

  const double len = z1 - z0;
  // release handle in a dark recess, towards the front of the door
  const double zh = z0 + len * (front ? 0.3 : 0.26);
  const double yh = sill(zh) - 0.11;
  kit.black.push_back(box(0.006, 0.05, 0.15, side * (xg - 0.073), yh, zh));
  kit.silver.push_back(box(0.014, 0.022, 0.1, side * (xg - 0.083), yh, zh + 0.012));
  // door pull on the armrest
  const double zp = z0 + len * 0.6;
  kit.black.push_back(box(0.05, 0.008, len * 0.22, side * (xg - 0.118), armY + 0.031, zp, 0, 0, side * 0.12));
  if (front && side < 0) {
    for (const double dz : {-0.03, -0.01, 0.01, 0.03}) kit.silver.push_back(box(0.022, 0.006, 0.012, side * (xg - 0.12), armY + 0.037, zp - len * 0.18 + dz, 0, 0, side * 0.12));
  }
  // loudspeaker grille low in the panel
  const double zl = z0 + len * (front ? 0.24 : 0.3);
  std::vector<Vec3> grille;
  for (int i = 0; i < 12; i++) {
    const double a = (i / 12.0) * PI * 2;
    grille.push_back({side * (xg - 0.079), 0.5 + std::sin(a) * 0.075, zl + std::cos(a) * 0.075});
  }
  kit.black.push_back(Sculpt().fan(grille, {-side, 0, 0}).build());
}

/**
 * A seat: cushion and backrest with raised side bolsters, a head restraint and
 * centre panels in a second fabric.  `width` 0.5 is a single seat, the rear
 * bench is the same shape drawn wide with two head restraints.
 */
void buildSeat(Kit& kit, double x, double zFront, double width, double recline, const std::vector<double>& restraints) {
  const double w = width / 2;
  const double top = 0.462;
  const double zRear = zFront + 0.48;
  // cushion: outline across the car, swept from front to rear
  const auto cushion = [x, w, top](double z, double rise, double scale) {
    const std::vector<UV> o = {
        {-w, -0.12},
        {w, -0.12},
        {w, -0.005},
        {w - 0.035, 0.028},
        {w - 0.095, 0},
        {-(w - 0.095), 0},
        {-(w - 0.035), 0.028},
        {-w, -0.005},
    };
    std::vector<Vec3> ring;
    for (const auto& [u, v] : o) ring.push_back({x + u * scale, top + rise + v * (v > -0.1 ? scale : 1), z});
    return ring;
  };
  kit.fabric.push_back(
      Sculpt().tube({cushion(zFront - 0.02, -0.035, 0.9), cushion(zFront + 0.02, 0.012, 1), cushion(zFront + 0.2, 0.004, 1), cushion(zRear, -0.02, 1)}, true, true).build(55));
  // backrest: outline across the car, swept up along the reclined back
  const Vec3 axis = {0, std::cos(recline), std::sin(recline)};
  const Vec3 fwd = {0, std::sin(recline), -std::cos(recline)};
  const Vec3 foot = {x, top - 0.03, zRear - 0.035};
  const auto back = [&](double s, double scale, double bolster, double thick) {
    const double b = w * scale;
    const std::vector<UV> o = {
        {-b, -thick},
        {b, -thick},
        {b, 0},
        {b - 0.03, 0.035 * bolster},
        {b - 0.09, 0},
        {-(b - 0.09), 0},
        {-(b - 0.03), 0.035 * bolster},
        {-b, 0},
    };
    return place(o, along(foot, axis, s), X, fwd);
  };
  const double height = 0.62;
  kit.fabric.push_back(
      Sculpt().tube({back(0, 0.96, 1, 0.1), back(0.22, 0.98, 1, 0.1), back(0.46, 0.94, 0.7, 0.085), back(height - 0.02, 0.86, 0.2, 0.07), back(height, 0.78, 0, 0.05)}, false, true).build(55));
  // centre panels, a few millimetres proud of the hollow between the bolsters
  const double half = w - 0.105;
  Sculpt panels;
  {
    const double zAt[3] = {zFront + 0.05, zFront + 0.2, zRear - 0.05};
    const double riseAt[3] = {0.014, 0.008, -0.006};
    std::vector<std::vector<Vec3>> rows;
    for (int i = 0; i < 3; i++) rows.push_back({{x - half, top + riseAt[i], zAt[i]}, {x + half, top + riseAt[i], zAt[i]}});
    panels.sheet(rows, Y);
  }
  {
    std::vector<std::vector<Vec3>> rows;
    for (const double s : {0.07, 0.3, 0.52}) {
      const Vec3 o = along(along(foot, axis, s), fwd, 0.004);
      const double b = w * 0.95 - 0.1;
      rows.push_back({{o[0] - b, o[1], o[2]}, {o[0] + b, o[1], o[2]}});
    }
    panels.sheet(rows, fwd);
  }
  kit.accent.push_back(panels.build());
  // head restraints on the line of the backrest
  for (const double rx : restraints) {
    std::vector<std::vector<Vec3>> rings;
    for (const auto& [s, k] : Rows<2>{
             {height - 0.005, 0.8},
             {height + 0.03, 1},
             {height + 0.13, 1},
             {height + 0.165, 0.8},
         })
      rings.push_back(place(roundRect(0.25 * k, 0.085 * k, 0.03 * k, 1), along(along({x + rx, foot[1], foot[2]}, axis, s), fwd, -0.03), X, fwd));
    kit.fabric.push_back(Sculpt().tube(rings, true, true).build(60));
  }
}

}  // namespace

PlayerCar::PlayerCar()
    : exterior(0xc9cdd2,
               CarExteriorOptions{
                   .plate = "KS FS 2026",
                   .seeThroughGlass = true,
                   .roofSign = true,
                   .noMirrorHousings = true,
                   .lettering = "maureranton.com",
                   .ownMaterials = true,
               }),
      depthOnly_(basic({
          // sorted with the see-through parts, drawn just before them
          .transparent = true,
          // A hair behind the surface it belongs to (micrometres at arm's length), so that surface
          // passes the depth test even where a graphics chip rounds two shaders' depths differently.
          .polygonOffset = true,
          .polygonOffsetFactor = 0,
          .polygonOffsetUnits = 6,
          .colorWrite = false,
      })) {
  const CarShell& sh = exterior.shell;
  root->add(exterior.group, cockpit);
  const double hw = sh.halfWidth;
  const double belt = sh.beltY;
  const double zc = sh.zCowl;
  const double ex = eye.x;

  const KitMaterials mats = {
      .soft = cabinMaterial(0x40444b),
      .hard = cabinMaterial(0x575d65),
      .pale = cabinMaterial(0xb9bbb8),
      .fabric = cabinMaterial(0x4a5466),
      .accent = cabinMaterial(0x77849a),
      .carpet = cabinMaterial(0x2c2e33),
      .silver = cabinMaterial(0xb9bec4, 50),
      .black = cabinMaterial(0x1a1c20),
  };
  const MaterialPtr leather = cabinMaterial(0x2a2c31, 18);
  Kit kit;
  const auto part = [this](const GeometryPtr& geo, const MaterialPtr& mat, int layer = 0, Object3D* parent = nullptr) {
    std::shared_ptr<Mesh> m = make<Mesh>(geo, mat);
    (parent ? *parent : *cockpit).add(m);
    ghost(m, layer);
    return m;
  };

  // ---- shell seen from inside ---------------------------------------------
  part(sh.lining, mats.carpet);
  part(sh.headliner, mats.pale);
  part(sh.pillarTrim, mats.pale);

  // ---- dashboard, instrument hood, console, doors, seats -------------------
  const Dashboard dash = buildDashboard(sh, kit);
  // The cluster stands where the driver sees it through the upper half of the
  // steering wheel: its top just under the rim, its foot just above the hub.
  const ClusterPlace cluster = {{ex, 0.905, zc + 0.435}, 0.32, 0.36, 0.12};
  buildBinnacle(kit, cluster, dash.browY(ex));
  buildConsole(kit, dash.browZ(0));
  // (each door's panel kept apart as well, for the doors that open)
  std::vector<Kit> doorKits(4);
  buildDoor(sh, doorKits[0], -1, zc + 0.1, sh.zPillarB - 0.04, true);
  buildDoor(sh, doorKits[1], 1, zc + 0.1, sh.zPillarB - 0.04, true);
  buildDoor(sh, doorKits[2], -1, sh.zPillarB + 0.04, sh.zRoofRear - 0.15, false);
  buildDoor(sh, doorKits[3], 1, sh.zPillarB + 0.04, sh.zRoofRear - 0.15, false);
  buildSeat(kit, ex, -0.15, 0.5, 0.3, {0});
  buildSeat(kit, -ex, -0.15, 0.5, 0.3, {0});
  buildSeat(kit, 0, 0.86, 1.3, 0.36, {-0.36, 0.36});
  // parcel shelf behind the rear seat
  kit.carpet.push_back(Sculpt()
                           .sheet(
                               {
                                   {
                                       {-hw + 0.1, 0.985, 1.49},
                                       {hw - 0.1, 0.985, 1.49},
                                   },
                                   {
                                       {-hw + 0.1, 0.975, sh.zTail + 0.02},
                                       {hw - 0.1, 0.975, sh.zTail + 0.02},
                                   },
                               },
                               Y)
                           .build());
  // seat belts hanging on the B pillars with their tongues, footrest beside the clutch
  for (const double side : {-1.0, 1.0}) {
    // from the turning loop high on the pillar (which leans in with the side glass) to the reel at its foot
    const Vec3 a = {side * (hw * 0.8 - 0.004), 1.22, sh.zPillarB - 0.023};
    const Vec3 b = {side * (hw - 0.065), 0.5, sh.zPillarB - 0.023};
    const Vec3 a2 = {a[0], a[1], a[2] + 0.046};
    const Vec3 b2 = {b[0], b[1], b[2] + 0.046};
    kit.black.push_back(Sculpt().quad(a, a2, b2, b).quad(a, b, b2, a2).build());
    kit.silver.push_back(box(0.012, 0.03, 0.06, a[0], a[1] + 0.012, sh.zPillarB));
    kit.silver.push_back(box(0.01, 0.05, 0.032, a[0] + (b[0] - a[0]) * 0.55 - side * 0.006, 0.824, sh.zPillarB, 0, 0, side * 0.16));
  }
  kit.black.push_back(box(0.1, 0.02, 0.24, ex - 0.36, 0.34, zc + 0.19, -0.9));
  // sun visors folded up against the roof lining, and the roof lamp
  const auto roofAt = [&sh, hw](double x) { return sh.roofY - 0.004 - 0.051 * (std::fabs(x) / (hw * 0.74)) - 0.025; };
  for (const double side : {-1.0, 1.0}) {
    kit.pale.push_back(box(0.33, 0.014, 0.15, side * 0.465, roofAt(0.465) - 0.008, sh.zRoofFront + 0.105, 0.01, 0, -side * 0.077));
  }
  kit.hard.push_back(box(0.15, 0.014, 0.085, 0.03, roofAt(0.03) - 0.006, sh.zRoofFront + 0.2));
  kit.pale.push_back(box(0.05, 0.004, 0.06, -0.005, roofAt(0.03) - 0.014, sh.zRoofFront + 0.2));
  kit.pale.push_back(box(0.05, 0.004, 0.06, 0.065, roofAt(0.03) - 0.014, sh.zRoofFront + 0.2));

  for (size_t name = 0; name < std::size(KIT_KEYS); name++) {
    const std::vector<GeometryPtr>& own = kit.*KIT_KEYS[name];
    std::vector<const std::vector<GeometryPtr>*> doors;
    for (const Kit& k : doorKits) doors.push_back(&(k.*KIT_KEYS[name]));
    std::vector<GeometryPtr> all = own;
    for (const std::vector<GeometryPtr>* l : doors) all.insert(all.end(), l->begin(), l->end());
    if (all.empty()) continue;
    const std::shared_ptr<Mesh> m = part(merge(all), mats.*MATS_KEYS[name]);
    if (std::any_of(doors.begin(), doors.end(), [](const std::vector<GeometryPtr>* l) { return !l->empty(); })) {
      DoorTrim trim;
      trim.mesh = m;
      trim.rest = !own.empty() ? merge(own) : nullptr;
      for (const std::vector<GeometryPtr>* l : doors) trim.doors.push_back(!l->empty() ? merge(*l) : nullptr);
      doorTrim.push_back(trim);
    }
  }

  // ---- instrument cluster and centre display: self-lit pictures ------------
  const std::shared_ptr<Mesh> dial = make<Mesh>(planeGeometry(cluster.width, cluster.height), basic({.map = panel_.texture, .toneMapped = false}));
  dial->name = "cluster";
  dial->position.set(cluster.centre[0], cluster.centre[1], cluster.centre[2]);
  dial->rotation.x = -cluster.tilt;
  cockpit->add(dial);
  // centre display, standing on the stack and turned a little towards the driver
  const std::shared_ptr<Group> display = make<Group>();
  display->position.set(0, dash.browY(0) - 0.025, dash.browZ(0) + 0.05);
  display->rotation.set(-0.2, -0.13, 0, Euler::YXZ);
  {
    std::vector<std::vector<Vec3>> rings;
    int i = 0;
    for (const double z : {0.0, -0.008, -0.016}) {
      rings.push_back(place(roundRect(i == 2 ? 0.25 : 0.262, i == 2 ? 0.14 : 0.152, 0.014, 1), {0, 0, z}, X, Y));
      i++;
    }
    const GeometryPtr bezel = Sculpt().tube(rings, true, true).build(50);
    part(bezel, mats.black, 0, display.get());
  }
  part(box(0.07, 0.07, 0.03, 0, -0.07, -0.028), mats.hard, 0, display.get());
  // (navigation is drawn on it by the HUD layer later)
  screenStill_ = screenTexture();
  mapTexture->srgb = true;
  const std::shared_ptr<Mesh> screen = make<Mesh>(planeGeometry(0.244, 0.134), basic({.map = screenStill_, .toneMapped = false}));
  screen->name = "screen";
  screen_ = screen;
  screen->position.z = 0.0012;
  display->add(screen);
  cockpit->add(display);

  // ---- steering wheel -------------------------------------------------------
  // Rim plane in the wheel's own XY plane, +Z towards the driver; the hub lies
  // deeper than the rim (a dished wheel).
  const double R = 0.17;
  const GeometryPtr rim = bare(torusGeometry(R, 0.0155, 8, 44));
  part(rim, leather, 1, steering_.get());
  std::vector<std::vector<Vec3>> hubRings;
  for (const auto& [z, kw, kh] : Rows<3>{
           {-0.085, 0.8, 0.9},
           {-0.05, 1, 1},
           {-0.03, 1, 1},
           {-0.022, 0.86, 0.84},
       }) {
    std::vector<Vec3> ring;
    for (const auto& [u, v] : softRing(0.074 * kw, 0.05 * kh, 0.058 * kh, 16)) ring.push_back({u, v - 0.004, z});
    hubRings.push_back(ring);
  }
  Sculpt wheelHard;
  wheelHard.tube(hubRings, true, true);
  // spokes at 9, 3 and 6 o'clock, sloping from the hub out to the rim
  const auto spoke = [&wheelHard, R](double angle, double wHub, double wRim) {
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    std::vector<std::vector<Vec3>> rings;
    for (const auto& [r, wd, z] : Rows<3>{
             {0.05, wHub, -0.04},
             {0.1, (wHub + wRim) / 2, -0.022},
             {R - 0.006, wRim, -0.004},
         }) {
      std::vector<Vec3> ring;
      for (const auto& [u, v] : roundRect(wd, 0.016, 0.006, 1)) ring.push_back({c * r - s * u, s * r + c * u, z + v});
      rings.push_back(ring);
    }
    wheelHard.tube(rings);
  };
  spoke(PI - 0.07, 0.056, 0.04);
  spoke(0.07, 0.056, 0.04);
  spoke(-PI / 2, 0.05, 0.034);
  part(wheelHard.build(50), mats.soft, 1, steering_.get());
  {
    std::vector<std::vector<Vec3>> surround;
    for (const double z : {-0.04, -0.031}) {
      std::vector<Vec3> ring;
      for (const auto& [u, v] : softRing(0.074 * 1.06, 0.05 * 1.07, 0.058 * 1.06, 16)) ring.push_back({u, v - 0.004, z});
      surround.push_back(ring);
    }
    part(Sculpt().tube(surround, false, true).build(50), mats.silver, 1, steering_.get());
  }
  // switch pads on the side spokes, a bright clasp on the lower one
  part(merge({box(0.04, 0.028, 0.004, -0.098, -0.004, -0.0125, 0, -0.2), box(0.04, 0.028, 0.004, 0.098, -0.004, -0.0125, 0, 0.2)}), mats.black, 1, steering_.get());
  part(box(0.03, 0.05, 0.004, 0, -0.112, -0.0105, 0.2), mats.silver, 1, steering_.get());
  // top-centre marker so the wheel position reads at a glance
  {
    const GeometryPtr arc = torusGeometry(R, 0.0165, 8, 2, 0.085);
    arc->rotateZ(PI / 2 - 0.0425);
    const std::shared_ptr<Mesh> marker = make<Mesh>(bare(arc), basic({.color = Color(0xd8dde2)}));
    steering_->add(marker);
  }
  const std::shared_ptr<Group> column = make<Group>();
  // The top of the rim stays just under the line of sight to the base of the windscreen.
  column->position.set(ex, 0.907, -0.27);
  column->rotation.x = -0.42;   // the wheel's axis tilts up towards the driver
  column->add(steering_);
  // column shroud from behind the hub into the dashboard
  {
    std::vector<std::vector<Vec3>> shroud;
    for (const auto& [z, w, h] : Rows<3>{
             {-0.05, 0.1, 0.085},
             {-0.12, 0.118, 0.1},
             {-0.3, 0.14, 0.12},
         })
      shroud.push_back(place(roundRect(w, h, 0.03), {0, -0.012, z}, X, Y));
    part(Sculpt().tube(shroud, true).build(50), mats.hard, 0, column.get());
  }
  // indicator stalk (left) and wiper stalk (right) do not turn with the wheel
  const auto stalkGeo = [](double side) {
    const GeometryPtr arm = cylinderGeometry(0.0055, 0.008, 0.14, 8);
    arm->rotateZ((side * PI) / 2).translate(side * 0.125, 0, 0);
    const GeometryPtr knob = cylinderGeometry(0.0105, 0.0105, 0.036, 8);
    knob->rotateZ((side * PI) / 2).translate(side * 0.2, 0, 0);
    return merge({bare(arm), bare(knob)});
  };
  stalk_->position.set(0, 0.04, -0.085);
  part(stalkGeo(-1), mats.black, 1, stalk_.get());
  const std::shared_ptr<Mesh> wiper = part(stalkGeo(1), mats.black, 1, column.get());
  wiper->position.set(0, 0.04, -0.085);
  wiper->rotation.z = -0.06;
  column->add(stalk_);
  cockpit->add(column);

  // ---- gear lever, handbrake, pedals ---------------------------------------
  lever_->position.set(0, 0.505, -0.12);
  // (the gaiter's skirt reaches down into the console, so tilting the lever opens no gap under it)
  {
    std::vector<std::vector<Vec3>> boot;
    for (const auto& [y, a, b] : Rows<3>{
             {-0.022, 0.05, 0.063},
             {0.05, 0.03, 0.036},
             {0.095, 0.014, 0.016},
         }) {
      std::vector<Vec3> ring;
      for (int i = 0; i < 10; i++) {
        const double an = (i / 10.0) * PI * 2;
        // a little pleated, like a leather gaiter
        const double k = i % 2 == 0 ? 1 : 0.9;
        ring.push_back({std::cos(an) * a * k, y, -std::sin(an) * b * k});
      }
      boot.push_back(ring);
    }
    part(Sculpt().tube(boot).build(35), leather, 1, lever_.get());
  }
  {
    const GeometryPtr rod = cylinderGeometry(0.0075, 0.0075, 0.12, 8);
    rod->translate(0, 0.14, 0);
    part(bare(rod), mats.silver, 1, lever_.get());
    const GeometryPtr knob = sphereGeometry(0.027, 12, 8);
    knob->scale(1, 0.9, 1.05).translate(0, 0.205, 0);
    part(bare(knob), leather, 1, lever_.get());
  }
  cockpit->add(lever_);

  handbrake_->position.set(0.058, 0.525, 0.25);
  {
    std::vector<std::vector<Vec3>> grip;
    for (const auto& [z, w, h] : Rows<3>{
             {0.01, 0.03, 0.026},
             {-0.07, 0.03, 0.026},
             {-0.1, 0.036, 0.034},
             {-0.2, 0.036, 0.034},
             {-0.212, 0.026, 0.024},
         })
      grip.push_back(place(roundRect(w, h, 0.01, 1), {0, 0.016, z}, X, Y));
    part(Sculpt().tube(grip, true, true).build(50), leather, 1, handbrake_.get());
    const GeometryPtr button = cylinderGeometry(0.008, 0.008, 0.014, 8);
    button->rotateX(PI / 2).translate(0, 0.016, -0.218);
    part(bare(button), mats.silver, 1, handbrake_.get());
  }
  cockpit->add(handbrake_);

  const double pedalX[3] = {ex - 0.19, ex - 0.05, ex + 0.11};
  for (int i = 0; i < 3; i++) {
    const std::shared_ptr<Group> p = make<Group>();
    p->position.set(pedalX[i], 0.52, zc + 0.15);
    part(box(0.016, 0.22, 0.012, 0, -0.11, 0), mats.black, 0, p.get());
    part(i == 2 ? box(0.046, 0.12, 0.014, 0, -0.2, 0.006) : box(0.07, 0.06, 0.014, 0, -0.23, 0.006), mats.black, 0, p.get());
    p->rotation.x = -0.5;
    pedals_.push_back(p);
    cockpit->add(p);
  }

  // ---- mirrors ---------------------------------------------------------------
  // Inside mirror: small, curved and tucked up under the roof's front edge.
  // There it stays clear of the stretch of windscreen in which the traffic
  // lights of a junction appear to the car waiting first at the line.
  const Vector3 inner(ex + 0.172, sh.roofY - 0.077, sh.zRoofFront + 0.044);
  mirrors.push_back(std::make_unique<Mirror>(MirrorSpec{
      .name = "inside",
      .centre = inner,
      .width = 0.15,
      .height = 0.042,
      .normal = mirrorNormal(inner, eye, Vector3(0, -0.03, 1)),
      .convex = 1.5,
      .resolution = {512, 160},
  }));
  for (const double side : {-1.0, 1.0}) {
    // at the front corner of the door glass: there the left one is inside the
    // forward view on an ordinary screen (about 36° off the line of sight)
    const Vector3 c(side * (hw + 0.1), belt + 0.06, zc + 0.28);
    mirrors.push_back(std::make_unique<Mirror>(MirrorSpec{
        .name = side < 0 ? "left" : "right",
        .centre = c,
        .width = 0.17,
        .height = 0.105,
        .normal = mirrorNormal(c, eye, Vector3(side * 0.16, -0.06, 1)),
        .convex = side < 0 ? 1.25 : 1.6,
        .resolution = {320, 200},
    }));
  }
  // Housings: a rounded shell behind each glass.  They stay solid when the
  // cabin is see-through – a mirror must always read as a mirror.
  const MaterialPtr shellPaint = exterior.paint->clone();
  const MaterialPtr mirrorPlastic = cabinMaterial(0x24262a);
  for (size_t i = 0; i < mirrors.size(); i++) {
    Mirror& m = *mirrors[i];
    const MirrorSpec& s = m.spec;
    const Vector3 n = s.normal.clone().normalize();
    const Vector3 up = Vector3(0, 1, 0).addScaledVector(n, -n.y).normalize();
    const Vector3 right = Vector3().crossVectors(up, n);
    const double depth = i == 0 ? 0.024 : 0.085;
    std::vector<std::vector<Vec3>> rings;
    for (const auto& [back, k] : Rows<2>{
             {0.002, 1},
             {0.4, 1},
             {0.8, 0.82},
             {1, 0.5},
         }) {
      const Vector3 o = s.centre.clone().addScaledVector(n, -back * depth);
      rings.push_back(place(roundRect((s.width + 0.022) * k, (s.height + 0.022) * k, 0.018 * k, 2), {o.x, o.y, o.z}, {right.x, right.y, right.z}, {up.x, up.y, up.z}));
    }
    const std::shared_ptr<Mesh> housing = make<Mesh>(Sculpt().tube(rings, true, true).build(50), i == 0 ? mirrorPlastic : shellPaint);
    housing->name = "mirror housing " + s.name;
    // (the housing goes along when the glass is moved: Mirror.place, ui/hudmirror.ts)
    m.attached.push_back(housing);
    cockpit->add(m.mesh, housing);
  }
  // stem of the inside mirror up to the roof's front edge, feet of the door mirrors
  Sculpt stem;
  {
    std::vector<std::vector<Vec3>> rings;
    int i = 0;
    for (const Vec3& o : {Vec3{inner.x + 0.02, inner.y + 0.012, inner.z - 0.02}, Vec3{inner.x + 0.035, sh.roofY - 0.036, sh.zRoofFront + 0.012}}) {
      rings.push_back(place(roundRect(i == 0 ? 0.022 : 0.04, i == 0 ? 0.022 : 0.03, 0.006, 1), o, X, {0, 0.5, 0.87}));
      i++;
    }
    stem.tube(rings, true, true);
  }
  std::vector<GeometryPtr> feet = {stem.build(50)};
  for (const double side : {-1.0, 1.0}) {
    // a short arm from the door, slimmer towards the housing it carries
    std::vector<std::vector<Vec3>> arm;
    for (const auto& [x, d, h] : Rows<3>{
             {hw - 0.035, 0.066, 0.038},
             {hw + 0.03, 0.056, 0.032},
             {hw + 0.092, 0.042, 0.022},
         })
      arm.push_back(place(roundRect(d, h, 0.009, 1), {side * x, belt - 0.013, zc + 0.264}, Z, Y));
    feet.push_back(Sculpt().tube(arm, false, true).build(50));
  }
  const std::shared_ptr<Mesh> feetMesh = make<Mesh>(merge(feet), mirrorPlastic);
  feetMesh->name = "mirror feet";
  cockpit->add(feetMesh);

  // ---- see-through cabin: the body belongs to it as well --------------------
  for (const std::shared_ptr<Mesh>& m : exterior.solids) ghost(m, 0);
}

Object3D* PlayerCar::screenMesh() const { return screen_.get(); }

void PlayerCar::setMapOnScreen(bool on) {
  Material& m = *screen_->material;
  const TexturePtr& want = on ? mapTexture : screenStill_;
  if (m.map != want) {
    m.map = want;
    m.needsUpdate();
  }
}

void PlayerCar::setCabinOpacity(double opacity) {
  const double o = std::isfinite(opacity) ? jsMin(1, jsMax(0.05, opacity)) : 1;
  const bool on = o < 0.995;
  const bool was = cabinOpacity_ < 1;
  cabinOpacity_ = on ? o : 1;
  if (on == was) return;
  for (const Ghost& g : ghosts_) {
    g.twin->visible = on;
    // draw order: depth of the cabin, cabin, depth of the controls, controls (then the glass, 5)
    g.mesh->renderOrder = on ? (g.layer == 0 ? 2 : 4) : 0;
  }
  for (const MaterialPtr& m : ghostMaterials_) {
    m->transparent = on;
    m->opacity = 1;
    m->needsUpdate();
  }
}

void PlayerCar::ghost(const std::shared_ptr<Mesh>& mesh, int layer) {
  const std::shared_ptr<Mesh> twin = make<Mesh>(mesh->geometry, depthOnly_);
  twin->visible = false;
  twin->renderOrder = layer == 0 ? 1 : 3;
  mesh->add(twin);
  ghosts_.push_back({mesh, twin, layer});
  const MaterialPtr& mat = mesh->material;
  if (std::find(ghostMaterials_.begin(), ghostMaterials_.end(), mat) != ghostMaterials_.end()) return;
  ghostMaterials_.push_back(mat);
  // Decided per picture: only the driver's own view from inside is see-through,
  // not the mirror pictures and not a camera outside the car.
  Material* m = mat.get();
  mat->onBeforeRender = [this, m](Renderer&, Scene&, Camera& camera) { m->opacity = cabinOpacity_ < 1 && seesFromInside(camera) ? cabinOpacity_ : 1; };
}

bool PlayerCar::seesFromInside(const Camera& camera) {
  for (const std::unique_ptr<Mirror>& m : mirrors)
    if (m->camera.get() == &camera) return false;
  const CarShell& sh = exterior.shell;
  const Vector3& p = root->worldToLocal(tmp_.setFromMatrixPosition(camera.matrixWorld));
  return std::fabs(p.x) < sh.halfWidth && p.y > 0.25 && p.y < sh.roofY && p.z > sh.zCowl - 0.1 && p.z < sh.zTail;
}

void PlayerCar::update(const CarVisualState& s, double dt, double now) {
  panel_.update(s, now);
  exterior.setWheels(s.wheelSteer, s.wheelSpin, s.wheelY);
  exterior.setLamps({
      .low = s.lowBeam,
      .high = s.highBeam,
      .brake = s.braking,
      .reverse = s.reversing,
      .left = s.blinkOn && (s.hazard || s.indicator < 0),
      .right = s.blinkOn && (s.hazard || s.indicator > 0),
  });
  steering_->rotation.z = -s.steerWheel;
  stalk_->rotation.z = s.indicator * 0.16;   // up = right, down = left

  // H gate: R far left forward, 1/2 left, 3/4 middle, 5/6 right
  double tx = 0;
  double tz = 0;
  const int g = s.gear;
  if (!s.automatic) {
    if (g == -1) {
      tx = -1.5;
      tz = -1;
    } else if (g > 0) {
      tx = std::floor((g - 1) / 2.0) - 1;
      tz = g % 2 == 1 ? -1 : 1;
    }
    if (g != 0) gate_ = tx;
    else tx = gate_ * 0.0;   // neutral springs to the 3/4 plane
  } else {
    tz = g == -2 ? -1.5 : g == -1 ? -0.75 : g == 0 ? 0 : 0.75;
  }
  const double k = jsMin(1, jsMax(0, dt * 14));
  leverX_ += (tx - leverX_) * k;
  leverZ_ += (tz - leverZ_) * k;
  lever_->rotation.z = -leverX_ * 0.17;
  lever_->rotation.x = leverZ_ * 0.2;
  handbrake_->rotation.x = s.handbrakeLever * 0.42;
  // pads hang towards the driver; pressing swings them forward (an automatic has no clutch pedal)
  pedals_[0]->visible = !s.automatic;
  pedals_[0]->rotation.x = -0.5 + s.clutch * 0.32;
  pedals_[1]->rotation.x = -0.5 + s.brake * 0.22;
  pedals_[2]->rotation.x = -0.5 + s.throttle * 0.28;
  if (detail_) detail_->update(s);
}

void PlayerCar::setDetail(bool on) {
  if (on && !detail_ && CarDetail::create) {
    std::vector<Mirror*> all;
    for (const std::unique_ptr<Mirror>& m : mirrors) all.push_back(m.get());
    detail_ = CarDetail::create(CarParts{
        .exterior = &exterior, .cockpit = cockpit, .mirrors = all, .steering = steering_, .lever = lever_, .handbrake = handbrake_, .pedals = pedals_, .stalk = stalk_, .eye = eye});
    for (const auto& [mesh, layer] : detail_->ghosts) ghost(mesh, layer);
    // (the see-through cabin takes the new parts in at once)
    const double o = cabinOpacity_;
    cabinOpacity_ = 1;
    setCabinOpacity(o);
  }
  if (detail_) detail_->show(on);
  if (on && detail_ && opener_) opener_->addDetail(detail_->meshes());
}

CarOpenings* PlayerCar::openings() {
  if (!opener_ && CarOpenings::create) {
    std::vector<Mirror*> all;
    for (const std::unique_ptr<Mirror>& m : mirrors) all.push_back(m.get());
    opener_ = CarOpenings::create(OpeningsHost{
        .root = root,
        .exterior = {.group = exterior.group, .shell = &exterior.shell, .dims = &exterior.dims},
        .cockpit = cockpit,
        .mirrors = all,
        .doorTrim = &doorTrim,
        .register_ = [this](const std::shared_ptr<Mesh>& m) { ghost(m, 0); },
    });
    if (detail_) opener_->addDetail(detail_->meshes());
    // (the see-through cabin takes the new parts in at once)
    const double o = cabinOpacity_;
    cabinOpacity_ = 1;
    setCabinOpacity(o);
  }
  return opener_.get();
}

void PlayerCar::setOpenings(const std::vector<double>& values) {
  if (opener_) opener_->pose(values);
  else
    for (size_t i = 0; i < values.size(); i++)
      if (values[i] > 0) {
        if (CarOpenings* o = openings()) o->pose(values);
        return;
      }
}

void PlayerCar::dispose() {
  for (const std::unique_ptr<Mirror>& m : mirrors) m->dispose();
}

}  // namespace fs

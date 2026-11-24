// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/crane.ts @ v1.7.7
#include "render/crane.h"

#include <cmath>
#include <utility>

#include "core/js.h"
#include "engine/geometries.h"
#include "render/car.h"
#include "render/truck.h"

namespace fs {

// ------------------------------------------------------------------------------------ materials

namespace {

/** (the original's module constants: made when first asked for; named as there, held together – fs::WHITE() is the truck's) */
struct CraneMaterials {
  const MaterialPtr YELLOW = lambert({.color = 0xf0b40c});
  const MaterialPtr WHITE = lambert({.color = 0xedefed});
  const MaterialPtr STEEL = lambert({.color = 0x3a3e44});
  const MaterialPtr GALV = lambert({.color = 0xa8adb2});
  const MaterialPtr CONCRETE = lambert({.color = 0x9d9b95});
  const MaterialPtr RED = lambert({.color = 0xb8231c});
  const MaterialPtr TIMBER = lambert({.color = 0x9a7448});
  const MaterialPtr ROPE = lambert({.color = 0x232323});
  const MaterialPtr CHROME = phong({.color = 0xc9d0d6, .specular = 0x9aa4ad, .shininess = 90});
  const MaterialPtr HOOK = lambert({.color = 0xf2c200});
  const MaterialPtr BEACON = basic({.color = 0xff2a10});
  const MaterialPtr BEACON_OFF = lambert({.color = 0x5a1a12});
  const MaterialPtr PAD = lambert({.color = 0x2a2b2d});
};

const CraneMaterials& K() {
  static const CraneMaterials k;
  return k;
}

// ------------------------------------------------------------------------------------ building parts

const Vector3 UP(0, 1, 0);

/** Position and normal only, un-indexed: the form in which parts are merged. */
GeometryPtr bare(const GeometryPtr& g) {
  GeometryPtr n = g->index ? g->toNonIndexed() : g;
  std::vector<std::string> names;
  for (const auto& [name, a] : n->attributes) names.push_back(name);
  for (const std::string& name : names)
    if (name != "position" && name != "normal") n->deleteAttribute(name);
  return n;
}

/**
 * Parts collected by material and merged into one mesh each: boxes from
 * corner to corner, bars and rods from point to point.
 */
class Parts {
 public:
  void box(const MaterialPtr& mat, double x0, double x1, double y0, double y1, double z0, double z1) {
    GeometryPtr g = boxGeometry(std::abs(x1 - x0), std::abs(y1 - y0), std::abs(z1 - z0));
    g->translate((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
    push(mat, g);
  }

  /** a straight member of square section w (or w × h) from one point to another */
  void bar(const MaterialPtr& mat, double ax, double ay, double az, double bx, double by, double bz, double w) { bar(mat, ax, ay, az, bx, by, bz, w, w); }
  void bar(const MaterialPtr& mat, double ax, double ay, double az, double bx, double by, double bz, double w, double h) {
    a_.set(ax, ay, az);
    b_.set(bx, by, bz).sub(a_);
    const double len = b_.length();
    if (len < 1e-4) return;
    GeometryPtr g = boxGeometry(w, len, h);
    q_.setFromUnitVectors(UP, b_.normalize());
    m_.compose(a_.addScaledVector(b_, len / 2), q_, Vector3(1, 1, 1));
    g->applyMatrix4(m_);
    push(mat, g);
  }

  /** a round member (tube, rope, rod) from one point to another */
  void rod(const MaterialPtr& mat, double ax, double ay, double az, double bx, double by, double bz, double r, int seg = 8) {
    a_.set(ax, ay, az);
    b_.set(bx, by, bz).sub(a_);
    const double len = b_.length();
    if (len < 1e-4) return;
    GeometryPtr g = cylinderGeometry(r, r, len, seg, 1);
    q_.setFromUnitVectors(UP, b_.normalize());
    m_.compose(a_.addScaledVector(b_, len / 2), q_, Vector3(1, 1, 1));
    g->applyMatrix4(m_);
    push(mat, g);
  }

  /** a cylinder (a drum, a sheave, a wheel) with its axis along x, y or z */
  void cyl(const MaterialPtr& mat, double x, double y, double z, double r, double len, char axis, int seg = 14) {
    GeometryPtr g = cylinderGeometry(r, r, len, seg, 1);
    if (axis == 'x') g->rotateZ(PI / 2);
    else if (axis == 'z') g->rotateX(PI / 2);
    g->translate(x, y, z);
    push(mat, g);
  }

  void sphere(const MaterialPtr& mat, double x, double y, double z, double r) {
    GeometryPtr g = sphereGeometry(r, 10, 6);
    g->translate(x, y, z);
    push(mat, g);
  }

  /** Merge what was collected into meshes added to `group`. */
  void build(Object3D& group, bool shadow = true) {
    for (const auto& [mat, list] : lists_) {
      GeometryPtr g = mergeGeometries(list, false);
      if (!g) continue;
      auto mesh = make<Mesh>(g, mat);
      mesh->castShadow = shadow && mat != GLASS();
      mesh->receiveShadow = false;
      group.add(mesh);
    }
    lists_.clear();
  }

 private:
  void push(const MaterialPtr& mat, const GeometryPtr& g) {
    for (auto& [m, l] : lists_) {
      if (m == mat) {
        l.push_back(bare(g));
        return;
      }
    }
    lists_.push_back({mat, {bare(g)}});
  }

  /** (a Map in the original: by material, in the order they came) */
  std::vector<std::pair<MaterialPtr, std::vector<GeometryPtr>>> lists_;
  Matrix4 m_;
  Quaternion q_;
  Vector3 a_;
  Vector3 b_;
};

/** A plate with lettering on both faces (a load plate on the jib, a name on the boom). */
std::shared_ptr<Group> plate(const std::string& text, double w, double h) {
  auto g = make<Group>();
  auto front = lettering(text, w, h);
  auto back = lettering(text, w, h);
  back->rotation.y = PI;
  back->position.z = -0.01;
  g->add(front, back);
  return g;
}

/** The hook block: sheaves between side plates, the crosshead, the hook – yellow, black stripes. */
std::shared_ptr<Group> hookBlock(double width, double height, int sheaves, const CraneDetail& detail) {
  const CraneMaterials& k = K();
  auto g = make<Group>();
  Parts p;
  const double hw = width / 2;
  // side plates and the sheaves between them
  p.box(k.HOOK, -hw, -hw + 0.05, -height * 0.55, 0, -0.22, 0.22);
  p.box(k.HOOK, hw - 0.05, hw, -height * 0.55, 0, -0.22, 0.22);
  for (int i = 0; i < sheaves; i++) {
    const double x = -hw + 0.1 + ((width - 0.2) * (i + 0.5)) / sheaves;
    p.cyl(k.STEEL, x, -height * 0.25, 0, jsMin(0.2, height * 0.3), jsMin(0.08, (width - 0.2) / sheaves - 0.02), 'x', 12);
  }
  // the crosshead, its nut, the hook's shank and the hook
  p.box(k.STEEL, -hw, hw, -height * 0.62, -height * 0.55, -0.18, 0.18);
  p.cyl(k.STEEL, 0, -height * 0.7, 0, 0.07, 0.16, 'y', 10);
  const double hookR = jsMax(0.12, width * 0.28);
  GeometryPtr curve = torusGeometry(hookR, hookR * 0.28, 8, 16, PI * 1.4);
  curve->rotateZ(-PI * 0.2);
  curve->translate(0, -height * 0.78 - hookR, 0);
  auto hookMesh = make<Mesh>(curve, k.HOOK);
  hookMesh->castShadow = true;
  g->add(hookMesh);
  if (detail == "full") {
    // black stripes on the side plates
    for (int i = 0; i < 3; i++) {
      const double y = -height * 0.1 - i * height * 0.15;
      p.box(DARK(), -hw - 0.003, -hw + 0.053, y - 0.035, y, -0.221, 0.221);
      p.box(DARK(), hw - 0.053, hw + 0.003, y - 0.035, y, -0.221, 0.221);
    }
  }
  p.build(*g);
  return g;
}

/** A wheel lying on the x axis: tyre and rim. */
std::shared_ptr<Group> wheelGroup(double radius, double width) {
  auto g = make<Group>();
  Parts p;
  p.cyl(TYRE(), 0, 0, 0, radius, width, 'x', 20);
  p.cyl(RIM(), 0, 0, 0, radius * 0.56, width + 0.02, 'x', 12);
  p.build(*g);
  return g;
}

Vector3 tmp;
Vector3 tmp2;
Quaternion tmpQ;

/** The group the whole crane is in, turned over its tipping line when it goes over. */
std::pair<std::shared_ptr<Group>, std::shared_ptr<Group>> tipping(Group& root) {
  auto tip = make<Group>();
  auto inner = make<Group>();
  tip->add(inner);
  root.add(tip);
  return {tip, inner};
}

void poseTip(Group& tip, Group& inner, const Crane& c) {
  const auto& t = c.tipped;
  if (!t || t->angle <= 0) {
    tip.position.set(0, 0, 0);
    tip.quaternion.identity();
    inner.position.set(0, 0, 0);
    return;
  }
  tip.position.set(t->px, 0, t->pz);
  inner.position.set(-t->px, 0, -t->pz);
  tmp.set(t->nz, 0, -t->nx).normalize();
  tip.quaternion.setFromAxisAngle(tmp, t->angle);
}

/** `s.wheelY[k] ?? d` */
double at(const std::vector<double>& v, size_t k, double d) { return k < v.size() ? v[k] : d; }

}  // namespace

namespace crane_render {

/** A rope (or anything long and thin that moves): a unit cylinder stretched from one point to another each picture. */
class Line {
 public:
  std::shared_ptr<Mesh> mesh;

  Line(Object3D& parent, double r, const MaterialPtr& mat = nullptr) : r_(r) {
    mesh = make<Mesh>(geo(), mat ? mat : K().ROPE);
    mesh->castShadow = true;
    parent.add(mesh);
  }

  /** from a to b (in the parent's frame) */
  void set(const Vector3& a, const Vector3& b) {
    d_.subVectors(b, a);
    const double len = d_.length();
    mesh->visible = len > 1e-3;
    if (!mesh->visible) return;
    mesh->position.copy(a);
    mesh->quaternion.setFromUnitVectors(UP, d_.divideScalar(len));
    mesh->scale.set(r_, len, r_);
  }

 private:
  static const GeometryPtr& geo() {
    static const GeometryPtr g = cylinderGeometry(1, 1, 1, 6, 1)->translate(0, 0.5, 0).shared_from_this();
    return g;
  }
  Vector3 d_;
  const double r_;
};

}  // namespace crane_render



// ------------------------------------------------------------------------------------ the tower crane

namespace {
const TowerGeometry& TG = TOWER_GEOMETRY;
/** the jib's bottom chords (the trolley runs on them) and its top chord */
const double JIB_Y = TOWER_GEOMETRY.sheave + 0.3;
const double JIB_TOP = JIB_Y + 1.6;
const double JIB_HALF = 0.6;
/** the tower head's apex (the pendant ties hang from it) */
const double APEX = 39.2;
}  // namespace

TowerCraneModel::TowerCraneModel(const CraneDetail& detail)
    : CraneModel("tower", Vector3(1.75, TOWER_GEOMETRY.mastTop + 1.3, -1.4)), cabEye(1.75, TOWER_GEOMETRY.mastTop + 1.3, -1.6) {
  const CraneMaterials& k = K();
  root->name = "tower crane";
  std::tie(tip, inner) = tipping(*root);
  const bool full = detail == "full";
  // ---- the base: four foundation blocks, the cross base, the ballast blocks on it
  Parts base;
  const double s = TOWER_CRANE.feet[2][0];
  for (const auto& f : TOWER_CRANE.feet) {
    const double x = f[0];
    const double z = f[1];
    base.box(k.CONCRETE, x - 0.75, x + 0.75, -0.7, 0.3, z - 0.75, z + 0.75);
  }
  // the cross: two girders from corner to corner, the middle piece the mast stands on
  base.bar(k.STEEL, -s, 0.55, -s, s, 0.55, s, 0.5);
  base.bar(k.STEEL, -s, 0.55, s, s, 0.55, -s, 0.5);
  base.box(k.STEEL, -1.1, 1.1, 0.3, 0.85, -1.1, 1.1);
  // the central ballast: two layers of four blocks round the mast's foot
  const double bl = 2.45;
  for (const auto& [y0, y1] : std::vector<std::pair<double, double>>{{0.85, 1.72}, {1.72, 2.6}}) {
    base.box(k.CONCRETE, -bl, bl, y0, y1 - 0.02, -bl, -1.15);
    base.box(k.CONCRETE, -bl, bl, y0, y1 - 0.02, 1.15, bl);
    base.box(k.CONCRETE, -bl, -1.15, y0, y1 - 0.02, -1.13, 1.13);
    base.box(k.CONCRETE, 1.15, bl, y0, y1 - 0.02, -1.13, 1.13);
  }
  if (full)
    for (const auto& f : TOWER_CRANE.feet) base.cyl(k.STEEL, f[0] * 0.9, 0.95, f[1] * 0.9, 0.06, 0.4, 'y', 6);
  // ---- the mast: a foot section, then lattice sections of 3 m
  const double mw = TG.mastWidth / 2;
  const double c = 0.075;
  base.box(k.YELLOW, -mw, mw, 0.85, 2.85, -mw, mw);
  const int sections = 9;
  const double top = TG.mastTop;
  const double sh = (top - 2.85) / sections;
  const std::array<std::array<double, 2>, 4> corners = {{{-mw, -mw}, {mw, -mw}, {mw, mw}, {-mw, mw}}};
  for (int i = 0; i < sections; i++) {
    const double y0 = 2.85 + i * sh;
    const double y1 = y0 + sh;
    for (const auto& [x, z] : corners) base.box(k.YELLOW, x - c, x + c, y0, y1, z - c, z + c);
    // horizontals at the joint, and a diagonal on each face (alternating)
    const std::array<std::array<double, 4>, 4> faces = {{{-mw, -mw, mw, -mw}, {mw, -mw, mw, mw}, {mw, mw, -mw, mw}, {-mw, mw, -mw, -mw}}};
    for (int kf = 0; kf < 4; kf++) {
      const auto [ax, az, bx, bz] = faces[kf];
      base.bar(k.YELLOW, ax, y0 + 0.05, az, bx, y0 + 0.05, bz, 0.07);
      if (!full && kf % 2 == 1) continue;
      const bool flip = (i + kf) % 2 == 0;
      base.bar(k.YELLOW, flip ? ax : bx, y0 + 0.08, flip ? az : bz, flip ? bx : ax, y1 - 0.05, flip ? bz : az, 0.06);
      if (full) base.bar(k.YELLOW, (ax + bx) / 2, y0 + 0.08, (az + bz) / 2, flip ? bx : ax, y0 + sh / 2, flip ? bz : az, 0.045);
    }
    // the bolted joints between the sections
    if (full)
      for (const auto& [x, z] : corners) base.box(k.STEEL, x - 0.11, x + 0.11, y0 - 0.12, y0 + 0.12, z - 0.11, z + 0.11);
  }
  base.bar(k.YELLOW, -mw, top - 0.05, -mw, mw, top - 0.05, -mw, 0.08);
  base.bar(k.YELLOW, -mw, top - 0.05, mw, mw, top - 0.05, mw, 0.08);
  if (full) {
    // the ladder inside the mast, and a rest platform every third section
    const double lx = 0.25;
    base.box(k.GALV, -lx - 0.03, -lx, 2.85, top, mw - 0.35, mw - 0.32);
    base.box(k.GALV, lx, lx + 0.03, 2.85, top, mw - 0.35, mw - 0.32);
    for (double y = 3.1; y < top; y += 0.3) base.box(k.GALV, -lx, lx, y, y + 0.025, mw - 0.35, mw - 0.32);
    for (int i = 3; i < sections; i += 3) base.box(k.GALV, -mw + 0.1, mw - 0.1, 2.85 + i * sh - 0.05, 2.85 + i * sh, -mw + 0.1, mw - 0.45);
  }
  // the slewing ring's lower half
  base.cyl(k.STEEL, 0, top + 0.1, 0, 1.1, 0.2, 'y', 24);
  base.build(*inner);
  // ---- what turns: the slewing ring's upper half, the platform, the cab, the tower head, the jib, the counter-jib
  Group& sw = *slewing;
  sw.position.y = 0;
  inner->add(slewing);
  Parts p;
  const double plat = top + 0.25;
  p.cyl(k.STEEL, 0, top + 0.27, 0, 1.12, 0.14, 'y', 24);
  p.box(k.YELLOW, -1.2, 1.2, plat + 0.1, plat + 0.6, -1.2, 1.2);
  // the cab: at the jib's root on the right, glass all round and below
  const double cx0 = 0.95;
  const double cx1 = 2.45;
  const double cy0 = top - 0.2;
  const double cy1 = top + 2.15;
  const double cz0 = -2.6;
  const double cz1 = -0.3;
  p.box(k.WHITE, cx0, cx1, cy0, cy0 + 0.15, cz0, cz1);
  p.box(k.WHITE, cx0, cx1, cy1 - 0.12, cy1, cz0, cz1);
  p.box(k.WHITE, cx0, cx1, cy0, cy1, cz1 - 0.08, cz1);
  for (const auto& [x, z] : std::array<std::array<double, 2>, 4>{{{cx0, cz0}, {cx1, cz0}, {cx0, cz1}, {cx1, cz1}}}) p.box(k.WHITE, x - 0.04, x + 0.04, cy0, cy1, z - 0.04, z + 0.04);
  p.box(k.WHITE, cx0, cx1, cy0 + 0.15, cy0 + 0.75, cz1 - 0.6, cz1 - 0.08);
  p.box(k.STEEL, cx0 + 0.3, cx1 - 0.3, cy0 + 0.15, cy0 + 0.55, cz0 + 0.7, cz0 + 1.2);
  Parts glass;
  glass.box(GLASS(), cx0, cx1, cy0 + 0.15, cy1 - 0.12, cz0 - 0.005, cz0 + 0.005);
  glass.box(GLASS(), cx0 - 0.005, cx0 + 0.005, cy0 + 0.75, cy1 - 0.12, cz0, cz1 - 0.6);
  glass.box(GLASS(), cx1 - 0.005, cx1 + 0.005, cy0 + 0.15, cy1 - 0.12, cz0, cz1 - 0.08);
  glass.box(GLASS(), cx0 + 0.05, cx1 - 0.05, cy0 + 0.155, cy0 + 0.165, cz0 + 0.05, cz0 + 0.65);
  // the tower head: four legs to the apex, braced
  for (const auto& [x, z] : std::array<std::array<double, 2>, 4>{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}}) p.bar(k.YELLOW, x, plat + 0.6, z, x * 0.18, APEX, z * 0.18, 0.14);
  for (int kk = 0; kk < (full ? 4 : 2); kk++) {
    const double y = plat + 0.6 + ((APEX - plat - 0.6) * (kk + 1)) / 5;
    const double w = 1 - (0.82 * (kk + 1)) / 5;
    p.bar(k.YELLOW, -w, y, -w, w, y, -w, 0.06);
    p.bar(k.YELLOW, -w, y, w, w, y, w, 0.06);
    p.bar(k.YELLOW, -w, y, -w, -w, y, w, 0.06);
    p.bar(k.YELLOW, w, y, -w, w, y, w, 0.06);
  }
  p.box(k.STEEL, -0.3, 0.3, APEX - 0.2, APEX + 0.15, -0.3, 0.3);
  // the jib: two bottom chords (the trolley's rails) and a top chord, braced in zigzags, sections of 10 m
  const double L = TG.jibLength;
  const double step = full ? 2 : 4;
  const auto zj = [](double a) { return -a; };
  p.box(k.YELLOW, -JIB_HALF - 0.08, -JIB_HALF + 0.08, JIB_Y, JIB_Y + 0.18, zj(L), zj(1.0));
  p.box(k.YELLOW, JIB_HALF - 0.08, JIB_HALF + 0.08, JIB_Y, JIB_Y + 0.18, zj(L), zj(1.0));
  p.box(k.YELLOW, -0.08, 0.08, JIB_TOP - 0.16, JIB_TOP, zj(L - 0.4), zj(1.2));
  for (double a = 1.0; a < L - 0.01; a += step) {
    const double b = jsMin(L, a + step);
    const double mid = (a + b) / 2;
    p.bar(k.YELLOW, -JIB_HALF, JIB_Y + 0.1, zj(a), 0, JIB_TOP - 0.08, zj(mid), 0.07);
    p.bar(k.YELLOW, 0, JIB_TOP - 0.08, zj(mid), -JIB_HALF, JIB_Y + 0.1, zj(b), 0.07);
    p.bar(k.YELLOW, JIB_HALF, JIB_Y + 0.1, zj(a), 0, JIB_TOP - 0.08, zj(mid), 0.07);
    p.bar(k.YELLOW, 0, JIB_TOP - 0.08, zj(mid), JIB_HALF, JIB_Y + 0.1, zj(b), 0.07);
    p.bar(k.YELLOW, -JIB_HALF, JIB_Y + 0.05, zj(a), JIB_HALF, JIB_Y + 0.05, zj(a), 0.06);
    if (full) p.bar(k.YELLOW, -JIB_HALF, JIB_Y + 0.05, zj(a), JIB_HALF, JIB_Y + 0.05, zj(b), 0.045);
  }
  // the joints of the jib's sections
  if (full)
    for (double a = 10; a < L; a += 10) p.box(k.STEEL, -JIB_HALF - 0.12, JIB_HALF + 0.12, JIB_Y - 0.05, JIB_Y + 0.25, zj(a) - 0.12, zj(a) + 0.12);
  p.box(k.YELLOW, -JIB_HALF - 0.1, JIB_HALF + 0.1, JIB_Y, JIB_TOP - 0.2, zj(L) - 0.1, zj(L) + 0.1);
  // the jib's root on the platform
  p.box(k.YELLOW, -1.0, 1.0, plat + 0.6, JIB_Y + 0.2, -1.2, -0.6);
  // the pendant ties: from the apex to the jib (two points) and to the counter-jib's end
  for (const double a : {22.0, 38.0}) {
    p.rod(k.STEEL, -0.12, APEX, -0.1, -0.05, JIB_TOP, zj(a), 0.04, 6);
    p.rod(k.STEEL, 0.12, APEX, -0.1, 0.05, JIB_TOP, zj(a), 0.04, 6);
  }
  const double CJ = TG.counterJib;
  for (const double side : {-1.0, 1.0}) p.rod(k.STEEL, side * 0.15, APEX, 0.1, side * 0.85, JIB_Y + 0.45, CJ - 0.6, 0.045, 6);
  // the counter-jib: two side girders, cross members, the walkway and its railings
  for (const double side : {-1.0, 1.0}) p.box(k.YELLOW, side * 0.9 - 0.13, side * 0.9 + 0.13, JIB_Y - 0.2, JIB_Y + 0.45, 0.6, CJ);
  for (double a = 1; a < CJ; a += full ? 1.5 : 3) p.box(k.YELLOW, -0.9, 0.9, JIB_Y - 0.15, JIB_Y - 0.02, a, a + 0.12);
  p.box(k.GALV, -0.85, 0.85, JIB_Y + 0.45, JIB_Y + 0.5, 0.6, CJ - 3);
  if (full) {
    for (const double side : {-1.0, 1.0}) {
      for (double a = 0.8; a < CJ - 2.8; a += 1.6) p.box(k.GALV, side * 0.95 - 0.02, side * 0.95 + 0.02, JIB_Y + 0.5, JIB_Y + 1.6, a, a + 0.04);
      p.box(k.GALV, side * 0.95 - 0.025, side * 0.95 + 0.025, JIB_Y + 1.55, JIB_Y + 1.6, 0.8, CJ - 3);
      p.box(k.GALV, side * 0.95 - 0.02, side * 0.95 + 0.02, JIB_Y + 1.0, JIB_Y + 1.04, 0.8, CJ - 3);
    }
  }
  // the counter-ballast: four concrete blocks hung at the end
  for (int kk = 0; kk < 4; kk++) {
    const double a0 = CJ - 2.8 + kk * 0.7;
    p.box(k.CONCRETE, -1.1, 1.1, JIB_Y - 2.3, JIB_Y + 0.3, a0, a0 + 0.66);
  }
  if (full)
    for (int kk = 0; kk < 4; kk++) p.box(k.RED, -1.11, 1.11, JIB_Y - 1.1, JIB_Y - 0.9, CJ - 2.8 + kk * 0.7, CJ - 2.8 + kk * 0.7 + 0.66);
  // the hoist winch (drum, gearbox, motor) and the trolley's winch at the root
  p.cyl(k.STEEL, 0, JIB_Y + 0.95, 8.8, 0.45, 1.25, 'x', 18);
  p.box(k.STEEL, 0.7, 1.05, JIB_Y + 0.5, JIB_Y + 1.35, 8.3, 9.3);
  p.box(k.GALV, -1.1, -0.7, JIB_Y + 0.5, JIB_Y + 1.25, 8.4, 9.2);
  p.cyl(k.STEEL, 0, JIB_Y + 0.55, -2.2, 0.25, 0.8, 'x', 12);
  // the hoist rope: off the winch's drum up over the sheave at the apex, down to the sheave at the jib's root
  p.cyl(k.STEEL, 0, APEX - 0.45, 0.25, 0.28, 0.08, 'x', 12);
  p.cyl(k.STEEL, 0, JIB_Y + 0.05, -1.25, 0.22, 0.08, 'x', 12);
  p.rod(k.ROPE, 0, JIB_Y + 1.38, 8.8, 0, APEX - 0.2, 0.3, 0.02, 5);
  p.rod(k.ROPE, 0, APEX - 0.72, 0.2, 0, JIB_Y + 0.27, -1.2, 0.02, 5);
  // the anemometer's mast on the apex, the obstruction lights
  p.box(k.STEEL, -0.02, 0.02, APEX + 0.15, APEX + 1.35, -0.02, 0.02);
  p.build(sw);
  glass.build(sw, false);
  // the cup anemometer: three arms with their cups, turning with the wind
  rotor->position.set(0, APEX + 1.4, 0);
  Parts rp;
  for (int kk = 0; kk < 3; kk++) {
    const double a = (kk / 3.0) * PI * 2;
    rp.bar(k.STEEL, 0, 0, 0, std::cos(a) * 0.32, 0, std::sin(a) * 0.32, 0.015);
    rp.sphere(DARK(), std::cos(a) * 0.32, 0, std::sin(a) * 0.32, 0.06);
  }
  rp.build(*rotor, false);
  sw.add(rotor);
  for (const auto& [x, y, z] : std::array<std::array<double, 3>, 3>{{{0, APEX + 0.3, 0}, {0, JIB_TOP + 0.15, zj(L - 0.2)}, {0, JIB_Y + 0.6, CJ - 0.3}}}) {
    auto light = make<Mesh>(sphereGeometry(0.12, 8, 6), k.BEACON);
    light->position.set(x, y, z);
    sw.add(light);
    lights.push_back(light);
  }
  // the load plates on the jib: what it lifts at 16 m and at its tip
  if (full) {
    const std::array<std::pair<double, std::string>, 2> plates = {{
        {TG.moment / (TG.maxLoad + TG.loss), toFixed(TG.maxLoad, 1) + " t"},
        {TG.trolleyMax, toFixed(TG.moment / TG.trolleyMax - TG.loss, 1) + " t"},
    }};
    for (const auto& [a, text] : plates) {
      auto pl = plate(text, 1.6, 0.8);
      pl->rotation.y = PI / 2;
      pl->position.set(JIB_HALF + 0.12, JIB_Y + 0.75, zj(a));
      sw.add(pl);
    }
  }
  // the trolley: a frame on the bottom chords, four wheels, two sheaves for the falls
  Group& tr = *trolley;
  Parts tp;
  tp.box(k.YELLOW, -JIB_HALF - 0.15, JIB_HALF + 0.15, TG.sheave - 0.25, TG.sheave - 0.1, -0.7, 0.7);
  for (const double side : {-1.0, 1.0}) {
    tp.box(k.YELLOW, side * (JIB_HALF + 0.12) - 0.04, side * (JIB_HALF + 0.12) + 0.04, TG.sheave - 0.25, JIB_Y + 0.25, -0.65, 0.65);
    for (const double z : {-0.45, 0.45}) tp.cyl(k.STEEL, side * (JIB_HALF - 0.02), JIB_Y + 0.22, z, 0.1, 0.06, 'x', 10);
    tp.cyl(k.STEEL, side * 0.15, TG.sheave, 0, 0.22, 0.07, 'x', 14);
  }
  tp.build(tr);
  sw.add(trolley);
  // the hook block on its two falls, and the rope along the jib
  hook = hookBlock(0.55, 0.9, 2, detail);
  inner->add(hook);
  for (int i = 0; i < 2; i++) falls.push_back(std::make_unique<crane_render::Line>(*inner, 0.022));
  jibRope = std::make_unique<crane_render::Line>(sw, 0.02);
  hoistRope = std::make_unique<crane_render::Line>(sw, 0.02);
}

TowerCraneModel::~TowerCraneModel() = default;

void TowerCraneModel::update(const CarVisualState& s, double dt, double now) { flash(now); }

void TowerCraneModel::flash(double now) {
  const bool on = jsMod(std::floor(now * 1.1), 2) == 0;
  for (const auto& l : lights) l->material = on ? K().BEACON : K().BEACON_OFF;
}

void TowerCraneModel::pose(const Crane& c, double now) {
  poseTip(*tip, *inner, c);
  const Joints& q = c.q;
  slewing->rotation.y = q.slew;
  trolley->position.z = -q.trolley;
  // the anemometer turns with the wind at the top
  rotor->rotation.y = -now * (0.4 + c.lmi.wind * 0.9);
  // (the rope runs from the root's sheave to the trolley, down to the hook block and up again, and on to its end at the jib's tip)
  hoistRope->set(tmp.set(0, JIB_Y + 0.05, -1.25), tmp2.set(0.1, TG.sheave + 0.12, -(q.trolley - 0.25)));
  jibRope->set(tmp.set(-0.1, TG.sheave + 0.12, -(q.trolley + 0.25)), tmp2.set(0, JIB_Y + 0.1, -(TG.jibLength - 0.3)));
  // the hook block where the rope has it, hanging plumb under its sheaves
  inner->updateMatrixWorld(true);
  // (`local` is `tmp`: as in the original, the falls' ends below are worked out in it, one after the other)
  Vector3& local = inner->worldToLocal(tmp.set(c.hook.x, c.hook.y, c.hook.z));
  hook->position.copy(local);
  hook->rotation.set(0, q.slew, 0);
  const double s = std::sin(q.slew);
  const double co = std::cos(q.slew);
  for (int i = 0; i < 2; i++) {
    const double side = i == 0 ? -0.15 : 0.15;
    // (the sheave on the trolley, turned with the jib)
    tmp2.set(-q.trolley * s + side * co, TG.sheave, -q.trolley * co - side * s);
    const Vector3& b = tmp.set(local.x + side * co, local.y - 0.05, local.z - side * s);
    falls[i]->set(tmp2, b);
  }
}

Vector3& TowerCraneModel::operatorEye(Vector3& out) {
  out.copy(cabEye);
  return slewing->localToWorld(out);
}

// ------------------------------------------------------------------------------------ the mobile crane

namespace {
const MobileGeometry& MG = MOBILE_GEOMETRY;
const OutriggerSpec& MO() { return *MOBILE_CRANE.outriggers; }
}  // namespace

MobileCraneModel::MobileCraneModel(const CraneDetail& detail) : CraneModel("mobile", Vector3(-0.88, 2.55, -3.25)) {
  const CraneMaterials& k = K();
  root->name = "mobile crane";
  std::tie(tip, inner) = tipping(*root);
  inner->add(body);
  const bool full = detail == "full";
  Group& body = *this->body;
  Parts p;
  const double hw = 1.27;
  // ---- the carrier: the frame, the decks over the axles, the driver's cab on the left, the engine at the rear
  p.box(k.STEEL, -0.55, 0.55, 0.75, 1.2, -3.8, 4.6);
  p.box(k.WHITE, -hw, hw, 1.2, 1.32, -2.1, 4.6);
  for (const double side : {-1.0, 1.0}) p.box(k.WHITE, side * hw - 0.02 * side, side * (hw - 0.4), 0.95, 1.32, -2.1, 4.6);
  // (the cab: white, glass to the front and the sides; the boom rests beside it on the right)
  const double c0 = -hw;
  const double c1 = -0.5;
  p.box(k.WHITE, c0, c1, 0.95, 1.85, -3.95, -2.25);
  p.box(k.WHITE, c0, c1, 2.95, 3.15, -3.95, -2.25);
  p.box(k.WHITE, c0, c1, 1.85, 2.95, -2.35, -2.25);
  for (const auto& [x, z] : std::array<std::array<double, 2>, 4>{{{c0, -3.95}, {c1, -3.95}, {c0, -2.3}, {c1, -2.3}}}) p.box(k.WHITE, x - 0.04, x + 0.04, 1.85, 2.95, z - 0.04, z + 0.04);
  p.box(k.WHITE, c1, hw, 0.95, 1.45, -3.95, -2.25);
  p.box(DARK(), -hw + 0.05, hw - 0.05, 0.55, 0.95, -3.98, -3.7);
  p.box(k.WHITE, -1.0, 1.0, 1.32, 1.75, 2.9, 4.5);
  p.box(DARK(), -0.95, 0.95, 1.4, 1.65, 4.5, 4.52);
  // the boom's rest at the front right
  p.box(k.STEEL, -0.2, 0.2, 1.32, 2.36, -3.55, -3.35);
  // the turntable's lower ring
  p.cyl(k.STEEL, 0, 1.9, MOBILE_CRANE.slewZ, 1.05, 0.25, 'y', 24);
  // the outrigger boxes, front and rear
  for (const double z : {MO().beams[0].z, MO().beams[2].z}) p.box(k.STEEL, -1.25, 1.25, 0.72, 1.18, z - 0.3, z + 0.3);
  // steps and tool boxes along the sides
  if (full) {
    for (const double side : {-1.0, 1.0}) {
      p.box(k.STEEL, side * 1.0, side * 1.28, 0.62, 1.08, -1.25, -0.35);
      p.box(k.STEEL, side * 1.0, side * 1.28, 0.62, 1.08, 3.3, 3.7);
      p.box(k.GALV, side * 1.05, side * 1.29, 0.5, 0.54, -2.25, -2.0);
    }
    p.box(k.GALV, -1.25, -1.0, 0.65, 0.69, -2.85, -2.55);
  }
  Parts glass;
  glass.box(GLASS(), c0 + 0.04, c1 - 0.04, 1.85, 2.95, -3.955, -3.945);
  glass.box(GLASS(), c0 - 0.005, c0 + 0.005, 1.85, 2.95, -3.9, -2.4);
  glass.box(GLASS(), c1 - 0.005, c1 + 0.005, 1.85, 2.95, -3.9, -2.4);
  // lamps
  for (const int side : {-1, 1}) {
    p.box(LAMP(), side * 0.95 - 0.12, side * 0.95 + 0.12, 0.65, 0.8, -3.99, -3.97);
    auto stop = make<Mesh>(boxGeometry(0.24, 0.16, 0.03), TAIL());
    stop->position.set(side * 0.95, 0.9, 4.62);
    body.add(stop);
    lamps.brake.push_back(stop);
    for (const double z : {-3.99, 4.62}) {
      auto ind = make<Mesh>(boxGeometry(0.14, 0.12, 0.03), AMBER());
      ind->position.set(side * 1.15, 0.9, z);
      body.add(ind);
      lamps.indicators.push_back({ind, side});
    }
  }
  p.build(body);
  glass.build(body, false);
  // the amber beacons on the cab's roof
  for (const double x : {-1.15, -0.62}) {
    auto b = make<Mesh>(cylinderGeometry(0.08, 0.08, 0.14, 10), AMBER());
    b->position.set(x, 3.23, -3.1);
    body.add(b);
    beacons.push_back(b);
  }
  // ---- the wheels: three axles of single tyres
  for (const double z : MOBILE_AXLES) {
    for (const double side : {-1.0, 1.0}) {
      auto w = wheelGroup(0.69, 0.39);
      w->position.set(side * 1.04, 0.69, z);
      body.add(w);
      wheels.push_back(w);
    }
  }
  // (`wheelY[i] ?? 0.69` before the first picture)
  wheelY.assign(wheels.size(), 0.69);
  // ---- the outriggers: a beam each, its jack (housing and rod), its pad, the mat under it
  for (const auto& b : MO().beams) {
    auto g = make<Group>();
    g->position.set(0, 0, b.z);
    Parts bp;
    // (the beam reaches from inside the box out to its jack; its outer end is at the group's origin, moved out)
    bp.box(k.YELLOW, -b.side * 1.9, 0, 0.78, 1.12, -0.2, 0.2);
    bp.cyl(k.YELLOW, 0, 0.85, 0, 0.13, 0.62, 'y', 12);
    bp.box(k.YELLOW, -0.16, 0.16, 1.1, 1.22, -0.16, 0.16);
    bp.build(*g);
    body.add(g);
    beams.push_back(g);
    auto rod = make<Mesh>(cylinderGeometry(0.08, 0.08, 1, 10)->translate(0, -0.5, 0).shared_from_this(), k.CHROME);
    rod->castShadow = true;
    g->add(rod);
    rods.push_back(rod);
    auto pad = make<Group>();
    Parts pp;
    pp.cyl(k.PAD, 0, 0.04, 0, 0.3, 0.08, 'y', 16);
    pp.sphere(k.STEEL, 0, 0.1, 0, 0.1);
    pp.build(*pad);
    g->add(pad);
    pads.push_back(pad);
    // (the mat lies on the ground, under the pad – it stays where it was put)
    auto mat = make<Mesh>(boxGeometry(1.0, 0.08, 1.0), k.TIMBER);
    mat->castShadow = true;
    mat->receiveShadow = true;
    body.add(mat);
    mats.push_back(mat);
  }
  // ---- the superstructure: it turns on the turntable
  Group& up = *upper;
  up.position.set(MOBILE_CRANE.slewX, 0, MOBILE_CRANE.slewZ);
  body.add(upper);
  Parts u;
  // (along the boom: −z; to its right: +x)
  const auto a = [](double v) { return -v; };
  u.cyl(k.STEEL, 0, 2.08, 0, 1.0, 0.12, 'y', 24);
  u.box(k.YELLOW, -0.95, 0.95, 2.15, 2.62, a(1.4), a(-2.3));
  // the crane cab on the left of the boom
  u.box(k.WHITE, -1.27, -0.55, 2.2, 2.35, a(2.15), a(0.2));
  u.box(k.WHITE, -1.27, -0.55, 3.95, 4.1, a(2.05), a(0.2));
  u.box(k.WHITE, -1.27, -0.55, 2.35, 3.95, a(0.3), a(0.2));
  for (const auto& [x, z] : std::array<std::array<double, 2>, 4>{{{-1.27, 2.05}, {-0.55, 2.05}, {-1.27, 0.25}, {-0.55, 0.25}}}) u.box(k.WHITE, x - 0.035, x + 0.035, 2.35, 3.95, a(z) - 0.035, a(z) + 0.035);
  u.box(k.STEEL, -1.15, -0.65, 2.35, 2.75, a(1.4), a(0.6));
  Parts ug;
  ug.box(GLASS(), -1.24, -0.58, 2.4, 3.92, a(2.15) - 0.005, a(2.15) + 0.005);
  ug.box(GLASS(), -1.275, -1.265, 2.4, 3.92, a(2.0), a(0.3));
  ug.box(GLASS(), -0.555, -0.545, 2.9, 3.92, a(2.0), a(0.3));
  ug.box(GLASS(), -1.22, -0.6, 4.095, 4.105, a(1.95), a(0.8));
  // the boom's foot bearings, the luffing cylinder's lower bearing
  for (const double side : {-1.0, 1.0}) u.box(k.YELLOW, side * 0.5 - 0.06, side * 0.5 + 0.06, 2.55, MG.footY + 0.25, a(MG.footA) - 0.35, a(MG.footA) + 0.35);
  u.cyl(k.STEEL, 0, MG.footY, a(MG.footA), 0.12, 1.15, 'x', 12);
  u.box(k.YELLOW, -0.3, 0.3, 1.85, 2.2, a(MG.cylA) - 0.25, a(MG.cylA) + 0.25);
  // the hoist winch behind the boom's foot
  u.cyl(k.STEEL, 0, 2.95, a(-1.75), 0.36, 0.9, 'x', 16);
  u.box(k.STEEL, 0.47, 0.72, 2.6, 3.25, a(-1.45), a(-2.05));
  // the counterweight: a base plate and three slabs
  for (int kk = 0; kk < 4; kk++) u.box(kk == 0 ? k.STEEL : k.CONCRETE, -1.25, 1.25, 1.85 + kk * 0.26, 2.09 + kk * 0.26, a(-2.55), a(-4.05));
  if (full) u.box(k.RED, -1.26, 1.26, 2.55, 2.62, a(-2.55), a(-4.06));
  u.build(up);
  ug.build(up, false);
  // ---- the boom: four sections, the head with its sheaves – pivoting at its foot
  Group& bm = *boom;
  bm.position.set(0, MG.footY, a(MG.footA));
  up.add(boom);
  struct Sec {
    double w, h, len, start;
  };
  const std::array<Sec, 4> secs = {{
      {0.85, 0.95, 9.7, -0.5},
      {0.72, 0.8, 8.6, 0},
      {0.6, 0.66, 8.6, 0},
      {0.5, 0.54, 8.6, 0},
  }};
  for (int kk = 0; kk < 4; kk++) {
    const Sec& sc = secs[kk];
    auto g = make<Group>();
    Parts sp;
    // (the boom points along −z in its own frame: the base section from behind its pin to 9.2 m; the others' ends are at their groups' origins, their length reaching back inside)
    if (kk == 0) {
      sp.box(k.YELLOW, -sc.w / 2, sc.w / 2, -sc.h / 2, sc.h / 2, -9.2, -sc.start);
      sp.box(k.YELLOW, -0.2, 0.2, -sc.h / 2 - 0.3, -sc.h / 2, -MG.cylOnBoom - 0.35, -MG.cylOnBoom + 0.35);
    } else sp.box(k.YELLOW, -sc.w / 2, sc.w / 2, -sc.h / 2, sc.h / 2, 0, sc.len);
    sp.build(*g);
    bm.add(g);
    sections.push_back(g);
  }
  if (full) {
    auto name = plate("40 t", 2.2, 0.6);
    name->rotation.y = -PI / 2;
    name->position.set(0.43, 0, -5.2);
    sections[0]->add(name);
  }
  // the head: plates and sheaves at the end of the last section
  Group& hd = *head;
  Parts hp;
  hp.box(k.YELLOW, -0.3, 0.3, -0.45, 0.3, -0.1, 0.35);
  for (const double x : {-0.18, -0.06, 0.06, 0.18}) hp.cyl(k.STEEL, x, -0.2, -0.45, 0.3, 0.07, 'x', 14);
  hp.box(k.YELLOW, -0.28, -0.24, -0.6, 0.2, -0.75, -0.1);
  hp.box(k.YELLOW, 0.24, 0.28, -0.6, 0.2, -0.75, -0.1);
  hp.build(hd);
  sections[3]->add(head);
  // the luffing cylinder: housing and rod, each placed between its pins every picture
  cylHousing = make<Mesh>(cylinderGeometry(0.2, 0.2, 1, 12)->translate(0, 0.5, 0).shared_from_this(), k.YELLOW);
  cylRod = make<Mesh>(cylinderGeometry(0.13, 0.13, 1, 12)->translate(0, 0.5, 0).shared_from_this(), k.CHROME);
  cylHousing->castShadow = cylRod->castShadow = true;
  up.add(cylHousing, cylRod);
  winchRope = std::make_unique<crane_render::Line>(up, 0.016);
  // the hook block on four falls
  hook = hookBlock(0.6, 1.0, 4, detail);
  inner->add(hook);
  for (int i = 0; i < 4; i++) falls.push_back(std::make_unique<crane_render::Line>(*inner, 0.018));
}

MobileCraneModel::~MobileCraneModel() = default;

void MobileCraneModel::update(const CarVisualState& s, double dt, double now) {
  lamps.update(s, root.get());
  for (size_t i = 0; i < wheels.size(); i++) {
    // (the physics knows four wheels: the middle and the last axle follow the rear pair)
    const size_t k = i < 2 ? i : 2 + (i % 2);
    Group& w = *wheels[i];
    wheelY[i] = at(s.wheelY, k, 0.69);
    w.position.y = wheelY[i];
    w.rotation.set(-at(s.wheelSpin, k, 0), i < 2 ? -at(s.wheelSteer, k, 0) : 0, 0, Euler::YXZ);
  }
  // the beacons flash while the crane works
  const bool on = operating && jsMod(std::floor(now * 2.5), 2) == 0;
  for (const auto& b : beacons) b->material = on ? AMBER() : TAIL();
}

void MobileCraneModel::pose(const Crane& c, double now) {
  poseTip(*tip, *inner, c);
  operating = c.time > 0;
  // the carrier lifted by its jacks, levelled against the ground's slope
  const double lift = c.lift;
  body->position.y = lift;
  body->rotation.set(-std::atan(c.levelFix.gz), 0, std::atan(c.levelFix.gx), Euler::XYZ);
  // (the wheels hang down out of their springs as it goes up, then leave the ground)
  const double droop = jsMin(lift, 0.12);
  for (size_t i = 0; i < wheels.size(); i++) wheels[i]->position.y = at(wheelY, i, 0.69) - droop;
  // outriggers: the beams out, the jacks down onto their pads, the mats under them
  const OutriggerSpec& mo = MO();
  for (size_t i = 0; i < mo.beams.size(); i++) {
    const auto& b = mo.beams[i];
    const double x = b.side * (mo.padIn + c.beams[i] * (mo.padOut - mo.padIn));
    Group& g = *beams[i];
    g.position.x = x;
    // (the pad's sole is `clearance` above the ground with the jack drawn up; the rod reaches down to it)
    const double stroke = c.jacks[i];
    const double sole = mo.clearance - stroke;
    Mesh& rod = *rods[i];
    rod.position.set(0, 0.7, 0);
    rod.scale.set(1, jsMax(0.01, 0.7 - sole - 0.12), 1);
    pads[i]->position.set(0, sole, 0);
    // the mat is laid on the ground under the pad once the beam is out
    Mesh& mat = *mats[i];
    mat.visible = c.beams[i] > 0.4;
    mat.position.set(x, c.padGround[i] - lift, b.z);
  }
  // the superstructure, the boom, its sections and the luffing cylinder
  const Joints& q = c.q;
  upper->rotation.y = q.slew;
  boom->rotation.x = q.luff;
  const double L = boomLength(q.tele);
  const double extra = (L - MG.length0) / 3;
  for (int k = 1; k < 4; k++) {
    // (each section's end: 13 cm beyond the one it comes out of, and out by its share of the telescoping)
    const double end = 9.2 + 0.13 * k + extra * k;
    sections[k]->position.z = -end;
  }
  head->position.z = 0;
  // the luffing cylinder from its bearing on the turntable to its point under the boom
  const double ax = 0;
  const double ay = 1.95;
  const double az = -MG.cylA;
  const double d = MG.cylOnBoom;
  const double bx = 0;
  const double by = MG.footY + d * std::sin(q.luff) - 0.55 * std::cos(q.luff);
  const double bz = -MG.footA - d * std::cos(q.luff) - 0.55 * std::sin(q.luff);
  tmp.set(bx - ax, by - ay, bz - az);
  const double len = tmp.length();
  tmp.divideScalar(len);
  tmpQ.setFromUnitVectors(UP, tmp);
  // (housing and rod 2.4 m each: the rod comes out of the housing as the boom goes up)
  const double part = jsMin(len, 2.4);
  cylHousing->position.set(ax, ay, az);
  cylHousing->quaternion.copy(tmpQ);
  cylHousing->scale.set(1, part, 1);
  const double out = jsMax(0, len - 2.4);
  cylRod->position.set(ax + tmp.x * out, ay + tmp.y * out, az + tmp.z * out);
  cylRod->quaternion.copy(tmpQ);
  cylRod->scale.set(1, part, 1);
  // the hoist rope along the boom, from the top of the winch's drum to the head's sheaves
  const double lh = L + MG.head;
  winchRope->set(tmp.set(0, 3.31, 1.75), tmp2.set(0, MG.footY + lh * std::sin(q.luff) + 0.12 * std::cos(q.luff), -MG.footA - lh * std::cos(q.luff) + 0.12 * std::sin(q.luff)));
  // the hook block where the rope has it, the falls up to the head's sheaves
  inner->updateMatrixWorld(true);
  // (`local` is `tmp`: as in the original, the falls' ends below are worked out in it, one after the other)
  Vector3& local = inner->worldToLocal(tmp.set(c.hook.x, c.hook.y, c.hook.z));
  hook->position.copy(local);
  hook->rotation.set(0, q.slew, 0);
  const auto hd = headOf("mobile", q);
  const double sn = std::sin(q.slew);
  const double co = std::cos(q.slew);
  for (int i = 0; i < 4; i++) {
    const double side = -0.18 + i * 0.12;
    const double hx = MOBILE_CRANE.slewX - hd.a * sn + side * co;
    const double hz = MOBILE_CRANE.slewZ - hd.a * co - side * sn;
    tmp2.set(hx, hd.y + lift - 0.2, hz);
    falls[i]->set(tmp2, tmp.set(local.x + side * co * 0.8, local.y - 0.15, local.z - side * sn * 0.8));
  }
}

Vector3& MobileCraneModel::operatorEye(Vector3& out) {
  // (the cab is on the left of the boom: the boom points along −z of the superstructure)
  out.set(-0.9, 3.55, -1.4);
  return upper->localToWorld(out);
}

// ------------------------------------------------------------------------------------ the loader crane

namespace {
const LoaderGeometry& LG = LOADER_GEOMETRY;
const OutriggerSpec& LO() { return *LOADER_CRANE.outriggers; }
}  // namespace

LoaderCraneModel::LoaderCraneModel(const CraneDetail& detail) : CraneModel("loader", Vector3(-0.62, 2.38, -3.3)) {
  const CraneMaterials& k = K();
  root->name = "loader crane";
  std::tie(tip, inner) = tipping(*root);
  inner->add(body);
  const bool full = detail == "full";
  Group& body = *this->body;
  Parts p;
  const double hw = 1.25;
  // ---- the truck: chassis, cab, flatbed with its boards
  p.box(k.STEEL, -0.5, 0.5, 0.72, 1.02, -3.7, LOADER_BED.z1);
  const double cz0 = -3.9;
  const double cz1 = -2.35;
  p.box(k.WHITE, -hw, hw, 1.0, 1.9, cz0, cz1);
  p.box(k.WHITE, -hw, hw, 2.85, 3.08, cz0, cz1 + 0.1);
  p.box(k.WHITE, -hw, hw, 1.9, 2.85, cz1 - 0.1, cz1);
  for (const auto& [x, z] : std::array<std::array<double, 2>, 2>{{{-hw, cz0}, {hw, cz0}}}) p.box(k.WHITE, x - 0.05, x + 0.05, 1.9, 2.85, z, z + 0.08);
  p.box(DARK(), -hw + 0.05, hw - 0.05, 0.5, 1.0, cz0 - 0.03, cz0 + 0.3);
  p.box(DARK(), -0.75, 0.75, 1.1, 1.6, cz0 - 0.012, cz0 + 0.02);
  Parts glass;
  glass.box(GLASS(), -hw + 0.06, hw - 0.06, 1.9, 2.85, cz0 + 0.01, cz0 + 0.02);
  glass.box(GLASS(), -hw - 0.005, -hw + 0.005, 1.9, 2.75, cz0 + 0.1, cz1 - 0.15);
  glass.box(GLASS(), hw - 0.005, hw + 0.005, 1.9, 2.75, cz0 + 0.1, cz1 - 0.15);
  // the flatbed: its floor, the boards round it (the front one high, as a load guard)
  const auto& B = LOADER_BED;
  p.box(k.TIMBER, -B.hw, B.hw, B.y - 0.08, B.y, B.z0, B.z1);
  p.box(k.STEEL, -B.hw, B.hw, B.y - 0.25, B.y - 0.08, B.z0, B.z1);
  p.box(k.GALV, -B.hw, B.hw, B.y, B.y + 1.1, B.z0 - 0.06, B.z0);
  for (const double side : {-1.0, 1.0}) p.box(k.GALV, side * B.hw - 0.03, side * B.hw + 0.03, B.y, B.y + 0.4, B.z0, B.z1);
  p.box(k.GALV, -B.hw, B.hw, B.y, B.y + 0.4, B.z1 - 0.03, B.z1 + 0.03);
  // mudguards, the rear lamps' bar, the underrun bar
  for (const double side : {-1.0, 1.0})
    for (const double z : LOADER_AXLES) p.box(DARK(), side * 0.75, side * 1.28, 1.02, 1.08, z - 0.6, z + 0.6);
  p.box(k.STEEL, -1.15, 1.15, 0.5, 0.62, LOADER_BED.z1 - 0.05, LOADER_BED.z1 + 0.05);
  for (const int side : {-1, 1}) {
    p.box(LAMP(), side * 0.9 - 0.12, side * 0.9 + 0.12, 0.62, 0.78, cz0 - 0.02, cz0);
    auto stop = make<Mesh>(boxGeometry(0.24, 0.16, 0.03), TAIL());
    stop->position.set(side * 0.9, 0.78, LOADER_BED.z1 + 0.06);
    body.add(stop);
    lamps.brake.push_back(stop);
    for (const double z : {cz0 - 0.02, LOADER_BED.z1 + 0.06}) {
      auto ind = make<Mesh>(boxGeometry(0.14, 0.12, 0.03), AMBER());
      ind->position.set(side * 1.1, 0.78, z);
      body.add(ind);
      lamps.indicators.push_back({ind, side});
    }
  }
  // ---- the crane's base on the chassis, with the housing of the outrigger beams and the control levers
  const double sz = LOADER_CRANE.slewZ;
  p.box(k.YELLOW, -0.62, 0.62, 1.02, 1.5, sz - 0.4, sz + 0.4);
  p.box(k.YELLOW, -1.2, 1.2, 0.68, 1.02, sz - 0.18, sz + 0.18);
  if (full) {
    for (const double side : {-1.0, 1.0}) {
      p.box(k.STEEL, side * 1.2 - 0.06, side * 1.2 + 0.06, 1.05, 1.35, sz - 0.25, sz + 0.25);
      for (int kk = 0; kk < 4; kk++) p.box(DARK(), side * 1.28 - 0.015, side * 1.28 + 0.015, 1.3, 1.55, sz - 0.18 + kk * 0.12, sz - 0.16 + kk * 0.12);
    }
  }
  p.build(body);
  glass.build(body, false);
  // ---- wheels: single at the front, twin at the rear
  for (const double z : LOADER_AXLES) {
    for (const double side : {-1.0, 1.0}) {
      auto w = wheelGroup(0.5, z < 0 ? 0.32 : 0.62);
      w->position.set(side * (z < 0 ? 0.95 : 0.9), 0.5, z);
      body.add(w);
      wheels.push_back(w);
    }
  }
  // ---- the outriggers: a beam each side, its jack and pad
  for (const auto& b : LO().beams) {
    auto g = make<Group>();
    g->position.set(0, 0, b.z);
    Parts bp;
    bp.box(k.YELLOW, -b.side * 1.3, 0, 0.72, 0.98, -0.13, 0.13);
    bp.cyl(k.YELLOW, 0, 0.78, 0, 0.1, 0.45, 'y', 12);
    bp.build(*g);
    body.add(g);
    beams.push_back(g);
    auto rod = make<Mesh>(cylinderGeometry(0.06, 0.06, 1, 10)->translate(0, -0.5, 0).shared_from_this(), k.CHROME);
    rod->castShadow = true;
    g->add(rod);
    rods.push_back(rod);
    auto pad = make<Group>();
    Parts pp;
    pp.box(k.PAD, -0.22, 0.22, 0, 0.06, -0.22, 0.22);
    pp.sphere(k.STEEL, 0, 0.08, 0, 0.07);
    pp.build(*pad);
    g->add(pad);
    pads.push_back(pad);
  }
  // ---- the column, turning on the base
  Group& col = *column;
  col.position.set(LOADER_CRANE.slewX, 0, sz);
  body.add(column);
  Parts cp;
  cp.cyl(k.STEEL, 0, 1.56, 0, 0.42, 0.12, 'y', 18);
  cp.box(k.YELLOW, -0.28, 0.28, 1.6, LG.pinY + 0.22, -0.28, 0.22);
  cp.box(k.YELLOW, -0.32, 0.32, LG.pinY - 0.18, LG.pinY + 0.12, -LG.pinA - 0.15, -LG.pinA + 0.15);
  cp.box(k.YELLOW, -0.18, 0.18, 1.5, 1.75, -0.55, -0.25);
  cp.build(col);
  // the inner boom on its pin
  Group& ib = *innerBoom;
  ib.position.set(0, LG.pinY, -LG.pinA);
  col.add(innerBoom);
  Parts ip;
  ip.box(k.YELLOW, -0.2, 0.2, -0.24, 0.24, -LG.inner, 0.18);
  ip.cyl(k.STEEL, 0, 0, -LG.inner, 0.1, 0.5, 'x', 10);
  ip.build(ib);
  // the outer boom at the inner one's end, its three extensions
  Group& ob = *outerBoom;
  ob.position.set(0, 0, -LG.inner);
  ib.add(outerBoom);
  Parts op;
  op.box(k.YELLOW, -0.18, 0.18, -0.21, 0.21, -LG.outer0, 0.42);
  op.box(k.YELLOW, -0.14, 0.14, 0.15, 0.42, 0.2, 0.55);
  op.build(ob);
  const double extLen = (LG.outer1 - LG.outer0) / 3;
  for (int kk = 0; kk < 3; kk++) {
    auto g = make<Group>();
    Parts ep;
    const double w = 0.15 - kk * 0.025;
    const double h = 0.18 - kk * 0.025;
    // (each extension's end at its group's origin, its length back inside the boom)
    ep.box(k.YELLOW, -w, w, -h, h, 0, LG.outer0 - 0.35);
    if (kk == 2) ep.box(k.STEEL, -0.08, 0.08, -0.25, -0.1, -0.12, 0.05);
    ep.build(*g);
    ob.add(g);
    ext.push_back(g);
    g->userData["len"] = extLen;
  }
  // the cylinders: lift (column to inner boom), fold (inner boom to the outer boom's lever)
  const auto cyl = [&k](double r) {
    auto m = make<Mesh>(cylinderGeometry(r, r, 1, 10)->translate(0, 0.5, 0).shared_from_this(), k.YELLOW);
    m->castShadow = true;
    return m;
  };
  const auto rodMesh = [&k](double r) {
    auto m = make<Mesh>(cylinderGeometry(r, r, 1, 10)->translate(0, 0.5, 0).shared_from_this(), k.CHROME);
    m->castShadow = true;
    return m;
  };
  liftCyl = {cyl(0.12), rodMesh(0.075)};
  col.add(liftCyl[0], liftCyl[1]);
  foldCyl = {cyl(0.1), rodMesh(0.065)};
  ib.add(foldCyl[0], foldCyl[1]);
  // the hook on its link
  hook = hookBlock(0.28, 0.45, 1, detail);
  inner->add(hook);
  link = std::make_unique<crane_render::Line>(*inner, 0.03, k.STEEL);
}

LoaderCraneModel::~LoaderCraneModel() = default;

void LoaderCraneModel::update(const CarVisualState& s, double dt, double now) {
  lamps.update(s, root.get());
  for (size_t i = 0; i < wheels.size(); i++) {
    Group& w = *wheels[i];
    w.position.y = at(s.wheelY, i, 0.5);
    w.rotation.set(-at(s.wheelSpin, i, 0), i < 2 ? -at(s.wheelSteer, i, 0) : 0, 0, Euler::YXZ);
  }
}

void LoaderCraneModel::ram(const std::array<std::shared_ptr<Mesh>, 2>& parts, const Vector3& a, const Vector3& b, double housing) {
  tmp2.subVectors(b, a);
  const double len = tmp2.length();
  tmp2.divideScalar(len != 0 ? len : 1);
  tmpQ.setFromUnitVectors(UP, tmp2);
  Mesh& h = *parts[0];
  Mesh& r = *parts[1];
  h.position.copy(a);
  h.quaternion.copy(tmpQ);
  h.scale.set(1, jsMin(len, housing), 1);
  r.position.copy(a).addScaledVector(tmp2, jsMax(0, len - housing));
  r.quaternion.copy(tmpQ);
  r.scale.set(1, housing, 1);
}

void LoaderCraneModel::pose(const Crane& c, double now) {
  poseTip(*tip, *inner, c);
  body->position.y = c.lift;
  body->rotation.set(0, 0, std::atan(c.levelFix.gx));
  const OutriggerSpec& lo = LO();
  for (size_t i = 0; i < lo.beams.size(); i++) {
    const auto& b = lo.beams[i];
    const double x = b.side * (lo.padIn + c.beams[i] * (lo.padOut - lo.padIn));
    beams[i]->position.x = x;
    const double len = 0.2 + c.jacks[i];
    rods[i]->position.set(0, 0.56, 0);
    rods[i]->scale.set(1, len, 1);
    pads[i]->position.set(0, 0.56 - len - 0.06, 0);
  }
  const Joints& q = c.q;
  column->rotation.y = q.slew;
  // (the booms point along −z: a rotation about x by +angle raises them)
  innerBoom->rotation.x = q.inner;
  outerBoom->rotation.x = q.outer;
  const double e = outerLength(q.ext) - LG.outer0;
  for (size_t k = 0; k < ext.size(); k++) {
    // (the last one's end is the head the hook hangs from: the outer boom's length)
    ext[k]->position.z = -(LG.outer0 - 0.1 * (2 - static_cast<double>(k)) + (e * (k + 1)) / 3);
  }
  // the lift cylinder: from the column's front low down to under the inner boom
  ram(liftCyl, tmp.set(0, 1.3, -0.5), Vector3(0, LG.pinY + 1.05 * std::sin(q.inner) - 0.24 * std::cos(q.inner), -LG.pinA - 1.05 * std::cos(q.inner) - 0.24 * std::sin(q.inner)), 0.7);
  // the folding cylinder: on top of the inner boom to the outer boom's lever behind its pin
  Vector3 lev = Vector3(0, 0.42, 0.45).applyAxisAngle(Vector3(1, 0, 0), q.outer);
  lev.z -= LG.inner;
  ram(foldCyl, tmp.set(0, 0.32, -0.45), lev, 0.95);
  // the hook where its link has it
  inner->updateMatrixWorld(true);
  const Vector3& local = inner->worldToLocal(tmp.set(c.hook.x, c.hook.y, c.hook.z));
  hook->position.copy(local);
  hook->rotation.set(0, q.slew, 0);
  const auto hd = headOf("loader", q);
  const double sn = std::sin(q.slew);
  const double co = std::cos(q.slew);
  tmp2.set(LOADER_CRANE.slewX - hd.a * sn, hd.y + c.lift - 0.2, LOADER_CRANE.slewZ - hd.a * co);
  link->set(tmp2, Vector3(local.x, local.y + 0.05, local.z));
}

Vector3& LoaderCraneModel::operatorEye(Vector3& out) {
  // (with the radio control, beside the truck on the left, a little behind the crane)
  out.set(-3.3, 1.72, LOADER_CRANE.slewZ + 1.4);
  return inner->localToWorld(out);
}

std::unique_ptr<CraneModel> makeCraneModel(const CraneType& type, const CraneDetail& detail) {
  if (type == "tower") return std::make_unique<TowerCraneModel>(detail);
  if (type == "mobile") return std::make_unique<MobileCraneModel>(detail);
  return std::make_unique<LoaderCraneModel>(detail);
}

}  // namespace fs

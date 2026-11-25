// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/theoryprops.ts @ v1.7.7
#include "render/theoryprops.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "engine/curves.h"
#include "engine/geometries.h"
#include "game/theoryscenes.h"
#include "game/theorystage.h"

namespace fs {

namespace {
constexpr double PI_ = 3.14159265358979323846;

struct Mats {
  MaterialPtr trunk = lambert({.color = Color(0x5a4330)});
  MaterialPtr crown = lambert({.color = Color(0x2f4f2a)});
  MaterialPtr white = lambert({.color = Color(0xeeeeea)});
  MaterialPtr black = lambert({.color = Color(0x1c1c1e)});
  MaterialPtr amber = basic({.color = Color(0xffa21a)});
  MaterialPtr red = basic({.color = Color(0xd8261c), .side = Side::Double});
  MaterialPtr green = basic({.color = Color(0x1f9a4a), .side = Side::Double});
  MaterialPtr barRed = lambert({.color = Color(0xc8231b)});
  MaterialPtr orange = lambert({.color = Color(0xf26a12)});
  MaterialPtr earth = lambert({.color = Color(0x6b5338)});
  MaterialPtr frame = lambert({.color = Color(0x7d1f2b)});
  MaterialPtr tyre = lambert({.color = Color(0x18181a)});
  MaterialPtr wood = lambert({.color = Color(0x8a8f96)});
  MaterialPtr concrete = lambert({.color = Color(0xb4b3ab), .transparent = true, .opacity = 0.42, .side = Side::Double, .depthWrite = false});
  MaterialPtr portal = lambert({.color = Color(0x9c9b94)});
  MaterialPtr lamp = basic({.color = Color(0xfff2c8)});
  MaterialPtr beam = basic({.vertexColors = true,
                            .transparent = true,
                            .depthWrite = false,
                            .blending = Blending::Additive,
                            .polygonOffset = true,
                            .polygonOffsetFactor = -6,
                            .polygonOffsetUnits = -18});
};

const Mats& MAT() {
  static const Mats* m = new Mats();
  return *m;
}

/** the shapes all lessons share (made when first needed) */
struct Shapes {
  GeometryPtr trunk, crown, post, band, reflector, triangle, leg, board, foot, cone, coneBand, heap, tube, wheel, seat, pole, disc, ring, low, high;
};

/** The light of headlights on the road: from the nose `len` m ahead, widening, brightest near the car. */
GeometryPtr beamShape(double len, double wide) {
  std::vector<float> pos;
  std::vector<float> col;
  const int n = 8;
  const Color c(0xfff0c8);
  for (int i = 0; i < n; i++) {
    const double a = static_cast<double>(i) / n;
    const double b = static_cast<double>(i + 1) / n;
    const double wa = 1.0 + (wide - 1.0) * a;
    const double wb = 1.0 + (wide - 1.0) * b;
    const double ka = 0.34 * std::pow(1 - a, 1.4);
    const double kb = 0.34 * std::pow(1 - b, 1.4);
    // (a quad from a to b: two triangles, facing up)
    const std::array<std::array<double, 3>, 4> pts = {{{-wa / 2, -a * len, ka}, {wa / 2, -a * len, ka}, {wb / 2, -b * len, kb}, {-wb / 2, -b * len, kb}}};
    for (const int k : {0, 2, 1, 0, 3, 2}) {
      const auto [x, z, f] = pts[static_cast<size_t>(k)];
      pos.insert(pos.end(), {static_cast<float>(x), 0.0f, static_cast<float>(z)});
      col.insert(col.end(), {static_cast<float>(c.r * f), static_cast<float>(c.g * f), static_cast<float>(c.b * f)});
    }
  }
  auto g = make<BufferGeometry>();
  g->setAttribute("position", attr(std::move(pos), 3));
  g->setAttribute("color", attr(std::move(col), 3));
  return g;
}

GeometryPtr tr(GeometryPtr g, double x, double y, double z) {
  g->translate(x, y, z);
  return g;
}

const Shapes& shapesOf() {
  static const Shapes* shapes = [] {
    Shape tri({Vector2(-0.24, 0), Vector2(0.24, 0), Vector2(0, 0.42)});
    tri.holes.push_back(std::make_shared<Path>(std::vector<Vector2>{Vector2(-0.15, 0.05), Vector2(0, 0.31), Vector2(0.15, 0.05)}));
    GeometryPtr wheel = cylinderGeometry(0.09, 0.09, 0.04, 10);
    wheel->rotateZ(PI_ / 2);
    return new Shapes{
        .trunk = tr(cylinderGeometry(0.12, 0.18, 2.4, 6), 0, 1.2, 0),
        .crown = tr(coneGeometry(1.7, 6.5, 7), 0, 5.2, 0),
        .post = tr(boxGeometry(0.12, 1.0, 0.1), 0, 0.5, 0),
        .band = tr(boxGeometry(0.124, 0.25, 0.104), 0, 0.82, 0),
        .reflector = tr(boxGeometry(0.04, 0.16, 0.012), 0.03, 0.72, -0.055),
        .triangle = tr(shapeGeometry(tri), 0, 0.1, 0),
        .leg = tr(boxGeometry(0.03, 0.03, 0.34), 0, 0.02, 0.1),
        .board = boxGeometry(0.42, 0.22, 0.03),
        .foot = tr(boxGeometry(0.06, 1.0, 0.5), 0, 0.5, 0),
        .cone = tr(coneGeometry(0.17, 0.5, 10), 0, 0.25, 0),
        .coneBand = tr(cylinderGeometry(0.095, 0.115, 0.08, 10), 0, 0.27, 0),
        .heap = tr(coneGeometry(1.7, 1.0, 8), 0, 0.5, 0),
        .tube = cylinderGeometry(0.014, 0.014, 1, 6),
        .wheel = wheel,
        .seat = boxGeometry(0.42, 0.04, 0.3),
        .pole = tr(cylinderGeometry(0.016, 0.016, 0.8, 6), 0, 0.4, 0),
        .disc = circleGeometry(0.2, 20),
        .ring = ringGeometry(0.2, 0.235, 20),
        .low = beamShape(55, 9),
        .high = beamShape(105, 15),
    };
  }();
  return *shapes;
}

using P3 = std::array<double, 3>;

/** A bar of the rollator's frame from a to b (local metres). */
void bar(Group& g, const P3& a, const P3& b, const MaterialPtr& mat) {
  const Shapes& s = shapesOf();
  Vector3 d(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
  auto m = make<Mesh>(s.tube, mat);
  m->scale.set(1, d.length(), 1);
  m->quaternion.setFromUnitVectors(Vector3(0, 1, 0), d.normalize());
  m->position.set((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2);
  g.add(m);
}

/** A rollator, its handles towards the person behind it (local: −z forwards, the handles at z = 0). */
std::shared_ptr<Group> makeRollator() {
  const Shapes& s = shapesOf();
  auto g = make<Group>();
  for (const double x : {-0.25, 0.25}) {
    bar(*g, {x, 0.9, 0.0}, {x, 0.09, -0.08}, MAT().frame);
    bar(*g, {x, 0.9, 0.0}, {x, 0.09, -0.6}, MAT().frame);
    auto back = make<Mesh>(s.wheel, MAT().tyre);
    back->position.set(x, 0.09, -0.08);
    auto front = make<Mesh>(s.wheel, MAT().tyre);
    front->position.set(x, 0.09, -0.6);
    g->add(back, front);
  }
  bar(*g, {-0.25, 0.55, -0.2}, {0.25, 0.55, -0.2}, MAT().frame);
  auto seat = make<Mesh>(s.seat, MAT().black);
  seat->position.set(0, 0.57, -0.3);
  g->add(seat);
  g->traverse([](Object3D& o) {
    if (o.type == ObjectType::Mesh) o.castShadow = true;
  });
  return g;
}
}  // namespace

TheoryExtras::TheoryExtras() {
  group->name = "theory: props";
  group->add(place);
}

// ------------------------------------------------------------------ props

void TheoryExtras::build(const theory::Stage& st, const StageFrame& f, const std::function<double(double x, double z)>& height) {
  const std::vector<theory::PropPlace>& props = st.props;
  if (props.empty()) return;
  const Shapes& s = shapesOf();
  const auto groundAt = [&](double x, double z) -> std::array<double, 3> {
    const auto [wx, wz] = f.toWorld(x, z);
    return {wx, height(wx, wz) + theory::liftAt(st, x, z), wz};
  };
  // the trees: two instanced meshes for the lot
  std::vector<const theory::PropPlace*> trees;
  for (const theory::PropPlace& p : props)
    if (p.kind == "tree") trees.push_back(&p);
  if (!trees.empty()) {
    const int n = static_cast<int>(trees.size());
    auto trunks = make<InstancedMesh>(s.trunk, MAT().trunk, n);
    auto crowns = make<InstancedMesh>(s.crown, MAT().crown, n);
    Matrix4 m;
    Quaternion q;
    for (int i = 0; i < n; i++) {
      const theory::PropPlace& p = *trees[static_cast<size_t>(i)];
      const auto [x, y, z] = groundAt(p.x, p.z);
      const double k = p.size.value_or(1);
      q.setFromAxisAngle(Vector3(0, 1, 0), std::fmod(p.x * 7.3 + p.z * 3.1, 6.28));
      m.compose(Vector3(x, y, z), q, Vector3(k, k, k));
      trunks->setMatrixAt(i, m);
      crowns->setMatrixAt(i, m);
    }
    for (const auto& im : {trunks, crowns}) {
      im->castShadow = true;
      im->instanceMatrix->needsUpdate();
      im->computeBoundingSphere();
      madeInstanced.push_back(im);
      place->add(im);
    }
  }
  for (const theory::PropPlace& p : props) {
    if (p.kind == "tree") continue;
    ObjectPtr obj = p.kind == "tunnel" ? tunnel(p, f, groundAt) : prop(p);
    if (!obj) continue;
    if (p.kind != "tunnel") {
      const auto [x, y, z] = groundAt(p.x, p.z);
      obj->position.set(x, y, z);
      obj->rotation.y = f.yawToWorld(p.yaw.value_or(0));
      const double k = p.size.value_or(1);
      obj->scale.set(k, k, k);
    }
    place->add(obj);
    if (p.from || p.to) timed.push_back({obj, p.from.value_or(-JS_INF), p.to.value_or(JS_INF)});
  }
}

ObjectPtr TheoryExtras::prop(const theory::PropPlace& p) {
  const Shapes& s = shapesOf();
  auto g = make<Group>();
  const auto add = [&g](const GeometryPtr& geo, const MaterialPtr& mat, double x = 0, double y = 0, double z = 0, double ry = 0) {
    auto m = make<Mesh>(geo, mat);
    m->position.set(x, y, z);
    m->rotation.y = ry;
    m->castShadow = true;
    g->add(m);
    return m;
  };
  if (p.kind == "post") {
    add(s.post, MAT().white);
    add(s.band, MAT().black);
    add(s.reflector, MAT().amber);
  } else if (p.kind == "triangle") {
    // the red frame facing the traffic, leaning back a little on its stand
    auto t = add(s.triangle, MAT().red);
    t->rotation.set(-0.12, PI_, 0);
    add(s.leg, MAT().black, -0.14);
    add(s.leg, MAT().black, 0.14);
  } else if (p.kind == "barrier") {
    // a striped board across the lane on two feet, a warning lamp on top
    for (int i = 0; i < 6; i++) add(s.board, i % 2 ? MAT().white : MAT().barRed, -1.05 + i * 0.42, 1.0, 0);
    add(s.foot, MAT().wood, -1.3, 0, 0);
    add(s.foot, MAT().wood, 1.3, 0, 0);
    auto lamp = add(s.coneBand, MAT().amber, -1.3, 0.85, 0);
    lamp->scale.set(0.8, 1.6, 0.8);
  } else if (p.kind == "cone") {
    add(s.cone, MAT().orange);
    add(s.coneBand, MAT().white);
  } else if (p.kind == "heap") {
    add(s.heap, MAT().earth)->scale.set(1, 1, 0.7);
  } else {
    return nullptr;
  }
  return g;
}

ObjectPtr TheoryExtras::tunnel(const theory::PropPlace& p, const StageFrame& f, const std::function<std::array<double, 3>(double x, double z)>& groundAt) {
  const double x0 = std::min(p.x, p.x1.value_or(p.x));
  const double x1 = std::max(p.x, p.x1.value_or(p.x));
  const double zS = std::max(p.z, p.z1.value_or(p.z));
  const double zN = std::min(p.z, p.z1.value_or(p.z));
  const double h = p.h.value_or(5);
  const double cx = (x0 + x1) / 2;
  const double cz = (zS + zN) / 2;
  const double len = zS - zN;
  const double wide = x1 - x0;
  auto g = make<Group>();
  // (the stage's frame turned onto the map: local −z its north, local x its east; level with the road at the portal)
  const double wy = groundAt(cx, zS - 2)[1];
  const auto [gx, gz] = f.toWorld(cx, cz);
  g->position.set(gx, wy, gz);
  g->rotation.y = f.yawToWorld(0);
  const auto piece = [&](double w, double hh, double l, double x, double y, double z, const MaterialPtr& mat) {
    GeometryPtr geo = boxGeometry(w, hh, l);
    geo->translate(x, y, z);
    made.push_back(geo);
    auto m = make<Mesh>(geo, mat);
    m->renderOrder = mat == MAT().concrete ? 8 : 0;
    g->add(m);
  };
  // the walls and the vault (local z: south +)
  piece(0.6, h, len, x0 - cx - 0.3, h / 2, 0, MAT().concrete);
  piece(0.6, h, len, x1 - cx + 0.3, h / 2, 0, MAT().concrete);
  piece(wide + 1.2, 0.5, len, 0, h + 0.25, 0, MAT().concrete);
  // the portal: solid pillars and the lintel round the opening
  const double south = len / 2;
  piece(1.4, h + 1.2, 0.8, x0 - cx - 0.7, (h + 1.2) / 2, south, MAT().portal);
  piece(1.4, h + 1.2, 0.8, x1 - cx + 0.7, (h + 1.2) / 2, south, MAT().portal);
  piece(wide, 1.2, 0.8, 0, h + 0.6, south, MAT().portal);
  // the lamps along both walls under the roof
  for (double z = south - 4; z > -south; z -= 7) {
    piece(0.12, 0.12, 1.2, x0 - cx + 0.1, h - 0.5, z, MAT().lamp);
    piece(0.12, 0.12, 1.2, x1 - cx - 0.1, h - 0.5, z, MAT().lamp);
  }
  return g;
}

// ---------------------------------------------------------------- animals

AnimalModel* TheoryExtras::animal(const AnimalKind& kind) {
  std::vector<std::unique_ptr<AnimalModel>>& list = pool.animals[kind];
  std::unique_ptr<AnimalModel> a;
  if (!list.empty()) {
    a = std::move(list.back());
    list.pop_back();
  } else a = std::make_unique<AnimalModel>(kind);
  AnimalModel* out = a.get();
  usedAnimals.push_back(std::move(a));
  return out;
}

// -------------------------------------------------------- what people hold

void TheoryExtras::hold(int i, const theory::ActorDef& def, double t, double x, double y, double z, double yaw) {
  const std::string& role = def.role;
  if (role != "senior" && role != "flagman") return;
  auto it = held.find(i);
  if (it == held.end()) {
    Held h;
    if (role == "senior") {
      std::shared_ptr<Group> r;
      if (!pool.rollators.empty()) {
        r = pool.rollators.back();
        pool.rollators.pop_back();
      } else r = makeRollator();
      used.push_back([this, r] { pool.rollators.push_back(r); });
      h = Held{r, nullptr};
    } else {
      if (!pool.paddles.empty()) {
        h = pool.paddles.back();
        pool.paddles.pop_back();
      } else {
        // A flagman's sign on its pole: the red side faces −z (where he looks), the green one +z.
        const Shapes& s = shapesOf();
        auto g = make<Group>();
        auto turn = make<Group>();
        // (in his right hand, held up in front of him)
        turn->position.set(0.24, 1.12, -0.52);
        auto pole = make<Mesh>(s.pole, MAT().black);
        auto red = make<Mesh>(s.disc, MAT().red);
        red->position.set(0, 0.95, -0.006);
        red->rotation.y = PI_;
        auto green = make<Mesh>(s.disc, MAT().green);
        green->position.set(0, 0.95, 0.006);
        for (const double side : {-1.0, 1.0}) {
          auto ring = make<Mesh>(s.ring, MAT().white);
          ring->position.set(0, 0.95, side * 0.008);
          if (side < 0) ring->rotation.y = PI_;
          turn->add(ring);
        }
        turn->add(pole, red, green);
        g->add(turn);
        h = Held{g, turn};
      }
      used.push_back([this, h] { pool.paddles.push_back(h); });
    }
    it = held.emplace(i, h).first;
    group->add(h.group);
  }
  const Held& h = it->second;
  h.group->visible = true;
  h.group->position.set(x, y, z);
  h.group->rotation.y = yaw;
  // (the rollator a step ahead of the feet)
  if (role == "senior") h.group->position.add(Vector3(-std::sin(yaw) * 0.42, 0, -std::cos(yaw) * 0.42));
  if (h.turn) h.turn->rotation.y = PI_ * theory::paddleAt(def, t);
}

void TheoryExtras::drop(int i) {
  const auto it = held.find(i);
  if (it != held.end()) it->second.group->visible = false;
}

// -------------------------------------------------------------- headlights

void TheoryExtras::light(int i, const theory::ActorDef& def, double t, double x, double y, double z, double yaw, double front) {
  const int state = theory::beamAt(def, t);
  auto it = beams.find(i);
  if (state == 0) {
    if (it != beams.end()) it->second->visible = false;
    return;
  }
  const Shapes& s = shapesOf();
  if (it == beams.end()) {
    auto m = make<Mesh>(s.low, MAT().beam);
    m->renderOrder = 7;
    it = beams.emplace(i, m).first;
    group->add(m);
  }
  Mesh& m = *it->second;
  m.visible = true;
  m.geometry = state == 2 ? s.high : s.low;
  m.position.set(x - std::sin(yaw) * front, y + 0.04, z - std::cos(yaw) * front);
  m.rotation.set(0, yaw, 0);
}

void TheoryExtras::update(double t) {
  for (Timed& p : timed) p.obj->visible = t >= p.from && t <= p.to;
}

void TheoryExtras::clear() {
  for (const GeometryPtr& m : made) m->dispose();
  made.clear();
  for (const auto& im : madeInstanced) im->dispose();
  madeInstanced.clear();
  place->clear();
  timed.clear();
  for (const auto& [i, h] : held) group->remove(h.group);
  held.clear();
  for (const auto& [i, m] : beams) group->remove(m);
  beams.clear();
  for (const auto& back : used) back();
  used.clear();
  for (std::unique_ptr<AnimalModel>& a : usedAnimals) {
    a->group->removeFromParent();
    pool.animals[a->kind].push_back(std::move(a));
  }
  usedAnimals.clear();
}

}  // namespace fs

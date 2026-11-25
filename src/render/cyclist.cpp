// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/cyclist.ts @ v1.7.7
#include "render/cyclist.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>

#include "engine/geometries.h"

namespace fs {

namespace {
constexpr double PI_ = 3.14159265358979323846;
constexpr double WHEEL_R = 0.34;
constexpr double AXLE = 0.53;
/** the bottom bracket, the hips and the lengths of the legs (m) */
constexpr double BB_Y = 0.3;
constexpr double BB_Z = 0.06;
constexpr double HIP_Y = 0.94;
constexpr double HIP_Z = 0.24;
constexpr double THIGH = 0.45;
constexpr double SHIN = 0.47;
constexpr double CRANK = 0.17;

GeometryPtr tint(const GeometryPtr& geo, uint32_t hex) {
  const Color c(hex);
  const size_t n = geo->getAttribute("position")->count();
  std::vector<float> col(n * 3);
  for (size_t i = 0; i < n; i++) {
    col[i * 3] = static_cast<float>(c.r);
    col[i * 3 + 1] = static_cast<float>(c.g);
    col[i * 3 + 2] = static_cast<float>(c.b);
  }
  geo->setAttribute("color", attr(std::move(col), 3));
  return geo;
}

using P3 = std::array<double, 3>;

/** A round bar from one point to another. */
GeometryPtr tube(const P3& a, const P3& b, double r, uint32_t hex) {
  Vector3 d(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
  const double len = d.length();
  GeometryPtr g = cylinderGeometry(r, r, len, 6);
  g->applyQuaternion(Quaternion().setFromUnitVectors(Vector3(0, 1, 0), d.normalize()));
  g->translate((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2);
  return tint(g, hex);
}

GeometryPtr box(double w, double h, double l, double x, double y, double z, uint32_t hex, double rx = 0) {
  GeometryPtr g = boxGeometry(w, h, l);
  if (rx != 0) g->rotateX(rx);
  g->translate(x, y, z);
  return tint(g, hex);
}

struct Shared {
  GeometryPtr frame;
  GeometryPtr wheel;
  GeometryPtr cranks;
  GeometryPtr torso;
  GeometryPtr head;
  GeometryPtr thigh;
  GeometryPtr shin;
  MaterialPtr painted;
  MaterialPtr cloth;
  MaterialPtr lamps;
  GeometryPtr lampGeo;
  std::map<int, MaterialPtr> shirts;
};

Shared* build() {
  const uint32_t FRAME = 0x3a4046;
  const uint32_t DARK = 0x16181b;
  const P3 head = {0, 0.86, -0.4};
  const P3 seat = {0, 0.82, 0.22};
  const P3 bb = {0, BB_Y, BB_Z};
  const P3 rear = {0, WHEEL_R, AXLE};
  const P3 front = {0, WHEEL_R, -AXLE};
  const GeometryPtr frame = mergeGeometries({
      tube(seat, head, 0.02, FRAME),
      tube(head, bb, 0.022, FRAME),
      tube(bb, {0, 0.9, 0.25}, 0.02, FRAME),
      tube(bb, rear, 0.014, FRAME),
      tube(seat, rear, 0.012, FRAME),
      tube({0, 0.98, -0.37}, front, 0.016, FRAME),
      // handlebar, saddle, mudguard stay, luggage rack
      tube({-0.26, 1.0, -0.36}, {0.26, 1.0, -0.36}, 0.014, DARK),
      box(0.15, 0.05, 0.26, 0, 0.92, 0.27, DARK),
      box(0.14, 0.02, 0.36, 0, 0.72, 0.55, DARK),
      tube({0, 0.72, 0.7}, rear, 0.008, DARK),
  });
  // a wheel: the tyre, and spokes that show that it turns (the axle along x, the wheel around the origin)
  GeometryPtr torus = torusGeometry(WHEEL_R - 0.025, 0.025, 6, 18);
  torus->rotateY(PI_ / 2);
  const GeometryPtr tyre = tint(torus, DARK);
  std::vector<GeometryPtr> wheelParts = {tyre};
  for (int i = 0; i < 4; i++) wheelParts.push_back(box(0.008, (WHEEL_R - 0.03) * 2, 0.008, 0, 0, 0, 0x9aa0a6, (i * PI_) / 4));
  const GeometryPtr wheel = mergeGeometries(wheelParts);
  // the cranks with their pedals, around the bottom bracket
  const GeometryPtr cranks = mergeGeometries({
      tube({-0.09, 0, 0}, {-0.09, -CRANK, 0}, 0.012, DARK),
      tube({0.09, 0, 0}, {0.09, CRANK, 0}, 0.012, DARK),
      box(0.1, 0.02, 0.08, -0.15, -CRANK, 0, DARK),
      box(0.1, 0.02, 0.08, 0.15, CRANK, 0, DARK),
      tube({-0.1, 0, 0}, {0.1, 0, 0}, 0.014, DARK),
  });
  // the rider's upper body leans forward on to the handlebar (white: takes the colour of the shirt)
  const double lean = 0.5;
  const GeometryPtr torso = mergeGeometries({
      box(0.38, 0.56, 0.22, 0, HIP_Y + std::cos(lean) * 0.27, HIP_Z - std::sin(lean) * 0.27, 0xffffff, -lean),
      tube({-0.21, 1.4, 0.02}, {-0.25, 1.02, -0.34}, 0.045, 0xffffff),
      tube({0.21, 1.4, 0.02}, {0.25, 1.02, -0.34}, 0.045, 0xffffff),
  });
  GeometryPtr skullGeo = sphereGeometry(0.115, 10, 8);
  skullGeo->translate(0, 1.58, -0.08);
  const GeometryPtr skull = tint(skullGeo, 0xd9a989);
  GeometryPtr helmetGeo = sphereGeometry(0.135, 10, 6, 0, PI_ * 2, 0, PI_ * 0.5);
  helmetGeo->translate(0, 1.6, -0.075);
  const GeometryPtr helmet = tint(helmetGeo, 0xe8e8e8);
  const GeometryPtr headGeo = mergeGeometries({skull, helmet, box(0.07, 0.07, 0.09, -0.25, 1.01, -0.36, 0xd9a989), box(0.07, 0.07, 0.09, 0.25, 1.01, -0.36, 0xd9a989)});
  GeometryPtr thigh = boxGeometry(0.14, THIGH, 0.15);
  thigh->translate(0, -THIGH / 2, 0);
  GeometryPtr shin = boxGeometry(0.11, SHIN, 0.12);
  shin->translate(0, -SHIN / 2, 0);
  // a white lamp at the front and a red one at the back
  const GeometryPtr lampGeo = mergeGeometries({box(0.05, 0.05, 0.03, 0, 0.9, -0.44, 0xfff6d8), box(0.05, 0.04, 0.02, 0, 0.7, 0.74, 0xff2a1a)});
  return new Shared{
      .frame = frame,
      .wheel = wheel,
      .cranks = cranks,
      .torso = torso,
      .head = headGeo,
      .thigh = thigh,
      .shin = shin,
      .painted = lambert({.vertexColors = true}),
      .cloth = lambert({.color = Color(0x2a2d33)}),
      .lamps = basic({.vertexColors = true}),
      .lampGeo = lampGeo,
      .shirts = {},
  };
}

Shared& shared() {
  static Shared* s = build();
  return *s;
}

class CyclistShapes final : public CyclistModel {
 public:
  explicit CyclistShapes(int colour) {
    Shared& s = shared();
    body->add(make<Mesh>(s.frame, s.painted), make<Mesh>(s.lampGeo, s.lamps), make<Mesh>(s.head, s.painted));
    for (const double z : {-AXLE, AXLE}) {
      auto w = make<Mesh>(s.wheel, s.painted);
      w->position.set(0, WHEEL_R, z);
      wheels.push_back(w);
      body->add(w);
    }
    cranks = make<Mesh>(s.cranks, s.painted);
    cranks->position.set(0, BB_Y, BB_Z);
    shirt = make<Mesh>(s.torso, s.cloth);
    body->add(cranks, shirt);
    for (const double side : {-1.0, 1.0}) {
      auto thigh = make<Mesh>(s.thigh, s.cloth);
      thigh->position.set(side * 0.12, HIP_Y, HIP_Z);
      auto shin = make<Mesh>(s.shin, s.cloth);
      thighs.push_back(thigh);
      shins.push_back(shin);
      body->add(thigh, shin);
    }
    group->add(body);
    setColour(colour);
  }

  void setColour(int colour) override {
    Shared& s = shared();
    MaterialPtr& m = s.shirts[colour];
    if (!m) m = lambert({.color = Color(static_cast<uint32_t>(colour)), .vertexColors = true});
    shirt->material = m;
  }

  /**
   * `phase`: how far the pedals have turned (rad), `roll`: how far the wheels
   * (rad), `down`: lies on the ground.
   */
  void pose(double phase, double roll, bool down) override {
    body->rotation.z = down ? -1.42 : 0;
    body->position.y = down ? 0.12 : 0;
    for (const auto& w : wheels) w->rotation.x = -roll;
    cranks->rotation.x = -phase;
    for (size_t i = 0; i < 2; i++) {
      // the foot on its pedal (the left one half a turn behind the right one)
      const double a = phase + (i == 0 ? PI_ : 0);
      const double fy = BB_Y + std::cos(a) * CRANK + 0.06;
      const double fz = BB_Z - std::sin(a) * CRANK;
      double dy = fy - HIP_Y;
      double dz = fz - HIP_Z;
      const double d = std::min(std::hypot(dy, dz), THIGH + SHIN - 0.004);
      double l = std::hypot(dy, dz);
      if (l == 0) l = 1;
      dy /= l;
      dz /= l;
      // the knee: where the two parts of the leg meet, on the forward side
      const double k = std::acos(std::max(-1.0, std::min(1.0, (THIGH * THIGH + d * d - SHIN * SHIN) / (2 * THIGH * d))));
      const double ky = dy * std::cos(k) - dz * std::sin(k);
      const double kz = dy * std::sin(k) + dz * std::cos(k);
      Mesh& thigh = *thighs[i];
      thigh.rotation.x = std::atan2(-kz, -ky);
      Mesh& shin = *shins[i];
      const double knY = HIP_Y + ky * THIGH;
      const double knZ = HIP_Z + kz * THIGH;
      shin.position.set(thigh.position.x, knY, knZ);
      shin.rotation.x = std::atan2(-(fz - knZ), -(fy - knY));
    }
  }

 private:
  /** everything that falls over together */
  const std::shared_ptr<Group> body = make<Group>();
  std::vector<std::shared_ptr<Mesh>> wheels;
  std::shared_ptr<Mesh> cranks;
  std::shared_ptr<Mesh> shirt;
  std::vector<std::shared_ptr<Mesh>> thighs;
  std::vector<std::shared_ptr<Mesh>> shins;
};
}  // namespace

std::unique_ptr<CyclistModel> makeCyclistModel(int colour) { return std::make_unique<CyclistShapes>(colour); }

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The player's car body, built from primitives in the car's own frame.
 *
 * Origin: src/render/carBody.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).  The proportions are the compact hatchback the vehicle spec
 * describes: 4.28 m long, 1.78 m wide, 1.45 m high, wheels at ±0.77 m.
 */
#include "render/carBody.h"

#include <cmath>

namespace fs {

namespace {
/** A box with the paint colour and a slight bevel via scaled copies. */
std::unique_ptr<Mesh> box(double w, double h, double d, double x, double y, double z,
                          uint32_t color, MaterialKind kind = MaterialKind::Struct) {
  auto g = makeBoxGeometry(w, h, d);
  g->translate(x, y, z);
  // vertex colours: three.js MeshLambertMaterial(vertexColors) reads `color`
  size_t n = g->position ? g->position->count() : 0;
  Color c;
  c.setHex(color);
  std::vector<double> cols(n * 3);
  for (size_t i = 0; i < n; i++) {
    cols[i * 3] = c.r;
    cols[i * 3 + 1] = c.g;
    cols[i * 3 + 2] = c.b;
  }
  g->color = std::make_shared<BufferAttribute>(cols, 3);
  return std::make_unique<Mesh>(g, kind);
}

/** A wheel: tyre plus a rim disc, in the XY plane (axle along Z). */
std::unique_ptr<Mesh> wheel(double radius, double width, int rim) {
  auto tyre = makeCylinderGeometry(radius, radius, width, 20, 1, false);
  tyre->rotateX(PI / 2);
  auto tyreC = makeCylinderGeometry(radius * 0.62, radius * 0.62, width * 1.02, 16, 1, false);
  tyreC->rotateX(PI / 2);
  auto hub = makeCylinderGeometry(radius * 0.3, radius * 0.3, width * 1.1, 12, 1, false);
  hub->rotateX(PI / 2);

  // colour the parts: dark tyre, lighter rim, dark hub
  auto paint = [](GeometryPtr g, uint32_t color) {
    size_t n = g->position ? g->position->count() : 0;
    Color c;
    c.setHex(color);
    std::vector<double> cols(n * 3);
    for (size_t i = 0; i < n; i++) {
      cols[i * 3] = c.r;
      cols[i * 3 + 1] = c.g;
      cols[i * 3 + 2] = c.b;
    }
    g->color = std::make_shared<BufferAttribute>(cols, 3);
    return g;
  };
  paint(tyre, 0x15161a);
  paint(tyreC, rim == 0 ? 0xb9bcc2 : 0x2b2d31);
  paint(hub, 0x53575d);
  (void)rim;

  auto mergedG = mergeGeometries({tyre, tyreC, hub}, false);
  mergedG->computeVertexNormals();
  return std::make_unique<Mesh>(mergedG, MaterialKind::Struct);
}
}  // namespace

std::unique_ptr<CarBody> CarBody::build(uint32_t paint, const WheelStyle& wheels) {
  auto body = std::make_unique<CarBody>();

  // ---- shell: floor, cabin, bonnet, boot ----------------------------------
  auto shell = std::make_unique<Group>();
  shell->name = "shell";
  // main body (4.28 m long, 1.78 m wide) sitting on the suspension
  shell->add(box(1.78, 0.62, 4.28, 0, 0.62, 0, paint));
  // cabin: narrower and set back; the screen and boot lid are approximated
  shell->add(box(1.62, 0.52, 2.1, 0, 1.16, -0.15, paint));
  shell->add(box(1.6, 0.3, 0.9, 0, 1.0, -1.35, paint));   // bonnet lip
  shell->add(box(1.6, 0.3, 0.75, 0, 1.0, 1.5, paint));    // boot lid

  // ---- glass: windscreen, side windows, rear window -----------------------
  auto glass = std::make_unique<Group>();
  glass->name = "glass";
  glass->add(box(1.5, 0.42, 0.06, 0, 1.2, -1.05, 0x1b2733, MaterialKind::Struct));
  glass->add(box(1.5, 0.34, 0.06, 0, 1.2, 0.85, 0x1b2733, MaterialKind::Struct));
  glass->add(box(0.06, 0.34, 1.9, -0.79, 1.2, -0.15, 0x1b2733, MaterialKind::Struct));
  glass->add(box(0.06, 0.34, 1.9, 0.79, 1.2, -0.15, 0x1b2733, MaterialKind::Struct));

  // ---- trim: bumpers, sills, mirrors, grille ------------------------------
  auto trim = std::make_unique<Group>();
  trim->name = "trim";
  trim->add(box(1.78, 0.22, 0.3, 0, 0.36, -2.1, 0x24272c));
  trim->add(box(1.78, 0.22, 0.3, 0, 0.36, 2.1, 0x24272c));
  trim->add(box(1.8, 0.12, 4.3, 0, 0.24, 0, 0x1c1e22));
  trim->add(box(0.14, 0.1, 0.24, -0.95, 1.28, -1.0, 0x24272c));  // mirror L
  trim->add(box(0.14, 0.1, 0.24, 0.95, 1.28, -1.0, 0x24272c));   // mirror R
  trim->add(box(1.2, 0.16, 0.06, 0, 0.78, -2.12, 0x14161a));     // grille

  // ---- lights --------------------------------------------------------------
  auto lights = std::make_unique<Group>();
  lights->name = "lights";
  lights->add(box(0.42, 0.16, 0.1, -0.58, 0.86, -2.16, 0xf5f2e8));
  lights->add(box(0.42, 0.16, 0.1, 0.58, 0.86, -2.16, 0xf5f2e8));
  lights->add(box(0.4, 0.14, 0.08, -0.6, 0.86, 2.16, 0xd23a2a));
  lights->add(box(0.4, 0.14, 0.08, 0.6, 0.86, 2.16, 0xd23a2a));

  // ---- wheels --------------------------------------------------------------
  double t = 0.77;
  double zf = -1.05, zr = 1.05;
  struct Pos {
    double x, z;
  } pos[4] = {{-t, zf}, {t, zf}, {-t, zr}, {t, zr}};
  for (int i = 0; i < 4; i++) {
    auto w = wheel(wheels.radius, wheels.width, wheels.rim);
    w->name = "wheel" + std::to_string(i);
    w->position.set(pos[i].x, wheels.radius, pos[i].z);
    w->updateMatrix();
    w->matrixAutoUpdate = false;
    body->wheels.push_back(std::move(w));
  }

  body->shell = std::move(shell);
  body->glass = std::move(glass);
  body->trim = std::move(trim);
  body->lights = std::move(lights);
  return body;
}

}  // namespace fs

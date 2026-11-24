// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/boxbody.ts @ v1.7.7
#include "render/boxbody.h"

#include <algorithm>
#include <cmath>

#include "engine/geometries.h"

namespace fs {

namespace {

const MaterialPtr& PLY() {
  static const MaterialPtr* m = new MaterialPtr(lambert({.color = Color(0xc2b59c)}));
  return *m;
}
const MaterialPtr& PLANKS() {
  static const MaterialPtr* m = new MaterialPtr(lambert({.color = Color(0x6e5a44)}));
  return *m;
}
const MaterialPtr& FRAME() {
  static const MaterialPtr* m = new MaterialPtr(lambert({.color = Color(0x3a3f45)}));
  return *m;
}
const MaterialPtr& STEEL() {
  static const MaterialPtr* m = new MaterialPtr(lambert({.color = Color(0x8d949b)}));
  return *m;
}

/** A quad facing along `normal`, from (a0, b0) to (a1, b1) in its plane – see `face`. */
std::shared_ptr<Mesh> face(char axis, double sign, double a0, double a1, double b0, double b1, double at, const MaterialPtr& material) {
  auto m = make<Mesh>(planeGeometry(std::abs(a1 - a0), std::abs(b1 - b0)), material);
  const double a = (a0 + a1) / 2;
  const double b = (b0 + b1) / 2;
  if (axis == 'z') {
    // a: x, b: y
    m->position.set(a, b, at);
    if (sign < 0) m->rotation.y = PI;
  } else if (axis == 'x') {
    // a: z, b: y
    m->position.set(at, b, a);
    m->rotation.y = sign > 0 ? PI / 2 : -PI / 2;
  } else {
    // a: x, b: z
    m->position.set(a, at, b);
    m->rotation.x = sign > 0 ? -PI / 2 : PI / 2;
  }
  return m;
}

std::shared_ptr<Mesh> block(double x0, double x1, double y0, double y1, double z0, double z1, const MaterialPtr& material) {
  auto m = make<Mesh>(boxGeometry(x1 - x0, y1 - y0, z1 - z0), material);
  m->position.set((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
  return m;
}

}  // namespace

BoxBody::BoxBody(double hw, double floor, double top, double front, double rear, const MaterialPtr& paint) {
  Group& g = *group;
  g.name = "box body";
  const double t = 0.05;
  // the shell outside: front, sides, roof, underside
  const std::shared_ptr<Mesh> outer[] = {
      face('z', -1, -hw, hw, floor, top, front, paint), face('x', -1, front, rear, floor, top, -hw, paint), face('x', 1, front, rear, floor, top, hw, paint),
      face('y', 1, -hw, hw, front, rear, top, paint),   face('y', -1, -hw, hw, front, rear, floor, paint),
  };
  for (const auto& m : outer) m->castShadow = true;
  for (const auto& m : outer) g.add(m);
  // inside: plywood walls and roof, a floor of planks
  const double i0 = -hw + t;
  const double i1 = hw - t;
  const double f = floor + 0.03;
  const double top1 = top - t;
  g.add(face('z', 1, i0, i1, f, top1, front + t, PLY()), face('x', 1, front + t, rear, f, top1, i0, PLY()), face('x', -1, front + t, rear, f, top1, i1, PLY()),
        face('y', -1, i0, i1, front + t, rear, top1, PLY()), face('y', 1, i0, i1, front + t, rear, f, PLANKS()));
  // the rear frame round the opening: the posts, the header and the sill
  g.add(block(-hw, i0, floor, top, rear - 0.08, rear, FRAME()), block(i1, hw, floor, top, rear - 0.08, rear, FRAME()), block(-hw, hw, top1, top, rear - 0.08, rear, FRAME()),
        block(-hw, hw, floor - 0.06, f, rear - 0.12, rear, STEEL()));
  inside = {i0, i1, f, top1, front + t, rear - 0.08};
  // the doors: hung at the rear corners, each closing half the opening
  for (const int side : {-1, 1}) {
    auto pivot = make<Group>();
    pivot->position.set(side * (hw + 0.02), 0, rear);
    const double w = hw + 0.01;
    const double x0 = side < 0 ? 0 : -w;
    const double x1 = side < 0 ? w : 0;
    auto leaf = block(x0, x1, floor + 0.02, top - 0.02, 0.005, 0.055, paint);
    leaf->castShadow = true;
    pivot->add(leaf);
    // two locking bars on each door, and its hinges
    for (const double k : {0.3, 0.75}) {
      auto bar = make<Mesh>(cylinderGeometry(0.018, 0.018, top - floor - 0.1, 6), STEEL());
      bar->position.set(-side * w * k, (floor + top) / 2, 0.075);
      pivot->add(bar);
    }
    for (const double y : {floor + 0.35, (floor + top) / 2, top - 0.35}) pivot->add(block(-0.05, 0.05, y - 0.06, y + 0.06, -0.01, 0.07, FRAME()));
    doors_.push_back(pivot);
    g.add(pivot);
  }
}

void BoxBody::setDoors(double open) {
  const double o = std::max(0.0, std::min(1.0, open));
  if (o == open_) return;
  open_ = o;
  // (the left door turns one way, the right one the other; both outwards first)
  const double a = o * ((260 * PI) / 180);
  doors_[0]->rotation.y = -a;
  doors_[1]->rotation.y = a;
  if (rearPlate) rearPlate->visible = o < 0.01;
}

}  // namespace fs

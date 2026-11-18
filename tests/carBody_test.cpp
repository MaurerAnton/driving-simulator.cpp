// SPDX-License-Identifier: AGPL-3.0-only
/** Car body: the pieces exist, sit in the right places, wheels on the axles. */
#include "../src/render/carBody.h"
#include <cassert>
#include <cstdio>

using namespace fs;

namespace {
void boundsOf(const Object3D& node, Vector3& mn, Vector3& mx) {
  mn.set(1e300, 1e300, 1e300);
  mx.set(-1e300, -1e300, -1e300);
  const_cast<Object3D&>(node).traverse([&](Object3D& o) {
    auto* mesh = dynamic_cast<Mesh*>(&o);
    if (!mesh || !mesh->geometry || !mesh->geometry->position) return;
    Vector3 bmin, bmax;
    mesh->geometry->computeBoundingBox(bmin, bmax);
    // the node's local matrix places the geometry in the parent's frame
    for (int i = 0; i < 8; i++) {
      Vector3 corner((i & 1) ? bmax.x : bmin.x, (i & 2) ? bmax.y : bmin.y,
                     (i & 4) ? bmax.z : bmin.z);
      corner.applyMatrix4(o.matrix);
      mn.x = std::min(mn.x, corner.x); mn.y = std::min(mn.y, corner.y); mn.z = std::min(mn.z, corner.z);
      mx.x = std::max(mx.x, corner.x); mx.y = std::max(mx.y, corner.y); mx.z = std::max(mx.z, corner.z);
    }
  });
}
}  // namespace

int main() {
  WheelStyle ws;
  auto body = CarBody::build(SCHOOL_CAR_PAINT, ws);

  // the four pieces exist and are named as the original groups them
  assert(body->shell && body->glass && body->trim && body->lights);
  assert(body->shell->name == "shell");
  assert(body->glass->name == "glass");
  assert(body->trim->name == "trim");
  assert(body->lights->name == "lights");
  assert(body->wheels.size() == 4);

  // the body spans the length and width the vehicle spec describes (4.28 × 1.78)
  Vector3 mn, mx;
  boundsOf(*body->shell, mn, mx);
  assert(mx.z - mn.z > 4.0 && mx.z - mn.z < 4.5);
  assert(mx.x - mn.x > 1.7 && mx.x - mn.x < 1.9);
  // and it sits on the ground (y from 0 up)
  assert(mn.y > -0.01 && mn.y < 0.4);
  assert(mx.y > 1.3 && mx.y < 1.7);

  // glass is inside the shell's silhouette and above the waistline
  Vector3 gmn, gmx;
  boundsOf(*body->glass, gmn, gmx);
  assert(gmx.y > 1.0);
  assert(gmx.x < mx.x + 1e-9);

  // wheels: front axle at z = -1.05, rear at +1.05, radius on the ground
  double z[4];
  double x[4];
  for (int i = 0; i < 4; i++) {
    z[i] = body->wheels[i]->position.z;
    x[i] = body->wheels[i]->position.x;
    assert(std::fabs(body->wheels[i]->position.y - ws.radius) < 1e-9);
    assert(std::fabs(std::fabs(x[i]) - 0.77) < 1e-9);
  }
  assert(z[0] < 0 && z[1] < 0);   // front axle
  assert(z[2] > 0 && z[3] > 0);   // rear axle
  assert(x[0] < 0 && x[1] > 0);   // left, right
  assert(x[2] < 0 && x[3] > 0);

  // the wheels carry vertex colours (tyre black, rim lighter)
  for (int i = 0; i < 4; i++) {
    auto* mesh = body->wheels[i].get();
    assert(mesh->geometry && mesh->geometry->color);
    assert(mesh->geometry->color->count() == mesh->geometry->position->count());
    bool sawDark = false, sawLight = false;
    for (size_t k = 0; k < mesh->geometry->color->count(); k++) {
      double r = mesh->geometry->color->getX(k);
      if (r < 0.15) sawDark = true;
      if (r > 0.5) sawLight = true;
    }
    assert(sawDark && sawLight);
  }

  // a different paint colour changes the shell's vertex colours
  auto red = CarBody::build(0xc02020, ws);
  Vector3 rmn, rmx;
  boundsOf(*red->shell, rmn, rmx);
  assert(std::fabs(rmx.x - mx.x) < 1e-9);   // same shape
  bool redFound = false;
  red->shell->traverse([&](Object3D& o) {
    auto* mesh = dynamic_cast<Mesh*>(&o);
    if (!mesh || !mesh->geometry || !mesh->geometry->color) return;
    for (size_t k = 0; k < mesh->geometry->color->count(); k++)
      if (mesh->geometry->color->getX(k) > 0.6 && mesh->geometry->color->getZ(k) < 0.3)
        redFound = true;
  });
  assert(redFound);

  std::printf("carBody: all checks passed (4.28 m body, 4 wheels)\n");
  return 0;
}

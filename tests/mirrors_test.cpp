// SPDX-License-Identifier: AGPL-3.0-only
/** Mirrors: glass geometry, housing, off-axis camera, mirror normal. */
#include "../src/render/mirrors.h"
#include <cassert>
#include <cstdio>

using namespace fs;

int main() {
  MirrorSpec spec;
  spec.name = "left";
  spec.centre.set(-0.9, 1.25, -1.1);
  spec.normal.set(-1, 0, 0.2);   // facing inwards and slightly back
  spec.width = 0.16;
  spec.height = 0.11;
  spec.convex = 1.15;
  spec.resolution = {64, 48};

  Mirror m(spec);

  // the glass is a quad in the mirror plane
  assert(m.mesh && m.mesh->geometry);
  assert(m.mesh->geometry->position->count() == 4);
  assert(m.mesh->geometry->index->count() == 6);
  assert(m.mesh->name == "mirror left");
  assert(!m.camera.matrixAutoUpdate);

  // the frame basis: up is world up projected into the glass, right = up × n
  assert(std::fabs(m.localUp.y - 1.0) < 0.2);
  assert(std::fabs(m.localUp.length() - 1.0) < 1e-9);
  assert(std::fabs(m.localRight.length() - 1.0) < 1e-9);
  // right must be perpendicular to up and to the normal
  assert(std::fabs(m.localRight.dot(m.localUp)) < 1e-6);

  // the glass quad lies on the plane through the centre with the given normal
  Vector3 n = spec.normal;
  n.normalize();
  for (size_t i = 0; i < 4; i++) {
    Vector3 v(m.mesh->geometry->position->getX(i), m.mesh->geometry->position->getY(i),
              m.mesh->geometry->position->getZ(i));
    Vector3 rel;
    rel.subVectors(v, spec.centre);
    assert(std::fabs(rel.dot(n)) < 1e-6);
  }
  // and it is as wide and as tall as the spec
  Vector3 p0(m.mesh->geometry->position->getX(0), m.mesh->geometry->position->getY(0),
             m.mesh->geometry->position->getZ(0));
  Vector3 p2(m.mesh->geometry->position->getX(2), m.mesh->geometry->position->getY(2),
             m.mesh->geometry->position->getZ(2));
  Vector3 diag;
  diag.subVectors(p2, p0);
  assert(std::fabs(diag.length() - std::sqrt(0.16 * 0.16 + 0.11 * 0.11)) < 1e-6);

  // the housing is a box aligned with the glass and pushed behind it
  auto housing = m.makeHousing(0.06, 0x24272c);
  assert(housing && housing->geometry && housing->geometry->position->count() == 36);
  Vector3 hmn, hmx;
  housing->geometry->computeBoundingBox(hmn, hmx);
  // it is behind the glass along the normal
  Vector3 hc;
  hc.subVectors(hmn, spec.centre);
  double dotMin = hc.dot(n);
  hc.subVectors(hmx, spec.centre);
  assert(std::min(dotMin, hc.dot(n)) < -0.02);

  // switching the mirror off tints the glass dark
  m.setEnabled(false);
  assert(!m.enabled);
  bool dark = false;
  auto* g = m.mesh->geometry.get();
  for (size_t i = 0; i < g->color->count(); i++)
    if (g->color->getX(i) < 0.2) dark = true;
  assert(dark);
  m.setEnabled(true);
  bool light = false;
  for (size_t i = 0; i < g->color->count(); i++)
    if (g->color->getX(i) > 0.9) light = true;
  assert(light);

  // rendering with the eye behind the glass does nothing (no crash)
  WebGLRenderer r;
  r.init(320, 240);
  Scene scene;
  Camera cam;
  Matrix4 car;
  car.makeTranslation(0, 0, 0);
  Vector3 eye(-0.36, 1.2, 0.2);
  r.render(scene, cam);
  m.render(r, scene, car, eye, 4500);
  // with the eye in front of the glass the camera gets a basis
  Vector3 eyeFront(-0.36, 1.2, -0.6);
  m.render(r, scene, car, eyeFront, 4500);
  Vector3 up(0, 1, 0);
  up.applyMatrix4(m.camera.matrix);
  assert(std::fabs(up.y) > 0.9);   // the camera keeps the world's up

  // mirrorNormal: the glass faces the driver when the driver looks at it
  Vector3 centre(0, 1.2, -1);
  Vector3 driverEye(0, 1.2, 0);
  Vector3 look(0, 0, -1);
  Vector3 nrm = mirrorNormal(centre, driverEye, look);
  assert(nrm.z < -0.9);            // pointing forwards, at the driver
  assert(std::fabs(nrm.length() - 1.0) < 1e-9);

  std::printf("mirrors: all checks passed\n");
  return 0;
}

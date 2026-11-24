// SPDX-License-Identifier: AGPL-3.0-only
/** Scene graph checks: parent/child, world matrices, lights. */
#include "../src/engine/scene.h"
#include <cmath>
#include <cassert>
#include <cstdio>

using namespace fs;

int main() {
  Scene scene;
  auto group = std::make_unique<Group>();
  Group* g = group.get();
  g->position.set(10, 0, 0);
  auto child = std::make_unique<Object3D>();
  Object3D* c = child.get();
  c->position.set(0, 5, 0);
  g->add(std::move(child));
  scene.add(std::move(group));
  scene.updateMatrixWorld();

  Vector3 wp;
  c->getWorldPosition(wp);
  assert(wp.x == 10 && wp.y == 5 && wp.z == 0);

  // removing a child updates the world matrix
  g->remove(c);
  scene.updateMatrixWorld();
  assert(g->children.empty());

  // lights carry colour and intensity
  DirectionalLight sun(0xfff4e0, 3.0);
  assert(sun.intensity == 3.0);
  assert(sun.color.r > 0.99);

  HemisphereLight hemi(0xbfd6ff, 0x6b6a55, 1.1);
  assert(hemi.intensity == 1.1);

  // camera projection: a point at the near plane maps to NDC z = -1
  Camera cam;
  cam.fov = 60;
  cam.aspect = 16.0 / 9.0;
  cam.near = 0.08;
  cam.far = 4500;
  cam.updateProjectionMatrix();
  Vector3 nearPt(0, 0, -cam.near);
  nearPt.applyMatrix4(cam.projectionMatrix);
  assert(std::fabs(nearPt.z + 1.0) < 1e-9);

  std::printf("scene: all checks passed\n");
  return 0;
}

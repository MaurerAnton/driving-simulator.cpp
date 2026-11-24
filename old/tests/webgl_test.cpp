// SPDX-License-Identifier: AGPL-3.0-only
/** Renderer traversal: the scene walk must produce the right draw items. */
#include "../src/engine/webgl.h"
#include <cmath>
#include <cassert>
#include <cstdio>

using namespace fs;

int main() {
  Scene scene;
  scene.fog = std::make_shared<Fog>(0xc4d4e4, 300, 2400);

  auto mesh = std::make_unique<Mesh>(makeBoxGeometry(1, 1, 1), MaterialKind::Struct);
  Mesh* m = mesh.get();
  m->position.set(5, 0, 0);
  scene.add(std::move(mesh));

  auto sub = std::make_unique<Group>();
  Group* g = sub.get();
  g->position.set(0, 10, 0);
  auto m2 = std::make_unique<Mesh>(makePlaneGeometry(1, 1), MaterialKind::Ground);
  g->add(std::move(m2));
  scene.add(std::move(sub));

  auto sun = std::make_unique<DirectionalLight>(0xfff4e0, 3.0);
  DirectionalLight* sunPtr = sun.get();
  sun->position.set(100, 300, 50);
  sun->target.position.set(0, 0, 0);
  scene.add(std::move(sun));

  auto hemi = std::make_unique<HemisphereLight>(0xbfd6ff, 0x6b6a55, 1.1);
  scene.add(std::move(hemi));

  scene.updateMatrixWorld();

  WebGLRenderer r;
  r.init(800, 600);
  r.render(scene, (Camera*)nullptr);  // no camera needed for collection

  assert(r.lastItems().size() == 2);
  assert(r.lastItems()[0].kind == MaterialKind::Struct);
  assert(r.lastItems()[1].kind == MaterialKind::Ground);

  // world matrices: the second mesh sits 10 m up (its group) — the renderer
  // must have composed the group transform into the item's world matrix
  Vector3 p;
  r.lastItems()[1].world.setFromMatrixPosition(p);
  assert(std::fabs(p.y - 10) < 1e-9);

  // lights gathered from the scene
  const FrameLights& L = r.lastLights();
  assert(L.sunIntensity == 3.0);
  assert(std::fabs(L.hemiIntensity - 1.1) < 1e-9);
  assert(std::fabs(L.sunDir.length() - 1.0) < 1e-9);
  assert(L.fogNear == 300 && L.fogFar == 2400);

  std::printf("webgl: all checks passed (%zu draw items)\n", r.lastItems().size());
  return 0;
}

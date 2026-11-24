// SPDX-License-Identifier: AGPL-3.0-only
/** The test scene of tools/dev/render_probe (and of the browser's bring-up page): boxes on a ground under the sun. */
#pragma once

#include <vector>

#include "engine/engine.h"

namespace fs::probe {

/** a box of the given size, standing on y = 0 (24 vertices, flat faces) */
inline GeometryPtr box(double w, double h, double d) {
  const float x = static_cast<float>(w / 2), y = static_cast<float>(h), z = static_cast<float>(d / 2);
  std::vector<float> p, n;
  std::vector<uint32_t> idx;
  auto face = [&](float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz, float dx, float dy, float dz, float nx, float ny, float nz) {
    uint32_t base = static_cast<uint32_t>(p.size() / 3);
    for (float v : {ax, ay, az, bx, by, bz, cx, cy, cz, dx, dy, dz}) p.push_back(v);
    for (int i = 0; i < 4; i++) {
      n.push_back(nx);
      n.push_back(ny);
      n.push_back(nz);
    }
    for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) idx.push_back(base + i);
  };
  face(-x, 0, z, x, 0, z, x, y, z, -x, y, z, 0, 0, 1);
  face(x, 0, -z, -x, 0, -z, -x, y, -z, x, y, -z, 0, 0, -1);
  face(x, 0, z, x, 0, -z, x, y, -z, x, y, z, 1, 0, 0);
  face(-x, 0, -z, -x, 0, z, -x, y, z, -x, y, -z, -1, 0, 0);
  face(-x, y, z, x, y, z, x, y, -z, -x, y, -z, 0, 1, 0);
  face(-x, 0, -z, x, 0, -z, x, 0, z, -x, 0, z, 0, -1, 0);
  auto g = make<BufferGeometry>();
  g->setAttribute("position", attr(std::move(p), 3));
  g->setAttribute("normal", attr(std::move(n), 3));
  g->setIndex(idx);
  return g;
}


struct ProbeScene {
  std::shared_ptr<Scene> scene;
  std::shared_ptr<Camera> camera;
};

inline ProbeScene makeScene(int W, int H) {
  auto scene = make<Scene>();
  scene->fog = std::make_shared<Fog>(Color(0xc4d4e4), 30, 220);
  scene->background = Color(0xc4d4e4);
  auto camera = perspectiveCamera(60, static_cast<double>(W) / H, 0.08, 4500);
  camera->position.set(9, 6, 14);
  camera->lookAt(0, 1.5, 0);

  LightPtr sun = directionalLight(Color(0xfff4e0), 3.2);
  sun->position.set(60, 120, 40);
  sun->castShadow = true;
  Camera& sc = *sun->shadow.camera;
  sc.left = -30;
  sc.right = 30;
  sc.top = 30;
  sc.bottom = -30;
  sc.near = 10;
  sc.far = 300;
  sc.updateProjectionMatrix();
  sun->shadow.mapSize.set(2048, 2048);
  sun->shadow.bias = -0.0004;
  sun->shadow.normalBias = 0.05;
  sun->shadow.radius = 1.6;
  scene->add(sun, sun->target, hemisphereLight(Color(0xbfd6ff), Color(0x6b6a55), 1.1));

  auto ground = make<Mesh>(box(400, 0.2, 400), lambert({.color = Color(0x6f8f55)}));
  ground->position.y = -0.2;
  ground->receiveShadow = true;
  scene->add(ground);
  const uint32_t colours[5] = {0xc8483a, 0x3a6fc8, 0xe0c040, 0xf0f0f0, 0x404448};
  for (int i = 0; i < 5; i++) {
    auto m = make<Mesh>(box(1.6 + 0.3 * i, 1.0 + 0.9 * i, 1.6), lambert({.color = Color(colours[i])}));
    m->position.set(-8 + 4.0 * i, 0, -1.5 * i);
    m->rotation.y = 0.3 * i;
    m->castShadow = m->receiveShadow = true;
    scene->add(m);
  }
  // a row of instanced posts, a see-through pane, a line
  auto posts = make<InstancedMesh>(box(0.2, 1.2, 0.2), lambert({.color = Color(0xffffff)}), 40);
  Matrix4 m4;
  for (int i = 0; i < 40; i++) {
    m4.makeTranslation(-20 + i, 0, 5);
    posts->setMatrixAt(i, m4);
    posts->setColorAt(i, Color(i % 2 ? 0xd03020 : 0xf4f4f4));
  }
  posts->castShadow = true;
  scene->add(posts);
  auto pane = make<Mesh>(box(6, 3, 0.05), basic({.color = Color(0x60a0ff), .transparent = true, .opacity = 0.35, .depthWrite = false}));
  pane->position.set(2, 0, 7.5);
  scene->add(pane);

  return ProbeScene{scene, camera};
}

}  // namespace fs::probe

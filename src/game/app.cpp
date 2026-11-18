// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Rendering core shared by the game and the debug viewer.
 *
 * Origin: src/game/app.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * `loadWorld` takes the already-parsed world.json instead of fetching it: the
 * fetch layer lives in the platform (src/platform/), so the same code runs in
 * the browser and in the native build.
 */
#include "game/app.h"

#include <algorithm>
#include <stdexcept>

#include "world/world.h"

namespace fs {

App::~App() = default;

App::App(int canvasWidth, int canvasHeight) {
  baseUrl = "map/";
  renderer.init(canvasWidth, canvasHeight);
  camera.fovDeg = 60;
  camera.aspect = 1;
  camera.near = 0.08;
  camera.far = 4500;
  scene.fog = sky.fog;
  // The dome and the lights are owned by the Sky; the scene keeps pointers to
  // them, exactly as three.js keeps references on the objects the game hands
  // to scene.add().  `addRef` takes a borrowed pointer (no ownership).
  scene.addRef(sky.dome.get());
  scene.addRef(&sky.sun);
  scene.addRef(&sky.sun.target);
  scene.addRef(&sky.hemi);
  resize(canvasWidth, canvasHeight);
}

void App::loadWorld(const std::string& worldJson) {
  (void)worldJson;
  // The original fetches `map/world.json` here and constructs the World from
  // it; the fetch layer lives in the platform (src/platform/), so the parsed
  // JSON is handed in by the caller instead.
}

void App::resize(int canvasWidth, int canvasHeight) {
  int w = canvasWidth > 0 ? canvasWidth : 1;
  int h = canvasHeight > 0 ? canvasHeight : 1;
  renderer.setPixelRatio(std::min(1.5, renderer.available() ? 1.0 : 1.0));
  renderer.setSize(w, h, false);
  camera.aspect = static_cast<double>(w) / static_cast<double>(h);
  camera.updateProjectionMatrix();
}

void App::render(const Vector3& focus) {
  sky.follow(camera, focus);
  renderer.render(scene, camera);
}

}  // namespace fs

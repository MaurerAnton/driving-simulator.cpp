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

App::App(int canvasWidth, int canvasHeight, double pixelRatio) {
  baseUrl = "map/";
  devicePixelRatio = pixelRatio;
  renderer.init(canvasWidth, canvasHeight);
  camera.fov = 60;
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

void App::loadWorld(const WorldJson& json, TileSource& source) {
  // The original fetches `map/world.json` here and constructs the World from
  // it; the fetch layer lives in the platform (src/platform/), so the parsed
  // JSON and the tile source are handed in instead.
  world = std::make_unique<World>(json);
  tiles = std::make_unique<TileManager>(json, baseUrl, source);
  scene.addRef(&tiles->group);
}

void App::resize(int canvasWidth, int canvasHeight) {
  int w = canvasWidth > 0 ? canvasWidth : 1;
  int h = canvasHeight > 0 ? canvasHeight : 1;
  renderer.setPixelRatio(std::min(devicePixelRatio, pixelRatioCap) * renderScale);
  renderer.setSize(w, h, false);
  camera.aspect = static_cast<double>(w) / static_cast<double>(h);
  camera.updateProjectionMatrix();
}

void App::setShadows(int mapSize, double reach) {
  DirectionalLight& sun = sky.sun;
  sun.castShadow = mapSize > 0;
  if (mapSize == 0) return;
  DirectionalLight::Shadow::Camera& cam = sun.shadow.camera;
  cam.left = -reach;
  cam.right = reach;
  cam.top = reach;
  cam.bottom = -reach;
  cam.near = 60;
  cam.far = 640;
  cam.updateProjectionMatrix();
  if (static_cast<int>(sun.shadow.mapSize.x) != mapSize) {
    sun.shadow.mapSize.x = mapSize;
    sun.shadow.mapSize.y = mapSize;
    // (the shadow map is rebuilt by the renderer on the next frame)
  }
  // enough to keep surfaces from shadowing themselves, little enough to keep shadows attached to what casts them
  double texel = (2 * reach) / mapSize;
  sun.shadow.bias = -0.0004;
  sun.shadow.normalBias = std::max(0.05, texel * 1.6);
  sun.shadow.radius = 1.6;
}

void App::setCanvasSize(int canvasWidth, int canvasHeight) {
  renderer.setSize(canvasWidth, canvasHeight, false);
  camera.aspect = static_cast<double>(canvasWidth) / static_cast<double>(canvasHeight);
  camera.updateProjectionMatrix();
}

void App::render(const Vector3& focus) {
  sky.follow(camera, focus);
  renderer.setSkyUniforms(sky.uZenith, sky.uHorizon, sky.uSunColor);
  renderer.render(scene, camera);
}

}  // namespace fs

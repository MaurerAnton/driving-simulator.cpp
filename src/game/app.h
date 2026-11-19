// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Rendering core shared by the game and the debug viewer: the canvas, the
 * WebGL2 renderer, the scene, the camera, the sky, the world and the tile
 * manager that streams the map around the viewer.
 *
 * Origin: src/game/app.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#pragma once

#include <memory>
#include <string>

#include "engine/scene.h"
#include "engine/webgl.h"
#include "render/sky.h"
#include "render/tiles.h"
#include "world/world.h"

namespace fs {

class App {
 public:
  explicit App(int canvasWidth, int canvasHeight);
  ~App();

  WebGLRenderer renderer;
  Scene scene;
  Camera camera;
  Sky sky;
  std::unique_ptr<World> world;
  std::unique_ptr<TileManager> tiles;
  std::string baseUrl;
  double pixelRatioCap = 1.5;

  /** Build the road network and the tile manager from the parsed world. */
  void loadWorld(const WorldJson& json, TileSource& source);
  void resize(int canvasWidth, int canvasHeight);
  void render(const Vector3& focus);
};

}  // namespace fs

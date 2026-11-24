// SPDX-License-Identifier: AGPL-3.0-only
/**
 * What every picture needs: the renderer, the scene, the camera, the sky – and
 * the map that is open (the road network around the player, its tiles).
 *
 * Origin: src/game/app.ts @ v1.7.7
 */
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "core/task.h"
#include "engine/engine.h"
#include "engine/renderer.h"
#include "platform/dom.h"
#include "render/sky.h"
#include "render/tiles.h"
#include "world/loader.h"
#include "world/world.h"

namespace fs {

class App {
 public:
  /** the canvas of the page the picture is drawn on */
  const dom::El canvas;
  Renderer renderer;
  const std::shared_ptr<Scene> scene = make<Scene>();
  const std::shared_ptr<Camera> camera = perspectiveCamera(60, 1, 0.08, 4500);
  Sky sky;
  std::unique_ptr<MapLoader> map;
  World* world = nullptr;
  std::unique_ptr<TileManager> tiles;
  /** where the map's files are (ends in "map/") */
  std::string baseUrl;
  double pixelRatioCap = 1.5;
  /** fraction of the screen's pixels that is drawn (below 1: a softer picture that costs less) */
  double renderScale = 1;

  explicit App(const dom::El& canvasElement);
  ~App();

  /** Open the map: its index and the first piece of the road network; `onProgress` names what is being fetched. */
  Task<void> loadWorld(std::function<void(std::string)> onProgress = nullptr);
  void resize();
  /**
   * The sun's shadows: `mapSize` pixels square (0: none) over `reach` metres around the player.
   */
  void setShadows(int mapSize, double reach);
  void render(const Vector3& focus);

 private:
  int resizeListener_ = 0;
};

}  // namespace fs

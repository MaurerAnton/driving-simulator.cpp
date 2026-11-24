// SPDX-License-Identifier: AGPL-3.0-only
/** App: camera, resize, sky wiring and the render traversal. */
#include "../src/game/app.h"
#include <cmath>
#include <cassert>
#include <cstdio>

using namespace fs;

int main() {
  App app(800, 600);

  // the camera starts where the original puts it
  assert(app.camera.fov == 60);
  assert(app.camera.near == 0.08 && app.camera.far == 4500);
  assert(app.pixelRatioCap == 1.5);
  assert(app.baseUrl == "map/");

  // the scene carries the sky, the sun and the hemisphere light, and the
  // fog is the sky's (so distant terrain blends into the horizon colour)
  assert(app.scene.fog == app.sky.fog);
  assert(app.scene.children.size() >= 3);

  // the aspect ratio follows the canvas
  assert(std::fabs(app.camera.aspect - 800.0 / 600.0) < 1e-9);
  app.resize(1000, 500);
  assert(std::fabs(app.camera.aspect - 2.0) < 1e-9);
  assert(app.renderer.width() == 1000 && app.renderer.height() == 500);

  // a degenerate size must not divide by zero
  app.resize(0, 0);
  assert(app.renderer.width() == 1 && app.renderer.height() == 1);

  // rendering walks the scene: the sky dome is a draw item, drawn first
  app.resize(800, 600);
  app.render(Vector3(0, 0, 0));
  assert(!app.renderer.lastItems().empty());
  bool sawSky = false;
  for (const auto& item : app.renderer.lastItems())
    if (item.kind == MaterialKind::Sky) sawSky = true;
  assert(sawSky);

  // the lights are gathered from the scene (sun + hemisphere)
  assert(app.renderer.lastLights().sunIntensity > 3.0);
  assert(app.renderer.lastLights().hemiIntensity > 1.0);
  assert(app.renderer.lastLights().fogNear == 300);

  std::printf("app: all checks passed (%zu draw items)\n",
              app.renderer.lastItems().size());
  return 0;
}

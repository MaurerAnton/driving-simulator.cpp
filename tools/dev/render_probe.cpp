// SPDX-License-Identifier: AGPL-3.0-only
// LINK: engine render platform core
/**
 * A first picture from the renderer: a few boxes on a ground under the sun, with shadows
 * and fog, drawn off screen and written as a PNG.
 *
 *   tools/t.sh render_probe out.png
 */
#include <cmath>
#include <cstdio>
#include <string>

#include "engine/engine.h"
#include "probe_scene.h"
#include "engine/renderer.h"
#include "platform/glhost.h"

using namespace fs;

int main(int argc, char** argv) {
  const int W = 960, H = 540;
  if (!plat::gl::createContext("view", W, H)) return 1;
  std::printf("driver: %s\n", plat::gl::rendererName().c_str());
  Renderer renderer;
  renderer.init();
  renderer.setSize(W, H);
  renderer.toneMapping = true;
  renderer.toneMappingExposure = 0.85;
  renderer.shadowMap.enabled = true;

  probe::ProbeScene ps = probe::makeScene(W, H);
  auto scene = ps.scene;
  auto camera = ps.camera;
  // (a second argument "top": seen from straight above, north up – where do the shadows fall?)
  if (argc > 2 && std::string(argv[2]) == "top") {
    camera->position.set(0, 45, 0.01);
    camera->lookAt(0, 0, 0);
  }
  if (argc > 2 && std::string(argv[2]) == "posts") {
    camera->position.set(14, 5, 5.01);
    camera->lookAt(14, 0, 5);
  }
  renderer.render(*scene, *camera);
  const Renderer::Info& info = renderer.info;
  std::printf("%d draw calls, %d triangles, %d programs, %d geometries, %d textures\n", info.calls, info.triangles, info.programs, info.geometries, info.textures);
  const char* out = argc > 1 ? argv[1] : "render_probe.png";
  std::printf("%s: %s\n", out, plat::gl::savePng(out, W, H) ? "written" : "NOT written");
  return 0;
}

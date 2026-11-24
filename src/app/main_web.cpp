// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The entry point in the browser.
 *
 * Origin: src/main.ts @ v1.7.7.  (Bringing the port up: for now it proves the host
 * layer – the page, a file over the network, the graphics context – and says so.)
 */
#include <GLES3/gl3.h>
#include <emscripten.h>

#include <cmath>
#include <string>

#include "../../tools/dev/probe_scene.h"
#include "core/task.h"
#include "engine/renderer.h"
#include "platform/dom.h"
#include "platform/glhost.h"
#include "platform/plat.h"

using namespace fs;

namespace {

dom::El line(const std::string& text) {
  dom::El p = dom::el("p", "", text);
  p.style("margin", "4px 0");
  return p;
}

Task<void> boot() {
  dom::El ui = dom::getElementById("ui");
  ui.replaceChildren();
  dom::El box = dom::el("div");
  box.style("position", "absolute").style("left", "16px").style("top", "16px").style("color", "#fff").style("font", "14px system-ui, sans-serif");
  box.style("background", "rgba(0,0,0,.55)").style("padding", "10px 14px").style("border-radius", "10px").style("pointer-events", "auto");
  ui.append(box);
  box.append(line("Fahrsim Baunatal 2 – the C++ port is being brought up."));

  bool gl = plat::gl::createContext("view", plat::innerWidth(), plat::innerHeight());
  box.append(line(std::string("graphics: ") + (gl ? plat::gl::rendererName() : "no WebGL 2")));
  if (gl) {
    // the renderer's test scene (tools/dev/render_probe), drawn every frame
    static Renderer renderer;
    renderer.init();
    renderer.setPixelRatio(plat::devicePixelRatio());
    renderer.setSize(plat::innerWidth(), plat::innerHeight());
    renderer.toneMapping = true;
    renderer.toneMappingExposure = 0.85;
    renderer.shadowMap.enabled = true;
    static probe::ProbeScene ps = probe::makeScene(plat::innerWidth(), plat::innerHeight());
    static dom::El stats = line("");
    box.append(stats);
    emscripten_set_main_loop(
        [] {
          static double last = plat::nowMs(), acc = 0;
          static int frames = 0;
          double now = plat::nowMs();
          double a = now * 0.0002;
          ps.camera->position.set(16 * std::sin(a), 6, 16 * std::cos(a));
          ps.camera->lookAt(0, 1.5, 0);
          renderer.render(*ps.scene, *ps.camera);
          acc += now - last;
          last = now;
          if (++frames == 30) {
            const Renderer::Info& i = renderer.info;
            stats.textContent(std::to_string(static_cast<int>(1000.0 * frames / acc)) + " fps; " + std::to_string(i.calls) + " draw calls, " + std::to_string(i.triangles) +
                              " triangles, " + std::to_string(i.programs) + " programs");
            frames = 0;
            acc = 0;
          }
        },
        0, 0);
  }

  // (where the map is: the page says – beside it when served locally, the 1.7.7 site's on the test site)
  dom::El meta = dom::querySelector("meta[name='fahrsim-map']");
  const std::string mapBase = plat::locationBase() + (meta ? meta.getAttribute("content") : std::string("map/"));
  plat::Response res = co_await plat::fetch(mapBase + "map.json");
  box.append(line(mapBase + "map.json: HTTP " + std::to_string(res.status) + ", " + std::to_string(res.body.size()) + " bytes, " + res.contentType));

  int clicks = 0;
  dom::El b = dom::el("button", "btn", "tap me");
  auto count = std::make_shared<int>(clicks);
  b.onclick([b, count] { b.textContent("tapped " + std::to_string(++*count) + "×"); });
  box.append(b);

  plat::localStorage().setItem("fahrsim2.probe", "ok");
  box.append(line("storage: " + plat::localStorage().getItem("fahrsim2.probe").value_or("none") + "; languages: " + (plat::languages().empty() ? "?" : plat::languages()[0]) +
                  "; " + std::to_string(plat::innerWidth()) + "×" + std::to_string(plat::innerHeight()) + " @" + std::to_string(plat::devicePixelRatio())));
  co_await plat::sleep(50);
  box.append(line("timer: ok after " + std::to_string(static_cast<int>(plat::nowMs())) + " ms"));
  EM_ASM({ window.__fs.ready = true; });
}

}  // namespace

int main() {
  boot().detach();
  return 0;
}

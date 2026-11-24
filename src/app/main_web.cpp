// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The entry point in the browser.
 *
 * Origin: src/main.ts @ v1.7.7.  (Bringing the port up: for now it proves the host
 * layer – the page, a file over the network, the graphics context – and says so.)
 */
#include <GLES3/gl3.h>
#include <emscripten.h>

#include <string>

#include "core/task.h"
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
    glViewport(0, 0, plat::innerWidth(), plat::innerHeight());
    glClearColor(0.16f, 0.36f, 0.6f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
  }

  plat::Response res = co_await plat::fetch(plat::locationBase() + "map/map.json");
  box.append(line("map/map.json: HTTP " + std::to_string(res.status) + ", " + std::to_string(res.body.size()) + " bytes, " + res.contentType));

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

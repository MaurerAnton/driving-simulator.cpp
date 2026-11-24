// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The entry point in the browser.
 *
 * Origin: src/main.ts @ v1.7.7.  (Bringing the port up: until the game itself is
 * on this foundation, the page shows the map – the site's own, or a region chosen
 * from the list – under a camera that is moved with a finger, the mouse or the
 * keys, and says what a frame costs.)
 *
 *   ?region=<id>   a region of the regions' index in place of the site's map
 *   ?at=x,z        where to look (world metres)
 *   ?h=…&pitch=…   the camera's height above the ground and its tilt (degrees, negative: down)
 *   ?radius=…      how far tiles are drawn;  ?shadows=0  none;  ?scale=0.5  fewer pixels
 */
#include <emscripten.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_set>

#include "core/js.h"
#include "core/task.h"
#include "game/app.h"
#include "platform/dom.h"
#include "platform/glhost.h"
#include "platform/plat.h"
#include "world/regions.h"

using namespace fs;

namespace {

/** `new URLSearchParams(location.search).get(name)` ('' when it is not there) */
std::string param(const std::string& name) {
  std::string q = plat::locationSearch();
  if (!q.empty() && q[0] == '?') q = q.substr(1);
  for (const std::string& part : split(q, "&")) {
    size_t eq = part.find('=');
    if (decodeURIComponent(part.substr(0, eq)) != name) continue;
    return eq == std::string::npos ? "" : decodeURIComponent(replaceAll(part.substr(eq + 1), "+", " "));
  }
  return "";
}

double numParam(const std::string& name, double otherwise) {
  const std::string v = param(name);
  if (v.empty()) return otherwise;
  const double n = jsNumber(v);
  return std::isfinite(n) ? n : otherwise;
}

/** What the page is looking at, and the hands on it. */
struct View {
  double x = 0, z = 0, height = 60, yaw = 0, pitch = -25;
  std::unordered_set<std::string> keys;
  bool dragging = false, turning = false;
  double lastX = 0, lastY = 0;
  /** the distance of two fingers at their last move (0: there are not two) */
  double pinch = 0;
};

std::unique_ptr<App> app;
View view;
dom::El stats, note;
double lastFrame = 0, acc = 0, worst = 0;
int frames = 0;

void say(const std::string& text) {
  if (note) note.textContent(text);
}

double clampHeight(double h) { return std::max(2.0, std::min(3000.0, h)); }

void frame() {
  const double now = plat::nowMs();
  const double dt = std::min(0.25, (now - lastFrame) / 1000);
  lastFrame = now;
  // the keys: W A S D / the arrows move over the ground, Q E turn, R F rise and sink
  const double speed = std::max(20.0, view.height * 1.5) * dt;
  const double fx = std::sin(view.yaw * DEG), fz = -std::cos(view.yaw * DEG);
  auto down = [](const char* a, const char* b) { return view.keys.count(a) > 0 || view.keys.count(b) > 0; };
  if (down("KeyW", "ArrowUp")) view.x += fx * speed, view.z += fz * speed;
  if (down("KeyS", "ArrowDown")) view.x -= fx * speed, view.z -= fz * speed;
  if (down("KeyA", "ArrowLeft")) view.x += fz * speed, view.z -= fx * speed;
  if (down("KeyD", "ArrowRight")) view.x -= fz * speed, view.z += fx * speed;
  if (down("KeyQ", "KeyQ")) view.yaw -= 60 * dt;
  if (down("KeyE", "KeyE")) view.yaw += 60 * dt;
  if (down("KeyR", "KeyR")) view.height = clampHeight(view.height * (1 + dt));
  if (down("KeyF", "KeyF")) view.height = clampHeight(view.height * (1 - dt));

  app->map->update(view.x, view.z);
  app->tiles->update(view.x, view.z);
  double ground = app->tiles->heightAt(view.x, view.z);
  if (std::isnan(ground)) ground = 0;
  Camera& cam = *app->camera;
  cam.position.set(view.x, ground + view.height, view.z);
  cam.rotation.set(view.pitch * DEG, -view.yaw * DEG, 0, Euler::YXZ);
  app->render(Vector3(view.x, ground, view.z));

  acc += dt;
  worst = std::max(worst, dt);
  if (++frames >= 30) {
    const Renderer::Info& i = app->renderer.info;
    const int tiles = static_cast<int>(app->tiles->tiles.size()), roads = static_cast<int>(app->world->allEdges.size());
    stats.textContent(std::to_string(static_cast<int>(std::round(frames / acc))) + " fps (worst frame " + std::to_string(static_cast<int>(worst * 1000)) + " ms) · " +
                      std::to_string(i.calls) + " draw calls · " + std::to_string(i.triangles) + " triangles · " + std::to_string(tiles) + " tiles · " + std::to_string(roads) +
                      " roads");
    // (for tools/dev/shot.mjs: fps, worst frame in ms, draw calls, triangles, tiles, roads, x, z)
    EM_ASM({ window.__fs.stats = [ $0, $1, $2, $3, $4, $5, $6, $7 ]; }, frames / acc, worst * 1000, i.calls, i.triangles, tiles, roads, view.x, view.z);
    frames = 0;
    acc = 0;
    worst = 0;
  }
}

/** The list of the regions: choosing one opens it (the page is loaded again with ?region=). */
Task<void> regionList(dom::El box, std::string current) {
  std::optional<RegionIndex> index = co_await loadRegionIndex(plat::locationBase());
  if (!index || index->regions.empty()) co_return;
  dom::El pick = dom::el("select");
  pick.style("font", "inherit").style("margin-top", "6px").style("max-width", "100%").style("min-height", "36px");
  dom::El own = dom::el("option", "", "North Hesse (the site's own map)");
  own.value("");
  pick.append(own);
  std::vector<const Region*> sorted;
  for (const Region& r : index->regions) sorted.push_back(&r);
  std::sort(sorted.begin(), sorted.end(), [](const Region* a, const Region* b) { return a->id < b->id; });
  for (const Region* r : sorted) {
    dom::El o = dom::el("option", "", r->name + " (" + r->id + ")");
    o.value(r->id);
    pick.append(o);
  }
  pick.value(current);
  pick.on("change", [pick] {
    const std::string id = pick.value();
    plat::replaceSearch(id.empty() ? "" : "?region=" + encodeURIComponent(id));
    plat::reload();
  });
  box.append(pick);
}

void hands(const dom::El& canvas) {
  canvas.style("touch-action", "none");
  canvas.on("pointerdown", [canvas](dom::Event& e) {
    view.dragging = true;
    view.turning = e.button == 2 || e.shiftKey;
    view.lastX = e.clientX;
    view.lastY = e.clientY;
    canvas.setPointerCapture(e.pointerId);
  });
  canvas.on("pointermove", [](dom::Event& e) {
    if (!view.dragging || !e.isPrimary || view.pinch > 0) return;
    const double dx = e.clientX - view.lastX, dy = e.clientY - view.lastY;
    view.lastX = e.clientX;
    view.lastY = e.clientY;
    if (view.turning) {
      view.yaw += dx * 0.25;
      view.pitch = std::max(-89.0, std::min(10.0, view.pitch - dy * 0.2));
      return;
    }
    // (the ground follows the finger: a pixel is about this many metres at the picture's middle)
    const double m = view.height / std::max(120.0, 0.6 * plat::innerHeight());
    const double fx = std::sin(view.yaw * DEG), fz = -std::cos(view.yaw * DEG);
    // right of the picture is (-fz, fx); a drag to the right moves the view to the left, a drag down moves it forward
    view.x += (dx * fz + dy * fx) * m;
    view.z += (-dx * fx + dy * fz) * m;
  });
  auto up = [](dom::Event&) { view.dragging = false; };
  canvas.on("pointerup", up);
  canvas.on("pointercancel", up);
  canvas.on("contextmenu", [](dom::Event& e) { e.preventDefault(); });
  canvas.on(
      "wheel",
      [](dom::Event& e) {
        e.preventDefault();
        view.height = clampHeight(view.height * std::exp(e.deltaY * 0.001));
      },
      dom::ListenOptions{.capture = false, .passive = false, .once = false});
  // (two fingers apart or together: the height)
  canvas.on("touchmove", [](dom::Event& e) {
    if (e.touches.size() != 2) {
      view.pinch = 0;
      return;
    }
    const double d = std::hypot(e.touches[0].clientX - e.touches[1].clientX, e.touches[0].clientY - e.touches[1].clientY);
    if (view.pinch > 0 && d > 0) view.height = clampHeight(view.height * view.pinch / d);
    view.pinch = d;
  });
  canvas.on("touchend", [](dom::Event&) { view.pinch = 0; });
  dom::window().on("keydown", [](dom::Event& e) { view.keys.insert(e.code); });
  dom::window().on("keyup", [](dom::Event& e) { view.keys.erase(e.code); });
  dom::window().on("blur", [] { view.keys.clear(); });
}

Task<void> boot() {
  dom::El ui = dom::getElementById("ui");
  ui.replaceChildren();
  dom::El box = dom::el("div");
  box.style("position", "absolute").style("left", "12px").style("top", "12px").style("max-width", "min(520px, calc(100vw - 24px))").style("color", "#fff");
  box.style("font", "13px system-ui, sans-serif").style("background", "rgba(0,0,0,.55)").style("padding", "8px 12px").style("border-radius", "10px");
  box.style("pointer-events", "auto");
  ui.append(box);
  box.append(dom::el("div", "", "Fahrsim Baunatal 2 – the C++ port, being brought up: the map."));
  note = dom::el("div", "", "starting …");
  stats = dom::el("div");
  box.append(note, stats);

  if (!plat::gl::createContext("view", plat::innerWidth(), plat::innerHeight())) {
    say("This browser has no WebGL 2: the picture cannot be drawn.");
    co_return;
  }
  app = std::make_unique<App>(dom::getElementById("view"));
  // (where the map is: the page says – beside it when served locally, the 1.7.7 site's on the test site)
  dom::El meta = dom::querySelector("meta[name='fahrsim-map']");
  if (meta && !meta.getAttribute("content").empty()) app->baseUrl = plat::locationBase() + meta.getAttribute("content");
  app->renderScale = numParam("scale", 1);
  app->resize();

  // the region asked for, if the index knows it
  const std::string wanted = param("region");
  std::optional<Region> region;
  if (!wanted.empty()) {
    say("reading the list of regions …");
    std::optional<RegionIndex> index = co_await loadRegionIndex(plat::locationBase());
    const Region* r = index ? regionById(&*index, wanted) : nullptr;
    if (r) region = *r;
  }
  say(region ? "opening " + region->name + " …" : "opening the map …");
  const double t0 = plat::nowMs();
  std::string why;
  try {
    co_await app->loadWorld(nullptr, region);
  } catch (const std::exception& e) {
    why = std::string("The map could not be opened: ") + e.what();
  }
  if (!why.empty()) {
    say(why);
    co_return;
  }
  const std::string name = app->regionId.empty() || !region ? std::string("The site's map") : region->name;
  say(name + ", build " + app->map->version + ", opened in " + std::to_string(static_cast<int>(plat::nowMs() - t0)) +
      " ms.  Drag: move · wheel or two fingers: height · right button, Shift or Q/E: turn.");

  // where to look: asked for, or the middle of the region, or the site's own start
  if (region && !app->regionId.empty()) {
    view.x = (region->box[0] + region->box[2]) / 2;
    view.z = (region->box[1] + region->box[3]) / 2;
  }
  const std::string at = param("at");
  if (!at.empty()) {
    const std::vector<std::string> xz = split(at, ",");
    if (xz.size() == 2) view.x = jsNumber(xz[0]), view.z = jsNumber(xz[1]);
  }
  view.height = clampHeight(numParam("h", 60));
  view.pitch = numParam("pitch", -25);
  view.yaw = numParam("yaw", 0);
  app->tiles->viewRadius = numParam("radius", 1400);
  const int shadows = static_cast<int>(numParam("shadows", 2048));
  if (shadows > 0) app->setShadows(shadows, 220);

  hands(app->canvas);
  lastFrame = plat::nowMs();
  emscripten_set_main_loop(frame, 0, 0);
  EM_ASM(window.__fs.ready = true);
  co_await regionList(box, app->regionId);
}

}  // namespace

int main() {
  boot().detach();
  return 0;
}

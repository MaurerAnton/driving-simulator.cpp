// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/app.ts @ v1.7.7
#include "game/app.h"

#include <cmath>

#include "core/js.h"
#include "platform/plat.h"

namespace fs {

namespace {

/** the share of the traffic lights that are out of order on a day (the original's LIGHTS_OUT) */
const double LIGHTS_OUT = 0.06;

/** `new URLSearchParams(location.search).get(name)` ('' when it is not there; `has` says whether it is) */
std::string searchParam(const std::string& name, bool* has = nullptr) {
  std::string q = plat::locationSearch();
  if (has) *has = false;
  if (!q.empty() && q[0] == '?') q = q.substr(1);
  for (const std::string& part : split(q, "&")) {
    size_t eq = part.find('=');
    std::string k = decodeURIComponent(replaceAll(part.substr(0, eq), "+", " "));
    if (k != name) continue;
    if (has) *has = true;
    return eq == std::string::npos ? "" : decodeURIComponent(replaceAll(part.substr(eq + 1), "+", " "));
  }
  return "";
}

/** The files of the map the site serves, by their path under `base` (and the map's build once it is known). */
class SiteFiles : public MapFiles {
 public:
  explicit SiteFiles(std::string base) : base_(std::move(base)) {}
  std::string version;

  Task<std::optional<Bytes>> bytes(std::string path) override {
    plat::Request req;
    req.url = base_ + path + (version.empty() ? "" : "?v=" + version);
    if (path == "map.json") req.cache = "no-cache";
    plat::Response res = co_await plat::fetch(std::move(req));
    // (a server that answers every unknown address with its start page: that is "not there")
    if (res.status == 404 || res.contentType.find("text/html") != std::string::npos) co_return std::nullopt;
    if (!res.ok()) throw std::runtime_error(path + ": HTTP " + std::to_string(res.status));
    if (plat::isGzip(res.body)) co_return plat::gunzip(res.body);
    co_return std::move(res.body);
  }

 private:
  std::string base_;
};

}  // namespace

App::App(const dom::El& canvasElement) : canvas(canvasElement) {
  baseUrl = plat::locationBase() + "map/";
  renderer.init();
  renderer.toneMapping = true;   // THREE.ACESFilmicToneMapping
  renderer.toneMappingExposure = 0.85;
  renderer.shadowMap.enabled = true;
  scene->fog = sky.fog;
  scene->add(sky.dome, sky.sun, sky.sun->target, sky.hemi);
  resize();
  resizeListener_ = dom::window().on("resize", [this] { resize(); });
}

App::~App() { dom::window().off("resize", resizeListener_); }

Task<void> App::loadWorld(std::function<void(std::string)> onProgress) {
  if (onProgress) onProgress("world");
  auto files = std::make_shared<SiteFiles>(baseUrl);
  // a few traffic lights are out of order every day (others each day); ?lightsout=0…1 sets the share
  bool asked = false;
  const double share0 = jsNumber(searchParam("lightsout", &asked));
  const double share = asked && std::isfinite(share0) ? std::max(0.0, std::min(1.0, share0)) : LIGHTS_OUT;
  const DateFields today = plat::localFields(plat::dateMs());
  const int day = today.year * 372 + today.month * 31 + today.date;
  WorldOptions options;
  // (which lights are out is decided by sim/signals' lightsOutBy(day, share): wired when that layer is merged)
  (void)day;
  (void)share;
  map = co_await MapLoader::open(files, options);
  co_await plat::sleep(0);
  files->version = map->version;
  world = &map->world;
  tiles = std::make_unique<TileManager>(map->tiles, baseUrl, map->version);
  scene->add(tiles->group);
}

void App::resize() {
  const double cw = canvas ? canvas.clientWidth() : 0, ch = canvas ? canvas.clientHeight() : 0;
  const int w = static_cast<int>(cw > 0 ? cw : plat::innerWidth());
  const int h = static_cast<int>(ch > 0 ? ch : plat::innerHeight());
  const double dpr = plat::devicePixelRatio();
  renderer.setPixelRatio(std::min(dpr > 0 ? dpr : 1.0, pixelRatioCap) * renderScale);
  renderer.setSize(w, h);
  camera->aspect = static_cast<double>(w) / static_cast<double>(h > 0 ? h : 1);
  camera->updateProjectionMatrix();
}

void App::setShadows(int mapSize, double reach) {
  Light& sun = *sky.sun;
  sun.castShadow = mapSize > 0;
  if (mapSize == 0) return;
  Camera& cam = *sun.shadow.camera;
  cam.left = -reach;
  cam.right = reach;
  cam.top = reach;
  cam.bottom = -reach;
  cam.near = 60;
  cam.far = 640;
  cam.updateProjectionMatrix();
  if (sun.shadow.mapSize.x != mapSize) {
    sun.shadow.mapSize.set(mapSize, mapSize);
    sun.shadow.map = nullptr;
  }
  // One texel of the shadow map covers this many metres: the bias along the normal that
  // keeps a surface from shadowing itself grows with it.
  const double texel = (2 * reach) / mapSize;
  sun.shadow.bias = -0.0004;
  sun.shadow.normalBias = std::max(0.05, texel * 1.6);
  sun.shadow.radius = 1.6;
}

void App::render(const Vector3& focus) {
  sky.follow(*camera, focus);
  renderer.render(*scene, *camera);
}

}  // namespace fs

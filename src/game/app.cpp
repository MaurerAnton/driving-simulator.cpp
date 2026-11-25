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

/**
 * The files of a region: its map.json, road network and network chunks, kept under
 * regions/<id>/ of the map-assets (world/regionfiles.ts' regionFiles over regionFetchSource).
 */
class RegionFiles : public MapFiles {
 public:
  RegionFiles(std::string root, std::string id) : root_(std::move(root)), id_(std::move(id)) {}

  Task<std::optional<Bytes>> bytes(std::string path) override {
    plat::Response res = co_await plat::fetch(root_ + std::string(REGIONS_DIR) + "/" + id_ + "/" + path);
    if (!res.ok()) co_return std::nullopt;
    if (plat::isGzip(res.body)) {
      // (a file that only looks like gzip is given as it is, as the original does)
      Bytes plain = plat::gunzip(res.body);
      if (!plain.empty()) co_return plain;
    }
    co_return std::move(res.body);
  }

 private:
  std::string root_, id_;
};

/** "…/tiles/412_398.fsb?v=…" → "412_398" ("" when the address is not a tile's) */
std::string tileKeyOf(const std::string& url) {
  const size_t at = url.rfind("tiles/");
  if (at == std::string::npos) return "";
  const size_t from = at + 6, dot = url.find(".fsb", from);
  return dot == std::string::npos ? "" : url.substr(from, dot - from);
}

}  // namespace

App::App(const dom::El& canvasElement) : canvas(canvasElement) {
  baseUrl = plat::locationBase() + "map/";
  // (the page may say where the map is: the test site takes the 1.7.7 site's – web/index.html, <meta name="fahrsim-map">)
  if (dom::El meta = dom::querySelector("meta[name='fahrsim-map']")) {
    const std::string at = meta.getAttribute("content");
    if (!at.empty()) baseUrl = plat::locationBase() + at;
  }
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

Task<void> App::loadWorld(std::function<void(std::string)> onProgress, std::optional<Region> region) {
  if (onProgress) onProgress("world");
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
  // a region chosen (settings: region) comes from map-assets: its own map.json and network.  The
  // world frame stays the shared one every region was built in, so no origin is set here.
  regionId.clear();
  regionTiles = nullptr;
  std::shared_ptr<SiteFiles> site;
  if (region) {
    // 2.0: its tiles are fetched one at a time out of the parts (1.7.7 downloads whole parts)
    auto parts = std::make_shared<RegionTiles>(plat::locationBase() + "parts/", MAP_ASSETS_URL);
    // (a few regions' index names the wrong parts: parts/regions.json says which really hold their tiles –
    // tools/build-region-parts.mjs)
    std::vector<std::string> partFiles = region->tiles;
    {
      plat::Response fixes = co_await plat::fetch(plat::locationBase() + "parts/regions.json");
      if (fixes.ok()) {
        // (no correction for it, or none readable: the index's own list)
        Json j;
        if (Json::parse(fixes.text(), j) && j.isObject()) {
          const Json& f = j.at("fixes").at(region->id);
          if (f.isArray() && !f.array.empty()) {
            partFiles.clear();
            for (const Json& p : f.array) partFiles.push_back(p.str);
          }
        }
      }
    }
    if (co_await parts->open(partFiles)) {
      // (the region's own index or network unreadable – offline, a file missing: the site's map, as when its parts are)
      std::string why;
      try {
        map = co_await MapLoader::open(std::make_shared<RegionFiles>(MAP_ASSETS_URL, region->id), options);
      } catch (const std::exception& e) {
        why = e.what();
      }
      if (why.empty() && map) {
        regionTiles = parts;
        regionId = region->id;
      } else {
        map = nullptr;
        plat::warn("[region] " + region->id + ": its map could not be opened (" + why + "): the site's map is loaded");
      }
    } else {
      plat::warn("[region] " + region->id + ": the index of its tiles could not be read: the site's map is loaded");
    }
  }
  if (!regionTiles) {
    site = std::make_shared<SiteFiles>(baseUrl);
    map = co_await MapLoader::open(site, options);
  }
  // (a breath for the page between the big steps, so that the loading screen is drawn)
  co_await plat::sleep(0);
  if (site) site->version = map->version;
  world = &map->world;
  tiles = std::make_unique<TileManager>(map->tiles, baseUrl, map->version);
  if (regionTiles) {
    // the region's tiles come from its parts; anything else the renderer asks for, from the site
    std::shared_ptr<RegionTiles> parts = regionTiles;
    tiles->fetch = [parts](std::string url) -> Task<plat::Response> {
      const std::string key = tileKeyOf(url);
      if (key.empty()) return plat::fetch(std::move(url));
      return parts->fetch(key);
    };
  }
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

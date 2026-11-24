// LINK: ui reality i18n game sim audio world geo core platform
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * 2.0: one map for all regions (docs/WORLD-MAP.md) on the native page: the big
 * map with a WorldLayer made of the countries' outlines (web/data/outline.bin)
 * and a regions index made here.  Zooming out shows the layer, the globe goes
 * to every region and back, a tap selects a region and shows its card, the
 * card's button calls the game back, and the search field finds a region.
 *
 *   tools/t.sh worldmap_test
 */
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/task.h"
#include "game/address.h"
#include "game/tourlog.h"
#include "game/track.h"
#include "geo/utm.h"
#include "platform/canvas.h"
#include "platform/dom.h"
#include "platform/native/canvas_native.h"
#include "platform/native/dom_native.h"
#include "platform/native/maps_native.h"
#include "platform/native/native.h"
#include "platform/plat.h"
#include "ui/bigmap.h"
#include "ui/worldlayer.h"
#include "ui/worldoutline.h"
#include "world/regions.h"
#include "world/types.h"
#include "world/world.h"

using namespace fs;
using dom::El;

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
  }
}

Bytes fileBytes(const char* path) {
  std::ifstream f(path, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

constexpr double W = 400;
constexpr double H = 700;

/** a box of `r` metres round a place */
std::array<double, 4> around(double lat, double lon, double r) {
  const auto [x, z] = latLonToWorld(lat, lon);
  return {x - r, z - r, x + r, z + r};
}

/** the middle of a box */
std::pair<double, double> mid(const std::array<double, 4>& b) { return {(b[0] + b[2]) / 2, (b[1] + b[3]) / 2}; }

void settle() {
  for (int i = 0; i < 8; i++) {
    plat::native::drain();
    plat::native::runFrames();
  }
}

void pointer(const El& c, const std::string& type, double x, double y, int id = 1) {
  dom::Event e;
  e.type = type;
  e.clientX = x;
  e.clientY = y;
  e.pointerId = id;
  e.pointerType = "touch";
  dom::native::dispatch(c, e);
}

void tap(const El& c, double x, double y) {
  pointer(c, "pointerdown", x, y);
  pointer(c, "pointerup", x, y);
  settle();
}

void wheel(const El& c, double deltaY, int times) {
  for (int i = 0; i < times; i++) {
    dom::Event e;
    e.type = "wheel";
    e.clientX = W / 2;
    e.clientY = H / 2;
    e.deltaY = deltaY;
    dom::native::dispatch(c, e);
  }
  settle();
}

bool drew(const El& canvasEl, const std::string& text) {
  const std::vector<std::string> t = canvasNative::texts(Canvas(canvasEl).getContext2D());
  return std::find(t.begin(), t.end(), text) != t.end();
}

void forget(const El& canvasEl) { canvasNative::forget(Canvas(canvasEl).getContext2D()); }

struct Game {
  std::unique_ptr<World> world;
  DrivenPath path{nullptr};
  std::vector<std::string> opened;
  BigMapHost host;

  explicit Game(const std::array<double, 4>& site) {
    WorldJson json;
    json.version = 1;
    json.meta.name = "Baunatal";
    json.meta.minX = site[0];
    json.meta.minZ = site[1];
    json.meta.maxX = site[2];
    json.meta.maxZ = site[3];
    json.meta.tileSize = 500;
    world = std::make_unique<World>(std::move(json));
    const auto [carX, carZ] = mid(site);
    host.world = world.get();
    host.net = []() -> NetIndex* { return nullptr; };
    host.lang = [] { return std::string("en"); };
    host.car = [carX, carZ] { return BigMapHost::Car{carX, carZ, 0}; };
    host.path = &path;
    host.spotAt = [](double, double) -> std::optional<SpawnJson> { return std::nullopt; };
    host.canTeleport = [] { return true; };
    host.teleport = [](const SpawnJson&) {};
    host.loadAddresses = []() -> Task<AddressIndex*> { co_return nullptr; };
    host.spotOf = [](const AddressHit&) -> std::optional<SpawnJson> { return std::nullopt; };
    host.startTrip = [](const std::vector<SpawnJson>&, bool, const TourPreview*) {};
    host.planTour = [](double, std::optional<SpawnJson>, std::function<void(double)>) -> Task<std::optional<TourPreview>> { co_return std::nullopt; };
    host.roadsKnown = [] { return false; };
    host.tourPrefs = [] { return TourPrefs{}; };
    host.setTourPrefs = [](const TourPrefs&) {};
    host.tripActive = [] { return false; };
    host.endTrip = [] {};
    host.latLon = [](double x, double z) { return worldToLatLon(x, z); };
    host.aerial = [] { return false; };
    host.setAerial = [](bool) {};
    host.close = [] {};
    host.region = [] { return std::string(WorldLayer::SITE); };
    host.onOpenRegion = [this](const Region& r) { opened.push_back(r.id); };
  }
};

}  // namespace

int main() {
  plat::native::manualClock(true);
  dom::native::reset();

  const WorldOutline outline = parseWorldOutline(fileBytes("web/data/outline.bin"));
  check(!outline.empty(), "web/data/outline.bin is read");

  const std::array<double, 4> site = around(51.2556, 9.4183, 8000);  // (Baunatal)
  RegionIndex index;
  index.latest = "bremen";
  index.regions.push_back({"bremen", "Bremen", around(53.0793, 8.8017, 14000), "regions/bremen/map.json", {"34.tar"}});
  index.regions.push_back({"muenchen", "München", around(48.1372, 11.5756, 18000), "regions/muenchen/map.json", {"40.tar"}});
  index.regions.push_back({"wien", "Wien", around(48.2082, 16.3738, 15000), "regions/wien/map.json", {"51.tar"}});

  // ---- without a layer: the map of 1.7.7, no globe, no world however far out ----
  {
    Game game(site);
    BigMap map(game.host);
    map.open(dom::body());
    El c = dom::body().querySelector(".bigmap canvas");
    dom::native::setRect(c, {0, 0, W, H});
    settle();
    El globe = dom::body().querySelector(".bm-world");
    check(globe && globe.hidden(), "no layer: the globe is hidden");
    forget(c);
    wheel(c, 120, 80);
    check(!drew(c, "Bremen") && !drew(c, "Wien"), "no layer: no regions drawn");
    map.close();
  }

  // ---- with the world ----
  auto layer = std::make_shared<WorldLayer>();
  layer->set(outline, &index, site, "Baunatal");
  layer->setDownloadBytes("bremen", 48.4e6);
  check(layer->ready() && layer->hasRegions(), "the layer is ready");
  check(layer->regions().size() == 4 && layer->regions()[0].id == WorldLayer::SITE, "the site's map first, then the index");
  const Region* bremen = layer->byId("bremen");
  check(bremen && layer->countryOf(*bremen, "en") == "Germany" && layer->countryOf(*bremen, "de") == "Deutschland", "Bremen lies in Germany");
  const Region* wien = layer->byId("wien");
  check(wien && layer->countryOf(*wien, "en") == "Austria", "Vienna lies in Austria");

  Game game(site);
  BigMap map(game.host, layer);
  map.open(dom::body());
  El c = dom::body().querySelector(".bigmap canvas");
  dom::native::setRect(c, {0, 0, W, H});
  settle();
  El globe = dom::body().querySelector(".bm-world");
  check(globe && !globe.hidden(), "the globe is in the bar");
  check(globe.getAttribute("aria-label") == "All regions", "the globe's label");
  El card = dom::body().querySelector(".bm-region");
  check(card && card.hidden(), "close up: no card");

  // zooming out shows the layer: the regions' names, the hint, the way back
  forget(c);
  wheel(c, 120, 80);
  check(drew(c, "Bremen") || drew(c, "München") || drew(c, "Wien"), "far out: the regions are drawn");
  check(!card.hidden(), "far out: the card says what to do");
  check(dom::native::text(card).find("Tap a region") != std::string::npos, "far out: the hint");
  check(static_cast<bool>(dom::native::find(card, "Back to Baunatal", "button")), "far out: the way back");
  // … and back in
  wheel(c, -120, 80);
  check(card.hidden(), "back in: no card");

  // the globe: every region in view – and pressed again, back to the view before
  globe.click();
  settle();
  check(globe.getAttribute("aria-pressed") == "true", "the globe is pressed");
  check(!card.hidden(), "every region: the card's hint");
  // a tap on Bremen (where the globe put it) selects it and shows its card
  MapView v = layer->fitAll(W, H);
  v.scale = std::max(WorldLayer::MIN_SCALE, std::min(v.scale, WorldLayer::WORLD_SCALE * 0.75));
  v.width = W;
  v.height = H;
  {
    const auto [bx, bz] = mid(bremen->box);
    const Region* under = layer->regionAt(v, v.sx(bx), v.sy(bz));
    check(under && under->id == "bremen", "regionAt finds Bremen");
    tap(c, v.sx(bx), v.sy(bz));
  }
  check(map.chosenRegion() && map.chosenRegion()->id == "bremen", "the tap chose Bremen");
  const std::string cardText = dom::native::text(card);
  check(cardText.find("Bremen") != std::string::npos, "the card: its name");
  check(cardText.find("Germany") != std::string::npos, "the card: its country");
  check(cardText.find("Download: 48 MB") != std::string::npos, "the card: its size");
  El openBtn = dom::native::find(card, "Open this map", "button");
  check(static_cast<bool>(openBtn), "the card: its button");
  // the button calls the game back with the region
  if (openBtn) openBtn.click();
  settle();
  check(game.opened == std::vector<std::string>{"bremen"}, "Open this map → onOpenRegion(Bremen)");

  // a tap on the sea: nothing chosen any more
  {
    const auto [sx, sz] = latLonToWorld(56.0, 3.0);  // (the North Sea)
    tap(c, v.sx(sx), v.sy(sz));
  }
  check(!map.chosenRegion(), "a tap on the sea: nothing chosen");

  // a tap on the loaded map flies back into it
  {
    const auto [hx, hz] = mid(site);
    tap(c, v.sx(hx), v.sy(hz));
  }
  check(card.hidden() && globe.getAttribute("aria-pressed") == "false", "the loaded region: back into it");

  // with keys: Home goes to every region and back
  c.focus();
  dom::native::key("Home");
  settle();
  check(globe.getAttribute("aria-pressed") == "true", "Home: every region");
  dom::native::key("Home");
  settle();
  check(globe.getAttribute("aria-pressed") == "false", "Home again: back");

  // the search field finds a region (after the places of the loaded map), and choosing it selects it
  {
    El input = dom::body().querySelector(".bm-search input");
    check(static_cast<bool>(input), "the search field");
    dom::native::dispatch(input, "focus");
    settle();
    dom::native::type(input, "wien");
    settle();
    El list = dom::body().querySelector(".bm-search .addr-list");
    El hit = dom::native::find(list, "Wien", "button");
    check(static_cast<bool>(hit), "the search finds Vienna");
    check(hit && dom::native::text(hit).find("Austria") != std::string::npos, "… with its country");
    if (hit) hit.click();
    settle();
    check(map.chosenRegion() && map.chosenRegion()->id == "wien", "choosing it selects Vienna");
    check(dom::native::text(card).find("Wien") != std::string::npos, "… and shows its card");
    // by its country
    dom::native::type(input, "austria");
    settle();
    check(static_cast<bool>(dom::native::find(list, "Wien", "button")), "the search finds Vienna by its country");
    dom::native::type(input, "");
    settle();
  }

  // showRegion of the loaded one: back into it
  map.showRegion(WorldLayer::SITE);
  settle();
  check(!map.chosenRegion() && card.hidden(), "showRegion(site): back into the map");

  map.close();
  check(!dom::body().querySelector(".bigmap"), "closed");

  if (failures) {
    std::fprintf(stderr, "worldmap_test: %d failed\n", failures);
    return 1;
  }
  std::printf("worldmap_test: ok\n");
  return 0;
}

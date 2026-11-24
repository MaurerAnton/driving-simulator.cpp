// SPDX-License-Identifier: AGPL-3.0-only
/**
 * 2.0: the big map as a map app (none of this is in 1.7.7, so there is no test of the original to mirror):
 *
 *   the light style beside the dark one (ui/mapstyle.h), chosen in the map's bar and kept;
 *   the schema of the whole network (ui/mapoverview.h): the roads by their kind and zoom, joined and thinned
 *     per zoom band, the names of the places ranked and kept off one another – and how long the real map takes;
 *   the search that finds everything: every town before anything is typed, the streets of the loaded pieces
 *     (ui/streetbook.h), the online place search (ui/onlinesearch.h – Photon, asked once typing pauses);
 *   another region's schema when one zooms into it, "Put the car here" on its road, the place kept for the
 *     reload (game/startat.h) – and the game booting straight into a free drive there.
 *
 *   tools/t.sh mapview_test
 */
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/task.h"
#include "game/address.h"
#include "game/game.h"
#include "game/startat.h"
#include "game/tourlog.h"
#include "game/track.h"
#include "geo/utm.h"
#include "platform/canvas.h"
#include "platform/dom.h"
#include "platform/glhost.h"
#include "platform/json.h"
#include "platform/native/canvas_native.h"
#include "platform/native/dom_native.h"
#include "platform/native/maps_native.h"
#include "platform/native/native.h"
#include "platform/plat.h"
#include "ui/addresspicker.h"
#include "ui/bigmap.h"
#include "ui/mapoverview.h"
#include "ui/mapstyle.h"
#include "ui/onlinesearch.h"
#include "ui/streetbook.h"
#include "ui/worldlayer.h"
#include "world/netfile.h"
#include "world/regions.h"
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

const std::string SCRATCH = "/tmp/fahrsim-mapview-test/";

constexpr double W = 400;
constexpr double H = 700;

Bytes fileBytes(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
void writeFile(const std::string& path, const Bytes& b) {
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}
void writeText(const std::string& path, const std::string& s) { writeFile(path, Bytes(s.begin(), s.end())); }

std::array<double, 4> around(double lat, double lon, double r) {
  const auto [x, z] = latLonToWorld(lat, lon);
  return {x - r, z - r, x + r, z + r};
}
std::pair<double, double> mid(const std::array<double, 4>& b) { return {(b[0] + b[2]) / 2, (b[1] + b[3]) / 2}; }

void settle() {
  for (int i = 0; i < 8; i++) {
    plat::native::drain();
    plat::native::runFrames();
  }
}
void later(double ms) {
  plat::native::advance(ms);
  settle();
}

void pointer(const El& c, const std::string& type, double x, double y) {
  dom::Event e;
  e.type = type;
  e.clientX = x;
  e.clientY = y;
  e.pointerId = 1;
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
bool drewPart(const El& canvasEl, const std::string& part) {
  for (const std::string& t : canvasNative::texts(Canvas(canvasEl).getContext2D()))
    if (t.find(part) != std::string::npos) return true;
  return false;
}

/**
 * A small network: a motorway of three roads in a row (from x0 eastwards, 3 km), a residential street across
 * its middle, a secondary road beside it.  All at z = z0.
 */
NetData smallNet(double x0, double z0) {
  NetData d;
  // junctions 0 … 3 along the motorway, 4 / 5 the ends of the street across, 6 / 7 the secondary road
  const double xs[8] = {x0, x0 + 1000, x0 + 2000, x0 + 3000, x0 + 1500, x0 + 1500, x0, x0 + 3000};
  const double zs[8] = {z0, z0, z0, z0, z0 - 600, z0 + 600, z0 + 300, z0 + 300};
  for (int i = 0; i < 8; i++) {
    d.nodeX.push_back(static_cast<float>(xs[i]));
    d.nodeZ.push_back(static_cast<float>(zs[i]));
  }
  const int ends[5][2] = {{0, 1}, {1, 2}, {2, 3}, {4, 5}, {6, 7}};
  const uint8_t cls[5] = {MOTORWAY, MOTORWAY, MOTORWAY, RESIDENTIAL, SECONDARY};
  for (int e = 0; e < 5; e++) {
    d.edgeA.push_back(static_cast<uint32_t>(ends[e][0]));
    d.edgeB.push_back(static_cast<uint32_t>(ends[e][1]));
    d.edgeLen.push_back(static_cast<float>(std::hypot(xs[ends[e][1]] - xs[ends[e][0]], zs[ends[e][1]] - zs[ends[e][0]])));
    d.edgeCls.push_back(cls[e]);
  }
  for (int w = 0; w < 10; w++) {
    d.wayKind.push_back(0);
    d.waySpeed.push_back(50);
  }
  d.first.assign(11, 0);
  // (each road with one point between its ends: half way, in quarter metres)
  d.polyFirst.push_back(0);
  for (int e = 0; e < 5; e++) {
    const double dx = (xs[ends[e][1]] - xs[ends[e][0]]) / 2, dz = (zs[ends[e][1]] - zs[ends[e][0]]) / 2;
    d.poly.push_back(static_cast<int16_t>(dx / POLY_UNIT / 4));
    d.poly.push_back(static_cast<int16_t>(dz / POLY_UNIT / 4));
    d.poly.push_back(static_cast<int16_t>(dx * 3 / POLY_UNIT / 4));
    d.poly.push_back(static_cast<int16_t>(dz * 3 / POLY_UNIT / 4));
    d.polyFirst.push_back(d.polyFirst.back() + 2);
  }
  return d;
}

struct Host {
  std::unique_ptr<World> world;
  DrivenPath path{nullptr};
  BigMapHost host;
  std::vector<std::pair<std::string, SpawnJson>> started;
  NetIndex* net = nullptr;

  explicit Host(const std::array<double, 4>& site, std::vector<PlaceJson> places = {}) {
    WorldJson json;
    json.version = 1;
    json.meta.name = "Baunatal";
    json.meta.minX = site[0];
    json.meta.minZ = site[1];
    json.meta.maxX = site[2];
    json.meta.maxZ = site[3];
    json.meta.tileSize = 500;
    json.places = std::move(places);
    world = std::make_unique<World>(std::move(json));
    const auto [carX, carZ] = mid(site);
    host.world = world.get();
    host.net = [this]() -> NetIndex* { return net; };
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
    host.onOpenRegion = [](const Region&) {};
    host.startInRegion = [this](const Region& r, const SpawnJson& s) { started.emplace_back(r.id, s); };
  }
};

void testStyleAndPrefs() {
  check(loadMapPrefs().style == "dark" && loadMapPrefs().online, "the defaults: the dark map (1.7.7's), the online search on");
  check(std::string(mapStyleOf("light").name) == "light" && std::string(mapStyleOf("x").name) == "dark", "the styles by name");
  check(roadKindOfClass(MOTORWAY) == RoadKind::motorway && roadKindOfClass(TRUNK) == RoadKind::motorway && roadKindOfClass(PRIMARY) == RoadKind::primary &&
            roadKindOfClass(RESIDENTIAL) == RoadKind::minor && roadKindOfClass(SERVICE) == RoadKind::service,
        "the kinds of road");
  check(roadKindOfTier(7) == RoadKind::motorway && roadKindOfTier(5) == RoadKind::primary && roadKindOfTier(2) == RoadKind::minor && roadKindOfTier(0) == RoadKind::service, "the kinds by tier");
  // every light colour is set (a map with a hole in its paint would draw black)
  const MapStyle& l = mapStyleOf("light");
  for (int k = 0; k < ROAD_KINDS; k++) check(l.road[k] && l.casing[k], "light: road colours");
  saveMapPrefs({"light", false});
  check(loadMapPrefs().style == "light" && !loadMapPrefs().online, "the preferences are kept");
  saveMapPrefs({});
}

void testOverview() {
  const auto net = std::make_shared<const NetData>(smallNet(0, 0));
  std::vector<PlaceJson> places = {{"Dorf", "village", 1500, 200}, {"Grossstadt", "city", 1500, -200}, {"Weiler", "hamlet", 2900, 900}, {"Stadtteil", "suburb", 100, 900}};
  MapOverview ov(net, places);
  check(ov.places()[0].name == "Grossstadt", "the places: the most prominent first");
  check(MapOverview::bandOf(0.0005) > MapOverview::bandOf(0.05) && MapOverview::bandOf(0.15) == 2 && MapOverview::bandOf(4) == 2, "the zoom bands");

  Canvas cv(dom::el("canvas"));
  const Ctx2D g = cv.getContext2D();
  MapView v;
  v.cx = 1500;
  v.cz = 0;
  v.width = W;
  v.height = H;
  // far out: the motorway only – its three roads joined into one line
  v.scale = 0.002;
  canvasNative::forget(g);
  ov.drawRoads(g, v, 1, mapStyleOf("dark"));
  const long far = canvasNative::calls(g);
  check(ov.bandsMade() == 1, "far out: one band made");
  v.scale = 0.05;
  canvasNative::forget(g);
  ov.drawRoads(g, v, 1, mapStyleOf("dark"));
  const long near = canvasNative::calls(g);
  check(near > far, "closer: more roads (the secondary road and the street appear)");
  // the light map draws a rim round each road first
  canvasNative::forget(g);
  ov.drawRoads(g, v, 1, mapStyleOf("light"));
  check(canvasNative::calls(g) > near, "light: the roads' rims");
  check(ov.bandsMade() == 2, "a second band");
  // the same band again: nothing is made anew
  ov.drawRoads(g, v, 1, mapStyleOf("dark"));
  check(ov.bandsMade() == 2, "a band is kept");

  // the names: far out the city only; closer the village too; the hamlet only close up
  std::vector<LabelBox> taken;
  v.scale = 0.002;
  canvasNative::forget(g);
  ov.drawNames(g, v, mapStyleOf("dark"), taken);
  std::vector<std::string> t = canvasNative::texts(g);
  check(std::count(t.begin(), t.end(), "Grossstadt") > 0 && std::count(t.begin(), t.end(), "Dorf") == 0, "far out: the city's name only");
  taken.clear();
  v.scale = 0.08;
  canvasNative::forget(g);
  ov.drawNames(g, v, mapStyleOf("dark"), taken);
  t = canvasNative::texts(g);
  check(std::count(t.begin(), t.end(), "Grossstadt") > 0 && std::count(t.begin(), t.end(), "Dorf") > 0 && std::count(t.begin(), t.end(), "Weiler") > 0, "close: the village and the hamlet too");
  // a name that would lie on another is left out (the bigger place keeps its name)
  taken.clear();
  v.scale = 0.0012;   // (the city and the village 400 m apart: under a pixel apart)
  MapOverview two(nullptr, {{"Dorf", "town", 1500, 1}, {"Grossstadt", "city", 1500, 0}});
  canvasNative::forget(g);
  two.drawNames(g, v, mapStyleOf("dark"), taken);
  t = canvasNative::texts(g);
  check(std::count(t.begin(), t.end(), "Grossstadt") > 0 && std::count(t.begin(), t.end(), "Dorf") == 0, "names do not overlap: the city wins");

  // the nearest road: on the street across (not the service ways), with its direction
  const std::optional<OverviewSpot> at = ov.nearest(1510, -400, 50);
  check(at && std::abs(at->x - 1500) < 1 && std::abs(at->z + 400) < 1, "nearest: on the street across");
  check(at && std::abs(std::abs(at->yaw) - M_PI) < 0.01, "nearest: the street runs south (a → b), yaw π");
  check(!ov.nearest(1500, 5000, 50), "nearest: nothing far from the roads");
  const PlaceJson* town = ov.nearestPlace(1400, -150, 5000);
  check(town && town->name == "Grossstadt", "the nearest town");
}

/** how long the real map's overview takes (the site's network, some hundred thousand roads) */
void testRealMap() {
  const std::string dir = "/home/user/driving-simulator/public/map/";
  if (!std::filesystem::exists(dir + "net.bin.gz")) {
    std::printf("(no real map: its timing is skipped)\n");
    return;
  }
  const auto t0 = std::chrono::steady_clock::now();
  const auto net = std::make_shared<const NetData>(unpackNet(plat::gunzip(fileBytes(dir + "net.bin.gz"))));
  std::vector<PlaceJson> places;
  {
    const Bytes b = fileBytes(dir + "map.json");
    Json j;
    if (Json::parse(std::string(b.begin(), b.end()), j))
      for (const Json& p : j.at("places").array) places.push_back({p.at("name").str, p.at("kind").str, p.at("x").number, p.at("z").number});
  }
  const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
  const auto t1 = std::chrono::steady_clock::now();
  MapOverview ov(net, places);
  Canvas cv(dom::el("canvas"));
  const Ctx2D g = cv.getContext2D();
  MapView v;
  v.width = 1280;
  v.height = 720;
  std::printf("real map: %zu roads, %zu places (read in %.0f ms)\n", net->edgeCls.size(), places.size(), ms(t0, t1));
  for (const double s : {0.0004, 0.002, 0.01, 0.05, 0.15}) {
    v.scale = s;
    const auto a = std::chrono::steady_clock::now();
    ov.drawRoads(g, v, 1, mapStyleOf("light"));
    const auto b = std::chrono::steady_clock::now();
    ov.drawRoads(g, v, 1, mapStyleOf("light"));
    std::vector<LabelBox> taken;
    ov.drawNames(g, v, mapStyleOf("light"), taken);
    const auto c = std::chrono::steady_clock::now();
    std::printf("  scale %.4f (band %d): first %.0f ms, then %.1f ms a picture, %zu names\n", s, MapOverview::bandOf(s), ms(a, b), ms(b, c), taken.size());
    check(ms(b, c) < 400, "a picture of the real map in less than 400 ms on this machine");
  }
  check(!places.empty(), "the real map's places");
}

void testStreetBook() {
  StreetBook b;
  b.add("Hauptstraße", 100, 0, "Baunatal");
  b.add("Hauptstraße", 5000, 0, "Kassel");
  b.add("Hauptstraße", 120, 10, "Baunatal");   // (the same street again)
  b.add("Am Stadtpark", 300, 0, "Baunatal");
  b.add("", 0, 0, "Baunatal");
  check(b.size() == 3, "each street of a town once, none without a name");
  check(b.near("hauptstrasse", 110, 0, 50) && !b.near("Hauptstraße", 2500, 0, 1000), "near: by name and distance");
  std::vector<AddressHit> hits = b.search("haupt", 8, AddressIndex::Near{4900, 0});
  check(hits.size() == 2 && hits[0].label == "Hauptstraße, Kassel", "search: the nearest first");
  hits = b.search("hauptstr baunatal", 8, std::nullopt);
  check(hits.size() == 1 && hits[0].label == "Hauptstraße, Baunatal" && hits[0].street == "Hauptstraße", "search: by street and town");
  check(b.search("stadtpark", 8, std::nullopt).size() == 1, "search: a word in the middle");
  const std::vector<AddressHit> close = b.nearest(250, 0, 2);
  check(close.size() == 2 && close[0].street == "Am Stadtpark" && close[1].label == "Hauptstraße, Baunatal", "nearest: the streets nearest first");
}

const char* const PHOTON_ANSWER = R"({"type":"FeatureCollection","features":[
 {"type":"Feature","geometry":{"type":"Point","coordinates":[9.4183,51.2556]},"properties":{"name":"Baunatal","osm_value":"town","state":"Hessen","country":"Deutschland"}},
 {"type":"Feature","geometry":{"type":"Point","coordinates":[8.8017,53.0793]},"properties":{"name":"Bremen","osm_value":"city","country":"Deutschland"}},
 {"type":"Feature","geometry":{"type":"Point","coordinates":[-74.006,40.7128]},"properties":{"name":"New York","osm_value":"city","state":"New York","country":"United States"}},
 {"type":"Feature","geometry":{"type":"Point","coordinates":[9.42,51.25]},"properties":{"street":"Marktplatz","housenumber":"5","city":"Baunatal"}},
 {"type":"Feature","geometry":{"type":"Point"},"properties":{"name":"broken"}}]})";

void testPhoton() {
  const std::string url = photonUrl("Kassel Hbf", std::make_pair(51.25, 9.4), 6, "de");
  check(url.rfind("https://photon.komoot.io/api/?q=Kassel%20Hbf", 0) == 0, "photon: the query, encoded");
  check(url.find("&limit=6") != std::string::npos && url.find("&lat=51.2500&lon=9.4000") != std::string::npos && url.find("&lang=de") != std::string::npos, "photon: limit, place, language");
  check(photonUrl("x", std::nullopt, 3, "pl").find("lang") == std::string::npos, "photon: a language it does not know is not asked for");
  const std::vector<OnlinePlace> p = parsePhoton(PHOTON_ANSWER);
  check(p.size() == 4, "photon: four places (the one without coordinates left out)");
  check(p.size() == 4 && p[0].name == "Baunatal" && p[0].detail == "Hessen, Deutschland" && std::abs(p[0].lat - 51.2556) < 1e-9 && p[0].kind == "town", "photon: name, where, position");
  check(p.size() == 4 && p[3].name == "Marktplatz 5" && p[3].detail == "Baunatal", "photon: an address");
  check(parsePhoton("<html>").empty(), "photon: not its answer");
}

/** the address picker of 2.0: the online search after a pause, every earlier question dropped */
void testPickerOnline() {
  int asked = 0;
  std::string lastQ;
  AddressPickerOptions<AddressHit> o;
  o.placeholder = "?";
  o.loading = "loading";
  o.none = "none";
  o.load = []() -> Task<AddressSearchOf<AddressHit>> {
    co_return [](const std::string& q, size_t, const std::optional<AddressIndex::Near>&) {
      std::vector<AddressHit> out;
      if (q.rfind("ka", 0) == 0) out.push_back({.label = "Kaufunger Straße", .street = "Kaufunger Straße", .x = 0, .z = 0, .exact = false});
      return out;
    };
  };
  o.pick = [](const AddressHit&) {};
  o.online = [&asked, &lastQ](std::string q) -> Task<std::vector<AddressHit>> {
    asked++;
    lastQ = q;
    co_return std::vector<AddressHit>{{.label = "Kassel (online)", .street = "", .x = 1, .z = 2, .exact = false}};
  };
  bool online = true;
  o.onlineOn = [&online] { return online; };
  o.onlineNote = "credit";
  o.searching = "searching";
  const AddressPicker p = addressPicker<AddressHit>(o);
  dom::body().append(p.node);
  dom::native::dispatch(p.input, "focus");
  settle();
  // typed quickly: only the last text is asked, once typing pauses
  dom::native::type(p.input, "ka");
  later(100);
  dom::native::type(p.input, "kas");
  later(100);
  dom::native::type(p.input, "kass");
  later(100);
  check(asked == 0, "online: not asked while typing");
  check(dom::native::text(p.node).find("searching") != std::string::npos, "online: that it is being asked");
  later(400);
  check(asked == 1 && lastQ == "kass", "online: asked once, with the last text");
  const std::string text = dom::native::text(p.node);
  check(text.find("Kaufunger") != std::string::npos && text.find("Kassel (online)") != std::string::npos, "online: its hits after the map's own");
  check(text.find("credit") != std::string::npos, "online: whose hits they are");
  // two characters: not asked
  dom::native::type(p.input, "kx");
  later(500);
  check(asked == 1, "online: not for two characters");
  // switched off: not asked
  online = false;
  dom::native::type(p.input, "kasse");
  later(500);
  check(asked == 1, "online: not when switched off");
  p.node.remove();
}

void testBigMap(const WorldOutline& outline) {
  const std::array<double, 4> site = around(51.2556, 9.4183, 8000);
  const auto [sx0, sz0] = mid(site);
  RegionIndex index;
  index.regions.push_back({"bremen", "Bremen", around(53.0793, 8.8017, 14000), "regions/bremen/map.json", {"34.tar"}});
  auto layer = std::make_shared<WorldLayer>();
  layer->set(outline, &index, site, "Baunatal");

  // the files of Bremen (its roads: the small network through its middle; its places) – and Photon's answer
  const auto [bx, bz] = mid(index.regions[0].box);
  writeFile(SCRATCH + "bremen/net.bin.gz", packNet(smallNet(bx - 1500, bz)));
  writeText(SCRATCH + "bremen/map.json", R"({"version":1,"places":[{"name":"Vegesack","kind":"town","x":)" + jsStr(bx) + R"(,"z":)" + jsStr(bz - 300) + "}]}");
  writeText(SCRATCH + "photon.json", PHOTON_ANSWER);
  plat::native::mount(std::string(MAP_ASSETS_URL) + "regions/bremen/", SCRATCH + "bremen/");
  plat::native::mount(std::string(PHOTON_API), SCRATCH + "photon.json");

  Host h(site, {{"Baunatal", "town", sx0, sz0}, {"Kassel", "city", sx0 + 3000, sz0 - 6000}, {"Rengershausen", "village", sx0 + 1000, sz0}, {"Altenritte", "suburb", sx0 - 1000, sz0}});
  BigMap map(h.host, layer);
  map.open(dom::body());
  El c = dom::body().querySelector(".bigmap canvas");
  dom::native::setRect(c, {0, 0, W, H});
  settle();

  // ---- the style: the button of the bar, kept ----
  El styleBtn = dom::body().querySelector(".bm-style");
  check(styleBtn && styleBtn.getAttribute("aria-pressed") == "false", "the style button: dark at first");
  check(styleBtn && styleBtn.getAttribute("aria-label") == "Light map", "the style button's label");
  if (styleBtn) styleBtn.click();
  settle();
  check(styleBtn.getAttribute("aria-pressed") == "true" && loadMapPrefs().style == "light", "light map chosen and kept");
  check(static_cast<bool>(dom::body().querySelector(".bigmap.bm-light")), "the overlay knows the light map");
  // far out: the towns' names are drawn on the world (1.7.7 showed nothing of the map there)
  canvasNative::forget(Canvas(c).getContext2D());
  wheel(c, 120, 12);
  check(drew(c, "Kassel"), "zoomed out: the city's name");
  wheel(c, -120, 12);
  if (styleBtn) styleBtn.click();
  settle();
  check(loadMapPrefs().style == "dark", "back to the dark map");

  // ---- the search: every town before anything is typed ----
  El input = dom::body().querySelector(".bm-search input");
  El list = dom::body().querySelector(".bm-search .addr-list");
  dom::native::dispatch(input, "focus");
  settle();
  const std::string all = dom::native::text(list);
  check(all.find("Kassel") != std::string::npos && all.find("Baunatal") != std::string::npos && all.find("Rengershausen") != std::string::npos, "focused: every town of the map");
  check(all.find("Kassel") < all.find("Rengershausen"), "… the city before the village");
  // ---- the online search: in the map, in another region, elsewhere (greyed) ----
  dom::native::type(input, "bau");
  later(400);
  std::string text = dom::native::text(list);
  check(text.find("Baunatal, Hessen, Deutschland") != std::string::npos, "online: a place in the loaded map");
  check(text.find("Bremen, Deutschland · Bremen") != std::string::npos, "online: a place in another region");
  check(text.find("New York, United States · outside the maps") != std::string::npos, "online: elsewhere");
  check(text.find("Photon by komoot") != std::string::npos, "online: the credit");
  El nyc = dom::native::find(list, "New York", "button");
  check(nyc && nyc.hasClass("addr-dim"), "elsewhere: greyed");
  El bremenHit = dom::native::find(list, "Bremen, Deutschland", "button");
  if (bremenHit) bremenHit.click();
  settle();
  check(map.chosenRegion() && map.chosenRegion()->id == "bremen", "a place in another region: that region chosen");
  // the map is there, close enough: Bremen's roads are fetched and drawn, its places named
  settle();
  canvasNative::forget(Canvas(c).getContext2D());
  wheel(c, 120, 1);
  wheel(c, -120, 1);
  check(drew(c, "Vegesack"), "another region zoomed into: its places");

  // ---- a tap on its road: "Put the car here" ----
  // (the map is at the place found, Bremen's middle: the secondary road runs 300 m south of it, east–west)
  El canvasEl = c;
  {
    // put the view on the street across the motorway: the middle of Bremen's box
    map.showRegion("bremen");
    settle();
    wheel(c, -120, 3);
    settle();
  }
  tap(canvasEl, W / 2 + 2, H / 2 + 2);
  El bar = dom::body().querySelector(".bm-spot");
  const std::string barText = dom::native::text(bar);
  check(barText.find("Map: Bremen") != std::string::npos, "the place panel: in Bremen");
  check(barText.find("Vegesack") != std::string::npos, "the place panel: the town nearest");
  check(barText.find("Autopilot") == std::string::npos && barText.find("drive there") == std::string::npos, "no autopilot across maps");
  El put = dom::native::find(bar, "Put the car here", "button");
  check(static_cast<bool>(put), "Put the car here");
  if (put) put.click();
  settle();
  check(h.started.size() == 1 && h.started[0].first == "bremen", "the game is asked to start in Bremen");
  check(h.started.size() == 1 && h.started[0].second.rough && std::hypot(h.started[0].second.x - bx, h.started[0].second.z - bz) < 700, "… at the road tapped, roughly");

  // the online search switched off (in the panel of layers): nothing asked
  El onlineBox = dom::native::find(dom::body().querySelector(".bm-layers"), "Online search", "label");
  check(static_cast<bool>(onlineBox), "the online search's switch");
  if (onlineBox) {
    El cb = onlineBox.querySelector("input");
    cb.checked(false);
    dom::native::dispatch(cb, "change");
  }
  check(!loadMapPrefs().online, "the online search switched off, and kept");
  dom::native::type(input, "new york");
  later(400);
  check(dom::native::text(list).find("New York") == std::string::npos, "switched off: no online hits");
  saveMapPrefs({});
  map.close();
  plat::native::unmountAll();
}

void testStartAt() {
  SpawnJson s;
  s.name = "Vegesack";
  s.x = 1234.5;
  s.z = -678;
  s.yaw = 1.25;
  saveStartAt("bremen", s);
  check(plat::sessionStorage().getItem(START_AT_KEY).has_value(), "the place is kept for the reload");
  check(!takeStartAt("hesse"), "another region: not this place");
  check(!plat::sessionStorage().getItem(START_AT_KEY).has_value(), "… and forgotten");
  saveStartAt("bremen", s);
  const std::optional<SpawnJson> got = takeStartAt("bremen");
  check(got && got->name == "Vegesack" && got->x == 1234.5 && got->z == -678 && got->yaw == 1.25 && got->rough && got->kind == "street", "the place, rough, on a street");
  check(!plat::sessionStorage().getItem(START_AT_KEY).has_value() && !takeStartAt("bremen"), "taken once");
}

/** the boot path: a place kept for the site's own map – the game begins a free drive there, without the menu */
void testBoot() {
  const std::string mapDir = "/home/user/driving-simulator/public/map/";
  if (!std::filesystem::exists(mapDir + "map.json")) {
    std::printf("(no real map: the boot is skipped)\n");
    return;
  }
  plat::native::mount(plat::locationBase() + "map/", mapDir);
  plat::native::setViewport(640, 360, 1);
  if (!plat::gl::createContext("mapview_test", 640, 360)) {
    std::printf("(no OpenGL ES here: the boot is skipped)\n");
    return;
  }
  // a place on a street of the map: the second start place of map.json, a little beside it
  double x = 0, z = 0;
  {
    const Bytes b = fileBytes(mapDir + "map.json");
    Json j;
    Json::parse(std::string(b.begin(), b.end()), j);
    const Json& sp = j.at("spawns").array.at(1);
    x = sp.at("x").number + 20;
    z = sp.at("z").number;
  }
  SpawnJson s;
  s.name = "Hier";
  s.x = x;
  s.z = z;
  saveStartAt("", s);
  Game game(dom::el("canvas"), dom::el("div"));
  plat::native::wait(game.boot(URLSearchParams("")));
  game.looping = false;
  check(!plat::sessionStorage().getItem(START_AT_KEY).has_value(), "boot: the place is taken");
  double now = plat::nowMs();
  for (int i = 0; i < 20000 && game.state != "drive"; i++) {
    plat::native::pump();
    now += 1000.0 / 60;
    game.frame(now);
  }
  check(game.state == "drive", "boot: straight into a drive (state " + game.state + ")");
  check(std::hypot(game.mpos[0] - x, game.mpos[2] - z) < 250, "boot: at the place kept (" + jsStr(game.mpos[0]) + ", " + jsStr(game.mpos[2]) + ")");
}

}  // namespace

int main() {
  plat::native::manualClock(true);
  dom::native::reset();
  testStyleAndPrefs();
  testOverview();
  testRealMap();
  testStreetBook();
  testPhoton();
  testPickerOnline();
  const WorldOutline outline = parseWorldOutline(fileBytes("web/data/outline.bin"));
  check(!outline.empty(), "web/data/outline.bin is read");
  testBigMap(outline);
  testStartAt();
  plat::native::manualClock(false);
  testBoot();
  if (failures) {
    std::fprintf(stderr, "mapview_test: %d failed\n", failures);
    return 1;
  }
  std::printf("mapview_test: ok\n");
  return 0;
}

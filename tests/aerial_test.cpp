// LINK: reality geo core platform
// SPDX-License-Identifier: AGPL-3.0-only
// Origin: tests/game/playtest.test.ts @ v1.7.7 ("aerial photographs: tiles in the map's own coordinates …")
/**
 * The aerial photographs (reality/aerial.h): 1.7.7's test of the tiles and their
 * addresses, the cache (a few at a time, the most recently used kept), and 2.0's
 * sources – which state a tile is asked of, its border tiles, Sentinel-2's Web
 * Mercator tiles and how they lie on the map's UTM frame.
 *
 *   tools/t.sh aerial_test
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/task.h"
#include "geo/utm.h"
#include "platform/canvas.h"
#include "platform/native/native.h"
#include "reality/aerial.h"

using namespace fs;

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    failures++;
  }
}

/** `new URL(u).searchParams` */
std::map<std::string, std::string> params(const std::string& url, std::string* base = nullptr) {
  std::map<std::string, std::string> out;
  const size_t q = url.find('?');
  if (base) *base = url.substr(0, q);
  if (q == std::string::npos) return out;
  const auto decode = [](const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
      if (s[i] == '+') r += ' ';
      else if (s[i] == '%' && i + 2 < s.size()) {
        r += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
        i += 2;
      } else r += s[i];
    }
    return r;
  };
  size_t i = q + 1;
  while (i <= url.size()) {
    size_t amp = url.find('&', i);
    if (amp == std::string::npos) amp = url.size();
    const std::string pair = url.substr(i, amp - i);
    const size_t eq = pair.find('=');
    out[decode(pair.substr(0, eq))] = eq == std::string::npos ? "" : decode(pair.substr(eq + 1));
    i = amp + 1;
  }
  return out;
}

std::vector<double> numbers(const std::string& s) {
  std::vector<double> out;
  size_t i = 0;
  while (i < s.size()) {
    size_t c = s.find(',', i);
    if (c == std::string::npos) c = s.size();
    out.push_back(std::stod(s.substr(i, c - i)));
    i = c + 1;
  }
  return out;
}

void original() {
  // aerial photographs: tiles in the map's own coordinates, fine enough for the zoom
  check(aerialTileSize(0) == AERIAL_TILE_PX * 0.2, "tile size of level 0");
  check(aerialLevel(5) == 0, "close up: the finest pictures (20 cm)");
  check(aerialLevel(0.02) == 8, "the whole map: the coarsest");
  for (double s = 0.02; s < 6; s *= 1.3) {
    const int level = aerialLevel(s);
    const double res = 0.2 * std::pow(2.0, level);  // metres per pixel of the pictures
    check(level == 0 || res <= (1 / s) * 1.3, "at " + std::to_string(s) + " px/m pictures of " + std::to_string(res) + " m/px are too coarse");
  }
  const std::vector<AerialTile> tiles = aerialTiles(2, 527900, 5677900, 528500, 5678100);
  check(tiles.size() >= 2, "at least two tiles");
  const double size = aerialTileSize(2);
  std::set<std::string> covered;
  for (const AerialTile& t : tiles) {
    check(std::abs(t.minE - t.col * size) < 1e-6 && std::abs(t.minN - t.row * size) < 1e-6, "tiles lie on the grid of their level");
    check(t.minE < 528500 && t.minE + size > 527900 && t.minN < 5678100 && t.minN + size > 5677900, "every tile touches the rectangle");
    covered.insert(std::to_string(t.col) + "/" + std::to_string(t.row));
  }
  check(covered.size() == tiles.size(), "no tile twice");
  std::string base;
  const auto q = params(aerialUrl(tiles[0]), &base);
  check(base.rfind("https:", 0) == 0, "https");
  check(base == AERIAL_SOURCE.wms, "Hesse's service");
  check(q.at("CRS") == "EPSG:25832", "CRS");
  const std::vector<double> box = numbers(q.at("BBOX"));
  check(box.size() == 4 && std::abs(box[0] - tiles[0].minE) < 0.01 && std::abs(box[1] - tiles[0].minN) < 0.01 && std::abs(box[2] - box[0] - size) < 0.02 &&
            std::abs(box[3] - box[1] - size) < 0.02,
        "east, north, east, north");
  check(q.at("WIDTH") == std::to_string(AERIAL_TILE_PX), "width");
  check(q.at("LAYERS") == "he_dop_rgb" && q.at("FORMAT") == "image/jpeg" && q.at("VERSION") == "1.3.0", "layer, format, version");
  // the address as URLSearchParams writes it
  check(aerialUrl(tiles[0]).find("CRS=EPSG%3A25832&BBOX=") != std::string::npos, "form-encoded");
  check(aerialKey(tiles[0]) == "2/" + std::to_string(tiles[0].col) + "/" + std::to_string(tiles[0].row), "key of 1.7.7");
}

/** the cache: pictures that arrive when the test says so */
void cache() {
  std::vector<std::pair<std::string, Resolver<Image>>> asked;
  int loads = 0;
  {
    AerialCache c([&] { loads++; }, 6);
    c.load = [&](std::string url) {
      Resolver<Image> r;
      asked.emplace_back(url, r);
      return r.task();
    };
    const std::vector<AerialTile> tiles = aerialTiles(0, 528000, 5678000, 528000 + 102.4 * 4, 5678000 + 102.4 * 3);
    check(tiles.size() == 12, "12 tiles");
    for (const AerialTile& t : tiles) check(c.get(t) == nullptr, "nothing there yet");
    check(asked.size() == 4 && c.loadingNow() == 4, "a few at a time: the server is a public service");
    // asking again does not order twice
    for (const AerialTile& t : tiles) c.get(t);
    check(asked.size() == 4, "no tile ordered twice");
    int handle = 100;
    asked[0].second.resolve(Image{handle++, 512, 512});
    check(loads == 1, "onLoad when a picture has arrived");
    check(asked.size() == 5, "the next one is fetched");
    const Image* img = c.get(tiles[0]);
    check(img && img->handle == 100 && img->width == 512, "the picture is there");
    // a failed picture is not asked for again
    asked[1].second.resolve(Image{});
    check(loads == 1, "no onLoad for a failure");
    check(c.get(tiles[1]) == nullptr, "a failed picture stays away");
    const size_t before = asked.size();
    c.get(tiles[1]);
    check(asked.size() == before, "and is not fetched again");
    // a new picture of the map: what was ordered but not started is forgotten
    c.beginFrame();
    for (size_t i = 2; i < asked.size(); i++) asked[i].second.resolve(Image{handle++, 512, 512});
    const size_t fetched = asked.size();
    check(fetched == 6, "only those on their way when the frame began, and what their arrival pumped (none: the queue was empty)");
    check(c.size() <= 6, "never more than max kept");
    // keep tile 0 fresh, then fetch six more: the least recently used go
    for (int k = 0; k < 6; k++) {
      c.beginFrame();
      c.get(tiles[0]);
      c.get(tiles[6 + k]);
      while (asked.size() > fetched + static_cast<size_t>(k)) {
        Resolver<Image> r = asked.back().second;
        if (r.task().done()) break;
        r.resolve(Image{handle++, 512, 512});
      }
    }
    check(c.size() <= 6, "trimmed to max");
    check(c.get(tiles[0]) != nullptr, "the most recently used is kept");
    check(c.get(tiles[2]) == nullptr, "an old one is gone (ordered anew)");
    // a picture that arrives after the cache is gone is let go
    c.beginFrame();
    c.get(tiles[3]);
  }
  for (auto& [url, r] : asked)
    if (!r.task().done()) r.resolve(Image{999, 1, 1});
  check(true, "no crash when pictures arrive after the cache is gone");
}

void states() {
  struct Place {
    const char* name;
    double lat, lon;
    const char* iso;  // "" none
  };
  const Place places[] = {
      {"Baunatal", 51.2556, 9.4183, "DE-HE"},   {"Frankfurt", 50.1109, 8.6821, "DE-HE"},  {"München", 48.1372, 11.5756, "DE-BY"},
      {"Berlin Mitte", 52.5200, 13.4050, "DE-BE"}, {"Potsdam", 52.3906, 13.0645, "DE-BB"}, {"Bremen", 53.0793, 8.8017, "DE-HB"},
      {"Hannover", 52.3759, 9.7320, "DE-NI"},   {"Köln", 50.9375, 6.9603, "DE-NW"},       {"Dresden", 51.0504, 13.7373, "DE-SN"},
      {"Erfurt", 50.9848, 11.0299, "DE-TH"},    {"Kiel", 54.3233, 10.1228, "DE-SH"},      {"Rostock", 54.0924, 12.0991, "DE-MV"},
      {"Mainz", 49.9929, 8.2473, "DE-RP"},      {"Saarbrücken", 49.2402, 6.9969, "DE-SL"}, {"Stuttgart", 48.7758, 9.1829, "DE-BW"},
      // not used (AERIAL_NOT_FREE), and abroad
      {"Hamburg", 53.5511, 9.9937, ""},         {"Magdeburg", 52.1205, 11.6276, ""},      {"Paris", 48.8566, 2.3522, ""},
      {"Feldkirch", 47.2379, 9.5981, ""},
  };
  for (const Place& p : places) {
    const AerialState* s = aerialStateAt(p.lat, p.lon);
    check((s ? s->iso : std::string()) == p.iso, std::string("state of ") + p.name + ": " + (s ? s->iso : "none"));
  }
  check(AERIAL_STATES().size() == 14, "14 states used");
  for (const AerialNotFree& n : AERIAL_NOT_FREE())
    for (const AerialState& s : AERIAL_STATES()) check(s.iso != n.iso, "a state not used is not among those used: " + n.iso);
  for (const AerialState& s : AERIAL_STATES()) {
    check(s.source.wms.rfind("https://", 0) == 0, "https: " + s.iso);
    check(!s.source.attribution.empty() && !s.source.licence.empty() && !s.source.layer.empty(), "credit, licence, layer: " + s.iso);
    check(s.box[0] > 5.5 && s.box[2] < 15.2 && s.box[1] > 47.2 && s.box[3] < 55.2, "inside Germany: " + s.iso);
  }

  // a tile in the middle of Hesse: Hesse's picture, as JPEG, the same address as 1.7.7's
  {
    const std::vector<AerialTile> tiles = aerialStateTiles(2, 527900, 5677900, 528500, 5678100);
    check(tiles.size() == aerialTiles(2, 527900, 5677900, 528500, 5678100).size(), "Baunatal: each tile once");
    for (const AerialTile& t : tiles) {
      check(t.src && t.src->id == "he" && !t.png, "Baunatal: Hesse's pictures, JPEG");
      AerialTile plain = t;
      plain.src = nullptr;
      check(aerialUrl(t) == aerialUrl(plain), "the same address as 1.7.7's");
      check(aerialKey(t) == "he/" + aerialKey(plain), "a key per source");
    }
  }
  // on the border of Hesse and Lower Saxony (Hann. Münden): both, as PNG with the outside transparent
  {
    const auto [e, n] = latLonToUtm32(51.4167, 9.6500);
    const std::vector<AerialTile> tiles = aerialStateTiles(3, e - 100, n - 100, e + 100, n + 100);
    std::set<std::string> ids;
    for (const AerialTile& t : tiles) {
      ids.insert(t.src->id);
      check(t.png, "a border tile is PNG");
      const auto q = params(aerialUrl(t));
      check(q.at("FORMAT") == "image/png" && q.at("TRANSPARENT") == "TRUE", "transparent outside the state");
    }
    check(ids.count("he") && ids.count("ni"), "Hann. Münden: Hesse and Lower Saxony");
  }
  // the North Sea and Paris: no state's pictures
  {
    const auto [e, n] = latLonToUtm32(54.5, 6.5);
    check(aerialStateTiles(4, e - 500, n - 500, e + 500, n + 500).empty(), "no state at sea");
  }
  // Bavaria speaks WMS 1.1.1 (SRS=), Bremen asks for two layers
  {
    const auto [e, n] = latLonToUtm32(48.1372, 11.5756);
    const std::vector<AerialTile> by = aerialStateTiles(1, e, n, e + 10, n + 10);
    check(by.size() == 1 && by[0].src->id == "by", "München: Bavaria");
    const auto q = params(aerialUrl(by[0]));
    check(q.count("SRS") && !q.count("CRS") && q.at("SRS") == "EPSG:25832" && q.at("VERSION") == "1.1.1", "Bavaria: WMS 1.1.1");
    const std::vector<double> box = numbers(q.at("BBOX"));
    check(std::abs(box[0] - by[0].minE) < 0.01 && std::abs(box[3] - by[0].minN - by[0].size) < 0.02, "Bavaria: east, north");
    const auto [be, bn] = latLonToUtm32(53.0793, 8.8017);
    const std::vector<AerialTile> hb = aerialStateTiles(1, be, bn, be + 10, bn + 10);
    check(hb.size() == 1 && hb[0].src->id == "hb", "Bremen: Bremen");
    check(params(aerialUrl(hb[0])).at("LAYERS") == "DOP20_2023_HB,DOP20_2023_BHV", "Bremen: both cities");
  }
}

void sentinel() {
  // the zoom: fine enough, never much finer
  for (double s = 0.0002; s < 2; s *= 1.3) {
    const int z = s2Zoom(s, 51);
    const double res = 156543.03392804097 * std::cos(51 * M_PI / 180) / std::pow(2.0, z);
    check(z == S2_MAX_ZOOM || res <= (1 / s) * 1.3, "S2 at " + std::to_string(s) + " px/m: " + std::to_string(res) + " m/px is too coarse");
    check(z == 0 || res * 2 > (1 / s) * 1.3 / 2, "S2 at " + std::to_string(s) + " not needlessly fine");
  }
  check(s2Zoom(100, 51) == S2_MAX_ZOOM, "close up: the finest zoom there is");
  // the tile over Kassel at zoom 7 (fetched by hand to check the address: .../g/7/43/67.jpg)
  const std::vector<AerialTile> kassel = s2Tiles(7, 51.3, 9.4, 51.31, 9.41);
  check(kassel.size() == 1 && kassel[0].col == 67 && kassel[0].row == 43, "Kassel at zoom 7");
  check(s2Url(kassel[0]) == "https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless_3857/default/g/7/43/67.jpg", "the WMTS address");
  check(aerialUrl(kassel[0]) == s2Url(kassel[0]), "aerialUrl of a Sentinel tile");
  check(aerialKey(kassel[0]) == "s2/7/67/43", "its key");
  // corners: the whole world at zoom 0, and the tiles of a box cover it
  const auto nw = s2TileLatLon(0, 0, 0);
  check(std::abs(nw[0] - 85.0511) < 1e-3 && nw[1] == -180, "zoom 0 starts at 85.05° N, 180° W");
  const auto se = s2TileLatLon(0, 1, 1);
  check(std::abs(se[0] + 85.0511) < 1e-3 && se[1] == 180, "and ends at 85.05° S, 180° E");
  const std::vector<AerialTile> de = s2Tiles(6, 47.2, 5.8, 55.1, 15.1);
  std::set<std::pair<int, int>> seen;
  for (const AerialTile& t : de) {
    const auto a = s2TileLatLon(6, t.col, t.row), b = s2TileLatLon(6, t.col + 1, t.row + 1);
    check(a[0] > 47.2 && b[0] < 55.1 && a[1] < 15.1 && b[1] > 5.8, "every tile touches Germany's box");
    seen.insert({t.col, t.row});
  }
  check(seen.size() == de.size() && de.size() >= 4 && de.size() <= 12, "Germany at zoom 6: a handful of tiles, none twice");
  for (double lat = 47.5; lat < 55; lat += 1.7) {
    for (double lon = 6; lon < 15; lon += 1.9) {
      bool in = false;
      for (const AerialTile& t : de) {
        const auto a = s2TileLatLon(6, t.col, t.row), b = s2TileLatLon(6, t.col + 1, t.row + 1);
        if (lat <= a[0] && lat >= b[0] && lon >= a[1] && lon <= b[1]) in = true;
      }
      check(in, "Germany is covered");
    }
  }
  // the layer's credits come with its pictures
  check(S2_SOURCE.licence == "CC BY 4.0" && S2_SOURCE.attribution.find("EOX IT Services GmbH") != std::string::npos &&
            S2_SOURCE.attribution.find("Copernicus Sentinel data 2016") != std::string::npos,
        "Sentinel-2 cloudless 2016: CC BY 4.0, credited as EOX asks");
  // fading in: the states' pictures close up, Sentinel alone far out
  check(aerialStatesAlpha(0) == 1 && aerialStatesAlpha(6) == 1 && aerialStatesAlpha(7) > 0 && aerialStatesAlpha(7) < 1 && aerialStatesAlpha(8) == 0, "fade by level");
}

/** drawAerial: what it asks for and whose credit it answers, far out and close up */
void drawing() {
  std::vector<std::string> urls;
  AerialCache c([] {}, 200);
  c.load = [&](std::string url) {
    urls.push_back(url);
    return resolved(Image{static_cast<int>(urls.size()), 256, 256});
  };
  const Canvas canvas(400, 300);
  const Ctx2D g = canvas.getContext2D();
  AerialView v;
  v.width = 400;
  v.height = 300;
  v.dpr = 1;
  v.originE = WORLD_ORIGIN_E;
  v.originN = WORLD_ORIGIN_N;
  // all of Germany on the screen: Sentinel only
  v.scale = 0.0003;
  const auto [ce, cn] = latLonToUtm32(51.2, 10.4);
  v.cx = ce - v.originE;
  v.cz = v.originN - cn;
  drawAerial(g, c, v);  // (the first time the pictures are ordered; they are there at once)
  std::vector<std::string> credits = drawAerial(g, c, v);
  check(!urls.empty(), "Germany: pictures asked for");
  for (const std::string& u : urls) check(u.find("tiles.maps.eox.at") != std::string::npos, "far out: only Sentinel's");
  check(urls.size() <= 80, "never more than a screenful");
  check(credits.size() == 1 && credits[0] == S2_SOURCE.attribution, "far out: Sentinel's credit");
  // Baunatal close up: Hesse's pictures over a coarse Sentinel ground
  urls.clear();
  v.scale = 2;
  v.cx = 0;
  v.cz = 0;
  drawAerial(g, c, v);
  credits = drawAerial(g, c, v);
  int he = 0, s2 = 0;
  for (const std::string& u : urls) {
    if (u.find("gds-srv.hessen.de") != std::string::npos) he++;
    if (u.find("tiles.maps.eox.at") != std::string::npos) s2++;
  }
  check(he > 0 && he <= 40, "close up in Baunatal: Hesse's pictures");
  check(s2 > 0 && s2 <= 9, "and only a coarse Sentinel ground under them");
  bool hesse = false;
  for (const std::string& cr : credits) hesse = hesse || cr == AERIAL_SOURCE.attribution;
  check(hesse, "Hesse's credit on the screen");
  // a region's map elsewhere (Dresden): Saxony's pictures, in the same world frame
  urls.clear();
  const auto [de, dn] = latLonToUtm32(51.0504, 13.7373);
  v.cx = de - v.originE;
  v.cz = v.originN - dn;
  drawAerial(g, c, v);
  int sn = 0;
  for (const std::string& u : urls) sn += u.find("geodienste.sachsen.de") != std::string::npos;
  check(sn > 0, "Dresden: Saxony's pictures");
}

}  // namespace

int main() {
  original();
  cache();
  states();
  sentinel();
  drawing();
  if (failures) {
    std::fprintf(stderr, "%d failed\n", failures);
    return 1;
  }
  std::printf("aerial_test: all passed\n");
  return 0;
}

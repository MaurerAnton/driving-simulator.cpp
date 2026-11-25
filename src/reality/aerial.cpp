// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/reality/aerial.ts @ v1.7.7
#include "reality/aerial.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "core/js.h"
#include "geo/utm.h"

namespace fs {

const AerialSource AERIAL_SOURCE = {
    "he",
    "Luftbilder DOP20 – HVBG",
    "Luftbilder: © Hessische Verwaltung für Bodenmanagement und Geoinformation",
    "https://hvbg.hessen.de/landesvermessung/geotopographie/luftbilder/digitale-orthophotos",
    "https://www.gds-srv.hessen.de/cgi-bin/lika-services/ogc-free-images.ows",
    "he_dop_rgb",
    "1.3.0",
    "§ 24 HVGG (frei)",
};

namespace {
/** ground resolution of the finest level in metres per pixel (that of the photographs) */
constexpr double FINEST = 0.2;

/** `new URLSearchParams(q).toString()`: application/x-www-form-urlencoded */
std::string formEncode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '*' || c == '-' || c == '.' || c == '_') out += static_cast<char>(c);
    else if (c == ' ') out += '+';
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}
std::string query(const std::vector<std::pair<std::string, std::string>>& q) {
  std::string out;
  for (const auto& [k, v] : q) out += (out.empty() ? "" : "&") + formEncode(k) + "=" + formEncode(v);
  return out;
}
}  // namespace

double aerialTileSize(int level) { return AERIAL_TILE_PX * FINEST * std::pow(2.0, level); }

int aerialLevel(double pxPerMetre) {
  const double want = 1 / std::max(pxPerMetre, 1e-6);  // metres per pixel on the screen
  return static_cast<int>(std::min<double>(AERIAL_MAX_LEVEL, std::max(0.0, std::floor(std::log2(want / FINEST) + 0.35))));
}

std::vector<AerialTile> aerialTiles(int level, double minE, double minN, double maxE, double maxN) {
  const double size = aerialTileSize(level);
  std::vector<AerialTile> out;
  for (double row = std::floor(minN / size); row * size < maxN; row++) {
    for (double col = std::floor(minE / size); col * size < maxE; col++) {
      AerialTile t;
      t.level = level;
      t.col = static_cast<int>(col);
      t.row = static_cast<int>(row);
      t.minE = col * size;
      t.minN = row * size;
      t.size = size;
      out.push_back(t);
    }
  }
  return out;
}

std::string aerialUrl(const AerialTile& t) {
  // 2.0: a Sentinel-2 tile is a WMTS tile; a state's tile asks that state's service
  if (t.src == &S2_SOURCE) return s2Url(t);
  const AerialSource& src = t.src ? *t.src : AERIAL_SOURCE;
  std::string bbox;
  for (double v : {t.minE, t.minN, t.minE + t.size, t.minN + t.size}) bbox += (bbox.empty() ? "" : ",") + jsStr(jsRound(v * 100) / 100);
  std::vector<std::pair<std::string, std::string>> q = {
      {"SERVICE", "WMS"},
      {"VERSION", src.version},
      {"REQUEST", "GetMap"},
      {"LAYERS", src.layer},
      {"STYLES", ""},
      {src.version == "1.1.1" ? "SRS" : "CRS", "EPSG:25832"},
      {"BBOX", bbox},
      {"WIDTH", jsStr(AERIAL_TILE_PX)},
      {"HEIGHT", jsStr(AERIAL_TILE_PX)},
      {"FORMAT", t.png ? "image/png" : "image/jpeg"},
  };
  // 2.0: on a border the outside of the state stays transparent (the neighbour's picture or Sentinel's shows through)
  if (t.png) q.push_back({"TRANSPARENT", "TRUE"});
  return src.wms + (src.wms.find('?') == std::string::npos ? "?" : "&") + query(q);
}

std::string aerialKey(const AerialTile& t) {
  const std::string key = jsStr(t.level) + "/" + jsStr(t.col) + "/" + jsStr(t.row);
  // 2.0: the same square of two sources (or as JPEG and as PNG) is two pictures
  return t.src ? t.src->id + (t.png ? "p/" : "/") + key : key;
}

// ---- AerialCache ----

AerialCache::AerialCache(std::function<void()> onLoad, int max) : in(std::make_shared<Inner>()) {
  in->onLoad = std::move(onLoad);
  in->max = max > 0 ? max : 90;
}

AerialCache::~AerialCache() {
  // (a picture still on its way is let go when it arrives)
  in->alive = false;
  for (auto& [key, e] : in->tiles) freeImage(e.img);
  in->tiles.clear();
  in->order.clear();
  in->queue.clear();
}

const Image* AerialCache::get(const AerialTile& t) {
  const std::string key = aerialKey(t);
  auto have = in->tiles.find(key);
  if (have != in->tiles.end()) {
    have->second.used = ++in->tick;
    return have->second.ready ? &have->second.img : nullptr;
  }
  if (std::none_of(in->queue.begin(), in->queue.end(), [&](const AerialTile& q) { return aerialKey(q) == key; })) in->queue.push_back(t);
  pump(in, load);
  return nullptr;
}

void AerialCache::beginFrame() { in->queue.clear(); }

size_t AerialCache::size() const { return in->tiles.size(); }
int AerialCache::loadingNow() const { return in->loading; }

void AerialCache::pump(const std::shared_ptr<Inner>& in, const std::function<Task<Image>(std::string)>& load) {
  // a few at a time: the server is a public service
  while (in->loading < 4 && !in->queue.empty()) {
    const AerialTile t = in->queue.front();
    in->queue.erase(in->queue.begin());
    const std::string key = aerialKey(t);
    if (in->tiles.count(key)) continue;
    Entry entry;
    entry.used = ++in->tick;
    in->tiles.emplace(key, entry);
    in->order.push_back(key);
    in->loading++;
    // nothing but the picture's address is sent: no cookies, no referrer (platform: loadImage)
    fetchOne(in, key, load(aerialUrl(t)), load).detach();
  }
}

Task<void> AerialCache::fetchOne(std::shared_ptr<Inner> in, std::string key, Task<Image> picture, std::function<Task<Image>(std::string)> load) {
  Image img;
  try {
    img = co_await picture;
  } catch (...) {
    img = Image();
  }
  if (!in->alive) {
    freeImage(img);
    co_return;
  }
  const bool ok = static_cast<bool>(img);
  in->loading--;
  auto it = in->tiles.find(key);
  if (it == in->tiles.end()) {
    freeImage(img);
  } else {
    it->second.img = img;
    it->second.ready = ok;
    it->second.failed = !ok;
  }
  trim(*in);
  pump(in, load);
  if (ok && in->onLoad) in->onLoad();
}

void AerialCache::trim(Inner& in) {
  if (static_cast<int>(in.tiles.size()) <= in.max) return;
  std::vector<std::string> old;
  for (const std::string& key : in.order) {
    const Entry& e = in.tiles.at(key);
    // (2.0: not one asked for in this picture – a wide view over a border needs more than the cache holds,
    //  and dropping what is on the screen fetched it again at every redraw)
    if ((e.ready || e.failed) && e.used < in.frameStart) old.push_back(key);
  }
  std::stable_sort(old.begin(), old.end(), [&](const std::string& a, const std::string& b) { return in.tiles.at(a).used < in.tiles.at(b).used; });
  const size_t n = std::min(old.size(), in.tiles.size() - static_cast<size_t>(in.max));
  for (size_t i = 0; i < n; i++) {
    auto it = in.tiles.find(old[i]);
    freeImage(it->second.img);
    in.tiles.erase(it);
    in.order.erase(std::find(in.order.begin(), in.order.end(), old[i]));
  }
}

// ---- 2.0: Germany and beyond ------------------------------------------------------------------

// (reality/aerialstates.cpp)
const std::vector<std::pair<std::string, std::vector<std::vector<double>>>>& aerialStateOutlines();

const AerialSource S2_SOURCE = {
    "s2",
    "Sentinel-2 cloudless 2016 – EOX",
    // (the credit EOX asks for since June 2026 – s2maps.eu is now cloudless.eox.at: its FAQ, "What is the required attribution text?")
    "EOxCloudless https://cloudless.eox.at by EOX IT Services GmbH (Contains modified Copernicus Sentinel data 2016)",
    "https://cloudless.eox.at/documentation/faq",
    "https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless_3857/default/g/{z}/{y}/{x}.jpg",
    "s2cloudless_3857",
    "",
    "CC BY 4.0",
};

namespace {
/** 2.0: the states' services (docs/AERIAL.md), the city states first: their outlines lie inside their neighbours' */
std::vector<std::pair<std::string, AerialSource>> stateSources() {
  return {
      {"DE-BE", {"be", "Luftbilder DOP20 2025 – Geoportal Berlin", "Luftbilder: Geoportal Berlin / Digitale farbige Orthophotos 2025 (DOP20RGBI), dl-de/zero-2.0", "https://gdi.berlin.de/services/wms/dop_2025_fruehjahr?REQUEST=GetCapabilities&SERVICE=wms", "https://gdi.berlin.de/services/wms/dop_2025_fruehjahr", "dop_2025", "1.3.0", "dl-de/zero-2.0"}},
      {"DE-HB", {"hb", "Luftbilder DOP20 2023 – Landesamt GeoInformation Bremen", "Luftbilder: © GeoBasis-DE / Landesamt GeoInformation Bremen 2023, CC BY 4.0", "https://geodienste.bremen.de/wms_dop20_2023?REQUEST=GetCapabilities&SERVICE=wms", "https://geodienste.bremen.de/wms_dop20_2023", "DOP20_2023_HB,DOP20_2023_BHV", "1.3.0", "CC BY 4.0"}},
      {"DE-HE", AERIAL_SOURCE},
      {"DE-NW", {"nw", "Luftbilder DOP – Geobasis NRW", "Luftbilder: Land NRW (2026), dl-de/zero-2.0", "https://www.bezreg-koeln.nrw.de/geobasis-nrw/produkte-und-dienste/luftbild-und-satellitenbildinformationen/aktuelle-luftbild-und-0", "https://www.wms.nrw.de/geobasis/wms_nw_dop", "nw_dop_rgb", "1.3.0", "dl-de/zero-2.0"}},
      {"DE-BY", {"by", "Luftbilder DOP20 – Bayerische Vermessungsverwaltung", "Luftbilder: © Bayerische Vermessungsverwaltung – www.geodaten.bayern.de, CC BY 4.0", "https://geodaten.bayern.de/opengeodata/", "https://geoservices.bayern.de/od/wms/dop/v1/dop20", "by_dop20c", "1.1.1", "CC BY 4.0"}},
      {"DE-BB", {"bb", "Luftbilder DOP20 – LGB Brandenburg", "Luftbilder: © GeoBasis-DE/LGB, dl-de/by-2-0 (Daten geändert)", "https://geobasis-bb.de", "https://isk.geobasis-bb.de/mapproxy/dop20c/service/wms", "bebb_dop20c", "1.3.0", "dl-de/by-2.0"}},
      {"DE-SN", {"sn", "Luftbilder DOP20 – GeoSN", "Luftbilder: GeoSN, dl-de/by-2-0", "https://www.geodaten.sachsen.de", "https://geodienste.sachsen.de/wms_geosn_dop-rgb/guest", "sn_dop_020", "1.3.0", "dl-de/by-2.0"}},
      {"DE-TH", {"th", "Luftbilder DOP20 – GDI-Th", "Luftbilder: © GDI-Th, CC BY 4.0", "https://www.geoportal-th.de", "https://www.geoproxy.geoportal-th.de/geoproxy/services/DOP", "th_dop", "1.3.0", "CC BY 4.0"}},
      {"DE-NI", {"ni", "Luftbilder DOP20 – LGLN", "Luftbilder: © GeoBasis-DE / LGLN (2026), CC BY 4.0", "https://www.lgln.niedersachsen.de/startseite/geodaten_karten/open_geodata/", "https://opendata.lgln.niedersachsen.de/doorman/noauth/dop_wms", "ni_dop20", "1.3.0", "CC BY 4.0"}},
      {"DE-SH", {"sh", "Luftbilder DOP20 – LVermGeo SH", "Luftbilder: © GeoBasis-DE/LVermGeo SH/CC BY 4.0", "https://www.schleswig-holstein.de/DE/fachinhalte/G/geoinformationen/opendata.html", "https://dienste.gdi-sh.de/WMS_SH_DOP20col_OpenGBD", "sh_dop20_rgb", "1.3.0", "CC BY 4.0"}},
      {"DE-RP", {"rp", "Luftbilder DOP20 – LVermGeoRP", "Luftbilder: ©GeoBasis-DE / LVermGeoRP (2026), dl-de/by-2-0, http://www.lvermgeo.rlp.de [Daten bearbeitet]", "https://lvermgeo.rlp.de", "https://geo4.service24.rlp.de/wms/rp_dop20.fcgi", "rp_dop20", "1.3.0", "dl-de/by-2.0"}},
      {"DE-SL", {"sl", "Luftbilder DOP20 – LVGL Saarland", "Luftbilder: © GeoBasis DE/LVGL-SL (2026), dl-de/by-2-0", "https://geoportal.saarland.de", "https://geoportal.saarland.de/freewms/dop", "sl_dop", "1.3.0", "dl-de/by-2.0"}},
      {"DE-MV", {"mv", "Luftbilder DOP20 – LAiV M-V", "Luftbilder: © GeoBasis-DE/M-V 2026, CC BY 4.0", "https://www.laiv-mv.de/Geoinformation/Geobasisdaten/", "https://www.geodaten-mv.de/dienste/adv_dop", "mv_dop", "1.3.0", "CC BY 4.0"}},
      {"DE-BW", {"bw", "Luftbilder DOP20 – LGL Baden-Württemberg", "Luftbilder: LGL-BW (2026) Datenlizenz Deutschland – Namensnennung – Version 2.0, www.lgl-bw.de", "https://www.lgl-bw.de", "https://owsproxy.lgl-bw.de/owsproxy/ows/WMS_LGL-BW_ATKIS_DOP_20_C", "IMAGES_DOP_20_RGB", "1.3.0", "dl-de/by-2.0"}},
  };
}

std::array<double, 4> boxOf(const std::vector<std::vector<double>>& rings) {
  std::array<double, 4> b = {1e9, 1e9, -1e9, -1e9};
  for (const auto& r : rings) {
    for (size_t i = 0; i + 1 < r.size(); i += 2) {
      b[0] = std::min(b[0], r[i]);
      b[1] = std::min(b[1], r[i + 1]);
      b[2] = std::max(b[2], r[i]);
      b[3] = std::max(b[3], r[i + 1]);
    }
  }
  return b;
}
}  // namespace

const std::vector<AerialState>& AERIAL_STATES() {
  static const std::vector<AerialState> states = [] {
    std::vector<AerialState> out;
    const auto& outlines = aerialStateOutlines();
    for (auto& [iso, source] : stateSources()) {
      auto o = std::find_if(outlines.begin(), outlines.end(), [&](const auto& p) { return p.first == iso; });
      if (o == outlines.end()) continue;
      AerialState s;
      s.iso = iso;
      s.source = source;
      s.rings = o->second;
      s.box = boxOf(s.rings);
      out.push_back(std::move(s));
    }
    return out;
  }();
  return states;
}

const std::vector<AerialNotFree>& AERIAL_NOT_FREE() {
  static const std::vector<AerialNotFree> list = {
      {"DE-HH",
       "the time series of the LGV's services (geodienste.hamburg.de/wms_dop_zeitreihe_unbelaubt, …_belaubt) holds satellite pictures under a closed licence "
       "(Maxar, Pléiades) beside the open aerial photographs (dl-de/by-2.0), and which years are which is not stated: not used until that is clear",
       "https://geoportal-hamburg.de"},
      {"DE-ST",
       "free (dl-de/by-2.0), but its service answers a browser with two Access-Control-Allow-Origin headers, which the browser refuses: "
       "its pictures cannot be drawn on the map (Sentinel-2 shows there)",
       "https://www.lvermgeo.sachsen-anhalt.de/de/gdp-open-data.html"},
  };
  return list;
}

bool ringHolds(const std::vector<double>& ring, double lon, double lat) {
  bool in = false;
  const size_t n = ring.size() / 2;
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const double xi = ring[2 * i], yi = ring[2 * i + 1], xj = ring[2 * j], yj = ring[2 * j + 1];
    if ((yi > lat) != (yj > lat) && lon < (xj - xi) * (lat - yi) / (yj - yi) + xi) in = !in;
  }
  return in;
}

namespace {
bool stateHolds(const AerialState& s, double lat, double lon) {
  if (lon < s.box[0] || lon > s.box[2] || lat < s.box[1] || lat > s.box[3]) return false;
  for (const auto& r : s.rings)
    if (ringHolds(r, lon, lat)) return true;
  return false;
}
/** how far a tile's corners are pushed out when its states are looked for: the outlines are about 3 km coarse */
constexpr double MARGIN = 3000;
}  // namespace

const AerialState* aerialStateAt(double lat, double lon) {
  for (const AerialState& s : AERIAL_STATES())
    if (stateHolds(s, lat, lon)) return &s;
  return nullptr;
}

std::vector<AerialTile> aerialStateTiles(int level, double minE, double minN, double maxE, double maxN) {
  std::vector<AerialTile> out;
  const auto& states = AERIAL_STATES();
  for (AerialTile t : aerialTiles(level, minE, minN, maxE, maxN)) {
    // the states its middle and its corners (pushed out by the outlines' coarseness) lie in
    const double e0 = t.minE - MARGIN, n0 = t.minN - MARGIN, e1 = t.minE + t.size + MARGIN, n1 = t.minN + t.size + MARGIN;
    const double pts[5][2] = {{(t.minE + e1 - MARGIN) / 2, (t.minN + n1 - MARGIN) / 2}, {e0, n0}, {e1, n0}, {e0, n1}, {e1, n1}};
    std::vector<int> hit;
    int inAny = 0;
    for (const auto& p : pts) {
      const auto [lat, lon] = utm32ToLatLon(p[0], p[1]);
      bool found = false;
      for (size_t i = 0; i < states.size(); i++) {
        if (!stateHolds(states[i], lat, lon)) continue;
        if (std::find(hit.begin(), hit.end(), static_cast<int>(i)) == hit.end()) hit.push_back(static_cast<int>(i));
        // (a city state inside its neighbour's outline: the first that holds the point is it)
        found = true;
        break;
      }
      if (found) inAny++;
    }
    if (hit.empty()) continue;
    std::sort(hit.begin(), hit.end());
    t.png = hit.size() > 1 || inAny < 5;
    for (int i : hit) {
      t.src = &states[static_cast<size_t>(i)].source;
      out.push_back(t);
    }
  }
  return out;
}

namespace {
/** metres a pixel of Web Mercator's zoom 0 at the equator (2π · 6378137 / 256) */
constexpr double MERC_RES0 = 156543.03392804097;
constexpr double PI = 3.14159265358979323846;
}  // namespace

int s2Zoom(double pxPerMetre, double lat) {
  const double want = 1 / std::max(pxPerMetre, 1e-9);  // metres per pixel on the screen
  const double k = std::log2(MERC_RES0 * std::cos(lat * PI / 180) / want);
  return static_cast<int>(std::min<double>(S2_MAX_ZOOM, std::max(0.0, std::ceil(k - 0.35))));
}

std::vector<AerialTile> s2Tiles(int zoom, double south, double west, double north, double east) {
  const double n = std::pow(2.0, zoom);
  const auto tx = [&](double lon) { return (lon + 180) / 360 * n; };
  const auto ty = [&](double lat) {
    const double r = std::max(-85.0511, std::min(85.0511, lat)) * PI / 180;
    return (1 - std::log(std::tan(r) + 1 / std::cos(r)) / PI) / 2 * n;
  };
  const int x0 = static_cast<int>(std::max(0.0, std::floor(tx(west))));
  const int x1 = static_cast<int>(std::min(n - 1, std::floor(tx(east))));
  const int y0 = static_cast<int>(std::max(0.0, std::floor(ty(north))));
  const int y1 = static_cast<int>(std::min(n - 1, std::floor(ty(south))));
  std::vector<AerialTile> out;
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      AerialTile t;
      t.level = zoom;
      t.col = x;
      t.row = y;
      t.src = &S2_SOURCE;
      out.push_back(t);
    }
  }
  return out;
}

std::array<double, 2> s2TileLatLon(int zoom, double x, double y) {
  const double n = std::pow(2.0, zoom);
  const double lon = x / n * 360 - 180;
  const double lat = std::atan(std::sinh(PI * (1 - 2 * y / n))) * 180 / PI;
  return {lat, lon};
}

std::string s2Url(const AerialTile& t) {
  std::string url = S2_SOURCE.wms;
  const auto put = [&](const std::string& what, int v) { url.replace(url.find(what), what.size(), jsStr(v)); };
  put("{z}", t.level);
  put("{y}", t.row);
  put("{x}", t.col);
  return url;
}

std::string aerialLayerName() { return "Luftbilder der Länder (DOP) · " + S2_SOURCE.name; }

double aerialStatesAlpha(int level) { return level <= 6 ? 1 : level == 7 ? 0.5 : 0; }

std::vector<std::string> drawAerial(const Ctx2D& g, AerialCache& cache, const AerialView& v) {
  cache.newFrame();
  std::vector<const AerialSource*> used;
  const auto credit = [&](const AerialSource* s) {
    if (std::find(used.begin(), used.end(), s) == used.end()) used.push_back(s);
  };
  if (v.scale <= 0 || v.width <= 0 || v.height <= 0) return {};
  cache.beginFrame();
  // the view in the world frame, and in UTM metres
  const double minX = v.cx - v.width / 2 / v.scale, maxX = v.cx + v.width / 2 / v.scale;
  const double minZ = v.cz - v.height / 2 / v.scale, maxZ = v.cz + v.height / 2 / v.scale;
  const double e0 = minX + v.originE, e1 = maxX + v.originE;
  const double n0 = v.originN - maxZ, n1 = v.originN - minZ;
  // screen (CSS pixels) of a UTM point
  const auto sx = [&](double e) { return v.width / 2 + (e - v.originE - v.cx) * v.scale; };
  const auto sy = [&](double n) { return v.height / 2 + (v.originN - n - v.cz) * v.scale; };
  g.save();

  // Sentinel-2 everywhere: the view's box of latitudes and longitudes (its edges are curved in UTM: their middles too)
  double south = 90, north = -90, west = 180, east = -180;
  for (double fx : {0.0, 0.5, 1.0}) {
    for (double fy : {0.0, 0.5, 1.0}) {
      const auto [lat, lon] = utm32ToLatLon(e0 + (e1 - e0) * fx, n0 + (n1 - n0) * fy);
      south = std::min(south, lat);
      north = std::max(north, lat);
      west = std::min(west, lon);
      east = std::max(east, lon);
    }
  }
  south = std::max(south, -85.0);
  north = std::min(north, 85.0);
  west = std::max(west, -180.0);
  east = std::min(east, 180.0);
  // the level of the states' pictures, and whether they cover the whole view (then Sentinel is only the coarse ground under them)
  int level = aerialLevel(v.scale * v.dpr);
  // never more than a screenful of pictures at once
  while (level < AERIAL_MAX_LEVEL && ((e1 - e0) / aerialTileSize(level) + 1) * ((n1 - n0) / aerialTileSize(level) + 1) > 30) level++;
  const double alpha = aerialStatesAlpha(level);
  bool covered = alpha >= 1;
  for (double fx : {0.0, 0.5, 1.0})
    for (double fy : {0.0, 0.5, 1.0})
      if (covered) {
        const auto [lat, lon] = utm32ToLatLon(e0 + (e1 - e0) * fx, n0 + (n1 - n0) * fy);
        covered = aerialStateAt(lat, lon) != nullptr;
      }
  if (north > south && east > west) {
    int zoom = s2Zoom(v.scale * v.dpr, (south + north) / 2);
    // never more than a screenful of pictures at once
    while (zoom > 0 && s2Tiles(zoom, south, west, north, east).size() > 40) zoom--;
    // a coarse picture first (it is there at once), the fine ones on top as they arrive
    const int zooms[2] = {std::max(0, zoom - 3), zoom};
    for (const int z : zooms) {
      if (covered && z == zoom && z != zooms[0]) break;
      // a coarse tile is long: laid on in pieces, each by the affine map of its own corners
      const int pieces = z <= 7 ? 4 : z <= 10 ? 2 : 1;
      for (const AerialTile& t : s2Tiles(z, south, west, north, east)) {
        const Image* img = cache.get(t);
        if (!img) continue;
        credit(&S2_SOURCE);
        const double step = 1.0 / pieces;
        const double px = static_cast<double>(img->width) / pieces, py = static_cast<double>(img->height) / pieces;
        for (int j = 0; j < pieces; j++) {
          for (int i = 0; i < pieces; i++) {
            const auto at = [&](double fx, double fy) {
              const auto ll = s2TileLatLon(z, t.col + fx, t.row + fy);
              const auto [e, n] = latLonToUtm32(ll[0], ll[1]);
              return std::array<double, 2>{sx(e), sy(n)};
            };
            const auto p00 = at(i * step, j * step), p10 = at((i + 1) * step, j * step), p01 = at(i * step, (j + 1) * step);
            g.setTransform(v.dpr * (p10[0] - p00[0]), v.dpr * (p10[1] - p00[1]), v.dpr * (p01[0] - p00[0]), v.dpr * (p01[1] - p00[1]), v.dpr * p00[0], v.dpr * p00[1]);
            // (a hair over the piece's edge: no seams between them)
            g.drawImage(*img, i * px, j * py, px, py, 0, 0, 1.004, 1.004);
          }
        }
      }
      if (z == zoom) break;
    }
  }

  // the states' orthophotos over it, fading in as the map zooms in
  g.setTransform(v.dpr, 0, 0, v.dpr, 0, 0);
  if (alpha > 0) {
    g.globalAlpha(alpha);
    const int levels[2] = {level + 3 <= AERIAL_MAX_LEVEL ? level + 3 : AERIAL_MAX_LEVEL, level};
    for (const int l : levels) {
      for (const AerialTile& t : aerialStateTiles(l, e0, n0, e1, n1)) {
        const Image* img = cache.get(t);
        if (l == level) credit(t.src);
        if (img) g.drawImage(*img, sx(t.minE), sy(t.minN + t.size), t.size * v.scale, t.size * v.scale);
      }
      if (l == level) break;
    }
  }
  g.restore();
  std::vector<std::string> out;
  for (const AerialSource* s : used) out.push_back(s->attribution);
  return out;
}

}  // namespace fs

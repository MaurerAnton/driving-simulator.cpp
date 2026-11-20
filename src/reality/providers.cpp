// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Asking the free street-level imagery services for pictures around a place.
 *
 * Origin: src/reality/providers.ts (MaurerAnton/driving-simulator @ 578e397,
 * v0.4.0).  The URLs and the field selections are the original's; the parsing
 * walks the JSON with platform/json.h.
 */
#include "reality/providers.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "platform/json.h"

namespace fs {

namespace {
/** Only ever hand https addresses on to the page. */
std::string https(const Json& v) {
  std::string s = v.asStr();
  return s.rfind("https://", 0) == 0 ? s : "";
}

bool okPos(double lat, double lon) {
  return std::isfinite(lat) && std::isfinite(lon) && std::fabs(lat) <= 90 &&
         std::fabs(lon) <= 180;
}

std::string licenceName(std::string id) {
  std::string k = id;
  for (auto& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (k.rfind("cc-by-sa-4", 0) == 0) return "CC BY-SA 4.0";
  if (k.rfind("cc-by-4", 0) == 0) return "CC BY 4.0";
  if (k.rfind("cc0", 0) == 0) return "CC0";
  if (k.rfind("etalab", 0) == 0) return "Licence Ouverte 2.0";
  return id.empty() ? "CC BY-SA 4.0" : id;
}

/** "2025-08-04 17:05:50.000" (UTC) → milliseconds since 1970. */
double parseKartaTime(std::string shot) {
  while (!shot.empty() && (shot.front() == ' ' || shot.front() == '\t')) shot.erase(shot.begin());
  while (!shot.empty() && (shot.back() == ' ' || shot.back() == '\t')) shot.pop_back();
  size_t sp = shot.find(' ');
  if (sp != std::string::npos) shot[sp] = 'T';
  if (shot.empty()) return NAN;
  bool hasZone = shot.find('Z') != std::string::npos || shot.find('z') != std::string::npos ||
                 (shot.size() > 3 && (shot[shot.size() - 3] == '+' || shot[shot.size() - 3] == '-'));
  if (!hasZone) shot += "Z";
  // "YYYY-MM-DDTHH:MM:SS(.sss)?Z" → ms since 1970 (UTC)
  int Y, Mo, D, H, Mi, S = 0;
  double frac = 0;
  int n = std::sscanf(shot.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &H, &Mi, &S);
  if (n < 6) return NAN;
  size_t dot = shot.find('.');
  if (dot != std::string::npos) frac = std::atof(shot.c_str() + dot);
  static const int kDays[13] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365};
  int days = (Y - 1970) * 365 + ((Y - 1969) / 4) - ((Y - 1901) / 100) + ((Y + 2099) / 400) +
             kDays[Mo > 0 && Mo < 13 ? Mo : 1] +
             (Mo > 2 && Y % 4 == 0 && (Y % 100 != 0 || Y % 400 == 0) ? 1 : 0) + (D - 1);
  return static_cast<double>(days) * 86400000.0 + H * 3600000.0 + Mi * 60000.0 +
         S * 1000.0 + frac * 1000.0;
}

/** "2025-08-04T17:05:50" → ms since 1970 (UTC), NaN when unparseable. */
double parseIsoTime(const std::string& t) {
  int Y, Mo, D, H = 0, Mi = 0, S = 0;
  if (std::sscanf(t.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &H, &Mi, &S) < 3) return NAN;
  static const int kDays[13] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365};
  int days = (Y - 1970) * 365 + ((Y - 1969) / 4) - ((Y - 1901) / 100) + ((Y + 2099) / 400) +
             kDays[Mo > 0 && Mo < 13 ? Mo : 1] +
             (Mo > 2 && Y % 4 == 0 && (Y % 100 != 0 || Y % 400 == 0) ? 1 : 0) + (D - 1);
  return static_cast<double>(days) * 86400000.0 + H * 3600000.0 + Mi * 60000.0 + S * 1000.0;
}

std::string fixed6(double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.6f", v);
  return buf;
}
}  // namespace

std::string panoramaxUrl(const SearchArea& a, int limit) {
  auto b = areaBox(a);
  return "https://api.panoramax.xyz/api/search?bbox=" + fixed6(b[0]) + "," + fixed6(b[1]) + "," +
         fixed6(b[2]) + "," + fixed6(b[3]) + "&limit=" + std::to_string(limit);
}

std::vector<StreetImage> parsePanoramax(const std::string& text) {
  std::vector<StreetImage> out;
  Json json;
  if (!Json::parse(text, json) || !json.isObject()) return out;
  const Json& features = json.at("features");
  if (!features.isArray()) return out;
  for (const auto& f : features.array) {
    if (!f.isObject()) continue;
    const Json& geometry = f.at("geometry");
    if (!geometry.isObject()) continue;
    const Json& coords = geometry.at("coordinates");
    if (!coords.isArray() || coords.array.size() < 2) continue;
    double lon = coords.array[0].asNum();
    double lat = coords.array[1].asNum();
    std::string id = f.at("id").asStr();
    const Json& assets = f.at("assets");
    auto href = [&](const char* k) -> std::string {
      if (!assets.isObject()) return "";
      const Json& a2 = assets.at(k);
      return a2.isObject() ? https(a2.at("href")) : "";
    };
    std::string thumb = href("thumb");
    if (thumb.empty()) thumb = href("sd");
    std::string url = href("sd");
    if (url.empty()) url = href("hd");
    if (id.empty() || !okPos(lat, lon) || url.empty()) continue;
    const Json& p = f.at("properties");
    double heading = p.at("view:azimuth").asNum();
    bool pano = false;
    if (p.has("pers:interior_orientation")) {
      const Json& io = p.at("pers:interior_orientation");
      pano = io.at("field_of_view").asNum() == 360;
    }
    std::string author = p.at("geovisio:producer").asStr();
    const Json& providers = f.at("providers");
    if (providers.isArray()) {
      for (const auto& pr : providers.array) {
        if (!pr.isObject()) continue;
        const Json& roles = pr.at("roles");
        bool producer = false;
        if (roles.isArray())
          for (const auto& r : roles.array)
            if (r.asStr() == "producer") producer = true;
        if (producer && !pr.at("name").asStr().empty()) author = pr.at("name").asStr();
      }
    }
    StreetImage img;
    img.source = ImageSource::Panoramax;
    img.id = id;
    img.lat = lat;
    img.lon = lon;
    img.heading = heading;
    img.pano = pano;
    img.time = parseIsoTime(p.at("datetime").asStr());
    img.author = author;
    img.licence = licenceName(p.at("license").asStr());
    img.thumb = thumb;
    img.url = url;
    img.full = href("hd");
    if (img.full.empty()) img.full = url;
    img.page = "https://api.panoramax.xyz/#focus=pic&pic=" + id;
    img.located = true;
    out.push_back(img);
  }
  return out;
}

std::string kartaviewUrl(const SearchArea& a, int limit) {
  int r = static_cast<int>(std::max(1.0, std::min(500.0, std::round(a.radius))));
  int per = std::min(150, limit);
  return "https://api.kartaview.org/2.0/photo/?lat=" + fixed6(a.lat) + "&lng=" + fixed6(a.lon) +
         "&radius=" + std::to_string(r) + "&itemsPerPage=" + std::to_string(per);
}

std::vector<StreetImage> parseKartaView(const std::string& text) {
  std::vector<StreetImage> out;
  Json json;
  if (!Json::parse(text, json) || !json.isObject()) return out;
  const Json& result = json.at("result");
  if (!result.isObject()) return out;
  const Json& data = result.at("data");
  if (!data.isArray()) return out;
  for (const auto& r : data.array) {
    if (!r.isObject()) continue;
    double lat = r.at("lat").asNum();
    double lon = r.at("lng").asNum();
    std::string id = r.at("id").asStr();
    std::string thumb = https(r.at("imageThUrl"));
    if (thumb.empty()) thumb = https(r.at("fileurlTh"));
    std::string url = https(r.at("imageLthUrl"));
    if (url.empty()) url = https(r.at("fileurlLTh"));
    if (id.empty() || !okPos(lat, lon) || url.empty()) continue;
    std::string seq = r.at("sequenceId").asStr();
    std::string projection = r.at("projection").asStr();
    for (auto& c : projection) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    StreetImage img;
    img.source = ImageSource::KartaView;
    img.id = id;
    img.lat = lat;
    img.lon = lon;
    img.heading = r.at("heading").asNum();
    img.pano = projection == "SPHERE";
    img.time = parseKartaTime(r.at("shotDate").asStr());
    img.author = "";
    img.licence = "CC BY-SA 4.0";
    img.thumb = thumb.empty() ? url : thumb;
    img.url = url;
    img.full = https(r.at("imageProcUrl"));
    if (img.full.empty()) img.full = https(r.at("fileurlProc"));
    if (img.full.empty()) img.full = url;
    if (!seq.empty())
      img.page = "https://kartaview.org/details/" + seq + "/" +
                 (r.at("sequenceIndex").asStr().empty() ? "0" : r.at("sequenceIndex").asStr()) +
                 "/track-info";
    else
      img.page = "https://kartaview.org/";
    img.located = true;
    out.push_back(img);
  }
  return out;
}

namespace {
const char* const MLY_FIELDS =
    "id,geometry,computed_geometry,compass_angle,computed_compass_angle,captured_at,is_pano,"
    "thumb_256_url,thumb_1024_url,thumb_2048_url,creator";
}  // namespace

std::string mapillaryUrl(const SearchArea& a, const std::string& token, int limit) {
  SearchArea capped = a;
  capped.radius = std::min(a.radius, 2000.0);
  auto b = areaBox(capped);
  return "https://graph.mapillary.com/images?access_token=" + token + "&fields=" + MLY_FIELDS +
         "&bbox=" + fixed6(b[0]) + "," + fixed6(b[1]) + "," + fixed6(b[2]) + "," + fixed6(b[3]) +
         "&limit=" + std::to_string(limit);
}

std::vector<StreetImage> parseMapillary(const std::string& text) {
  std::vector<StreetImage> out;
  Json json;
  if (!Json::parse(text, json) || !json.isObject()) return out;
  const Json& data = json.at("data");
  if (!data.isArray()) return out;
  for (const auto& r : data.array) {
    if (!r.isObject()) continue;
    const Json& g = r.has("computed_geometry") ? r.at("computed_geometry") : r.at("geometry");
    if (!g.isObject()) continue;
    const Json& coords = g.at("coordinates");
    if (!coords.isArray() || coords.array.size() < 2) continue;
    double lon = coords.array[0].asNum();
    double lat = coords.array[1].asNum();
    std::string id = r.at("id").asStr();
    std::string url = https(r.at("thumb_1024_url"));
    if (url.empty()) url = https(r.at("thumb_2048_url"));
    if (id.empty() || !okPos(lat, lon) || url.empty()) continue;
    double angle = r.at("computed_compass_angle").asNum();
    if (!std::isfinite(angle)) angle = r.at("compass_angle").asNum();
    StreetImage img;
    img.source = ImageSource::Mapillary;
    img.id = id;
    img.lat = lat;
    img.lon = lon;
    img.heading = angle;
    img.pano = r.at("is_pano").kind == Json::Kind::Bool && r.at("is_pano").boolean;
    img.time = r.at("captured_at").asNum();
    img.author = r.at("creator").isObject() ? r.at("creator").at("username").asStr() : "";
    img.licence = "CC BY-SA 4.0";
    img.thumb = https(r.at("thumb_256_url"));
    if (img.thumb.empty()) img.thumb = url;
    img.url = url;
    img.full = https(r.at("thumb_2048_url"));
    if (img.full.empty()) img.full = url;
    img.page = "https://www.mapillary.com/app/?pKey=" + id + "&focus=photo";
    img.located = true;
    out.push_back(img);
  }
  return out;
}

std::string serviceError(const std::string& text, int status) {
  Json json;
  if (Json::parse(text, json) && json.isObject()) {
    const Json& e = json.at("error");
    if (e.isObject() && !e.at("message").asStr().empty()) return e.at("message").asStr();
    if (!json.at("message").asStr().empty()) return json.at("message").asStr();
    const Json& st = json.at("status");
    if (st.isObject() && !st.at("apiMessage").asStr().empty()) return st.at("apiMessage").asStr();
  }
  return "HTTP " + std::to_string(status);
}

std::vector<StreetImage> searchImages(ImageSource source, const SearchArea& area,
                                       const std::string& token, HttpBackend* http) {
  std::string url;
  if (source == ImageSource::Panoramax) {
    url = panoramaxUrl(area);
  } else if (source == ImageSource::KartaView) {
    url = kartaviewUrl(area);
  } else {
    if (token.empty()) throw std::runtime_error("token");
    url = mapillaryUrl(area, token);
  }
  // no cookies, no referrer: the service learns the place asked for and the
  // IP address, nothing else
  if (!http) throw std::runtime_error("token");
  bool ok = false;
  int status = 0;
  std::string body = http->get(url, ok, status);
  Json json;
  bool parsed = Json::parse(body, json);
  if (!ok) throw std::runtime_error(serviceError(parsed ? body : "", status));
  if (source == ImageSource::Panoramax) return parsePanoramax(body);
  if (source == ImageSource::KartaView) return parseKartaView(body);
  return parseMapillary(body);
}

std::vector<PlaceLink> placeLinks(double lat, double lon, double heading) {
  char la[32], lo[32], hs[32];
  std::snprintf(la, sizeof la, "%.6f", lat);
  std::snprintf(lo, sizeof lo, "%.6f", lon);
  int h = std::isfinite(heading) ? static_cast<int>(std::lround(std::fmod(heading, 360.0))) : 0;
  if (h < 0) h += 360;
  std::snprintf(hs, sizeof hs, "%d", h);
  std::string L(la), O(lo), H(hs);
  return {
      {"kartaview", "KartaView", "https://kartaview.org/map/@" + L + "," + O + ",18z"},
      {"mapillary", "Mapillary", "https://www.mapillary.com/app/?lat=" + L + "&lng=" + O + "&z=18"},
      {"panoramax", "Panoramax",
       "https://api.panoramax.xyz/#focus=map&map=18/" + L + "/" + O},
      {"google", "Google Street View",
       "https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=" + L + "," + O +
           "&heading=" + H},
      {"googlemaps", "Google Maps",
       "https://www.google.com/maps/@?api=1&map_action=map&center=" + L + "," + O +
           "&zoom=19&basemap=satellite"},
      {"googleearth", "Google Earth",
       "https://earth.google.com/web/@" + L + "," + O + ",0a,450d,35y," + H + "h,45t,0r"},
      {"osm", "OpenStreetMap",
       "https://www.openstreetmap.org/?mlat=" + L + "&mlon=" + O + "#map=19/" + L + "/" + O},
  };
}

}  // namespace fs

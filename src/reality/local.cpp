// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The player's own pictures: file names, side-car lists and the panos index.
 *
 * Origin: src/reality/local.ts (MaurerAnton/driving-simulator @ 578e397,
 * v0.4.0).
 */
#include "reality/local.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <regex>

#include "platform/json.h"

namespace fs {

namespace {
/** name without folder, extension and heading suffix – what lists refer to */
std::string baseName(const std::string& name) {
  size_t slash = name.find_last_of("/\\");
  std::string b = slash == std::string::npos ? name : name.substr(slash + 1);
  // the original strips one ".ext" of 1–5 characters (e.g. ".jpg", ".tar.gz")
  size_t dot = b.find_last_of('.');
  if (dot != std::string::npos && b.size() - dot >= 2 && b.size() - dot <= 6) {
    bool alnum = true;
    for (size_t i = dot + 1; i < b.size(); i++)
      if (!std::isalnum(static_cast<unsigned char>(b[i]))) alnum = false;
    if (alnum) b.erase(dot);
  }
  return b;
}

std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

}  // namespace

bool hasExt(const std::string& name, const char* pattern) {
  std::regex r(pattern, std::regex::icase);
  return std::regex_search(name, r);
}

namespace {

/** The first of `names` the record carries as a number (NaN when none). */
double firstOf(const Json& r, const char* const* names, size_t n) {
  for (size_t i = 0; i < n; i++) {
    double v = r.at(names[i]).asNum();
    if (std::isfinite(v)) return v;
  }
  return NAN;
}

double firstFinite(std::initializer_list<double> v) {
  for (double x : v)
    if (std::isfinite(x)) return x;
  return NAN;
}
}  // namespace

NameInfo parseFileName(const std::string& name,
                       const std::function<bool(double, double)>& plausible) {
  std::string base = baseName(name);
  NameInfo out;
  out.id = base;

  // "<lat>,<lon>" in any of , _ ; x @ + between the two numbers
  static const std::regex coord(
      R"((-?\d{1,3}\.\d{3,})[\s_,;x@+]+(-?\d{1,3}\.\d{3,}))");
  std::smatch c;
  if (std::regex_search(base, c, coord)) {
    double a = std::atof(c[1].str().c_str());
    double b = std::atof(c[2].str().c_str());
    bool latFirst = std::fabs(a) <= 90 && std::fabs(b) <= 180;
    bool lonFirst = std::fabs(b) <= 90 && std::fabs(a) <= 180;
    if (latFirst && (plausible(a, b) || !lonFirst || !plausible(b, a))) {
      out.lat = a;
      out.lon = b;
    } else if (lonFirst) {
      out.lat = b;
      out.lon = a;
    }
  }
  std::string rest = c.ready() ? base : base;
  if (c.ready()) {
    rest = base;
    rest.replace(static_cast<size_t>(c.position(0)), static_cast<size_t>(c.length(0)), " ");
  }
  // a named heading: heading=090, hdg_45, bearing-12 …
  static const std::regex named(
      R"((?:^|[\s_\-.])(?:h|hdg|head|heading|yaw|dir|az|azimuth|bearing)[_=\-]?(\d{1,3}(?:\.\d+)?)(?![\d.]))",
      std::regex::icase);
  std::smatch n;
  if (std::regex_search(rest, n, named)) {
    double v = std::atof(n[1].str().c_str());
    if (v <= 360) out.heading = std::fmod(v, 360.0);
  } else {
    // "<panorama id>_090": one picture per direction
    static const std::regex tail(R"((.{6,})_(\d{3}))");
    std::smatch t;
    if (std::regex_search(base, t, tail)) {
      double v = std::atof(t[2].str().c_str());
      if (v <= 360) {
        out.heading = std::fmod(v, 360.0);
        out.id = t[1].str();
      }
    }
  }
  return out;
}

namespace {
std::vector<std::string> splitCsvLine(const std::string& line, char sep) {
  std::vector<std::string> out;
  std::string cur;
  bool quoted = false;
  for (size_t i = 0; i < line.size(); i++) {
    char ch = line[i];
    if (quoted) {
      if (ch == '"' && i + 1 < line.size() && line[i + 1] == '"') {
        cur += '"';
        i++;
      } else if (ch == '"') {
        quoted = false;
      } else {
        cur += ch;
      }
    } else if (ch == '"') {
      quoted = true;
    } else if (ch == sep) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur += ch;
    }
  }
  out.push_back(cur);
  for (auto& v : out) {
    size_t s = v.find_first_not_of(" \t\r\n");
    size_t e = v.find_last_not_of(" \t\r\n");
    v = s == std::string::npos ? "" : v.substr(s, e - s + 1);
  }
  return out;
}

/** Fill one record from the common key spellings. */
void entryOf(const Json& r, double lat, double lon, const std::string& key, ListEntry& out) {
  out.key = key;
  out.lat = lat;
  out.lon = lon;
  static const char* const KEYS[] = {"file", "filename", "path", "image",     "name",
                                     "panoid", "pano",   "panoramaid", "id"};
  static const char* const HEAD[] = {"heading", "yaw", "bearing", "azimuth",
                                     "compassangle", "north", "rotation", "direction"};
  auto pick = [&](const char* const* names, size_t n) -> std::string {
    for (size_t i = 0; i < n; i++) {
      const Json& v = r.at(names[i]);
      if (v.kind != Json::Kind::Null) return v.asStr();
    }
    return "";
  };
  if (out.key.empty()) out.key = pick(KEYS, 9);
  if (!std::isfinite(out.lat)) {
    static const char* const LAT[] = {"lat", "latitude", "y"};
    out.lat = firstOf(r, LAT, 3);
  }
  if (!std::isfinite(out.lon)) {
    static const char* const LON[] = {"lon", "lng", "long", "longitude", "x"};
    out.lon = firstOf(r, LON, 5);
  }
  if (!std::isfinite(out.heading)) out.heading = firstOf(r, HEAD, 7);
  if (!std::isfinite(out.time)) {
    static const char* const TIME[] = {"date", "time", "datetime", "capturedat",
                                       "timestamp", "shotdate"};
    for (const char* k : TIME) {
      double v = r.at(k).asNum();
      if (std::isfinite(v)) {
        out.time = v;
        break;
      }
      std::string s = r.at(k).asStr();
      if (!s.empty()) {
        int Y, Mo, D, H = 0, Mi = 0, S = 0;
        if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &H, &Mi, &S) >= 3) {
          static const int kDays[13] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365};
          int days = (Y - 1970) * 365 + ((Y - 1969) / 4) - ((Y - 1901) / 100) +
                     ((Y + 2099) / 400) + kDays[Mo > 0 && Mo < 13 ? Mo : 1] +
                     (Mo > 2 && Y % 4 == 0 && (Y % 100 != 0 || Y % 400 == 0) ? 1 : 0) + (D - 1);
          out.time = static_cast<double>(days) * 86400000.0 + H * 3600000.0 + Mi * 60000.0 + S * 1000.0;
        }
      }
    }
  }
}
}  // namespace

std::vector<ListEntry> parseList(const std::string& text) {
  std::vector<ListEntry> out;
  std::string t = text;
  // strip a BOM and surrounding space
  if (t.size() >= 3 && static_cast<unsigned char>(t[0]) == 0xEF &&
      static_cast<unsigned char>(t[1]) == 0xBB && static_cast<unsigned char>(t[2]) == 0xBF)
    t.erase(0, 3);
  size_t s = t.find_first_not_of(" \t\r\n");
  size_t e = t.find_last_not_of(" \t\r\n");
  t = s == std::string::npos ? "" : t.substr(s, e - s + 1);

  auto add = [&](ListEntry entry) {
    // the original stores keys lower case, without the extension
    entry.key = lower(baseName(entry.key));
    if (!entry.key.empty()) out.push_back(entry);
  };

  if (!t.empty() && (t[0] == '{' || t[0] == '[')) {
    Json json;
    if (!Json::parse(t, json)) return out;
    std::function<void(const Json&, const std::string&)> visit = [&](const Json& v,
                                                                    const std::string& key) {
      if (v.isArray()) {
        for (const auto& x : v.array) visit(x, "");
        return;
      }
      if (v.isObject()) {
        if (v.has("features") && v.at("features").isArray()) {
          for (const auto& f : v.at("features").array) {
            if (!f.isObject()) continue;
            const Json& g = f.at("geometry");
            if (!g.isObject() || g.at("type").asStr() != "Point") continue;
            const Json& coords = g.at("coordinates");
            if (!coords.isArray() || coords.array.size() < 2) continue;
            ListEntry entry;
            entryOf(f.at("properties"), coords.array[1].asNum(), coords.array[0].asNum(),
                    f.at("id").asStr(), entry);
            add(entry);
          }
          return;
        }
        ListEntry entry;
        std::string id = key;
        if (id.empty()) id = v.at("file").asStr();
        entryOf(v, NAN, NAN, id, entry);
        if (!entry.key.empty()) {
          add(entry);
        } else {
          // { "<id>": { lat, lng, … }, … } or a wrapper such as { panoramas: [ … ] }
          for (const auto& kv : v.object) {
            if (kv.second.isObject() || kv.second.isArray()) visit(kv.second, kv.first);
          }
        }
      }
    };
    visit(json, "");
    return out;
  }

  // CSV / TSV with a header line
  std::vector<std::string> lines;
  size_t pos = 0;
  while (pos <= t.size()) {
    size_t nl = t.find('\n', pos);
    std::string line = t.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) lines.push_back(line);
    if (nl == std::string::npos) break;
    pos = nl + 1;
  }
  if (lines.size() < 2) return out;
  const std::string& first = lines[0];
  char sep = first.find('\t') != std::string::npos
                 ? '\t'
                 : (std::count(first.begin(), first.end(), ';') >
                            std::count(first.begin(), first.end(), ',')
                        ? ';'
                        : ',');
  std::vector<std::string> header = splitCsvLine(first, sep);
  for (size_t i = 1; i < lines.size(); i++) {
    std::vector<std::string> cells = splitCsvLine(lines[i], sep);
    Json rec;
    rec.kind = Json::Kind::Object;
    for (size_t j = 0; j < header.size(); j++) {
      Json v;
      v.kind = Json::Kind::String;
      v.str = j < cells.size() ? cells[j] : "";
      rec.object[header[j]] = v;
    }
    ListEntry entry;
    entryOf(rec, NAN, NAN, "", entry);
    add(entry);
  }
  return out;
}

const ListEntry* findInList(const std::vector<ListEntry>& list, const std::string& name,
                            const NameInfo& info) {
  std::string base = lower(baseName(name));
  std::string id = lower(info.id);
  const ListEntry* prefix = nullptr;
  for (const auto& e : list) {
    if (e.key == base || e.key == id) return &e;
    if (!prefix && e.key.size() >= 8 && (base.rfind(e.key, 0) == 0 || base.find(e.key) != std::string::npos))
      prefix = &e;
  }
  return prefix;
}

StreetImage describeLocal(const LocalSource& src, const std::vector<ListEntry>& list,
                          const std::function<bool(double, double)>& plausible) {
  NameInfo info = parseFileName(src.name, plausible);
  const ListEntry* entry = findInList(list, src.name, info);
  const ImageMeta& m = src.meta;
  double lat = m.lat, lon = m.lon;
  if (!std::isfinite(lat) || !std::isfinite(lon)) {
    lat = entry ? entry->lat : info.lat;
    lon = entry ? entry->lon : info.lon;
  }
  bool located = std::isfinite(lat) && std::isfinite(lon);
  StreetImage img;
  img.source = ImageSource::Local;
  img.id = src.name;
  img.lat = located ? lat : NAN;
  img.lon = located ? lon : NAN;
  img.heading = firstFinite({m.heading, entry ? entry->heading : NAN, info.heading});
  img.pano = looksLikePanorama(m);
  img.time = firstFinite({m.time, entry ? entry->time : NAN});
  img.author = "";
  img.licence = "";
  img.thumb = src.hasThumb ? src.thumb : src.url;
  img.url = src.url;
  img.full = src.url;
  img.page = "";
  size_t slash = src.name.find_last_of("/\\");
  img.title = slash == std::string::npos ? src.name : src.name.substr(slash + 1);
  img.located = located;
  return img;
}

const char* const IMAGE_FILE = R"(\.(jpe?g|png|webp|avif)$)";
const char* const LIST_FILE = R"(\.(json|geojson|csv|tsv)$)";

std::vector<StreetImage> imagesFromIndex(const std::string& text, const std::string& baseUrl) {
  std::vector<StreetImage> out;
  Json index;
  if (!Json::parse(text, index) || !index.isObject()) return out;
  const Json& list = index.at("images");
  if (!list.isArray()) return out;
  for (const auto& r : list.array) {
    if (!r.isObject()) continue;
    std::string file = r.at("file").asStr();
    if (file.empty() || !hasExt(file, IMAGE_FILE) || file.find("..") != std::string::npos)
      continue;
    double lat = r.at("lat").asNum();
    double lon = r.at("lon").asNum();
    bool located = std::isfinite(lat) && std::isfinite(lon);
    // percent-encode every path segment (as encodeURIComponent does)
    std::string url = baseUrl;
    size_t start = 0;
    while (start <= file.size()) {
      size_t slash = file.find('/', start);
      std::string seg = file.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
      std::string enc;
      for (unsigned char c : seg) {
        // encodeURIComponent leaves unreserved marks alone — including the dot
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
          enc += static_cast<char>(c);
        } else {
          char buf[8];
          std::snprintf(buf, sizeof buf, "%%%02X", c);
          enc += buf;
        }
      }
      url += enc;
      if (slash == std::string::npos) break;
      url += '/';
      start = slash + 1;
    }
    StreetImage img;
    img.source = ImageSource::Local;
    img.id = "panos/" + file;
    img.lat = located ? lat : NAN;
    img.lon = located ? lon : NAN;
    img.heading = r.at("heading").asNum();
    img.pano = r.at("pano").kind == Json::Kind::Bool && r.at("pano").boolean;
    img.time = r.at("time").asNum();
    img.author = "";
    img.licence = "";
    img.thumb = url;
    img.url = url;
    img.full = url;
    img.page = "";
    size_t slash = file.find_last_of('/');
    img.title = slash == std::string::npos ? file : file.substr(slash + 1);
    img.located = located;
    out.push_back(img);
  }
  return out;
}

}  // namespace fs

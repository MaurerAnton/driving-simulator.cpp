// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/history.ts @ v1.7.7
#include "game/history.h"

#include <cmath>
#include <cstdio>

#include "core/date.h"
#include "core/js.h"
#include "core/jsonwrite.h"
#include "platform/json.h"
#include "platform/plat.h"

namespace fs {

namespace {

const char* const PLACES_KEY = "fahrsim.places";
const char* const WAYS_KEY = "fahrsim.ways";
const char* const FAVOURITES_KEY = "fahrsim.favourites";

/** The list stored under a key (none: nothing stored, or not a list). */
Json read(const char* key) {
  Json list;
  const std::optional<std::string> raw = plat::localStorage().getItem(key);
  if (!raw || raw->empty() || !Json::parse(*raw, list) || !list.isArray()) {
    list = Json();
    list.kind = Json::Kind::Array;
  }
  return list;
}

/** (storage full or not allowed: the history is a convenience) */
void write(const char* key, const JsonOut& list) { plat::localStorage().setItem(key, list.text()); }

bool finite(const Json& n) { return n.isNumber() && std::isfinite(n.number); }

/** (the way one faces at a place; what is no number is none: north) */
double yawOf(const Json& n) { return n.isNumber() ? n.number : 0; }

/** `Math.round((yaw || 0) * 1000) / 1000` */
double roundYaw(double yaw) { return jsRound((yaw == yaw ? yaw : 0) * 1000) / 1000; }

/** `new Date(ms).toISOString()` */
std::string isoString(double ms) {
  const DateFields f = utcFields(ms);
  char buf[40];
  if (f.year >= 0 && f.year <= 9999) std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", f.year, f.month + 1, f.date, f.hours, f.minutes, f.seconds, f.milliseconds);
  else std::snprintf(buf, sizeof buf, "%c%06d-%02d-%02dT%02d:%02d:%02d.%03dZ", f.year < 0 ? '-' : '+', f.year < 0 ? -f.year : f.year, f.month + 1, f.date, f.hours, f.minutes, f.seconds, f.milliseconds);
  return buf;
}

void put(JsonOut& o, const PlaceRecord& p) {
  o.beginObject();
  o.key("name");
  o.string(p.name);
  o.key("x");
  o.number(p.x);
  o.key("z");
  o.number(p.z);
  o.key("yaw");
  o.number(p.yaw);
  o.key("date");
  o.string(p.date);
  o.endObject();
}

void put(JsonOut& o, const NamedPlace& p) {
  o.beginObject();
  o.key("name");
  o.string(p.name);
  o.key("x");
  o.number(p.x);
  o.key("z");
  o.number(p.z);
  o.key("yaw");
  o.number(p.yaw);
  o.endObject();
}

void put(JsonOut& o, const WayRecord& w) {
  o.beginObject();
  o.key("stops");
  o.beginArray();
  for (const NamedPlace& s : w.stops) put(o, s);
  o.endArray();
  o.key("self");
  o.boolean(w.self);
  o.key("date");
  o.string(w.date);
  o.endObject();
}

template <class T>
void writeList(const char* key, const std::vector<T>& list) {
  JsonOut o;
  o.beginArray();
  for (const T& e : list) put(o, e);
  o.endArray();
  write(key, o);
}

/** the places of a way as one text: the same places are the same way */
std::string keyOf(const std::vector<NamedPlace>& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (i) out += ";";
    out += s[i].name + "|" + jsStr(jsRound(s[i].x)) + "|" + jsStr(jsRound(s[i].z));
  }
  return out;
}

}  // namespace

std::vector<PlaceRecord> loadPlaces() {
  std::vector<PlaceRecord> out;
  const Json list = read(PLACES_KEY);
  for (const Json& p : list.array) {
    if (!(p.at("name").isString() && finite(p.at("x")) && finite(p.at("z")))) continue;
    out.push_back({.name = p.at("name").str, .x = p.at("x").number, .z = p.at("z").number, .yaw = yawOf(p.at("yaw")), .date = p.at("date").str});
  }
  return out;
}

std::vector<PlaceRecord> savePlace(const NamedPlace& p) { return savePlace(p, plat::dateMs()); }

std::vector<PlaceRecord> savePlace(const NamedPlace& p, double now) {
  std::vector<PlaceRecord> list;
  for (const PlaceRecord& q : loadPlaces())
    if (q.name != p.name || std::hypot(q.x - p.x, q.z - p.z) > 30) list.push_back(q);
  list.insert(list.begin(), {.name = p.name, .x = jsRound(p.x * 10) / 10, .z = jsRound(p.z * 10) / 10, .yaw = roundYaw(p.yaw), .date = isoString(now)});
  if (list.size() > PLACES_KEPT) list.resize(PLACES_KEPT);
  writeList(PLACES_KEY, list);
  return list;
}

std::vector<WayRecord> loadWays() {
  std::vector<WayRecord> out;
  const Json list = read(WAYS_KEY);
  for (const Json& w : list.array) {
    const Json& stops = w.at("stops");
    if (!stops.isArray() || stops.array.empty()) continue;
    WayRecord r;
    bool ok = true;
    for (const Json& s : stops.array) {
      if (!(finite(s.at("x")) && finite(s.at("z")))) {
        ok = false;
        break;
      }
      r.stops.push_back({.name = s.at("name").str, .x = s.at("x").number, .z = s.at("z").number, .yaw = yawOf(s.at("yaw"))});
    }
    if (!ok) continue;
    r.self = w.at("self").kind == Json::Kind::Bool && w.at("self").boolean;
    r.date = w.at("date").str;
    out.push_back(std::move(r));
  }
  return out;
}

std::vector<WayRecord> saveWay(const std::vector<NamedPlace>& stops, bool self) { return saveWay(stops, self, plat::dateMs()); }

std::vector<WayRecord> saveWay(const std::vector<NamedPlace>& stops, bool self, double now) {
  if (stops.empty()) return loadWays();
  const std::string mine = keyOf(stops);
  std::vector<WayRecord> list;
  for (const WayRecord& w : loadWays())
    if (keyOf(w.stops) != mine) list.push_back(w);
  WayRecord r;
  for (const NamedPlace& s : stops) r.stops.push_back({.name = s.name, .x = jsRound(s.x * 10) / 10, .z = jsRound(s.z * 10) / 10, .yaw = roundYaw(s.yaw)});
  r.self = self;
  r.date = isoString(now);
  list.insert(list.begin(), std::move(r));
  if (list.size() > WAYS_KEPT) list.resize(WAYS_KEPT);
  writeList(WAYS_KEY, list);
  return list;
}

// ------------------------------------------------------------------ favourite places

std::vector<FavouriteRecord> loadFavourites() {
  std::vector<FavouriteRecord> out;
  const Json list = read(FAVOURITES_KEY);
  for (const Json& p : list.array) {
    if (!(p.at("name").isString() && finite(p.at("x")) && finite(p.at("z")))) continue;
    out.push_back({.name = p.at("name").str, .x = p.at("x").number, .z = p.at("z").number, .yaw = yawOf(p.at("yaw"))});
  }
  return out;
}

std::vector<FavouriteRecord> saveFavourite(const FavouriteRecord& p) {
  std::vector<FavouriteRecord> list;
  for (const FavouriteRecord& q : loadFavourites())
    if (std::hypot(q.x - p.x, q.z - p.z) > 20) list.push_back(q);
  list.insert(list.begin(), {.name = p.name, .x = jsRound(p.x * 10) / 10, .z = jsRound(p.z * 10) / 10, .yaw = roundYaw(p.yaw)});
  if (list.size() > FAVOURITES_KEPT) list.resize(FAVOURITES_KEPT);
  writeList(FAVOURITES_KEY, list);
  return list;
}

std::vector<FavouriteRecord> removeFavourite(double x, double z) {
  std::vector<FavouriteRecord> list;
  for (const FavouriteRecord& q : loadFavourites())
    if (std::hypot(q.x - x, q.z - z) > 20) list.push_back(q);
  writeList(FAVOURITES_KEY, list);
  return list;
}

}  // namespace fs

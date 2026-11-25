// SPDX-License-Identifier: AGPL-3.0-only
// 2.0: the online place search (ui/onlinesearch.h)
#include "ui/onlinesearch.h"

#include <cmath>

#include "core/js.h"
#include "platform/json.h"
#include "platform/plat.h"

namespace fs {

std::string photonUrl(const std::string& q, std::optional<std::pair<double, double>> near, int limit, const std::string& lang) {
  std::string url = std::string(PHOTON_API) + "?q=" + encodeURIComponent(q) + "&limit=" + std::to_string(limit);
  if (near) url += "&lat=" + toFixed(near->first, 4) + "&lon=" + toFixed(near->second, 4);
  if (lang == "de" || lang == "en" || lang == "fr" || lang == "it") url += "&lang=" + lang;
  return url;
}

std::vector<OnlinePlace> parsePhoton(const std::string& text) {
  std::vector<OnlinePlace> out;
  Json j;
  if (!Json::parse(text, j) || !j.isObject() || !j.at("features").isArray()) return out;
  for (const Json& f : j.at("features").array) {
    const Json& c = f.at("geometry").at("coordinates");
    if (!c.isArray() || c.array.size() < 2 || !c.array[0].isNumber() || !c.array[1].isNumber()) continue;
    const Json& p = f.at("properties");
    const auto str = [&p](const char* k) { return p.at(k).isString() ? p.at(k).str : std::string(); };
    OnlinePlace o;
    o.lon = c.array[0].number;
    o.lat = c.array[1].number;
    if (!std::isfinite(o.lat) || !std::isfinite(o.lon)) continue;
    o.name = str("name");
    const std::string street = str("street"), house = str("housenumber");
    if (o.name.empty()) o.name = house.empty() ? street : street + " " + house;
    else if (!street.empty() && o.name != street) o.name += ", " + (house.empty() ? street : street + " " + house);
    if (o.name.empty()) continue;
    // its town (where it is not the place itself), the state, the country
    std::vector<std::string> where;
    for (const char* k : {"city", "state", "country"}) {
      const std::string v = str(k);
      if (!v.empty() && v != o.name && (where.empty() || where.back() != v)) where.push_back(v);
    }
    o.detail = join(where, ", ");
    o.kind = str("osm_value");
    out.push_back(std::move(o));
  }
  return out;
}

Task<std::vector<OnlinePlace>> searchPhoton(std::string q, std::optional<std::pair<double, double>> near, int limit, std::string lang) {
  plat::Response res = co_await plat::fetch(photonUrl(q, near, limit, lang));
  // (a language the server does not know is refused with an error: then once more without asking for one)
  if (!res.ok() && res.status >= 400 && res.status < 500 && !lang.empty()) res = co_await plat::fetch(photonUrl(q, near, limit, ""));
  if (!res.ok()) co_return std::vector<OnlinePlace>();
  co_return parsePhoton(res.text());
}

}  // namespace fs

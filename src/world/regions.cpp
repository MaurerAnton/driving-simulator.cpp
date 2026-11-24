// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/regions.ts and the index loading of src/ui/regions.ts @ v1.7.7
#include "world/regions.h"

#include <cmath>
#include <unordered_set>

#include "platform/plat.h"

namespace fs {

std::string regionMapPath(const std::string& id) { return std::string(REGIONS_DIR) + "/" + id + "/map.json"; }

std::optional<RegionIndex> parseRegions(const Json& json) {
  if (!json.isObject()) return std::nullopt;
  const Json& list = json.at("regions");
  if (!list.isArray()) return std::nullopt;
  RegionIndex index;
  std::unordered_set<std::string> seen;
  for (const Json& e : list.array) {
    if (!e.isObject()) continue;
    const std::string id = e.at("id").isString() ? e.at("id").str : "";
    if (id.empty() || seen.count(id)) continue;
    const Json& b = e.at("box");
    if (!b.isArray() || b.array.size() != 4) continue;
    Region r;
    bool ok = true;
    for (size_t i = 0; i < 4; i++) {
      if (!b.array[i].isNumber() || !std::isfinite(b.array[i].number)) ok = false;
      else r.box[i] = b.array[i].number;
    }
    if (!ok) continue;
    r.id = id;
    r.map = e.at("map").isString() && !e.at("map").str.empty() ? e.at("map").str : regionMapPath(id);
    r.name = e.at("name").isString() && !e.at("name").str.empty() ? e.at("name").str : id;
    if (e.at("tiles").isArray())
      for (const Json& t : e.at("tiles").array)
        if (t.isString()) r.tiles.push_back(t.str);
    seen.insert(id);
    index.regions.push_back(std::move(r));
  }
  const std::string latest = json.at("latest").isString() ? json.at("latest").str : "";
  index.latest = seen.count(latest) ? latest : (index.regions.empty() ? "" : index.regions[0].id);
  return index;
}

const Region* defaultRegion(const RegionIndex* index) {
  if (!index) return nullptr;
  for (const Region& r : index->regions)
    if (r.id == index->latest) return &r;
  return index->regions.empty() ? nullptr : &index->regions[0];
}

const Region* regionById(const RegionIndex* index, const std::string& id) {
  if (!index || id.empty()) return nullptr;
  for (const Region& r : index->regions)
    if (r.id == id) return &r;
  return nullptr;
}

bool boxContains(const std::array<double, 4>& box, double x, double z) { return x >= box[0] && x <= box[2] && z >= box[1] && z <= box[3]; }

const Region* regionAt(const RegionIndex* index, double x, double z) {
  if (!index) return nullptr;
  for (const Region& r : index->regions)
    if (boxContains(r.box, x, z)) return &r;
  return nullptr;
}

Task<std::optional<RegionIndex>> loadRegionIndex(std::string siteBase) {
  const std::string urls[2] = {std::string(MAP_ASSETS_URL) + "regions.json", siteBase + "regions.json"};
  for (const std::string& url : urls) {
    plat::Request req;
    req.url = url;
    req.cache = "no-cache";
    plat::Response res = co_await plat::fetch(std::move(req));
    if (!res.ok()) continue;   // (offline, or a site without the copy: try the next)
    Json json;
    if (!Json::parse(res.text(), json)) continue;
    std::optional<RegionIndex> index = parseRegions(json);
    if (index) co_return index;
  }
  co_return std::nullopt;
}

}  // namespace fs

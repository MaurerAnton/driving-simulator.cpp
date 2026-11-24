// SPDX-License-Identifier: AGPL-3.0-only
#include "world/regiontiles.h"

#include "core/js.h"
#include "platform/json.h"

namespace fs {

RegionTiles::RegionTiles(std::string partsBase, std::string assetsBase) : partsBase_(std::move(partsBase)), assetsBase_(std::move(assetsBase)) {}

Task<bool> RegionTiles::open(std::vector<std::string> partFiles) {
  // (all indexes are asked for at once; each is a few tens of kilobytes)
  std::vector<Task<plat::Response>> asked;
  for (const std::string& file : partFiles) {
    // "62.tar" → parts/62.json
    const size_t dot = file.rfind('.');
    asked.push_back(plat::fetch(partsBase_ + file.substr(0, dot) + ".json"));
  }
  bool any = false;
  for (size_t i = 0; i < asked.size(); i++) {
    plat::Response res = co_await std::move(asked[i]);
    if (!res.ok()) {
      plat::warn("[region] no index of part " + partFiles[i] + " (HTTP " + std::to_string(res.status) + ")");
      continue;
    }
    Json json;
    if (!Json::parse(res.text(), json) || !json.at("tiles").isObject()) {
      plat::warn("[region] the index of part " + partFiles[i] + " cannot be read");
      continue;
    }
    const int part = static_cast<int>(files_.size());
    files_.push_back(json.at("file").isString() ? json.at("file").str : partFiles[i]);
    for (const auto& kv : json.at("tiles").object) {
      const Json& v = kv.second;
      if (!v.isArray() || v.array.size() < 2) continue;
      // (a tile that two parts hold: the later part is the newer build)
      where_[kv.first] = Where{part, v.array[0].asNum(), v.array[1].asNum()};
    }
    any = true;
  }
  co_return any;
}

Task<plat::Response> RegionTiles::fetch(std::string key) {
  auto it = where_.find(key);
  if (it == where_.end()) {
    plat::Response none;
    none.status = 404;
    co_return none;
  }
  const Where w = it->second;
  plat::Request req;
  req.url = assetsBase_ + files_[static_cast<size_t>(w.part)];
  // (a plain byte range: browsers send it without asking the server first, and the server answers 206)
  req.headers.emplace_back("Range", "bytes=" + jsStr(w.offset) + "-" + jsStr(w.offset + w.size - 1));
  plat::Response res = co_await plat::fetch(std::move(req));
  if (res.status == 206) res.status = 200;
  else if (res.status == 200 && static_cast<double>(res.body.size()) > w.size) {
    // a server that ignores ranges sent the whole part: the tile is in it
    if (static_cast<double>(res.body.size()) >= w.offset + w.size) {
      Bytes slice(res.body.begin() + static_cast<long>(w.offset), res.body.begin() + static_cast<long>(w.offset + w.size));
      res.body = std::move(slice);
    } else res.status = 0;
  }
  if (res.ok()) bytesFetched += static_cast<double>(res.body.size());
  co_return res;
}

}  // namespace fs

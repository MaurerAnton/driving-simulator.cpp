// SPDX-License-Identifier: AGPL-3.0-only
// 2.0: play on another region in one step (game/startat.h)
#include "game/startat.h"

#include <cmath>

#include "platform/json.h"
#include "platform/plat.h"

namespace fs {

void saveStartAt(const std::string& region, const SpawnJson& spot) {
  JsonWriter w;
  w.beginObject();
  w.key("region");
  w.string(region);
  w.key("name");
  w.string(spot.name);
  w.key("x");
  w.number(spot.x);
  w.key("z");
  w.number(spot.z);
  w.key("yaw");
  w.number(spot.yaw);
  w.endObject();
  plat::sessionStorage().setItem(START_AT_KEY, w.take());
}

std::optional<SpawnJson> takeStartAt(const std::string& region) {
  const std::optional<std::string> raw = plat::sessionStorage().getItem(START_AT_KEY);
  if (!raw) return std::nullopt;
  // (used once: whatever happens, it is not kept for the next start)
  plat::sessionStorage().removeItem(START_AT_KEY);
  Json j;
  if (!Json::parse(*raw, j) || !j.isObject()) return std::nullopt;
  if (!j.at("region").isString() || j.at("region").str != region) return std::nullopt;
  if (!j.at("x").isNumber() || !j.at("z").isNumber() || !std::isfinite(j.at("x").number) || !std::isfinite(j.at("z").number)) return std::nullopt;
  SpawnJson s;
  s.name = j.at("name").isString() ? j.at("name").str : "";
  s.x = j.at("x").number;
  s.z = j.at("z").number;
  s.yaw = j.at("yaw").isNumber() && std::isfinite(j.at("yaw").number) ? j.at("yaw").number : 0;
  s.kind = "street";
  s.rough = true;
  return s;
}

}  // namespace fs

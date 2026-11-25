// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/extrasigns.ts @ v1.7.7
#include "world/extrasigns.h"

#include <cmath>

#include "world/loader.h"

namespace fs {

FS_JSON_DEFINE(ExtraPost, x, y, z, yaw, s, h, big, r)
FS_JSON_DEFINE(TimedRuleJson, e, d, k, v, w)

void fromJson(JsonReader& r, ExtraSignsJson& v) {
  r.object([&](std::string_view k) {
    if (k == "v") {
      double n = 0;
      r.number(n);
      v.v = static_cast<int>(n);
    } else if (k == "map") r.string(v.map);
    else if (k == "tiles") {
      r.object([&](std::string_view key) {
        v.tiles.emplace_back(std::string(key), std::vector<ExtraPost>{});
        fromJson(r, v.tiles.back().second);
      });
    } else if (k == "rules") fromJson(r, v.rules);
  });
}

ExtraSigns::ExtraSigns(const ExtraSignsJson& json) : rules(json.rules) {
  for (const auto& [key, list] : json.tiles) tiles[key] = list;
}

int ExtraSigns::posts() const {
  int n = 0;
  for (const auto& [key, list] : tiles) n += static_cast<int>(list.size());
  return n;
}

void ExtraSigns::mergeInto(const std::string& key, std::vector<SignPost>& signs, const std::function<double(double x, double z, double yRef)>& ground) const {
  const auto it = tiles.find(key);
  if (it == tiles.end()) return;
  for (const ExtraPost& p : it->second) {
    if (p.r) {
      SignPost* own = nullptr;
      for (SignPost& q : signs) {
        if (std::abs(q.x - p.x) < 0.6 && std::abs(q.z - p.z) < 0.6) {
          own = &q;
          break;
        }
      }
      if (own) {
        own->s = p.s;
        continue;
      }
    }
    SignPost copy;
    copy.x = p.x;
    copy.y = p.y;
    copy.z = p.z;
    copy.yaw = p.yaw;
    copy.s = p.s;
    copy.h = p.h;
    if (p.big) copy.big = p.big;
    const double y = ground ? ground(p.x, p.z, p.y + 1.5) : JS_NAN;
    if (std::isfinite(y) && std::abs(y - p.y) < 6) copy.y = y;
    signs.push_back(copy);
  }
}

Task<std::shared_ptr<ExtraSigns>> loadExtraSigns(std::shared_ptr<MapLoader> map) {
  try {
    const std::optional<ExtraSignsJson> json = co_await map->json<ExtraSignsJson>("signs.json.gz");
    if (!json || json->v != 1 || json->map != map->world.meta.generated) co_return nullptr;
    co_return std::make_shared<ExtraSigns>(*json);
  } catch (...) {
    co_return nullptr;
  }
}

TimedRules::TimedRules(const std::vector<TimedRuleJson>& rules, std::function<double()> clock) : clock(std::move(clock)) {
  for (const TimedRuleJson& r : rules) {
    Rule rule;
    rule.d = r.d;
    rule.k = r.k;
    rule.v = r.v.value_or(0);
    if (r.w == "wet") {
      rule.wet = true;
      rule.text = "bei Nässe";
    } else {
      const std::optional<TimeWindow> when = parseTimeWindow(r.w);
      if (!when) continue;
      rule.when = *when;
      rule.text = windowText(*when);
    }
    byEdge[r.e].push_back(std::move(rule));
  }
}

const SignTime& TimedRules::now() {
  const double ms = clock();
  if (!at || !(std::abs(ms - atMs) < 1000)) {
    at = signTimeOf(ms);
    atMs = ms;
  }
  return *at;
}

bool TimedRules::holdsNow(const Rule& r) { return r.wet ? wet() : holds(r.when, now()); }

std::optional<TimedRules::Limit> TimedRules::limit(int edge, int dir, bool heavy) {
  const auto it = byEdge.find(edge);
  if (it == byEdge.end()) return std::nullopt;
  std::optional<Limit> best;
  for (const Rule& r : it->second) {
    if (r.k != "max" && !(heavy && r.k == "hgvMax")) continue;
    if (r.d != 0 && r.d != dir) continue;
    if (!holdsNow(r)) continue;
    if (!best || r.v < best->v) best = Limit{r.v, r.text};
  }
  return best;
}

std::string TimedRules::shut(int edge, int dir, bool heavy) {
  const auto it = byEdge.find(edge);
  if (it == byEdge.end()) return "";
  for (const Rule& r : it->second) {
    if (r.k != "shut" && !(heavy && r.k == "hgvShut")) continue;
    if (r.d != 0 && r.d != dir) continue;
    if (holdsNow(r)) return r.text;
  }
  return "";
}

}  // namespace fs

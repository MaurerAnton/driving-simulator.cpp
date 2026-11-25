// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/buslines.ts @ v1.7.7
#include "world/buslines.h"

#include <algorithm>

#include "world/loader.h"
#include "world/world.h"
#include "world/worldjson.h"

namespace fs {

std::vector<int> encodeWays(const std::vector<int32_t>& ways) {
  std::vector<int> out;
  for (size_t i = 0; i < ways.size(); i++) out.push_back(i == 0 ? ways[0] : ways[i] - ways[i - 1]);
  return out;
}

BusLines::BusLines(const BusLinesJson& json) : map(json.map), places(json.places) {
  size_t total = 0;
  int maxWay = -1;
  for (size_t index = 0; index < json.lines.size(); index++) {
    const BusLineJson& l = json.lines[index];
    std::vector<int32_t> ways(l.w.size());
    int w = 0;
    for (size_t i = 0; i < l.w.size(); i++) {
      w = i == 0 ? l.w[0] : w + l.w[i];
      ways[i] = w;
      if (w > maxWay) maxWay = w;
    }
    std::vector<BusLineStop> stops;
    for (size_t i = 0; i + 2 < l.st.size(); i += 3)
      if (l.st[i + 1] < static_cast<double>(ways.size())) stops.push_back(BusLineStop{static_cast<int>(l.st[i]), static_cast<int>(l.st[i + 1]), l.st[i + 2]});
    std::stable_sort(stops.begin(), stops.end(), [](const BusLineStop& a, const BusLineStop& b) { return a.k != b.k ? a.k < b.k : a.m < b.m; });
    total += ways.size();
    BusLine line;
    line.index = static_cast<int>(index);
    line.ref = l.ref;
    line.name = l.name;
    line.from = l.from;
    line.to = l.to;
    line.operator_ = l.operator_;
    line.colour = l.colour;
    line.kind = l.kind;
    line.osm = l.osm;
    line.end = std::min(l.end.value_or(static_cast<int>(ways.size())), static_cast<int>(ways.size()));
    line.ways = std::move(ways);
    line.stops = std::move(stops);
    own_.push_back(std::move(line));
    lines.push_back(&own_.back());
  }
  for (size_t i = 0; i < json.lines.size(); i++) {
    const std::optional<int>& b = json.lines[i].back;
    if (b && *b >= 0 && *b < static_cast<int>(lines.size())) lines[i]->back = lines[static_cast<size_t>(*b)];
  }
  // which lines run along each way: counted, then filled in (one pass each)
  start_.assign(static_cast<size_t>(maxWay + 2), 0);
  for (const BusLine* line : lines)
    for (int32_t w : line->ways)
      if (w >= 0) start_[static_cast<size_t>(w) + 1]++;
  for (int w = 0; w <= maxWay; w++) start_[static_cast<size_t>(w) + 1] += start_[static_cast<size_t>(w)];
  refLine_.assign(total, 0);
  refK_.assign(total, 0);
  std::vector<uint32_t> fill(start_.begin(), start_.begin() + (maxWay + 1));
  for (const BusLine* line : lines) {
    for (size_t k = 0; k < line->ways.size(); k++) {
      const int32_t w = line->ways[k];
      if (w < 0) continue;
      const uint32_t at = fill[static_cast<size_t>(w)]++;
      refLine_[at] = static_cast<uint32_t>(line->index);
      refK_[at] = static_cast<uint32_t>(k);
    }
  }
}

bool BusLines::fits(const std::string& generated, std::optional<int> placesIn) const { return map == generated && (!placesIn || *placesIn == places); }

void BusLines::forEachAt(int w, const std::function<void(BusLine& line, int k)>& visit) const {
  if (w < 0 || static_cast<size_t>(w) + 1 >= start_.size()) return;
  for (uint32_t i = start_[static_cast<size_t>(w)]; i < start_[static_cast<size_t>(w) + 1]; i++) visit(*lines[refLine_[i]], static_cast<int>(refK_[i]));
}

std::vector<BusLines::At> BusLines::at(int w) const {
  std::vector<At> out;
  forEachAt(w, [&](BusLine& line, int k) { out.push_back(At{&line, k}); });
  return out;
}

bool BusLines::has(int w) const { return w >= 0 && static_cast<size_t>(w) + 1 < start_.size() && start_[static_cast<size_t>(w) + 1] > start_[static_cast<size_t>(w)]; }

std::vector<BusLineStop>& BusLines::stopsOn(const BusLine& line, int k0, int k1, std::vector<BusLineStop>& out) const {
  out.clear();
  const std::vector<BusLineStop>& s = line.stops;
  size_t lo = 0;
  size_t hi = s.size();
  while (lo < hi) {
    const size_t mid = (lo + hi) >> 1;
    if (s[mid].k < k0) lo = mid + 1;
    else hi = mid;
  }
  for (size_t i = lo; i < s.size() && s[i].k <= k1; i++) out.push_back(s[i]);
  return out;
}

Task<std::shared_ptr<BusLines>> loadBusLines(MapLoader* map, std::optional<int> places) {
  try {
    std::optional<BusLinesJson> json = co_await map->json<BusLinesJson>("buslines.json.gz");
    if (!json) co_return nullptr;
    auto lines = std::make_shared<BusLines>(*json);
    co_return lines->fits(map->world.json.meta.generated, places) ? lines : nullptr;
  } catch (...) {
    co_return nullptr;
  }
}

}  // namespace fs

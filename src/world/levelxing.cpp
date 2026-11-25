// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/levelxing.ts @ v1.7.7
#include "world/levelxing.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "core/js.h"
#include "world/levelcrossings.data.h"

namespace fs {

namespace {

/** One record of the list: where it is and what the map says. */
struct Tagged {
  double x = 0;
  double z = 0;
  bool rail = false;
  XingTags tags;
};

constexpr double CELL = 100;
int64_t key(int64_t cx, int64_t cz) { return (cx + 4096) * 8192 + (cz + 4096); }

/** The list of the map's tags, by cells of 100 m. */
const std::unordered_map<int64_t, std::vector<Tagged>>& tagged() {
  static const auto* index = [] {
    auto* idx = new std::unordered_map<int64_t, std::vector<Tagged>>();
    for (const std::string& rec : split(LEVEL_CROSSING_DATA, ";")) {
      const std::vector<std::string> f = split(rec, " ");
      if (f.size() < 3 || f[2].size() < 5) continue;
      const std::string& code = f[2];
      Tagged t;
      t.x = jsNumber(f[0]);
      t.z = jsNumber(f[1]);
      t.rail = code[0] == 'r';
      t.tags = XingTags{code[1], code[2], code[3], code[4]};
      (*idx)[key(static_cast<int64_t>(std::floor(t.x / CELL)), static_cast<int64_t>(std::floor(t.z / CELL)))].push_back(t);
    }
    return idx;
  }();
  return *index;
}

/** half barriers on the main roads (tertiary and up) where a railway crosses them; the trams' crossings have none */
Barrier guessBarrier(bool rail, double tier) { return rail && tier >= 3 ? "half" : "none"; }

/** lights where a railway crosses a residential road or a bigger one */
bool guessLit(bool rail, double tier) { return rail && tier >= 2; }

}  // namespace

std::optional<XingTags> taggedAt(double x, double z, double reach) {
  const auto& idx = tagged();
  const int64_t cx = static_cast<int64_t>(std::floor(x / CELL));
  const int64_t cz = static_cast<int64_t>(std::floor(z / CELL));
  const Tagged* best = nullptr;
  double bd = reach;
  for (int i = -1; i <= 1; i++) {
    for (int j = -1; j <= 1; j++) {
      const auto it = idx.find(key(cx + i, cz + j));
      if (it == idx.end()) continue;
      for (const Tagged& t : it->second) {
        const double d = std::hypot(t.x - x, t.z - z);
        if (d <= bd) {
          bd = d;
          best = &t;
        }
      }
    }
  }
  if (!best) return std::nullopt;
  return best->tags;
}

Equipment equipmentFor(double x, double z, bool rail, double tier, const std::optional<XingTags>& tags) {
  (void)x;
  (void)z;
  if (tags) {
    // (a road's traffic lights at a tram's crossing: those rule it; nothing of the railway's own)
    if (tags->control == 's' && !rail) return Equipment{"none", "none", false, rail, true};
    const Barrier barrier = tags->barrier == 'f'                                                 ? "full"
                            : tags->barrier == 'h' || tags->barrier == 'd' || tags->barrier == 'y' ? "half"
                            : tags->barrier == 'n'                                                 ? "none"
                                                                                                   : guessBarrier(rail, tier);
    // (barriers always come with lights in Germany; without barriers the map's word, else the road's)
    const bool lit = tags->light == 'y' || (tags->light != 'n' && (barrier != "none" || (tags->barrier != 'n' && guessLit(rail, tier))));
    const LightKind light = !lit ? "none" : barrier != "none" || !rail ? "yellowRed" : "blink";
    const bool bell = tags->bell == 'y' || (tags->bell != 'n' && lit && rail);
    return Equipment{barrier, light, bell, rail, true};
  }
  const Barrier barrier = guessBarrier(rail, tier);
  const bool lit = barrier != "none" || guessLit(rail, tier);
  return Equipment{barrier, !lit ? "none" : barrier != "none" ? "yellowRed" : "blink", lit && rail, rail, false};
}

}  // namespace fs

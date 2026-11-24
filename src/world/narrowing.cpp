// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/narrowing.ts @ v1.7.7
#include "world/narrowing.h"

#include <utility>

namespace fs {

std::vector<double> buildOutEntries(const std::vector<BuildOutFeature>& feats) {
  std::vector<double> out;
  for (const BuildOutFeature& f : feats) {
    if (f.kind != "bump") continue;
    // (a tag that is not there: null)
    auto tag = [&](const char* name) -> const std::string* {
      auto it = f.tags.find(name);
      return it == f.tags.end() ? nullptr : &it->second;
    };
    auto is = [&](const char* name, const char* value) {
      const std::string* v = tag(name);
      return v && *v == value;
    };
    const std::string* tc = tag("traffic_calming");
    if (!tc || (*tc != "choker" && *tc != "chicane")) continue;
    const std::string* lengthTag = tag("length");
    double len = jsNumber(lengthTag ? *lengthTag : std::string(), false);
    if (!(len >= 1.5 && len <= 20)) len = 5;
    const std::string* sideTag = tag("side");
    if (!sideTag) sideTag = tag("traffic_calming:side");
    std::vector<std::pair<int, double>> sides;
    if (sideTag && *sideTag == "left") sides = {{1, f.s}};
    else if (sideTag && *sideTag == "right") sides = {{-1, f.s}};
    else if (*tc == "chicane") sides = {{-1, f.s - len * 0.8}, {1, f.s + len * 0.8}};
    else sides = {{-1, f.s}, {1, f.s}};
    const std::string* depthTag = tag("traffic_calming:depth");
    const double want = jsNumber(depthTag ? *depthTag : std::string(), false);
    int flags = *tc == "choker" && sides.size() > 1 ? BO_PAIRED : 0;
    if (is("beacon", "yes") || is("traffic_calming:beacon", "yes")) flags |= BO_BEACON;
    if (is("planted", "no")) flags |= BO_BARE;
    if (is("tree", "no")) flags |= BO_NO_TREE;
    for (const auto& [side, at] : sides) {
      out.push_back(jsRound(at * 100) / 100);
      out.push_back(side);
      out.push_back(len);
      out.push_back(want > 0 ? jsRound(want * 100) / 100 : 0);
      out.push_back(flags);
    }
  }
  return out;
}

}  // namespace fs

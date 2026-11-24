// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/placesearch.ts @ v1.7.7
#include "ui/placesearch.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "core/js.h"

namespace fs {

namespace {

/** how big a kind of town is (a city first) */
int rankOf(const std::string& kind) {
  static const std::pair<const char*, int> RANK[] = {{"city", 0}, {"town", 1}, {"village", 2}, {"suburb", 3}, {"quarter", 4}, {"neighbourhood", 5}, {"hamlet", 6}, {"isolated_dwelling", 7}};
  for (const auto& [k, r] : RANK)
    if (kind == k) return r;
  return 9;
}

struct Match {
  bool ok = false;
  bool whole = false;
};

/** `text.split(' ')` */
std::vector<std::string> splitSpaces(const std::string& text) { return split(text, " "); }

/** Does the name match the words asked for: each word starts one of the name's words? */
Match matches(const std::string& name, const std::vector<std::string>& words) {
  const std::string n = normaliseAddress(name);
  const std::vector<std::string> parts = splitSpaces(n);
  bool ok = true;
  for (const std::string& w : words) {
    bool some = false;
    for (const std::string& p : parts)
      if (startsWith(p, w)) {
        some = true;
        break;
      }
    if (!some) {
      ok = false;
      break;
    }
  }
  return {ok, ok && startsWith(n, join(words, " "))};
}

/** `normaliseAddress(query).split(' ').filter(Boolean)` */
std::vector<std::string> wordsOf(const std::string& query) {
  std::vector<std::string> words;
  for (const std::string& w : splitSpaces(normaliseAddress(query)))
    if (!w.empty()) words.push_back(w);
  return words;
}

}  // namespace

std::vector<PlaceHit> searchPlaces(const std::string& query, const std::vector<TownSource>& towns, const std::vector<StopSource>& stops, const AddressSearch& addresses, size_t limit, const PlaceNear& near,
                                   const std::vector<LineSource>& lines) {
  const std::vector<std::string> words = wordsOf(query);
  if (words.empty()) return {};
  const auto dist = [&](double x, double z) { return near ? std::hypot(x - near->x, z - near->z) : 0.0; };
  std::vector<PlaceHit> out;
  std::unordered_set<std::string> seen;
  const auto add = [&](PlaceHit h) {
    const std::string key = normaliseAddress(h.label) + "|" + jsStr(jsRound(h.x / 50)) + "|" + jsStr(jsRound(h.z / 50));
    if (seen.count(key) || out.size() >= limit) return;
    seen.insert(key);
    out.push_back(std::move(h));
  };
  // a line number asked for: the line comes first ("5" finds the tram 5)
  const std::string q = trim(normaliseAddress(query));
  if (!q.empty()) {
    int n = 0;
    for (const LineSource& l : lines) {
      if (normaliseAddress(l.ref) != q) continue;
      if (n++ >= 2) break;
      PlaceHit h;
      h.label = l.ref + " · " + (l.kind == "tram" ? "tram" : "bus");
      h.street = "";
      h.x = l.x;
      h.z = l.z;
      h.exact = true;
      h.what = "line";
      h.line = l;
      add(std::move(h));
    }
  }
  // towns and villages: the name begun with first, the bigger the earlier
  struct TownMatch {
    const TownSource* p;
    Match m;
  };
  std::vector<TownMatch> t;
  for (const TownSource& p : towns) {
    const Match m = matches(p.name, words);
    if (m.ok) t.push_back({&p, m});
  }
  std::stable_sort(t.begin(), t.end(), [&](const TownMatch& a, const TownMatch& b) {
    if (a.m.whole != b.m.whole) return a.m.whole;
    const int ra = rankOf(a.p->kind), rb = rankOf(b.p->kind);
    if (ra != rb) return ra < rb;
    const double da = dist(a.p->x, a.p->z), db = dist(b.p->x, b.p->z);
    if (da != db) return da < db;
    return compareGerman(a.p->name, b.p->name) < 0;
  });
  const size_t townsShown = std::max<size_t>(3, static_cast<size_t>(std::ceil(static_cast<double>(limit) / 2)));
  for (size_t i = 0; i < t.size() && i < townsShown; i++) {
    PlaceHit h;
    h.label = t[i].p->name;
    h.street = "";
    h.x = t[i].p->x;
    h.z = t[i].p->z;
    h.exact = false;
    h.what = "town";
    add(std::move(h));
  }
  // stops: by their name, or by town and name ("Baunatal Rathaus")
  struct StopMatch {
    const StopSource* p;
    Match m;
    Match n;
  };
  std::vector<StopMatch> s;
  for (const StopSource& p : stops) {
    const Match m = matches(p.town + " " + p.name, words);
    const Match n = matches(p.name, words);
    if (m.ok || n.ok) s.push_back({&p, m, n});
  }
  std::stable_sort(s.begin(), s.end(), [&](const StopMatch& a, const StopMatch& b) {
    if (a.n.whole != b.n.whole) return a.n.whole;
    return dist(a.p->x, a.p->z) < dist(b.p->x, b.p->z);
  });
  for (size_t i = 0; i < s.size() && i < 3; i++) {
    const StopSource& p = *s[i].p;
    PlaceHit h;
    h.label = !p.town.empty() && p.name.find(p.town) == std::string::npos ? p.name + ", " + p.town : p.name;
    h.street = "";
    h.x = p.x;
    h.z = p.z;
    h.exact = false;
    h.what = "stop";
    add(std::move(h));
  }
  // streets and houses
  if (addresses)
    for (const AddressHit& a : addresses(query, limit, near)) {
      PlaceHit h;
      static_cast<AddressHit&>(h) = a;
      h.what = "address";
      add(std::move(h));
    }
  return out;
}

// 2.0: the regions of the map of all regions, by their name and their country
std::vector<PlaceHit> searchRegions(const std::string& query, const std::vector<RegionSource>& regions, size_t limit, const std::function<std::string(const RegionSource&)>& label) {
  const std::vector<std::string> words = wordsOf(query);
  if (words.empty()) return {};
  struct RegionMatch {
    const RegionSource* r;
    bool whole;
  };
  std::vector<RegionMatch> found;
  for (const RegionSource& r : regions) {
    const Match byName = matches(r.name, words);
    // (a country alone finds its regions: "norway"; a name and its country together: "bergen norway")
    const Match both = byName.ok ? byName : matches(r.name + " " + r.country, words);
    if (both.ok) found.push_back({&r, byName.whole});
  }
  std::stable_sort(found.begin(), found.end(), [](const RegionMatch& a, const RegionMatch& b) { return a.whole && !b.whole; });
  std::vector<PlaceHit> out;
  for (size_t i = 0; i < found.size() && out.size() < limit; i++) {
    const RegionSource& r = *found[i].r;
    PlaceHit h;
    h.label = label ? label(r) : (r.country.empty() ? r.name : r.name + ", " + r.country);
    h.street = "";
    h.x = r.x;
    h.z = r.z;
    h.exact = false;
    h.what = "region";
    h.region = r.id;
    out.push_back(std::move(h));
  }
  return out;
}

}  // namespace fs

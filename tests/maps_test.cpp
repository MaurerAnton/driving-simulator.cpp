// LINK: ui i18n game sim audio world geo core platform
// SPDX-License-Identifier: AGPL-3.0-only
// Origin: tests/ui/{addresspicker,geoip,placesearch,regions,pathdraw}.test.ts @ v1.7.7
/**
 * The maps' parts that can be checked without a picture: the search field's line
 * numbers (addresspicker), where the player is from the IP address (geoip), the
 * search of the big map (placesearch), the region index (regions), and a line
 * drawn on the map laid onto the roads (pathdraw).
 *
 *   tools/t.sh maps_test
 */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/js.h"
#include "core/task.h"
#include "geo/utm.h"
#include "platform/native/native.h"
#include "platform/plat.h"
#include "ui/addresspicker.h"
#include "ui/bigmap.h"
#include "ui/geoip.h"
#include "ui/pathdraw.h"
#include "ui/placesearch.h"
#include "ui/regions.h"
#include "world/lanes.h"
#include "world/regions.h"
#include "world/routegraph.h"

using namespace fs;

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
  }
}

plat::Response answer(int status, const std::string& body) {
  plat::Response r;
  r.status = status;
  r.body.assign(body.begin(), body.end());
  return r;
}

// ------------------------------------------------------------------------------------------
// addresspicker
// ------------------------------------------------------------------------------------------

void addressPickerTests() {
  // a line number is whole at one character
  for (const char* q : {"5", "52", "N3", "n3", "A49", "x7"}) check(isLineToken(q), std::string("line ") + q);
  // a name is not a line number
  for (const char* q : {"a", "ab", "rathaus", "5a", "N", "baunatal"}) check(!isLineToken(q), std::string("no line ") + q);
  // a line token with a space around it still counts
  check(isLineToken(" 5 "), "' 5 '");
}

// ------------------------------------------------------------------------------------------
// geoip
// ------------------------------------------------------------------------------------------

FetchFn fetchOf(const std::string& json, bool ok = true) {
  return [json, ok](plat::Request) -> Task<plat::Response> { co_return answer(ok ? 200 : 500, json); };
}

void geoipTests() {
  // the place of an IP address, from the service
  {
    const IpPlace place = plat::native::wait(ipPlace(fetchOf(R"({"latitude": 50.11, "longitude": 8.68, "success": true})")));
    check(place && *place == (LatLon{50.11, 8.68}), "ipPlace");
  }
  // no coordinates, a refusal or a failure: no place, no throwing
  check(!plat::native::wait(ipPlace(fetchOf(R"({"success": false})"))), "refused");
  check(!plat::native::wait(ipPlace(fetchOf(R"({"latitude": "x", "longitude": 8})"))), "no number");
  check(!plat::native::wait(ipPlace(fetchOf("{}", false))), "not ok");
  check(!plat::native::wait(ipPlace([](plat::Request) -> Task<plat::Response> { co_return plat::Response{}; })), "offline");
  // the region whose box holds the place, and none for a place outside
  {
    const auto [x, z] = latLonToWorld(50.11, 8.68);  // (Hesse)
    RegionIndex index;
    index.latest = "suedost";
    index.regions.push_back({"suedost", "Südost-Hessen", {x - 1000, z - 1000, x + 1000, z + 1000}, "regions/suedost/map.json", {}});
    const Region* r = ipRegion(&index, LatLon{50.11, 8.68});
    check(r && r->id == "suedost", "ipRegion");
    const auto [farX, farZ] = latLonToWorld(48.0, 11.0);  // (Bavaria, far away)
    check(ipRegion(&index, LatLon{48.0, 11.0}) == nullptr, "far away: none");
    check(!((farX >= x - 1000 && farX <= x + 1000) && (farZ >= z - 1000 && farZ <= z + 1000)), "far is outside");
    // (no index, no place: no region)
    check(ipRegion(nullptr, LatLon{50, 8}) == nullptr, "no index");
    check(ipRegion(&index, std::nullopt) == nullptr, "no place");
  }
}

// ------------------------------------------------------------------------------------------
// placesearch
// ------------------------------------------------------------------------------------------

std::vector<std::string> labels(const std::vector<PlaceHit>& hits) {
  std::vector<std::string> out;
  for (const PlaceHit& h : hits) out.push_back(h.label);
  return out;
}

void placeSearchTests() {
  const std::vector<TownSource> towns = {
      {"Baunatal", "town", 0, 0},
      {"Kassel", "city", 6700, -7000},
      {"Großenritte", "suburb", -2000, 300},
      {"Altenbauna", "suburb", 500, -400},
  };
  const std::vector<StopSource> stops = {
      {"Rathaus", "Baunatal", 100, 50},
      {"Rathaus", "Kassel", 6600, -7100},
      {"Großenritte Bahnhof", "Baunatal", -2100, 200},
  };
  // a town by the beginning of its name, whatever the keyboard makes of the umlaut
  {
    const auto hits = searchPlaces("grossenr", towns, stops, nullptr);
    check(!hits.empty() && hits[0].label == "Großenritte" && hits[0].what == "town", "grossenr: town first");
    bool stop = false;
    for (const PlaceHit& h : hits) stop = stop || (h.what == "stop" && h.label.rfind("Großenritte Bahnhof", 0) == 0);
    check(stop, "grossenr: the stop");
  }
  // stops by their name, the nearer first, with their town
  {
    const auto hits = searchPlaces("rathaus", towns, stops, nullptr, 8, AddressIndex::Near{6000, -7000});
    check(labels(hits) == std::vector<std::string>{"Rathaus, Kassel", "Rathaus, Baunatal"}, "rathaus: nearer first");
    for (const PlaceHit& h : hits) check(h.what == "stop", "rathaus: stops");
    // by town and name
    const auto byTown = searchPlaces("baunatal rathaus", towns, stops, nullptr);
    check(!byTown.empty() && byTown[0].label == "Rathaus, Baunatal", "by town and name");
  }
  // the addresses come after the towns and the stops, each place once, and at most as many as asked
  {
    const AddressSearch addresses = [](const std::string& q, size_t, const PlaceNear&) {
      std::vector<AddressHit> out;
      std::string low = q;
      for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (low.rfind("kass", 0) == 0) out.push_back({"Kasseler Straße, Baunatal", "Kasseler Straße", 10, 10, false});
      return out;
    };
    const auto hits = searchPlaces("kass", towns, stops, addresses);
    std::vector<std::string> what;
    for (const PlaceHit& h : hits) what.push_back(h.what);
    // (Kassel the town, the stop "Rathaus" of Kassel, then the street)
    check(what == std::vector<std::string>{"town", "stop", "address"}, "kass: town, stop, address");
    check(searchPlaces("", towns, stops, addresses).empty(), "empty query");
    check(searchPlaces("a", towns, stops, addresses, 2).size() <= 2, "limit");
  }
  // a line number finds the line of public transport
  {
    const std::vector<LineSource> lines = {{"5", "tram", 100, 50, 0}, {"52", "bus", 6600, -7100, 3}, {"N3", "bus", 6700, -7000, 7}};
    const auto hits = searchPlaces("5", towns, stops, nullptr, 8, std::nullopt, lines);
    const PlaceHit* line = nullptr;
    for (const PlaceHit& h : hits)
      if (h.what == "line" && !line) line = &h;
    check(line != nullptr, "the line 5 is found");
    if (line) {
      check(line->line && line->line->ref == "5" && line->line->kind == "tram", "line 5: a tram");
      check(line->x == 100, "line 5: where its first stop is");
    }
    // a line number wins over a town that begins with it
    check(!hits.empty() && hits[0].what == "line", "line first");
  }
  // a line number with letters, and one that matches nothing
  {
    const std::vector<LineSource> lines = {{"N3", "bus", 1, 2, 0}, {"52", "bus", 3, 4, 1}};
    bool n3 = false;
    for (const PlaceHit& h : searchPlaces("n3", towns, stops, nullptr, 8, std::nullopt, lines)) n3 = n3 || (h.what == "line" && h.line && h.line->ref == "N3");
    check(n3, "n3");
    // a number no line has: no line hit (but the towns/stops as before)
    for (const PlaceHit& h : searchPlaces("99", towns, stops, nullptr, 8, std::nullopt, lines)) check(h.what != "line", "99: no line");
  }
}

// ------------------------------------------------------------------------------------------
// regions
// ------------------------------------------------------------------------------------------

const char* const INDEX_JSON = R"({"latest": "hesse", "regions": [
  {"id": "hesse", "name": "Nordhessen", "box": [-65000, -46000, 59000, 60000], "map": "regions/hesse/map.json", "tiles": ["1.tar"]},
  {"id": "bremen", "name": "Bremen", "box": [-48000, -215000, -31000, -200000], "map": "regions/bremen/map.json", "tiles": ["34.tar"]}]})";

/** A fetch that answers the index at the URLs it knows, and 404 elsewhere. */
FetchFn fakeFetch(std::optional<std::string> index) {
  return [index](plat::Request req) -> Task<plat::Response> {
    const std::string& url = req.url;
    const std::string tail = "/regions.json";
    if (index && url.size() >= tail.size() && url.compare(url.size() - tail.size(), tail.size(), tail) == 0) co_return answer(200, *index);
    co_return answer(404, "null");
  };
}

void regionsTests() {
  // the index comes from the repository, the site as a fallback
  {
    const auto index = plat::native::wait(loadRegionIndex(true, "map-assets/", fakeFetch(INDEX_JSON)));
    check(index.has_value(), "index from the repository");
    if (index) {
      check(index->latest == "hesse", "latest");
      check(index->regions.size() == 2 && index->regions[0].id == "hesse" && index->regions[1].id == "bremen", "ids");
    }
  }
  // the site alone is enough (no repository)
  {
    const auto index = plat::native::wait(loadRegionIndex(false, "map-assets/", fakeFetch(INDEX_JSON)));
    check(index && index->regions.size() == 2, "the site alone");
  }
  // a repository without regions: the site is tried
  {
    const FetchFn repoFirst = [](plat::Request req) -> Task<plat::Response> {
      const std::string& url = req.url;
      if (url.rfind("https://raw.githubusercontent.com", 0) == 0) co_return answer(404, "null");
      const std::string tail = "/regions.json";
      if (url.size() >= tail.size() && url.compare(url.size() - tail.size(), tail.size(), tail) == 0) co_return answer(200, INDEX_JSON);
      co_return answer(404, "null");
    };
    const auto index = plat::native::wait(loadRegionIndex(true, "map-assets/", repoFirst));
    check(index && index->regions.size() == 2, "the site after the repository");
  }
  // no regions anywhere: null (only the site's map is offered)
  check(!plat::native::wait(loadRegionIndex(true, "map-assets/", fakeFetch(std::nullopt))), "no regions anywhere");
  // an unreadable index (not parseable): null, not a crash
  check(!plat::native::wait(loadRegionIndex(true, "map-assets/", [](plat::Request) -> Task<plat::Response> { co_return answer(200, "\"nope\""); })), "unreadable index");
  // a region is found by its id, or null
  {
    const auto index = plat::native::wait(loadRegionIndex(false, "map-assets/", fakeFetch(INDEX_JSON)));
    assert(index);
    const Region* b = findRegion(&*index, "bremen");
    check(b && b->name == "Bremen", "findRegion bremen");
    check(findRegion(&*index, "ghost") == nullptr, "findRegion ghost");
    check(findRegion(nullptr, "bremen") == nullptr, "findRegion no index");
    check(findRegion(&*index, "") == nullptr, "findRegion ''");
  }
}

// ------------------------------------------------------------------------------------------
// pathdraw: a line drawn on the map laid onto the roads, on a grid of streets 100 m apart
// ------------------------------------------------------------------------------------------

/**
 * A grid of nx × nz junctions 100 m apart (junction (i, j) at x = 100 i,
 * z = 100 j): the streets of each row (west to east) come first, then those of
 * each column (north to south).  `oneWay(e)`: road e may only be driven from
 * its junction a to b.  Every turn is allowed, turning round only at a dead end.
 */
struct Grid {
  int nx = 0;
  std::vector<std::pair<double, double>> nodes;
  std::vector<std::pair<int, int>> edges;
  RouteGraph g;
  RoadSource roads;

  Grid(int nx_, int nz, const std::function<bool(int)>& oneWay = [](int) { return false; }, const std::function<bool(int)>& noEntry = [](int) { return false; })
      : nx(nx_) {
    for (int j = 0; j < nz; j++)
      for (int i = 0; i < nx; i++) nodes.push_back({i * 100.0, j * 100.0});
    for (int j = 0; j < nz; j++)
      for (int i = 0; i + 1 < nx; i++) edges.push_back({j * nx + i, j * nx + i + 1});
    for (int j = 0; j + 1 < nz; j++)
      for (int i = 0; i < nx; i++) edges.push_back({j * nx + i, (j + 1) * nx + i});
    const int n = static_cast<int>(edges.size()) * 2;
    const auto start = [this](int w) { return (w & 1) ? edges[w >> 1].second : edges[w >> 1].first; };
    const auto end = [this](int w) { return (w & 1) ? edges[w >> 1].first : edges[w >> 1].second; };
    g.n = n;
    g.len.assign(n, 0);
    g.kind.assign(n, 0);
    for (int w = 0; w < n; w++) {
      g.len[w] = (w & 1) == 1 && oneWay(w >> 1) ? 0 : 100;
      if (noEntry(w)) g.kind[w] |= RG_NO_ENTRY;
    }
    g.first.assign(n + 1, 0);
    for (int w = 0; w < n; w++) {
      g.first[w] = static_cast<uint32_t>(g.to.size());
      std::vector<int> outs;
      for (int t = 0; t < n; t++)
        if (g.len[t] > 0 && start(t) == end(w) && t != (w ^ 1)) outs.push_back(t);
      if (outs.empty() && g.len[w ^ 1] > 0) outs.push_back(w ^ 1);
      for (const int t : outs) {
        g.to.push_back(static_cast<uint32_t>(t));
        g.turn.push_back(static_cast<uint8_t>(t == (w ^ 1) ? TURN_UTURN : TURN_STRAIGHT));
        g.across.push_back(5);
      }
    }
    g.first[n] = static_cast<uint32_t>(g.to.size());
    for (int w = 0; w < n; w++) {
      g.secs.push_back(g.len[w] / 14);
      g.x0.push_back(static_cast<float>(nodes[start(w)].first));
      g.z0.push_back(static_cast<float>(nodes[start(w)].second));
      g.x1.push_back(static_cast<float>(nodes[end(w)].first));
      g.z1.push_back(static_cast<float>(nodes[end(w)].second));
    }
    g.cls.assign(n, 0);
    g.firstIn.assign(n + 1, 0);
    roads.g = &g;
    roads.roadsIn = [this](double minX, double minZ, double maxX, double maxZ, const std::function<void(int)>& visit) {
      for (int e = 0; e < static_cast<int>(edges.size()); e++) {
        const auto [ax, az] = nodes[edges[e].first];
        const auto [bx, bz] = nodes[edges[e].second];
        if (std::max(ax, bx) >= minX && std::min(ax, bx) <= maxX && std::max(az, bz) >= minZ && std::min(az, bz) <= maxZ) visit(e);
      }
    };
    roads.course = [this](int e) -> std::optional<std::vector<double>> {
      const auto [a, b] = edges[e];
      return std::vector<double>{nodes[a].first, nodes[a].second, nodes[b].first, nodes[b].second};
    };
  }
  Grid(const Grid&) = delete;

  /** the way from junction (i1, j1) to the neighbouring junction (i2, j2) */
  int way(int i1, int j1, int i2, int j2) const {
    const int a = j1 * nx + i1;
    const int b = j2 * nx + i2;
    for (int e = 0; e < static_cast<int>(edges.size()); e++)
      if ((edges[e].first == a && edges[e].second == b) || (edges[e].first == b && edges[e].second == a)) return e * 2 + (edges[e].first == a ? 0 : 1);
    assert(!"no such street");
    return -1;
  }
};

/** A hand-drawn line through these points, a little off the road and wobbling. */
std::vector<double> stroke(std::initializer_list<std::pair<double, double>> list) {
  const std::vector<std::pair<double, double>> pts(list);
  std::vector<double> out;
  for (size_t k = 0; k + 1 < pts.size(); k++) {
    const auto [ax, az] = pts[k];
    const auto [bx, bz] = pts[k + 1];
    const int n = std::max(2, static_cast<int>(jsRound(std::hypot(bx - ax, bz - az) / 7)));
    for (int i = 0; i < n; i++) {
      const double t = static_cast<double>(i) / n;
      out.push_back(ax + (bx - ax) * t + std::sin(i * 1.7) * 3);
      out.push_back(az + (bz - az) * t + std::cos(i * 2.3) * 3);
    }
  }
  out.push_back(pts.back().first);
  out.push_back(pts.back().second);
  return out;
}

std::vector<double> join(std::vector<double> a, const std::vector<double>& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

/** consecutive ways are joined by a turn of the junction between them */
bool joined(const RouteGraph& g, const std::vector<int>& ways) {
  for (size_t k = 1; k < ways.size(); k++) {
    bool ok = false;
    for (uint32_t q = g.first[ways[k - 1]]; q < g.first[ways[k - 1] + 1]; q++)
      if (static_cast<int>(g.to[q]) == ways[k]) ok = true;
    if (!ok) return false;
  }
  return true;
}

bool has(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

void pathDrawTests() {
  // a line along a street is laid onto it, in the direction it was drawn
  {
    const Grid G(4, 3);
    const auto m = matchStroke(stroke({{10, 104}, {290, 97}}), G.roads);
    check(m.has_value(), "along: matched");
    if (m) {
      check(m->ways == std::vector<int>{G.way(0, 1, 1, 1), G.way(1, 1, 2, 1), G.way(2, 1, 3, 1)}, "along: ways");
      check(std::abs(m->metres - 280) < 6, "along: metres " + std::to_string(m->metres));
      // on the road itself, from where the drawing began to where it ended
      check(std::abs(m->start.x - 10) < 4 && std::abs(m->start.z - 100) < 0.01, "along: start");
      check(std::abs(m->end.x - 290) < 4 && std::abs(m->end.z - 100) < 0.01, "along: end");
      // east: yaw −90° (forward = (−sin yaw, −cos yaw))
      check(std::abs(m->start.yaw + M_PI / 2) < 0.01, "along: yaw");
    }
    // drawn the other way round: the other direction of the same streets
    const auto back = matchStroke(stroke({{290, 97}, {10, 104}}), G.roads);
    check(back && back->ways == std::vector<int>{G.way(3, 1, 2, 1), G.way(2, 1, 1, 1), G.way(1, 1, 0, 1)}, "along: back");
  }
  // a turn drawn at a junction follows the street turned into
  {
    const Grid G(4, 4);
    // east along the second row, then south down the third column
    const auto m = matchStroke(stroke({{20, 103}, {196, 98}, {203, 290}}), G.roads);
    check(m.has_value(), "turn: matched");
    if (m) {
      check(m->ways == std::vector<int>{G.way(0, 1, 1, 1), G.way(1, 1, 2, 1), G.way(2, 1, 2, 2), G.way(2, 2, 2, 3)}, "turn: ways");
      // the line runs through the corner of the junction
      bool corner = false;
      for (size_t i = 0; i + 1 < m->line.size(); i += 2)
        if (std::hypot(m->line[i] - 200, m->line[i + 1] - 100) < 0.5) corner = true;
      check(corner, "turn: corner");
    }
  }
  // a one-way street drawn against its direction is driven round, by legal turns only
  {
    const Grid probe(4, 3);
    // the street from (1,1) to (2,1) may only be driven eastwards
    const int e = probe.way(1, 1, 2, 1) >> 1;
    const Grid G(4, 3, [e](int k) { return k == e; });
    const auto m = matchStroke(stroke({{290, 102}, {10, 98}}), G.roads);
    check(m.has_value(), "one-way: matched");
    if (m) {
      check(!has(m->ways, e * 2 + 1), "not against the one-way street");
      for (const int w : m->ways) check(G.g.len[w] > 0, "every way may be driven");
      check(joined(G.g, m->ways), "one-way: turns");
      // it begins and ends where it was drawn
      check(!m->ways.empty() && m->ways.front() == G.way(3, 1, 2, 1), "one-way: first");
      check(!m->ways.empty() && m->ways.back() == G.way(1, 1, 0, 1), "one-way: last");
    }
  }
  // two strokes with a gap between them are joined by the shortest legal way
  {
    const Grid G(5, 3);
    // drawn on the first row near the west end, then again further east: the gap is closed along the row
    const auto m = matchStroke(join(stroke({{10, 2}, {90, -2}}), stroke({{310, 3}, {390, -1}})), G.roads);
    check(m && m->ways == std::vector<int>{G.way(0, 0, 1, 0), G.way(1, 0, 2, 0), G.way(2, 0, 3, 0), G.way(3, 0, 4, 0)}, "gap: along the row");
    // … and across the blocks: from the first row to the last, by streets and the turns between them
    const auto c = matchStroke(join(stroke({{10, 2}, {90, -2}}), stroke({{310, 203}, {390, 199}})), G.roads);
    check(c.has_value(), "gap: across matched");
    if (c) {
      check(c->ways.front() == G.way(0, 0, 1, 0), "gap: first");
      check(c->ways.back() == G.way(3, 2, 4, 2), "gap: last");
      check(joined(G.g, c->ways), "gap: turns");
      // (any staircase of streets from (10, 0) to (390, 200) is 380 m east and 200 m south long – no detour)
      check(c->metres > 565 && c->metres < 600, "gap: metres " + std::to_string(c->metres));
    }
  }
  // roads not to be entered are not used
  {
    const Grid probe(4, 3);
    const int blocked = probe.way(1, 1, 2, 1);
    const Grid G(4, 3, [](int) { return false; }, [blocked](int w) { return w == blocked || w == (blocked ^ 1); });
    const auto m = matchStroke(stroke({{10, 100}, {290, 100}}), G.roads);
    check(m.has_value(), "no entry: matched");
    if (m) {
      check(!has(m->ways, blocked) && !has(m->ways, blocked ^ 1), "no entry: not used");
      check(m->ways.front() == G.way(0, 1, 1, 1), "no entry: first");
      check(m->ways.back() == G.way(2, 1, 3, 1), "no entry: last");
    }
  }
  // nothing near any road: no way
  {
    const Grid G(3, 3);
    check(!matchStroke(stroke({{1000, 1000}, {1200, 1100}}), G.roads), "far: none");
    check(!matchStroke({}, G.roads), "empty: none");
  }
  // a matched way becomes a tour for the car to drive: from its first way on
  {
    const Grid G(4, 3);
    const auto m = matchStroke(stroke({{10, 100}, {290, 100}}), G.roads);
    check(m.has_value(), "preview: matched");
    if (m) {
      const TourPreview p = previewOf(*m);
      check(p.from == G.way(0, 1, 1, 1), "preview: from");
      check(p.plan.ways == std::vector<int>{G.way(1, 1, 2, 1), G.way(2, 1, 3, 1)}, "preview: ways");
      check(p.plan.metres == m->metres, "preview: metres");
      check(std::abs(p.end.x - 290) < 4 && p.end.kind == "street", "preview: end");
      check(p.line == m->line, "preview: line");
    }
  }
}

}  // namespace

int main() {
  addressPickerTests();
  geoipTests();
  placeSearchTests();
  regionsTests();
  pathDrawTests();
  if (failures) {
    std::fprintf(stderr, "maps_test: %d failed\n", failures);
    return 1;
  }
  std::printf("maps_test: ok\n");
  return 0;
}

// LINK: world i18n engine core platform geo
// (scratch – not part of the port, never committed) the game's tests that need no simulation, with the files they need,
// in one program: built once through tools/t.sh until the game library itself can be built
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "src/game/nav.cpp"
#include "src/game/tour.cpp"
#include "src/game/address.cpp"
#include "src/game/lessonplaces.cpp"
#include "src/game/routepresets.cpp"
#include "src/game/releases.cpp"
#include "src/game/netstats.cpp"
#include "src/game/clock.cpp"

#include "core/date.h"
#include "core/geom.h"
#include "core/js.h"
#include "game/address.h"
#include "game/clock.h"
#include "game/lessonplaces.h"
#include "game/nav.h"
#include "game/netstats.h"
#include "game/releases.h"
#include "game/routepresets.h"
#include "game/tour.h"
#include "i18n/index.h"
#include "platform/json.h"
#include "platform/native/native.h"
#include "platform/plat.h"
#include "tests/game_realmap.h"
#include "tests/town.h"
#include "world/lanes.h"
#include "world/netfile.h"
#include "world/routegraph.h"
#include "world/types.h"

namespace t_clock {
#define main run
#include "tests/game_clock_test.cpp"
#undef main
}  // namespace t_clock

namespace t_tour {
#define main run
#include "tests/game_tour_test.cpp"
#undef main
}  // namespace t_tour

namespace t_routepresets {
#define main run
#include "tests/game_routepresets_test.cpp"
#undef main
}  // namespace t_routepresets

namespace t_releases {
#define main run
#include "tests/game_releases_test.cpp"
#undef main
}  // namespace t_releases

namespace t_lessonplaces {
#define main run
#include "tests/game_lessonplaces_test.cpp"
#undef main
}  // namespace t_lessonplaces

namespace t_address {
#define main run
#include "tests/game_address_test.cpp"
#undef main
}  // namespace t_address

namespace t_addrcheck {
// (scratch, not part of the port) the port's address code against what the original answers (gen_expect.mjs)

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace fs;

static std::string slurp(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

int run() {
  const std::string dir = "/tmp/claude-1001/-home-user-driving-simulator/3ec80623-2fac-4dd3-a238-6a3e9eccd7e6/scratchpad/gamew/address/";
  int bad = 0;
  {
    Json list;
    assert(Json::parse(slurp(dir + "expect_norm.json"), list));
    int wrong = 0;
    for (const Json& e : list.array) {
      const std::string got = normaliseAddress(e.array[0].str);
      if (got != e.array[1].str) {
        if (wrong++ < 15) std::printf("norm \"%s\": \"%s\" != \"%s\"\n", e.array[0].str.c_str(), got.c_str(), e.array[1].str.c_str());
      }
    }
    std::printf("normalise: %zu texts, %d wrong\n", list.array.size(), wrong);
    bad += wrong;
  }
  {
    Json list;
    assert(Json::parse(slurp(dir + "expect_cmp.json"), list));
    int wrong = 0;
    for (const Json& e : list.array) {
      const int got = compareGerman(e.array[0].str, e.array[1].str);
      const int want = static_cast<int>(e.array[2].number);
      if ((got > 0) - (got < 0) != want) {
        if (wrong++ < 25) {
          std::string cps;
          for (int k = 0; k < 2; k++) {
            const std::string& s = e.array[static_cast<size_t>(k)].str;
            for (size_t i = 0; i < s.size();) {
              char buf[16];
              std::snprintf(buf, sizeof buf, "%04x ", static_cast<unsigned>(nextChar(s, i)));
              cps += buf;
            }
            cps += "| ";
          }
          std::printf("cmp \"%s\" \"%s\": %d, wanted %d   [%s]\n", e.array[0].str.c_str(), e.array[1].str.c_str(), got, want, cps.c_str());
        }
      }
    }
    std::printf("compare: %zu pairs, %d wrong\n", list.array.size(), wrong);
    bad += wrong;
  }
  {
    const std::string text = slurp(dir + "addresses.json");
    const std::unique_ptr<AddressIndex> index = AddressIndex::parse(text);
    assert(index);
    AddressJson json;
    assert(jsonParse(text, json));
    const AddressIndex second(json);
    Json want;
    assert(Json::parse(slurp(dir + "expect_search.json"), want));
    std::printf("index: %zu houses in %zu streets (the original: %.0f in %.0f)\n", index->size(), index->streetCount(), want.at("size").number, want.at("streetCount").number);
    if (static_cast<double>(index->size()) != want.at("size").number || static_cast<double>(index->streetCount()) != want.at("streetCount").number || second.size() != index->size()) bad++;
    int wrong = 0;
    for (const Json& f : want.at("found").array) {
      const Json& near = f.at("near");
      std::optional<AddressIndex::Near> n;
      if (near.isObject()) n = AddressIndex::Near{near.at("x").number, near.at("z").number};
      const size_t limit = static_cast<size_t>(f.at("limit").number);
      for (const AddressIndex* ix : {static_cast<const AddressIndex*>(index.get()), &second}) {
        const std::vector<AddressHit> hits = ix->search(f.at("q").str, limit, n);
        const std::vector<Json>& w = f.at("hits").array;
        bool same = hits.size() == w.size();
        for (size_t i = 0; same && i < hits.size(); i++)
          same = hits[i].label == w[i].at("label").str && hits[i].street == w[i].at("street").str && hits[i].x == w[i].at("x").number && hits[i].z == w[i].at("z").number && hits[i].exact == w[i].at("exact").boolean;
        if (!same) {
          if (wrong++ < 12) {
            std::printf("search \"%s\" limit %zu near %s:\n   got:", f.at("q").str.c_str(), limit, n ? "yes" : "no");
            for (const AddressHit& h : hits) std::printf(" [%s]", h.label.c_str());
            std::printf("\n  want:");
            for (const Json& h : w) std::printf(" [%s]", h.at("label").str.c_str());
            std::printf("\n");
          }
        }
      }
    }
    std::printf("search: %zu searches (each on both indexes), %d wrong\n", want.at("found").array.size(), wrong);
    bad += wrong;
  }
  std::printf(bad ? "address check: %d DIFFERENCES\n" : "address check: all the same as the original\n", bad);
  return bad ? 1 : 0;
}
}  // namespace t_addrcheck

namespace t_realdump {
// (scratch, not part of the port) the port on the real map: what dump.mts prints of the original

#include <cstdio>
#include <fstream>

#include "tests/game_realmap.h"

using namespace fs;

static std::string n6(double v) { return jsStr(jsRound(v * 1e6) / 1e6); }
static double numOf(const std::variant<std::string, double>& v) { return std::holds_alternative<double>(v) ? std::get<double>(v) : jsNumber(std::get<std::string>(v)); }
static std::string txtOf(const std::variant<std::string, double>& v) { return std::holds_alternative<double>(v) ? jsStr(std::get<double>(v)) : std::get<std::string>(v); }

int run() {
  std::vector<std::string> out;
  const std::unique_ptr<RealMap> opened = openRealMap();
  if (!opened) return 1;
  RealMap& r = *opened;
  World& world = *r.world;
  LaneGraph& graph = *r.lanes;
  const auto main = [&r](const Lane& l) { return r.usable(l); };
  const SpawnJson home = world.json.spawns[0];
  r.loadAround(home.x, home.z, 5200);
  std::optional<AddressJson> addresses = plat::native::wait(r.map->json<AddressJson>("addresses.json.gz"));
  const AddressJson& json = *addresses;

  int k = 0;
  std::vector<SpawnJson> spots;
  for (const AddressJson::Street& s : json.streets) {
    for (size_t i = 0; i + 2 < s.h.size(); i += 3) {
      const double hx = numOf(s.h[i + 1]);
      const double hz = numOf(s.h[i + 2]);
      if (jsHypot(hx - home.x, hz - home.z) > 4000 || k++ % 11 != 0) continue;
      const std::optional<SpawnJson> spot = findRoadSpot(world, graph, hx, hz, s.n, 220, main);
      out.push_back("spot " + s.n + " " + txtOf(s.h[i]) + ": " + (spot ? n6(spot->x) + " " + n6(spot->z) + " " + n6(spot->yaw) + " " + spot->name : "none"));
      if (spot && spots.size() < 400 && k % 55 == 1) spots.push_back(*spot);
    }
  }
  const double pts[4][2] = {{744, -272}, {1046, -793}, {-95, -988}, {1717, 817}};
  for (const auto& p : pts) {
    const auto a = findRoadSpot(world, graph, p[0], p[1]);
    const auto b = findRoadSpot(world, graph, p[0], p[1], "", 400, main, [](const Edge& e) { return e.width >= 6; }, 12);
    out.push_back("spot2 " + jsStr(p[0]) + " " + jsStr(p[1]) + ": " + (a ? n6(a->x) + " " + n6(a->z) + " " + n6(a->yaw) + " " + a->name : "none") + " | " +
                  (b ? n6(b->x) + " " + n6(b->z) + " " + n6(b->yaw) + " " + b->name : "none"));
  }
  {
    const std::unordered_set<int> m = mainNetwork(world, graph);
    // (the sum is taken in the order of the numbers: the original goes through its set in the order it was filled, the sum is the same)
    std::vector<int> ids(m.begin(), m.end());
    long long sum = 0;
    for (int id : ids) sum = (sum + static_cast<long long>(id) * 31) % 1000000007;
    out.push_back("mainNetwork " + std::to_string(m.size()) + " " + std::to_string(sum));
  }
  {
    std::map<std::string, int> counts;
    std::vector<std::string> order;
    for (Node* node : world.allNodes)
      for (const char* t : {"rbl", "signals", "roundabout", "priority"})
        if (junctionOfType(*node, t)) {
          if (!counts.count(t)) order.push_back(t);
          counts[t]++;
        }
    int dead = 0;
    long long sum = 0;
    for (Edge* e : world.allEdges)
      if (graph.edgeLanes[e->id] && deadEndSpur(graph, *e)) {
        dead++;
        sum = (sum + static_cast<long long>(e->id) * 7) % 1000000007;
      }
    std::string c = "{";
    for (size_t i = 0; i < order.size(); i++) c += (i ? "," : "") + std::string("\"") + order[i] + "\":" + std::to_string(counts[order[i]]);
    out.push_back("junctions " + c + "} deadEnds " + std::to_string(dead) + " " + std::to_string(sum));
  }
  PolyProjection proj;
  int done = 0;
  for (const SpawnJson& spot : spots) {
    Lane* lane = nullptr;
    double bd = JS_INF;
    world.edgesInBox(spot.x - 8, spot.z - 8, spot.x + 8, spot.z + 8, [&](Edge& e) {
      projectOnPolyline(e.pl, spot.x, spot.z, proj);
      const LaneGraph::EdgeLanes* el = graph.edgeLanes[e.id];
      for (const std::vector<Lane*>* side : {&el->fwd, &el->back})
        for (Lane* l : *side) {
          const double d = std::fabs(proj.off - l->off) + std::max(0.0, std::sqrt(proj.d2) - e.width);
          const size_t i0 = static_cast<size_t>(std::min(static_cast<int>(e.pl.cum.size()) - 2, std::max(0, proj.seg)));
          const double tx = (e.pl.pts[i0 * 3 + 3] - e.pl.pts[i0 * 3]) * l->dir;
          const double tz = (e.pl.pts[i0 * 3 + 5] - e.pl.pts[i0 * 3 + 2]) * l->dir;
          if (-std::sin(spot.yaw) * tx - std::cos(spot.yaw) * tz > 0 && d < bd) {
            bd = d;
            lane = l;
          }
        }
    });
    if (!lane) continue;
    std::string targets;
    for (const char* type : {"rbl", "signals", "roundabout", "priority", "rural", "motorway"}) {
      Edge* t = findTarget(world, graph, type, spot.x, spot.z, *lane, nullptr, {});
      targets += std::string(" ") + type + "=" + (t ? std::to_string(t->id) : "none");
      if (t && done < 40 && std::string(type) != "motorway") {
        done++;
        Navigator nav(graph);
        const bool ok = nav.setGoal(t, lane);
        std::string says;
        if (ok && nav.route) {
          NavHint h = nav.update(0.1, lane, 3);
          says += " " + h.what + "/" + n6(h.dist) + "/" + std::to_string(h.n) + "/" + std::to_string(h.arrow) + "/" + std::to_string(h.lane);
          const std::vector<Connector*> route = *nav.route;
          for (Connector* c : route) {
            h = nav.update(0.1, c->to, 2, c->to->edge->a->x, c->to->edge->a->z);
            says += " " + h.what + "/" + n6(h.dist) + "/" + std::to_string(h.n) + "/" + std::to_string(h.arrow) + "/" + std::to_string(h.lane) + "/" + (h.done ? "1" : "0");
          }
        }
        out.push_back("nav " + std::to_string(lane->id) + "->" + std::to_string(t->id) + " " + (ok ? "true" : "false") + " left " + std::to_string(nav.left()) + says);
      }
    }
    out.push_back("targets " + std::to_string(lane->id) + targets);
  }
  {
    double seed = 11;
    const auto rnd = [&seed] {
      seed = std::fmod(seed * 16807, 2147483647);
      return seed / 2147483647;
    };
    const double centres[2][2] = {{744, -272}, {4265, -5927}};
    for (const auto& c : centres) {
      r.loadAround(c[0], c[1], 3100);
      for (const char* kind : {"quiet", "rbl", "priority", "signals", "roundabout", "rural", "motorway"}) {
        PlaceOptions o;
        o.rnd = rnd;
        o.usable = main;
        const auto at = findLessonPlace(world, graph, kind, c[0], c[1], o);
        out.push_back(std::string("lesson ") + kind + " " + jsStr(c[0]) + " " + jsStr(c[1]) + ": " +
                      (at ? n6(at->x) + " " + n6(at->z) + " " + n6(at->yaw) + " " + at->name + " goal " + (at->goal ? std::to_string(at->goal->id) : "none") + " node " + (at->node ? std::to_string(at->node->id) : "none") : "none"));
      }
    }
  }
  if (r.index) {
    const RouteGraph& g = r.index->graph;
    const std::vector<uint8_t>& mainWays = r.index->main;
    const auto sumOf = [](const std::vector<int>& ways) {
      long long sum = 0;
      for (int w : ways) sum = (sum * 31 + w) % 1000000007;
      return std::to_string(sum);
    };
    const auto list = [](const std::vector<int>& v) {
      std::string s;
      for (size_t i = 0; i < v.size(); i++) s += (i ? "," : "") + std::to_string(v[i]);
      return s;
    };
    for (const RoutePreset& p : ROUTE_PRESETS) {
      const int start = snapPoint(g, p.points[0], mainWays)[0];
      const auto plan = planPreset(g, start, p, mainWays);
      const std::vector<float> line = plan ? routeLine(g, start, plan->ways) : std::vector<float>();
      out.push_back("preset " + p.id + " start " + std::to_string(start) + " " +
                    (plan ? std::to_string(plan->ways.size()) + " " + n6(plan->metres) + " at " + list(plan->at) + " sum " + sumOf(plan->ways) + " line " + std::to_string(line.size()) + " " + n6(line[line.size() - 2]) : "none"));
      for (const RoutePoint& pt : p.points) out.push_back("snap " + p.id + " " + list(snapPoint(g, pt, mainWays)));
    }
    const double tours[5][4] = {{744, -272, 3000, 1}, {744, -272, 8000, 2}, {4265, -5927, 5000, 3}, {1046, -793, 12000, 4}, {-1160, 554, 6000, 5}};
    for (const auto& t : tours) {
      const int start = snapPoint(g, {.x = t[0], .z = t[1]}, mainWays)[0];
      const int goal = snapPoint(g, {.x = t[0] + 900, .z = t[1] + 400}, mainWays)[0];
      for (int end : {start, goal}) {
        const auto plan = planTour(g, start, end, t[2], {.budgetMs = 0, .seed = t[3]});
        out.push_back("tour " + std::to_string(start) + "->" + std::to_string(end) + " " + jsStr(t[2]) + " seed " + jsStr(t[3]) + ": " +
                      (plan ? std::to_string(plan->ways.size()) + " " + n6(plan->metres) + " " + n6(plan->fresh) + " " + n6(plan->again) + " " + n6(plan->valued) + " sum " + sumOf(plan->ways) : "none"));
      }
      const auto wished = planTour(g, start, start, t[2],
                                   {.known = [](int e) { return e % 7 == 0; }, .budgetMs = 0, .seed = t[3], .avoid = [&g](int w) { return g.cls[static_cast<size_t>(w)] <= 5; }, .like = [&g](int e) { return g.cls[static_cast<size_t>(e) * 2] >= 10 ? 2.0 : 0.5; }});
      out.push_back("tourWished " + std::to_string(start) + " " + jsStr(t[2]) + " seed " + jsStr(t[3]) + ": " +
                    (wished ? std::to_string(wished->ways.size()) + " " + n6(wished->metres) + " " + n6(wished->fresh) + " " + n6(wished->valued) + " sum " + sumOf(wished->ways) : "none"));
    }
    r.loadAround(home.x, home.z, 5200);
    const int a = snapPoint(g, {.x = 744, .z = -272}, mainWays)[0];
    const double ends[4][2] = {{1046, -793}, {-95, -988}, {1717, 817}, {-1160, 554}};
    for (const auto& e : ends) {
      const int b = snapPoint(g, {.x = e[0], .z = e[1]}, mainWays)[0];
      const auto ways = quickest(g, a, b);
      const LaneGraph::EdgeLanes* el = graph.edgeLanes[a >> 1];
      Lane* first = el ? ((a & 1) == 0 ? el->fwd : el->back)[0] : nullptr;
      const auto path = ways && first ? connectorsAlong(graph, *ways, *first) : std::nullopt;
      long long sum = 0;
      if (path)
        for (Connector* c : *path) sum = (sum * 31 + c->to->id) % 1000000007;
      out.push_back("along " + std::to_string(a) + "->" + std::to_string(b) + ": ways " + (ways ? std::to_string(ways->size()) : "none") + " path " + (path ? std::to_string(path->size()) + " " + std::to_string(sum) : "none"));
      if (ways && first) {
        Navigator nav(graph);
        nav.net = &g;
        nav.setWays(*ways, *first, false);
        std::string says;
        Lane* lane = first;
        for (int step = 0; step < 400 && nav.route; step++) {
          const NavHint h = nav.update(0.1, lane, 1);
          says += " " + h.what + "/" + n6(h.dist) + "/" + std::to_string(h.n) + "/" + std::to_string(h.arrow) + "/" + std::to_string(h.lane);
          if (h.done) break;
          Connector* c = nullptr;
          for (Connector* q : *nav.route)
            if (q->from == lane || (q->from->edge == lane->edge && q->from->dir == lane->dir)) {
              c = q;
              break;
            }
          if (!c) break;
          lane = c->to;
        }
        out.push_back("navWays " + std::to_string(a) + "->" + std::to_string(b) + ": left " + std::to_string(nav.left()) + " lanes " + std::to_string(nav.routeLanes->size()) + says);
      }
    }
  }
  std::ofstream f("/tmp/claude-1001/-home-user-driving-simulator/3ec80623-2fac-4dd3-a238-6a3e9eccd7e6/scratchpad/gamew/real/cpp.txt");
  for (const std::string& l : out) f << l << "\n";
  std::printf("%zu lines\n", out.size());
  return 0;
}
}  // namespace t_realdump

int main(int argc, char** argv) {
  const std::string which = argc > 1 ? argv[1] : "";
  if (which == "clock") return t_clock::run();
  if (which == "tour") return t_tour::run();
  if (which == "routepresets") return t_routepresets::run();
  if (which == "releases") return t_releases::run();
  if (which == "lessonplaces") return t_lessonplaces::run();
  if (which == "address") return t_address::run();
  if (which == "addrcheck") return t_addrcheck::run();
  if (which == "realdump") return t_realdump::run();
  std::printf("built\n");
  return 0;
}

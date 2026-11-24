// SPDX-License-Identifier: AGPL-3.0-only
// LINK: world core platform geo
/**
 * The lanes and the paths through the junctions (src/world/lanes.h), and the
 * network as the route planner sees it (src/world/routegraph.h), on a small
 * town against the original: tools/diff/world_town.ts --part lanes prints what
 * src/world/lanes.ts and routegraph.ts @ v1.7.7 make of the same town
 * (tests/fixtures/lanes-town.txt); this prints the same lines from the port
 * and compares them – every lane, every path with its conflicts and siblings,
 * routes (also for a long vehicle and with a road closed), what is near a
 * point, walls, posts, the ways of the route planner.
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "tests/dump.h"
#include "tests/town.h"
#include "world/lanes.h"
#include "world/routegraph.h"
#include "world/world.h"

using namespace fs;
using fs::dump::lineSum;
using fs::dump::out;
using fs::dump::sumOf;

namespace {

TownOptions townOptions() {
  TownOptions o;
  o.cols = 4;
  o.rows = 3;
  o.pavement = 2;
  o.deadEnd = 60;
  o.wide = {{1, 1, 2, 1}};
  o.oneWay = {{0, 0, 1, 0}};
  o.lights = {{1, 1}, {2, 1}};
  o.crossings = {{{2, 0, 3, 0}, 0.5, XING_ZEBRA}, {{1, 2, 2, 2}, 0.4, XING_SIGNAL}};
  o.bans = {{0, 1, 1, 1, 1, 0}};
  return o;
}

template <class T>
std::string idsOf(const std::vector<T*>& list) {
  std::string s;
  for (const T* v : list) s += (s.empty() ? "" : ",") + std::to_string(v->id);
  return s.empty() ? "-" : s;
}

template <class T>
std::vector<int> idList(const std::vector<T*>& list) {
  std::vector<int> ids;
  for (const T* v : list) ids.push_back(v->id);
  return ids;
}

}  // namespace

int main() {
  Town town = makeTown(townOptions());
  World& world = *town.world;
  LaneGraph& lanes = *town.lanes;
  out("graph", lanes.lanes.size(), lanes.connectors.size(), lanes.edgeLanes.size());
  PolySample tmp = newPolySample();
  for (const Lane* lp : lanes.lanes) {
    const Lane& l = *lp;
    lanePoint(l, l.len * 0.3, tmp);
    out("lane", l.id, l.edge->id, l.dir, l.idx, l.count, l.off, l.width, l.s0, l.s1, l.len, l.from->id, l.to->id, l.armTo->edge->id, l.armFrom->edge->id, l.limit, l.stopAt, l.out.size(), l.in.size(), tmp.x, tmp.y, tmp.z,
        tmp.tx, tmp.tz);
  }
  for (const Connector* c : lanes.connectors) out("path", c->id, c->node->id, c->from->id, c->to->id, c->turn, c->len, c->pl.cum.size(), lineSum(c->pl.pts));
  for (const Connector* c : lanes.connectors) {
    for (const Conflict& k : c->conflicts) out("conflict", c->id, k.other->id, k.sSelf, k.sOther, k.merge, k.yield);
    for (const Sibling& s : c->siblings) out("sibling", c->id, s.other->id, s.until, s.untilOther);
  }
  for (const Node* n : world.nodes) out("through", n->id, idList(lanes.pathsThrough(*n)));
  for (const Node* n : world.nodes)
    for (const Arm* a : n->arms) out("into", n->id, a->idx, idsOf(lanes.lanesInto(*a)), idsOf(lanes.lanesOutOf(*a)));
  // ways through the lanes
  auto lane = [&](int i0, int j0, int i1, int j1, bool back = false) -> Lane* {
    const LaneGraph::EdgeLanes* el = lanes.edgeLanes[town.street(i0, j0, i1, j1)];
    return back ? el->back[0] : el->fwd[0];
  };
  struct Route {
    Lane* start;
    int goal;
    bool forTraffic;
    int goalDir;
  };
  const std::vector<Route> routes = {
      {lane(0, 0, 1, 0), town.street(2, 2, 3, 2), false, 0}, {lane(0, 0, 1, 0), town.street(2, 2, 3, 2), true, 0},   {lane(0, 1, 1, 1), town.street(1, 0, 1, 1), false, 0},
      {lane(0, 1, 1, 1), town.street(1, 0, 1, 1), false, 1}, {lane(0, 1, 1, 1), town.street(1, 0, 1, 1), false, -1}, {lane(2, 1, 3, 1, true), town.street(0, 0, 1, 0), false, 0},
      {lane(3, 0, 3, 1), town.street(3, 0, 3, 1), false, 0}, {lane(0, 0, 0, 1), static_cast<int>(world.edges.size()) - 1, false, 0},
  };
  for (const Route& q : routes) {
    const std::vector<Connector*> r = lanes.route(*q.start, *world.edges[q.goal], q.forTraffic, q.goalDir);
    out("route", q.start->id, q.goal, q.forTraffic, q.goalDir, r.empty() ? -1 : static_cast<int>(r.size()), idList(r));
    // (each step begins on the lane the one before ended on, and the last ends on the road asked for)
    for (size_t i = 1; i < r.size(); i++) assert(r[i]->from == r[i - 1]->to);
    if (!r.empty()) assert(r.front()->from == q.start && r.back()->to->edge == world.edges[q.goal]);
  }
  lanes.longVehicle = true;
  lanes.wayCost = [&](int way) -> double { return (way >> 1) == town.street(1, 0, 2, 0) ? JS_INF : (way >> 1) == town.street(1, 1, 2, 1) ? 500 : 0; };
  for (size_t i = 0; i < 3; i++) {
    const Route& q = routes[i];
    const std::vector<Connector*> r = lanes.route(*q.start, *world.edges[q.goal], q.forTraffic, q.goalDir);
    out("routeCost", r.empty() ? -1 : static_cast<int>(r.size()), idList(r));
  }
  lanes.longVehicle = false;
  lanes.wayCost = nullptr;
  // what is near a point: the middle of a lane, a path through a junction; walls; posts
  PathFoot foot;
  const double nears[][4] = {{50, 1, 100, 3}, {50, 1, 105, 3}, {50, 1, JS_NAN, 3}, {50, 6, 100, 3}, {100, 100, 100, 4}, {103, 97.5, 100, 2}, {150, 101.6, 100, 2}, {360, 0.4, 100, 3}, {50, 50, 100, 10}};
  for (const auto& q : nears) {
    const double d = lanes.nearestPath(q[0], q[1], q[2], q[3], foot);
    out("nearpath", d, std::isfinite(d) ? foot.x : 0.0, std::isfinite(d) ? foot.z : 0.0);
  }
  out("walls", lanes.wallInLane(40, -6, 60, -6, 100, 0.8), lanes.wallInLane(40, -6, 60, -1.5, 100, 0.8), lanes.wallInLane(40, -6, 60, -1.5, 110, 0.8), lanes.wallInLane(40, -6, 60, -1.5, JS_NAN, 0.8),
      lanes.wallInLane(96, 96, 104, 104, 100, 0.5), lanes.wallInLane(120, 120, 130, 130, 100, 2));
  const double posts[][3] = {{50, 1.3, 1.4}, {50, 0, 1.4}, {50, 7, 1.4}, {100, 100, 1.4}, {150, 100, 6}};
  for (const auto& q : posts) {
    const PathFoot* post = lanes.besideLanes(q[0], q[1], 100, q[2], foot);
    assert(post == nullptr || post == &foot);
    out("beside", post != nullptr, post ? foot.x : 0.0, post ? foot.z : 0.0, post ? foot.d : 0.0);
  }
  out("drivable", LaneGraph::drivable(*world.edges[0], true), LaneGraph::drivable(*world.edges[0], false));
  // the same network as the route planner sees it
  const RouteGraph g = routeGraphOf(lanes);
  out("rg", g.n, g.to.size(), sumOf(g.len), sumOf(g.secs), sumOf(g.kind), sumOf(g.cls), sumOf(g.x0), sumOf(g.z0), sumOf(g.x1), sumOf(g.z1), sumOf(g.first), sumOf(g.to), sumOf(g.turn), sumOf(g.across), sumOf(g.firstIn),
      sumOf(g.from), sumOf(g.inTurn));
  {
    const std::vector<uint8_t> main = mainWays(g);
    out("main", sumOf(main), std::vector<int>(main.begin(), main.end()));
  }
  auto way = [&](int i0, int j0, int i1, int j1, bool back = false) { return wayOf(*lane(i0, j0, i1, j1, back)); };
  const std::vector<std::pair<int, int>> ways = {
      {way(0, 0, 1, 0), way(2, 2, 3, 2)}, {way(0, 1, 1, 1), way(1, 0, 1, 1)},     {way(2, 1, 3, 1, true), way(0, 0, 1, 0)},
      {way(0, 0, 0, 1), way(0, 0, 0, 1)}, {way(0, 0, 0, 1), way(0, 0, 1, 0) + 1}, {way(3, 0, 3, 1), (static_cast<int>(world.edges.size()) - 1) * 2},
  };
  for (const auto& [a, b] : ways) {
    const std::optional<std::vector<int>> r = quickest(g, a, b);
    out("quickest", a, b, r ? static_cast<int>(r->size()) : -1, r ? *r : std::vector<int>());
    const std::optional<std::vector<int>> e = quickestToEdge(g, a, b >> 1);
    out("toEdge", e ? static_cast<int>(e->size()) : -1, e ? *e : std::vector<int>());
    // (the seconds a way takes are those of its roads, its turns and the junctions it crosses)
    const std::optional<TimedRoute> timed = quickestTimed(g, a, b);
    assert(static_cast<bool>(timed) == static_cast<bool>(r) && (!timed || (timed->ways == *r && timed->secs >= 0)));
  }
  routeFor.wayCost = [&](int w) -> double { return (w >> 1) == town.street(1, 0, 2, 0) ? JS_INF : 0; };
  for (size_t i = 0; i < 2; i++) {
    const std::optional<std::vector<int>> r = quickest(g, ways[i].first, ways[i].second);
    out("quickestCost", r ? static_cast<int>(r->size()) : -1, r ? *r : std::vector<int>());
  }
  routeFor.wayCost = nullptr;

  // --- beside what the original prints
  // the conflicts of a path are worked out when they are first looked at: junction by junction
  {
    Town fresh = makeTown(townOptions());
    Connector* c = fresh.lanes->connectors[0];
    assert(c->foundConflicts.empty() && c->foundSiblings.empty());
    size_t n = 0;
    for (const Conflict& k : c->conflicts) n += k.other != nullptr;
    assert(n == c->foundConflicts.size() && c->conflicts.size() == n && (n == 0) == c->conflicts.empty());
    // (its junction is worked out now – the others are not)
    for (const Connector* other : fresh.lanes->connectors) {
      if (other->node == c->node) continue;
      assert(other->foundConflicts.empty() && other->foundSiblings.empty());
    }
    // what each side knows of a conflict is the other's the other way round
    fresh.lanes->resolveAll();
    for (const Connector* a : fresh.lanes->connectors) {
      for (const Conflict& k : a->conflicts) {
        bool mirrored = false;
        for (const Conflict& back : k.other->conflicts) mirrored = mirrored || (back.other == a && back.sSelf == k.sOther && back.sOther == k.sSelf && back.merge == k.merge && !(back.yield && k.yield));
        assert(mirrored);
      }
    }
  }
  // lanes and paths point at each other
  for (const Lane* l : lanes.lanes) {
    for (const Connector* c : l->out) assert(c->from == l && c->node == l->to);
    for (const Connector* c : l->in) assert(c->to == l && c->node == l->from);
    assert(lanes.lanes[l->id] == l);
  }

  const int bad = dump::against("tests/fixtures/lanes-town.txt");
  if (bad) {
    std::fprintf(stderr, "lanes: %d lines differ from the original's\n", bad);
    return 1;
  }
  std::printf("lanes: all checks passed (%zu lines the same as the original's)\n", dump::lines().size());
  return 0;
}

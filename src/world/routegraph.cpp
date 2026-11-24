// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/routegraph.ts @ v1.7.7
/** The road network as a route planner sees it (see routegraph.h). */
#include "world/routegraph.h"

#include <cmath>
#include <stdexcept>
#include <unordered_map>

#include "core/js.h"
#include "world/lanes.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

RouteFor routeFor;

int wayOf(const Lane& lane) { return lane.edge->id * 2 + (lane.dir > 0 ? 0 : 1); }

namespace {

/** the turns by where they lead */
void turnsByWhereTheyLead(RouteGraph& g) {
  const size_t n = static_cast<size_t>(g.n);
  std::vector<uint32_t> count(n + 1, 0);
  for (size_t k = 0; k < g.to.size(); k++) count[g.to[k] + 1]++;
  for (size_t w = 0; w < n; w++) count[w + 1] += count[w];
  g.firstIn = count;
  g.from.assign(g.to.size(), 0);
  g.inTurn.assign(g.to.size(), 0);
  std::vector<uint32_t> fill(count.begin(), count.begin() + static_cast<long>(n));
  for (size_t w = 0; w < n; w++) {
    for (uint32_t k = g.first[w]; k < g.first[w + 1]; k++) {
      const uint32_t at = fill[g.to[k]]++;
      g.from[at] = static_cast<uint32_t>(w);
      g.inTurn[at] = k;
    }
  }
}

/** What a turn costs on top of the time it takes: seconds. */
double turnSeconds(const RouteGraph& g, int w, uint32_t k) {
  const int t = g.turn[k];
  if (t == TURN_LEFT) return 6;
  if (t == TURN_RIGHT) return 3;
  // turning round is for when there is no other way – least of all where the map ends
  if (t == TURN_UTURN) return g.kind[static_cast<size_t>(w)] & RG_TO_MAP_END ? 900 : 150;
  return 0;
}

}  // namespace

RouteGraph routeGraphOf(const LaneGraph& lanes) {
  const Sparse<Edge>& edges = lanes.world.edges;
  const size_t n = edges.size() * 2;
  RouteGraph g;
  g.n = static_cast<int>(n);
  g.len.assign(n, 0);
  g.secs.assign(n, 0);
  g.kind.assign(n, 0);
  g.cls.assign(n, 0);
  g.x0.assign(n, 0);
  g.z0.assign(n, 0);
  g.x1.assign(n, 0);
  g.z1.assign(n, 0);
  g.first.assign(n + 1, 0);
  std::vector<uint32_t>& to = g.to;
  std::vector<uint8_t>& turn = g.turn;
  std::vector<float>& across = g.across;
  for (const Edge* edge : edges) {
    // (a network with gaps is not what this is for: the original fails at the first one)
    if (!edge) throw std::runtime_error("routeGraphOf: the road network has gaps (it is loaded in pieces)");
    const Edge& e = *edge;
    const LaneGraph::EdgeLanes* el = lanes.edgeLanes[e.id];
    if (!el) throw std::runtime_error("routeGraphOf: a road without lanes");
    for (int d = 0; d < 2; d++) {
      const size_t w = static_cast<size_t>(e.id) * 2 + static_cast<size_t>(d);
      const std::vector<Lane*>& list = d == 0 ? el->fwd : el->back;
      g.first[w] = static_cast<uint32_t>(to.size());
      g.cls[w] = static_cast<uint8_t>(e.cls);
      const Node& a = d == 0 ? *e.a : *e.b;
      const Node& b = d == 0 ? *e.b : *e.a;
      g.x0[w] = static_cast<float>(a.x);
      g.z0[w] = static_cast<float>(a.z);
      g.x1[w] = static_cast<float>(b.x);
      g.z1[w] = static_cast<float>(b.z);
      if (list.empty()) continue;
      g.len[w] = static_cast<float>(jsMax(0.5, e.len));
      const double limit = jsMin(std::isfinite(list[0]->limit) ? list[0]->limit : 36, 36);
      g.secs[w] = static_cast<float>(static_cast<double>(g.len[w]) / jsMax(limit, 2));
      int kind = 0;
      if (e.access == ACCESS_PRIVATE || e.access == ACCESS_AGRICULTURAL) kind |= RG_NO_ENTRY;
      if (e.access == ACCESS_DESTINATION || (e.flags & EF_MINOR_SERVICE) != 0) kind |= RG_ACCESS_ONLY;
      if (e.tier < 2) kind |= RG_MINOR;
      if (e.laneW < 2.4) kind |= RG_NARROW;
      if (b.ctrl == CTRL_MAP_END) kind |= RG_TO_MAP_END;
      if (e.flags & EF_URBAN) kind |= RG_URBAN;
      if (e.flags & EF_ROUNDABOUT) kind |= RG_ROUNDABOUT;
      g.kind[w] = static_cast<uint8_t>(kind);
      // the turns of all its lanes: each way turned into once, by the gentlest of the paths there
      std::unordered_map<int, size_t> seen;
      for (const Lane* lane : list) {
        for (const Connector* c : lane->out) {
          const int t = wayOf(*c->to);
          auto at = seen.find(t);
          if (at == seen.end()) {
            seen[t] = to.size();
            to.push_back(static_cast<uint32_t>(t));
            turn.push_back(static_cast<uint8_t>(c->turn));
            across.push_back(static_cast<float>(c->len));
          } else if (c->turn == TURN_STRAIGHT && turn[at->second] != TURN_STRAIGHT) {
            turn[at->second] = static_cast<uint8_t>(c->turn);
          }
        }
      }
    }
  }
  g.first[n] = static_cast<uint32_t>(to.size());
  turnsByWhereTheyLead(g);
  return g;
}

std::optional<std::vector<int>> quickest(const RouteGraph& g, int start, int goal) {
  std::optional<TimedRoute> r = quickestTimed(g, start, goal);
  if (!r) return std::nullopt;
  return std::move(r->ways);
}

std::optional<std::vector<int>> quickestToEdge(const RouteGraph& g, int start, int edge) {
  const size_t w = static_cast<size_t>(edge) * 2;
  std::optional<TimedRoute> a = g.len[w] > 0 ? quickestTimed(g, start, edge * 2) : std::nullopt;
  std::optional<TimedRoute> b = g.len[w + 1] > 0 ? quickestTimed(g, start, edge * 2 + 1) : std::nullopt;
  if (a && b) return a->secs <= b->secs ? std::move(a->ways) : std::move(b->ways);
  if (a) return std::move(a->ways);
  if (b) return std::move(b->ways);
  return std::nullopt;
}

std::optional<TimedRoute> quickestTimed(const RouteGraph& g, int start, int goal) {
  if (start == goal) return TimedRoute{};
  const size_t n = static_cast<size_t>(g.n);
  std::vector<double> dist(n, JS_INF);
  std::vector<int32_t> prev(n, -1);
  MinHeap heap;
  // straight-line time at motorway speed never overestimates
  // (Math.sqrt rather than Math.hypot: the same to the last bit as the WebAssembly – src/wasm/kernels.cpp)
  const double gx = g.x1[static_cast<size_t>(goal)];
  const double gz = g.z1[static_cast<size_t>(goal)];
  auto rest = [&](uint32_t w) -> double {
    const double dx = static_cast<double>(g.x1[w]) - gx;
    const double dz = static_cast<double>(g.z1[w]) - gz;
    return std::sqrt(dx * dx + dz * dz) / 36;
  };
  dist[static_cast<size_t>(start)] = 0;
  heap.push(static_cast<uint32_t>(start), rest(static_cast<uint32_t>(start)));
  while (heap.size > 0) {
    const double key = heap.topKey();
    const uint32_t w = heap.pop();
    if (static_cast<int>(w) == goal) break;
    const double d = dist[w];
    if (key > d + rest(w) + 1e-6) continue;
    for (uint32_t k = g.first[w]; k < g.first[w + 1]; k++) {
      const uint32_t t = g.to[k];
      const bool isGoal = static_cast<int>(t) == goal;
      if (!isGoal && (g.kind[t] & RG_NO_ENTRY) != 0) continue;
      if (routeFor.noMotorway && !isGoal && g.cls[t] <= MOTORWAY_LINK) continue;
      const double extra = routeFor.wayCost ? routeFor.wayCost(static_cast<int>(t)) : 0;
      if (extra == JS_INF && !isGoal) continue;
      const double through = !isGoal && (g.kind[t] & RG_ACCESS_ONLY) != 0 ? 240 : 0;
      const double nd = d + static_cast<double>(g.across[k]) / 6 + static_cast<double>(g.secs[t]) + turnSeconds(g, static_cast<int>(w), k) + through + (extra == JS_INF ? 0 : extra);
      if (nd < dist[t]) {
        dist[t] = nd;
        prev[t] = static_cast<int32_t>(w);
        heap.push(t, nd + rest(t));
      }
    }
  }
  if (!std::isfinite(dist[static_cast<size_t>(goal)])) return std::nullopt;
  TimedRoute route;
  for (int w = goal; w != start; w = prev[static_cast<size_t>(w)]) {
    if (w < 0) return std::nullopt;
    route.ways.push_back(w);
  }
  std::vector<int> ways(route.ways.rbegin(), route.ways.rend());
  route.ways = std::move(ways);
  route.secs = dist[static_cast<size_t>(goal)];
  return route;
}

std::vector<uint8_t> mainWays(const RouteGraph& g) {
  const size_t n = static_cast<size_t>(g.n);
  // from the longest of the bigger roads that can be driven both ways
  long seed = -1;
  for (size_t w = 0; w + 1 < n; w += 2) {
    if (g.cls[w] < 4 || g.cls[w] > 8 || g.len[w] == 0 || g.len[w + 1] == 0) continue;
    if (seed < 0 || g.len[w] > g.len[static_cast<size_t>(seed)]) seed = static_cast<long>(w);
  }
  std::vector<uint8_t> out(n, 0);
  if (seed < 0) return out;
  std::vector<uint8_t> there(n, 0);
  std::vector<uint8_t> back(n, 0);
  std::vector<uint32_t> stack = {static_cast<uint32_t>(seed)};
  there[static_cast<size_t>(seed)] = 1;
  while (!stack.empty()) {
    const uint32_t w = stack.back();
    stack.pop_back();
    for (uint32_t k = g.first[w]; k < g.first[w + 1]; k++) {
      const uint32_t v = g.to[k];
      if (!there[v] && (g.kind[v] & RG_NO_ENTRY) == 0) {
        there[v] = 1;
        stack.push_back(v);
      }
    }
  }
  stack.push_back(static_cast<uint32_t>(seed));
  back[static_cast<size_t>(seed)] = 1;
  while (!stack.empty()) {
    const uint32_t w = stack.back();
    stack.pop_back();
    for (uint32_t p = g.firstIn[w]; p < g.firstIn[w + 1]; p++) {
      const uint32_t v = g.from[p];
      if (!back[v] && (g.kind[v] & RG_NO_ENTRY) == 0) {
        back[v] = 1;
        stack.push_back(v);
      }
    }
  }
  for (size_t w = 0; w < n; w++) out[w] = there[w] & back[w];
  return out;
}

}  // namespace fs

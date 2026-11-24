// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The road network as a route planner sees it (see routegraph.h).
 *
 * Origin: src/world/routegraph.ts (MaurerAnton/driving-simulator @ 3420ad0,
 * v0.8.0).
 */
#include "world/routegraph.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "world/lanes.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

int wayOfLane(const Lane& lane) { return lane.edge->id * 2 + (lane.dir > 0 ? 0 : 1); }

RouteGraph routeGraphOf(const LaneGraph& lanes) {
  World& world = const_cast<World&>(lanes.world);
  const auto& edges = world.edges();
  const int nE = static_cast<int>(edges.size());
  RouteGraph g;
  g.n = nE * 2;
  const size_t n = static_cast<size_t>(g.n);
  g.len.assign(n, 0);
  g.secs.assign(n, 0);
  g.kind.assign(n, 0);
  g.cls.assign(n, 0);
  g.x0.assign(n, 0);
  g.z0.assign(n, 0);
  g.x1.assign(n, 0);
  g.z1.assign(n, 0);
  g.first.assign(n + 1, 0);
  g.firstIn.assign(n + 1, 0);
  std::vector<uint32_t> to;
  std::vector<uint8_t> turn;
  std::vector<double> across;
  for (const auto& e : edges) {
    if (e.id < 0 || static_cast<size_t>(e.id) >= lanes.edgeLanes.size()) continue;
    const LaneGraph::EdgeLanes& el = lanes.edgeLanes[static_cast<size_t>(e.id)];
    for (int d = 0; d < 2; d++) {
      const size_t w = static_cast<size_t>(e.id) * 2 + d;
      const std::vector<Lane*>& list = d == 0 ? el.fwd : el.back;
      g.first[w] = static_cast<uint32_t>(to.size());
      g.cls[w] = static_cast<uint8_t>(e.cls);
      const Node* a = d == 0 ? e.a : e.b;
      const Node* b = d == 0 ? e.b : e.a;
      g.x0[w] = a->x;
      g.z0[w] = a->z;
      g.x1[w] = b->x;
      g.z1[w] = b->z;
      if (list.empty()) continue;
      g.len[w] = std::max(0.5, e.len);
      const double limit =
          std::min(std::isfinite(list[0]->limit) ? list[0]->limit : 36.0, 36.0);
      g.secs[w] = g.len[w] / std::max(limit, 2.0);
      int kind = 0;
      if (e.access == ACCESS_PRIVATE || e.access == ACCESS_AGRICULTURAL) kind |= RG_NO_ENTRY;
      if (e.access == ACCESS_DESTINATION || (e.flags & EF_MINOR_SERVICE) != 0) kind |= RG_ACCESS_ONLY;
      if (e.tier < 2) kind |= RG_MINOR;
      if (e.laneW < 2.4) kind |= RG_NARROW;
      if (b->ctrl == CTRL_MAP_END) kind |= RG_TO_MAP_END;
      if (e.flags & EF_URBAN) kind |= RG_URBAN;
      if (e.flags & EF_ROUNDABOUT) kind |= RG_ROUNDABOUT;
      g.kind[w] = static_cast<uint8_t>(kind);
      // the turns of all its lanes: each way turned into once, by the gentlest of the paths there
      std::map<int, size_t> seen;
      for (const Lane* lane : list) {
        for (const Connector* c : lane->out) {
          const int t = wayOfLane(*c->to);
          auto at = seen.find(t);
          if (at == seen.end()) {
            seen[t] = to.size();
            to.push_back(static_cast<uint32_t>(t));
            turn.push_back(static_cast<uint8_t>(c->turn));
            across.push_back(c->len);
          } else if (c->turn == TURN_STRAIGHT && turn[at->second] != TURN_STRAIGHT)
            turn[at->second] = static_cast<uint8_t>(c->turn);
        }
      }
    }
  }
  g.first[n] = static_cast<uint32_t>(to.size());
  g.to = std::move(to);
  g.turn = std::move(turn);
  g.across = std::move(across);
  // the turns by where they lead
  std::vector<uint32_t> count(n + 1, 0);
  for (uint32_t t : g.to)
    if (static_cast<size_t>(t) + 1 < count.size()) count[static_cast<size_t>(t) + 1]++;
  for (size_t w = 0; w < n; w++) count[w + 1] += count[w];
  g.firstIn = count;
  g.from.assign(g.to.size(), 0);
  g.inTurn.assign(g.to.size(), 0);
  std::vector<uint32_t> fill(count.begin(), count.begin() + static_cast<long>(n));
  for (size_t w = 0; w < n; w++) {
    for (uint32_t k = g.first[w]; k < g.first[w + 1]; k++) {
      const size_t t = g.to[k];
      const size_t at = fill[t]++;
      g.from[at] = static_cast<uint32_t>(w);
      g.inTurn[at] = k;
    }
  }
  return g;
}

double turnSeconds(const RouteGraph& g, int w, int k) {
  const int t = g.turn[static_cast<size_t>(k)];
  if (t == TURN_LEFT) return 6;
  if (t == TURN_RIGHT) return 3;
  // turning round is for when there is no other way – least of all where the map ends
  if (t == TURN_UTURN) return (g.kind[static_cast<size_t>(w)] & RG_TO_MAP_END) ? 900 : 150;
  return 0;
}

bool quickestTimed(const RouteGraph& g, int start, int goal, std::vector<int>& ways, double& secs) {
  ways.clear();
  secs = 0;
  if (start == goal) return true;
  const size_t n = static_cast<size_t>(g.n);
  std::vector<double> dist(n, std::numeric_limits<double>::infinity());
  std::vector<int> prev(n, -1);
  MinHeap heap;
  // straight-line time at motorway speed never overestimates
  const double gx = g.x1[static_cast<size_t>(goal)];
  const double gz = g.z1[static_cast<size_t>(goal)];
  auto rest = [&](int w) {
    return std::hypot(g.x1[static_cast<size_t>(w)] - gx, g.z1[static_cast<size_t>(w)] - gz) / 36.0;
  };
  dist[static_cast<size_t>(start)] = 0;
  heap.push(start, rest(start));
  while (heap.size() > 0) {
    const double key = heap.topKey();
    const int w = heap.pop();
    if (w == goal) break;
    const double d = dist[static_cast<size_t>(w)];
    if (key > d + rest(w) + 1e-6) continue;
    for (uint32_t k = g.first[static_cast<size_t>(w)]; k < g.first[static_cast<size_t>(w) + 1];
         k++) {
      const int t = static_cast<int>(g.to[k]);
      if (t != goal && (g.kind[static_cast<size_t>(t)] & RG_NO_ENTRY) != 0) continue;
      const double through =
          (t != goal && (g.kind[static_cast<size_t>(t)] & RG_ACCESS_ONLY) != 0) ? 240.0 : 0.0;
      const double nd = d + g.across[k] / 6 + g.secs[static_cast<size_t>(t)] +
                        turnSeconds(g, w, static_cast<int>(k)) + through;
      if (nd < dist[static_cast<size_t>(t)]) {
        dist[static_cast<size_t>(t)] = nd;
        prev[static_cast<size_t>(t)] = w;
        heap.push(t, nd + rest(t));
      }
    }
  }
  if (!std::isfinite(dist[static_cast<size_t>(goal)])) return false;
  for (int w = goal; w != start; w = prev[static_cast<size_t>(w)]) {
    if (w < 0) return false;
    ways.push_back(w);
  }
  std::reverse(ways.begin(), ways.end());
  secs = dist[static_cast<size_t>(goal)];
  return true;
}

std::vector<int> quickest(const RouteGraph& g, int start, int goal) {
  std::vector<int> ways;
  double secs = 0;
  if (!quickestTimed(g, start, goal, ways, secs)) return {};
  return ways;
}

std::vector<int> quickestToEdge(const RouteGraph& g, int start, int edge) {
  std::vector<int> a, b;
  double sa = 0, sb = 0;
  const bool hasA = g.len[static_cast<size_t>(edge) * 2] > 0 &&
                    quickestTimed(g, start, edge * 2, a, sa);
  const bool hasB = g.len[static_cast<size_t>(edge) * 2 + 1] > 0 &&
                    quickestTimed(g, start, edge * 2 + 1, b, sb);
  if (hasA && hasB) return sa <= sb ? a : b;
  if (hasA) return a;
  if (hasB) return b;
  return {};
}

std::vector<uint8_t> mainWays(const RouteGraph& g) {
  // from the longest of the bigger roads that can be driven both ways
  int seed = -1;
  for (int w = 0; w < g.n; w += 2) {
    if (g.cls[static_cast<size_t>(w)] < 4 || g.cls[static_cast<size_t>(w)] > 8 ||
        g.len[static_cast<size_t>(w)] == 0 || g.len[static_cast<size_t>(w) + 1] == 0)
      continue;
    if (seed < 0 || g.len[static_cast<size_t>(w)] > g.len[static_cast<size_t>(seed)]) seed = w;
  }
  std::vector<uint8_t> out(static_cast<size_t>(g.n), 0);
  if (seed < 0) return out;
  std::vector<uint8_t> there(out.size(), 0), back(out.size(), 0);
  std::vector<int> stack;
  stack.push_back(seed);
  there[static_cast<size_t>(seed)] = 1;
  while (!stack.empty()) {
    const int w = stack.back();
    stack.pop_back();
    for (uint32_t k = g.first[static_cast<size_t>(w)];
         k < g.first[static_cast<size_t>(w) + 1]; k++) {
      const int v = static_cast<int>(g.to[k]);
      if (!there[static_cast<size_t>(v)] && (g.kind[static_cast<size_t>(v)] & RG_NO_ENTRY) == 0) {
        there[static_cast<size_t>(v)] = 1;
        stack.push_back(v);
      }
    }
  }
  stack.push_back(seed);
  back[static_cast<size_t>(seed)] = 1;
  while (!stack.empty()) {
    const int w = stack.back();
    stack.pop_back();
    for (uint32_t p = g.firstIn[static_cast<size_t>(w)];
         p < g.firstIn[static_cast<size_t>(w) + 1]; p++) {
      const int v = static_cast<int>(g.from[p]);
      if (!back[static_cast<size_t>(v)] && (g.kind[static_cast<size_t>(v)] & RG_NO_ENTRY) == 0) {
        back[static_cast<size_t>(v)] = 1;
        stack.push_back(v);
      }
    }
  }
  for (int w = 0; w < g.n; w++)
    out[static_cast<size_t>(w)] =
        static_cast<uint8_t>(there[static_cast<size_t>(w)] & back[static_cast<size_t>(w)]);
  return out;
}

}  // namespace fs

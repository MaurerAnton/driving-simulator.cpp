// SPDX-License-Identifier: AGPL-3.0-only
// Origin: tests/sim/queues.test.ts, tests/sim/smoothpose.test.ts, tests/sim/limitmatch.test.ts @ v1.7.7 (makeNet)
/**
 * Made-up roads between points for tests, as several of the original's test
 * files build them for themselves (each has a `makeNet` of its own): built the
 * way the map build hands a road network to the game (world.json).  The town
 * on a grid is tests/town.h.
 */
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/geom.h"
#include "core/js.h"
#include "sim/jsmath.h"
#include "world/lanes.h"
#include "world/sparse.h"
#include "world/types.h"
#include "world/world.h"

namespace fs::roads {

/** signal controllers as the map hands them on – a list by their number (`new SignalSystem([a, b])` of the original's tests) */
inline Sparse<SignalControllerJson> controllers(std::vector<SignalControllerJson> list = {}) {
  Sparse<SignalControllerJson> out;
  for (size_t i = 0; i < list.size(); i++) out.set(static_cast<int64_t>(i), std::make_unique<SignalControllerJson>(std::move(list[i])));
  return out;
}

/** the points of a straight road from A to B, one about every 20 m (y = 100) */
inline std::vector<double> straightPoints(const NodeJson& A, const NodeJson& B) {
  std::vector<double> p;
  const int n = static_cast<int>(jsMax(1, jsRound(jsHypot(B.x - A.x, B.z - A.z) / 20)));
  for (int k = 0; k <= n; k++) {
    p.push_back(A.x + ((B.x - A.x) * k) / n);
    p.push_back(100);
    p.push_back(A.z + ((B.z - A.z) * k) / n);
  }
  return p;
}

/** the arms of each junction, counter-clockwise: `role(n)` gives the role of an arm of a junction with n arms */
inline void linkArms(std::vector<NodeJson>& nodes, const std::vector<EdgeJson>& edges, const std::function<int(size_t arms)>& role, bool control) {
  for (size_t ni = 0; ni < nodes.size(); ni++) {
    NodeJson& node = nodes[ni];
    std::vector<std::pair<int, double>> arms;  // edge, angle
    for (size_t ei = 0; ei < edges.size(); ei++) {
      const EdgeJson& e = edges[ei];
      if (e.a != static_cast<int>(ni) && e.b != static_cast<int>(ni)) continue;
      const NodeJson& other = nodes[static_cast<size_t>(e.a == static_cast<int>(ni) ? e.b : e.a)];
      arms.emplace_back(static_cast<int>(ei), jsAtan2(-(other.z - node.z), other.x - node.x));
    }
    std::stable_sort(arms.begin(), arms.end(), [](const auto& p, const auto& q) { return p.second - q.second < 0; });
    node.arms.clear();
    for (const auto& a : arms) {
      ArmJson arm;
      arm.e = a.first;
      arm.r = role(arms.size());
      node.arms.push_back(arm);
    }
    if (control) node.t = arms.size() > 2 ? CTRL_RBL : CTRL_NONE;
  }
}

// ---------------------------------------------------------------------------------------------
// tests/sim/queues.test.ts: makeNet

struct Road {
  int a = 0;
  int b = 0;
  int lf = 1;
  int lb = 1;
  bool urban = false;
  int flags = 0;
  int cls = SECONDARY;
};

struct Net {
  WorldJson json;
  std::unique_ptr<World> world;
  std::unique_ptr<LaneGraph> lanes;
};

/**
 * Straight roads between junctions at these points (y = 100), as the map build hands them to
 * the game.  `rbl`: junctions of more than two roads are right before left (tests/sim/queues.test.ts);
 * without it no junction has a rule at all (tests/sim/smoothpose.test.ts).
 */
inline Net makeNet(const std::vector<std::pair<double, double>>& points, const std::vector<Road>& roads, bool rbl = true) {
  Net net;
  std::vector<NodeJson> nodes;
  for (const auto& [x, z] : points) {
    NodeJson n;
    n.x = x;
    n.y = 100;
    n.z = z;
    n.t = CTRL_NONE;
    nodes.push_back(n);
  }
  std::vector<EdgeJson> edges;
  for (size_t i = 0; i < roads.size(); i++) {
    const Road& r = roads[i];
    const NodeJson& A = nodes[static_cast<size_t>(r.a)];
    const NodeJson& B = nodes[static_cast<size_t>(r.b)];
    EdgeJson e;
    e.a = r.a;
    e.b = r.b;
    e.c = r.cls;
    e.n = "Road " + std::to_string(i);
    e.p = straightPoints(A, B);
    e.lf = r.lf;
    e.lb = r.lb;
    e.lw = 3.2;
    e.w = (r.lf + r.lb) * 3.2;
    e.mf = 50;
    e.mb = 50;
    e.f = (r.urban ? static_cast<int>(EF_URBAN) : 0) | r.flags;
    e.ca = 0;
    e.cb = 0;
    edges.push_back(e);
  }
  linkArms(nodes, edges, [rbl](size_t arms) { return rbl && arms > 2 ? ROLE_RBL : ROLE_NONE; }, rbl);
  WorldJson& json = net.json;
  json.version = 1;
  json.meta.name = "Test roads";
  json.meta.minX = -3000;
  json.meta.maxX = 4000;
  json.meta.minZ = -3000;
  json.meta.maxZ = 4000;
  json.meta.tileSize = 500;
  json.nodes = nodes;
  json.edges = edges;
  computeAllCuts(json);
  net.world = std::make_unique<World>(json);
  net.lanes = std::make_unique<LaneGraph>(*net.world);
  return net;
}

/** One straight road of 700 m from west to east, joined on at both ends so that the cars drive on (tests/sim/queues.test.ts: straight). */
inline Net straight(Road road = {}) {
  road.a = 0;
  road.b = 1;
  return makeNet({{0, 0}, {700, 0}, {700, 400}, {0, 400}}, {road, Road{.a = 1, .b = 2}, Road{.a = 2, .b = 3}, Road{.a = 3, .b = 0}});
}

// ---------------------------------------------------------------------------------------------
// tests/sim/limitmatch.test.ts: makeNet

struct Road3 {
  int a = 0;
  int b = 0;
  int cls = RESIDENTIAL;
  int lf = 1;
  int lb = 1;
  double lw = 3;
  double max = 50;
  std::string name = "";
  int flags = 0;
};

/** Straight roads between junctions at these points (x, y, z), built as the map build hands them to the game. */
inline Net makeNet3(const std::vector<std::array<double, 3>>& points, const std::vector<Road3>& roads) {
  Net net;
  std::vector<NodeJson> nodes;
  for (const auto& pt : points) {
    NodeJson n;
    n.x = pt[0];
    n.y = pt[1];
    n.z = pt[2];
    n.t = CTRL_NONE;
    nodes.push_back(n);
  }
  std::vector<EdgeJson> edges;
  for (size_t i = 0; i < roads.size(); i++) {
    const Road3& r = roads[i];
    const NodeJson& A = nodes[static_cast<size_t>(r.a)];
    const NodeJson& B = nodes[static_cast<size_t>(r.b)];
    EdgeJson e;
    const int n = static_cast<int>(jsMax(1, jsRound(jsHypot(B.x - A.x, B.z - A.z) / 20)));
    for (int k = 0; k <= n; k++) {
      const double t = static_cast<double>(k) / n;
      e.p.push_back(A.x + (B.x - A.x) * t);
      e.p.push_back(A.y + (B.y - A.y) * t);
      e.p.push_back(A.z + (B.z - A.z) * t);
    }
    e.a = r.a;
    e.b = r.b;
    e.c = r.cls;
    e.n = !r.name.empty() ? r.name : "Road " + std::to_string(i);
    e.lf = r.lf;
    e.lb = r.lb;
    e.lw = r.lw;
    e.w = (r.lf + r.lb) * r.lw;
    e.mf = r.max;
    e.mb = r.max;
    e.f = r.flags;
    e.ca = 0;
    e.cb = 0;
    edges.push_back(e);
  }
  linkArms(nodes, edges, [](size_t arms) { return arms > 2 ? ROLE_RBL : ROLE_NONE; }, true);
  WorldJson& json = net.json;
  json.version = 1;
  json.meta.name = "Limit match";
  json.meta.minX = -3000;
  json.meta.maxX = 3000;
  json.meta.minZ = -3000;
  json.meta.maxZ = 3000;
  json.meta.tileSize = 500;
  json.nodes = nodes;
  json.edges = edges;
  computeAllCuts(json);
  net.world = std::make_unique<World>(json);
  net.lanes = std::make_unique<LaneGraph>(*net.world);
  return net;
}

}  // namespace fs::roads

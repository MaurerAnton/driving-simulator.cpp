// SPDX-License-Identifier: AGPL-3.0-only
// Origin: tests/game/town.ts @ v1.7.7 (makeTown) and tests/sim/queues.test.ts (makeNet)
/**
 * A small town for tests: streets on a square grid, built the way the map
 * build hands a road network to the game (world.json) – without needing the
 * real map.  And made-up roads between points (makeNet), as several of the
 * original's test files build them for themselves.
 */
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "src/core/geom.h"
#include "src/core/js.h"
#include "src/sim/jsmath.h"
#include "src/world/lanes.h"
#include "src/world/types.h"
#include "src/world/world.h"

namespace fs::town {

/**
 * src/world/world.ts, computeAllCuts: "Pipeline helper: create a `World` whose cuts have not
 * been computed yet, compute them, and write them back into the JSON."  (The map layer of this
 * worktree has computeJunctionCuts, not this: sim/world_needs.h.)
 */
inline void computeAllCuts(WorldJson& json) {
  // first pass with zero cuts to get linked structures
  for (EdgeJson& e : json.edges) {
    e.ca = 0;
    e.cb = 0;
  }
  World w(json);
  for (Node& node_ : w.nodes()) {
    Node* node = &node_;
    computeJunctionCuts(*node);
    std::vector<double>& fr = json.nodes[static_cast<size_t>(node->id)].fr;
    fr.clear();
    for (const double r : node->fr) fr.push_back(jsRound(r * 100) / 100);
    for (const Arm& arm : node->arms) {
      EdgeJson& ej = json.edges[static_cast<size_t>(arm.edge->id)];
      const double c = jsRound(arm.cut * 100) / 100;
      if (arm.out) ej.ca = c;
      else ej.cb = c;
    }
  }
  // an edge shorter than the sum of its cuts keeps a sliver of ribbon
  for (EdgeJson& e : json.edges) {
    const double len = makePolyline(e.p).len;
    if (e.ca + e.cb > len - 0.2) {
      const double k = jsMax(0, len - 0.2) / (e.ca + e.cb);
      e.ca = std::floor(e.ca * k * 100) / 100;
      e.cb = std::floor(e.cb * k * 100) / 100;
    }
  }
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
    e.f = (r.urban ? EF_URBAN : 0) | r.flags;
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
  std::string name;
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

// ---------------------------------------------------------------------------------------------
// tests/game/town.ts: makeTown

struct Street {
  int i0, j0, i1, j1;
};

struct TownCrossing {
  Street street;
  double at = 0;
  int k = 0;
};

struct TownOptions {
  /** streets along the columns and rows: `cols` × `rows` junctions, `block` metres apart */
  int cols = 4;
  int rows = 4;
  double block = 100;
  /** streets (given by the grid points they join) that may only be driven from the first point to the second */
  std::vector<Street> oneWay;
  /** streets that are left out */
  std::vector<Street> missing;
  /** streets with two lanes in each direction (turning right from the right one, left from the left one) */
  std::vector<Street> wide;
  /** forbidden turns: from the street (i0, j0)–(i1, j1) at (i1, j1) into the street (i1, j1)–(i2, j2) */
  std::vector<std::array<int, 6>> bans;
  /** a cul-de-sac of this length (m) leading east from the last junction of the first row */
  double deadEnd = 0;
  /** pavements of this width (m) on both sides of every street */
  double pavement = 0;
  /**
   * Junctions (column, row) with traffic lights: the streets along the rows
   * are the main direction (group 0, with the right of way when the lights
   * are out), those along the columns the side direction (group 1, give way).
   */
  std::vector<std::pair<int, int>> lights;
  /** … without signs under the lights: with the lights out, right before left counts there */
  bool lightsUnsigned = false;
  /**
   * Crossings for people on foot: on the street between two grid points, at a share of its length, a
   * zebra crossing or a pedestrian light of its own (XING.ZEBRA, XING.SIGNAL) – 4 m wide
   */
  std::vector<TownCrossing> crossings;
  // (the original's `lightsOut`: the map layer of this worktree has no lights that are out – sim/world_needs.h, Node.dark)
};

struct Town {
  /** the town as the map build writes a map in one piece */
  WorldJson json;
  std::unique_ptr<World> world;
  std::unique_ptr<LaneGraph> lanes;
  int cols = 0;
  std::map<std::pair<int, int>, int> byKey;

  /** the node at column i, row j of the grid */
  int node(int i, int j) const { return j * cols + i; }
  /** the street between two neighbouring nodes of the grid (its edge) */
  int street(int i0, int j0, int i1, int j1) const {
    const int a = node(i0, j0);
    const int b = node(i1, j1);
    auto it = byKey.find({std::min(a, b), std::max(a, b)});
    if (it == byKey.end()) throw std::runtime_error("no such street");
    return it->second;
  }
};

inline Town makeTown(const TownOptions& opt = {}) {
  Town town;
  const int cols = opt.cols;
  const int rows = opt.rows;
  const double block = opt.block;
  town.cols = cols;
  std::vector<NodeJson> nodes;
  std::vector<EdgeJson> edges;
  const auto at = [cols](int i, int j) { return j * cols + i; };
  for (int j = 0; j < rows; j++) {
    for (int i = 0; i < cols; i++) {
      NodeJson n;
      n.x = i * block;
      n.y = 100;
      n.z = j * block;
      n.t = CTRL_NONE;
      nodes.push_back(n);
    }
  }
  const auto key = [](int a, int b) { return std::make_pair(std::min(a, b), std::max(a, b)); };
  std::map<std::pair<int, int>, int>& byKey = town.byKey;
  const auto same = [&](const std::vector<Street>& list, int a, int b) {
    for (const Street& s : list) {
      if (at(s.i0, s.j0) == a && at(s.i1, s.j1) == b) return 1;
      if (at(s.i0, s.j0) == b && at(s.i1, s.j1) == a) return -1;
    }
    return 0;
  };
  const auto join = [&](int a, int b) {
    if (same(opt.missing, a, b) != 0) return;
    const int way = same(opt.oneWay, a, b);
    const bool one = way != 0;
    const int lanes = same(opt.wide, a, b) != 0 ? 2 : 1;
    const NodeJson& A = nodes[static_cast<size_t>(a)];
    const NodeJson& B = nodes[static_cast<size_t>(b)];
    byKey[key(a, b)] = static_cast<int>(edges.size());
    EdgeJson e;
    e.a = a;
    e.b = b;
    e.c = RESIDENTIAL;
    e.n = "Street " + std::to_string(edges.size());
    e.p = straightPoints(A, B);
    e.lf = one && way < 0 ? 0 : lanes;
    e.lb = one && way > 0 ? 0 : lanes;
    e.lw = one ? 3.5 : lanes > 1 ? 3.25 : 2.75;
    e.w = one ? 3.5 * lanes : lanes > 1 ? 13 : 5.5;
    e.mf = 50;
    e.mb = 50;
    e.f = EF_URBAN;
    e.ca = 0;
    e.cb = 0;
    if (opt.pavement != 0) {
      e.sl = opt.pavement;
      e.sr = opt.pavement;
    }
    edges.push_back(e);
  };
  for (int j = 0; j < rows; j++) {
    for (int i = 0; i < cols; i++) {
      if (i + 1 < cols) join(at(i, j), at(i + 1, j));
      if (j + 1 < rows) join(at(i, j), at(i, j + 1));
    }
  }
  if (opt.deadEnd != 0) {
    const int from = at(cols - 1, 0);
    NodeJson n;
    n.x = nodes[static_cast<size_t>(from)].x + opt.deadEnd;
    n.y = 100;
    n.z = nodes[static_cast<size_t>(from)].z;
    n.t = CTRL_NONE;
    nodes.push_back(n);
    join(from, static_cast<int>(nodes.size()) - 1);
  }
  // the arms of each junction, counter-clockwise
  linkArms(nodes, edges, [](size_t arms) { return arms > 2 ? ROLE_RBL : ROLE_NONE; }, true);
  // traffic lights, with the programme the map build gives a crossing of two streets
  std::vector<SignalControllerJson> signals;
  for (const auto& [i, j] : opt.lights) {
    NodeJson& node = nodes[static_cast<size_t>(at(i, j))];
    node.t = CTRL_SIGNALS;
    node.sc = static_cast<int>(signals.size());
    for (ArmJson& arm : node.arms) {
      const EdgeJson& e = edges[static_cast<size_t>(arm.e)];
      const NodeJson& other = nodes[static_cast<size_t>(e.a == at(i, j) ? e.b : e.a)];
      const bool main = std::abs(other.z - node.z) < 1;
      arm.g = main ? 0 : 1;
      if (!opt.lightsUnsigned) arm.r = main ? ROLE_PRIORITY : ROLE_YIELD;
    }
    SignalControllerJson sc;
    sc.x = node.x;
    sc.z = node.z;
    sc.groups = 2;
    sc.phases = {{28, "GR"}, {3, "AR"}, {1, "RR"}, {1, "RY"}, {18, "RG"}, {3, "RA"}, {1, "RR"}, {1, "YR"}};
    sc.off = 0;
    signals.push_back(sc);
  }
  // crossings, each with a light of its own as the map build gives one (it rests on green for the vehicles)
  std::vector<CrossingJson> crossings;
  for (const TownCrossing& c : opt.crossings) {
    auto it = byKey.find(key(at(c.street.i0, c.street.j0), at(c.street.i1, c.street.j1)));
    if (it == byKey.end()) throw std::runtime_error("no such street");
    const int e = it->second;
    const EdgeJson& E = edges[static_cast<size_t>(e)];
    const double len = jsHypot(nodes[static_cast<size_t>(E.b)].x - nodes[static_cast<size_t>(E.a)].x, nodes[static_cast<size_t>(E.b)].z - nodes[static_cast<size_t>(E.a)].z);
    CrossingJson x;
    x.e = e;
    x.s = len * c.at;
    x.k = c.k;
    x.w = 4;
    if (c.k == XING_SIGNAL) {
      x.sc = static_cast<int>(signals.size());
      x.g = 0;
      SignalControllerJson sc;
      sc.x = 0;
      sc.z = 0;
      sc.groups = 1;
      sc.phases = {{48, "G"}, {3, "A"}, {13, "R"}, {1, "Y"}};
      sc.off = 0;
      sc.ped = true;
      signals.push_back(sc);
    }
    crossings.push_back(x);
  }
  std::vector<std::vector<int>> banned;
  for (const std::array<int, 6>& b : opt.bans) {
    auto from = byKey.find(key(at(b[0], b[1]), at(b[2], b[3])));
    auto to = byKey.find(key(at(b[2], b[3]), at(b[4], b[5])));
    if (from == byKey.end() || to == byKey.end()) throw std::runtime_error("no such street");
    banned.push_back({from->second, at(b[2], b[3]), to->second});
  }
  WorldJson& json = town.json;
  json.version = 1;
  json.meta.name = "Test town";
  json.meta.minX = -200;
  json.meta.maxX = cols * block + 400;
  json.meta.minZ = -200;
  json.meta.maxZ = rows * block + 200;
  json.meta.tileSize = 500;
  json.nodes = nodes;
  json.edges = edges;
  json.banned = banned;
  json.signals = signals;
  json.crossings = crossings;
  computeAllCuts(json);
  town.world = std::make_unique<World>(json);
  town.lanes = std::make_unique<LaneGraph>(*town.world);
  return town;
}

}  // namespace fs::town

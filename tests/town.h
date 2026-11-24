// SPDX-License-Identifier: AGPL-3.0-only
// Origin: tests/game/town.ts @ v1.7.7
/**
 * A small town for tests: streets on a square grid, built the way the map
 * build hands a road network to the game (world.json) – without needing the
 * real map.
 */
#pragma once

#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/geom.h"
#include "world/lanes.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

struct TownCrossing {
  std::array<int, 4> street;
  double at = 0;
  int k = 0;
};

struct TownOptions {
  /** streets along the columns and rows: `cols` × `rows` junctions, `block` metres apart */
  int cols = 4;
  int rows = 4;
  double block = 100;
  /** streets (given by the grid points they join) that may only be driven from the first point to the second */
  std::vector<std::array<int, 4>> oneWay;
  /** streets that are left out */
  std::vector<std::array<int, 4>> missing;
  /** streets with two lanes in each direction (turning right from the right one, left from the left one) */
  std::vector<std::array<int, 4>> wide;
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
  std::vector<std::array<int, 2>> lights;
  /** … without signs under the lights: with the lights out, right before left counts there */
  bool lightsUnsigned = false;
  /** the signal controllers (by their number: the order of `lights`) that are out of order */
  std::function<bool(int controller)> lightsOut;
  /**
   * Crossings for people on foot: on the street between two grid points, at a share of its length, a
   * zebra crossing or a pedestrian light of its own (XING.ZEBRA, XING.SIGNAL) – 4 m wide
   */
  std::vector<TownCrossing> crossings;
};

struct Town {
  /** the town as the map build writes a map in one piece */
  WorldJson json;
  std::unique_ptr<World> world;
  std::unique_ptr<LaneGraph> lanes;
  int cols = 0;
  /** the node at column i, row j of the grid */
  int node(int i, int j) const { return j * cols + i; }
  /** the street between two neighbouring nodes of the grid (its edge) */
  int street(int i0, int j0, int i1, int j1) const {
    auto e = byKey.find(key(node(i0, j0), node(i1, j1)));
    if (e == byKey.end()) throw std::runtime_error("no such street");
    return e->second;
  }

  std::map<std::pair<int, int>, int> byKey;
  static std::pair<int, int> key(int a, int b) { return {std::min(a, b), std::max(a, b)}; }
};

inline Town makeTown(const TownOptions& opt = {}) {
  Town town;
  const int cols = opt.cols;
  const int rows = opt.rows;
  const double block = opt.block;
  town.cols = cols;
  std::vector<NodeJson> nodes;
  std::vector<EdgeJson> edges;
  auto at = [cols](int i, int j) { return j * cols + i; };
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
  auto same = [&](const std::vector<std::array<int, 4>>& list, int a, int b) -> int {
    for (const auto& [i0, j0, i1, j1] : list) {
      if (at(i0, j0) == a && at(i1, j1) == b) return 1;
      if (at(i0, j0) == b && at(i1, j1) == a) return -1;
    }
    return 0;
  };
  auto join = [&](int a, int b) {
    if (same(opt.missing, a, b) != 0) return;
    const int way = same(opt.oneWay, a, b);
    const bool one = way != 0;
    const int lanes = same(opt.wide, a, b) != 0 ? 2 : 1;
    const NodeJson& A = nodes[static_cast<size_t>(a)];
    const NodeJson& B = nodes[static_cast<size_t>(b)];
    std::vector<double> p;
    const double n = jsMax(1, jsRound(jsHypot(B.x - A.x, B.z - A.z) / 20));
    for (double k = 0; k <= n; k++) {
      p.push_back(A.x + ((B.x - A.x) * k) / n);
      p.push_back(100);
      p.push_back(A.z + ((B.z - A.z) * k) / n);
    }
    town.byKey[Town::key(a, b)] = static_cast<int>(edges.size());
    EdgeJson e;
    e.a = a;
    e.b = b;
    e.c = RESIDENTIAL;
    e.n = "Street " + std::to_string(edges.size());
    e.p = std::move(p);
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
    edges.push_back(std::move(e));
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
  for (size_t ni = 0; ni < nodes.size(); ni++) {
    NodeJson& node = nodes[ni];
    struct A {
      int e;
      double ang;
    };
    std::vector<A> arms;
    for (size_t ei = 0; ei < edges.size(); ei++) {
      const EdgeJson& e = edges[ei];
      if (e.a != static_cast<int>(ni) && e.b != static_cast<int>(ni)) continue;
      const NodeJson& other = nodes[static_cast<size_t>(e.a == static_cast<int>(ni) ? e.b : e.a)];
      arms.push_back({static_cast<int>(ei), angleOf(other.x - node.x, other.z - node.z)});
    }
    std::stable_sort(arms.begin(), arms.end(), [](const A& p, const A& q) { return p.ang < q.ang; });
    node.arms.clear();
    for (const A& a : arms) {
      ArmJson aj;
      aj.e = a.e;
      aj.r = arms.size() > 2 ? ROLE_RBL : ROLE_NONE;
      node.arms.push_back(aj);
    }
    node.t = arms.size() > 2 ? CTRL_RBL : CTRL_NONE;
  }
  // traffic lights, with the programme the map build gives a crossing of two streets
  std::vector<SignalControllerJson> signals;
  for (const auto& [i, j] : opt.lights) {
    NodeJson& node = nodes[static_cast<size_t>(at(i, j))];
    node.t = CTRL_SIGNALS;
    node.sc = static_cast<int>(signals.size());
    for (ArmJson& arm : node.arms) {
      const EdgeJson& e = edges[static_cast<size_t>(arm.e)];
      const NodeJson& other = nodes[static_cast<size_t>(e.a == at(i, j) ? e.b : e.a)];
      const bool main = std::fabs(other.z - node.z) < 1;
      arm.g = main ? 0 : 1;
      if (!opt.lightsUnsigned) arm.r = main ? ROLE_PRIORITY : ROLE_YIELD;
    }
    SignalControllerJson c;
    c.x = node.x;
    c.z = node.z;
    c.groups = 2;
    c.phases = {{28, "GR"}, {3, "AR"}, {1, "RR"}, {1, "RY"}, {18, "RG"}, {3, "RA"}, {1, "RR"}, {1, "YR"}};
    c.off = 0;
    signals.push_back(std::move(c));
  }
  // crossings, each with a light of its own as the map build gives one (it rests on green for the vehicles)
  std::vector<CrossingJson> crossings;
  for (const TownCrossing& c : opt.crossings) {
    const auto& [i0, j0, i1, j1] = c.street;
    auto found = town.byKey.find(Town::key(at(i0, j0), at(i1, j1)));
    if (found == town.byKey.end()) throw std::runtime_error("no such street");
    const int e = found->second;
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
      SignalControllerJson s;
      s.x = 0;
      s.z = 0;
      s.groups = 1;
      s.phases = {{48, "G"}, {3, "A"}, {13, "R"}, {1, "Y"}};
      s.off = 0;
      s.ped = true;
      signals.push_back(std::move(s));
    }
    crossings.push_back(x);
  }
  std::vector<std::array<int, 3>> banned;
  for (const auto& [i0, j0, i1, j1, i2, j2] : opt.bans) {
    auto from = town.byKey.find(Town::key(at(i0, j0), at(i1, j1)));
    auto to = town.byKey.find(Town::key(at(i1, j1), at(i2, j2)));
    if (from == town.byKey.end() || to == town.byKey.end()) throw std::runtime_error("no such street");
    banned.push_back({from->second, at(i1, j1), to->second});
  }
  WorldJson& json = town.json;
  json.version = 1;
  json.meta.name = "Test town";
  json.meta.generated = "";
  json.meta.minX = -200;
  json.meta.maxX = cols * block + 400;
  json.meta.minZ = -200;
  json.meta.maxZ = rows * block + 200;
  json.meta.tileSize = 500;
  json.meta.originE = 0;
  json.meta.originN = 0;
  json.nodes = std::move(nodes);
  json.edges = std::move(edges);
  json.banned = std::move(banned);
  json.signals = std::move(signals);
  json.crossings = std::move(crossings);
  computeAllCuts(json);
  WorldOptions wo;
  wo.lightsOut = opt.lightsOut;
  town.world = std::make_unique<World>(json, wo);
  town.lanes = std::make_unique<LaneGraph>(*town.world);
  return town;
}

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
/** Lane graph: lanes per edge, connectors at junctions, conflicts, routing. */
#include "../src/world/lanes.h"
#include <cmath>
#include "../src/world/world.h"
#include <cassert>
#include <cstdio>

using namespace fs;

namespace {
WorldJson makeNet() {
  WorldJson j;
  j.version = 1;
  j.meta.tileSize = 500;
  auto node = [](double x, double y, double z, int t) {
    NodeJson n;
    n.x = x; n.y = y; n.z = z; n.t = t;
    return n;
  };
  j.nodes.push_back(node(0, 0, 0, CTRL_RBL));
  j.nodes.push_back(node(100, 0, 0, CTRL_NONE));
  j.nodes.push_back(node(0, 0, 100, CTRL_NONE));
  j.nodes.push_back(node(0, 0, -100, CTRL_NONE));
  auto edge = [](int a, int b, std::vector<double> p, double w, int lf, int lb) {
    EdgeJson e;
    e.a = a; e.b = b; e.c = RESIDENTIAL; e.p = std::move(p);
    e.w = w; e.lf = lf; e.lb = lb; e.lw = 3.5; e.mf = 50; e.mb = 50;
    return e;
  };
  j.edges.push_back(edge(0, 1, {-100, 0, 0, 100, 0, 0}, 7.0, 1, 1));  // E-W
  j.edges.push_back(edge(3, 2, {0, 0, -100, 0, 0, 100}, 7.0, 1, 1));  // N-S
  auto arm = [](int e, int r) {
    ArmJson a;
    a.e = e; a.r = r;
    return a;
  };
  j.nodes[0].arms = {arm(0, ROLE_PRIORITY), arm(1, ROLE_PRIORITY)};
  j.nodes[1].arms = {arm(0, ROLE_NONE)};
  j.nodes[2].arms = {arm(1, ROLE_NONE)};
  j.nodes[3].arms = {arm(1, ROLE_NONE)};
  return j;
}
}  // namespace

int main() {
  World w(makeNet());
  LaneGraph g(w);

  // 2 edges × 2 directions × 1 lane = 4 lanes
  assert(g.lanes.size() == 4);
  assert(g.edgeLanes.size() == 2);
  for (const auto& l : g.lanes) {
    assert(l.edge != nullptr && l.from != nullptr && l.to != nullptr);
    assert(l.armTo != nullptr && l.armFrom != nullptr);
    assert(l.len > 199 && l.len < 201);
    assert(l.width == 3.5);
    // the lane spans the edge between its cut-backs
    assert(l.s0 <= l.s1 + 1e-9);
    // stop line distance from the start of the lane
    assert(l.stopAt >= 0 && l.stopAt <= l.len + 1e-9);
    // speed limit in m/s
    assert(l.limit > 10 && l.limit < 20);
  }
  // lanes of an edge: forward and backward lists
  assert(g.edgeLanes[0].fwd.size() == 1 && g.edgeLanes[0].back.size() == 1);

  // connectors at the junction: every arriving lane reaches every leaving lane
  assert(!g.connectors.empty());
  int straight = 0, left = 0, right = 0;
  for (const auto& c : g.connectors) {
    assert(c.node != nullptr && c.from != nullptr && c.to != nullptr);
    assert(c.len > 0 && c.pl.cum.size() >= 2);
    assert(c.from->out.size() >= 1 && c.to->in.size() >= 1);
    if (c.turn == TURN_STRAIGHT) straight++;
    else if (c.turn == TURN_LEFT) left++;
    else if (c.turn == TURN_RIGHT) right++;
  }
  assert(straight >= 2);    // E-W through, N-S through
  assert(left == right);    // symmetric junction

  // conflicts exist between the crossing connectors, and the yielding side
  // is decided (not both, not neither)
  int conflicts = 0, yields = 0;
  for (const auto& c : g.connectors) {
    for (const auto& f : c.conflicts) {
      conflicts++;
      if (f.yield) yields++;
      assert(f.other != nullptr && f.other != &c);
      assert(f.sSelf >= 0 && f.sOther >= 0);
    }
  }
  assert(conflicts > 0);
  assert(yields > 0 && yields < conflicts);

  // routing: from a lane on the E-W road to the N-S edge needs connectors
  const Lane& fwd = *g.edgeLanes[0].fwd[0];
  std::vector<Connector*> path = g.route(fwd, w.edges()[1], false);
  assert(!path.empty());
  assert(path.back()->to->edge == &w.edges()[1]);
  // each step of the route starts where the previous one ended
  for (size_t i = 1; i < path.size(); i++) assert(path[i]->from == path[i - 1]->to);

  // a route to the edge we start on is trivially empty
  std::vector<Connector*> same = g.route(fwd, w.edges()[0], false);
  assert(same.empty());

  // lane point sampling: at half length the sample sits mid-edge, offset
  // sideways by the lane's lateral offset
  PolySample s = lanePoint(fwd, 100, PolySample());
  assert(std::fabs(s.z) > 1.0);   // off the centreline (lane is 1.75 m left)
  assert(std::fabs(s.x) < 1e-6);  // but still on the centreline station

  // lane point at the far end of a backward lane runs the other way
  const Lane& back = *g.edgeLanes[0].back[0];
  PolySample s2 = lanePoint(back, 0, PolySample());
  assert(s2.x > 99);  // the backward lane starts at station len (the east end)

  // drivable(): private and agricultural edges are excluded
  assert(LaneGraph::drivable(w.edges()[0], true));
  w.edges()[0].access = ACCESS_PRIVATE;
  assert(!LaneGraph::drivable(w.edges()[0], true));
  assert(!LaneGraph::drivable(w.edges()[0], false));
  w.edges()[0].access = ACCESS_YES;

  std::printf("lanes: all checks passed (%zu lanes, %zu connectors)\n",
              g.lanes.size(), g.connectors.size());
  return 0;
}

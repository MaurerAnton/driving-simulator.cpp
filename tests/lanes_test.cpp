// SPDX-License-Identifier: AGPL-3.0-only
/** Lane graph: lanes per edge, connectors at junctions, routing. */
#include "../src/world/lanes.h"
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
  auto edge = [](int a, int b, std::vector<double> p, int w, int lf, int lb) {
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
  assert(g.lanes().size() == 4);
  for (const auto& l : g.lanes()) {
    assert(l.len > 199 && l.len < 201);
    assert(l.width == 3.5);
    assert(l.edge != nullptr);
  }
  // forward lanes sit on the +x side of the centreline, backward lanes mirrored
  const Lane* fwd = nullptr;
  const Lane* bwd = nullptr;
  for (const auto& l : g.lanes()) {
    if (l.edge->id == 0 && l.dir == 1) fwd = &l;
    if (l.edge->id == 0 && l.dir == -1) bwd = &l;
  }
  assert(fwd && bwd);
  assert(fwd->off > 0 && bwd->off < 0);
  assert(std::fabs(fwd->off + bwd->off) < 1e-9);

  // connectors exist at the junction: every arriving lane reaches every
  // leaving lane on the other edge (turns are allowed by default)
  assert(!g.connectors().empty());
  int straight = 0, left = 0, right = 0;
  for (const auto& c : g.connectors()) {
    if (c.turn == TURN_STRAIGHT) straight++;
    else if (c.turn == TURN_LEFT) left++;
    else if (c.turn == TURN_RIGHT) right++;
  }
  assert(straight >= 2);   // E-W through, N-S through
  assert(left == right);   // symmetric junction

  // routing: from a lane on the E-W road to the N-S edge needs connectors
  std::vector<Connector*> path;
  assert(g.route(*fwd, &w.edges()[1], false, path));
  assert(!path.empty());
  assert(path.back()->to->edge == &w.edges()[1]);

  // a route to the edge we are already on is trivially empty
  std::vector<Connector*> same;
  assert(g.route(*fwd, &w.edges()[0], false, same));
  assert(same.empty());

  // lane point sampling: at half length the sample sits mid-edge
  PolySample s = LaneGraph::lanePoint(*fwd, 100, PolySample());
  assert(std::fabs(s.x - 0) < 1e-6 && std::fabs(s.z - 0) < 1e-6);

  // nearest lane on an edge
  double off = 0;
  Lane* nl = g.nearestLane(w.edges()[0], 0, 0, &off);
  assert(nl != nullptr);

  std::printf("lanes: all checks passed (%zu lanes, %zu connectors)\n",
              g.lanes().size(), g.connectors().size());
  return 0;
}

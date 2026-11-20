// SPDX-License-Identifier: AGPL-3.0-only
/**
 * World construction and surface queries on a small hand-built network:
 * two roads crossing at right angles, checked against the same numbers the
 * TypeScript world.ts produces.
 */
#include "../src/world/world.h"
#include <cmath>
#include <cassert>
#include <cstdio>

using namespace fs;

namespace {
WorldJson makeCross() {
  WorldJson j;
  j.version = 1;
  j.meta.name = "test";
  j.meta.minX = -100;
  j.meta.maxX = 100;
  j.meta.minZ = -100;
  j.meta.maxZ = 100;
  j.meta.tileSize = 500;

  // node 0 at the origin, node 1 east, node 2 south, node 3 north
  auto node = [](double x, double y, double z, int t) {
    NodeJson n;
    n.x = x; n.y = y; n.z = z; n.t = t;
    return n;
  };
  j.nodes.push_back(node(0, 0, 0, CTRL_RBL));
  j.nodes.push_back(node(100, 0, 0, CTRL_NONE));
  j.nodes.push_back(node(0, 0, 100, CTRL_NONE));
  j.nodes.push_back(node(0, 0, -100, CTRL_NONE));

  auto edge = [](int a, int b, std::vector<double> p, int cls, double w, int lf, int lb) {
    EdgeJson e;
    e.a = a; e.b = b; e.c = cls; e.p = std::move(p);
    e.w = w; e.lf = lf; e.lb = lb; e.lw = 3.5;
    e.mf = 50; e.mb = 50;
    return e;
  };
  // east-west road through the origin
  j.edges.push_back(edge(0, 1, {-100, 0, 0, 100, 0, 0}, RESIDENTIAL, 7.0, 1, 1));
  // north-south road through the origin
  j.edges.push_back(edge(3, 2, {0, 0, -100, 0, 0, 100}, RESIDENTIAL, 7.0, 1, 1));

  // arms: node 0 has two (the E-W road and the N-S road), each with two ends
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
  WorldJson json = makeCross();
  World w(json);

  assert(w.nodes().size() == 4);
  assert(w.edges().size() == 2);
  assert(std::fabs(w.edges()[0].len - 200) < 1e-6);
  assert(w.edges()[0].armA && w.edges()[0].armB);
  assert(w.nodes()[0].arms.size() == 2);

  // the junction shape exists and contains the origin
  assert(w.nodes()[0].shape != nullptr);
  assert(pointInRing(0, 0, w.nodes()[0].shape->ring));
  assert(!pointInRing(50, 50, w.nodes()[0].shape->ring));

  // surface on the east-west road, away from the junction
  SurfaceHit hit;
  assert(w.surfaceAt(40, 0, 0, hit));
  assert(hit.edge != nullptr && hit.edge->id == 0);
  assert(hit.zone == 0);
  assert(std::fabs(hit.off) < 1e-6);
  assert(std::fabs(hit.y - 0) < 1e-6);

  // sidewalk: 3.5 m half-width, so 4 m off the centreline is the pavement
  SurfaceHit sw;
  assert(w.surfaceAt(40, 4, 0, sw));
  assert(sw.zone == 1);
  assert(sw.surf == SURF_PAVING);

  // far from any road: zone 2 (the caller falls back to the terrain)
  SurfaceHit none;
  assert(!w.surfaceAt(60, 60, 0, none));
  assert(none.zone == 2);

  // map matching
  SurfaceHit loc;
  assert(w.locate(40, 1.5, 0, 20, loc));
  assert(loc.edge != nullptr && loc.edge->id == 0);

  // nearest road footprint
  SurfaceHit nr;
  assert(w.nearestRoad(40, 20, 30, 0, nr));
  assert(nr.off > 0);  // outside the footprint

  // banned movements
  assert(!w.isBanned(0, 1, 2));
  json.banned.push_back({0, 0, 1});
  World w2(json);
  assert(w2.isBanned(0, 0, 1));
  assert(!w2.isBanned(1, 0, 0));

  std::printf("world: all checks passed (%zu nodes, %zu edges)\n", w.nodes().size(), w.edges().size());
  return 0;
}

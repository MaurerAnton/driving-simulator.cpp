// SPDX-License-Identifier: AGPL-3.0-only
// LINK: world core platform geo
/**
 * The road network (src/world/world.h) on a small town, against the original:
 * tools/diff/world_town.ts --part world prints what src/world/world.ts @ v1.7.7
 * makes of the same town (tests/fixtures/world-town.txt); this prints the same
 * lines from the port and compares them – roads, junctions with their arms,
 * areas and kerbs, crossings, the surface on a grid of points, map matching,
 * the lookups by place, forbidden turns, the cuts of a junction.
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "tests/dump.h"
#include "tests/town.h"
#include "world/world.h"

using namespace fs;
using fs::dump::lineSum;
using fs::dump::out;
using fs::dump::sumOf;

namespace {

/** the town both tests look at: one-way and wide streets, lights, crossings, a forbidden turn, a cul-de-sac, pavements */
TownOptions townOptions(bool lightsOut) {
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
  if (lightsOut) o.lightsOut = [](int c) { return c == 0; };
  return o;
}

std::string joined(const std::vector<std::string>& list) {
  std::string s;
  for (const std::string& v : list) s += (s.empty() ? "" : ",") + v;
  return s.empty() ? "-" : s;
}

}  // namespace

int main() {
  for (bool dark : {false, true}) {
    Town town = makeTown(townOptions(dark));
    World& world = *town.world;
    out("town", dark, world.nodes.size(), world.edges.size(), world.allNodes.size(), world.allEdges.size(), world.signals.size(), world.crossings.size());
    for (const Edge* ep : world.edges) {
      const Edge& e = *ep;
      out("edge", e.id, e.a->id, e.b->id, e.len, e.width, e.lanesF, e.lanesB, e.laneW, e.cutA, e.cutB, e.blendA, e.blendB, e.minX, e.maxX, e.minZ, e.maxZ, e.tier, e.swL, e.swR, e.kerbL, e.kerbR, e.marginL, e.marginR,
          e.xings.size(), e.pl.cum.size(), lineSum(e.pl.pts));
    }
    for (const Node* np : world.nodes) {
      const Node& n = *np;
      const JunctionShape* sh = n.shape.get();
      out("node", n.id, n.x, n.z, n.ctrl, n.sc, n.dark, n.stub, n.arms.size(), sumOf(n.fr), sh ? static_cast<long>(sh->ring.size()) : -1L, sh ? ringArea(sh->ring) : 0.0, sh ? sh->corners.size() : static_cast<size_t>(0),
          sh ? sh->minX : 0.0, sh ? sh->maxX : 0.0, sh ? sh->minZ : 0.0, sh ? sh->maxZ : 0.0);
      for (const Arm* a : n.arms) out("arm", n.id, a->idx, a->edge->id, a->out, a->role, a->sg, a->lamp, a->stopS, a->cut, a->dx, a->dz, a->ang, a->half, a->swL, a->swR, a->isApproach, a->isExit);
      if (sh)
        for (const JunctionCorner& c : sh->corners) out("corner", n.id, c.kerb.size(), sumOf(c.kerb), c.swA, c.swB, c.arc0, c.arc1, c.ox, c.oz, c.r);
    }
    for (const CrossingJson* x : world.crossings) out("crossing", x->e, x->s, x->k, x->w, x->sc.value_or(-1), x->g.value_or(-1));
    if (dark) {
      // (lights out of order: the junction is ruled by its signs, the heads still stand there)
      const Node& n = *world.nodes[town.node(1, 1)];
      assert(n.dark && n.ctrl == CTRL_SIGNS && n.sc == 0 && n.arms[0]->sg == -1 && n.arms[0]->lamp >= 0);
      assert(!world.nodes[town.node(2, 1)]->dark && world.nodes[town.node(2, 1)]->ctrl == CTRL_SIGNALS);
      continue;
    }
    // the surface: a grid of points over the town, at the height of the streets and well above a bridge that is not there
    SurfaceHit hit;
    for (double z = -20; z <= 220; z += 7.3) {
      for (double x = -20; x <= 380; x += 9.1) {
        const bool found = world.surfaceAt(x, z, 100.5, hit);
        if (found) out("surface", x, z, hit.y, hit.edge ? hit.edge->id : -1, hit.node ? hit.node->id : -1, hit.s, hit.off, hit.zone, hit.surf);
      }
    }
    {
      const bool a = world.surfaceAt(50, 0, 90, hit);
      const bool b = world.surfaceAt(50, 0, 98.39, hit);
      const bool c = world.surfaceAt(50, 0, 98.41, hit);
      out("below", a, b, c);
      // (a miss leaves the height and the place along the road as they were, and says so by the zone)
      SurfaceHit kept;
      kept.y = 7;
      kept.s = 8;
      kept.off = 9;
      kept.surf = 3;
      assert(!world.surfaceAt(50, 50, 100.5, kept) && kept.y == 7 && kept.s == 8 && kept.off == 9 && kept.surf == 3 && kept.zone == 2 && !kept.edge && !kept.node);
    }
    const double locates[][5] = {{50, 3, 100, 5, 4.5}, {50, 9, 100, 5, 4.5}, {50, 9, 100, 7, 4.5}, {50, 3, 106, 5, 4.5}, {50, 3, 106, 5, 8}, {150, 148, 100, 60, 4.5}, {330, 20, 100, 25, 4.5}};
    for (const auto& q : locates) {
      const bool located = world.locate(q[0], q[1], q[2], q[3], hit, q[4]);
      out("locate", located, located ? hit.edge->id : -1, located ? hit.s : 0.0, located ? hit.off : 0.0, located ? hit.y : 0.0, located ? hit.zone : 0);
    }
    const double nears[][4] = {{50, 30, 40, 0}, {50, 30, 20, 0}, {150, 148, 60, 0}, {150, 148, 60, 256}, {-30, -30, 50, 0}, {100.5, 100.2, 5, 0}};
    for (const auto& q : nears) {
      const bool near = world.nearestRoad(q[0], q[1], q[2], static_cast<int>(q[3]), hit);
      out("nearest", near, near ? hit.edge->id : -1, near ? hit.s : 0.0, near ? hit.off : 0.0, near ? hit.y : 0.0, near ? hit.zone : 0);
    }
    {
      std::vector<std::string> list;
      world.edgesInBox(90, 90, 160, 110, [&](Edge& e) { list.push_back(std::to_string(e.id)); });
      out("edgesInBox", joined(list));
      list.clear();
      world.nodesInBox(90, -10, 310, 110, [&](Node& n) { list.push_back(std::to_string(n.id)); });
      out("nodesInBox", joined(list));
      list.clear();
      world.segmentsInBox(95, 95, 105, 105, [&](Edge& e, int seg) { list.push_back(std::to_string(e.id) + ":" + std::to_string(seg)); });
      out("segmentsInBox", joined(list));
    }
    Edge& e = *world.edges[town.street(0, 1, 1, 1)];
    out("banned", world.isBanned(e, *world.nodes[town.node(1, 1)], *world.edges[town.street(1, 1, 1, 0)]), world.isBanned(e, *world.nodes[town.node(1, 1)], *world.edges[town.street(1, 1, 2, 1)]),
        world.isBanned(*world.edges[town.street(1, 1, 1, 0)], *world.nodes[town.node(1, 1)], e));
    assert(world.isBanned(e.id, town.node(1, 1), town.street(1, 1, 1, 0)));
    out("planeY", world.planeY(*world.nodes[5], 110, 95), world.edgeY(e, 3, 3, 100), world.edgeY(e, 50, 50, 100), world.edgeY(e, 97, 97, 101));
    // packed points and back; the cuts of a junction worked out again; its shape
    const std::vector<double> p = {12.34, 100.5, -7.01, 13.0, 100.56, -9.99, -250.25, 99.0, 1234.56};
    out("packed", packEdgePoints(p), edgePoints(packEdgePoints(p)));
    Node& n5 = *world.nodes[5];
    std::vector<double> before;
    for (const Arm* a : n5.arms) before.push_back(a->cut);
    computeJunctionCuts(n5);
    std::vector<double> after;
    for (const Arm* a : n5.arms) after.push_back(a->cut);
    out("cuts", before, after, n5.fr);
    const std::unique_ptr<JunctionShape> sh = buildJunctionShape(n5);
    out("shape", sh ? static_cast<long>(sh->ring.size()) : -1L, sh ? sumOf(sh->ring) : 0.0);

    // --- beside what the original prints: what the port adds to it
    // the roads and junctions by their numbers, with gaps: nothing beyond the end, nothing below zero
    assert(world.edges[static_cast<int64_t>(world.edges.size())] == nullptr && world.edges[-1] == nullptr && world.nodes[100000] == nullptr);
    assert(world.edges.count() == world.allEdges.size() && world.nodes.count() == world.allNodes.size());
    // the sets are gone through in the order of the numbers here (a map in one piece is added in that order)
    {
      int next = 0;
      for (const Edge* edge : world.allEdges) assert(edge->id == next++);
      next = 0;
      for (const Node* node : world.allNodes) assert(node->id == next++);
    }
    // the arms of a road are its own: they point back at it and at the junctions at its ends
    for (const Edge* edge : world.allEdges) {
      assert(edge->armA->edge == edge && edge->armB->edge == edge && edge->armA->node == edge->a && edge->armB->node == edge->b && edge->armA->out && !edge->armB->out);
      assert(edge->a->arms[static_cast<size_t>(edge->armA->idx)] == edge->armA && edge->b->arms[static_cast<size_t>(edge->armB->idx)] == edge->armB);
    }
    // the crossings in the world's list are those of the roads
    for (const CrossingJson* x : world.crossings) {
      bool own = false;
      for (const CrossingJson& mine : world.edges[x->e]->xings) own = own || &mine == x;
      assert(own);
    }
    assert(&world.meta == &world.json.meta && world.meta.name == "Test town" && world.json.edges.size() == world.edges.size());
    assert(hasFlag(e, EF_URBAN) && !hasFlag(e, EF_BRIDGE));
  }

  // a junction whose road is not there: said so
  {
    Town town = makeTown({});
    WorldJson json = town.json;
    json.nodes[0].arms[0].e = 999;
    bool threw = false;
    try {
      World broken(json);
    } catch (const std::exception& err) {
      threw = std::string(err.what()) == "junction 0: its road 999 is not part of the piece it came with";
    }
    assert(threw);
  }

  const int bad = dump::against("tests/fixtures/world-town.txt");
  if (bad) {
    std::fprintf(stderr, "world: %d lines differ from the original's\n", bad);
    return 1;
  }
  std::printf("world: all checks passed (%zu lines the same as the original's)\n", dump::lines().size());
  return 0;
}

// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim world core platform geo
/**
 * The lane graph the port's map layer makes of a world.json, as numbers – beside
 * tools/diff/sim_town.ts, which writes that file and prints the original's:
 *
 *   node tools/diff/sim_town.ts /home/user/driving-simulator /tmp/town.json plain > a.txt
 *   build/sim_town_dump /tmp/town.json > b.txt        (or: --make plain – the town as tests/town.h makes it)
 *   tools/diff/compare.py a.txt b.txt
 *
 * (The paths through the junctions are numbered otherwise by the port's map layer than by the
 * original's: tools/diff/sim_town_check.py compares the two by the lanes they join.)
 */
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "src/core/js.h"
#include "src/world/lanes.h"
#include "src/world/world.h"
#include "tools/dev/sim_towns.h"

using namespace fs;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: sim_town_dump world.json\n");
    return 2;
  }
  WorldJson json;
  if (std::string(argv[1]) == "--make") {
    // the town made here (tests/town.h) instead of read: the port's makeTown beside the original's
    json = town::makeTown(simdiff::townOptions(argc > 2 ? argv[2] : "plain")).json;
  } else {
    std::ifstream in(argv[1]);
    std::stringstream text;
    text << in.rdbuf();
    if (!parseWorldJson(text.str(), json)) {
      std::fprintf(stderr, "not a world.json: %s\n", argv[1]);
      return 2;
    }
  }
  World world(json);
  LaneGraph g(world);
  g.resolveAll();
  std::string out;
  const auto num = [&](double x) { out += ' ' + jsStr(x); };
  for (const Lane& l : g.lanes) {
    if (!l.edge) continue;
    out += "lane";
    for (double x : {double(l.id), double(l.edge->id), double(l.dir), double(l.idx), double(l.count), l.off, l.width, l.s0, l.s1, l.len, double(l.from->id), double(l.to->id),
                     std::isfinite(l.limit) ? l.limit : -1.0, l.stopAt})
      num(x);
    out += " out";
    for (const Connector* c : l.out) num(c->id);
    out += " in";
    for (const Connector* c : l.in) num(c->id);
    out += '\n';
  }
  for (const Connector& c : g.connectors) {
    if (!c.node) continue;
    out += "conn";
    for (double x : {double(c.id), double(c.node->id), double(c.from->id), double(c.to->id), double(c.turn), c.len}) num(x);
    out += " pts";
    for (double x : c.pl.pts) num(x);
    out += '\n';
    for (const Conflict& k : c.conflicts) {
      out += "conflict";
      for (double x : {double(c.id), double(k.other->id), k.sSelf, k.sOther, k.merge ? 1.0 : 0.0, k.yield ? 1.0 : 0.0}) num(x);
      out += '\n';
    }
    for (const Sibling& s : c.siblings) {
      out += "sibling";
      for (double x : {double(c.id), double(s.other->id), s.until, s.untilOther}) num(x);
      out += '\n';
    }
  }
  for (const Edge& edge : world.edges()) {
    const Edge* e = &edge;
    if (!e->a) continue;
    out += "edge";
    for (double x : {double(e->id), double(e->a->id), double(e->b->id), e->len, e->cutA, e->cutB, e->width, e->tier, e->blendA, e->blendB}) num(x);
    out += '\n';
  }
  for (const Node& node : world.nodes()) {
    const Node* n = &node;
    out += "node";
    for (double x : {double(n->id), double(n->ctrl), double(n->sc)}) num(x);
    out += " arms";
    for (const Arm& a : n->arms) {
      out += ' ' + jsStr(a.edge->id) + '/' + jsStr(a.role) + '/' + jsStr(a.sg) + '/' + jsStr(a.stopS) + '/' + jsStr(a.cut) + '/' + jsStr(a.dx) + '/' + jsStr(a.dz) + '/' + (a.isApproach ? "1" : "0") + '/' +
             (a.isExit ? "1" : "0");
    }
    out += '\n';
  }
  std::fputs(out.c_str(), stdout);
  return 0;
}

// SPDX-License-Identifier: AGPL-3.0-only
// LINK: world core platform geo
/**
 * The map layer of the port, printed as numbers – to be compared with what
 * tools/diff/world.ts prints from the original (tools/diff/compare.py):
 *
 *   FS_JOBS=1 tools/t.sh world_dump --part none        (builds it; prints nothing)
 *   build/world_dump [--map /home/user/driving-simulator/public/map] [--part all|net|tiles] > port.txt
 *   tools/diff/compare.py original.txt port.txt
 *
 * It follows tools/diff/world.ts line for line: what is printed, and in which
 * order, is described there.
 */
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "core/geom.h"
#include "core/js.h"
#include "platform/json.h"
#include "platform/native/native.h"
#include "platform/plat.h"
#include "world/far.h"
#include "world/grid.h"
#include "world/lanes.h"
#include "world/loader.h"
#include "world/mapfiles.h"
#include "world/netdiff.h"
#include "world/netfile.h"
#include "world/netindex.h"
#include "world/roadfeatures.h"
#include "world/routegraph.h"
#include "world/tilefmt.h"
#include "world/world.h"
#include "world/worldjson.h"

using namespace fs;

namespace {

// ---------------------------------------------------------------------------
// printing

std::string line;

void put(const std::string& text) {
  line += ' ';
  line += text;
}
void fmt(double v) { put(jsNumberText(v)); }
void fmt(float v) { put(jsNumberText(static_cast<double>(v))); }
void fmt(int v) { put(std::to_string(v)); }
void fmt(long v) { put(std::to_string(v)); }
void fmt(long long v) { put(std::to_string(v)); }
void fmt(unsigned v) { put(std::to_string(v)); }
void fmt(unsigned long v) { put(std::to_string(v)); }
void fmt(unsigned long long v) { put(std::to_string(v)); }
void fmt(bool v) { put(v ? "1" : "0"); }
void fmt(const std::string& v) {
  std::string s;
  bool space = false;
  for (char c : v) {
    const bool white = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    if (white) {
      if (!space) s += '_';
    } else {
      s += c;
    }
    space = white;
  }
  put(s.empty() ? "\"\"" : s);
}
void fmt(const char* v) { fmt(std::string(v)); }
/** (what is not there: null or undefined in the original) */
struct Nothing {};
void fmt(Nothing) { put("-"); }

template <class... T>
void out(const char* name, const T&... values) {
  line = name;
  (fmt(values), ...);
  std::fputs(line.c_str(), stdout);
  std::fputc('\n', stdout);
}

/** a number for a list of numbers that changes when one of them does, or their order: Σ v[i] · (i mod 13 + 1) */
template <class A>
double sumOf(const A& a) {
  double s = 0;
  for (size_t i = 0; i < a.size(); i++) s += static_cast<double>(a[i]) * static_cast<double>((i % 13) + 1);
  return s;
}
double sumOf(const SectionArray& a) {
  double s = 0;
  for (size_t i = 0; i < a.size(); i++) s += a.at(i) * static_cast<double>((i % 13) + 1);
  return s;
}
/** the same for a stride-3 polyline, taken around its first point (small numbers: nothing cancels) */
double lineSum(const std::vector<double>& p) {
  double s = 0;
  for (size_t i = 0; i + 2 < p.size(); i += 3) s += ((p[i] - p[0]) + 2 * (p[i + 1] - p[1]) + 3 * (p[i + 2] - p[2])) * static_cast<double>(((i / 3) % 13) + 1);
  return s;
}

/** `text.length` of the original: the UTF-16 units of a text kept as UTF-8 here */
size_t utf16Length(const std::string& text) {
  size_t n = 0;
  for (unsigned char c : text) {
    if ((c & 0xC0) == 0x80) continue;  // (not the first byte of a character)
    n += c >= 0xF0 ? 2 : 1;
  }
  return n;
}

int keysOf(const RoadSideJson& s) {
  int n = 0;
  for (bool has : {s.p.has_value(), s.po.has_value(), s.pw.has_value(), s.pm.has_value(), s.pq.has_value(), s.c.has_value(), s.cw.has_value(),
                   s.cr.has_value(), s.ca.has_value(), s.cs.has_value(), s.v.has_value(), s.k.has_value(), s.g.has_value(), s.el.has_value(),
                   s.vp.has_value(), s.vt.has_value(), s.pt.has_value(), s.ft.has_value(), s.fh.has_value(), s.fo.has_value(), s.wm.has_value()})
    if (has) n++;
  return n;
}

std::string argOf(int argc, char** argv, const char* name, const char* dflt) {
  const std::string flag = std::string("--") + name;
  for (int i = 1; i + 1 < argc; i++)
    if (flag == argv[i]) return argv[i + 1];
  return dflt;
}

// ---------------------------------------------------------------------------
// the map

struct Place {
  std::string name;
  double x;
  double z;
};

struct Dump {
  std::shared_ptr<MapFiles> files;
  std::shared_ptr<MapLoader> map;
  World* world = nullptr;
  std::unique_ptr<LaneGraph> lanes;
  std::shared_ptr<const NetData> net;
  std::unique_ptr<NetIndex> netIndex;
  std::vector<Place> places;

  PolySample tmp = newPolySample();
  SurfaceHit hit;
  PathFoot foot;

  void counts(const char* what) {
    int stubs = 0;
    for (const Node* n : world->allNodes)
      if (n->stub) stubs++;
    int laneCount = 0;
    for (const Lane* l : lanes->lanes)
      if (l) laneCount++;
    int signals = 0;
    for (const SignalControllerJson* c : world->signals)
      if (c) signals++;
    out("counts", what, world->allNodes.size(), stubs, world->allEdges.size(), laneCount, lanes->connectors.size(), world->crossings.size(), signals, world->nodes.size(), world->edges.size(),
        lanes->lanes.size());
    // the order things are kept in (it decides which lane and which path gets which number, and who is asked first)
    size_t i = 0;
    double edgeOrder = 0;
    for (const Edge* e : world->allEdges) edgeOrder += static_cast<double>((i++ % 97) + 1) * e->id;
    i = 0;
    double nodeOrder = 0;
    for (const Node* n : world->allNodes) nodeOrder += static_cast<double>((i++ % 97) + 1) * n->id;
    double crossingOrder = 0;
    for (i = 0; i < world->crossings.size(); i++) crossingOrder += static_cast<double>((i % 97) + 1) * (world->crossings[i]->e + world->crossings[i]->s);
    double pathOrder = 0;
    for (i = 0; i < lanes->connectors.size(); i++) pathOrder += static_cast<double>((i % 97) + 1) * lanes->connectors[i]->id;
    double signalSum = 0;
    for (i = 0; i < world->signals.size(); i++) {
      const SignalControllerJson* c = world->signals[static_cast<int64_t>(i)];
      if (!c) continue;
      signalSum += static_cast<double>(i) + c->x + 2 * c->z + c->groups + c->off + static_cast<double>(c->phases.size()) + (c->ped ? 1 : 0);
      for (const SignalPhaseJson& ph : c->phases) signalSum += ph.d + static_cast<double>(utf16Length(ph.s));
    }
    out("orders", edgeOrder, nodeOrder, crossingOrder, pathOrder, signalSum);
  }

  void dumpNetwork(const std::string& name, double px, double pz) {
    World& w = *world;
    LaneGraph& lg = *lanes;
    out("place", name, px, pz, map->ready(px, pz), map->progress(px, pz));
    counts("at");

    // --- every road: sums over all of them (in the order of their numbers), every 25th in full
    {
      struct {
        double len = 0, width = 0, margin = 0, kerb = 0, cut = 0, blend = 0, box = 0, speed = 0, flags = 0, lanes = 0, pts = 0, xings = 0, calm = 0, narrow = 0, turns = 0, side = 0;
      } sum;
      for (size_t id = 0; id < w.edges.size(); id++) {
        const Edge* ep = w.edges[static_cast<int64_t>(id)];
        if (!ep) continue;
        const Edge& e = *ep;
        sum.len += e.len;
        sum.width += e.width + e.laneW + e.swL + 2 * e.swR;
        sum.margin += e.margin + 2 * e.marginL + 3 * e.marginR;
        sum.kerb += e.kerbL + 2 * e.kerbR;
        sum.cut += e.cutA + 2 * e.cutB;
        sum.blend += e.blendA + e.blendB;
        sum.box += e.maxX - e.minX + (e.maxZ - e.minZ);
        sum.speed += e.maxF + 2 * e.maxB + 3 * e.zone;
        sum.flags += e.flags + e.access * 4096 + e.surf * 16384 + e.cls * 65536 + e.median + e.mouth + e.layer * 7 + e.tier;
        sum.lanes += e.lanesF + 3 * e.lanesB;
        sum.pts += static_cast<double>(e.pl.cum.size()) + lineSum(e.pl.pts);
        sum.xings += static_cast<double>(e.xings.size());
        for (const CrossingJson& x : e.xings) sum.xings += x.s + x.k * 3 + x.w + (x.isl ? 1 : 0) + x.sc.value_or(-1) + x.g.value_or(-1) + x.e;
        if (e.calm) sum.calm += static_cast<double>(e.calm->size()) + sumOf(*e.calm) + calmLift(e, e.len / 2);
        if (e.narrow)
          for (const Narrowing& n : *e.narrow) sum.narrow += 1 + n.s0 + n.s1 + n.at + n.side + n.depth + n.lo + n.hi + (n.single ? 1 : 0) + n.flags;
        sum.turns += static_cast<double>(utf16Length(e.turnF) + 2 * utf16Length(e.turnB) + utf16Length(e.name) + utf16Length(e.ref));
        sum.side += (e.xl ? 1 + keysOf(*e.xl) : 0) + (e.xr ? 100 + keysOf(*e.xr) : 0);
      }
      out("edgesum", sum.len, sum.width, sum.margin, sum.kerb, sum.cut, sum.blend, sum.box, sum.speed, sum.flags, sum.lanes, sum.pts, sum.xings, sum.calm, sum.narrow, sum.turns, sum.side);
      for (size_t id = 0; id < w.edges.size(); id += 25) {
        const Edge* ep = w.edges[static_cast<int64_t>(id)];
        if (!ep) continue;
        const Edge& e = *ep;
        const std::vector<double>& p = e.pl.pts;
        const size_t n = e.pl.cum.size();
        out("edge", id, e.cls, e.len, e.width, e.lanesF, e.lanesB, e.laneW, e.maxF, e.maxB, e.zone, e.surf, e.flags, e.access, e.cutA, e.cutB, e.layer, e.margin, e.marginL, e.marginR, e.median, e.kerbL,
            e.kerbR, e.mouth, e.swL, e.swR, e.tier, e.blendA, e.blendB, e.a->id, e.b->id, static_cast<long long>(e.osm), n, p[0], p[1], p[2], p[n * 3 - 3], p[n * 3 - 2], p[n * 3 - 1], lineSum(p), e.minX,
            e.maxX, e.minZ, e.maxZ, e.xings.size());
      }
    }

    // --- every lane: sums, and every 25th in full
    {
      struct {
        double off = 0, len = 0, stop = 0, limit = 0, links = 0, ends = 0;
      } sum;
      for (size_t id = 0; id < lg.lanes.size(); id++) {
        const Lane* lp = lg.lanes[static_cast<int64_t>(id)];
        if (!lp) continue;
        const Lane& l = *lp;
        sum.off += l.off + l.width;
        sum.len += l.len + l.s0 + 2 * l.s1;
        sum.stop += l.stopAt;
        sum.limit += std::isfinite(l.limit) ? l.limit : 1000;
        sum.links += static_cast<double>(l.out.size() + 3 * l.in.size()) + l.idx + 5 * l.count + l.dir;
        sum.ends += l.edge->id + 2 * l.from->id + 3 * l.to->id + l.armTo->idx + 7 * l.armFrom->idx;
      }
      out("lanesum", sum.off, sum.len, sum.stop, sum.limit, sum.links, sum.ends);
      for (size_t id = 0; id < lg.lanes.size(); id += 25) {
        const Lane* lp = lg.lanes[static_cast<int64_t>(id)];
        if (!lp) continue;
        const Lane& l = *lp;
        lanePoint(l, l.len / 2, tmp);
        out("lane", id, l.edge->id, l.dir, l.idx, l.count, l.off, l.width, l.s0, l.s1, l.len, l.from->id, l.to->id, l.armTo->edge->id, l.armFrom->edge->id, l.limit, l.stopAt, l.out.size(), l.in.size(),
            tmp.x, tmp.y, tmp.z, tmp.tx, tmp.tz);
      }
    }

    // --- the paths through the junctions: sums over all, and every 10th junction in full
    lg.resolveAll();
    {
      struct {
        double len = 0, turn = 0, pts = 0, conflicts = 0, at = 0, yields = 0, merges = 0, siblings = 0, until = 0;
      } sum;
      for (const Connector* c : lg.connectors) {
        sum.len += c->len;
        sum.turn += c->turn + 1;
        sum.pts += static_cast<double>(c->pl.cum.size()) + lineSum(c->pl.pts);
        sum.conflicts += static_cast<double>(c->conflicts.size());
        for (const Conflict& k : c->conflicts) {
          sum.at += k.sSelf + 2 * k.sOther;
          if (k.yield) sum.yields++;
          if (k.merge) sum.merges++;
        }
        sum.siblings += static_cast<double>(c->siblings.size());
        for (const Sibling& s : c->siblings) sum.until += s.until + 2 * s.untilOther;
      }
      out("pathsum", sum.len, sum.turn, sum.pts, sum.conflicts, sum.at, sum.yields, sum.merges, sum.siblings, sum.until);
      for (size_t id = 0; id < w.nodes.size(); id += 10) {
        const Node* np = w.nodes[static_cast<int64_t>(id)];
        if (!np) continue;
        const Node& n = *np;
        const JunctionShape* sh = n.shape.get();
        // (the area of the junction is taken around the junction: around the map's origin the big numbers would swallow the small ones)
        std::vector<double> local;
        if (sh)
          for (size_t i = 0; i < sh->ring.size(); i++) local.push_back(sh->ring[i] - (i % 2 == 0 ? n.x : n.z));
        out("node", id, n.x, n.y, n.z, n.gx, n.gz, n.ctrl, n.sc, n.dark, n.stub, n.tc, static_cast<long long>(n.osm), n.arms.size(), n.fr.size(), sumOf(n.fr), sh ? static_cast<long>(sh->ring.size()) : -1L,
            sh ? ringArea(local) : 0.0, sh ? sh->corners.size() : static_cast<size_t>(0), sh ? sh->minX : 0.0, sh ? sh->maxX : 0.0, sh ? sh->minZ : 0.0, sh ? sh->maxZ : 0.0);
        if (sh) {
          const std::vector<double>& r = sh->ring;
          double s = 0;
          for (size_t i = 0; i + 1 < r.size(); i += 2) s += ((r[i] - n.x) + 2 * (r[i + 1] - n.z)) * static_cast<double>(((i / 2) % 13) + 1);
          double k = 0;
          for (const JunctionCorner& c : sh->corners) k += static_cast<double>(c.kerb.size()) + c.swA + 2 * c.swB + c.arc0 + 3 * c.arc1 + (c.ox - n.x) * (c.r > 0 ? 1 : 0) + (c.oz - n.z) * (c.r > 0 ? 1 : 0) + c.r;
          out("shape", id, s, k);
        }
        for (const Arm* a : n.arms) out("arm", id, a->idx, a->edge->id, a->out, a->role, a->sg, a->lamp, a->stopS, a->cut, a->dx, a->dz, a->ang, a->half, a->swL, a->swR, a->isApproach, a->isExit);
        if (n.stub) continue;
        for (const Connector* c : lg.pathsThrough(n)) {
          out("path", c->id, c->from->id, c->to->id, c->turn, c->len, c->pl.cum.size(), lineSum(c->pl.pts), c->conflicts.size(), c->siblings.size());
          for (const Conflict& k : c->conflicts) out("conflict", c->id, k.other->id, k.sSelf, k.sOther, k.merge, k.yield);
          for (const Sibling& s : c->siblings) out("sibling", c->id, s.other->id, s.until, s.untilOther);
        }
      }
    }

    // --- the surface: points on and beside every 7th road
    {
      for (size_t id = 0; id < w.edges.size(); id += 7) {
        const Edge* ep = w.edges[static_cast<int64_t>(id)];
        if (!ep) continue;
        const Edge& e = *ep;
        for (double at : {0.03, 0.5, 0.97}) {
          samplePolyline(e.pl, e.len * at, tmp);
          // left of the direction (tx, tz) is (tz, -tx)
          for (double side : {0.0, e.width / 2 - 0.3, -(e.width / 2 + 0.8), e.width / 2 + e.swL + 2.5}) {
            const double x = tmp.x + tmp.tz * side;
            const double z = tmp.z - tmp.tx * side;
            const bool found = w.surfaceAt(x, z, tmp.y + 0.6, hit);
            out("surface", id, at, found, found ? hit.y : 0.0, hit.edge ? hit.edge->id : -1, hit.node ? hit.node->id : -1, found ? hit.s : 0.0, found ? hit.off : 0.0, hit.zone, found ? hit.surf : 0);
          }
          const double x = tmp.x + tmp.tz * 1.1;
          const double z = tmp.z - tmp.tx * 1.1;
          const bool located = w.locate(x, z, tmp.y, 6, hit);
          out("locate", id, at, located, located ? hit.edge->id : -1, located ? hit.s : 0.0, located ? hit.off : 0.0, located ? hit.y : 0.0, located ? hit.zone : 0);
          const bool near = w.nearestRoad(tmp.x + tmp.tz * (e.width / 2 + 9), tmp.z - tmp.tx * (e.width / 2 + 9), 30, 0, hit);
          out("nearest", id, at, near, near ? hit.edge->id : -1, near ? hit.s : 0.0, near ? hit.off : 0.0, near ? hit.y : 0.0, near ? hit.zone : 0);
          const double d = lg.nearestPath(x, z, tmp.y, 4, foot);
          out("nearpath", id, at, d, std::isfinite(d) ? foot.x : 0.0, std::isfinite(d) ? foot.z : 0.0);
          const PathFoot* post = lg.besideLanes(x, z, JS_NAN, 1.4, foot);
          out("beside", id, at, post != nullptr, post ? foot.x : 0.0, post ? foot.z : 0.0, post ? foot.d : 0.0);
          out("wall", id, at, lg.wallInLane(x - 3, z - 2, x + 3, z + 2, tmp.y, 0.8), lg.wallInLane(x + tmp.tz * 30, z - tmp.tx * 30, x + tmp.tz * 34, z - tmp.tx * 34, JS_NAN, 0.8));
        }
      }
      double inBox = 0;
      double segs = 0;
      double nodesIn = 0;
      w.edgesInBox(px - 300, pz - 300, px + 300, pz + 300, [&](Edge& e) { inBox += e.id + 1; });
      w.segmentsInBox(px - 300, pz - 300, px + 300, pz + 300, [&](Edge& e, int seg) { segs += static_cast<double>(e.id) * 3 + seg + 1; });
      w.nodesInBox(px - 300, pz - 300, px + 300, pz + 300, [&](Node& n) { nodesIn += n.id + 1; });
      out("inbox", inBox, segs, nodesIn);
    }

    // --- ways through the lanes that are loaded: from the lanes of the lowest roads to the highest
    {
      std::vector<int> ids;
      for (size_t id = 0; id < w.edges.size(); id++)
        if (w.edges[static_cast<int64_t>(id)]) ids.push_back(static_cast<int>(id));
      auto pick = [&](double k) -> Edge* { return w.edges[ids[static_cast<size_t>(std::floor(static_cast<double>(ids.size() - 1) * k))]]; };
      struct Pair {
        double from, to;
        bool forTraffic;
        int goalDir;
      };
      for (const Pair& pair : {Pair{0.1, 0.9, false, 0}, Pair{0.3, 0.6, true, 0}, Pair{0.5, 0.2, false, 1}, Pair{0.8, 0.4, true, -1}}) {
        const LaneGraph::EdgeLanes* el = lg.edgeLanes[pick(pair.from)->id];
        const Lane* start = !el->fwd.empty() ? el->fwd[0] : !el->back.empty() ? el->back[0] : nullptr;
        const Edge* goal = pick(pair.to);
        if (!start) {
          out("laneroute", pick(pair.from)->id, goal->id, -2);
          continue;
        }
        const std::vector<Connector*> r = lg.route(*start, *goal, pair.forTraffic, pair.goalDir);
        double len = 0;
        line = "laneroute";
        fmt(pick(pair.from)->id);
        fmt(goal->id);
        fmt(r.empty() ? -1 : static_cast<int>(r.size()));
        for (const Connector* c : r) len += c->len + c->to->len;
        fmt(len);
        for (size_t i = 0; i < r.size() && i < 10; i++) fmt(r[i]->id);
        std::puts(line.c_str());
      }
    }
    std::fflush(stdout);
  }
};

Task<void> run(std::string mapDir, std::string part) {
  Dump d;
  d.files = diskFiles(mapDir);
  d.map = co_await MapLoader::open(d.files);
  MapLoader& map = *d.map;
  World& world = map.world;
  d.world = &world;
  d.lanes = std::make_unique<LaneGraph>(world);
  LaneGraph& lanes = *d.lanes;
  // (as src/sim/follow.ts keeps the lanes in step with the network)
  MapListener listener;
  listener.added = [&lanes](const WorldChange& c) {
    for (Edge* e : c.edges) lanes.addEdge(*e);
    for (Node* n : c.nodes) lanes.completeNode(*n);
  };
  listener.removed = [&lanes](const WorldChange& c) {
    for (Node* n : c.nodes) lanes.removeNode(*n);
    for (Edge* e : c.edges) lanes.removeEdge(*e);
  };
  map.listen(listener);

  MapIndexJson index;
  {
    std::optional<Bytes> buf = co_await d.files->bytes("map.json");
    if (buf) jsonParse(jsonTextOf(*buf), index);
  }
  out("map", map.version, map.pieces, map.tiles.x0, map.tiles.z0, map.tiles.size, world.meta.name, world.meta.minX, world.meta.maxX, world.meta.minZ, world.meta.maxZ, world.meta.originE, world.meta.originN);
  out("index", index.nodes, index.edges, GridSet::from(index.tiles).count(), GridSet::from(index.chunks).count(), world.json.places.size(), world.json.spawns.size());
  if (world.json.yard) out("yard", world.json.yard->x, world.json.yard->z, world.json.yard->len, world.json.yard->wid, world.json.yard->rot);
  else out("yard", Nothing(), Nothing(), Nothing(), Nothing(), Nothing());
  {
    double s = 0;
    for (const SpawnJson& p : world.json.spawns) s += p.x + 2 * p.z + 3 * p.yaw + (p.rough ? 1 : 0) + static_cast<double>(utf16Length(p.name)) + static_cast<double>(utf16Length(p.g)) + static_cast<double>(utf16Length(p.kind));
    for (const PlaceJson& p : world.json.places) s += p.x - p.z + static_cast<double>(utf16Length(p.name)) + static_cast<double>(utf16Length(p.kind));
    out("places", s);
  }

  d.net = co_await map.net();
  const NetData& net = *d.net;
  d.netIndex = std::make_unique<NetIndex>(d.net);
  NetIndex& netIndex = *d.netIndex;

  /** where things are looked at: Baunatal, the middle of Kassel, a point on a motorway */
  d.places = {{"Baunatal", 0, 0}, {"Kassel", 6500, -7600}};
  {
    size_t e = 0;
    while (e < net.edgeA.size() && !(net.edgeCls[e] == 0 && net.edgeLen[e] > 500)) e++;
    d.places.push_back({"motorway", net.nodeX[net.edgeA[e]], net.nodeZ[net.edgeA[e]]});
  }
  const std::vector<Place>& places = d.places;

  if (part == "all" || part == "net") {
    for (const Place& place : places) {
      const double x = place.x;
      const double z = place.z;
      co_await map.loadAround(x, z);
      // (what lies far behind goes, one chunk with each call)
      for (int i = 0; i < 60; i++) map.update(x, z, JS_INF);
      d.dumpNetwork(place.name, x, z);
      // six kilometres on: chunks are added in front and dropped behind
      co_await map.loadAround(x + 6000, z);
      for (int i = 0; i < 60; i++) map.update(x + 6000, z, JS_INF);
      d.counts("moved");
      out("ready", map.ready(x + 6000, z), map.ready(x, z), map.progress(x, z, 9000));
      // the lanes of what is loaded now made anew, all at once: in the order the world keeps its roads and junctions
      {
        LaneGraph again(world);
        double laneSum = 0;
        for (const Lane* l : again.lanes) laneSum += static_cast<double>(l->id) * 7 + l->edge->id;
        double pathSum = 0;
        for (const Connector* c : again.connectors) pathSum += static_cast<double>(c->id) * 3 + c->from->id + 2.0 * c->to->id + c->node->id;
        out("regraph", again.lanes.size(), again.connectors.size(), laneSum, pathSum);
      }
      std::fflush(stdout);
    }
  }

  // ---------------------------------------------------------------------------
  // the network of the whole map: ways, and what is where

  if (part == "all" || part == "net") {
    const RouteGraph& g = netIndex.graph;
    out("net", net.nodeX.size(), net.edgeA.size(), net.to.size(), net.poly.size(), g.n, fingerprint(net));
    out("netsum", sumOf(net.nodeX), sumOf(net.nodeZ), sumOf(net.edgeLen), sumOf(net.edgeCls), sumOf(net.wayKind), sumOf(net.waySpeed), sumOf(net.turn), sumOf(net.across));
    out("graphsum", sumOf(g.len), sumOf(g.secs), sumOf(g.across), sumOf(g.x0), sumOf(g.z1), sumOf(g.firstIn), sumOf(g.from), sumOf(g.inTurn), sumOf(netIndex.main));
    const std::vector<std::pair<double, double>> spots = {{0, 0}, {6500, -7600}, {places[2].x, places[2].z}, {-40000, 30000}, {40000, -30000}, {20000, 45000}, {-55000, -20000}, {744, -272}};
    auto usable = [&](int e) { return netIndex.main[static_cast<size_t>(e) * 2] == 1 || netIndex.main[static_cast<size_t>(e) * 2 + 1] == 1; };
    std::vector<int> wayAt;
    for (const auto& [x, z] : spots) {
      const std::optional<NetSpot> any = netIndex.nearest(x, z, 500);
      const std::optional<NetSpot> spot = netIndex.nearest(x, z, 30000, usable);
      out("spot", x, z, any ? any->edge : -1, any ? any->d : 0.0, spot ? spot->edge : -1, spot ? spot->x : 0.0, spot ? spot->z : 0.0, spot ? spot->yaw : 0.0, spot ? spot->d : 0.0);
      wayAt.push_back(spot ? spot->edge * 2 + (netIndex.main[static_cast<size_t>(spot->edge) * 2] == 1 ? 0 : 1) : -1);
      double n = 0;
      double s = 0;
      netIndex.edgesInBox(x - 1500, z - 800, x + 1500, z + 800, [&](int e) {
        n++;
        s += e;
      });
      double nb = 0;
      double sb = 0;
      netIndex.edgesInBox(
          x - 5000, z - 5000, x + 5000, z + 5000,
          [&](int e) {
            nb++;
            sb += e;
          },
          true);
      out("box", n, s, nb, sb);
      if (spot) {
        const std::vector<double> p = netPolyline(net, spot->edge);
        double ps = 0;
        for (size_t i = 0; i + 1 < p.size(); i += 2) ps += ((p[i] - p[0]) + 2 * (p[i + 1] - p[1])) * static_cast<double>(i / 2 + 1);
        struct {
          double n = 0, s = 0;
          void moveTo(double px, double pz) {
            n++;
            s += px - 2 * pz;
          }
          void lineTo(double px, double pz) {
            n++;
            s += px + 3 * pz;
          }
        } path;
        netIndex.trace(spot->edge, path);
        out("course", spot->edge, p.size(), ps, path.n, path.s);
      }
    }
    const std::vector<std::pair<int, int>> pairs = {{0, 1}, {1, 2}, {3, 4}, {5, 6}, {7, 3}, {2, 5}};
    for (const auto& [a, b] : pairs) {
      const int start = wayAt[static_cast<size_t>(a)];
      const int goal = wayAt[static_cast<size_t>(b)];
      const std::optional<std::vector<int>> ways = start >= 0 && goal >= 0 ? quickest(g, start, goal) : std::nullopt;
      double secs = 0;
      double len = 0;
      if (ways) {
        for (int w : *ways) {
          secs += static_cast<double>(g.secs[static_cast<size_t>(w)]);
          len += static_cast<double>(g.len[static_cast<size_t>(w)]);
        }
      }
      line = "route";
      fmt(start);
      fmt(goal);
      fmt(ways ? static_cast<int>(ways->size()) : -1);
      fmt(secs);
      fmt(len);
      if (ways)
        for (size_t i = 0; i < ways->size() && i < 10; i++) fmt((*ways)[i] >> 1);
      std::puts(line.c_str());
      const std::optional<std::vector<int>> toEdge = start >= 0 && goal >= 0 ? quickestToEdge(g, start, goal >> 1) : std::nullopt;
      out("routeto", toEdge ? static_cast<int>(toEdge->size()) : -1, toEdge && !toEdge->empty() ? toEdge->back() : -1);
    }
    std::fflush(stdout);
  }

  // ---------------------------------------------------------------------------
  // tiles, and the far view

  if (part == "all" || part == "tiles") {
    const std::vector<std::pair<double, double>> tileAt = {{0, 0}, {6500, -7600}, {places[2].x, places[2].z}, {688.22, -294.77}};
    for (const auto& [x, z] : tileAt) {
      const int ix = gridIndex(x, TILE);
      const int iz = gridIndex(z, TILE);
      std::optional<Bytes> buf = co_await d.files->bytes("tiles/" + std::to_string(ix) + "_" + std::to_string(iz) + ".fsb");
      out("tile", ix, iz, map.tiles.has(ix, iz), buf.has_value());
      if (!buf) continue;
      UnpackedTile tile = unpackTile(std::move(*buf));
      const TileHeader& h = tile.header;
      out("header", h.v, h.ix, h.iz, h.cx, h.cz, h.yBase, h.size, h.terrainN, h.terrainMin, h.terrainScale, h.coverN, h.signs.size(), h.objects.size(), h.clr.value_or(-1), h.sections.size());
      for (const auto& [name, info] : h.sections) {
        const SectionArray* a = tile.get(name);
        out("section", name, sectionTypeName(info.t), info.n, info.c, info.s, a->size(), sumOf(*a));
      }
      for (const char* name : {"bldg_a16", "trees", "collPts", "nothing"}) {
        const SectionArray* a = tile.get(name);
        out("derived", name, a ? static_cast<long>(a->size()) : -1L, a ? sumOf(*a) : 0.0);
      }
      const SectionArray* hgt = tile.get("hgt");
      for (const auto& [dx, dz] : std::vector<std::pair<double, double>>{{0, 0}, {123.4, -56.7}, {-249.99, 249.99}, {250, -250}, {17.3, 201.9}}) out("ground", tileGround(h, hgt->u16, h.cx + dx, h.cz + dz));
      double s = 0;
      for (const SignPost& p : h.signs) {
        size_t chars = 0;
        for (const std::string& code : p.s) chars += utf16Length(code);
        s += p.x + 2 * p.y + 3 * p.z + p.yaw + p.h + p.big + static_cast<double>(p.s.size()) + static_cast<double>(chars);
      }
      for (const TileObject& o : h.objects) s += o.x + 2 * o.y + 3 * o.z + o.yaw.value_or(0) + static_cast<double>(utf16Length(o.k)) + static_cast<double>(utf16Length(o.p));
      out("things", s);
    }
    const std::shared_ptr<const Bytes> farBuf = co_await map.far();
    const std::optional<FarGround> far = farBuf ? parseFar(*farBuf) : std::nullopt;
    out("far", far.has_value(), far ? far->minX : 0, far ? far->minZ : 0, far ? far->cell : 0, far ? far->w : 0, far ? far->h : 0, far ? far->base : 0.0, far ? far->step : 0.0, far ? sumOf(far->heights) : 0.0,
        far ? sumOf(far->cover) : 0.0, far && far->buildings ? sumOf(*far->buildings) : -1.0);
    if (far) {
      for (const auto& [x, z] : std::vector<std::pair<double, double>>{{0, 0}, {6500, -7600}, {-64999, -45999}, {12345.6, 23456.7}, {58999, 59999}, {1e7, 0}}) out("farat", farHeight(*far, x, z), farBuilt(*far, x, z));
    }
    // (small things the map layer leans on)
    Rng r = rng(20261124);
    const double r1 = r();
    const double r2 = r();
    const double r3 = r();
    out("rng", r1, r2, r3, hash01(0), hash01(1), hash01(-5), hash01(123456789), hash01(4294967296.0 + 7));
    std::fflush(stdout);
  }
}

}  // namespace

int main(int argc, char** argv) {
  const std::string mapDir = argOf(argc, argv, "map", "/home/user/driving-simulator/public/map");
  const std::string part = argOf(argc, argv, "part", "all");
  try {
    plat::native::wait(run(mapDir, part));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "world_dump: %s\n", e.what());
    return 1;
  }
  return 0;
}

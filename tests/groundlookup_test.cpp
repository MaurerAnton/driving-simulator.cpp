// SPDX-License-Identifier: AGPL-3.0-only
// LINK: world core platform geo
// Origin: tests/world/groundlookup.test.ts @ v1.7.7 (the part about the road network)
/**
 * The ground lookup made cheaper in 1.7.0 gives exactly what it gave before,
 * bit for bit: `World::surfaceAt` with the bounding box of each segment in the
 * segment grid (it turns away the far segments of a cell before projecting
 * onto them).  The old implementation is kept here as the reference; both are
 * asked the same many thousands of questions – on synthetic towns and on the
 * real map (Baunatal centre with its raised tables, humps, flush mouths,
 * parking strips and junction corners, and a rural area; skipped without the
 * map) – also while pieces of the network come and go in between.
 *
 * (The original's test goes on to the terrain tiles of the renderer –
 * TileManager.heightAt / coverAt: that part belongs to the render layer.)
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/geom.h"
#include "platform/native/native.h"
#include "tests/town.h"
#include "world/loader.h"
#include "world/mapfiles.h"
#include "world/roadfeatures.h"
#include "world/seggrid.h"
#include "world/world.h"
#include "world/xsection.h"

namespace fs {
/** what the test sees of the inside of a world: its grids */
struct WorldInside {
  static const SegGrid& segGrid(const World& w) { return w.segGrid_; }
  static const Grid2<Node*>& nodeGrid(const World& w) { return w.nodeGrid_; }
};
}  // namespace fs

using namespace fs;

namespace {

// ---------------------------------------------------------------------------
// the reference: World.surfaceAt as it was before the segment boxes (a copy)

/** Is (x,z) on the sidewalk strip behind a junction kerb line?  (copy of world.cpp) */
bool refPointNearKerb(double x, double z, const JunctionCorner& c) {
  const std::vector<double>& k = c.kerb;
  const size_t n = k.size() / 2;
  double best = JS_INF;
  double bestT = 0;
  double side = 0;
  double total = 0;
  double acc = 0;
  for (size_t i = 0; i + 1 < n; i++) total += jsHypot(k[i * 2 + 2] - k[i * 2], k[i * 2 + 3] - k[i * 2 + 1]);
  if (total < 1e-6) return false;
  for (size_t i = 0; i + 1 < n; i++) {
    const double ax = k[i * 2];
    const double az = k[i * 2 + 1];
    const double dx = k[i * 2 + 2] - ax;
    const double dz = k[i * 2 + 3] - az;
    const double l2 = dx * dx + dz * dz;
    const double l = std::sqrt(l2);
    double t = l2 > 1e-9 ? ((x - ax) * dx + (z - az) * dz) / l2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    const double ex = x - (ax + dx * t);
    const double ez = z - (az + dz * t);
    const double d2 = ex * ex + ez * ez;
    if (d2 < best) {
      best = d2;
      bestT = (acc + l * t) / total;
      side = ex * dz - ez * dx;
    }
    acc += l;
  }
  if (side >= 0) return false;
  const double w = c.swA + (c.swB - c.swA) * bestT;
  return best <= w * w;
}

/** The old `surfaceAt` over the same (private) grids of a world. */
class ReferenceSurface {
 public:
  explicit ReferenceSurface(const World& world) : w(world) {}

  bool operator()(double x, double z, double yRef, SurfaceHit& out) {
    bestFound = false;
    out.edge = nullptr;
    out.node = nullptr;
    out.zone = 2;
    const double ceiling = yRef + 1.6;
    const std::vector<Node*>& nodes = WorldInside::nodeGrid(w).at(x, z);
    for (size_t i = 0; i < nodes.size(); i++) {
      Node* node = nodes[i];
      const JunctionShape& sh = *node->shape;
      if (x < sh.minX || x > sh.maxX || z < sh.minZ || z > sh.maxZ) continue;
      if (!pointInRing(x, z, sh.ring)) continue;
      const double y = w.planeY(*node, x, z);
      if (y > ceiling) continue;
      consider(out, y, -50, nullptr, node, 0, 0, 0, SURF_ASPHALT);
    }
    const std::vector<int64_t>& segs = WorldInside::segGrid(w).at(x, z);
    for (size_t i = 0; i < segs.size(); i++) {
      const int64_t code = segs[i];
      Edge* edge = w.edges[code / 4096];
      if (x < edge->minX || x > edge->maxX || z < edge->minZ || z > edge->maxZ) continue;
      const int seg = static_cast<int>(code % 4096);
      projectOnPolyline(edge->pl, x, z, tmpProj, seg, seg);
      const double s = tmpProj.s;
      const double off = tmpProj.off;
      const double absOff = std::fabs(off);
      const double half = edge->width / 2;
      const double sw = off > 0 ? edge->swL : edge->swR;
      const double reach = half + sw;
      if (absOff > reach) continue;
      const std::vector<double>& cum = edge->pl.cum;
      const bool clampedStart = s <= cum[static_cast<size_t>(seg)] + 1e-6;
      const bool clampedEnd = s >= cum[static_cast<size_t>(seg) + 1] - 1e-6;
      double lateral = absOff;
      if (clampedStart || clampedEnd) {
        const bool interior = clampedStart ? seg > 0 : seg < static_cast<int>(cum.size()) - 2;
        if (interior) {
          if (tmpProj.d2 > reach * reach) continue;
          lateral = std::sqrt(tmpProj.d2);
        } else if (tmpProj.d2 > absOff * absOff + 0.04) {
          continue;
        }
      }
      const bool inJunctionZone = s < edge->cutA || s > edge->len - edge->cutB;
      if (inJunctionZone && lateral > half) continue;
      double y = w.edgeY(*edge, s, x, z);
      const bool onSidewalk = lateral > half;
      if (edge->calm && !onSidewalk) y += calmLift(*edge, s);
      if (onSidewalk) y += off > 0 ? edge->kerbL : edge->kerbR;
      if (y > ceiling) continue;
      consider(out, y, lateral - half + (onSidewalk ? 2 : 0) + (inJunctionZone ? 1 : 0), edge, nullptr, s, off, onSidewalk ? 1 : 0, onSidewalk ? SURF_PAVING : edge->surf);
    }
    if (!bestFound || out.zone != 0) {
      for (size_t i = 0; i < nodes.size(); i++) {
        Node* node = nodes[i];
        const JunctionShape& sh = *node->shape;
        const double pad = 3.5;
        if (x < sh.minX - pad || x > sh.maxX + pad || z < sh.minZ - pad || z > sh.maxZ + pad) continue;
        for (size_t c = 0; c < sh.corners.size(); c++) {
          const JunctionCorner& corner = sh.corners[c];
          if (corner.swA <= 0 && corner.swB <= 0) continue;
          if (!refPointNearKerb(x, z, corner)) continue;
          const double y = w.planeY(*node, x, z) + KERB_H;
          if (y > ceiling) continue;
          consider(out, y, 3, nullptr, node, 0, 0, 1, SURF_PAVING);
        }
      }
    }
    return bestFound;
  }

 private:
  const World& w;
  PolyProjection tmpProj;
  double bestY = 0;
  double bestScore = 0;
  bool bestFound = false;

  void consider(SurfaceHit& out, double y, double score, Edge* edge, Node* node, double s, double off, int zone, int surf) {
    if (bestFound) {
      const double dy = y - bestY;
      if (dy < -1.5) return;
      if (dy <= 1.5 && score >= bestScore) return;
    }
    bestFound = true;
    bestY = y;
    bestScore = score;
    out.y = y;
    out.edge = edge;
    out.node = node;
    out.s = s;
    out.off = off;
    out.zone = zone;
    out.surf = surf;
  }
};

// ---------------------------------------------------------------------------
// asking both the same

/** `Object.is` for numbers: the same number, a zero with the same sign */
bool is(double a, double b) { return (a == b && std::signbit(a) == std::signbit(b)) || (a != a && b != b); }

/** how many points were compared (all tests) */
long compared = 0;

/**
 * Compare the new and the old lookup on the points [x, z, yRef, …] – each
 * with its own result object kept from call to call, as the game keeps one
 * (fields a miss leaves alone must be left alone alike).  Returns how many
 * were on a road.
 */
long compare(World& world, const std::vector<double>& pts, const char* label) {
  ReferenceSurface ref(world);
  SurfaceHit a = newSurfaceHit();
  SurfaceHit b = newSurfaceHit();
  long on = 0;
  for (size_t i = 0; i + 2 < pts.size(); i += 3) {
    const double x = pts[i];
    const double z = pts[i + 1];
    const double yRef = pts[i + 2];
    const bool ra = world.surfaceAt(x, z, yRef, a);
    const bool rb = ref(x, z, yRef, b);
    if (ra != rb || !is(a.y, b.y) || a.edge != b.edge || a.node != b.node || !is(a.s, b.s) || !is(a.off, b.off) || a.zone != b.zone || a.surf != b.surf) {
      std::fprintf(stderr, "%s: (%.17g, %.17g, %.17g) found %d y %.17g s %.17g off %.17g zone %d, before %d y %.17g s %.17g off %.17g zone %d\n", label, x, z, yRef, ra, a.y, a.s, a.off, a.zone, rb, b.y, b.s, b.off,
                   b.zone);
      assert(false);
    }
    if (ra) on++;
  }
  compared += static_cast<long>(pts.size() / 3);
  return on;
}

PolySample tmpS = newPolySample();

/**
 * Points that probe the edges of what a road accepts: along each edge, across
 * the carriageway, the kerb, the sidewalk and beyond it; exactly at the
 * borders; around the bends and past the open ends; with heights around the
 * road's own (decks, the ceiling) and far above.
 */
template <class Edges>
void probeEdges(const Edges& edges, Rng& rand, int perEdge, std::vector<double>& out) {
  for (const Edge* ep : edges) {
    const Edge& e = *ep;
    const double half = e.width / 2;
    const double swMax = jsMax(e.swL, e.swR);
    const std::vector<double>& cum = e.pl.cum;
    for (int k = 0; k < perEdge; k++) {
      const double s = -4 + rand() * (e.len + 8);
      samplePolyline(e.pl, jsMax(0, jsMin(e.len, s)), tmpS);
      const double lx = tmpS.tz;
      const double lz = -tmpS.tx;
      const double side = rand() < 0.5 ? 1 : -1;
      const double sw = side > 0 ? e.swL : e.swR;
      const double r = rand();
      // across the whole width, or exactly on the borders (± a hair)
      const double borders[] = {half, half + sw, half + swMax, half + swMax + 0.5, half + sw + 0.2};
      double off;
      if (r < 0.6) {
        off = side * rand() * (half + swMax + 2);
      } else {
        const double border = borders[jsInt32(rand() * 5)];
        off = side * (border + (rand() - 0.5) * 1e-9);
      }
      const double along = s < 0 ? s : s > e.len ? s - e.len : 0;
      const double x = tmpS.x + lx * off + tmpS.tx * along;
      const double z = tmpS.z + lz * off + tmpS.tz * along;
      const double yRef = rand() < 0.1 ? 300 : tmpS.y + (rand() - 0.5) * 6;
      out.push_back(x);
      out.push_back(z);
      out.push_back(yRef);
    }
    // round the vertices: at the reach and just beyond it
    for (size_t i = 0; i < cum.size(); i++) {
      const double vx = e.pl.pts[i * 3];
      const double vy = e.pl.pts[i * 3 + 1];
      const double vz = e.pl.pts[i * 3 + 2];
      // (the list is made first, as the original makes it: its last distance is drawn before the angles)
      const double ds[] = {half + e.swL, half + e.swR, half + swMax + 0.19, half + swMax + 0.21, half + swMax + 0.5, rand() * (half + swMax + 1)};
      for (double d : ds) {
        const double a = rand() * PI * 2;
        out.push_back(vx + std::cos(a) * d);
        out.push_back(vz + std::sin(a) * d);
        out.push_back(vy + (rand() - 0.5) * 3);
      }
    }
  }
}

/** Points around junction areas and their kerbed corners. */
template <class Nodes>
void probeNodes(const Nodes& nodes, Rng& rand, int perNode, std::vector<double>& out) {
  for (const Node* np : nodes) {
    const Node& n = *np;
    const JunctionShape* sh = n.shape.get();
    if (!sh) continue;
    for (int k = 0; k < perNode; k++) {
      const double x = sh->minX - 5 + rand() * (sh->maxX - sh->minX + 10);
      const double z = sh->minZ - 5 + rand() * (sh->maxZ - sh->minZ + 10);
      const double y = n.y + (rand() - 0.5) * 4;
      out.push_back(x);
      out.push_back(z);
      out.push_back(y);
    }
    for (const JunctionCorner& c : sh->corners) {
      const std::vector<double>& kerb = c.kerb;
      for (size_t i = 0; i + 1 < kerb.size(); i += 2) {
        const double r = rand() * jsMax(jsMax(c.swA, c.swB), 0.5) * 1.3;
        const double a = rand() * PI * 2;
        const double y = n.y + (rand() - 0.5) * 2;
        out.push_back(kerb[i] + std::cos(a) * r);
        out.push_back(kerb[i + 1] + std::sin(a) * r);
        out.push_back(y);
      }
    }
  }
}

void randomPoints(double cx, double cz, double half, double y, int n, Rng& rand, std::vector<double>& out) {
  for (int i = 0; i < n; i++) {
    const double x = cx + (rand() * 2 - 1) * half;
    const double z = cz + (rand() * 2 - 1) * half;
    const double yy = y + (rand() - 0.5) * 40;
    out.push_back(x);
    out.push_back(z);
    out.push_back(yy);
  }
}

// ---------------------------------------------------------------------------
// synthetic towns

/**
 * A grid town given what the real streets have: bends (and a doubled point),
 * pavements of different widths on the two sides, parking strips and lowered
 * or flush kerbs, raised tables and humps, a road crossing over another, and
 * mouths over a continuous pavement.
 */
WorldJson variedTown() {
  TownOptions o;
  o.cols = 5;
  o.rows = 4;
  o.block = 90;
  o.pavement = 2;
  o.deadEnd = 70;
  Town town = makeTown(o);
  WorldJson json = town.json;
  Rng rand = rng(7);
  for (size_t i = 0; i < json.edges.size(); i++) {
    EdgeJson& e = json.edges[i];
    // a bend in the middle (and a point given twice: a segment of length zero)
    const std::vector<double> p = e.p;
    const size_t n = p.size() / 3;
    if (n == 2 && i % 3 != 2) {
      const double ax = p[0], ay = p[1], az = p[2], bx = p[3], by = p[4], bz = p[5];
      const double dx = bx - ax;
      const double dz = bz - az;
      const double l = jsHypot(dx, dz);
      const double bend = (rand() - 0.5) * 12;
      std::vector<double> pts = {ax, ay, az};
      for (double t : {0.3, 0.5, 0.7}) {
        const double k = std::sin(PI * t) * bend;
        pts.push_back(ax + dx * t + (dz / l) * k);
        pts.push_back(ay + (by - ay) * t + (i % 4 == 0 ? 3 * std::sin(PI * t) : 0));
        pts.push_back(az + dz * t - (dx / l) * k);
        if (i % 5 == 0 && t == 0.5) {
          const double x3 = pts[pts.size() - 3], y3 = pts[pts.size() - 2], z3 = pts[pts.size() - 1];
          pts.push_back(x3);
          pts.push_back(y3);
          pts.push_back(z3);
        }
      }
      pts.push_back(bx);
      pts.push_back(by);
      pts.push_back(bz);
      e.p = pts;
    }
    const double sl[] = {0, 1.5, 2.5, 4.2};
    const double sr[] = {3, 0, 1.8, 2.2};
    e.sl = sl[i % 4];
    e.sr = sr[(i >> 1) % 4];
    if (i % 3 == 0) {
      RoadSideJson side;
      side.p = PARK_STREET_SIDE;
      side.k = 1;
      e.xl = std::make_shared<const RoadSideJson>(side);
    }
    if (i % 4 == 1) {
      RoadSideJson side;
      side.p = PARK_HALF_ON_KERB;
      side.k = 2;
      side.v = 1.2;
      e.xr = std::make_shared<const RoadSideJson>(side);
    }
    if (i % 3 == 1) e.cm = std::vector<double>{20, CALM_TABLE, 4, 0.08, 45, CALM_HUMP, 3.7, 0.08};
    if (i % 6 == 2) e.cm = std::vector<double>{30, CALM_BUMP, 0.6, 0.06};
    if (i % 7 == 3) e.mk = 1;
    if (i % 5 == 4) e.md = 1;
  }
  // a road on a bridge across the town, at 8 m above it
  const int top = static_cast<int>(json.nodes.size());
  for (const auto& [x, z] : {std::pair<double, double>{-40, 125}, std::pair<double, double>{400, 145}}) {
    NodeJson n;
    n.x = x;
    n.y = 108;
    n.z = z;
    n.t = 0;
    ArmJson arm;
    arm.e = static_cast<int>(json.edges.size());
    arm.r = 0;
    n.arms = {arm};
    json.nodes.push_back(n);
  }
  EdgeJson bridge = json.edges[0];
  bridge.a = top;
  bridge.b = top + 1;
  bridge.p = {-40, 108, 125, 180, 109, 135, 400, 108, 145};
  bridge.w = 7;
  bridge.sl = 1.5;
  bridge.sr = 0;
  bridge.cm.reset();
  bridge.mk = 0;
  bridge.xl.reset();
  bridge.xr.reset();
  bridge.md = 0;
  bridge.x.clear();
  bridge.bn.clear();
  json.edges.push_back(bridge);
  computeAllCuts(json);
  return json;
}

/** Its junctions sorted into pieces (as the map build cuts a map into chunks), each with every road at them. */
std::vector<WorldChunkJson> pieces(const WorldJson& json, int n) {
  std::vector<WorldChunkJson> out(static_cast<size_t>(n));
  for (size_t i = 0; i < json.nodes.size(); i++) {
    const NodeJson& node = json.nodes[i];
    WorldChunkJson& c = out[static_cast<size_t>(std::min(n - 1, std::max(0, static_cast<int>(std::floor(((node.x + 50) / 500) * n)))))];
    c.nodes.emplace_back(static_cast<int>(i), node);
    for (const ArmJson& arm : node.arms) {
      bool have = false;
      for (const auto& e : c.edges) have = have || e.first == arm.e;
      if (have) continue;
      EdgeJson e = json.edges[static_cast<size_t>(arm.e)];
      e.q = packEdgePoints(e.p);
      e.p.clear();
      c.edges.emplace_back(arm.e, std::move(e));
    }
  }
  return out;
}

WorldJson emptyOf(const WorldJson& json) {
  WorldJson e = json;
  e.nodes.clear();
  e.edges.clear();
  e.banned.clear();
  e.signals.clear();
  e.crossings.clear();
  return e;
}

std::vector<double> townPoints(World& world, uint32_t seed) {
  Rng rand = rng(seed);
  std::vector<double> pts;
  randomPoints(180, 140, 260, 104, 6000, rand, pts);
  probeEdges(world.allEdges, rand, 60, pts);
  probeNodes(world.allNodes, rand, 30, pts);
  return pts;
}

// ---------------------------------------------------------------------------
// the real map

const double CENTRE[2] = {690, -295};
const double RURAL[2] = {-9000, 9000};

struct Near {
  std::vector<Edge*> edges;
  std::vector<Node*> nodes;
};

Near near(World& world, double x, double z, double r) {
  Near out;
  for (Edge* e : world.allEdges)
    if (e->maxX > x - r && e->minX < x + r && e->maxZ > z - r && e->minZ < z + r) out.edges.push_back(e);
  for (Node* n : world.allNodes)
    if (n->shape && std::fabs(n->x - x) < r && std::fabs(n->z - z) < r) out.nodes.push_back(n);
  return out;
}

/** Points over an area of the real map: anywhere, and on the roads with what the detail region gave them. */
std::vector<double> realPoints(World& world, double x, double z, double r, uint32_t seed, double scale = 1) {
  Rng rand = rng(seed);
  const Near found = near(world, x, z, r);
  std::vector<double> pts;
  const double y = !found.nodes.empty() ? found.nodes[0]->y : 200;
  randomPoints(x, z, r, y, static_cast<int>(jsRound(8000 * scale)), rand, pts);
  // the special ones: tables and humps, flush mouths, parking strips, kerbs of other heights, medians, uneven pavements
  std::vector<Edge*> special;
  for (Edge* e : found.edges)
    if (e->calm || e->mouth || e->xl || e->xr || e->median || e->swL != e->swR) special.push_back(e);
  probeEdges(special, rand, static_cast<int>(jsRound(40 * scale)), pts);
  std::vector<Edge*> third;
  for (size_t i = 0; i < found.edges.size(); i += 3) third.push_back(found.edges[i]);
  probeEdges(third, rand, static_cast<int>(jsRound(8 * scale)), pts);
  std::vector<Node*> some;
  for (Node* n : found.nodes) {
    bool mouth = false;
    for (const Arm* a : n->arms) mouth = mouth || a->edge->mouth;
    if (mouth || n->id % 2 == 0) some.push_back(n);
  }
  probeNodes(some, rand, static_cast<int>(jsRound(10 * scale)), pts);
  return pts;
}

std::string mapDirOf() {
  const char* env = std::getenv("MAP_DIR");
  // (the map is not part of the port: docs/PORT.md says where the current one lies)
  return env && *env ? env : "/home/user/driving-simulator/public/map";
}

Task<void> realMap(std::string dir) {
  const long before0 = compared;
  std::shared_ptr<MapLoader> mapPtr = co_await MapLoader::open(diskFiles(dir));
  MapLoader& map = *mapPtr;
  World& world = map.world;
  co_await map.loadAround(CENTRE[0], CENTRE[1], 300);
  {
    const std::vector<Edge*> special = near(world, CENTRE[0], CENTRE[1], 2000).edges;
    bool calm = false, mouth = false, parking = false;
    for (const Edge* e : special) {
      calm = calm || e->calm;
      mouth = mouth || e->mouth;
      parking = parking || (e->xl && e->xl->p.value_or(0)) || (e->xr && e->xr->p.value_or(0));
    }
    assert(calm);     // tables or humps near the centre
    assert(mouth);    // mouths over a continuous pavement near the centre
    assert(parking);  // parking strips near the centre
  }
  for (const Edge* e : world.allEdges) assert(e->pl.cum.size() - 1 < 4096);  // segment codes stay apart
  // the centre and what the detail region gave its roads
  long on = compare(world, realPoints(world, CENTRE[0], CENTRE[1], 2000, 21), "centre");
  assert(on > 5000);
  on = compare(world, realPoints(world, CENTRE[0], CENTRE[1], 250, 22), "centre (close)");
  // a rural area
  co_await map.loadAround(RURAL[0], RURAL[1], 300);
  on = compare(world, realPoints(world, RURAL[0], RURAL[1], 1500, 23), "rural");
  assert(on > 300);
  // drop what is far from the rural area, one chunk at a time, asking in between
  map.keepRadius = 1000;
  map.loadRadius = 300;
  const size_t before = world.allEdges.size();
  for (int k = 0; k < 12; k++) {
    map.update(RURAL[0], RURAL[1], 4);
    co_await plat::sleep(0);
    const std::string label = "centre while dropping (" + std::to_string(k) + ")";
    compare(world, realPoints(world, CENTRE[0], CENTRE[1], 600, static_cast<uint32_t>(30 + k), 0.15), label.c_str());
  }
  assert(world.allEdges.size() < before);  // pieces were dropped
  // and back to the centre: its pieces come again, a portion at a time (roads first, junctions after)
  for (int k = 0; k < 400; k++) {
    map.update(CENTRE[0], CENTRE[1], 0);
    co_await plat::sleep(k < 5 ? 50 : 0);
    if (k % 4 == 0) {
      const std::string label = "centre while loading (" + std::to_string(k) + ")";
      compare(world, realPoints(world, CENTRE[0], CENTRE[1], 400, static_cast<uint32_t>(60 + k), 0.08), label.c_str());
    }
    if (map.ready(CENTRE[0], CENTRE[1], 300) && k > 20) break;
  }
  co_await map.loadAround(CENTRE[0], CENTRE[1], 300);
  compare(world, realPoints(world, CENTRE[0], CENTRE[1], 2000, 99), "centre again");
  compare(world, realPoints(world, RURAL[0], RURAL[1], 1500, 98, 0.3), "rural again");
  std::printf("groundlookup: %ld points compared on the real map\n", compared - before0);
}

}  // namespace

int main() {
  // --- surfaceAt with segment boxes: the same as before on a varied town, in one piece
  {
    const WorldJson json = variedTown();
    World world(json);
    const std::vector<double> pts = townPoints(world, 1);
    const long on = compare(world, pts, "town");
    assert(on > static_cast<long>(pts.size() / 3 / 4));  // enough of the points on a road
  }

  // --- surfaceAt with segment boxes: the same as before while pieces of the town come and go
  {
    const WorldJson json = variedTown();
    const std::vector<WorldChunkJson> parts = pieces(json, 4);
    World world(emptyOf(json));
    Rng rand = rng(3);
    std::vector<std::vector<int>> ids;
    for (const WorldChunkJson& p : parts) {
      ids.emplace_back();
      for (const auto& n : p.nodes) ids.back().push_back(n.first);
    }
    // roads first, junctions later – as the loader adds them – and pieces dropped and taken again
    const std::vector<std::pair<std::string, int>> steps = {
        {"edges", 1}, {"nodes", 1}, {"edges", 2}, {"edges", 0}, {"nodes", 2}, {"nodes", 0}, {"drop", 1},  {"edges", 3}, {"nodes", 3}, {"drop", 0},
        {"drop", 2},  {"edges", 1}, {"nodes", 1}, {"edges", 0}, {"nodes", 0}, {"drop", 3},  {"edges", 2}, {"nodes", 2}, {"edges", 3}, {"nodes", 3},
    };
    for (const auto& [what, k] : steps) {
      const WorldChunkJson& part = parts[static_cast<size_t>(k)];
      if (what == "edges") {
        WorldChunkJson c = part;
        c.nodes.clear();
        world.add(c);
      } else if (what == "nodes") {
        WorldChunkJson c = part;
        c.edges.clear();
        c.signals.clear();
        world.add(c);
      } else {
        world.remove(ids[static_cast<size_t>(k)]);
      }
      std::vector<double> pts;
      randomPoints(180, 140, 260, 104, 1500, rand, pts);
      probeEdges(world.allEdges, rand, 15, pts);
      probeNodes(world.allNodes, rand, 10, pts);
      const std::string label = "town after " + what + " " + std::to_string(k);
      compare(world, pts, label.c_str());
    }
    // all in again: the same as the town in one piece
    World whole(json);
    const std::vector<double> pts = townPoints(whole, 9);
    SurfaceHit a = newSurfaceHit();
    SurfaceHit b = newSurfaceHit();
    for (size_t i = 0; i + 2 < pts.size(); i += 3) {
      const bool ra = world.surfaceAt(pts[i], pts[i + 1], pts[i + 2], a);
      const bool rb = whole.surfaceAt(pts[i], pts[i + 1], pts[i + 2], b);
      assert(ra == rb);
      assert(is(a.y, b.y) && is(a.s, b.s) && is(a.off, b.off) && a.zone == b.zone && (a.edge ? a.edge->id : -1) == (b.edge ? b.edge->id : -1) && (a.node ? a.node->id : -1) == (b.node ? b.node->id : -1));
    }
  }

  // --- a segment box (in its cell of the grid) holds everything the segment can give
  {
    // what the old lookup (no boxes) found on a segment lies in that segment's box: the farthest it reaches
    // is half + the sidewalk of that side, √(reach² + 0.04) past an open end
    const WorldJson json = variedTown();
    World world(json);
    const SegGrid& grid = WorldInside::segGrid(world);
    Rng rand = rng(11);
    SurfaceHit hit = newSurfaceHit();
    ReferenceSurface ref(world);
    long checked = 0;
    for (Edge* e : world.allEdges) {
      for (int k = 0; k < 400; k++) {
        const double s = -1 + rand() * (e->len + 2);
        samplePolyline(e->pl, jsMax(0, jsMin(e->len, s)), tmpS);
        const double off = (rand() * 2 - 1) * (e->width / 2 + jsMax(e->swL, e->swR) + 0.5);
        const double x = tmpS.x + tmpS.tz * off + tmpS.tx * (s - jsMax(0, jsMin(e->len, s)));
        const double z = tmpS.z - tmpS.tx * off + tmpS.tz * (s - jsMax(0, jsMin(e->len, s)));
        if (!ref(x, z, 1000, hit) || hit.edge != e) continue;
        // the segment it was found on lies around it
        PolyProjection p;
        const SegCell* cell = grid.cellAt(x, z);
        assert(cell && cell->boxes.size() == cell->codes.size() * 4);
        bool inside = false;
        for (size_t i = 0; i + 1 < e->pl.cum.size(); i++) {
          projectOnPolyline(e->pl, x, z, p, static_cast<int>(i), static_cast<int>(i));
          if (std::fabs(p.s - hit.s) > 1e-9 || p.off != hit.off) continue;
          const int64_t code = static_cast<int64_t>(e->id) * 4096 + static_cast<int64_t>(i);
          long j = -1;
          for (size_t c = 0; c < cell->codes.size() && j < 0; c++)
            if (cell->codes[c] == code) j = static_cast<long>(c);
          const std::vector<double>& b = cell->boxes;
          inside = inside || (j >= 0 && x >= b[static_cast<size_t>(j) * 4] && z >= b[static_cast<size_t>(j) * 4 + 1] && x <= b[static_cast<size_t>(j) * 4 + 2] && z <= b[static_cast<size_t>(j) * 4 + 3]);
        }
        assert(inside);
        checked++;
      }
    }
    assert(checked > 1000);
  }

  // --- the segment grid holds its codes as Grid2 did (same cells, same order), each with its own box
  {
    Rng rand = rng(17);
    SegGrid sg(40);
    Grid2<int64_t> g2(40);
    // (the codes that are in, in the order they were put in: the original draws one of them by its place in that order)
    std::vector<int64_t> order;
    std::map<int64_t, std::array<double, 4>> placed;
    auto boxOf = [](int64_t code) -> std::array<double, 4> { return {code * 0.5, -static_cast<double>(code), code + 0.25, code * 3.0}; };
    for (int step = 0; step < 6000; step++) {
      if (!order.empty() && rand() < 0.45) {
        const size_t at = static_cast<size_t>(jsInt32(rand() * static_cast<double>(order.size())));
        const int64_t code = order[at];
        const std::array<double, 4> box = placed[code];
        sg.removeBox(box[0], box[1], box[2], box[3], code);
        g2.removeBox(box[0], box[1], box[2], box[3], code);
        placed.erase(code);
        order.erase(order.begin() + static_cast<long>(at));
      } else {
        const int64_t road = jsInt32(rand() * 300);
        const int64_t code = road * 4096 + jsInt32(rand() * 50);
        if (placed.count(code)) continue;
        const double x0 = (rand() - 0.5) * 400;
        const double z0 = (rand() - 0.5) * 400;
        const double x1 = x0 + rand() * 90;
        const double z1 = z0 + rand() * 90;
        const std::array<double, 4> b = boxOf(code);
        sg.insertBox(x0, z0, x1, z1, code, b[0], b[1], b[2], b[3]);
        g2.insertBox(x0, z0, x1, z1, code);
        placed[code] = {x0, z0, x1, z1};
        order.push_back(code);
      }
      if (step % 50 == 0) {
        for (double x = -260; x <= 260; x += 40) {
          for (double z = -260; z <= 260; z += 40) {
            assert(sg.at(x, z) == g2.at(x, z));
            const SegCell* cell = sg.cellAt(x, z);
            if (!cell) continue;
            assert(cell->boxes.size() == cell->codes.size() * 4);
            for (size_t i = 0; i < cell->codes.size(); i++) {
              const std::array<double, 4> b = boxOf(cell->codes[i]);
              assert(cell->boxes[i * 4] == b[0] && cell->boxes[i * 4 + 1] == b[1] && cell->boxes[i * 4 + 2] == b[2] && cell->boxes[i * 4 + 3] == b[3]);
            }
            std::vector<int64_t> a;
            std::vector<int64_t> b;
            sg.queryBox(x - 50, z - 30, x + 20, z + 70, [&](int64_t c) { a.push_back(c); });
            g2.queryBox(x - 50, z - 30, x + 20, z + 70, [&](int64_t c) { b.push_back(c); });
            assert(a == b);
          }
        }
      }
    }
  }
  const long synthetic = compared;

  // --- surfaceAt with segment boxes: the same as before on the real map, while its pieces come and go
  const std::string dir = mapDirOf();
  if (std::ifstream(dir + "/map.json").good()) {
    plat::native::wait(realMap(dir));
  } else {
    std::printf("groundlookup: no map in %s – the part on the real map is skipped\n", dir.c_str());
  }

  std::printf("groundlookup: all checks passed (%ld points compared on the towns)\n", synthetic);
  return 0;
}

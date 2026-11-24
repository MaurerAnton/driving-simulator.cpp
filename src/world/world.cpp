// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/world.ts @ v1.7.7
/**
 * Runtime road network: linked nodes/edges/arms, junction geometry, spatial
 * index and the analytic road-surface function (see world.h).
 */
#include "world/world.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "world/roadfeatures.h"

namespace fs {

namespace {

PolySample tmpSample = newPolySample();
PolyProjection tmpProj;

void computeArmDirection(Arm& arm) {
  const Edge& e = *arm.edge;
  // Fixed probe distance: must not depend on the cut, otherwise the pipeline
  // (which solves the cuts) and the runtime would see different corner geometry.
  const double probe = clamp(8, 0.5, e.len);
  armPoint(arm, probe, tmpSample);
  double dx = tmpSample.x - arm.node->x;
  double dz = tmpSample.z - arm.node->z;
  const double l = jsHypot(dx, dz);
  if (l < 1e-6) {
    dx = 1;
    dz = 0;
  } else {
    dx /= l;
    dz /= l;
  }
  arm.dx = dx;
  arm.dz = dz;
  arm.ang = angleOf(dx, dz);
}

/** Desired kerb radius between two arms (before length limits). */
double desiredFilletRadius(const Arm& a, const Arm& b) {
  // (a mouth over a continuous pavement has no kerbed corners: src/world/roadfeatures.h)
  if (flushMouth(a) || flushMouth(b)) return 0.6;
  const double t = jsMin(a.edge->tier, b.edge->tier);
  double r = t >= 3 ? 8 : t >= 2 ? 5.5 : t >= 1 ? 3.5 : 3;
  const double sw = jsMax(a.swL, b.swR);
  if (sw > 0) r = jsMax(r, sw + 0.6);
  return r;
}

struct CornerSolve {
  /** distances from the node (along each arm) to the sharp corner, or -1 */
  double t = -1;
  double u = -1;
  double cx = 0;
  double cz = 0;
  double phi = 0;
};

CornerSolve solveCorner(const Arm& a, const Arm& b, const Node& n) {
  const double phi = wrap2Pi(b.ang - a.ang);
  CornerSolve res;
  res.phi = phi;
  if (phi < 0.12 || phi > 3.0) return res;  // ~7° … ~172°
  // left edge of a: P + left_a * half_a ; left of (dx,dz) is (dz,-dx)
  const double ax = n.x + a.dz * a.half;
  const double az = n.z - a.dx * a.half;
  // right edge of b: P - left_b * half_b
  const double bx = n.x - b.dz * b.half;
  const double bz = n.z + b.dx * b.half;
  const LineHit r = lineLine(ax, az, a.dx, a.dz, bx, bz, b.dx, b.dz);
  if (!r) return res;
  res.t = r.t;
  res.u = r.u;
  res.cx = ax + a.dx * r.t;
  res.cz = az + a.dz * r.t;
  return res;
}

/** Mouth corner of an arm: the ribbon edge point at the cut station. side = +1 left, -1 right (looking away from node). */
void mouthPoint(const Arm& arm, double side, double out[2]) {
  armPoint(arm, arm.cut, tmpSample);
  // tangent of the polyline points a→b; flip so it points away from the node
  const double tx = arm.out ? tmpSample.tx : -tmpSample.tx;
  const double tz = arm.out ? tmpSample.tz : -tmpSample.tz;
  out[0] = tmpSample.x + tz * arm.half * side;
  out[1] = tmpSample.z - tx * arm.half * side;
}

/** Is (x,z) on the sidewalk strip behind a junction kerb line? */
bool pointNearKerb(double x, double z, const JunctionCorner& c) {
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
      // The kerb is part of the counter-clockwise junction ring, so the
      // carriageway is on its left and the block (sidewalk) on its RIGHT.
      // left-of-(dx,dz) is (dz,-dx): positive = left.
      side = ex * dz - ez * dx;
    }
    acc += l;
  }
  if (side >= 0) return false;
  const double w = c.swA + (c.swB - c.swA) * bestT;
  return best <= w * w;
}

}  // namespace

PolySample& armPoint(const Arm& arm, double d, PolySample& out) {
  const Edge& e = *arm.edge;
  return samplePolyline(e.pl, arm.out ? d : e.len - d, out);
}

void computeJunctionCuts(Node& node) {
  const std::vector<Arm*>& arms = node.arms;
  const size_t n = arms.size();
  node.fr.assign(n, 0);
  for (Arm* arm : arms) arm->cut = 0;
  if (n < 2) return;
  for (Arm* arm : arms) computeArmDirection(*arm);

  std::vector<double> maxCut;
  maxCut.reserve(n);
  for (const Arm* arm : arms) {
    const Edge& e = *arm->edge;
    const Node& other = arm->out ? *e.b : *e.a;
    // an edge that ends in a dead end may be consumed almost entirely
    const double share = other.arms.size() <= 1 ? 0.8 : 0.45;
    maxCut.push_back(jsMax(0, e.len * share));
  }

  if (n == 2) {
    Arm& p = *arms[0];
    Arm& q = *arms[1];
    const bool same = std::fabs(p.half - q.half) < 0.05 && std::fabs(p.swL - q.swR) < 0.05 && std::fabs(p.swR - q.swL) < 0.05;
    const double bend = std::fabs(PI - wrap2Pi(q.ang - p.ang));
    if (same && bend < 0.6) return;  // smooth continuation: ribbons simply meet
    const double taper = bend >= 0.6 ? jsMax(p.half, q.half) * std::tan(jsMin(bend, 2.4) / 2) + 1 : 2;
    p.cut = jsMin(maxCut[0], taper);
    q.cut = jsMin(maxCut[1], taper);
    return;
  }

  for (size_t i = 0; i < n; i++) {
    const size_t j = (i + 1) % n;
    Arm& a = *arms[i];
    Arm& b = *arms[j];
    const CornerSolve c = solveCorner(a, b, node);
    if (c.t < 0 || c.u < 0) {
      // No convex corner (straight-through side or reflex gap): tiny cut only.
      a.cut = jsMax(a.cut, jsMin(maxCut[i], 0.6));
      b.cut = jsMax(b.cut, jsMin(maxCut[j], 0.6));
      continue;
    }
    const double tanHalf = std::tan(c.phi / 2);
    double r = desiredFilletRadius(a, b);
    // keep the tangent length sane on acute corners and inside short edges
    r = jsMin(r, 12 * tanHalf);
    const double room = jsMin(maxCut[i] - c.t, maxCut[j] - c.u);
    r = jsMin(r, jsMax(0, room) * tanHalf);
    if (r < 0.4) r = 0;
    node.fr[i] = r;
    const double tl = r > 0 ? r / tanHalf : 0;
    a.cut = jsMax(a.cut, jsMin(maxCut[i], c.t + tl + 0.05));
    b.cut = jsMax(b.cut, jsMin(maxCut[j], c.u + tl + 0.05));
  }
  for (size_t i = 0; i < n; i++) arms[i]->cut = clamp(arms[i]->cut, 0, maxCut[i]);
}

std::unique_ptr<JunctionShape> buildJunctionShape(const Node& node) {
  const std::vector<Arm*>& arms = node.arms;
  const size_t n = arms.size();
  if (n == 1 && node.tc > 0) {
    // turning circle at the end of a cul-de-sac
    auto shape = std::make_unique<JunctionShape>();
    const int steps = 20;
    for (int k = 0; k < steps; k++) {
      const double t = (static_cast<double>(k) / steps) * PI * 2;
      // counter-clockwise seen from above (north-up): x = cos, north = sin → z = -sin
      shape->ring.push_back(node.x + std::cos(t) * node.tc);
      shape->ring.push_back(node.z - std::sin(t) * node.tc);
    }
    shape->minX = node.x - node.tc;
    shape->maxX = node.x + node.tc;
    shape->minZ = node.z - node.tc;
    shape->maxZ = node.z + node.tc;
    return shape;
  }
  if (n < 2) return nullptr;
  bool any = false;
  for (const Arm* a : arms)
    if (a->cut > 1e-3) any = true;
  if (!any) return nullptr;

  auto shape = std::make_unique<JunctionShape>();
  std::vector<double>& ring = shape->ring;
  std::vector<JunctionCorner>& corners = shape->corners;
  double pt[2] = {0, 0};
  for (size_t i = 0; i < n; i++) {
    const Arm& a = *arms[i];
    const Arm& b = *arms[(i + 1) % n];
    mouthPoint(a, -1, pt);
    ring.push_back(pt[0]);
    ring.push_back(pt[1]);
    mouthPoint(a, +1, pt);
    const double lx = pt[0];
    const double lz = pt[1];
    mouthPoint(b, -1, pt);
    const double rx = pt[0];
    const double rz = pt[1];

    std::vector<double> kerb = {lx, lz};
    int arc0 = -1;
    int arc1 = -1;
    double ox = 0;
    double oz = 0;
    const double r = i < node.fr.size() ? node.fr[i] : 0;
    CornerSolve c;
    const bool solved = n > 2;
    if (solved) c = solveCorner(a, b, node);
    if (solved && c.t >= 0 && c.u >= 0) {
      if (r > 0) {
        const double tl = r / std::tan(c.phi / 2);
        const double tix = c.cx + a.dx * tl;
        const double tiz = c.cz + a.dz * tl;
        const double tjx = c.cx + b.dx * tl;
        const double tjz = c.cz + b.dz * tl;
        double bx = a.dx + b.dx;
        double bz = a.dz + b.dz;
        double bl = jsHypot(bx, bz);
        if (bl == 0 || bl != bl) bl = 1;
        bx /= bl;
        bz /= bl;
        const double od = r / std::sin(c.phi / 2);
        ox = c.cx + bx * od;
        oz = c.cz + bz * od;
        const double sweep = PI - c.phi;
        const double steps = jsMax(2, std::ceil(sweep / 0.21));
        const double a0 = std::atan2(tiz - oz, tix - ox);
        double a1 = std::atan2(tjz - oz, tjx - ox);
        double d = a1 - a0;
        while (d > PI) d -= 2 * PI;
        while (d < -PI) d += 2 * PI;
        a1 = a0 + d;
        arc0 = static_cast<int>(kerb.size() / 2);
        for (double k = 0; k <= steps; k++) {
          const double t = a0 + (d * k) / steps;
          kerb.push_back(ox + std::cos(t) * r);
          kerb.push_back(oz + std::sin(t) * r);
        }
        arc1 = static_cast<int>(kerb.size() / 2) - 1;
      } else {
        kerb.push_back(c.cx);
        kerb.push_back(c.cz);
      }
    }
    kerb.push_back(rx);
    kerb.push_back(rz);
    // drop consecutive duplicates
    std::vector<double> clean = {kerb[0], kerb[1]};
    int removedBeforeArc0 = 0;
    int removedBeforeArc1 = 0;
    for (size_t k = 2; k < kerb.size(); k += 2) {
      const double px = clean[clean.size() - 2];
      const double pz = clean[clean.size() - 1];
      if (jsHypot(kerb[k] - px, kerb[k + 1] - pz) < 0.02) {
        if (static_cast<int>(k / 2) <= arc0) removedBeforeArc0++;
        if (static_cast<int>(k / 2) <= arc1) removedBeforeArc1++;
        continue;
      }
      clean.push_back(kerb[k]);
      clean.push_back(kerb[k + 1]);
    }
    if (arc0 >= 0) {
      arc0 -= removedBeforeArc0;
      arc1 -= removedBeforeArc1;
    }
    for (size_t k = 0; k + 2 < clean.size(); k += 2) {
      ring.push_back(clean[k]);
      ring.push_back(clean[k + 1]);
    }
    JunctionCorner corner;
    corner.kerb = std::move(clean);
    corner.swA = a.swL;
    corner.swB = b.swR;
    corner.arc0 = arc0;
    corner.arc1 = arc1;
    corner.ox = ox;
    corner.oz = oz;
    corner.r = r;
    corners.push_back(std::move(corner));
  }

  double minX = JS_INF;
  double maxX = -JS_INF;
  double minZ = JS_INF;
  double maxZ = -JS_INF;
  for (size_t k = 0; k < ring.size(); k += 2) {
    minX = jsMin(minX, ring[k]);
    maxX = jsMax(maxX, ring[k]);
    minZ = jsMin(minZ, ring[k + 1]);
    maxZ = jsMax(maxZ, ring[k + 1]);
  }
  shape->minX = minX;
  shape->maxX = maxX;
  shape->minZ = minZ;
  shape->maxZ = maxZ;
  return shape;
}

std::vector<double> edgePoints(const std::vector<double>& q) {
  std::vector<double> p(q.size());
  double x = 0;
  double y = 0;
  double z = 0;
  for (size_t i = 0; i + 2 < q.size(); i += 3) {
    x += q[i];
    y += q[i + 1];
    z += q[i + 2];
    p[i] = x / 100;
    p[i + 1] = y / 100;
    p[i + 2] = z / 100;
  }
  return p;
}

std::vector<double> packEdgePoints(const std::vector<double>& p) {
  std::vector<double> q(p.size());
  double x = 0;
  double y = 0;
  double z = 0;
  for (size_t i = 0; i + 2 < p.size(); i += 3) {
    const double cx = jsRound(p[i] * 100);
    const double cy = jsRound(p[i + 1] * 100);
    const double cz = jsRound(p[i + 2] * 100);
    q[i] = cx - x;
    q[i + 1] = cy - y;
    q[i + 2] = cz - z;
    x = cx;
    y = cy;
    z = cz;
  }
  return q;
}

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------

World::World(WorldJson json_, WorldOptions opts)
    : json(std::move(json_)), meta(json.meta), lightsOut(opts.lightsOut ? std::move(opts.lightsOut) : std::function<bool(int)>([](int) { return false; })) {
  if (json.nodes.empty() && json.edges.empty()) return;
  // the same as one big piece: crossings and forbidden turns go with their edges
  std::unordered_map<int, std::vector<EdgeCrossingJson>> xings;
  for (const CrossingJson& c : json.crossings) xings[c.e].push_back(c);
  std::unordered_map<int, std::vector<int>> bans;
  for (const std::array<int, 3>& b : json.banned) {
    std::vector<int>& list = bans[b[0]];
    list.push_back(b[1]);
    list.push_back(b[2]);
  }
  WorldChange change;
  for (size_t i = 0; i < json.signals.size(); i++) signals.set(static_cast<int64_t>(i), std::make_unique<SignalControllerJson>(json.signals[i]));
  for (size_t i = 0; i < json.edges.size(); i++) {
    const int id = static_cast<int>(i);
    if (edges[id]) continue;
    auto x = xings.find(id);
    auto bn = bans.find(id);
    change.edges.push_back(addEdge(id, json.edges[i], x == xings.end() ? nullptr : &x->second, bn == bans.end() ? nullptr : &bn->second));
  }
  for (size_t i = 0; i < json.nodes.size(); i++) {
    const int id = static_cast<int>(i);
    const Node* have = nodes[id];
    if (have && !have->stub) continue;
    change.nodes.push_back(completeNode(id, json.nodes[i]));
  }
  // (the far ends of the new roads: which way they leave, for whoever looks)
  for (Edge* e : change.edges) {
    if (e->a->stub) computeArmDirection(*e->armA);
    if (e->b->stub) computeArmDirection(*e->armB);
  }
}

Node* World::stubNode(int id, const std::vector<double>& p, size_t at) {
  auto node = std::make_unique<Node>();
  node->id = id;
  // (a road without a course: the original reads `undefined` there)
  node->x = at < p.size() ? p[at] : JS_NAN;
  node->y = at + 1 < p.size() ? p[at + 1] : JS_NAN;
  node->z = at + 2 < p.size() ? p[at + 2] : JS_NAN;
  node->gx = 0;
  node->gz = 0;
  node->ctrl = CTRL_NONE;
  node->sc = -1;
  node->dark = false;
  node->tc = 0;
  node->osm = 0;
  node->stub = true;
  Node* made = nodes.set(id, std::move(node));
  allNodes.add(made);
  return made;
}

void World::newArm(Arm& arm, Node* node, Edge* edge, bool out) {
  arm.node = node;
  arm.edge = edge;
  arm.out = out;
  arm.idx = 0;
  arm.role = ROLE_NONE;
  arm.sg = -1;
  arm.lamp = -1;
  arm.stopS = out ? edge->cutA : edge->cutB;
  arm.cut = out ? edge->cutA : edge->cutB;
  arm.dx = 1;
  arm.dz = 0;
  arm.ang = 0;
  arm.half = edge->width / 2;
  arm.swL = out ? edge->swL : edge->swR;
  arm.swR = out ? edge->swR : edge->swL;
  arm.isApproach = out ? edge->lanesB > 0 : edge->lanesF > 0;
  arm.isExit = out ? edge->lanesF > 0 : edge->lanesB > 0;
}

Edge* World::addEdge(int id, const EdgeJson& ej, const std::vector<EdgeCrossingJson>* x, const std::vector<int>* bn) {
  std::vector<double> p = ej.q ? edgePoints(*ej.q) : ej.p;
  Node* a = nodes[ej.a];
  if (!a) a = stubNode(ej.a, p, 0);
  Node* b = nodes[ej.b];
  if (!b) b = stubNode(ej.b, p, p.size() - 3);
  auto made = std::make_unique<Edge>();
  Edge& e = *made;
  e.id = id;
  e.a = a;
  e.b = b;
  e.cls = ej.c;
  e.tier = ej.c >= 0 && ej.c < 15 ? CLASS_TIER[ej.c] : JS_NAN;
  e.name = ej.n;
  e.ref = ej.r;
  e.pl = makePolyline(std::move(p));
  e.len = e.pl.len;
  e.lanesF = ej.lf;
  e.lanesB = ej.lb;
  e.laneW = ej.lw;
  e.width = ej.w;
  e.margin = ej.mg;
  e.marginL = ej.mg + sideInner(ej.xl.get());
  e.marginR = ej.mg + sideInner(ej.xr.get());
  e.xl = ej.xl;
  e.xr = ej.xr;
  e.median = ej.md;
  e.kerbL = kerbHeight(ej.xl.get());
  e.kerbR = kerbHeight(ej.xr.get());
  e.maxF = ej.mf;
  e.maxB = ej.mb;
  e.zone = ej.z;
  e.surf = ej.s;
  e.swL = ej.sl;
  e.swR = ej.sr;
  e.flags = ej.f;
  e.access = ej.ac;
  e.cutA = ej.ca;
  e.cutB = ej.cb;
  e.layer = ej.ly;
  e.turnF = ej.tf;
  e.turnB = ej.tb;
  e.osm = ej.o;
  e.calm = ej.cm;
  e.mouth = ej.mk;
  e.blendA = 0;
  e.blendB = 0;
  e.minX = 0;
  e.maxX = 0;
  e.minZ = 0;
  e.maxZ = 0;
  const std::vector<EdgeCrossingJson>& xs = x ? *x : ej.x;
  e.xings.reserve(xs.size());
  for (const EdgeCrossingJson& one : xs) {
    CrossingJson c;
    static_cast<EdgeCrossingJson&>(c) = one;
    c.e = id;
    e.xings.push_back(c);
  }
  if (ej.bo) e.narrow = narrowingsOf(e, &*ej.bo);
  newArm(e.ends[0], a, &e, true);
  newArm(e.ends[1], b, &e, false);
  e.armA = &e.ends[0];
  e.armB = &e.ends[1];
  // (of a junction that is not loaded itself, the roads are kept in the order they came)
  if (a->stub) a->arms.push_back(e.armA);
  if (b->stub) b->arms.push_back(e.armB);
  Edge* kept = edges.set(id, std::move(made));
  allEdges.add(kept);
  indexEdge(*kept, true);
  for (CrossingJson& c : kept->xings) crossings.push_back(&c);
  const std::vector<int>& bans = bn ? *bn : ej.bn;
  if (bn || !ej.bn.empty()) banned_[id] = bans;
  return kept;
}

Node* World::completeNode(int id, const NodeJson& nj) {
  Node* node = nodes[id];
  if (!node) {
    auto made = std::make_unique<Node>();
    made->id = id;
    made->x = nj.x;
    made->y = nj.y;
    made->z = nj.z;
    made->ctrl = nj.t;
    made->stub = true;
    node = nodes.set(id, std::move(made));
    allNodes.add(node);
  }
  node->x = nj.x;
  node->y = nj.y;
  node->z = nj.z;
  node->gx = nj.gx;
  node->gz = nj.gz;
  node->ctrl = nj.t;
  node->sc = nj.sc;
  node->dark = nj.t == CTRL_SIGNALS && node->sc >= 0 && lightsOut(node->sc);
  node->fr = nj.fr;
  node->tc = nj.tc;
  node->osm = nj.o;
  node->stub = false;
  // the arms in their stored (counter-clockwise) order
  std::vector<Arm*> arms;
  arms.reserve(nj.arms.size());
  std::vector<const Edge*> seen;
  for (size_t k = 0; k < nj.arms.size(); k++) {
    const ArmJson& aj = nj.arms[k];
    Edge* edge = edges[aj.e];
    if (!edge) throw std::runtime_error("junction " + std::to_string(id) + ": its road " + std::to_string(aj.e) + " is not part of the piece it came with");
    // A loop edge (a === b) appears twice: first occurrence is the "out" end.
    const bool out = edge->a == node && !(edge->b == node && std::find(seen.begin(), seen.end(), edge) != seen.end());
    seen.push_back(edge);
    Arm* arm = out ? edge->armA : edge->armB;
    arm->idx = static_cast<int>(k);
    arm->role = aj.r;
    arm->lamp = aj.g;
    arm->sg = node->dark ? -1 : arm->lamp;
    arm->stopS = jsMax(aj.sl, arm->cut);
    arms.push_back(arm);
  }
  node->arms = std::move(arms);
  // Lights out of order: what the signs say counts (the map build gave every approach its role
  // before it put up the lights) – and where there are no signs, right before left.
  if (node->dark) {
    bool roundabout = false;
    bool rbl = false;
    for (const Arm* a : node->arms) {
      if ((a->edge->flags & EF_ROUNDABOUT) != 0) roundabout = true;
      if (a->isApproach && a->role == ROLE_RBL) rbl = true;
    }
    node->ctrl = roundabout ? CTRL_ROUNDABOUT : rbl ? CTRL_RBL : CTRL_SIGNS;
  }
  for (Arm* arm : node->arms) computeArmDirection(*arm);
  node->shape = buildJunctionShape(*node);
  if (node->shape) nodeGrid_.insertBox(node->shape->minX, node->shape->minZ, node->shape->maxX, node->shape->maxZ, node);
  return node;
}

WorldChange World::add(const WorldChunkJson& chunk) {
  WorldChange change;
  for (const auto& [id, c] : chunk.signals) signals.set(id, std::make_unique<SignalControllerJson>(c));
  for (const auto& [id, ej] : chunk.edges)
    if (!edges[id]) change.edges.push_back(addEdge(id, ej));
  for (const auto& [id, nj] : chunk.nodes) {
    const Node* have = nodes[id];
    if (have && !have->stub) continue;
    change.nodes.push_back(completeNode(id, nj));
  }
  // (the far ends of the new roads: which way they leave, for whoever looks)
  for (Edge* e : change.edges) {
    if (e->a->stub) computeArmDirection(*e->armA);
    if (e->b->stub) computeArmDirection(*e->armB);
  }
  return change;
}

WorldChange World::remove(const std::vector<int>& nodeIds) {
  // (what went the last time has been let go of by now)
  goneEdges_.clear();
  goneNodes_.clear();
  removals++;
  WorldChange change;
  OrderedSet<Node> touched;
  for (int id : nodeIds) {
    Node* node = nodes[id];
    if (!node || node->stub) continue;
    if (node->shape) nodeGrid_.removeBox(node->shape->minX, node->shape->minZ, node->shape->maxX, node->shape->maxZ, node);
    node->shape = nullptr;
    node->stub = true;
    change.nodes.push_back(node);
    touched.add(node);
  }
  // (the far ends of the roads that go are looked at as well: the set grows while it is gone through)
  for (auto it = touched.begin(); it != touched.end(); ++it) {
    Node* node = *it;
    const std::vector<Arm*> arms = node->arms;
    for (Arm* arm : arms) {
      Edge* e = arm->edge;
      if (!edges[e->id] || !e->a->stub || !e->b->stub) continue;
      // both ends are gone: the road goes
      indexEdge(*e, false);
      goneEdges_.push_back(edges.take(e->id));
      allEdges.erase(e);
      banned_.erase(e->id);
      for (CrossingJson& x : e->xings) {
        auto at = std::find(crossings.begin(), crossings.end(), &x);
        if (at != crossings.end()) {
          *at = crossings.back();
          crossings.pop_back();
        }
      }
      for (Arm* end : {e->armA, e->armB}) {
        std::vector<Arm*>& list = end->node->arms;
        auto at = std::find(list.begin(), list.end(), end);
        if (at != list.end()) list.erase(at);
        touched.add(end->node);
      }
      change.edges.push_back(e);
    }
  }
  for (Node* node : touched) {
    if (node->stub && node->arms.empty()) {
      if (nodes[node->id] == node) goneNodes_.push_back(nodes.take(node->id));
      allNodes.erase(node);
    }
  }
  return change;
}

void World::indexEdge(Edge& e, bool on) {
  const double usable = jsMax(0, e.len - e.cutA - e.cutB);
  e.blendA = jsMin(14, usable * 0.45);
  e.blendB = jsMin(14, usable * 0.45);
  const std::vector<double>& pts = e.pl.pts;
  const size_t n = e.pl.cum.size();
  const double pad = e.width / 2 + jsMax(e.swL, e.swR) + 1;
  const double segPad = e.width / 2 + jsMax(e.swL, e.swR) + 0.5;
  double minX = JS_INF;
  double maxX = -JS_INF;
  double minZ = JS_INF;
  double maxZ = -JS_INF;
  for (size_t i = 0; i < n; i++) {
    const double x = pts[i * 3];
    const double z = pts[i * 3 + 2];
    minX = jsMin(minX, x);
    maxX = jsMax(maxX, x);
    minZ = jsMin(minZ, z);
    maxZ = jsMax(maxZ, z);
    if (i + 1 < n) {
      const double x2 = pts[i * 3 + 3];
      const double z2 = pts[i * 3 + 5];
      const double x0 = jsMin(x, x2) - pad;
      const double z0 = jsMin(z, z2) - pad;
      const double x1 = jsMax(x, x2) + pad;
      const double z1 = jsMax(z, z2) + pad;
      const int64_t code = static_cast<int64_t>(e.id) * 4096 + static_cast<int64_t>(i);
      if (on) segGrid_.insertBox(x0, z0, x1, z1, code, jsMin(x, x2) - segPad, jsMin(z, z2) - segPad, jsMax(x, x2) + segPad, jsMax(z, z2) + segPad);
      else segGrid_.removeBox(x0, z0, x1, z1, code);
    }
  }
  e.minX = minX - pad;
  e.maxX = maxX + pad;
  e.minZ = minZ - pad;
  e.maxZ = maxZ + pad;
}

bool World::isBanned(int from, int via, int to) const {
  auto it = banned_.find(from);
  if (it == banned_.end()) return false;
  const std::vector<int>& list = it->second;
  for (size_t i = 0; i + 1 < list.size(); i += 2)
    if (list[i] == via && list[i + 1] == to) return true;
  return false;
}

double World::edgeY(const Edge& edge, double s, double x, double z) const {
  samplePolyline(edge.pl, s, tmpSample);
  double y = tmpSample.y;
  const double sa = s - edge.cutA;
  if (sa < edge.blendA) {
    const double w = edge.blendA > 0 ? 1 - smoothstep(0, edge.blendA, sa) : sa <= 0 ? 1 : 0;
    if (w > 0) y += (planeY(*edge.a, x, z) - y) * w;
  }
  const double sb = edge.len - s - edge.cutB;
  if (sb < edge.blendB) {
    const double w = edge.blendB > 0 ? 1 - smoothstep(0, edge.blendB, sb) : sb <= 0 ? 1 : 0;
    if (w > 0) y += (planeY(*edge.b, x, z) - y) * w;
  }
  return y;
}

void World::consider(SurfaceHit& out, double y, double score, Edge* edge, Node* node, double s, double off, int zone, int surf) {
  if (bestFound_) {
    const double dy = y - bestY_;
    if (dy < -1.5) return;
    if (dy <= 1.5 && score >= bestScore_) return;
  }
  bestFound_ = true;
  bestY_ = y;
  bestScore_ = score;
  out.y = y;
  out.edge = edge;
  out.node = node;
  out.s = s;
  out.off = off;
  out.zone = zone;
  out.surf = surf;
}

bool World::surfaceAt(double x, double z, double yRef, SurfaceHit& out) {
  bestFound_ = false;
  out.edge = nullptr;
  out.node = nullptr;
  out.zone = 2;
  const double ceiling = yRef + 1.6;

  // 1) junction carriageway polygons
  const std::vector<Node*>& nodesHere = nodeGrid_.at(x, z);
  for (size_t i = 0; i < nodesHere.size(); i++) {
    Node* node = nodesHere[i];
    const JunctionShape& sh = *node->shape;
    if (x < sh.minX || x > sh.maxX || z < sh.minZ || z > sh.maxZ) continue;
    if (!pointInRing(x, z, sh.ring)) continue;
    const double y = planeY(*node, x, z);
    if (y > ceiling) continue;
    consider(out, y, -50, nullptr, node, 0, 0, 0, SURF_ASPHALT);
  }

  // 2) edges: carriageway between the cuts, plus sidewalks
  const SegCell* cell = segGrid_.cellAt(x, z);
  const size_t segCount = cell ? cell->codes.size() : 0;
  for (size_t i = 0; i < segCount; i++) {
    // (the segment's own box first: most segments of a cell lie far away – see `segGrid`)
    const double* box = &cell->boxes[i * 4];
    if (x < box[0] || x > box[2] || z < box[1] || z > box[3]) continue;
    const int64_t code = cell->codes[i];
    const int64_t id = code / 4096;
    const int seg = static_cast<int>(code % 4096);
    Edge* edge = edges[id];
    if (x < edge->minX || x > edge->maxX || z < edge->minZ || z > edge->maxZ) continue;
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
    // lateral distance used for ranking: for a projection clamped to a
    // segment end it is the true distance to that vertex – the
    // perpendicular offset alone would make far-away segments look close
    double lateral = absOff;
    if (clampedStart || clampedEnd) {
      const bool interior = clampedStart ? seg > 0 : seg < static_cast<int>(cum.size()) - 2;
      if (interior) {
        // outer side of a bend: round join around the vertex
        if (tmpProj.d2 > reach * reach) continue;
        lateral = std::sqrt(tmpProj.d2);
      } else if (tmpProj.d2 > absOff * absOff + 0.04) {
        continue;  // beyond the physical end of the road
      }
    }
    const bool inJunctionZone = s < edge->cutA || s > edge->len - edge->cutB;
    if (inJunctionZone && lateral > half) continue;  // corner sidewalks are handled below
    double y = edgeY(*edge, s, x, z);
    const bool onSidewalk = lateral > half;
    // (raised tables and humps lift the carriageway: src/world/roadfeatures.h)
    if (edge->calm && !onSidewalk) y += calmLift(*edge, s);
    if (onSidewalk) y += off > 0 ? edge->kerbL : edge->kerbR;
    if (y > ceiling) continue;
    consider(out, y, lateral - half + (onSidewalk ? 2 : 0) + (inJunctionZone ? 1 : 0), edge, nullptr, s, off, onSidewalk ? 1 : 0, onSidewalk ? SURF_PAVING : edge->surf);
  }

  // 3) sidewalks around junction corners
  if (!bestFound_ || out.zone != 0) {
    for (size_t i = 0; i < nodesHere.size(); i++) {
      Node* node = nodesHere[i];
      const JunctionShape& sh = *node->shape;
      const double pad = 3.5;
      if (x < sh.minX - pad || x > sh.maxX + pad || z < sh.minZ - pad || z > sh.maxZ + pad) continue;
      for (size_t c = 0; c < sh.corners.size(); c++) {
        const JunctionCorner& corner = sh.corners[c];
        if (corner.swA <= 0 && corner.swB <= 0) continue;
        if (!pointNearKerb(x, z, corner)) continue;
        const double y = planeY(*node, x, z) + KERB_H;
        if (y > ceiling) continue;
        consider(out, y, 3, nullptr, node, 0, 0, 1, SURF_PAVING);
      }
    }
  }
  return bestFound_;
}

bool World::locate(double x, double z, double yRef, double maxDist, SurfaceHit& out, double maxDy) {
  double best = JS_INF;
  bool found = false;
  auto visit = [&](int64_t code) {
    Edge* edge = edges[code / 4096];
    const int seg = static_cast<int>(code % 4096);
    projectOnPolyline(edge->pl, x, z, tmpProj, seg, seg);
    const double d = std::sqrt(tmpProj.d2) - edge->width / 2;
    if (d > maxDist) return;
    samplePolyline(edge->pl, tmpProj.s, tmpSample, seg);
    const double dy = std::fabs(tmpSample.y - yRef);
    if (dy > maxDy) return;
    const double score = d + dy * 0.5;
    if (score < best) {
      best = score;
      found = true;
      out.edge = edge;
      out.s = tmpProj.s;
      out.off = tmpProj.off;
      out.y = tmpSample.y;
      out.zone = std::fabs(tmpProj.off) <= edge->width / 2 ? 0 : 2;
      out.surf = edge->surf;
      out.node = nullptr;
    }
  };
  segGrid_.queryBox(x - maxDist, z - maxDist, x + maxDist, z + maxDist, visit);
  return found;
}

bool World::nearestRoad(double x, double z, double maxDist, int skipFlags, SurfaceHit& out) {
  double best = JS_INF;
  Edge* bestEdge = nullptr;
  double bestS = 0;
  const double c = segGrid_.cell;
  const double x0 = std::floor((x - maxDist) / c);
  const double x1 = std::floor((x + maxDist) / c);
  const double z0 = std::floor((z - maxDist) / c);
  const double z1 = std::floor((z + maxDist) / c);
  for (double ix = x0; ix <= x1; ix++) {
    for (double iz = z0; iz <= z1; iz++) {
      const std::vector<int64_t>& segs = segGrid_.at(ix * c + 0.5 * c, iz * c + 0.5 * c);
      for (size_t i = 0; i < segs.size(); i++) {
        const int64_t code = segs[i];
        Edge* edge = edges[code / 4096];
        if (edge->flags & skipFlags) continue;
        const int seg = static_cast<int>(code % 4096);
        projectOnPolyline(edge->pl, x, z, tmpProj, seg, seg);
        const double reach = edge->width / 2 + (tmpProj.off > 0 ? edge->swL : edge->swR);
        const double d = jsMax(0, std::sqrt(tmpProj.d2) - reach);
        if (d < best) {
          best = d;
          bestEdge = edge;
          bestS = tmpProj.s;
        }
      }
    }
  }
  if (!bestEdge || best > maxDist) return false;
  out.edge = bestEdge;
  out.node = nullptr;
  out.s = bestS;
  out.off = best;
  out.y = edgeY(*bestEdge, bestS, x, z);
  out.zone = best <= 0 ? 0 : 2;
  out.surf = bestEdge->surf;
  return true;
}

void computeAllCuts(WorldJson& json) {
  // first pass with zero cuts to get linked structures
  for (EdgeJson& e : json.edges) {
    e.ca = 0;
    e.cb = 0;
  }
  World w(json);
  for (Node* node : w.nodes) {
    if (!node) continue;
    computeJunctionCuts(*node);
    std::vector<double>& fr = json.nodes[static_cast<size_t>(node->id)].fr;
    fr.clear();
    for (double r : node->fr) fr.push_back(jsRound(r * 100) / 100);
    for (const Arm* arm : node->arms) {
      EdgeJson& ej = json.edges[static_cast<size_t>(arm->edge->id)];
      const double c = jsRound(arm->cut * 100) / 100;
      if (arm->out) ej.ca = c;
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

}  // namespace fs

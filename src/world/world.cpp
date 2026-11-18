// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Runtime road network: linked nodes/edges/arms, junction geometry, spatial
 * index and the analytic road-surface function.
 *
 * Origin: src/world/world.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * The maths mirrors the JavaScript originals so the pipeline and the game
 * agree on every junction corner and surface height.
 */
#include "world/world.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace fs {

namespace {
PolySample gTmpSample;
PolyProjection gTmpProj;
}  // namespace

SurfaceHit newSurfaceHit() { return SurfaceHit(); }

PolySample armPoint(const Arm& arm, double d, PolySample out) {
  const Edge& e = *arm.edge;
  return samplePolyline(e.pl, arm.out ? d : e.len - d, out);
}

void computeArmDirection(Arm& arm) {
  const Edge& e = *arm.edge;
  // Fixed probe distance: must not depend on the cut, otherwise the pipeline
  // (which solves the cuts) and the runtime would see different corner geometry.
  double probe = clamp(8, 0.5, e.len);
  gTmpSample = armPoint(arm, probe, gTmpSample);
  double dx = gTmpSample.x - arm.node->x;
  double dz = gTmpSample.z - arm.node->z;
  double l = std::hypot(dx, dz);
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
static double desiredFilletRadius(const Arm& a, const Arm& b) {
  double t = std::min(a.edge->tier, b.edge->tier);
  double r = t >= 3 ? 8 : t >= 2 ? 5.5 : t >= 1 ? 3.5 : 3;
  double sw = std::max(a.swL, b.swR);
  if (sw > 0) r = std::max(r, sw + 0.6);
  return r;
}

struct CornerSolve {
  /** distances from the node (along each arm) to the sharp corner, or -1 */
  double t = -1, u = -1;
  double cx = 0, cz = 0;
  double phi = 0;
};

static CornerSolve solveCorner(const Arm& a, const Arm& b, const Node& n) {
  double phi = wrap2Pi(b.ang - a.ang);
  CornerSolve res;
  res.phi = phi;
  if (phi < 0.12 || phi > 3.0) return res;  // ~7° … ~172°
  // left edge of a: P + left_a * half_a ; left of (dx,dz) is (dz,-dx)
  double ax = n.x + a.dz * a.half;
  double az = n.z - a.dx * a.half;
  // right edge of b: P - left_b * half_b
  double bx = n.x - b.dz * b.half;
  double bz = n.z + b.dx * b.half;
  LineHit r = lineLine(ax, az, a.dx, a.dz, bx, bz, b.dx, b.dz);
  if (!r.has) return res;
  res.t = r.t;
  res.u = r.u;
  res.cx = ax + a.dx * r.t;
  res.cz = az + a.dz * r.t;
  return res;
}

void computeJunctionCuts(Node& node) {
  auto& arms = node.arms;
  const int n = static_cast<int>(arms.size());
  node.fr.assign(static_cast<size_t>(n), 0);
  for (auto& arm : arms) arm.cut = 0;
  if (n < 2) return;
  for (auto& arm : arms) computeArmDirection(arm);

  std::vector<double> maxCut(static_cast<size_t>(n));
  for (int i = 0; i < n; i++) {
    const Edge& e = *arms[static_cast<size_t>(i)].edge;
    const Node& other = *(arms[static_cast<size_t>(i)].out ? e.b : e.a);
    // an edge that ends in a dead end may be consumed almost entirely
    double share = other.arms.size() <= 1 ? 0.8 : 0.45;
    maxCut[static_cast<size_t>(i)] = std::max(0.0, e.len * share);
  }

  if (n == 2) {
    Arm& p = arms[0];
    Arm& q = arms[1];
    bool same = std::fabs(p.half - q.half) < 0.05 && std::fabs(p.swL - q.swR) < 0.05 &&
                std::fabs(p.swR - q.swL) < 0.05;
    double bend = std::fabs(PI_ - wrap2Pi(q.ang - p.ang));
    if (same && bend < 0.6) return;  // smooth continuation: ribbons simply meet
    double taper = bend >= 0.6 ? std::max(p.half, q.half) * std::tan(std::min(bend, 2.4) / 2) + 1 : 2;
    p.cut = std::min(maxCut[0], taper);
    q.cut = std::min(maxCut[1], taper);
    return;
  }

  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    Arm& a = arms[static_cast<size_t>(i)];
    Arm& b = arms[static_cast<size_t>(j)];
    CornerSolve c = solveCorner(a, b, node);
    if (c.t < 0 || c.u < 0) {
      // No convex corner (straight-through side or reflex gap): tiny cut only.
      a.cut = std::max(a.cut, std::min(maxCut[static_cast<size_t>(i)], 0.6));
      b.cut = std::max(b.cut, std::min(maxCut[static_cast<size_t>(j)], 0.6));
      continue;
    }
    double tanHalf = std::tan(c.phi / 2);
    double r = desiredFilletRadius(a, b);
    // keep the tangent length sane on acute corners and inside short edges
    r = std::min(r, 12 * tanHalf);
    double room = std::min(maxCut[static_cast<size_t>(i)] - c.t, maxCut[static_cast<size_t>(j)] - c.u);
    r = std::min(r, std::max(0.0, room) * tanHalf);
    if (r < 0.4) r = 0;
    node.fr[static_cast<size_t>(i)] = r;
    double tl = r > 0 ? r / tanHalf : 0;
    a.cut = std::max(a.cut, std::min(maxCut[static_cast<size_t>(i)], c.t + tl + 0.05));
    b.cut = std::max(b.cut, std::min(maxCut[static_cast<size_t>(j)], c.u + tl + 0.05));
  }
  for (int i = 0; i < n; i++)
    arms[static_cast<size_t>(i)].cut = clamp(arms[static_cast<size_t>(i)].cut, 0, maxCut[static_cast<size_t>(i)]);
}

/** Mouth corner of an arm: the ribbon edge point at the cut station. side = +1 left, -1 right. */
static void mouthPoint(const Arm& arm, double side, double& outX, double& outZ) {
  gTmpSample = armPoint(arm, arm.cut, gTmpSample);
  // tangent of the polyline points a→b; flip so it points away from the node
  double tx = arm.out ? gTmpSample.tx : -gTmpSample.tx;
  double tz = arm.out ? gTmpSample.tz : -gTmpSample.tz;
  outX = gTmpSample.x + tz * arm.half * side;
  outZ = gTmpSample.z - tx * arm.half * side;
}

std::unique_ptr<JunctionShape> buildJunctionShape(Node& node) {
  auto& arms = node.arms;
  const int n = static_cast<int>(arms.size());
  if (n == 1 && node.tc > 0) {
    // turning circle at the end of a cul-de-sac
    auto shape = std::make_unique<JunctionShape>();
    const int steps = 20;
    for (int k = 0; k < steps; k++) {
      double t = static_cast<double>(k) / steps * TAU;
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
  for (const auto& a : arms)
    if (a.cut > 1e-3) any = true;
  if (!any) return nullptr;

  auto shape = std::make_unique<JunctionShape>();
  auto& ring = shape->ring;
  auto& corners = shape->corners;

  for (int i = 0; i < n; i++) {
    const Arm& a = arms[static_cast<size_t>(i)];
    const Arm& b = arms[static_cast<size_t>((i + 1) % n)];
    double lx, lz, rx, rz;
    mouthPoint(a, -1, lx, lz);
    ring.push_back(lx);
    ring.push_back(lz);
    mouthPoint(a, +1, lx, lz);
    const double alx = lx, alz = lz;
    mouthPoint(b, -1, rx, rz);

    std::vector<double> kerb{alx, alz};
    int arc0 = -1, arc1 = -1;
    double ox = 0, oz = 0;
    double r = node.fr.empty() ? 0 : node.fr[static_cast<size_t>(i)];
    CornerSolve c = n > 2 ? solveCorner(a, b, node) : CornerSolve();
    if (c.t >= 0 && c.u >= 0) {
      if (r > 0) {
        double tl = r / std::tan(c.phi / 2);
        double tix = c.cx + a.dx * tl;
        double tiz = c.cz + a.dz * tl;
        double tjx = c.cx + b.dx * tl;
        double tjz = c.cz + b.dz * tl;
        double bx = a.dx + b.dx, bz = a.dz + b.dz;
        double bl = std::hypot(bx, bz);
        if (bl == 0) bl = 1;
        bx /= bl;
        bz /= bl;
        double od = r / std::sin(c.phi / 2);
        ox = c.cx + bx * od;
        oz = c.cz + bz * od;
        double sweep = PI_ - c.phi;
        int steps = std::max(2, static_cast<int>(std::ceil(sweep / 0.21)));
        double a0 = std::atan2(tiz - oz, tix - ox);
        double a1 = std::atan2(tjz - oz, tjx - ox);
        double d = a1 - a0;
        while (d > PI_) d -= TAU;
        while (d < -PI_) d += TAU;
        a1 = a0 + d;
        arc0 = static_cast<int>(kerb.size() / 2);
        for (int k = 0; k <= steps; k++) {
          double t = a0 + d * k / steps;
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
    std::vector<double> clean{kerb[0], kerb[1]};
    int removedBeforeArc0 = 0, removedBeforeArc1 = 0;
    for (size_t k = 2; k < kerb.size(); k += 2) {
      double px = clean[clean.size() - 2], pz = clean[clean.size() - 1];
      if (std::hypot(kerb[k] - px, kerb[k + 1] - pz) < 0.02) {
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
    JunctionCorner corner;
    corner.kerb = clean;
    corner.swA = a.swL;
    corner.swB = b.swR;
    corner.arc0 = arc0;
    corner.arc1 = arc1;
    corner.ox = ox;
    corner.oz = oz;
    corner.r = r;
    corners.push_back(std::move(corner));
    for (size_t k = 0; k + 1 < clean.size(); k += 2) {
      ring.push_back(clean[k]);
      ring.push_back(clean[k + 1]);
    }
  }

  double minX = 1e300, maxX = -1e300, minZ = 1e300, maxZ = -1e300;
  for (size_t k = 0; k < ring.size(); k += 2) {
    minX = std::min(minX, ring[k]);
    maxX = std::max(maxX, ring[k]);
    minZ = std::min(minZ, ring[k + 1]);
    maxZ = std::max(maxZ, ring[k + 1]);
  }
  shape->minX = minX;
  shape->maxX = maxX;
  shape->minZ = minZ;
  shape->maxZ = maxZ;
  return shape;
}

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------

World::World(const WorldJson& json) : json_(&json) {
  signals_ = json.signals;
  crossings_ = json.crossings;

  nodes_.resize(json.nodes.size());
  for (size_t i = 0; i < json.nodes.size(); i++) {
    const NodeJson& nj = json.nodes[i];
    Node& n = nodes_[i];
    n.id = static_cast<int>(i);
    n.x = nj.x;
    n.y = nj.y;
    n.z = nj.z;
    n.gx = nj.gx;
    n.gz = nj.gz;
    n.ctrl = nj.t;
    n.sc = nj.sc;
    n.fr = nj.fr;
    n.tc = nj.tc;
    n.osm = nj.o;
    n.shape = nullptr;
  }

  edges_.resize(json.edges.size());
  for (size_t i = 0; i < json.edges.size(); i++) {
    const EdgeJson& ej = json.edges[i];
    Edge& e = edges_[i];
    e.id = static_cast<int>(i);
    e.a = &nodes_[static_cast<size_t>(ej.a)];
    e.b = &nodes_[static_cast<size_t>(ej.b)];
    e.cls = ej.c;
    e.tier = CLASS_TIER[ej.c];
    e.name = ej.n;
    e.ref = ej.r;
    e.pl = makePolyline(ej.p);
    e.len = e.pl.len;
    e.lanesF = ej.lf;
    e.lanesB = ej.lb;
    e.laneW = ej.lw;
    e.width = ej.w;
    e.margin = ej.mg;
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
    e.armA = nullptr;
    e.armB = nullptr;
    e.blendA = 0;
    e.blendB = 0;
  }

  // arms in stored (counter-clockwise) order
  for (size_t i = 0; i < json.nodes.size(); i++) {
    Node& node = nodes_[i];
    const NodeJson& nj = json.nodes[i];
    node.arms.reserve(nj.arms.size());
    for (size_t k = 0; k < nj.arms.size(); k++) {
      const ArmJson& aj = nj.arms[k];
      Edge& edge = edges_[static_cast<size_t>(aj.e)];
      // A loop edge (a === b) appears twice: first occurrence is the "out" end.
      bool out = edge.a == &node && !(edge.b == &node && edge.armA != nullptr);
      Arm arm;
      arm.node = &node;
      arm.edge = &edge;
      arm.out = out;
      arm.idx = static_cast<int>(k);
      arm.role = aj.r;
      arm.sg = aj.g;
      arm.stopS = aj.sl;
      arm.cut = out ? edge.cutA : edge.cutB;
      arm.dx = 1;
      arm.dz = 0;
      arm.ang = 0;
      arm.half = edge.width / 2;
      arm.swL = out ? edge.swL : edge.swR;
      arm.swR = out ? edge.swR : edge.swL;
      arm.isApproach = out ? edge.lanesB > 0 : edge.lanesF > 0;
      arm.isExit = out ? edge.lanesF > 0 : edge.lanesB > 0;
      if (arm.stopS < arm.cut) arm.stopS = arm.cut;
      node.arms.push_back(std::move(arm));
    }
    // point the edge arms back at the stored copies
    for (auto& arm : node.arms) {
      if (arm.out) arm.edge->armA = &arm;
      else arm.edge->armB = &arm;
    }
  }

  for (auto& e : edges_) finishEdge(e);

  shapes_.reserve(nodes_.size());
  for (auto& node : nodes_) {
    for (auto& arm : node.arms) computeArmDirection(arm);
    std::unique_ptr<JunctionShape> sh = buildJunctionShape(node);
    if (sh) {
      nodeGrid_.insertBox(sh->minX, sh->minZ, sh->maxX, sh->maxZ, &node);
      node.shape = nullptr;  // filled once shapes_ is stable (below)
      shapes_.push_back(std::move(*sh));
      node.shape = &shapes_.back();
    } else {
      node.shape = nullptr;
    }
  }

  banned_.reserve(json.banned.size());
  for (const auto& b : json.banned)
    banned_.push_back(std::to_string(b[0]) + ":" + std::to_string(b[1]) + ":" + std::to_string(b[2]));
}

void World::finishEdge(Edge& e) {
  double usable = std::max(0.0, e.len - e.cutA - e.cutB);
  e.blendA = std::min(14.0, usable * 0.45);
  e.blendB = std::min(14.0, usable * 0.45);
  const auto& pts = e.pl.pts;
  const size_t n = e.pl.cum.size();
  double pad = e.width / 2 + std::max(e.swL, e.swR) + 1;
  double minX = 1e300, maxX = -1e300, minZ = 1e300, maxZ = -1e300;
  for (size_t i = 0; i < n; i++) {
    double x = pts[i * 3];
    double z = pts[i * 3 + 2];
    minX = std::min(minX, x);
    maxX = std::max(maxX, x);
    minZ = std::min(minZ, z);
    maxZ = std::max(maxZ, z);
    if (i + 1 < n) {
      double x2 = pts[i * 3 + 3];
      double z2 = pts[i * 3 + 5];
      segGrid_.insertBox(std::min(x, x2) - pad, std::min(z, z2) - pad,
                         std::max(x, x2) + pad, std::max(z, z2) + pad, e.id * 4096 + static_cast<int>(i));
    }
  }
  e.minX = minX - pad;
  e.maxX = maxX + pad;
  e.minZ = minZ - pad;
  e.maxZ = maxZ + pad;
}

bool World::isBanned(int fromEdge, int viaNode, int toEdge) const {
  std::string key = std::to_string(fromEdge) + ":" + std::to_string(viaNode) + ":" + std::to_string(toEdge);
  for (const auto& b : banned_)
    if (b == key) return true;
  return false;
}

double World::planeY(const Node& node, double x, double z) const {
  return node.y + node.gx * (x - node.x) + node.gz * (z - node.z);
}

double World::edgeY(const Edge& edge, double s, double x, double z) const {
  gTmpSample = samplePolyline(edge.pl, s, gTmpSample);
  double y = gTmpSample.y;
  double sa = s - edge.cutA;
  if (sa < edge.blendA) {
    double w = edge.blendA > 0 ? 1 - smoothstep(0, edge.blendA, sa) : (sa <= 0 ? 1 : 0);
    if (w > 0) y += (planeY(*edge.a, x, z) - y) * w;
  }
  double sb = edge.len - s - edge.cutB;
  if (sb < edge.blendB) {
    double w = edge.blendB > 0 ? 1 - smoothstep(0, edge.blendB, sb) : (sb <= 0 ? 1 : 0);
    if (w > 0) y += (planeY(*edge.b, x, z) - y) * w;
  }
  return y;
}

namespace {
/** Candidate selection state for surfaceAt (mirrors the JS private fields). */
struct BestState {
  double y = 0;
  double score = 0;
  bool found = false;
  void reset() { found = false; }
  void consider(SurfaceHit& out, double y_, double score_, Edge* edge, Node* node, double s,
                double off, int zone, int surf) {
    if (found) {
      double dy = y_ - y;
      if (dy < -1.5) return;
      if (dy <= 1.5 && score_ >= score) return;
    }
    found = true;
    y = y_;
    score = score_;
    out.y = y_;
    out.edge = edge;
    out.node = node;
    out.s = s;
    out.off = off;
    out.zone = zone;
    out.surf = surf;
  }
};
}  // namespace

bool World::surfaceAt(double x, double z, double yRef, SurfaceHit& out) {
  BestState best;
  out = SurfaceHit();
  out.zone = 2;
  double ceiling = yRef + 1.6;

  // 1) junction carriageway polygons
  const auto& nodes = nodeGrid_.at(x, z);
  for (Node* node : nodes) {
    JunctionShape* sh = node->shape;
    if (!sh) continue;
    if (x < sh->minX || x > sh->maxX || z < sh->minZ || z > sh->maxZ) continue;
    if (!pointInRing(x, z, sh->ring)) continue;
    double y = planeY(*node, x, z);
    if (y > ceiling) continue;
    best.consider(out, y, -50, nullptr, node, 0, 0, 0, SURF_ASPHALT);
  }

  // 2) edges: carriageway between the cuts, plus sidewalks
  const auto& segs = segGrid_.at(x, z);
  for (int code : segs) {
    Edge& edge = edges_[static_cast<size_t>(code / 4096)];
    if (x < edge.minX || x > edge.maxX || z < edge.minZ || z > edge.maxZ) continue;
    int seg = code % 4096;
    gTmpProj = projectOnPolyline(edge.pl, x, z, gTmpProj, seg, seg);
    double s = gTmpProj.s;
    double off = gTmpProj.off;
    double absOff = std::fabs(off);
    double half = edge.width / 2;
    double sw = off > 0 ? edge.swL : edge.swR;
    double reach = half + sw;
    if (absOff > reach) continue;
    const auto& cum = edge.pl.cum;
    bool clampedStart = s <= cum[static_cast<size_t>(seg)] + 1e-6;
    bool clampedEnd = s >= cum[static_cast<size_t>(seg) + 1] - 1e-6;
    // lateral distance used for ranking: for a projection clamped to a
    // segment end it is the true distance to that vertex
    double lateral = absOff;
    if (clampedStart || clampedEnd) {
      bool interior = clampedStart ? seg > 0 : seg < static_cast<int>(cum.size()) - 2;
      if (interior) {
        if (gTmpProj.d2 > reach * reach) continue;
        lateral = std::sqrt(gTmpProj.d2);
      } else if (gTmpProj.d2 > absOff * absOff + 0.04) {
        continue;  // beyond the physical end of the road
      }
    }
    bool inJunctionZone = s < edge.cutA || s > edge.len - edge.cutB;
    if (inJunctionZone && lateral > half) continue;  // corner sidewalks are handled below
    double y = edgeY(edge, s, x, z);
    bool onSidewalk = lateral > half;
    if (onSidewalk) y += KERB_H;
    if (y > ceiling) continue;
    best.consider(out, y, lateral - half + (onSidewalk ? 2 : 0) + (inJunctionZone ? 1 : 0), &edge,
                  nullptr, s, off, onSidewalk ? 1 : 0, onSidewalk ? SURF_PAVING : edge.surf);
  }

  // 3) sidewalks around junction corners
  if (!best.found || out.zone != 0) {
    for (Node* node : nodes) {
      JunctionShape* sh = node->shape;
      if (!sh) continue;
      const double pad = 3.5;
      if (x < sh->minX - pad || x > sh->maxX + pad || z < sh->minZ - pad || z > sh->maxZ + pad)
        continue;
      for (const auto& corner : sh->corners) {
        if (corner.swA <= 0 && corner.swB <= 0) continue;
        if (!pointNearKerb(x, z, corner)) continue;
        double y = planeY(*node, x, z) + KERB_H;
        if (y > ceiling) continue;
        best.consider(out, y, 3, nullptr, node, 0, 0, 1, SURF_PAVING);
      }
    }
  }
  out.found = best.found;
  return best.found;
}

bool World::locate(double x, double z, double yRef, double maxDist, SurfaceHit& out) {
  double best = 1e300;
  bool found = false;
  segGrid_.queryBox(x - maxDist, z - maxDist, x + maxDist, z + maxDist, [&](int code) {
    Edge& edge = edges_[static_cast<size_t>(code / 4096)];
    int seg = code % 4096;
    gTmpProj = projectOnPolyline(edge.pl, x, z, gTmpProj, seg, seg);
    double d = std::sqrt(gTmpProj.d2) - edge.width / 2;
    if (d > maxDist) return;
    gTmpSample = samplePolyline(edge.pl, gTmpProj.s, gTmpSample, seg);
    double dy = std::fabs(gTmpSample.y - yRef);
    if (dy > 4.5) return;
    double score = d + dy * 0.5;
    if (score < best) {
      best = score;
      found = true;
      out.edge = &edge;
      out.s = gTmpProj.s;
      out.off = gTmpProj.off;
      out.y = gTmpSample.y;
      out.zone = std::fabs(gTmpProj.off) <= edge.width / 2 ? 0 : 2;
      out.surf = edge.surf;
      out.node = nullptr;
    }
  });
  return found;
}

bool World::nearestRoad(double x, double z, double maxDist, int skipFlags, SurfaceHit& out) {
  double best = 1e300;
  Edge* bestEdge = nullptr;
  double bestS = 0;
  double c = segGrid_.cell();
  long x0 = static_cast<long>(std::floor((x - maxDist) / c));
  long x1 = static_cast<long>(std::floor((x + maxDist) / c));
  long z0 = static_cast<long>(std::floor((z - maxDist) / c));
  long z1 = static_cast<long>(std::floor((z + maxDist) / c));
  for (long ix = x0; ix <= x1; ix++) {
    for (long iz = z0; iz <= z1; iz++) {
      const auto& segs = segGrid_.at(ix * c + 0.5 * c, iz * c + 0.5 * c);
      for (int code : segs) {
        Edge& edge = edges_[static_cast<size_t>(code / 4096)];
        if (edge.flags & skipFlags) continue;
        int seg = code % 4096;
        gTmpProj = projectOnPolyline(edge.pl, x, z, gTmpProj, seg, seg);
        double reach = edge.width / 2 + (gTmpProj.off > 0 ? edge.swL : edge.swR);
        double d = std::max(0.0, std::sqrt(gTmpProj.d2) - reach);
        if (d < best) {
          best = d;
          bestEdge = &edge;
          bestS = gTmpProj.s;
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

void World::edgesInBox(double minX, double minZ, double maxX, double maxZ,
                       const std::function<void(Edge&)>& visit) {
  std::set<int> seen;
  segGrid_.queryBox(minX, minZ, maxX, maxZ, [&](int code) {
    int id = code / 4096;
    if (!seen.insert(id).second) return;
    visit(edges_[static_cast<size_t>(id)]);
  });
}

Edge* World::nearestEdge(double x, double z, double radius) {
  Edge* best = nullptr;
  double bestD = radius * radius;
  segGrid_.queryBox(x - radius, z - radius, x + radius, z + radius, [&](int code) {
    Edge& e = edges_[static_cast<size_t>(code / 4096)];
    int seg = code % 4096;
    gTmpProj = projectOnPolyline(e.pl, x, z, gTmpProj, seg, seg);
    if (gTmpProj.d2 < bestD) {
      bestD = gTmpProj.d2;
      best = &e;
    }
  });
  return best;
}

/** Is (x,z) on the sidewalk strip behind a junction kerb line? */
bool pointNearKerb(double x, double z, const JunctionCorner& c) {
  const auto& k = c.kerb;
  size_t n = k.size() / 2;
  double best = 1e300;
  double bestT = 0;
  double side = 0;
  double total = 0;
  double acc = 0;
  for (size_t i = 0; i + 1 < n; i++)
    total += std::hypot(k[(i + 1) * 2] - k[i * 2], k[(i + 1) * 2 + 1] - k[i * 2 + 1]);
  if (total < 1e-6) return false;
  for (size_t i = 0; i + 1 < n; i++) {
    double ax = k[i * 2], az = k[i * 2 + 1];
    double dx = k[(i + 1) * 2] - ax;
    double dz = k[(i + 1) * 2 + 1] - az;
    double l2 = dx * dx + dz * dz;
    double l = std::sqrt(l2);
    double t = l2 > 1e-9 ? ((x - ax) * dx + (z - az) * dz) / l2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    double ex = x - (ax + dx * t);
    double ez = z - (az + dz * t);
    double d2 = ex * ex + ez * ez;
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
  double w = c.swA + (c.swB - c.swA) * bestT;
  return best <= w * w;
}

}  // namespace fs

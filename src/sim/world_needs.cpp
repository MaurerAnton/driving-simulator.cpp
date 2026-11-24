// SPDX-License-Identifier: AGPL-3.0-only
/** The stand-ins of sim/world_needs.h: the original's functions (src/world/lanes.ts, world.ts @ v1.7.7), written against the map layer as it is. */
#include "sim/world_needs.h"

#include <unordered_map>

#include "core/js.h"
#include "sim/jsmath.h"

namespace fs::needs {

std::vector<Edge*> allEdges(World& world) {
  std::vector<Edge*> out;
  if (!world.allEdges().empty()) {
    // (in the order of their numbers: for a map in one piece that is the order the original's set has them in)
    for (Edge& e : world.edges())
      if (world.allEdges().count(&e)) out.push_back(&e);
    return out;
  }
  for (Edge& e : world.edges())
    if (e.a && e.b) out.push_back(&e);
  return out;
}

Edge* edgeAt(World& world, int id) {
  std::vector<Edge>& edges = world.edges();
  if (id < 0 || static_cast<size_t>(id) >= edges.size()) return nullptr;
  Edge* e = &edges[static_cast<size_t>(id)];
  if (world.allEdges().empty()) return e->a && e->b ? e : nullptr;
  return world.allEdges().count(e) ? e : nullptr;
}

const LaneGraph::EdgeLanes* edgeLanes(const LaneGraph& graph, int edgeId) {
  if (edgeId < 0 || static_cast<size_t>(edgeId) >= graph.edgeLanes.size()) return nullptr;
  return &graph.edgeLanes[static_cast<size_t>(edgeId)];
}

Lane* laneAt(LaneGraph& graph, int id) {
  if (id < 0 || static_cast<size_t>(id) >= graph.lanes.size()) return nullptr;
  Lane* l = &graph.lanes[static_cast<size_t>(id)];
  return l->edge ? l : nullptr;
}

namespace {

template <typename W>
bool locate_(W& world, double x, double z, double yRef, double maxDist, SurfaceHit& out, double maxDy) {
  if constexpr (HAS_LOCATE_MAXDY<W>) {
    return world.locate(x, z, yRef, maxDist, out, maxDy);
  } else {
    double best = JS_INF;
    bool found = false;
    PolyProjection proj;
    PolySample sample = newPolySample();
    world.segmentsInBox(x - maxDist, z - maxDist, x + maxDist, z + maxDist, [&](int code) {
      Edge& edge = world.edges()[static_cast<size_t>(code / 4096)];
      const int seg = code % 4096;
      proj = projectOnPolyline(edge.pl, x, z, proj, seg, seg);
      const double d = std::sqrt(proj.d2) - edge.width / 2;
      if (d > maxDist) return;
      sample = samplePolyline(edge.pl, proj.s, sample, seg);
      const double dy = std::abs(sample.y - yRef);
      if (dy > maxDy) return;
      const double score = d + dy * 0.5;
      if (score < best) {
        best = score;
        found = true;
        out.edge = &edge;
        out.s = proj.s;
        out.off = proj.off;
        out.y = sample.y;
        out.zone = std::abs(proj.off) <= edge.width / 2 ? 0 : 2;
        out.surf = edge.surf;
        out.node = nullptr;
      }
    });
    return found;
  }
}

template <typename W>
bool isBanned_(W& world, const Edge& from, const Node& via, const Edge& to) {
  if constexpr (HAS_ISBANNED_OBJECTS<W>) return world.isBanned(from, via, to);
  else return world.isBanned(from.id, via.id, to.id);
}

/** the paths through each junction, from the list of all paths (made anew when that list has changed) */
struct PathIndex {
  size_t count = static_cast<size_t>(-1);
  const Connector* first = nullptr;
  std::unordered_map<int, std::vector<const Connector*>> at;
};

template <typename G>
const std::vector<const Connector*>& pathsThrough_(G& graph, const Node& node) {
  static const std::vector<const Connector*> none;
  static std::unordered_map<const void*, PathIndex> byGraph;
  PathIndex& index = byGraph[&graph];
  const Connector* first = graph.connectors.empty() ? nullptr : &graph.connectors[0];
  if (index.count != graph.connectors.size() || index.first != first) {
    index.count = graph.connectors.size();
    index.first = first;
    index.at.clear();
    for (const Connector& c : graph.connectors)
      if (c.node) index.at[c.node->id].push_back(&c);
  }
  auto it = index.at.find(node.id);
  return it == index.at.end() ? none : it->second;
}

/** Squared distance of a point from a segment. */
double pointSegDist2(double x, double z, double ax, double az, double bx, double bz) {
  const double dx = bx - ax;
  const double dz = bz - az;
  const double l2 = dx * dx + dz * dz;
  double t = l2 > 1e-12 ? ((x - ax) * dx + (z - az) * dz) / l2 : 0;
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  const double ex = x - (ax + dx * t);
  const double ez = z - (az + dz * t);
  return ex * ex + ez * ez;
}

/** Squared distance between two segments in the plane (0 when they cross). */
double segSegDist2(double ax, double az, double bx, double bz, double cx, double cz, double dx, double dz) {
  // crossing: each segment has the ends of the other on different sides
  const double d1 = (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
  const double d2 = (bx - ax) * (dz - az) - (bz - az) * (dx - ax);
  const double d3 = (dx - cx) * (az - cz) - (dz - cz) * (ax - cx);
  const double d4 = (dx - cx) * (bz - cz) - (dz - cz) * (bx - cx);
  if (d1 * d2 < 0 && d3 * d4 < 0) return 0;
  return jsMin(jsMin(pointSegDist2(ax, az, cx, cz, dx, dz), pointSegDist2(bx, bz, cx, cz, dx, dz)),
               jsMin(pointSegDist2(cx, cz, ax, az, bx, bz), pointSegDist2(dx, dz, ax, az, bx, bz)));
}

}  // namespace

bool locate(World& world, double x, double z, double yRef, double maxDist, SurfaceHit& out, double maxDy) {
  return locate_(world, x, z, yRef, maxDist, out, maxDy);
}

bool isBanned(World& world, const Edge& from, const Node& via, const Edge& to) { return isBanned_(world, from, via, to); }

const std::vector<const Connector*>& pathsThrough(LaneGraph& graph, const Node& node) { return pathsThrough_(graph, node); }

double nearestPath(LaneGraph& graph, double x, double z, double y, double reach, PathFoot& out) {
  World& world = graph.world;
  double best = reach;
  bool found = false;
  PolyProjection proj;
  PolySample smp = newPolySample();
  world.segmentsInBox(x - reach, z - reach, x + reach, z + reach, [&](int code) {
    const Edge& e = world.edges()[static_cast<size_t>(code / 4096)];
    const int seg = code % 4096;
    const LaneGraph::EdgeLanes* el = edgeLanes(graph, e.id);
    if (!el || el->fwd.size() + el->back.size() == 0) return;
    proj = projectOnPolyline(e.pl, x, z, proj, seg, seg);
    const double far = e.width / 2 + best;
    if (proj.d2 > far * far) return;
    // (the lanes run between the junction areas at the two ends)
    smp = samplePolyline(e.pl, clamp(proj.s, e.cutA, jsMax(e.cutA, e.len - e.cutB)), smp);
    if (!std::isnan(y) && std::abs(smp.y - y) > 2.6) return;
    for (int side = 0; side < 2; side++) {
      const std::vector<Lane*>& list = side == 0 ? el->fwd : el->back;
      for (size_t i = 0; i < list.size(); i++) {
        const double px = smp.x + smp.tz * list[i]->off;
        const double pz = smp.z - smp.tx * list[i]->off;
        const double d = jsHypot(x - px, z - pz);
        if (d < best) {
          best = d;
          out.x = px;
          out.z = pz;
          found = true;
        }
      }
    }
  });
  world.nodesInBox(x - reach, z - reach, x + reach, z + reach, [&](Node& node) {
    if (!std::isnan(y) && std::abs(world.planeY(node, x, z) - y) > 2.6) return;
    const std::vector<const Connector*>& paths = pathsThrough(graph, node);
    for (size_t i = 0; i < paths.size(); i++) {
      proj = projectOnPolyline(paths[i]->pl, x, z, proj);
      if (proj.d2 >= best * best) continue;
      best = std::sqrt(proj.d2);
      smp = samplePolyline(paths[i]->pl, proj.s, smp);
      out.x = smp.x;
      out.z = smp.z;
      found = true;
    }
  });
  out.d = found ? best : JS_INF;
  return out.d;
}

bool wallInLane(LaneGraph& graph, double x1, double z1, double x2, double z2, double y, double need) {
  World& world = graph.world;
  const double minX = jsMin(x1, x2) - need;
  const double maxX = jsMax(x1, x2) + need;
  const double minZ = jsMin(z1, z2) - need;
  const double maxZ = jsMax(z1, z2) + need;
  const double need2 = need * need;
  bool hit = false;
  world.segmentsInBox(minX, minZ, maxX, maxZ, [&](int code) {
    if (hit) return;
    const Edge& e = world.edges()[static_cast<size_t>(code / 4096)];
    const size_t seg = static_cast<size_t>(code % 4096);
    const LaneGraph::EdgeLanes* el = edgeLanes(graph, e.id);
    if (!el || el->fwd.size() + el->back.size() == 0) return;
    const std::vector<double>& p = e.pl.pts;
    const double ax = p[seg * 3];
    const double ay = p[seg * 3 + 1];
    const double az = p[seg * 3 + 2];
    const double bx = p[seg * 3 + 3];
    const double bz = p[seg * 3 + 5];
    if (!std::isnan(y) && std::abs((ay + p[seg * 3 + 4]) / 2 - y) > 2.6 + std::abs(ay - p[seg * 3 + 4]) / 2) return;
    // (most pieces of road in reach of the grid are nowhere near the wall)
    const double far = e.width / 2 + need;
    if (jsMax(ax, bx) + far < minX + need || jsMin(ax, bx) - far > maxX - need || jsMax(az, bz) + far < minZ + need || jsMin(az, bz) - far > maxZ - need) return;
    const double l = jsHypot(bx - ax, bz - az);
    if (l < 1e-6) return;
    // left of the direction of travel a→b
    const double nx = (bz - az) / l;
    const double nz = -(bx - ax) / l;
    for (int side = 0; side < 2 && !hit; side++) {
      const std::vector<Lane*>& list = side == 0 ? el->fwd : el->back;
      for (size_t i = 0; i < list.size(); i++) {
        const double o = list[i]->off;
        if (segSegDist2(x1, z1, x2, z2, ax + nx * o, az + nz * o, bx + nx * o, bz + nz * o) < need2) {
          hit = true;
          break;
        }
      }
    }
  });
  if (hit) return true;
  world.nodesInBox(minX, minZ, maxX, maxZ, [&](Node& node) {
    if (hit) return;
    if (!std::isnan(y) && std::abs(world.planeY(node, (x1 + x2) / 2, (z1 + z2) / 2) - y) > 2.6) return;
    const std::vector<const Connector*>& paths = pathsThrough(graph, node);
    for (size_t k = 0; k < paths.size() && !hit; k++) {
      const std::vector<double>& p = paths[k]->pl.pts;
      for (size_t i = 0; i + 5 < p.size(); i += 3) {
        if (segSegDist2(x1, z1, x2, z2, p[i], p[i + 2], p[i + 3], p[i + 5]) < need2) {
          hit = true;
          break;
        }
      }
    }
  });
  return hit;
}

PathFoot* besideLanes(LaneGraph& graph, double x, double z, double y, double need, PathFoot& out) {
  PathFoot postFoot;
  out.x = x;
  out.z = z;
  for (int tries = 0; tries < 5; tries++) {
    const double d = nearestPath(graph, out.x, out.z, y, need, postFoot);
    if (d >= need) {
      out.d = jsHypot(out.x - x, out.z - z);
      return &out;
    }
    // away from the middle of that lane (straight out of it; from dead centre towards where the post came from)
    double ux = out.x - postFoot.x;
    double uz = out.z - postFoot.z;
    if (d < 0.05) {
      ux = x - postFoot.x;
      uz = z - postFoot.z;
      if (jsHypot(ux, uz) < 0.05) return nullptr;
    }
    const double l = jsHypot(ux, uz);
    out.x = postFoot.x + (ux / l) * (need + 0.15);
    out.z = postFoot.z + (uz / l) * (need + 0.15);
    if (jsHypot(out.x - x, out.z - z) > 4.5) return nullptr;
  }
  return nullptr;
}

std::vector<const char*> missing() {
  std::vector<const char*> out;
  out.push_back("World.allEdges / allNodes filled by the constructor for a map in one piece (world.ts) – checked when it runs: needs::allEdges");
  if (!HAS_NODE_DARK<Node>) out.push_back("Node.dark (world.ts)");
  if (!HAS_EDGE_MARGINS<Edge>) out.push_back("Edge.marginL, Edge.marginR (world.ts)");
  if (!HAS_EDGE_NARROW<Edge>) out.push_back("Edge.narrow (world.ts)");
  if (!needs_detail::HasWayOf<Lane>) out.push_back("wayOf(lane) (routegraph.ts)");
  if (!HAS_CONFLICT_GETTERS<Connector>) out.push_back("Connector.conflicts / siblings as getters that resolve the junction (lanes.ts) – LaneGraph.resolveNode is called instead");
  if (!HAS_LOCATE_MAXDY<World>) out.push_back("World.locate(..., maxDy) (world.ts)");
  if (!HAS_ISBANNED_OBJECTS<World>) out.push_back("World.isBanned(Edge, Node, Edge) (world.ts)");
  if (!HAS_PATHS_THROUGH<LaneGraph>) out.push_back("LaneGraph.pathsThrough(node) (lanes.ts)");
  if (!HAS_LANE_CLEARANCE<LaneGraph>) out.push_back("LaneGraph.nearestPath, wallInLane, besideLanes + PathFoot (lanes.ts)");
  if (!HAS_LANES_IN_PIECES<LaneGraph>) out.push_back("LaneGraph.addEdge, completeNode, removeNode, removeEdge (lanes.ts) and MapLoader (loader.ts)");
  return out;
}

}  // namespace fs::needs

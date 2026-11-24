// SPDX-License-Identifier: AGPL-3.0-only
/**
 * NEEDS FROM WORLD
 *
 * What the simulation at v1.7.7 asks of the map layer (src/world) that the map layer of this
 * worktree did not have when the simulation was translated – it stood at 0.8.0 and was being
 * brought to v1.7.7 in another worktree at the same time.  Nothing in src/world was touched.
 *
 * Each entry first looks whether the real thing is there under the original's name (a
 * `requires`): then it is used, and the stand-in below it is dead code.  Where it is not, the
 * stand-in is either what a map without that feature has (no junction with its lights out, no
 * kerb build-outs) or the original's function itself, written against what the map layer
 * offers.  When the layers are joined: go through this file from the top, see that each entry
 * finds the real member (tools/dev/sim_needs.cpp prints which do), and replace the calls.
 * Every entry names where the original defines the thing.
 */
#pragma once

#include <cmath>
#include <type_traits>
#include <vector>

#include "core/geom.h"
#include "world/lanes.h"
#include "world/routegraph.h"
#include "world/world.h"

namespace fs {

// (looked for in namespace fs by the argument's type, where a later map layer may declare them)
namespace needs_detail {
template <typename L>
concept HasWayOf = requires(const L& l) { wayOf(l); };
template <typename L>
concept HasWayOfLane = requires(const L& l) { wayOfLane(l); };
template <typename L>
int wayOf_(const L& lane) {
  if constexpr (HasWayOf<L>) return wayOf(lane);
  else if constexpr (HasWayOfLane<L>) return wayOfLane(lane);
  else return lane.edge->id * 2 + (lane.dir > 0 ? 0 : 1);
}
}  // namespace needs_detail

namespace needs {

// ---- fields ------------------------------------------------------------------------------

/**
 * Node.dark – src/world/world.ts, interface Node: "the lights of this junction are out of order
 * (World.lightsOut): its signs count, or right before left".  Stand-in: no junction is.
 */
template <typename N>
inline bool dark(const N& node) {
  if constexpr (requires { node.dark; }) return node.dark;
  else return false;
}
template <typename N>
inline constexpr bool HAS_NODE_DARK = requires(const N& n) { n.dark; };

/**
 * Edge.marginL, Edge.marginR – src/world/world.ts, interface Edge: the paved strip outside the
 * lanes on the left and on the right of a→b (they differ where a hard shoulder is on one side).
 * Stand-in: Edge.margin on both sides.
 */
template <typename E>
inline double marginL(const E& e) {
  if constexpr (requires { e.marginL; }) return e.marginL;
  else return e.margin;
}
template <typename E>
inline double marginR(const E& e) {
  if constexpr (requires { e.marginR; }) return e.marginR;
  else return e.margin;
}
template <typename E>
inline constexpr bool HAS_EDGE_MARGINS = requires(const E& e) {
  e.marginL;
  e.marginR;
};

/**
 * Edge.narrow – src/world/world.ts, interface Edge: `narrow?: Narrowing[] | null`, the kerb
 * build-outs along the road (src/world/narrowing.ts).  Asked only whether there are any
 * (`if (edge.narrow)`).  Stand-in: none.
 */
template <typename E>
inline bool narrow(const E& e) {
  if constexpr (requires { static_cast<bool>(e.narrow); }) return static_cast<bool>(e.narrow);
  else if constexpr (requires { e.narrow.empty(); }) return !e.narrow.empty();
  else return false;
}
template <typename E>
inline constexpr bool HAS_EDGE_NARROW = requires(const E& e) { e.narrow; };

// ---- functions ---------------------------------------------------------------------------

/**
 * wayOf(lane) – src/world/routegraph.ts: "The way a lane belongs to": edge.id * 2 + (dir > 0 ? 0 : 1).
 * (The map layer at 0.8.0 calls it wayOfLane.)
 */
inline int wayOf(const Lane& lane) { return needs_detail::wayOf_(lane); }

/**
 * world.edges[id] where the list has gaps (the map comes in pieces) – src/world/world.ts,
 * World.edges.  Null where no road of that number is loaded.
 */
Edge* edgeAt(World& world, int id);

/**
 * graph.edgeLanes[edge.id] – src/world/lanes.ts, LaneGraph.edgeLanes: undefined for a road
 * whose lanes are not (or no longer) there.  Null then.
 */
const LaneGraph::EdgeLanes* edgeLanes(const LaneGraph& graph, int edgeId);

/** graph.lanes[id] – src/world/lanes.ts, LaneGraph.lanes: undefined where the lane is gone.  Null then. */
Lane* laneAt(LaneGraph& graph, int id);

/**
 * connector.conflicts, connector.siblings – src/world/lanes.ts, class JunctionPath: getters that
 * work out the conflicts of the whole junction when one of its paths is first asked.  The map
 * layer at 0.8.0 keeps them as lists that LaneGraph.resolveNode(node) fills: that is asked first.
 */
template <typename G, typename C>
inline decltype(auto) conflicts(G& graph, C& c) {
  if constexpr (requires { c.conflicts(); }) {
    return c.conflicts();
  } else {
    graph.resolveNode(c.node->id);
    return (c.conflicts);
  }
}
template <typename G, typename C>
inline decltype(auto) siblings(G& graph, C& c) {
  if constexpr (requires { c.siblings(); }) {
    return c.siblings();
  } else {
    graph.resolveNode(c.node->id);
    return (c.siblings);
  }
}
template <typename C>
inline constexpr bool HAS_CONFLICT_GETTERS = requires(C& c) {
  c.conflicts();
  c.siblings();
};

/**
 * World.locate(x, z, yRef, maxDist, out, maxDy = 4.5) – src/world/world.ts: the sixth argument,
 * how far above or below `yRef` a road may lie to be taken (the rule monitor asks for 1.5 m).
 * Stand-in: the original's function, with the map layer's segment index.
 */
bool locate(World& world, double x, double z, double yRef, double maxDist, SurfaceHit& out, double maxDy);
template <typename W>
inline constexpr bool HAS_LOCATE_MAXDY = requires(W& w, SurfaceHit& h) { w.locate(0.0, 0.0, 0.0, 0.0, h, 0.0); };

/**
 * World.isBanned(from: Edge, via: Node, to: Edge) – src/world/world.ts.  The map layer at 0.8.0
 * takes the three numbers.
 */
bool isBanned(World& world, const Edge& from, const Node& via, const Edge& to);
template <typename W>
inline constexpr bool HAS_ISBANNED_OBJECTS = requires(W& w, const Edge& e, const Node& n) { w.isBanned(e, n, e); };

/** A foot point on the nearest lane or path – src/world/lanes.ts, interface PathFoot. */
struct PathFoot {
  double x = 0;
  double z = 0;
  double d = 0;
};

/** LaneGraph.pathsThrough(node) – src/world/lanes.ts: "The paths through a junction." */
const std::vector<const Connector*>& pathsThrough(LaneGraph& graph, const Node& node);
template <typename G>
inline constexpr bool HAS_PATHS_THROUGH = requires(G& g, const Node& n) { g.pathsThrough(n); };

/**
 * LaneGraph.nearestPath(x, z, y, reach, out) – src/world/lanes.ts: "The nearest centre line of a
 * lane, or of a path through a junction, to a point – on the same level (`y`; NaN: on any).
 * Returns the distance and writes the foot point to `out`; Infinity when none is closer than
 * `reach`."  Stand-in: the original's function.
 */
double nearestPath(LaneGraph& graph, double x, double z, double y, double reach, PathFoot& out);

/**
 * LaneGraph.wallInLane(x1, z1, x2, z2, y, need) – src/world/lanes.ts: "Does a wall from (x1, z1)
 * to (x2, z2) come closer than `need` to the middle of a lane (on the level `y`; NaN: on any)?"
 * Stand-in: the original's function.
 */
bool wallInLane(LaneGraph& graph, double x1, double z1, double x2, double z2, double y, double need);

/**
 * LaneGraph.besideLanes(x, z, y, need, out) – src/world/lanes.ts: "Where a post (a sign, a lamp)
 * may stand: the place itself when it keeps `need` metres from the middle of every lane,
 * otherwise the nearest place beside the lanes within a few metres – or null when there is
 * none."  Stand-in: the original's function.
 */
PathFoot* besideLanes(LaneGraph& graph, double x, double z, double y, double need, PathFoot& out);
template <typename G>
inline constexpr bool HAS_LANE_CLEARANCE = requires(G& g, PathFoot& f) {
  g.nearestPath(0.0, 0.0, 0.0, 0.0, f);
  g.besideLanes(0.0, 0.0, 0.0, 0.0, f);
  g.wallInLane(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
};

/**
 * LaneGraph.addEdge, completeNode, removeNode, removeEdge – src/world/lanes.ts: the lane graph
 * following a map that comes in pieces; and MapLoader (src/world/loader.ts), which tells of the
 * pieces.  No stand-in: sim/follow.h is written against them and compiles once they are there.
 */
template <typename G>
inline constexpr bool HAS_LANES_IN_PIECES = requires(G& g, Edge& e, Node& n) {
  g.addEdge(e);
  g.completeNode(n);
  g.removeNode(n);
  g.removeEdge(e);
};

/** What of the above the map layer this was compiled against still lacks (for tools/dev/sim_needs.cpp). */
std::vector<const char*> missing();

}  // namespace needs
}  // namespace fs

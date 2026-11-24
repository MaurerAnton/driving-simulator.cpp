// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/world.ts @ v1.7.7
/**
 * Runtime road network: linked nodes/edges/arms, junction geometry, spatial
 * index and the analytic road-surface function.
 *
 * The same code runs in the map pipeline (to build meshes and conform the
 * terrain) and in the game (physics ground, map matching, AI), which is what
 * keeps what you see and what the tyres feel identical.
 *
 * Who owns what: the `World` owns its junctions and roads (`nodes`, `edges`);
 * each keeps its address for as long as it is part of the world.  Everybody
 * else points to them (`Edge*`, `Node*`).  The two arms of a road belong to
 * the road and live as long as it does; a junction's `arms` point to them.
 * What `remove()` takes out of the world stays valid until the next call of
 * `remove()` – long enough for everybody to be told and to let go of it.
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/geom.h"
#include "world/narrowing.h"
#include "world/seggrid.h"
#include "world/sparse.h"
#include "world/types.h"
#include "world/xsection.h"

namespace fs {

struct Node;
struct Edge;

struct Arm {
  Node* node = nullptr;
  Edge* edge = nullptr;
  /** true when the edge leaves the node (node === edge.a) */
  bool out = false;
  /** index in node.arms (counter-clockwise order) */
  int idx = 0;
  int role = ROLE_NONE;
  /** the group of the signal controller that rules this approach – -1: none, or its lights are out of order */
  int sg = -1;
  /** the group of the light that stands at this approach (it stands there when it is out of order, too); -1: none */
  int lamp = -1;
  /** stop/yield line distance from the node */
  double stopS = 0;
  double cut = 0;
  /** unit direction pointing away from the node */
  double dx = 1;
  double dz = 0;
  double ang = 0;
  double half = 0;
  /** sidewalk widths left/right when looking away from the node */
  double swL = 0;
  double swR = 0;
  /** true if vehicles can arrive at the node on this arm */
  bool isApproach = false;
  /** true if vehicles can leave the node on this arm */
  bool isExit = false;
};

struct Edge {
  int id = 0;
  Node* a = nullptr;
  Node* b = nullptr;
  int cls = 0;
  double tier = 0;
  std::string name;
  std::string ref;
  Polyline3 pl;
  double len = 0;
  int lanesF = 0;
  int lanesB = 0;
  double laneW = 0;
  double width = 0;
  /** paved strip outside the lanes on each side */
  double margin = 0;
  /** what lies between the outermost lane and the kerb on the left / right of a→b: the hard strip, a cycle lane, parked cars (src/world/xsection.h) */
  double marginL = 0;
  double marginR = 0;
  /** the cross-section beyond the lanes, left and right of a→b (src/world/xsection.h); null: nothing said */
  std::shared_ptr<const RoadSideJson> xl;
  std::shared_ptr<const RoadSideJson> xr;
  /** a carriageway of a dual road: what lies on its left (MEDIAN bits) */
  int median = 0;
  /** height of the kerb on the left / right */
  double kerbL = 0;
  double kerbR = 0;
  double maxF = 0;
  double maxB = 0;
  int zone = 0;
  int surf = SURF_ASPHALT;
  double swL = 0;
  double swR = 0;
  int flags = 0;
  int access = 0;
  double cutA = 0;
  double cutB = 0;
  int layer = 0;
  std::string turnF;
  std::string turnB;
  int64_t osm = 0;
  /** traffic calming across the carriageway (EdgeJson.cm; src/world/roadfeatures.h), null: none */
  std::optional<std::vector<double>> calm;
  /** kerb build-outs narrowing the carriageway (EdgeJson.bo; src/world/narrowing.h), null: none */
  std::optional<std::vector<Narrowing>> narrow;
  /** 1: meets other roads over a continuous pavement (EdgeJson.mk) */
  int mouth = 0;
  Arm* armA = nullptr;
  Arm* armB = nullptr;
  /** blend lengths over which the surface relaxes from junction plane to profile */
  double blendA = 0;
  double blendB = 0;
  double minX = 0;
  double maxX = 0;
  double minZ = 0;
  double maxZ = 0;
  /** the crossings on this edge (they keep their place for as long as the road lives: `World::crossings` points to them) */
  std::vector<CrossingJson> xings;

  /** the two arms themselves, at a and at b – `armA` and `armB` point here once the world has made the road */
  Arm ends[2];

  Edge() = default;
  Edge(const Edge&) = delete;
  Edge& operator=(const Edge&) = delete;
};

/** One kerb line between two neighbouring arms of a junction. */
struct JunctionCorner {
  /** flat [x,z,…] from the left mouth of arm i to the right mouth of arm i+1 */
  std::vector<double> kerb;
  /** sidewalk width at the start (arm i, its left side) and end (arm i+1, right side) */
  double swA = 0;
  double swB = 0;
  /** index range of the fillet arc inside `kerb` (point indices), -1 when none */
  int arc0 = -1;
  int arc1 = -1;
  double ox = 0;
  double oz = 0;
  double r = 0;
};

struct JunctionShape {
  /** counter-clockwise flat ring [x,z,…] of the carriageway area */
  std::vector<double> ring;
  std::vector<JunctionCorner> corners;
  double minX = 0;
  double maxX = 0;
  double minZ = 0;
  double maxZ = 0;
};

struct Node {
  int id = 0;
  double x = 0;
  double y = 0;
  double z = 0;
  double gx = 0;
  double gz = 0;
  int ctrl = CTRL_NONE;
  std::vector<Arm*> arms;
  /** its signal controller, or -1 */
  int sc = -1;
  /**
   * The traffic lights of this junction are out of order: they flash amber.
   * It is ruled by its signs then – or by right before left –, and `ctrl`
   * says so; the heads still stand there (`Arm.lamp`).
   */
  bool dark = false;
  std::vector<double> fr;
  double tc = 0;
  int64_t osm = 0;
  std::unique_ptr<JunctionShape> shape;
  /**
   * Only the roads that are loaded are known of this junction – its own
   * piece of the map is not (the network comes in chunks).  It has no area
   * and nothing leads through it yet; it is where the loaded network ends.
   */
  bool stub = false;
};

/** What changed when a piece of the network was added or taken away. */
struct WorldChange {
  /** roads that came (or went) */
  std::vector<Edge*> edges;
  /** junctions that are whole now (or no longer) */
  std::vector<Node*> nodes;
};

struct SurfaceHit {
  /** surface height */
  double y = 0;
  /** edge the point belongs to (nearest centreline), or null when off-road */
  Edge* edge = nullptr;
  /** node whose junction area contains the point, or null */
  Node* node = nullptr;
  /** station along the edge (from node a) and signed lateral offset (+ = left of a→b) */
  double s = 0;
  double off = 0;
  /** 0 = carriageway, 1 = sidewalk, 2 = off-road */
  int zone = 2;
  int surf = SURF_GRASS;
};

inline SurfaceHit newSurfaceHit() { return SurfaceHit(); }

/** Point on an arm at distance `d` from its node. */
PolySample& armPoint(const Arm& arm, double d, PolySample& out);

/**
 * Pipeline step: decide how far each arm is cut back from the node and which
 * kerb radius each corner gets.  Writes `arm.cut`, `edge.cutA/B`, `node.fr`.
 */
void computeJunctionCuts(Node& node);

/** Build the carriageway polygon and kerb lines of a junction from stored cuts/radii. */
std::unique_ptr<JunctionShape> buildJunctionShape(const Node& node);

/** The centre line of an edge from its packed form (EdgeJson.q): metres. */
std::vector<double> edgePoints(const std::vector<double>& q);

/** … and into it. */
std::vector<double> packEdgePoints(const std::vector<double>& p);

struct WorldOptions {
  /** the signal controllers (by their number) whose lights are out of order */
  std::function<bool(int controller)> lightsOut;
};

class World {
 public:
  /** what was read first: the description of the map (and, for a map in one piece, all of it) */
  WorldJson json;
  const WorldMetaJson& meta;
  /** by their number in the whole map – with gaps where the map is not loaded: go through `allNodes` / `allEdges` */
  Sparse<Node> nodes;
  Sparse<Edge> edges;
  /** every junction (stubs too) and every road that is loaded */
  OrderedSet<Node> allNodes;
  OrderedSet<Edge> allEdges;
  /** signal controllers by their number (with gaps, like the nodes) */
  Sparse<SignalControllerJson> signals;
  /** the crossings of all loaded roads */
  std::vector<CrossingJson*> crossings;

  /**
   * How many times a piece was taken out of the world (`remove`).  Who keeps things for the
   * roads and junctions that went lets go of them for good when the next piece goes.
   */
  uint64_t removals = 0;

  /**
   * Which signal controllers are out of order (their lights flash amber):
   * asked when a junction is put together.  See `Node.dark`.
   */
  std::function<bool(int controller)> lightsOut;

  /**
   * A map in one piece (all its nodes and edges in `json`) – or, with none
   * in it, an empty one that pieces are added to (`add`).
   */
  explicit World(WorldJson json, WorldOptions opts = {});
  World(const World&) = delete;
  World& operator=(const World&) = delete;

  /**
   * Add a piece of the network: the junctions of a chunk and every road at
   * them.  Roads that are there already (they came with the chunk beside)
   * are kept; junctions known only as the far end of such a road become
   * whole.  Returns the new roads and the junctions that are whole now –
   * for whoever keeps something for each of them (the lanes, the signals).
   */
  WorldChange add(const WorldChunkJson& chunk);

  /**
   * Take a piece of the network away again: these junctions (those of a
   * chunk).  A road goes when the junctions at both its ends are gone; a
   * junction with a road that stays is kept as the end of that road.
   * Returns what went – to be told to whoever keeps something for it,
   * BEFORE this is called if they need to look at it once more.
   *
   * (What went can still be looked at – it is given back to the memory only
   * when `remove` is called the next time.)
   */
  WorldChange remove(const std::vector<int>& nodeIds);

  bool isBanned(const Edge& from, const Node& via, const Edge& to) const { return isBanned(from.id, via.id, to.id); }
  /** the same by their numbers */
  bool isBanned(int from, int via, int to) const;

  /** Height of the junction plane of `node` at (x,z). */
  double planeY(const Node& node, double x, double z) const { return node.y + node.gx * (x - node.x) + node.gz * (z - node.z); }

  /** Road surface height on `edge` at station s for ground point (x,z). */
  double edgeY(const Edge& edge, double s, double x, double z) const;

  /**
   * Road surface under (x,z).  `yRef` picks the right deck where roads cross
   * at different levels (surfaces more than 1.6 m above it are ignored).
   * Returns false (out.zone = 2) when the point is not on any road or
   * sidewalk — the caller then falls back to the terrain.
   */
  bool surfaceAt(double x, double z, double yRef, SurfaceHit& out);

  /**
   * Map-match a point to the road network: nearest edge by lateral distance
   * within `maxDist` metres of its carriageway edge, and at most `maxDy`
   * metres above or below `yRef`.  Returns false if none.
   *
   * `maxDy` defaults to 4.5 m – how far a road may lie above or below the
   * point and still be taken for the one it stands on.  Callers that must
   * not take a road on a bridge for the road under it (src/sim/rules.ts)
   * pass a smaller one.
   */
  bool locate(double x, double z, double yRef, double maxDist, SurfaceHit& out, double maxDy = 4.5);

  /**
   * Nearest road footprint (carriageway + sidewalks) to a ground point, used
   * to shape the terrain around roads.  `out.off` holds the distance OUTSIDE
   * the footprint (0 when inside), `out.y` the road surface height there.
   * Edges with any of the `skipFlags` are ignored.
   */
  bool nearestRoad(double x, double z, double maxDist, int skipFlags, SurfaceHit& out);

  /** Visit the junctions whose area touches the given box (each once): visit(Node&). */
  template <class Visit>
  void nodesInBox(double minX, double minZ, double maxX, double maxZ, Visit&& visit) const {
    std::vector<int> seen;
    nodeGrid_.queryBox(minX, minZ, maxX, maxZ, [&](Node* n) {
      for (int id : seen)
        if (id == n->id) return;
      seen.push_back(n->id);
      const JunctionShape& sh = *n->shape;
      if (sh.maxX < minX || sh.minX > maxX || sh.maxZ < minZ || sh.minZ > maxZ) return;
      visit(*n);
    });
  }

  /** Visit the pieces of centre line near the given box: visit(Edge&, index of the segment) – a piece may come more than once. */
  template <class Visit>
  void segmentsInBox(double minX, double minZ, double maxX, double maxZ, Visit&& visit) const {
    segGrid_.queryBox(minX, minZ, maxX, maxZ, [&](int64_t code) { visit(*edges[code / 4096], static_cast<int>(code % 4096)); });
  }

  /** Visit edges whose padded bounding box touches the given box: visit(Edge&). */
  template <class Visit>
  void edgesInBox(double minX, double minZ, double maxX, double maxZ, Visit&& visit) const {
    std::unordered_set<int64_t> seen;
    segGrid_.queryBox(minX, minZ, maxX, maxZ, [&](int64_t code) {
      const int64_t id = code / 4096;
      if (!seen.insert(id).second) return;
      visit(*edges[id]);
    });
  }

 private:
  /** (a test looks into the grids to ask them as the lookup before this one did: tests/groundlookup_test.cpp) */
  friend struct WorldInside;

  /** forbidden turns: from edge → [via node, to edge, …] (few edges have any) */
  std::unordered_map<int, std::vector<int>> banned_;
  /**
   * edge segments: value = edgeId * 4096 + segmentIndex – and beside each the
   * bounding box of that segment (src/world/seggrid.h), padded by half the
   * carriageway + the wider sidewalk + 0.5 m.  That is wider than anything
   * `surfaceAt` accepts from the segment (at most half + the sidewalk of that
   * side, or √(reach² + 0.04) at the open end of a road), so the box only
   * turns away points the projection would turn away too – it saves fetching
   * the edge and projecting.  Put in and taken out with the segments in
   * `indexEdge`; it depends on the centre line, the width and the sidewalks
   * only, which an edge keeps for its life (the cuts do not matter to it).
   */
  SegGrid segGrid_{40};
  Grid2<Node*> nodeGrid_{40};
  /** what the last `remove` took out of the world (kept until the next one) */
  std::vector<std::unique_ptr<Edge>> goneEdges_;
  std::vector<std::unique_ptr<Node>> goneNodes_;

  double bestY_ = 0;
  double bestScore_ = 0;
  bool bestFound_ = false;

  Node* stubNode(int id, const std::vector<double>& p, size_t at);
  void newArm(Arm& arm, Node* node, Edge* edge, bool out);
  /** (`x`, `bn`: the crossings and forbidden turns to take instead of the record's own – a map in one piece keeps them in lists of their own) */
  Edge* addEdge(int id, const EdgeJson& ej, const std::vector<EdgeCrossingJson>* x = nullptr, const std::vector<int>* bn = nullptr);
  Node* completeNode(int id, const NodeJson& nj);
  /** Compute the cached geometry of an edge and index its segments (`on`), or take it out of the index again. */
  void indexEdge(Edge& e, bool on);
  /**
   * Candidate selection for `surfaceAt`: where decks overlap, the highest
   * surface that is not above the reference wins; on the same level the
   * smaller lateral score (more "inside" its road) wins.
   */
  void consider(SurfaceHit& out, double y, double score, Edge* edge, Node* node, double s, double off, int zone, int surf);
};

/**
 * Pipeline helper: create a `World` whose cuts have not been computed yet,
 * compute them, and write them back into the JSON.
 */
void computeAllCuts(WorldJson& json);

inline bool hasFlag(const Edge& e, int flag) { return (e.flags & flag) != 0; }

}  // namespace fs

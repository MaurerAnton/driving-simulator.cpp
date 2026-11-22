// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Runtime road network: linked nodes/edges/arms, junction geometry, spatial
 * index and the analytic road-surface function.
 *
 * The same code runs in the map pipeline (to build meshes and conform the
 * terrain) and in the game (physics ground, map matching, AI), which is what
 * keeps what you see and what the tyres feel identical.
 *
 * Origin: src/world/world.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/geom.h"
#include "world/types.h"

namespace fs {

struct Node;

struct Edge;

/** One kerb line between two neighbouring arms of a junction. */
struct JunctionCorner {
  /** flat [x,z,…] from the left mouth of arm i to the right mouth of arm i+1 */
  std::vector<double> kerb;
  /** sidewalk width at the start (arm i, its left side) and end (arm i+1, right side) */
  double swA = 0, swB = 0;
  /** index range of the fillet arc inside `kerb` (point indices), -1 when none */
  int arc0 = -1, arc1 = -1;
  double ox = 0, oz = 0, r = 0;
};

struct JunctionShape {
  /** counter-clockwise flat ring [x,z,…] of the carriageway area */
  std::vector<double> ring;
  std::vector<JunctionCorner> corners;
  double minX = 0, maxX = 0, minZ = 0, maxZ = 0;
};

/** One arm of a junction: where a road meets a node. */
struct Arm {
  Node* node = nullptr;
  Edge* edge = nullptr;
  /** true when the edge leaves the node (node === edge.a) */
  bool out = false;
  /** index in node.arms (counter-clockwise order) */
  int idx = 0;
  int role = ROLE_NONE;
  int sg = -1;
  /** stop/yield line distance from the node */
  double stopS = 0;
  double cut = 0;
  /** unit direction pointing away from the node */
  double dx = 1, dz = 0;
  double ang = 0, half = 0;
  /** sidewalk widths left/right when looking away from the node */
  double swL = 0, swR = 0;
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
  int lanesF = 0, lanesB = 0;
  double laneW = 0, width = 0;
  /** paved strip outside the lanes on each side */
  double margin = 0;
  double maxF = 0, maxB = 0;
  int zone = 0;
  int surf = SURF_ASPHALT;
  double swL = 0, swR = 0;
  int flags = 0;
  int access = ACCESS_YES;
  double cutA = 0, cutB = 0;
  int layer = 0;
  std::string turnF, turnB;
  long long osm = 0;
  Arm* armA = nullptr;
  Arm* armB = nullptr;
  /** blend lengths over which the surface relaxes from junction plane to profile */
  double blendA = 0, blendB = 0;
  double minX = 0, maxX = 0, minZ = 0, maxZ = 0;
};

struct Node {
  int id = 0;
  double x = 0, y = 0, z = 0;
  double gx = 0, gz = 0;
  int ctrl = CTRL_NONE;
  std::vector<Arm> arms;
  int sc = -1;
  std::vector<double> fr;
  double tc = 0;
  long long osm = 0;
  JunctionShape* shape = nullptr;
};

/** Where the ground is under a point: height, surface, edge and zone. */
struct SurfaceHit {
  /** true when a road surface (not the terrain) was found */
  bool found = false;
  Edge* edge = nullptr;
  Node* node = nullptr;
  /** height of the surface at the query point (m) */
  double y = 0;
  /** station along the edge (m from node a) */
  double s = 0;
  /** signed lateral offset from the centreline (m, left = +) */
  double off = 0;
  /** 0 = carriageway, 1 = sidewalk, 2 = not on a road (caller uses terrain) */
  int zone = 2;
  /** surface class (SURF_*) */
  int surf = SURF_ASPHALT;
};

SurfaceHit newSurfaceHit();

/** Position/tangent on an arm at distance d from the node. */
PolySample armPoint(const Arm& arm, double d, PolySample out);

/** Junction cut-backs (ca/cb) for a node's arms. */
void computeJunctionCuts(Node& node);

/** Kerb line + ring of the carriageway at a node (nullptr when degenerate). */
std::unique_ptr<JunctionShape> buildJunctionShape(Node& node);

/** Unit direction of an arm away from its node. */
void computeArmDirection(Arm& arm);

class World {
 public:
  explicit World(const WorldJson& json);

  const WorldMetaJson& meta() const { return json_->meta; }
  const WorldJson& json() const { return *json_; }
  std::vector<Node>& nodes() { return nodes_; }
  std::vector<Edge>& edges() { return edges_; }
  const std::vector<SignalControllerJson>& signals() const { return signals_; }
  const std::vector<CrossingJson>& crossings() const { return crossings_; }

  /** True when the movement (fromEdge, viaNode, toEdge) is forbidden. */
  bool isBanned(int fromEdge, int viaNode, int toEdge) const;

  /** Nearest edge to (x, z) within `radius` metres, or nullptr. */
  Edge* nearestEdge(double x, double z, double radius = 30);

  /** Height of the junction plane of `node` at (x,z). */
  double planeY(const Node& node, double x, double z) const;

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
   * within `maxDist` metres of its carriageway edge.  Returns false if none.
   */
  bool locate(double x, double z, double yRef, double maxDist, SurfaceHit& out);

  /**
   * Nearest road footprint (carriageway + sidewalks) to a ground point, used
   * to shape the terrain around roads.  `out.off` holds the distance OUTSIDE
   * the footprint (0 when inside), `out.y` the road surface height there.
   * Edges with any of the `skipFlags` are ignored.
   */
  bool nearestRoad(double x, double z, double maxDist, int skipFlags, SurfaceHit& out);

  /** Visit edges whose padded bounding box touches the given box. */
  void edgesInBox(double minX, double minZ, double maxX, double maxZ,
                  const std::function<void(Edge&)>& visit);

 private:
  const WorldJson* json_;
  std::vector<SignalControllerJson> signals_;   // declared before nodes_/edges_: the
  std::vector<CrossingJson> crossings_;        // constructor copies them from the JSON
  std::vector<Node> nodes_;
  std::vector<Edge> edges_;
  /** forbidden turns: from edge → [via node, to edge, …] (few edges have any) */
  std::map<int, std::vector<int>> banned_;
  Grid2<int> segGrid_{40};   // edge id * 4096 + segment index
  Grid2<Node*> nodeGrid_{40};
  std::vector<JunctionShape> shapes_;

  void finishEdge(Edge& e);
};

/** Is (x,z) on the sidewalk strip behind a junction kerb line? */
bool pointNearKerb(double x, double z, const JunctionCorner& c);

/** Flag test helper (the JS `hasFlag`). */
inline bool hasFlag(const Edge& e, int flag) { return (e.flags & flag) != 0; }


/**
 * Read `map/world.json` (the document of this visit).  False when it is not
 * that document – the caller keeps what it has (as a failed fetch does).
 */
bool parseWorldJson(const std::string& text, WorldJson& out);

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The road-network model shared by the map pipeline and the game: constants,
 * enums and the serialised shapes of `public/map/world.json`.
 *
 * Origin: src/world/types.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * The values are carried over exactly, including the comment on each flag.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fs {

// ---------------------------------------------------------------------------
// Road classes
// ---------------------------------------------------------------------------

enum RoadClass : int {
  MOTORWAY = 0,
  MOTORWAY_LINK = 1,
  TRUNK = 2,
  TRUNK_LINK = 3,
  PRIMARY = 4,
  PRIMARY_LINK = 5,
  SECONDARY = 6,
  SECONDARY_LINK = 7,
  TERTIARY = 8,
  TERTIARY_LINK = 9,
  UNCLASSIFIED = 10,
  RESIDENTIAL = 11,
  LIVING_STREET = 12,
  SERVICE = 13,
  TRACK = 14,
};

extern const char* const ROAD_CLASSES[15];

/**
 * Priority tier used to derive right-of-way where OSM carries no explicit
 * sign.  Higher tier = more important road.  Links rank below the roads they
 * feed so that ramps and slip lanes give way at their end.
 * Tier < 2 means "§ 10 StVO": leaving such a road you yield to everybody.
 */
extern const double CLASS_TIER[15];

/** Default lane width (m) per class when OSM has no width. */
extern const double CLASS_LANE_WIDTH[15];

// ---------------------------------------------------------------------------
// Surfaces
// ---------------------------------------------------------------------------

enum Surf : int {
  SURF_ASPHALT = 0,
  SURF_CONCRETE = 1,
  SURF_PAVING = 2,
  SURF_SETT = 3,
  SURF_GRAVEL = 4,
  SURF_DIRT = 5,
  SURF_GRASS = 6,
};

/** Tyre grip multiplier per surface. */
extern const double SURF_GRIP[7];
/** Rolling-resistance multiplier per surface. */
extern const double SURF_ROLLING[7];

// ---------------------------------------------------------------------------
// Edge flag bits
// ---------------------------------------------------------------------------

enum EdgeFlags : uint32_t {
  EF_BRIDGE = 1,
  EF_TUNNEL = 2,
  EF_ROUNDABOUT = 4,
  EF_LINK = 8,
  EF_LIT = 16,
  /** continuous centre line (overtaking prohibited / lane separation) */
  EF_SOLID_CENTER = 32,
  /** carriageway has painted lane/centre markings at all */
  EF_MARKED = 64,
  /** painted edge lines */
  EF_EDGE_LINES = 128,
  /** built-up area (innerorts) */
  EF_URBAN = 256,
  /** parking aisle / driveway / alley */
  EF_MINOR_SERVICE = 512,
  /** structurally separated carriageway of a dual road */
  EF_DUAL = 1024,
  /** bicycle road (Zeichen 244.1) */
  EF_BICYCLE_ROAD = 2048,
};

// ---------------------------------------------------------------------------
// Access / junction control / approach role / crossings
// ---------------------------------------------------------------------------

enum Access : int {
  ACCESS_YES = 0,
  /** Anlieger frei */
  ACCESS_DESTINATION = 1,
  /** private / no / customers-only yard: entering is a violation in the sim */
  ACCESS_PRIVATE = 2,
  /** agricultural / forestry traffic only */
  ACCESS_AGRICULTURAL = 3,
};

enum Ctrl : int {
  /** plain joint (2 arms) or dead end */
  CTRL_NONE = 0,
  /** rechts vor links */
  CTRL_RBL = 1,
  /** priority regulated by signs (or by § 10 for minor arms) */
  CTRL_SIGNS = 2,
  /** traffic lights */
  CTRL_SIGNALS = 3,
  CTRL_ROUNDABOUT = 4,
  /** motorway-style merge/diverge */
  CTRL_MERGE = 5,
  /** edge of the simulated world */
  CTRL_MAP_END = 6,
};

enum Role : int {
  /** has priority (Zeichen 301/306, roundabout ring, through carriageway) */
  ROLE_PRIORITY = 0,
  /** Zeichen 205 */
  ROLE_YIELD = 1,
  /** Zeichen 206 */
  ROLE_STOP = 2,
  /** equal-ranked: right before left */
  ROLE_RBL = 3,
  /** § 10 StVO: driveway, track, traffic-calmed area, lowered kerb */
  ROLE_MINOR = 4,
  /** not an approach (one-way leaving the node) or plain joint */
  ROLE_NONE = 5,
};

enum Xing : int {
  XING_UNMARKED = 0,
  XING_ZEBRA = 1,
  XING_SIGNAL = 2,
  XING_RAIL = 3,
  XING_TRAM = 4,
};

/** Height of a standard kerb in metres. */
constexpr double KERB_H = 0.12;

// ---------------------------------------------------------------------------
// Serialised form (world.json)
// ---------------------------------------------------------------------------

struct EdgeJson {
  int a = 0, b = 0;   // node indices
  int c = 0;          // class index
  std::string n;      // name
  std::string r;      // ref
  /** centreline, [x, y, z] triples in world metres (cm precision) */
  std::vector<double> p;
  int lf = 0;         // lanes forward (along a→b)
  int lb = 0;         // lanes backward
  double lw = 0;      // lane width
  double w = 0;       // carriageway width
  /** paved margin outside the lanes on each side (hard strip / shoulder), default 0 */
  double mg = 0;
  double mf = 0;      // max speed forward km/h (0 = unlimited)
  double mb = 0;      // max speed backward km/h
  int z = 0;          // 0, or 30/20 for Tempo zones, 7 for verkehrsberuhigter Bereich
  int s = 0;          // surface (default asphalt)
  double sl = 0;      // sidewalk width on the left of a→b
  double sr = 0;      // sidewalk width on the right
  int f = 0;          // EF flags
  int ac = 0;         // ACCESS
  double ca = 0;      // junction cut-back at node a
  double cb = 0;      // junction cut-back at node b
  int ly = 0;         // layer
  std::string tf;     // turn:lanes forward (left→right, as in OSM)
  std::string tb;     // turn:lanes backward
  long long o = 0;    // OSM way id (provenance / debugging)
};

struct ArmJson {
  int e = 0;          // edge index
  /** approach role (ROLE) for traffic arriving on this arm */
  int r = 0;
  /** signal group index inside the node's controller, or -1 */
  int g = -1;
  /** distance from the node to the stop/yield line along the edge (>= cut) */
  double sl = 0;
};

struct NodeJson {
  double x = 0, y = 0, z = 0;
  /** junction plane gradient (dy/dx, dy/dz) */
  double gx = 0, gz = 0;
  int t = 0;          // CTRL
  /** arms in counter-clockwise order */
  std::vector<ArmJson> arms;
  /** signal controller index */
  int sc = -1;
  /** corner fillet radius per arm gap (arm i → arm i+1), parallel to `arms` */
  std::vector<double> fr;
  /** turning circle radius (dead ends) */
  double tc = 0;
  long long o = 0;    // OSM node id
};

struct SignalPhaseJson {
  /** duration in seconds */
  double d = 0;
  /** one char per group: R red, Y red+yellow, G green, A amber (yellow), O off */
  std::string s;
};

struct SignalControllerJson {
  double x = 0, z = 0;
  int groups = 0;
  std::vector<SignalPhaseJson> phases;
  /** cycle offset in seconds (staggers neighbouring junctions) */
  double off = 0;
  /** pedestrian-crossing light (rests on green for vehicles) */
  bool ped = false;
};

struct CrossingJson {
  int e = 0;          // edge
  double s = 0;       // station along the edge (from node a)
  int k = 0;          // XING
  double w = 0;       // width of the crossing strip along the road
  bool isl = false;
  /** signal controller + group for signalised crossings */
  int sc = -1;
  int g = -1;
};

struct PlaceJson {
  std::string name;
  std::string kind;
  double x = 0, z = 0;
};

/** A spot where the player can start (parking lots, driving school yard). */
struct SpawnJson {
  std::string name;
  double x = 0, z = 0;
  /** yaw in radians (0 = north, positive = left/counter-clockwise) */
  double yaw = 0;
  std::string kind;  // 'parking' | 'street' | 'school'
};

struct WorldMetaJson {
  std::string name;
  std::string generated;
  /** world-space bounds in metres */
  double minX = 0, maxX = 0, minZ = 0, maxZ = 0;
  double tileSize = 0;
  double originE = 0, originN = 0;
  std::vector<std::string> attribution;
};

enum RailKind : int {
  RAIL_RAIL = 0,
  RAIL_TRAM = 1,
};

/** A piece of track between two switches (or ends), as a path for the vehicles that run on it. */
struct RailJson {
  /** RAIL */
  int k = 0;
  /** top of the rail along the middle of the track: x, y, z, … a point every few metres */
  std::vector<double> p;
  /** the pieces that join at the start and at the end: index * 2 + (0: their start is here, 1: their end) */
  std::vector<int> a;
  std::vector<int> b;
  /** stops: metres from the start */
  std::vector<double> st;
  bool hasSt = false;
};

/** A level, open rectangle of the map: its middle, its size, and whether its long side runs east-west (0) or north-south (1). */
struct YardJson {
  double x = 0, z = 0;
  double len = 0, wid = 0;
  int rot = 0;
};

struct WorldJson {
  int version = 0;
  WorldMetaJson meta;
  std::vector<NodeJson> nodes;
  std::vector<EdgeJson> edges;
  /** forbidden movements: [fromEdge, viaNode, toEdge] */
  std::vector<std::vector<int>> banned;
  std::vector<SignalControllerJson> signals;
  std::vector<CrossingJson> crossings;
  std::vector<PlaceJson> places;
  std::vector<SpawnJson> spawns;
  /** tile keys ("ix_iz") that exist on disk */
  std::vector<std::string> tiles;
  /** the open field the practice yard of the truck lessons is laid on (0.7.0) */
  bool hasYard = false;
  YardJson yard;
  /** tram and railway tracks as paths (maps built before trams ran have none) */
  std::vector<RailJson> rails;
};

}  // namespace fs

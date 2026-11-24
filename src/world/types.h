// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/types.ts @ v1.7.7
/**
 * The road-network model shared by the map pipeline (tools/) and the game.
 *
 * The pipeline writes `public/map/world.json` in the compact `WorldJson`
 * shape; `World` (world.h) turns it into the linked network used at
 * runtime (lanes, connectors, spatial index, surface queries).
 *
 * The constant objects of the original are written with an underscore here:
 * EF.BRIDGE is EF_BRIDGE, CTRL.SIGNALS is CTRL_SIGNALS, CLS.MOTORWAY is plain
 * MOTORWAY.  In the records read from JSON a field that may be missing has the
 * value the original's readers take for it (`ej.mg ?? 0`: mg = 0) – or is a
 * std::optional where it matters to somebody that nothing was said.
 */
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "world/grid.h"
#include "world/xsection.h"

namespace fs {

extern const char* const ROAD_CLASSES[15];

/** CLS */
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

/**
 * Priority tier used to derive right-of-way where OSM carries no explicit
 * sign.  Higher tier = more important road.  Links rank below the roads they
 * feed so that ramps and slip lanes give way at their end.
 * Tier < 2 means "§ 10 StVO": leaving such a road you yield to everybody.
 */
extern const double CLASS_TIER[15];

/** Default lane width (m) per class when OSM has no width. */
extern const double CLASS_LANE_WIDTH[15];

/** SURF */
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

/** Edge flag bits (EF). */
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

/** Who may drive here (ACCESS). */
enum Access : int {
  ACCESS_YES = 0,
  /** Anlieger frei */
  ACCESS_DESTINATION = 1,
  /** private / no / customers-only yard: entering is a violation in the sim */
  ACCESS_PRIVATE = 2,
  /** agricultural / forestry traffic only */
  ACCESS_AGRICULTURAL = 3,
};

/** Junction control type (CTRL). */
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

/** Role of an approach (traffic arriving at the node on this arm) (ROLE). */
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

/** Pedestrian crossing kinds (XING). */
enum Xing : int { XING_UNMARKED = 0, XING_ZEBRA = 1, XING_SIGNAL = 2, XING_RAIL = 3, XING_TRAM = 4 };

/** What runs on a track: trams, light rail (regional trams), trains (RAIL). */
enum RailKind : int { RAIL_TRAM = 0, RAIL_LIGHT = 1, RAIL_TRAIN = 2 };

/** A level, open rectangle of the map: its middle, its size, and whether its long side runs east–west (0) or north–south (1). */
struct YardJson {
  double x = 0;
  double z = 0;
  double len = 0;
  double wid = 0;
  int rot = 0;
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
  /** stops: metres from the start (none: empty) */
  std::vector<double> st;
  /** their names, as the stops are signed (tools/build-stops.ts; '' where the map has none) */
  std::vector<std::string> sn;
  /** on which side of the track their platform lies: 1 right, -1 left of the direction it was drawn in, 0 not known */
  std::vector<int> ss;
  /** an overhead wire along it: 1 yes, 0 no – missing when the map does not say */
  std::optional<int> e;
};

/** What a stop has, and what kind of stop it is (bits) (STOP_FLAG). */
inline constexpr int STOP_FLAG_SHELTER = 1;
inline constexpr int STOP_FLAG_BENCH = 2;
inline constexpr int STOP_FLAG_BIN = 4;
/** a departure board */
inline constexpr int STOP_FLAG_BOARD = 8;
/** tactile paving at the edge */
inline constexpr int STOP_FLAG_TACTILE = 16;
inline constexpr int STOP_FLAG_LIT = 32;
inline constexpr int STOP_FLAG_TRAM = 64;
inline constexpr int STOP_FLAG_BUS = 128;
inline constexpr int STOP_FLAG_RAIL = 256;
/** a pole that stands on the road itself: the tiles already show its sign */
inline constexpr int STOP_FLAG_ON_ROAD = 512;
/** a platform drawn as an area, not as a line */
inline constexpr int STOP_FLAG_AREA = 1024;

/**
 * The stops of the region (stops.json.gz, tools/build-stops.ts).  A place is
 * one stop under one name – both directions, all its poles and platforms.
 */
struct StopsJson {
  int v = 0;
  std::vector<std::string> towns;
  /** name, index into towns, kind (STOP_FLAG.TRAM | BUS | RAIL), x, z (the middle of its members) */
  std::vector<std::tuple<std::string, int, int, double, double>> places;
  /** where people wait: place index, x, z, STOP_FLAG bits */
  std::vector<std::tuple<int, double, double, int>> poles;
  /** platforms drawn as ways: place index, STOP_FLAG bits, width (0: not known), x, z, … */
  std::vector<std::tuple<int, int, double, std::vector<double>>> platforms;
};

struct RunwayJson {
  /** "04/22" */
  std::string ref;
  /** the ends of its middle line */
  double x1 = 0;
  double z1 = 0;
  double x2 = 0;
  double z2 = 0;
  /** width (m) and surface (OpenStreetMap: asphalt, concrete, grass, …) */
  double w = 0;
  std::string s;
};

struct TaxiwayJson {
  double w = 0;
  std::string s;
  std::vector<double> p;
};

struct ApronJson {
  std::string s;
  std::vector<double> p;
};

struct AirfieldJson {
  std::string name;
  std::string icao;
  /** airport, airfield or gliding */
  std::string kind;
  double x = 0;
  double z = 0;
  std::vector<RunwayJson> runways;
  std::vector<TaxiwayJson> taxiways;
  std::vector<ApronJson> aprons;
};

/**
 * A line of the trams (lines.json.gz, tools/build-lines.ts): one direction of
 * it – from its first stop to its last –, the pieces of track it runs (by
 * their number in rails.json.gz, and which way: 1 as drawn, −1 against it)
 * and the stops it calls at, in their order (piece, mark).
 */
struct LineJson {
  /** "5", "RT4" */
  std::string ref;
  std::string name;
  std::string from;
  std::string to;
  /** "#e3000f" or '' */
  std::string colour;
  /** 0 tram, 1 regional tram (light rail) */
  int kind = 0;
  std::vector<std::pair<int, int>> paths;
  std::vector<std::pair<int, double>> stops;
};

struct LinesJson {
  int v = 0;
  std::vector<LineJson> lines;
};

/**
 * A line of the city buses (buslines.json.gz, tools/build-buslines.ts): one
 * direction of it, as a route relation of OpenStreetMap has it – the roads
 * it drives along, in their order, and the stops it calls at.
 *
 * A road is named by its number in the whole map (the same in net.bin.gz
 * and in every chunk of the network that holds it) and driven in one
 * direction: a "way", road * 2 + (0: from its junction a to b, 1: from b to
 * a), as routegraph.h counts them.  The numbers belong to one build of the
 * road network: `map` says which.
 */
struct BusLineJson {
  /** "52", "N3" ('' where the relation has none) */
  std::string ref;
  std::string name;
  std::string from;
  std::string to;
  /** (`operator` in the file) */
  std::string operator_;
  /** "#e3000f" or '' */
  std::string colour;
  /** 0 bus, 1 trolleybus */
  int kind = 0;
  /** the route relation in OpenStreetMap */
  int64_t osm = 0;
  /** the ways it drives, in order – each as the difference to the way before (the first as it is): mostly ±2 */
  std::vector<int> w;
  /**
   * how many of the ways are the route itself: after them come the ways
   * from its end round to where the line `back` begins (missing: none)
   */
  std::optional<int> end;
  /** the stops it calls at, in order: stop place (its number in stops.json.gz), index into the ways, metres along that way (in the direction of travel) – three numbers each */
  std::vector<double> st;
  /** the line (index in `lines`) it goes on as at the end of its route – missing where none begins there */
  std::optional<int> back;
};

struct BusLinesJson {
  int v = 0;
  /** the build of the road network the road numbers belong to (meta.generated of map.json) */
  std::string map;
  /** how many stop places stops.json.gz had (the stops are numbered in it) */
  int places = 0;
  std::vector<BusLineJson> lines;
};

struct TimetableFeedJson {
  std::string name;
  std::string version;
  std::string url;
  /** first and last day it is valid for (YYYY-MM-DD) */
  std::string start;
  std::string end;
};

/** One way the trips of a line run: the same stops at the same intervals. */
struct TimetablePatternJson {
  /** its stops (indices into TimetableJson.stops), in the order it calls at them – only those of the line on the map */
  std::vector<int> s;
  /** the same stops by their place in the line's `stops` in lines.json.gz (where its piece of track and mark are) */
  std::vector<int> j;
  /** the departure from each, seconds after the departure from the first */
  std::vector<double> d;
  /** the arrival at each, the same way – only where some arrival differs from its departure */
  std::optional<std::vector<double>> a;
  /** where its trams go: the last stop of the whole trip */
  std::string to;
  /** 1: the trips come from further back than the first of `s` / go on beyond the last (the map's tracks end there) */
  std::optional<int> pre;
  std::optional<int> post;
  /** when its trips leave the first stop, seconds after midnight, sorted (past 86 400: the night after) – Monday–Friday, Saturday, Sunday and holidays */
  std::array<std::vector<double>, 3> t;
};

struct TimetableLineJson {
  /** the line's place in lines.json.gz `lines`, and its ref there (to see that the two files belong together) */
  int line = 0;
  std::string ref;
  /** its colour ("#00954B", or a name OpenStreetMap uses: "red"): OpenStreetMap's, else the feed's route_color – missing where neither has one (version 2) */
  std::optional<std::string> colour;
  std::vector<TimetablePatternJson> patterns;
};

/** One way the trips of a bus line run: the same stops at the same intervals. */
struct TimetableBusPatternJson {
  /** the line's stops it calls at, in order, by their place in the line's `s`: the first as it is, each next as the step from the one before */
  std::vector<int> j;
  /** the departure from each stop, as the seconds since the departure from the one before (the first: 0) */
  std::vector<double> d;
  /** how long it stands at each stop before it leaves (its arrival is that much before its departure), seconds – only where it stands somewhere */
  std::optional<std::vector<double>> w;
  /** where it goes as its front says, and from which stop on: [k, index into `dests`], … – the first for k = 0 */
  std::vector<int> to;
  /** 1: its trips come from further back than its first stop / go on beyond its last (beyond the line's stops on the map) */
  std::optional<int> pre;
  std::optional<int> post;
  /** when its trips leave the first stop – Monday–Friday, Saturday, Sunday and holidays: the first in seconds after midnight (past 86 400: the night after), each next as the step from the one before */
  std::array<std::vector<double>, 3> t;
};

struct TimetableBusLineJson {
  /** the line's place in buslines.json.gz `lines`, its ref and its route relation there (to see that the two files belong together) */
  int line = 0;
  std::string ref;
  int64_t osm = 0;
  /** its colour: OpenStreetMap's, else the feed's route_color – missing where neither has one */
  std::optional<std::string> colour;
  /** the stop places of the line's stops, in the order BusLines (src/world/buslines.ts) keeps them – the patterns name the stops by their place here */
  std::vector<int> s;
  std::vector<TimetableBusPatternJson> patterns;
};

/**
 * The timetables of the buses: for each line of buslines.json.gz that has
 * one, the ways its trips run – which of the line's stops they call at, and
 * the seconds between them – and when they leave.  Lists that count up are
 * kept as the steps from one number to the next, which packs small.
 */
struct TimetableBusJson {
  /** the bus lines the timetables belong to: their build (buslines.json.gz `map`) and how many stop places their stops count on */
  std::string map;
  int places = 0;
  /** the names of the stop places the buses call at: [number in stops.json.gz, name] */
  std::vector<std::pair<int, std::string>> stops;
  /** where the buses go, as their fronts say it: the patterns name them by their place here */
  std::vector<std::string> dests;
  std::vector<TimetableBusLineJson> lines;
};

/**
 * The timetables of the trams and buses (timetable.json.gz,
 * tools/build-timetable.ts, read by src/sim/timetable.ts): for each line of
 * lines.json.gz that has one, the ways its trips run – the stops in their
 * order and the seconds between them – and when they leave, on a working
 * day, a Saturday and a Sunday.  Taken from a GTFS feed, from one ordinary
 * week of it.  Version 2 adds the buses (`bus`) and where the trams' stops
 * are among the stop places (`stopPlaces`); version 1 files have the trams
 * alone and are read as well.
 */
struct TimetableJson {
  /** 1: the trams only; 2: with `stopPlaces` and `bus` (both may be missing) */
  int version = 0;
  /** when the file was made (ISO 8601) */
  std::string generated;
  /** the feeds it was made from */
  std::vector<TimetableFeedJson> feeds;
  /** the days all of them are valid for (YYYY-MM-DD) */
  std::array<std::string, 2> valid;
  /** the week the trips were taken from (its Monday and Sunday) */
  std::array<std::string, 2> week;
  /** the day of it each day type comes from: Monday–Friday, Saturday, Sunday and holidays */
  std::array<std::string, 3> days;
  /** whom to name, and under which licence, wherever the times are shown */
  std::string attribution;
  /** the names of the stops, as the tracks have them (rails.json.gz `sn`): the patterns refer to them by their place here */
  std::vector<std::string> stops;
  std::vector<TimetableLineJson> lines;
  /** for each of `stops`: the stop places of stops.json.gz the trams call at under that name (by their number there; none where the build found none) */
  std::optional<std::vector<std::vector<int>>> stopPlaces;
  /** the timetables of the buses (version 2) */
  std::optional<TimetableBusJson> bus;
};

/** The airfields of the region (air.json.gz, tools/build-stops.ts). */
struct AirJson {
  int v = 0;
  std::vector<AirfieldJson> airfields;
};

/** Height of a standard kerb in metres. */
constexpr double KERB_H = 0.12;

// ---------------------------------------------------------------------------
// Serialised form (world.json)
// ---------------------------------------------------------------------------

/** A crossing as part of its edge's record (CrossingJson without the edge). */
struct EdgeCrossingJson {
  double s = 0;
  int k = 0;
  double w = 0;
  bool isl = false;
  std::optional<int> sc;
  std::optional<int> g;
};

struct CrossingJson : EdgeCrossingJson {
  int e = 0;  // edge
  // s: station along the edge (from node a)
  // k: XING
  // w: width of the crossing strip along the road
  // sc, g: signal controller + group for signalised crossings
};

struct EdgeJson {
  /** node indices */
  int a = 0;
  int b = 0;
  int c = 0;      // class index
  std::string n;  // name
  std::string r;  // ref
  /** centreline, [x, y, z] triples in world metres (cm precision) */
  std::vector<double> p;
  /**
   * the same in whole centimetres, each point as the step from the one before
   * (the first from the origin) – what chunks of the network carry instead
   * of `p`, which is then empty: it takes half the room
   */
  std::optional<std::vector<double>> q;
  int lf = 0;     // lanes forward (along a→b)
  int lb = 0;     // lanes backward
  double lw = 0;  // lane width
  double w = 0;   // carriageway width (paved surface between kerbs/verges)
  /** paved margin outside the lanes on each side (hard strip / shoulder), default 0 */
  double mg = 0;
  double mf = 0;  // max speed forward km/h (0 = unlimited)
  double mb = 0;  // max speed backward km/h
  /** 0, or 30/20 for Tempo zones, 7 for verkehrsberuhigter Bereich */
  int z = 0;
  int s = 0;      // surface (default asphalt)
  double sl = 0;  // sidewalk width on the left of a→b
  double sr = 0;  // sidewalk width on the right
  int f = 0;      // EF flags
  int ac = 0;     // ACCESS
  double ca = 0;  // junction cut-back at node a
  double cb = 0;  // junction cut-back at node b
  int ly = 0;     // layer
  std::string tf;  // turn:lanes forward (left→right, as in OSM)
  std::string tb;  // turn:lanes backward
  int64_t o = 0;   // OSM way id (provenance / debugging)
  /** crossings on this edge (where the road network comes in chunks: there they travel with their edge) */
  std::vector<EdgeCrossingJson> x;
  /** forbidden movements from this edge, in chunks: via node, to edge, via node, to edge, … */
  std::vector<int> bn;
  /** the cross-section beyond the lanes on the left and right of a→b – parking, cycle lanes, verge, kerb (src/world/xsection.h) */
  std::shared_ptr<const RoadSideJson> xl;
  std::shared_ptr<const RoadSideJson> xr;
  /** a one-way carriageway of a dual road: what lies on its left, towards the other one (MEDIAN bits) */
  int md = 0;
  /** traffic calming across the carriageway: per entry station, CALM kind, length, height (src/world/roadfeatures.h) */
  std::optional<std::vector<double>> cm;
  /** kerb build-outs narrowing the carriageway: per entry station, side, length, depth, BO flags (src/world/narrowing.h) */
  std::optional<std::vector<double>> bo;
  /** 1: meets the roads at its ends over a continuous pavement – a lowered kerb across its mouth (src/world/roadfeatures.h) */
  int mk = 0;
  /** give-way line (Zeichen 341) at a yield: 1 painted, 0 not; absent: as the road's markings say */
  std::optional<int> gl;
  /** zigzag lines (Zeichen 299) along the kerb: 1 left, 2 right of a→b (bits) */
  int zz = 0;
  /** text painted on the carriageway: [text, station, +1 read going a→b / −1 going b→a], … */
  std::vector<std::tuple<std::string, double, int>> rt;
  /** zigzag lines over a stretch only: [from station, to station, side bits], … (a bus stop's) */
  std::vector<double> zs;
};

struct ArmJson {
  int e = 0;  // edge index
  /** approach role (ROLE) for traffic arriving on this arm */
  int r = 0;
  /** signal group index inside the node's controller, or -1 */
  int g = -1;
  /** distance from the node to the stop/yield line along the edge (>= cut) */
  double sl = 0;
};

struct NodeJson {
  double x = 0;
  double y = 0;
  double z = 0;
  /** junction plane gradient (dy/dx, dy/dz) */
  double gx = 0;
  double gz = 0;
  int t = 0;  // CTRL
  /** arms in counter-clockwise order */
  std::vector<ArmJson> arms;
  /** signal controller index (-1: none) */
  int sc = -1;
  /** corner fillet radius per arm gap (arm i → arm i+1), parallel to `arms` */
  std::vector<double> fr;
  /** turning circle radius (dead ends) */
  double tc = 0;
  int64_t o = 0;  // OSM node id
};

struct SignalPhaseJson {
  /** duration in seconds */
  double d = 0;
  /** one char per group: R red, Y red+yellow, G green, A amber (yellow), O off */
  std::string s;
};

struct SignalControllerJson {
  double x = 0;
  double z = 0;
  int groups = 0;
  std::vector<SignalPhaseJson> phases;
  /** cycle offset in seconds (staggers neighbouring junctions) */
  double off = 0;
  /** pedestrian-crossing light (rests on green for vehicles) */
  bool ped = false;
};

struct PlaceJson {
  std::string name;
  std::string kind;
  double x = 0;
  double z = 0;
};

/** A spot where the player can start (parking lots, driving school yard). */
struct SpawnJson {
  std::string name;
  double x = 0;
  double z = 0;
  /** yaw in radians (0 = north, positive = left/counter-clockwise) */
  double yaw = 0;
  /** 'parking' | 'street' | 'school' */
  std::string kind;
  /**
   * The place was taken from the overview of the whole map (its roads were
   * not loaded): it lies on a road only roughly, and is put into a lane once
   * the roads there are loaded.
   */
  bool rough = false;
  /** the town or municipality it lies in: the list of start points is grouped by it ('': none said) */
  std::string g;
};

/** WorldJson['meta'] */
struct WorldMetaJson {
  std::string name;
  std::string generated;
  /** when tiles of the map were replaced after its build (tools/fetch-map.ts: tiles-patch.tar) – part of their address */
  std::string patched;
  /** world-space bounds in metres */
  double minX = 0;
  double maxX = 0;
  double minZ = 0;
  double maxZ = 0;
  double tileSize = 0;
  double originE = 0;
  double originN = 0;
  std::vector<std::string> attribution;
};

struct WorldJson {
  int version = 0;
  WorldMetaJson meta;
  std::vector<NodeJson> nodes;
  std::vector<EdgeJson> edges;
  /** forbidden movements: [fromEdge, viaNode, toEdge] */
  std::vector<std::array<int, 3>> banned;
  std::vector<SignalControllerJson> signals;
  std::vector<CrossingJson> crossings;
  std::vector<PlaceJson> places;
  std::vector<SpawnJson> spawns;
  /** tram and railway tracks as paths (maps built before trams ran have none) */
  std::vector<RailJson> rails;
  /** the open field the practice yard of the truck lessons is laid on */
  std::optional<YardJson> yard;
  /** tile keys ("ix_iz") that exist on disk */
  std::vector<std::string> tiles;
};

// ---------------------------------------------------------------------------
// A map in pieces: the index (map.json), chunks of the road network (w/<cx>_<cz>.json.gz)
// ---------------------------------------------------------------------------

/**
 * One chunk of the road network: the junctions that lie in a square of the
 * chunk grid, and every road that begins or ends at one of them.  Nodes,
 * edges and signal controllers keep their numbers of the whole map; a road
 * between two chunks is in both.
 */
struct WorldChunkJson {
  int v = 1;
  std::vector<std::pair<int, NodeJson>> nodes;
  std::vector<std::pair<int, EdgeJson>> edges;
  std::vector<std::pair<int, SignalControllerJson>> signals;
};

/** What the game loads first of a map in pieces. */
struct MapIndexJson {
  int version = 2;
  WorldMetaJson meta;
  std::vector<PlaceJson> places;
  std::vector<SpawnJson> spawns;
  std::optional<YardJson> yard;
  /** which tiles (500 m) and which chunks of the road network (4 km) exist */
  GridBits tiles;
  GridBits chunks;
  /** numbers of nodes and edges in the whole map */
  int nodes = 0;
  int edges = 0;
};

}  // namespace fs

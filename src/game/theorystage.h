// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/theorystage.ts @ v1.7.7
/**
 * The places where the theory lessons happen (game/theory.ts): a crossroads,
 * a roundabout, a stretch of straight road – laid out on a plane of their
 * own and drawn on the practice yard by render/theory.ts.
 *
 * Everything here is in the stage's own metres: x to the east of its middle,
 * z to the south (as on the map), and headings are counted like a vehicle's
 * yaw on the map (0 = facing north, positive = turned to the left).  The
 * learner – "you" – always comes from the south and drives north, so that a
 * scene reads like the drawing of a theory book; the yard turns the whole
 * stage so that this long way lies along its long side.
 *
 * A stage is plain data: the areas of asphalt, pavement and paint as
 * outlines, the kerbs, the signs, the traffic lights, and the ways the
 * vehicles and people take – paths with named places on them (where the
 * line is that one waits at, where the junction begins, …).  Nothing here
 * draws, so that the lessons can be checked without a browser.
 */
#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fs::theory {

/** A way through the stage: points about half a metre apart, and places on it by name. */
struct Path {
  /** x, z, x, z, … */
  std::vector<double> pts;
  /** the distance from the start to each point */
  std::vector<double> acc;
  double len = 0;
  /** places along it (metres from its start): 'line' where a vehicle's nose stops to wait, 'in' / 'out' where it enters and leaves the junction, … */
  std::map<std::string, double> marks;
};

/** A point of a path and the direction of travel there (a unit vector). */
struct PathPoint {
  double x = 0;
  double z = 0;
  double dx = 0;
  double dz = -1;
};

inline PathPoint newPathPoint() { return PathPoint{}; }

/** The point `s` metres along a path – before its start and past its end it goes on in a straight line. */
PathPoint& pathPoint(const Path& p, double s, PathPoint& out);

/** The heading (yaw) of a direction of travel. */
double yawOf(double dx, double dz);

/** Drawing a path: straight lines, bends and arcs, one after the other. */
class Trace {
 public:
  std::vector<double> pts;
  /** how long it is so far */
  double len = 0;

  /** a straight line from the last point (the first call only sets the start) */
  Trace& to(double x, double z);
  /** a cubic curve from the last point, with two control points */
  Trace& curve(double c1x, double c1z, double c2x, double c2z, double x, double z);
  /** an arc round (cx, cz) of radius r from the angle a0 to a1 (angles as atan2(z, x)) */
  Trace& arc(double cx, double cz, double r, double a0, double a1);
  /**
   * On from the last point, heading (hx, hz), to (x, z) arriving heading
   * (ex, ez): straight when the two are in line, else a bend close to an arc.
   */
  Trace& join(double hx, double hz, double x, double z, double ex, double ez);
  Path path(const std::map<std::string, double>& marks = {}) const;

 private:
  std::vector<double> lens;
  void push(double x, double z);
};

/** A path through the given points (x, z, x, z, …), straight from one to the next. */
Path polyPath(const std::vector<double>& points, const std::map<std::string, double>& marks = {});

// ------------------------------------------------------------------ what is drawn
/** asphalt, pavement, the raised middle of a roundabout, its paved apron, paint, a red cycle crossing, steel of rails ('asphalt' | 'walk' | 'island' | 'apron' | 'paint' | 'red' | 'steel') */
using SurfaceKind = std::string;

/** An area: its outline (x, z, …) and holes in it. */
struct Surface {
  SurfaceKind kind;
  std::vector<double> ring;
  std::vector<std::vector<double>> holes{};
};

/** How high each kind of surface lies above the ground of the map (m). */
double LIFT(const SurfaceKind& kind);

/** A sign post: where it stands, which way its plates face (yaw), its plates from the top down. */
struct SignPlace {
  double x = 0;
  double z = 0;
  double yaw = 0;
  std::vector<std::string> codes;
};

/** A traffic light: for vehicles (red, amber, green – with the green-arrow sign beside it) or for people on foot (red, green). */
struct LightPlace {
  std::string id;
  double x = 0;
  double z = 0;
  double yaw = 0;
  bool ped = false;
  /** the sign with the green arrow (Zeichen 720) beside the red lamp */
  bool arrow = false;
};

/**
 * A thing of a place: a tree, a guide post (Leitpfosten), a warning
 * triangle, a barrier across a lane, a traffic cone, a heap of earth – at
 * (x, z), turned by `yaw`, `size` times its usual size – or a tunnel over
 * the road from (x, z) to (x1, z1): its walls at those x, its ends at those
 * z, its roof `h` high.  `from` / `to`: only between these times of the script.
 */
struct PropPlace {
  /** 'tree' | 'post' | 'triangle' | 'barrier' | 'cone' | 'heap' | 'tunnel' */
  std::string kind;
  double x = 0;
  double z = 0;
  std::optional<double> yaw{};
  std::optional<double> size{};
  std::optional<double> x1{};
  std::optional<double> z1{};
  std::optional<double> h{};
  std::optional<double> from{};
  std::optional<double> to{};
};

struct Kerb {
  std::vector<double> line;
  SurfaceKind lo;
  SurfaceKind hi;
};

struct Stage {
  std::vector<Surface> surfaces;
  /** kerbs: lines (x, z, …) along which the ground steps up from one surface to another */
  std::vector<Kerb> kerbs;
  /** crash barriers (lines) */
  std::vector<std::vector<double>> guards;
  std::vector<SignPlace> signs;
  std::vector<LightPlace> lights;
  std::map<std::string, Path> paths;
  /** a box round everything (min x, min z, max x, max z) */
  std::array<double, 4> bounds{0, 0, 0, 0};
  /** things beside and on the road (render/theoryprops.ts): trees, posts, a warning triangle, barriers, a tunnel */
  std::vector<PropPlace> props;
};

Stage emptyStage();

/** The box round everything of a stage. */
Stage& finish(Stage& st);
Stage finish(Stage&& st);

/** Is the point inside the outline (x, z, …)? */
bool inRing(const std::vector<double>& ring, double x, double z);

/** Does this surface cover the point (inside its outline, not in a hole)? */
bool covers(const Surface& s, double x, double z);

/** How high the ground of the stage is at a point, above the map's (the highest surface there: a pavement, else the road). */
double liftAt(const Stage& st, double x, double z);

// ---------------------------------------------------------------------- paint
/** A painted bar from (x0, z0) to (x1, z1), `w` wide. */
Surface bar(double x0, double z0, double x1, double z1, double w, const SurfaceKind& kind = "paint");

/** A dashed line: dashes of `dash` metres with gaps of `gap` from (x0, z0) to (x1, z1). */
std::vector<Surface> dashes(double x0, double z0, double x1, double z1, double w, double dash, double gap);

/**
 * An arrow painted in a lane (Zeichen 297), its tip at (x, z), pointing
 * along the heading `yaw`: straight on, or bending to the left or right ('straight' | 'left' | 'right').
 */
Surface laneArrow(double x, double z, double yaw, const std::string& kind);

// ------------------------------------------------------------- a crossroads
/** 'N' | 'E' | 'S' | 'W' */
using Dir = char;

/** One road through a crossroads: its lanes, what is in its middle, beside it. */
struct Road {
  /** lanes in each direction */
  int lanes = 1;
  double laneW = 0;
  /** a strip in the middle (for tram tracks), its width */
  double median = 0;
  /** a cycle lane on the right of each direction, its width */
  double bike = 0;
  /** the pavement beside it (0: none) */
  double walk = 0;
  /** 'dash' | 'solid' | 'none' ("": not given) */
  std::string centre{};
  /** tram tracks in the middle strip, one each way */
  bool tracks = false;
};

/** A crossing for people: its middle this far from the edge of the junction, its width, zebra or a crossing with lights. */
struct ArmXing {
  double at = 0;
  double w = 0;
  /** 'zebra' | 'furt' */
  std::string kind;
};

struct ArmSign {
  double at = 0;
  std::vector<std::string> codes;
};

struct ArmLight {
  std::string id;
  bool arrow = false;
};

/** What one arm of a crossroads has. */
struct ArmOpts {
  /** from the middle of the crossroads to its end (m) */
  double len = 0;
  /** a stop line or a give-way line where its traffic waits ('stop' | 'give'; "": none) */
  std::string line{};
  /** a crossing for people */
  std::optional<ArmXing> xing{};
  /** signs on the right of the traffic coming in, this far from the middle */
  std::vector<ArmSign> signs{};
  /** a traffic light for the traffic coming in (with the green-arrow sign) */
  std::optional<ArmLight> light{};
  /** lights for the people on its crossing ("": none) */
  std::string pedLight{};
};

struct CrossArms {
  ArmOpts N, E, S, W;
  const ArmOpts& operator[](Dir d) const { return d == 'N' ? N : d == 'E' ? E : d == 'S' ? S : W; }
};

struct CrossOpts {
  /** the road from north to south, and the one from east to west */
  Road ns;
  Road ew;
  /** the radius of the kerbs at the corners */
  double r = 0;
  CrossArms arms;
};

/** A crossroads with what was asked of it, and the ways through it. */
class Cross {
 public:
  Stage stage;

  explicit Cross(const CrossOpts& o);

  /**
   * The way from one arm to another (a car's lane, the cycle lane, the tram
   * track – 'car' | 'bike' | 'tram'), with its marks: 'line' (where the nose
   * waits), 'in' and 'out' (where the bend through the junction begins and
   * ends), 'xing' (where the nose waits for people crossing the arm it goes
   * into), 'end'.
   */
  Path move(Dir from, Dir to, const std::string& lane = "car") const;
  /** half the width of the road on that arm (to the kerb) */
  double half(Dir d) const;
  /** the middle of the crossing of an arm, from the middle of the crossroads */
  double xingAt(Dir d) const;
  /** the place of a point on an arm: `along` from the middle, `side` to the right of the traffic coming in */
  std::array<double, 2> at(Dir d, double along, double side) const;

 private:
  CrossOpts o;
  const Road& road(Dir d) const;
  double edge(Dir d) const;
  double lineAt(Dir d) const;
};

Cross crossroads(const CrossOpts& o);

// -------------------------------------------------------------- a roundabout
struct DirLens {
  double N = 0, E = 0, S = 0, W = 0;
  double operator[](Dir d) const { return d == 'N' ? N : d == 'E' ? E : d == 'S' ? S : W; }
};

struct RoundOpts {
  /** the raised middle, the paved ring round it (its outer radius), the outer edge of the carriageway */
  double island = 0;
  double apron = 0;
  double outer = 0;
  /** the roads coming in: their lanes (one each way) and how long each arm is (0: none) */
  double laneW = 0;
  DirLens len;
  /** the radius of the kerbs where an arm meets the ring */
  double r = 0;
};

class Round {
 public:
  Stage stage;

  explicit Round(const RoundOpts& o);

  /** in on one arm, round, out on another; marks: 'line', 'ring' (on the ring), 'exit' (leaving it), 'pass:<D>' (passing that arm), 'end' */
  Path move(Dir from, Dir to) const;

 private:
  RoundOpts o;
  double t = 0;
};

Round roundabout(const RoundOpts& o);

// ----------------------------------------------------------- a straight road
struct Taper {
  double z0 = 0;
  double z1 = 0;
  double to = 0;
};

/**
 * A line along a straight road (it runs north–south): at `x`, or moving to
 * `to` between z0 and z1 (driving north: z0 > z1) – where a lane ends.
 */
struct Bound {
  double x = 0;
  std::optional<Taper> taper{};
};

/** The x of a line along the road at z. */
double boundAt(const Bound& b, double z);

struct StraightOpts {
  /** from the south end to the north end (z0 > z1) */
  double z0 = 0;
  double z1 = 0;
  /** the lines along it, from west to east */
  std::vector<Bound> bounds;
  /** what lies between line i and line i + 1 (a SurfaceKind or 'grass') */
  std::vector<std::string> strips;
  /** kerbs along these lines */
  std::vector<int> kerbs{};
  /** crash barriers along these x */
  std::vector<double> guards{};
};

/** The road's areas and kerbs. */
Stage straight(const StraightOpts& o);

/** A painted line along the road at the line `b` from z0 to z1: dashed (the length of dash and gap), or solid. */
std::vector<Surface> along(const Bound& b, double z0, double z1, double w, std::optional<std::array<double, 2>> dash = std::nullopt);

/** A straight way along the road at x from z0 to z1 (either way). */
Path lanePath(double x, double z0, double z1, const std::map<std::string, double>& marks = {});

/** Every direction the stage's arms point in (for the tests). */
extern const std::vector<Dir> ARMS;

/** (helper: append the surfaces of `more` to `to`, as `push(...more)`) */
inline void pushAll(std::vector<Surface>& to, const std::vector<Surface>& more) { to.insert(to.end(), more.begin(), more.end()); }

}  // namespace fs::theory

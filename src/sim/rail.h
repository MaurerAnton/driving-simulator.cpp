// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Trams and trains: vehicles that follow the tracks of the map (see
 * tools/mapgen/rails.ts), wherever the player is near tracks.
 *
 * They run in the direction the track was drawn, take the straight way at
 * switches most of the time, slow down for curves, call at the stops, and –
 * being unable to swerve – brake for whatever stands on their track.  At
 * the end of a line they wait and go back.  For the cars of the traffic and
 * for the player they are obstacles like any other vehicle.
 *
 * Origin: src/sim/rail.ts @ v1.7.7
 */
#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "world/types.h"

namespace fs {

/** A piece of track, ready for running on. */
struct RailPath {
  int kind = 0;
  /** x, y, z of the points along the middle of the track (top of rail) */
  std::vector<double> pts;
  /** metres from the start at each point */
  std::vector<double> cum;
  double len = 0;
  /** pieces joining at the start / at the end: index * 2 + (0: their start is here, 1: their end) */
  std::vector<int> a;
  std::vector<int> b;
  /** stops: metres from the start */
  std::vector<double> stops;
  /** their names ('' where the map has none) and the side of their platforms (1 right, -1 left of the way the piece runs, 0 not known) */
  std::vector<std::string> stopNames;
  std::vector<int> stopSides;
  /** an overhead wire along it: 1 yes, 0 no, -1 the map does not say */
  int wire = -1;
  /** the speed its curves allow at each point (m/s) */
  std::vector<float> cap;
  /**
   * Which way it is run: 1 along the way it was drawn, -1 against it, 0
   * both ways (a single track).  On a double track everybody keeps right,
   * so the other track lies to the left of the direction of travel.
   */
  int way = 0;   // 1 | -1 | 0
};

/** What a rail vehicle is made of. */
struct Consist {
  int cars = 0;
  double carLen = 0;
  /** the joint between two cars */
  double gap = 0;
  double width = 0;
  double height = 0;
  double vmax = 0;
  double accel = 0;
  double brake = 0;
  /** the kinds of track it may run on */
  std::vector<int> tracks;
  /** units coupled together, each of `cars` sections with a driver's cab at both ends (double traction: 2; 1 if not said) */
  std::optional<int> units;
  /** a trailer car behind the unit(s): one body this long, without cab or motors (none if not said) */
  std::optional<double> trailer;
  /** the space at a coupling: between two units, before the trailer */
  std::optional<double> couple;
};

/** What one body of a consist is: where it begins behind the front end, how long it is, and what it has. */
struct CarRole {
  /** the unit it belongs to (the trailer: after the last unit) */
  int unit = 0;
  double start = 0;
  double len = 0;
  /** a driver's cab at its front end (looking ahead) or its back end (looking back) */
  bool front = false;
  bool back = false;
  /** the end of the whole consist: its lamps are lit (white ahead, red behind) */
  bool lampsFront = false;
  bool lampsBack = false;
  /** a trailer car (no cab, no motors) */
  bool trailer = false;
  /** the section with the pantograph on its roof */
  bool panto = false;
  /** bellows behind it to the next section of the same unit */
  bool bellows = false;
};

/** The bodies of a consist from the front to the back. */
const std::vector<CarRole>& carLayout(const Consist& c);

/** From the front end to the back end (m). */
double consistLength(const Consist& c);

/** A consist of its kind as a single unit, two coupled units, or a unit with a trailer car ('single' | 'double' | 'trailer'). */
using TramSet = std::string;
const Consist* consistOf(int kind, const TramSet& set = "single");

/** How much heavier a consist is than its single unit, and how much of its weight is driven (for the pull of the player's tram). */
struct ConsistMass {
  double mass = 0;
  double driven = 0;
};
ConsistMass consistMass(const Consist& c);

/** the three vehicles of the game, by RAIL kind */
const std::map<int, Consist>& CONSISTS();

/** One car body: middle of its floor line at rail level, heading (0 = north, positive = left) and slope. */
struct RailCar {
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  double pitch = 0;
};

/** A piece of track and the direction it is run in. */
struct PathDir {
  int path = 0;
  int dir = 1;   // 1 | -1
  bool operator==(const PathDir& o) const { return path == o.path && dir == o.dir; }
};

/** The way a vehicle has come: positions of its front end and where on the network they were. */
struct Trail {
  int n = 0;
  std::vector<double> x;
  std::vector<double> y;
  std::vector<double> z;
  std::vector<int32_t> path;
  std::vector<float> s;
  std::vector<int8_t> dir;
  /** index of the newest entry, and how many are valid */
  int head = 0;
  int count = 0;
  /** metres run since the newest entry */
  double since = 0;

  explicit Trail(int n = 1);
  /** k entries back from the newest */
  int at(int k) const { return (head - k + n * 4) % n; }
  void push(double x, double y, double z, int path, double s, int dir);
};

/** the line a vehicle runs (its number in RailTraffic.lines) and how far along it it is (the place in its pieces) */
struct RailLineAt {
  int index = 0;
  int k = 0;
};

struct RailVehicle {
  int id = 0;
  int kind = 0;
  const Consist* consist = nullptr;
  /** where its front end is: piece of track, metres from the piece's start, direction of travel along it */
  int path = 0;
  double s = 0;
  int dir = 1;   // 1 | -1
  double speed = 0;
  /** seconds left standing (at a stop, at the end of the line) */
  double dwell = 0;
  /** metres to run before the next stop counts (so that two marks of one stop are served once) */
  double noStop = 0;
  /** pieces chosen for the way ahead, nearest first */
  std::vector<PathDir> plan;
  std::vector<RailCar> cars;
  /** where the front end has been, newest first, a point every TRAIL_STEP metres */
  Trail trail;
  /** something is on the track ahead (for a bell, for tests) */
  bool blocked = false;
  /** driven by the player (sim/tramdrive.ts): not by the logic here, and it stays when the others go */
  bool player = false;
  /** the line it runs (its number in RailTraffic.lines) and how far along it it is (the place in its pieces) */
  std::optional<RailLineAt> line;
  /** its pantographs: 0 down … 1 up against the wire (up if not said) */
  double panto = 1;
  /** the trip of the timetable it runs (the schedule's own name for it), if it runs one ('' : none) */
  std::string trip;
  /** its passenger doors: 0 shut … 1 open (shut if not said) – they take DOOR_TIME to open or close */
  double doors = 0;
  /** its trip has ended (at the last stop of its timetable): it goes as soon as the player is not close by */
  bool retire = false;
  /** the speed it keeps to so as to be at its next stop on time (m/s, by the timetable – Infinity: none), and when that is worked out again (s) */
  std::optional<double> pace;
  double paceT = 0;
  /** its way over the tracks to the next stop of its trip ("piece:mark" of that stop), the pieces in order – null: none worked out */
  std::optional<std::string> routeFor;
  std::optional<std::vector<PathDir>> route;
};

/** how long the doors of a tram take to open or to close (s) */
inline constexpr double DOOR_TIME = 1.6;

/** A trip under way, as the timetable has it (RailSchedule.due). */
struct RailDue {
  std::string trip;
  int line = 0;
  int path = 0;
  double s = 0;
  int dir = 1;
  int k = 0;
  double speed = 0;
  std::optional<double> dwell;
};

/** A stop of a piece of track: the piece and the mark on it. */
struct PathMark {
  int path = 0;
  double mark = 0;
};

/**
 * The timetable, as the traffic of the trams asks it (the game answers from
 * sim/timetable.ts): which trips are under way round a place – and where –,
 * and how long a tram still has to stand at a stop to leave on time.
 * (The members the original marks optional say with has…() whether they are there.)
 */
class RailSchedule {
 public:
  virtual ~RailSchedule() = default;
  /** the trips under way within `r1` of (x, z) – those further than `r0` only, unless `all` –: where each is, and how fast it runs */
  virtual std::vector<RailDue> due(double x, double z, double r0, double r1, bool all) = 0;
  /**
   * how much longer the vehicle should stand at the stop at `mark` of piece
   * `path` to leave on time (s; 0 or less: it may go – negative: it is that
   * late already, and stands no longer than it must)
   */
  virtual double wait(const RailVehicle& v, int path, double mark) = 0;
  /** does its trip call at the stop at `mark` of piece `path`?  (Those it passes it does not stop at.) */
  virtual bool hasCalls() const { return false; }
  virtual bool calls(const RailVehicle&, int, double) { return true; }
  /** the speed to run at to arrive at the stop at `mark` of `path`, `dist` metres ahead, in time to leave from it on time (m/s; Infinity: as fast as it may) */
  virtual bool hasPace() const { return false; }
  virtual double pace(const RailVehicle&, int, double, double) { return std::numeric_limits<double>::infinity(); }
  /** has its trip ended (at the stop it stands at now)? */
  virtual bool hasEnds() const { return false; }
  virtual bool ends(const RailVehicle&) { return false; }
  /** where the next stop of its trip is (piece, mark) – null: none, or not known */
  virtual bool hasToward() const { return false; }
  virtual std::optional<PathMark> toward(const RailVehicle&) { return std::nullopt; }
};

/** the least a tram of the timetable stands at a stop (s): the doors open and close */
inline constexpr double TIMED_DWELL = 8;

/** A line of the trams (lines.json.gz): the pieces it runs, which way, and its stops. */
struct RailLine {
  std::string ref;
  std::string from;
  std::string to;
  std::string colour;
  int kind = 0;
  std::vector<PathDir> seq;
};

/** Something a rail vehicle must not run into: a circle on the ground. */
struct RailObstacle {
  double x = 0;
  double z = 0;
  double r = 0;
};

/** What the rail traffic needs to know about the player's car. */
struct RailPlayer {
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  double length = 0;
  double width = 0;
};

/** A point of a piece of track (RailNetwork.sample). */
struct RailSample {
  double x = 0;
  double y = 0;
  double z = 0;
  double tx = 0;
  double tz = 0;
  double cap = 0;
};

class RailNetwork {
 public:
  explicit RailNetwork(const std::vector<RailJson>& rails);

  std::vector<RailPath> paths;

  /** Places on tracks in the 200 m cell around a point: pairs of (piece, metres). */
  const std::vector<double>& near(double x, double z) const;

  /** Position (and direction of the track) at `s` metres along a piece. */
  void sample(int path, double s, RailSample& out) const;

  /**
   * The ways on from the end of a piece that is being left in direction
   * `dir`: pieces of a kind the vehicle may use that continue without a
   * sharp bend, the straightest first.
   */
  struct Way {
    int path = 0;
    int dir = 1;
    double straight = 0;
  };
  std::vector<Way> waysOn(int path, int dir, const std::vector<int>& tracks) const;

 private:
  /** points to start from, bucketed by 200 m cells: pairs of (piece, metres) */
  std::unordered_map<int64_t, std::vector<double>> cells_;
  static int64_t cell(double x, double z);
};

/** A place on a line (RailTraffic.lineSpot). */
struct LineSpot {
  int path = 0;
  double s = 0;
  int dir = 1;
  int k = 0;
};

class RailTraffic {
 public:
  explicit RailTraffic(const std::vector<RailJson>& rails, std::function<double()> random = nullptr);

  RailNetwork net;
  /** (held by unique_ptr: the vehicles have identity – the player's tram, the one hidden, the one ridden) */
  std::vector<std::unique_ptr<RailVehicle>> vehicles;
  /** how many vehicles may be about at once */
  int target = 4;
  /** the share of the trams of the traffic that come as two coupled units */
  double doubleShare = 0.2;
  /** new vehicles appear this far from the player (out of the way, but soon in sight), and go when further than `far` */
  double ringMin = 260;
  double ringMax = 620;
  double far = 900;
  /** how far ahead of a vehicle a level crossing is that is not secured yet – it brakes before it (src/sim/levelcrossing.ts; Infinity: none) */
  std::function<double(RailVehicle& v)> hold = nullptr;

  void clear();

  /**
   * The world's clock jumped (another moment, or back to the real time):
   * the trams of the traffic go, and come again at once where the timetable
   * has them now (the player's own stays).
   */
  void resync();

  /** the timetable the trams of the traffic keep to (null: they come at random, every line as often) */
  RailSchedule* schedule = nullptr;

  /**
   * Where along line `line` a tram is that has left the stop `a` (piece,
   * mark) for the stop `b` and done the share `frac` of the way: the piece,
   * the metres on it, the direction, and its place in the line's pieces.
   * Null if the line does not lead from a to b.
   */
  std::optional<LineSpot> lineSpot(int line, const PathMark& a, const PathMark& b, double frac) const;

  /** the lines of the trams (empty: they run where the tracks lead them) */
  std::vector<RailLine> lines;

  void setLines(std::vector<RailLine> lines);

  /** Whether a line runs along the piece `path` in the direction `dir`. */
  bool hasLine(int path, int dir) const;

  /**
   * A line for a vehicle that runs a piece of it: one of the lines of its
   * kind through where it stands, by chance – or `prefer` (a line's number)
   * where that one runs here.  Null if none does.
   */
  std::optional<RailLineAt> assignLine(RailVehicle& v, const std::string& prefer = "");

  /**
   * The way over the tracks from the piece `path` (run in direction `dir`)
   * to the piece `to` – the shortest, at most `reach` metres, without sharp
   * bends – as the pieces in order, both ends included.  Null if there is none.
   */
  std::optional<std::vector<PathDir>> wayTo(int path, int dir, int to, const std::vector<int>& tracks, double reach = 9000) const;

  /** The piece after (path, dir) on the way a tram of the timetable has worked out to its next stop – null where it has none, or (path, dir) is not on it. */
  std::optional<PathDir> routeNext(const RailVehicle& v, int path, int dir) const;

  /** The piece that comes after (path, dir) on the vehicle's line – null where it has no line, or the line ends. */
  std::optional<PathDir> lineNext(const RailVehicle& v, int path, int dir) const;

  /**
   * Put a vehicle on the track at a place (its front end), running in
   * direction `dir` of that piece.  Null if its length does not fit behind it.
   */
  RailVehicle* spawnAt(int path, double s, int dir, double speed = 0, const TramSet& set = "single");

  /** The cars of all vehicles as circles (three along each car), for whoever must keep clear of them. */
  void circles(const RailVehicle* skip, std::vector<RailObstacle>& out) const;

  /**
   * One step.  `others`: what must not be run into besides the player – the
   * cars of the road traffic as circles.
   */
  void update(double dt, const RailPlayer& player, const std::vector<RailObstacle>& others = {});

  /**
   * Whether there is an overhead wire along a piece: where the map says so,
   * and on the tracks of the trams also where it does not say (they all have
   * one); railway lines only where the map says.
   */
  bool wired(int path) const;

  /**
   * The next stop a vehicle will call at: where (piece of track, mark) and how
   * far ahead of its front – the one it stands at does not count.  Null if
   * there is none within `reach` metres of its way.
   */
  struct NextStop {
    int path = 0;
    double mark = 0;
    double dist = 0;
  };
  std::optional<NextStop> nextStop(const RailVehicle& v, double reach = 4000, const std::function<bool(int path, double mark)>& accept = nullptr) const;

  /**
   * Move the front end of a vehicle `move` metres on along its way (the
   * pieces in its plan), leaving the trail its cars follow.  False if the
   * way ended before (it then stands at the end).
   */
  bool advance(RailVehicle& v, double move);

  /** Put the cars on the trail behind the front end. */
  void place(RailVehicle& v) const;

  /**
   * Does the player's car (a rectangle on the ground) touch a rail vehicle?
   * Returns how to push it clear (unit vector away from the vehicle, depth)
   * and how fast that vehicle moves, or null.
   */
  struct Touch {
    double nx = 0;
    double nz = 0;
    double depth = 0;
    double vx = 0;
    double vz = 0;
  };
  std::optional<Touch> touching(const RailPlayer& player) const;

  /** the vehicle of this id (null: none) */
  RailVehicle* byId(int id) const;
  /** take a vehicle out (and give it back to the caller; null when it was not there) */
  std::unique_ptr<RailVehicle> remove(const RailVehicle* v);

 private:
  int nextId_ = 1;
  double spawnT_ = 0;
  std::function<double()> random_;
  std::vector<RailObstacle> obstacles_;
  /** nothing on the tracks yet (a new drive, a jump): the trips are put where they are, also close to the player */
  bool fresh_ = true;
  /** for each piece and direction ("path:dir"): the lines that run it, and where on them */
  std::unordered_map<std::string, std::vector<std::pair<int, int>>> onLine_;

  void steer(RailVehicle& v, RailSchedule& sch);
  void followLine(RailVehicle& v);
  RailVehicle* spawnNear(const RailPlayer& player);
  void step(RailVehicle& v, double dt, const std::vector<RailObstacle>& obstacles);
  void reverse(RailVehicle& v);
};

/**
 * Two rectangles on the ground (centre, heading, half length, half width):
 * if they overlap, the shortest way to push the first one clear of the
 * second – a unit vector and a depth.  (Separating-axis test.)
 */
bool boxOverlap(double ax, double az, double ayaw, double ahl, double ahw, double bx, double bz, double byaw, double bhl, double bhw, double& nx, double& nz, double& depth);

}  // namespace fs

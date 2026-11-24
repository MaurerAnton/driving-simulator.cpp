// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/pedestrians.ts @ v1.7.7
/**
 * The people on foot in the town: they walk along the pavements with
 * somewhere to go – a shop, home, a stop of the trams and buses – cross the
 * streets the way people do, and keep out of the way of the traffic.
 *
 *  - Where: along the pavements of the streets (Edge.swL, swR), round the
 *    corners of the junctions along their kerbs, on the right-hand half of
 *    the pavement, each at a pace of their own – the old more slowly,
 *    children quicker and less steadily, some in a hurry –, now and then two
 *    or three together.  They step aside for each other, for cyclists, posts
 *    and walls.
 *  - Crossing: at a pedestrian light they press the button and wait for the
 *    green, and hurry when it turns red while they are on the road; at a
 *    zebra crossing they wait at the kerb, look left and right and step out
 *    once the vehicles stop; anywhere else – over a side street at a
 *    junction, across to where they are going – only in a gap of the
 *    traffic, after looking.  A car that turns in is let by at the kerb.  A
 *    few do not wait for the green when the road is empty, or cross on a
 *    slant.
 *  - Vehicles: they step back when one comes too close or too fast, and
 *    start at a horn; on the road they hurry, or go back to the kerb.
 *  - Pauses: at a shop window, for a chat, at a stop (where the people of the
 *    stop take them over: render/transit – and whoever gets off there
 *    becomes one of the town's people), and at the end of their way – into
 *    a house.  Fewer of them at night (the game's clock); in the rain (the
 *    game's weather) they hurry, under umbrellas.
 *  - Some have errands just across a street with a zebra crossing or a light
 *    near them: a learner driver meets people at the crossings, as before.
 *
 * The traffic (sim/traffic) sees them as it saw the people at the
 * crossings before: whoever waits at a crossing or walks over it (`state`,
 * `crossing`, `waited`), and whoever is on the carriageway as something in
 * the way.  So the cars stop for them at zebra crossings and when they turn,
 * and the instructor (sim/rules) judges the player by them.
 *
 * Cheap enough for a phone: a person walks along a few straight pieces of
 * way worked out ahead – a few multiplications a frame; what is around them
 * is looked at four times a second, staggered, through a grid of the people.
 *
 * (This header is included by sim/hooks.h, where the stand-in of the crowd stood.)
 */
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/geom.h"
#include "sim/insertion_map.h"
#include "sim/playerstate.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

struct TrafficCar;
struct TrafficEvent;
struct Cyclist;
class SignalSystem;
class Stops;

/** One of the people on foot, as the traffic, the instructor, the replay and the picture see them (interface Walker). */
struct Walker {
  virtual ~Walker() = default;
  int id = 0;
  /** what they look like: the look number of render/people (a replay records it as the "colour") */
  int colour = 0;
  double x = 0;
  double y = 0;
  double z = 0;
  /** the way they face (0 = north, + = to the left) */
  double yaw = 0;
  /** metres walked (the gait follows it) – −1 while they stand */
  double phase = 0;
  /** what the traffic sees of somebody (WalkerState): 'walk' | 'wait' | 'cross' | 'gone' */
  std::string state = "walk";
  /** the crossing they wait at or walk over – null: none (also where they cross without one) */
  const CrossingJson* crossing = nullptr;
  /** how long they have been waiting at its kerb (s) */
  double waited = 0;
  /** how fast they walk now (m/s) */
  double speed = 0;
  /** for the picture: the head turned (rad, + to the left), a start (0 … 1), leaning into the walk (rad), an umbrella up */
  double head = 0;
  double startle = 0;
  double lean = 0;
  bool umbrella = false;
};

/** How old somebody is – as they look (render/people). */
namespace AGE {
inline constexpr int CHILD = 0;
inline constexpr int ADULT = 1;
inline constexpr int OLD = 2;
}  // namespace AGE

/**
 * How old somebody looks, from their look number: the kind render/people
 * (lookOf) picks for it – the older people are its fourth and fifth kinds,
 * the children its seventh and twelfth.
 */
int ageOfLook(int look);

/** A look number of somebody of that age. */
int lookOfAge(int age, const std::function<double()>& rand);

/** The part of the weather the people care for: its kind and how hard it falls. */
struct RainWeather {
  std::string kind;
  double intensity = 0;
};

/** How hard it rains for the people (0 … 1), from the weather of src/game/weather (or anything with a kind and an intensity). */
double rainOf(const std::optional<RainWeather>& w);

/**
 * How many people there are around the player at most, for the settings:
 * the traffic's density (0 … 1, as the cars) and the graphics preset – a
 * phone on the low preset draws a few dozen, a fast computer up to CROWD_MAX.
 */
int crowdTarget(double traffic, const std::string& quality);

/** How many of the people are out at an hour of the day (0 … 24), as a share of the busiest hours. */
double outAt(double hour);

/** the most people there are (the picture has room for so many) */
inline constexpr int CROWD_MAX = 180;
/** points of the way ahead a person keeps at most */
inline constexpr int PED_MAXP = 24;

/** The seconds after midnight on the clocks in Kassel at a moment (ms since 1970) – sim/timetable's berlinClock(date).s. */
double berlinSeconds(double ms);

struct Person;

/** Options for a person put somewhere on purpose (the tests, people getting off a bus). */
struct WalkerOptions {
  struct Goal {
    Edge* edge = nullptr;
    int side = 1;
    double s = 0;
    /** 'errand' | 'home' | 'stop' | 'stroll' ('': errand) */
    std::string kind;
  };
  std::optional<int> look;
  /** AGE (default: as the look says) */
  std::optional<int> age;
  /** the pace they like (m/s) */
  std::optional<double> pace;
  /** the time they want in hand when they cross in a gap (s) */
  std::optional<double> margin;
  std::optional<bool> bold;
  std::optional<bool> hurry;
  /** where they are going: a point on a pavement (default: a place of their own) */
  std::optional<Goal> goal;
  /** how readily they stop for a chat or a shop window (0 … 1; the tests set 0) */
  std::optional<double> idle;
};

/**
 * The people on foot around the player.  Owned by the traffic, which updates
 * it every step; `target` says how many there are at most (0: none).
 */
class Crowd {
 public:
  Crowd(World& world, SignalSystem& signals, double seed = 31337);
  ~Crowd();
  Crowd(const Crowd&) = delete;
  Crowd& operator=(const Crowd&) = delete;

  /** how many people there are around the player at most – by day, in the dry, in the middle of a town */
  int target = 0;
  /** the weather (src/game/weather): in the rain they hurry, under umbrellas – and fewer are out (null: the weather shown – clear until it is translated) */
  std::function<std::optional<RainWeather>()> weather = nullptr;
  /** the hour of the day (0 … 24): the game sets the game's clock as the clocks in Kassel show it, like the timetables and the sun (null: noon) */
  std::function<double()> clock = nullptr;
  /** how hard it rains now (0 … 1: rainOf the weather) */
  double rain = 0;
  /** the stops of the map: some people go to them */
  const Stops* stops = nullptr;
  /** is there something standing at (x, z) – a post, a wall – for somebody of radius r on the ground at height y?  (null: nothing is) */
  std::function<bool(double x, double y, double z, double r)> blocked = nullptr;
  /** somebody has come to a stop: the stop's people take them over (true) – or not (false: they wait by the pole themselves) */
  std::function<bool(const Walker& w)> toStop = nullptr;
  struct Spot {
    double x = 0;
    double z = 0;
  };
  /** others standing about that the people walk round: those waiting at the stops (render/transit) – null: none */
  std::function<std::vector<Spot>()> standing = nullptr;

  /** Everybody (for the traffic, the picture, the replay). */
  const std::vector<Walker*>& people();
  /** Nobody any more (a new drive, a jump elsewhere). */
  void clear();
  /** Roads taken out of the network: whoever is on them (or going there) goes too. */
  void removeRoads(const InsertionSet<Edge*>& gone);
  /** Those within r metres of (x, z) – e.g. for a replay, which keeps the people near the player (the list is reused: use it at once). */
  const std::vector<Walker*>& near(double x, double z, double r);
  /** What doing somebody is (for the tests): 'walk' | 'kerb' | 'cross' | 'pause' | 'follow'. */
  std::string modeOf(const Walker& w) const;
  /** Those on the carriageway, as boxes in the way of the cars (added to `out`). */
  void addBlocks(std::vector<PlayerState*>& out);
  /**
   * Is somebody walking over the road `lane` leads along, near where it
   * begins (from a few metres inside its junction to 20 m out) – where a
   * vehicle that turns into it has to let them cross first?
   */
  bool crossingLane(const Lane& lane);
  /** Put somebody on the pavement of `edge` on `side`, `s` metres along it, walking `dir` (+1 towards its end b). */
  Walker* add(Edge& edge, int side, double s, int dir, const WalkerOptions& opts = {});
  /**
   * Somebody who has just got off a tram or a bus at (x, y, z): from here on
   * one of the people of the town – if there is a pavement within a few
   * metres.  Returns whether they were taken.
   */
  bool adopt(double x, double y, double z, int look);
  /**
   * One step: people come and go round the player, walk on, cross, wait,
   * look round.  `cars`: the traffic's cars; `cyclists`; `obstacles`: what
   * else stands or moves on the roads (the trams, the player's trailer or
   * parked vehicle) – what happens to the people because of the player is
   * added to `events`.
   */
  void update(double dt, const PlayerState& player, const std::vector<TrafficCar*>& cars, const std::vector<Cyclist*>& cyclists,
              const std::vector<PlayerState*>& obstacles, std::vector<TrafficEvent>& events);

  /** A vehicle as the people see them (interface Veh). */
  struct Veh {
    double x = 0;
    double y = 0;
    double z = 0;
    /** its nose (unit), its speed along it (m/s, < 0 backwards), how it speeds up */
    double fx = 0;
    double fz = -1;
    double v = 0;
    double a = 0;
    double len = 4;
    double wid = 1.8;
    /** the road it turns into at the junction ahead of it or under it (null: none, or not known) */
    const Edge* into = nullptr;
  };

 private:
  struct PavementAt {
    Edge* edge = nullptr;
    int side = 1;
    double s = 0;
    double sw = 0;
    bool road = false;
  };
  struct At {
    double x = 0;
    double y = 0;
    double z = 0;
  };

  void sweep();
  double wanted();
  void spawning(double dt, const PlayerState& player);
  int spawn(const PlayerState& player, bool anywhere);
  Person* make(int look, std::optional<int> age = std::nullopt);
  void onPavement(Person& p, Edge& e, int side, double s, int dir, bool place = true);
  double keepRight(const Edge& e, int side, int dir, const Person* p = nullptr);
  const At& pointOn(const Edge& e, int side, double s, double lat);
  void put(Person& p, const Edge& e, int side, double s, double lat, bool kerb);
  void putXYZ(Person& p, double x, double y, double z);
  std::optional<PavementAt> samplePavement(double x, double z, double r);
  std::optional<PavementAt> nearestPavement(double x, double z, double r);
  void setGoal(Person& p, int kind, Edge& e, int side, double s);
  void newGoal(Person& p);
  bool crossingGoal(Person& p);
  void planAlong(Person& p, bool turned = false);
  void bounds(Person& p);
  double along(Person& p, const Edge& e, int side, double s0, double s1, double lat);
  const CrossingJson* crossingNear(const Edge& e, double s, double range);
  void decide(Person& p);
  void nextMove(Person& p);
  void corner(Person& p, Node& N, int k, int sg);
  void crossArm(Person& p, Node& N, Arm& arm, int sg);
  void wait(Person& p, Edge& e, int side, double xs, double ws, const CrossingJson* c, Node* node, Arm* arm, bool moves, int after);
  void turnRound(Person& p);
  void arrive(Person& p);
  void atGoal(Person& p);
  void atWindow(Person& p);
  double faceRoad(Person& p);
  void stand(Person& p, int why, double seconds, double face);
  void atKerb(Person& p, bool back = false);
  void registerAt(Person& p);
  void go(Person& p);
  void crossed(Person& p);
  double pace(Person& p);
  void step(Person& p, double dt);
  void spread(Person& p);
  void walk(Person& p, double dt);
  void atKerbStep(Person& p, double dt);
  void turnTo(Person& p, double yaw, double dt, double rate);
  void follow(Person& f, double dt);
  void look(Person& p);
  void avoid(Person& p);
  bool chatty(const Person& p, const Person& q);
  void pauseHere(Person& p, int why, double seconds, double face);
  bool inWay(Person& p, double d);
  bool flinch(Person& p);
  void kerbLook(Person& p);
  void crossLook(Person& p);
  bool allCanStop(Person& p);
  bool zebraClear(Person& p);
  bool noneRunning(Person& p);
  bool clearToGo(Person& p, double margin, double empty);
  bool turningIn(Person& p, double range);
  bool closeIn(Person& p, double range);
  double lineLen(const Person& p) const;
  double toLine(const Veh& V, const Person& p, double reach);
  void endanger(Person& p, const PlayerState& player, std::vector<TrafficEvent>& events);
  void honk(const PlayerState& player);
  void see(double dt, const PlayerState& player, const std::vector<TrafficCar*>& cars, const std::vector<PlayerState*>& obstacles);
  void grid();
  double rnd() { return rand_(); }

  World& world;
  SignalSystem& signals;
  /** everybody (owned: `all` points into it in the original's order) */
  std::vector<std::unique_ptr<Person>> owned_;
  std::vector<Person*> all;
  /** the same, as the traffic sees them */
  std::vector<Walker*> walkers_;
  Rng rand_;
  int nextId = 1;
  double time = 0;
  int frame = 0;
  double hour = 12;
  double hourAt = -100;
  double spawnT = 0;
  /** how often a place round the player had a pavement to put somebody on (a running share): how much of a town it is */
  double urban = 0.5;
  /** just begun (or cleared): the first people come up anywhere around the player, not only out of sight */
  bool fresh = true;
  double freshAt = 0;
  /** the vehicles around, as the people see them (made anew each step) */
  std::vector<Veh> vehs;
  int nVeh = 0;
  Veh* playerVeh = nullptr;
  double pv = 0;
  double pa = 0;
  bool hornOn = false;
  double hornAt = -100;
  /** somebody on foot who is the player, and the cyclists: the people step aside for them */
  const PlayerState* player = nullptr;
  const std::vector<Cyclist*>* cyclists = nullptr;
  const std::vector<PlayerState*>* obstacles = nullptr;
  const PlayerState* playerCar = nullptr;
  /** those on the carriageway, and their boxes for the cars */
  std::vector<Person*> onRoad;
  std::vector<std::unique_ptr<PlayerState>> boxes;
  // ---- a grid of the people (4 m cells, hashed): who is near whom – and of the others standing about
  std::vector<int32_t> cellHead;
  std::vector<int32_t> cellNext;
  std::vector<int32_t> otherHead;
  std::vector<int32_t> otherNext;
  std::vector<Spot> others;
  std::vector<Walker*> nearList;
  At at_;
  /** where on the line (m from the near kerb) the vehicle last asked about meets it */
  double lineAt = 0;
};

}  // namespace fs

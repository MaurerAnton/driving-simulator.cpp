// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/emergency.ts @ v1.7.7
/**
 * Police cars, ambulances, fire engines and the emergency doctor's car – and
 * how everybody makes way for them.
 *
 * Now and then – every few minutes in town, more rarely outside – one of them
 * is called out near the player: it turns up some way off and drives through
 * with blue lights and siren to where it is called.  It goes faster than the
 * limit, crosses red lights and the others' right of way slowly and only once
 * it sees the way is clear, and passes whoever is in front of it.  The others
 * make way (§ 38 Abs. 1 StVO): the cars ahead pull over to the right and slow
 * down or stop, those coming towards it keep right and slow down, and at the
 * junctions it crosses everybody waits before the line.  On a road with two
 * or more lanes in its direction they leave the emergency corridor
 * (Rettungsgasse, § 11 Abs. 2 StVO): the left-most lane moves to the left, all
 * the others to the right, and it drives through the gap between them.
 * Police cars also go about on patrol among the traffic, without lights.
 *
 * An emergency vehicle is an ordinary car of the traffic with `em` set.  Its
 * style (EM_STYLE …) says what it is and whether its blue lights are on, so
 * that the drawing – and the replay of a drive – need nothing else.  A car's
 * place across its lane is `TrafficCar.side`.
 *
 * The traffic (src/sim/traffic.ts) asks this module at a few points of every
 * car's step: how fast it may go (desired), where it has to stop (stopFor),
 * what is in front of an emergency vehicle (lead), and whether one may go on
 * into a junction (through).  The rule monitor asks how the player behaved
 * (judge), the game what the instructor says about it (tip).
 */
#pragma once

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/geom.h"
#include "core/js.h"
#include "sim/playerstate.h"
#include "world/lanes.h"
#include "world/world.h"

namespace fs {

class Traffic;
struct TrafficCar;
struct PlayerSnapshot;

/** The kinds of emergency vehicle (EM.POLICE …). */
inline constexpr int EM_POLICE = 0;
inline constexpr int EM_AMBULANCE = 1;
inline constexpr int EM_FIRE = 2;
inline constexpr int EM_DOCTOR = 3;

/**
 * The style numbers (TrafficCar.style) of the emergency vehicles: EM_STYLE +
 * kind · 2, and one more while the blue lights are on.  (0 … 2 are the
 * ordinary cars, 3 the city bus.)
 */
inline constexpr int EM_STYLE = 8;

/** The style of an emergency vehicle of this kind, its blue lights on or off. */
inline int emStyle(int kind, bool lit) { return EM_STYLE + kind * 2 + (lit ? 1 : 0); }

/** The kind (EM) of a car of the traffic by its style; −1 for any other car. */
inline int emKind(int style) { return style >= EM_STYLE && style < EM_STYLE + 8 ? (style - EM_STYLE) >> 1 : -1; }

/** Are the blue lights of a car of this style on? */
inline bool emLit(int style) { return emKind(style) >= 0 && ((style - EM_STYLE) & 1) == 1; }

/** Length and width of each kind (m): the patrol car (an estate), the ambulance (a van with a box body), the fire engine, the doctor's car. */
struct EmSize {
  double length;
  double width;
};
inline constexpr std::array<EmSize, 4> EM_SIZE = {EmSize{4.75, 1.84}, EmSize{6.95, 2.3}, EmSize{8.6, 2.5}, EmSize{4.85, 1.9}};

/** What makes a car of the traffic an emergency vehicle. */
struct EmergencyCar {
  /** EM */
  int kind = 0;
  /** on a call: blue lights and siren on (false: a police car on patrol) */
  bool call = false;
  /** the connectors of its way after the one it takes next, in order (Traffic.nextFor hands them out) */
  std::vector<Connector*> route;
  /** the last connector of its way: a little beyond it, it has arrived */
  Connector* last = nullptr;
  /** seconds since the call began */
  double t = 0;
  /** the pitch of its horn (about 1) and where it is in the rhythm of its lights and horn (s) */
  double pitch = 1;
  double phase = 0;
  /**
   * Worked out at the start of every step: the nearest vehicle ahead it
   * cannot pass – the gap to it (m, Infinity: none) and its speed along the
   * way –, how fast it may squeeze past the others (m/s), where across its
   * lane it wants to be (m, + left), and how fast it wanted to go last.
   */
  double gap = JS_INF;
  double gapV = 0;
  double passV = JS_INF;
  double target = 0;
  double want = 0;
  /** what the instructor has said about it, and which of the player's faults it has seen (bits) */
  int told = 0;
  int faults = 0;
  /** the car it has been held up behind (its id, −1: none) and for how long (s) */
  int64_t stuckOn = -1;
  double stuckT = 0;
};

/** A fault of the player's for the rule monitor. */
struct EmergencyFault {
  std::string id;
  /** 'hint' | 'minor' | 'major' | 'fail' */
  std::string severity;
};

class Emergencies {
 public:
  Traffic& traffic;
  /** calls come by themselves now and then (false: only those sent with dispatch) */
  bool autoCalls = true;
  /** the seconds between two calls, when given (0: every few minutes, by where the player is) */
  const double every;
  /** the share of the traffic's new cars that are police cars on patrol */
  double patrolShare = 0.015;
  /** whether the player's driving is judged (not on a demonstration drive: the automatic driver does not make way) */
  bool judging = true;

  struct Lead {
    double gap = JS_INF;
    double v = 0;
  };

  /** `every`: a call every so many seconds (for trying it out: ?emergency=<seconds>), 0 for every few minutes. */
  explicit Emergencies(Traffic& traffic, double every = 0);
  Emergencies(const Emergencies&) = delete;
  Emergencies& operator=(const Emergencies&) = delete;

  /** Called by the traffic at the start of its step: calls begin and end, and everybody works out how to make way. */
  void update(double dt, const PlayerState& player);

  /**
   * The speed a car wants (m/s), given what it would want otherwise (`v0`)
   * and the limit: an emergency vehicle on its call goes faster – about
   * 20 km/h over the limit in town, up to 30 outside, slower where it squeezes
   * past the others –; a car that makes way slows down.
   */
  double desired(TrafficCar& car, double v0, double limit);
  /** How far ahead of its front a car has to stop to make way (m; Infinity: it need not). */
  double stopFor(TrafficCar& car);
  /** For an emergency vehicle on its call: the nearest vehicle ahead it cannot pass – the gap (m) and its speed along the way. */
  Lead lead(TrafficCar& car);
  /**
   * An emergency vehicle on its call `distToLine` metres before a junction
   * it crosses by `next`: where the way is the others' – a red light, a sign,
   * right before left – it may go all the same (§ 35 Abs. 1, § 38 Abs. 1 StVO),
   * but with care: up to the line no faster than it could still stop there,
   * and on only when nobody is in the junction or too close to stop before it.
   * Returns the speed it may have now (m/s; Infinity: no limit), or −1: wait
   * at the line.  `mustStop`: what would hold an ordinary car there.
   */
  double through(TrafficCar& car, const Connector& next, double distToLine, bool mustStop);

  /**
   * Send an emergency vehicle of `kind` (EM) out on a call: from `lane`, its
   * middle `s` metres along it, along `route` – the connectors to take one
   * after the other, the first at the end of `lane`.  Returns its car.
   */
  TrafficCar* dispatch(int kind, Lane& lane, double s, const std::vector<Connector*>& route);

  /** What the instructor says about the emergency vehicle that has just come near: a text key, once – or '' (null). */
  std::string tip();

  /**
   * The player's faults (for the rule monitor, every step): holding up an
   * emergency vehicle for more than a few seconds without making way, no
   * corridor in slow traffic on a road with several lanes, into the junction
   * it is about to cross.  Each once for each emergency vehicle.
   */
  const std::vector<EmergencyFault>& judge(double dt, const PlayerSnapshot& p);

 private:
  /** How a car makes way in this step: at most this fast (m/s), stopping this far ahead of its front (m), this far across its lane (m, + left). */
  struct Way {
    double cap = JS_INF;
    double stop = JS_INF;
    double side = 0;
  };
  /** A piece of an emergency vehicle's way – a lane, or a path through a junction – and how far ahead of its middle the piece begins (m). */
  struct Piece {
    Lane* lane = nullptr;
    Connector* conn = nullptr;
    double at = 0;
  };
  /** the junctions on its way: how far ahead the path through each begins (m; below 0 it is in it), and in how many seconds it is there */
  struct RunNode {
    Node* node = nullptr;
    double d = 0;
    double eta = 0;
  };
  /** An emergency vehicle on its call, in this step: its way ahead and the junctions on it. */
  struct Run {
    TrafficCar* car = nullptr;
    std::vector<Piece> pieces;
    std::vector<RunNode> nodes;
  };
  /** Where something is as an emergency vehicle sees it: how far ahead of its middle (m), how far left of the middle of its way (m), and whether it goes the same way. */
  struct Seen {
    double along = 0;
    double lat = 0;
    bool same = true;
    /** on which piece of its way (0: the one it is on) */
    size_t piece = 0;
  };
  /** where a car that stops to make way comes to rest: its lane or path, and the place of its front there */
  struct Hold {
    const void* on = nullptr;
    double s = 0;
  };

  Rng rand = rng(20261116);
  double time = 0;
  /** seconds until the next call */
  double wait;
  /** the highest number of a car seen so far (those above it are new) */
  int64_t seenId = 0;
  std::vector<Run> runs;
  std::unordered_map<TrafficCar*, Way> ways;
  std::unordered_map<TrafficCar*, Hold> holds;
  const PlayerState* player = nullptr;
  Seen seen;
  Seen seen2;
  /** the player and the nearest emergency vehicle in this step (judged in judge) */
  struct PlayerJudge {
    bool blocking = false;
    bool corridor = false;
    bool junction = false;
    TrafficCar* car = nullptr;
  } pj;
  double blockT = 0;
  double corridorT = 0;
  Lane* lastLane = nullptr;
  double lastFront = 0;
  std::string pendingTip;
  bool hasTip = false;
  double tipAt = 0;
  std::vector<EmergencyFault> faults;
  Way w;
  PolySample tmp = newPolySample();

  Run runOf(TrafficCar& car);
  bool see(const Run& run, const Lane* lane, const Connector* conn, double s, double side, Seen& out);
  void clearWay(Run& run, double dt);
  const Way* makeWay(TrafficCar& car);
  const Way* wayFor(const Run& run, TrafficCar& car);
  double aside(const Lane& lane, const TrafficCar& car, bool right);
  double stopHere(TrafficCar& car);
  void slideAcross(TrafficCar& car, double dt);
  bool clear(TrafficCar& car, const Connector& next);
  void equip(TrafficCar& car, int kind, bool call, std::vector<Connector*> route);
  void patrol(TrafficCar& car);
  bool arrived(TrafficCar& car);
  void endCall(TrafficCar& car);
  double interval(const PlayerState& p);
  int pickKind();
  bool call(const PlayerState& p);
  const std::vector<Lane*>& carriageway(Lane& lane);
  const std::vector<Lane*>& opposite(Lane& lane);
  Connector* onward(const Lane& lane);
  Connector* inward(const Lane& lane);
  std::vector<Connector*>& wayOn(Lane& lane, double metres, std::vector<Connector*>& out);
  struct Found {
    Lane* lane = nullptr;
    double s = 0;
    std::vector<Connector*> path;
  };
  bool walkBack(Lane& end, double back, double d, Found& out);
  bool freeAt(const Lane& lane, double s, const PlayerState& p);
  bool fromBehind(const PlayerState& p, Lane& lane, int kind);
  bool fromSide(const PlayerState& p, Lane& lane, int kind);
  bool fromAhead(const PlayerState& p, Lane& lane, int kind);
  bool patrolCall(const PlayerState& p);
  bool somewhere(const PlayerState& p, int kind);
  double across(const PlayerState& p);
  void watchPlayer(const PlayerState& p);
  void say(EmergencyCar& em, int bit, const std::string& key);
};

}  // namespace fs

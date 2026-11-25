// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Driving a tram: the player's tram runs on the tracks of the map (sim/rail.ts)
 * like the others, but by the player's hand –
 *
 *  - one controller for power and brake (the pedals: forwards = power,
 *    backwards = brake), the emergency brake with the magnetic track brake
 *    (the handbrake), the bell (the horn);
 *  - at a switch the way is asked for with the indicator (left, right, or
 *    none: straight on), as on real trams; the switch is set some metres
 *    before it and then stays;
 *  - at a stop the doors are opened (the door key), the passengers get off
 *    and on, the doors are closed, and only then the tram moves;
 *  - the power comes from the overhead wire: the pantograph is raised before
 *    the tram can pull (it takes a few seconds), and lowered – not while the
 *    motors draw current – before the wire ends, where a regional tram runs
 *    on its engine; a second unit coupled on pulls with it, a trailer car is
 *    only weight.
 *
 * What the instructor looks at (BOStrab, StVO where the track runs in the
 * street): the speed the curves and the road allow, stopping at every stop
 * at its mark, doors only open while standing at a stop – and closed only
 * when everybody is in –, red lights where the track crosses a junction.
 *
 * Origin: src/sim/tramdrive.ts @ v1.7.7
 */
#pragma once

#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "sim/rail.h"
#include "world/world.h"

namespace fs {

class SignalSystem;

/** a low-floor tram of 30 m: how it pulls and brakes */
struct TramSpec {
  /** full power: up to this, then what the motors' power gives (m/s², and m/s at which power takes over) */
  double accel = 1.2;
  double powerFrom = 12.5;
  double vmax = 70 / 3.6;
  /** service brake at full lever, emergency brake with the track brake (m/s²) */
  double brake = 1.6;
  double emergency = 2.8;
  /** driving resistance: constant and with the square of the speed (m/s²) */
  double roll = 0.02;
  double air = 0.0005;
};
inline constexpr TramSpec TRAM{};

/** how long the pantograph takes to rise to the wire, and to come down (s) */
inline constexpr double PANTO_UP_TIME = 3;
inline constexpr double PANTO_DOWN_TIME = 1.5;

/** 'speed' | 'tramCurve' | 'redLight' | 'tramStopMissed' | 'tramStopPlace' | 'tramDoorsMoving' | 'tramDoorsEarly' | 'tramPantoLoad' | 'tramPantoWireEnd' */
using TramFault = std::string;

struct TramEvent {
  TramFault fault;
  /** 'hint' | 'minor' | 'major' | 'fail' */
  std::string severity;
  /** (the original's data: numbers by name) */
  std::map<std::string, double> data;
};

class TramDriver {
 public:
  TramDriver(RailTraffic& rail, RailVehicle& v, World& world, SignalSystem& signals, std::function<double()> random = nullptr);

  RailVehicle& v;
  RailTraffic& rail;
  /** the controller: −1 full brake … 0 coasting … 1 full power */
  double lever = 0;
  bool emergency = false;
  /** the way asked for at the next switch: 1 left, −1 right, 0 straight on (as the indicator) */
  int wish = 0;
  bool doorsOpen = false;
  /** at a stop with the doors open: passengers still to get off and on, and the seconds until the next one is through */
  int boarding = 0;
  int alighting = 0;
  /** what lies ahead (for the HUD and the instructor) */
  struct Ahead {
    /** the next stop's mark (how far, on which piece, where on it) and the speed the curves allow from here (m, m/s) */
    double stop = std::numeric_limits<double>::infinity();
    int stopPath = -1;
    double stopMark = 0;
    double curve = std::numeric_limits<double>::infinity();
    /** the next switch, and which way the tram will take there (1 left, −1 right, 0 straight) */
    double switchIn = std::numeric_limits<double>::infinity();
    int switchTurn = 0;
    /** a junction with lights the track crosses: how far, what the light for the tram shows */
    double signalIn = std::numeric_limits<double>::infinity();
    int signal = -1;
    /** where the overhead wire ends ahead (m; Infinity: not within sight, or there is none here) */
    double wireEndIn = std::numeric_limits<double>::infinity();
  } ahead;
  /** the speed limit here (km/h): the road's where the track runs in the street, else 70 */
  double limit = 70;
  /** stops served (stood at the mark with the doors open, everybody through) */
  int served = 0;
  /** metres driven */
  double odometer = 0;
  /** the pantograph: 0 down … 1 up against the wire, and which way the player has set it (up when a tram is taken over; the game begins a drive with it down) */
  double panto = 1;
  bool pantoUp = true;

  double speed() const { return v.speed; }
  /** a regional tram, with an engine for the lines without a wire */
  bool hybrid() const { return v.kind == RAIL_LIGHT; }
  /** whether there is a wire over the tram here */
  bool wired() const { return rail.wired(v.path); }
  /** whether the motors get power: from the wire with the pantograph up – or, on a regional tram where there is no wire, from its engine */
  bool powered() const { return wired() ? panto >= 0.999 : hybrid(); }
  /** Raise or lower the pantograph (its key); lowering it while the motors draw current is a fault (it draws an arc). */
  void togglePanto(std::vector<TramEvent>& events);
  /** passengers still getting off or on */
  int passengers() const { return boarding + alighting; }
  /** Is the tram standing at a stop's mark (within the allowance)? */
  bool standingAtStop() const { return atStop_.has_value(); }
  /** Standing at a stop that has not been served yet: time to open the doors. */
  bool atUnservedStop() const;
  /** Open or close the doors (the door key): only standing; closing while people still get on is a fault. */
  void toggleDoors(std::vector<TramEvent>& events);
  /**
   * Driving by itself (a demonstration drive): the controller as a tram
   * driver works it – up to the speed the line allows here, the curves ahead
   * and what braking to the next stop or to a red light leaves – and at each
   * stop the doors: open once it stands at the mark, closed once everybody
   * is through.  Call it before step().
   */
  void drive(std::vector<TramEvent>& events);
  /**
   * One step: the controller moves the tram along its way; what happened
   * that the instructor has to say is added to `events`.
   */
  void step(double dt, std::vector<TramEvent>& events);

 private:
  World& world_;
  SignalSystem& signals_;
  SurfaceHit hit_ = newSurfaceHit();
  double nextPassenger_ = 0;
  /** the stop (its piece and mark) the tram stands at, if it does */
  std::optional<PathMark> atStop_;
  bool servedHere_ = false;
  /** a stop the front has come within reach of: was it served? – and the stop last stood at with open doors */
  struct Pending {
    int path = 0;
    double mark = 0;
    double s = 0;
  };
  std::optional<Pending> pending_;
  std::optional<PathMark> lastAt_;
  /** whether the piece the front is on has a wire (to notice where it ends) */
  bool wasWired_ = true;
  double overLimit_ = 0;
  int lastSignalNode_ = -1;
  std::function<double()> random_;

  /** The stop whose mark the front stands at (within the allowance), or null. */
  std::optional<PathMark> findStop() const;
  /** Which of the ways on from the end of the piece being run to take: the one asked for, else straight on. */
  std::optional<PathDir> choose(int path, int dir) const;
  /** What lies ahead: stops, curves, switches, lights – and the rules about them. */
  void look(double dt, std::vector<TramEvent>& events);
};

/** A place to start a tram (tramStart). */
struct TramStart {
  int path = 0;
  double s = 0;
  int dir = 1;
  double x = 0;
  double z = 0;
};

/**
 * A place to start a tram: the stop mark nearest to (x, z) on a tram track,
 * with the direction the track is run in there (on a double track the
 * right-hand one).  Null when the map has no tram track.
 */
std::optional<TramStart> tramStart(const RailTraffic& rail, double x, double z, const std::vector<int>& kinds, const std::string& name = "");

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/autopilot.ts @ v1.7.7
/**
 * The autopilot of the light aeroplane (sim/flight.ts): it flies the
 * aeroplane by itself, as the demonstration drives drive the car and the
 * tram – the take-off, climbing and holding a heading and a height, flying
 * to a place and circling it, the circuit, the approach, the landing and the
 * roll-out to a stop.  And, below it, the assist (FlightAssist): help for
 * the player's own hands – a softer stick, the rudder kept coordinated, the
 * wings brought level when the stick is let go, no stall and no overspeed.
 *
 * Both only decide.  Every step they read the aeroplane's instruments and
 * fill in the controls – the FlightControls the game builds from the
 * player's stick, the flaps on the lever's detents 0°, 10°, 20°, 30° – and
 * whoever uses them steps the aeroplane.  No DOM, no randomness: the same
 * flight gives the same controls.
 *
 * The autopilot flies the way an instructor describes it, as loops inside
 * each other:
 *
 *  - Sideways: the way over the ground (the track) gives a bank angle, the
 *    bank angle the ailerons; the rudder keeps the ball in the middle.  A leg
 *    from one point to the next is flown by aiming at the line some seconds
 *    ahead, and a turn onto the next leg begins early by the turn's radius,
 *    so that it rolls out on the new line rather than beyond it.
 *  - Up and down: the elevator sets the pitch attitude.  The attitude comes
 *    either from the airspeed – climbing at full power, the nose up when too
 *    fast – or from a vertical speed: to hold a height or to follow the
 *    glide path the nose sets the path and the throttle the speed.  The trim
 *    is what level flight at the target speed needs (Aircraft.levelFlight),
 *    so the elevator works near the middle.
 *  - On the runway: the rudder steers the nose wheel along the centre line,
 *    the ailerons keep the wings level, the brakes stop it.
 *
 * Its limits: 25° of bank, −10° … +15° of pitch, the angle of attack kept
 * 3° below the stall, the airspeed never far below 1.2 times the stall speed
 * of the flaps that are out nor above 85 % of vne.  En route (height hold,
 * circling, the sightseeing route) it keeps 150 m (500 ft, the legal minimum
 * over open country) above the highest ground in the next 3 km.
 *
 * The circuit it flies (and joins to land) is a flying school's: left-hand
 * unless it comes from the right, 1000 ft above the field, the downwind leg
 * 1 km beside the runway, a 2.6 km final on a 3° glide path; flaps 10° about
 * abeam the threshold, 20° on base, 30° on the final.  The approach is
 * checked at 100 ft (lined up, on the path, at the right speed) and a landing
 * that would end beyond the runway is not tried: both become a go-around and
 * another circuit; after three attempts it circles the field.
 *
 * (Port: the modes, phases and vertical modes are the original's strings, as
 * the game compares them and builds its texts' keys from them.)
 */
#pragma once

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "sim/flight.h"
#include "sim/flightspec.h"
#include "sim/vehicle/types.h"
#include "world/types.h"

namespace fs {

/** Limits, the measures of the circuit, and the gains of the loops. */
struct AutopilotConstants {
  static constexpr double DEG_ = 3.14159265358979323846 / 180;
  static constexpr double FT_ = 0.3048;
  /** greatest bank (rad): in turns, below `low` m above the field, in the flare */
  double bankMax = 25 * DEG_;
  double bankLow = 10 * DEG_;
  double low = 30;
  double bankFlare = 4 * DEG_;
  /** bank the turns from one leg to the next are planned with (rad): less than the greatest, to have some left to correct */
  double bankPlan = 20 * DEG_;
  /** pitch attitude it commands at most (rad), and how far below the critical angle of attack it stays */
  double pitchMin = -10 * DEG_;
  double pitchMax = 15 * DEG_;
  double aoaMargin = 3 * DEG_;
  /** fastest change of the bank (rad/s), of the commanded vertical speed (m/s²), of the throttle (1/s) and of the trim (1/s) */
  double rollRate = 10 * DEG_;
  double vsRate = 1;
  double throttleRate = 0.5;
  double trimRate = 0.3;
  /** take-off: the attitude the nose is raised to at vr (rad) and how fast (rad/s); the climb right after lift-off (m/s) */
  double rotatePitch = 8 * DEG_;
  double rotateRate = 4 * DEG_;
  double liftoffClimb = 1.5;
  /** heights above the field (m): the flaps come up after a take-off, the take-off is over, a plain take-off climbs to */
  double flapsUp = 100;
  double departed = 150;
  double departHeight = 1500 * FT_;
  /** the circuit: height above the field, the downwind leg's distance from the centre line, the length of the final, how far beyond the runway's end the crosswind leg is (m) */
  double circuitHeight = 1000 * FT_;
  double downwind = 1000;
  double final = 2600;
  double upwind = 600;
  /** glide path (rad); the gate where the approach must be stable and the flare height (m) */
  double glide = 3 * DEG_;
  double gate = 100 * FT_;
  double flare = 6;
  /** where along the runway it aims: this share of its length, within these bounds (m) */
  double aimShare = 0.15;
  double aimMin = 60;
  double aimMax = 250;
  /** circling a place: radius (m) */
  double orbit = 800;
  /** en route never below this much above the ground ahead (m), looked for this far ahead (m) */
  double clearance = 150;
  double lookAhead = 3000;
  /** landing attempts before it gives up and circles the field */
  double attempts = 3;
  /** a height change bigger than this (m) is a climb at full power and climb speed, ended this close to the target */
  double climbStart = 60;
  double climbEnd = 20;
  /** greatest vertical speeds holding or changing height (m/s) */
  double vsUp = 2.5;
  double vsDown = 3.5;
};
inline const AutopilotConstants AUTOPILOT{};

/** The speeds it flies (indicated, m/s), from the aeroplane's figures. */
struct AutopilotSpeeds {
  double climb = 0;
  double cruise = 0;
  /** clean in the circuit, with 10°, 20° and 30° of flap on the approach, over the threshold */
  double circuit = 0;
  double flaps10 = 0;
  double flaps20 = 0;
  double final = 0;
  double ref = 0;
  /** the flaps are put out only below this */
  double flapsOut = 0;
};

AutopilotSpeeds autopilotSpeeds(const AircraftGeometry& s);

/** 'off' | 'takeoff' | 'hold' | 'goto' | 'land' | 'demo' */
using AutopilotMode = std::string;

/**
 * What it is doing, for the HUD:
 *  - on the runway: 'lineup' (flaps set, brakes on), 'takeoffRoll', 'rotate', 'rejected' (stopping: the runway
 *    left would not have been enough), 'flare', 'rollout', 'stopped';
 *  - in the air: 'initialClimb' (after lift-off), 'climb' (to the height of a plain take-off), 'cruise' (heading
 *    and height hold), 'enroute' (to the place to circle), 'orbit', 'route' (the sightseeing waypoints), the legs of
 *    the circuit 'upwind', 'crosswind', 'downwind', 'base', 'final', and 'goAround';
 *  - 'crashed', 'off'.
 */
using AutopilotPhase = std::string;

/** How it keeps the height: 'ground' | 'climb' | 'hold' | 'descend' | 'glidePath' | 'flare' – on the ground, climbing at full power, holding it, changing it, on the glide path, flaring. */
using AutopilotVertical = std::string;

struct AutopilotWaypoint {
  double x = 0;
  double z = 0;
  /** m above sea level; left out: the height of the circuit */
  std::optional<double> altitude = std::nullopt;
};

struct AutopilotPoint {
  double x = 0;
  double z = 0;
  bool operator==(const AutopilotPoint&) const = default;
};

struct AutopilotState {
  AutopilotMode mode = "off";
  AutopilotPhase phase = "off";
  AutopilotVertical vertical = "ground";
  /** compass degrees it steers for: the track over the ground (the heading in heading hold and on the runway) */
  double targetHeading = 0;
  /** m above sea level (NaN on the ground) */
  double targetAltitude = std::numeric_limits<double>::quiet_NaN();
  /** indicated airspeed it flies at (m/s; NaN on the ground) */
  double targetSpeed = std::numeric_limits<double>::quiet_NaN();
  /** the flap lever it has set: 0…3 for 0°, 10°, 20°, 30° */
  int flapNotch = 1;
  /** where it flies to: the end of the leg, the place it circles, the aiming point on the runway (none: nowhere in particular) */
  std::optional<AutopilotPoint> waypoint = std::nullopt;
  /** m to the waypoint – on an approach along the way to the aiming point (NaN: none) */
  double distance = std::numeric_limits<double>::quiet_NaN();
  /** the waypoint of the sightseeing route flown to (0-based; = routeCount once they are done), and how many there are */
  int routeIndex = 0;
  int routeCount = 0;
  /** the runway of the take-off or landing as it is painted ("27"), '' if none */
  std::string runway;
  /** landing attempt (1 on the first approach), go-arounds so far */
  int attempt = 0;
  int goArounds = 0;
  /** standing still at the end: landed, or a take-off stopped ('rejected' before) */
  bool done = false;
  /** the take-off was stopped on the runway: it would not have been long enough */
  bool rejected = false;
  /** it gave up landing after AUTOPILOT.attempts tries and circles the field */
  bool gaveUp = false;
};

/** Throttle and trim of level flight at an indicated speed, worked out now and then (Aircraft.levelFlight is not cheap). */
class LevelFlightCache {
 public:
  double throttle = 0.5;
  double trim = 0;
  void get(Aircraft& a, double ias, double flap, double t);
  void clear();

 private:
  double at = -1;
  double key = std::numeric_limits<double>::quiet_NaN();
};

/** The pilot's controls the autopilot begins from (takeOver). */
struct TakeOverControls {
  double throttle = 0;
  double trim = 0;
};

struct GotoOptions {
  std::optional<double> radius = std::nullopt;
  std::optional<double> speed = std::nullopt;
  bool right = false;
};

class FlightAutopilot {
 public:
  const AircraftGeometry spec;
  const AutopilotSpeeds speeds;
  /** the controls of the last step (one object, filled again every step) */
  FlightControls controls;
  /** what it is doing, for the HUD (one object, updated every step) */
  AutopilotState state;

  explicit FlightAutopilot(const AircraftGeometry& spec = TRAINER);

  /** Switched off: the controls are the pilot's again (`update` returns the last ones unchanged). */
  void off();
  /**
   * Take off from the runway's start (as runwayStart(runway, fromSecond)
   * places the aeroplane): flaps 10°, full power, along the centre line,
   * rotate at vr, climb at vy, flaps up – then climb on along the runway's
   * direction to `altitude` (m above sea level; by default 1500 ft above the
   * field) and hold it there: the mode becomes 'hold'.  Started in the air
   * it holds the runway's direction at the height it is at.
   */
  void takeoff(const RunwayJson& runway, bool fromSecond = false, double altitude = std::numeric_limits<double>::quiet_NaN());
  /**
   * Hold a compass heading (degrees) and a height (m above sea level) at
   * `speed` (indicated m/s; by default cruise); turns the shorter way, climbs
   * at full power and climb speed when the height is far above.  Works from
   * wherever the aeroplane is – also right after Aircraft.placeInAir.
   */
  void hold(double heading, double altitude, double speed = std::numeric_limits<double>::quiet_NaN());
  /** Fly to (x, z) at `altitude` (m above sea level) and circle it (left-hand unless `right`) at `radius` m. */
  void goto_(double x, double z, double altitude, const GotoOptions& opts = {});
  /**
   * Land on the runway in the direction runwayStart(runway, fromSecond)
   * would take off in: join the circuit (or come straight in from far out
   * on the extended centre line), approach on the 3° path with the flaps
   * out, flare, touch down on the main wheels, brake to a stop on the
   * centre line.  A go-around and another circuit when the approach is not
   * stable at 100 ft or the touchdown would come too late.
   */
  void land(const RunwayJson& runway, bool fromSecond = false);
  /**
   * The demonstration flight: take off from the runway's start, fly the
   * circuit – or the sightseeing `route` first, then join the circuit – and
   * land again on the same runway in the same direction.  Started in the air
   * it leaves out the take-off.
   */
  void demo(const RunwayJson& runway, bool fromSecond = false, const std::vector<AutopilotWaypoint>& route = {});
  /**
   * Begin from the pilot's controls as they are – the throttle lever, the
   * trim – rather than from those of level flight.  Call it after choosing
   * the task and before its first update.
   */
  void takeOver(const TakeOverControls& c);

  bool engaged() const { return mode != "off"; }
  /** The flap lever it has set (0…3: 0°, 10°, 20°, 30°) – for the game's lever. */
  int flapNotch() const { return notch; }

  /**
   * The controls for this step: read the aeroplane, decide, return the
   * controls to step it with: Aircraft.step(dt, controls, ground).  `ground`
   * is the one the aeroplane is stepped on (the field's height is taken from
   * it).  The returned object is the autopilot's own and is reused.
   */
  FlightControls& update(Aircraft& a, double dt, Ground& ground);
  /**
   * update() and Aircraft.step() together, in pieces of at most 1/30 s: for
   * callers whose frames are long (a slow phone), so that the loops still
   * run often enough.
   */
  void fly(Aircraft& a, double dt, Ground& ground);

 private:
  enum class LegKind { route, out, upwind, crosswind, downwind, base, final };
  struct Leg {
    double ax, az, bx, bz;
    /** direction, length, compass course */
    double ux, uz, len, course;
    LegKind kind;
    /** m above sea level to fly it at (NaN: the plan's height) */
    double alt;
  };
  /** A runway in the direction it is used: from its threshold (where the take-off begins, the landing aims) towards the far end. */
  struct Rwy {
    std::string name;
    double tx, tz, ux, uz, len, course;
    /** aiming point: this far beyond the threshold (m) */
    double aim;
    /** height of the touchdown zone (m), sampled from the ground */
    double elev;
  };
  enum class LatMode { track, heading, ground, wings };
  enum class VertMode { ground, pitch, speed, vs, flare };

  static Leg makeLeg(double ax, double az, double bx, double bz, LegKind kind, double alt = std::numeric_limits<double>::quiet_NaN());
  static Rwy frame(const RunwayJson& r, bool second);
  static Leg runwayLeg(const Rwy& w, double s1, double v1, double s2, double v2, LegKind kind);
  static const char* legPhase(LegKind k);

  // ---- what it was told
  AutopilotMode mode = "off";
  AutopilotPhase phase = "off";
  /** the next step begins the new task (the loops take over from where the aeroplane is) */
  bool fresh = false;
  /** the pilot's throttle and trim to begin from (takeOver), else NaN */
  double handThrottle = std::numeric_limits<double>::quiet_NaN();
  double handTrim = std::numeric_limits<double>::quiet_NaN();
  /** the runway of the take-off and of the landing */
  std::optional<Rwy> rwy;
  /** the plan ends with a landing on `rwy` */
  bool landing = false;
  double side = -1;
  std::vector<AutopilotWaypoint> route;
  int routeIdx = 0;
  std::vector<Leg> legs;
  int leg = 0;
  /** heading hold, the height to hold or climb to, the speed asked for (NaN: cruise) */
  double holdCourse = 0;
  double holdAlt = std::numeric_limits<double>::quiet_NaN();
  double holdSpeed = std::numeric_limits<double>::quiet_NaN();
  /** the place to circle, the radius, −1 left-hand, +1 right-hand */
  double cx = 0;
  double cz = 0;
  double orbitR = AUTOPILOT.orbit;
  double orbitDir = -1;
  int attempt = 0;
  int goArounds = 0;
  bool gaveUp = false;
  bool rejected = false;
  bool gateDone = false;

  // ---- what it measures
  double wheelDrop = 0;
  int noseWheel = -1;
  double track = 0;
  /** where the aeroplane was at the last step (for the distances in the state), and the waypoint the state shows */
  double posX = 0;
  double posZ = 0;
  AutopilotPoint wp;
  /** m left along the plan to the aiming point (landing), NaN otherwise */
  double dRem = std::numeric_limits<double>::quiet_NaN();
  double elevAt = -1;
  double terrainAt = -1;
  double terrainFloor = -std::numeric_limits<double>::infinity();

  // ---- the loops
  double t = 0;
  double phaseT = 0;
  double airT = 0;
  double flapT = 0;
  VertMode vert = VertMode::ground;
  AutopilotVertical vertical = "ground";
  double pitchCmd = 0;
  double pitchI = 0;
  double rotateFrom = 0;
  double flarePitch = 0;
  /** the ailerons' share that holds the bank against the engine's torque and the rigging */
  double ailI = 0;
  double vsCmd = 0;
  double bankCmd = 0;
  double turnDir = 0;
  bool climbing = false;
  bool climbSpeedReached = false;
  double thrI = 0;
  bool thrFixed = true;
  double throttle = 0;
  double trim = 0;
  int notch = 1;
  LevelFlightCache lf;
  double ffClimb = 0.12;

  // ---- this step's targets (set by the guidance, flown by the loops)
  struct Targets {
    LatMode lat = LatMode::wings;
    double course = 0;
    double bankMax = AUTOPILOT.bankMax;
    double bankFF = 0;
    VertMode vert = VertMode::ground;
    double pitch = 0;
    double vs = 0;
    double speed = 0;
    /** a fixed throttle (0…1), or −1: the throttle holds `speed` */
    double throttle = 0;
    double brake = 0;
    bool park = false;
    double altitude = std::numeric_limits<double>::quiet_NaN();
  } tg;

  void begin(const AutopilotMode& mode);
  void start(Aircraft& a, Ground& ground);
  void lineupFrom(Aircraft& a, Ground& ground);
  void setPhase(const AutopilotPhase& p);
  void guide(Aircraft& a, Ground& ground);
  void lineup(Aircraft& a);
  void takeoffRoll(Aircraft& a);
  void rotate(Aircraft& a);
  void climbOut(Aircraft& a);
  void afterDeparture(Aircraft& a);
  void routeLegs(Aircraft& a);
  void departureClimb(Aircraft& a, Ground& ground);
  void cruise(Aircraft& a, Ground& ground);
  void circle(Aircraft& a, Ground& ground);
  void followPlan(Aircraft& a, Ground& ground);
  void approach(Aircraft& a, double h, double path);
  void flare(Aircraft& a);
  void rollout(Aircraft& a);
  void stopped(Aircraft& a);
  void goAround(Aircraft& a);
  void circuitAgain(Aircraft& a);
  void joinCircuit(Aircraft& a);
  std::vector<Leg> circuit(const Rwy& w, double from, double side) const;
  void sequence(Aircraft& a);
  double remaining(Aircraft& a) const;
  double legCourse(const Leg& leg, Aircraft& a, double look = std::numeric_limits<double>::quiet_NaN()) const;
  void onRunway(Aircraft& a);
  void alongRunway(Aircraft& a, double bankMax);
  void setNotch(int n);
  void cleanUp(Aircraft& a);
  void holdHeight(Aircraft& a, double alt, double speed, double vsFF = 0, double gain = 0.12);
  double floor(Aircraft& a, Ground& ground);
  double heightAboveField(Aircraft& a) const;
  bool mainContact(Aircraft& a) const;
  void loops(Aircraft& a, double dt);
  void publish();
};

// ---------------------------------------------------------------------------
// which runway

struct RunwayAt {
  const AirfieldJson* field = nullptr;
  const RunwayJson* runway = nullptr;
  bool fromSecond = false;
};

/**
 * The runway the aeroplane stands on and which way along it it faces – as
 * runwayStart's `fromSecond` – for a take-off from where it is; none when it
 * is on none.
 */
std::optional<RunwayAt> runwayAt(const std::vector<AirfieldJson>& fields, double x, double z, double yaw);

struct LandingRunway {
  const AirfieldJson* field = nullptr;
  const RunwayJson* runway = nullptr;
  bool fromSecond = false;
  double distance = 0;
};

/**
 * A runway to land on: at the nearest airfield with one at least
 * `minLength` m long, its longest; of its two directions the one whose
 * approach the aeroplane at (x, z) is on the side of.
 */
std::optional<LandingRunway> landingRunway(const std::vector<AirfieldJson>& fields, double x, double z, double minLength = 400);

// ---------------------------------------------------------------------------
// the assist: help for the player's own hands

/** The player's stick and pedals, as the game reads them (Input.flightInput). */
struct StickInput {
  /** −1…1, + = pulled (nose up) */
  double pitch = 0;
  /** −1…1, + = to the right */
  double roll = 0;
  /** −1…1 pedals, + = right */
  double yaw = 0;
};

struct AssistOptions {
  /**
   * 0…1: how much the stick is softened – less movement round the middle
   * (an expo curve) and keys that push it over some tenths of a second
   * instead of all at once.
   */
  double soften = 0;
  /** the rudder keeps the ball in the middle by itself (the pedals add to it) */
  bool coordinate = false;
  /**
   * The stick asks for a roll and a pitch rate instead of moving the
   * surfaces: let go, the wings come level and the nose stays where it was
   * put – within 45° of bank and ±15° of pitch.
   */
  bool stabilize = false;
  /**
   * No stall, no overspeed, no more than 60° of bank, 30° of pitch and 3.5 g –
   * also without `stabilize`; above manoeuvring speed the stick moves the
   * elevator less, so that a full stick asks for about the same g at any speed.
   */
  bool protect = false;
  /** the trim follows the speed flown, so the stick can be let go (with `stabilize` it always does) */
  bool autoTrim = false;
};

/** Everything on: for the first flights. */
inline const AssistOptions ASSIST_BEGINNER{0.6, true, true, true, true};
/** The aeroplane as it is, with a softer stick, the rudder coordinated and the edges of the envelope guarded. */
inline const AssistOptions ASSIST_LIGHT{0.35, true, false, true, true};

/** Rates and limits of the assist. */
struct AssistConstants {
  static constexpr double DEG_ = 3.14159265358979323846 / 180;
  /** stabilised: roll and pitch rate at full stick (rad/s); greatest bank and pitch (rad); the wings come level at (rad/s) */
  double rollRate = 30 * DEG_;
  double pitchRate = 8 * DEG_;
  double bank = 45 * DEG_;
  double pitchUp = 15 * DEG_;
  double pitchDown = -15 * DEG_;
  double levelRate = 8 * DEG_;
  /** protection: angle of attack kept this far below the critical one (rad); pitch and bank (rad); load factor (g); airspeed as a share of vne */
  double aoaMargin = 3 * DEG_;
  double pitchMax = 30 * DEG_;
  double bankMax = 60 * DEG_;
  double gMax = 3.5;
  double gMin = -1.5;
  double vMax = 0.85;
};
inline const AssistConstants ASSIST{};

/**
 * Help for the player flying by hand.  Each step `apply` turns the stick
 * into elevator, ailerons, rudder and trim (throttle, flaps and brakes stay
 * the game's).  On the ground only the softening and the nose wheel's
 * steering from the stick (as the game has it) apply.
 */
class FlightAssist {
 public:
  AssistOptions options;

  explicit FlightAssist(const AssistOptions& options = ASSIST_BEGINNER, const AircraftGeometry& spec = TRAINER);

  /** Begin afresh from the aeroplane as it is (after a placement, after the autopilot flew), with the trim the lever has. */
  void reset(double trim = std::numeric_limits<double>::quiet_NaN());

  /**
   * Fill `out.elevator`, `out.aileron`, `out.rudder` and `out.trim` for this
   * step from the stick; `out.trim` is read first as the lever's trim when
   * the assist does not trim by itself.
   */
  void apply(Aircraft& a, const StickInput& stick, double dt, FlightControls& out);

 private:
  AircraftGeometry spec;
  /** the stick after softening */
  double sPitch = 0;
  double sRoll = 0;
  double sYaw = 0;
  /** stabilised: the attitude held (NaN: take the present one), and the ailerons' integral */
  double pitchRef = std::numeric_limits<double>::quiet_NaN();
  double bankRef = 0;
  double ailI = 0;
  double trim = std::numeric_limits<double>::quiet_NaN();
  double t = 0;
  double lastIas = std::numeric_limits<double>::quiet_NaN();
  double iasRate = 0;
  LevelFlightCache lf;
};

}  // namespace fs

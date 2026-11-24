// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/flight.ts @ v1.7.7
/**
 * Flight dynamics of the light aeroplane (sim/flightspec.ts): a 6-DOF rigid
 * body under aerodynamic, propeller, gravity and landing-gear forces.
 *
 *  - Aerodynamics: whole-aircraft coefficients in the classic stability-
 *    derivative form (lift, drag and side force; pitching, rolling and yawing
 *    moments), of the size published for two-seat high-wing trainers.  The
 *    lift curve rounds off below the critical angle and falls gently past it
 *    towards that of a flat plate, so the aeroplane is defined at any angle
 *    of attack and sideslip – tumbling, flying backwards – not only in normal
 *    flight.  Near the ground the induced drag drops (ground effect).
 *  - Atmosphere: ISA density with height; an optional wind.
 *  - Engine and fixed-pitch propeller: the rpm follows the balance of engine
 *    torque and propeller load, thrust falls with airspeed (thrust and power
 *    coefficients over the advance ratio); with the engine off the propeller
 *    windmills, or stops when the air is too slow to turn it.
 *  - Landing gear: three sprung and damped legs.  The tyres are bristle models
 *    as in the car (vehicle/vehicle.ts) – real static friction, so the parked
 *    aeroplane neither creeps nor trembles – with brakes on the main wheels
 *    and a nose wheel steered by the rudder pedals.
 *  - Crashes: too hard a touchdown, a wing tip or the propeller on the
 *    ground, the nose wheel folding, the airframe overstressed.
 *
 * Integrated in substeps of at most 1/240 s, allocation-free in step(), and
 * without three.js like the rest of the simulation.
 *
 * Frames as everywhere in the game: world x east, y up, z south; body x to
 * the right, y up, z to the back (the nose looks towards -z), origin at the
 * centre of mass.  The aerodynamic coefficients are the textbook ones
 * (stability axes: x forward, y right, z down; p roll right, q pitch up,
 * r yaw right); `substep` turns them into the body frame of the game.
 *
 * Port notes:
 *  - The TS getters of Aircraft are methods here: ias(), tas(), groundSpeed(),
 *    altitude(), agl(), verticalSpeed(), heading(), yaw(), pitch(), bank(),
 *    slip(), aoa(), sideslip(), turnRate(), rpm(), engineRunning(),
 *    enginePower(), propThrust(), fuelFlow(), onGround(), stallWarning(),
 *    stalled(), loadFactor(), propAngle(), flapsDeg(), surfaces().
 *  - `crashed: null | CrashKind` is `std::optional<CrashKind>` (an enum class);
 *    crashKindName() gives the TS string ('impact', 'wing', 'noseOver',
 *    'overspeed'), e.g. for the text key `planeCrash.<kind>`.
 *  - levelFlight's default arguments (the flaps as they are, the present
 *    altitude) are overloads.
 *  - placeInAir's optional ground is a pointer (nullptr: none).
 */
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "sim/flightspec.h"
#include "sim/vehicle/types.h"

namespace fs {

struct FlightControls {
  /** 0…1 */
  double throttle = 0;
  /** -1…1, + = pull (nose up) */
  double elevator = 0;
  /** -1…1, + = roll right */
  double aileron = 0;
  /** -1…1, + = yaw right (also steers the nose wheel) */
  double rudder = 0;
  /** 0…1 of TRAINER.flapsMax; the flaps follow at about 5°/s */
  double flaps = 0;
  /** -1…1 elevator trim, + = nose up */
  double trim = 0;
  /** 0…1, both main wheels */
  double brake = 0;
  /** optional, added to `brake` on that side */
  std::optional<double> brakeLeft = std::nullopt;
  std::optional<double> brakeRight = std::nullopt;
  bool parkingBrake = false;
  /** (true as the game's controls begin, game.ts:435) */
  bool engineOn = true;
};

enum class CrashKind { impact, wing, noseOver, overspeed };

/** The TS name of a crash: "impact", "wing", "noseOver", "overspeed". */
const char* crashKindName(CrashKind k);

struct WheelView {
  /** strut compression (m) */
  double compression = 0;
  /** accumulated rotation (rad) */
  double spin = 0;
  bool contact = false;
  /** steering angle (rad, + = right) */
  double steer = 0;
};

/** x, y, z (world or body, as the field says) */
struct FlightVec {
  double x = 0;
  double y = 0;
  double z = 0;
};

namespace flight_detail {
inline constexpr double DEG = M_PI / 180;
}

/**
 * Aerodynamic coefficients of the whole aeroplane, per radian where they are
 * derivatives; reference area, span and chord come from the geometry.  The
 * values are of the size published for two-seat high-wing trainers, then
 * tuned so that the aeroplane flies its figures in flightspec.ts.
 */
inline constexpr struct AeroCoefficients {
  /** lift at zero angle of attack (the wing's incidence), slope, from pitch rate (per qc/2V) and per rad elevator (trailing edge up) */
  double CL0 = 0.307;
  double CLa = 4.41;
  double CLq = 3.9;
  double CLde = -0.3;
  /** angle of attack (rad) at the top of the lift curve, clean; the curve starts to round off this far below it */
  double alphaCrit = 0.29;
  double round = 0.052;
  /** top of the lift curve at negative angles (rad) */
  double alphaCritNeg = -0.21;
  /** full flaps: lift added, shift of the critical angle (rad) */
  double flapCL = 0.6;
  double flapAlpha = -0.016;
  /** past the critical angle the lift falls to this share of its maximum within `stallDrop` rad, then joins the flat plate */
  double postStall = 0.72;
  double stallDrop = 0.105;
  /** flat plate: lift = flatCL·sin 2α, drag = CD0 + CDgear + flatCD·sin²α */
  double flatCL = 1.1;
  double flatCD = 1.25;
  /** drag: airframe, fixed gear, Oswald factor, full flaps, separated flow at the stall, sideslip */
  double CD0 = 0.026;
  double CDgear = 0.008;
  double oswald = 0.72;
  double flapCD = 0.055;
  double stallCD = 0.08;
  double CDbeta = 0.25;
  /** side force: sideslip, rudder (trailing edge right), yaw rate */
  double CYb = -0.39;
  double CYdr = -0.19;
  double CYr = 0.2;
  /** pitch: at zero angle of attack, static stability (per sin α), damping incl. downwash lag, elevator, full flaps, the nose dropping at the stall, ground effect (per unit of 1 − φ, see sampleGround) */
  double Cm0 = 0.042;
  double Cma = -0.6;
  double Cmq = -18;
  double Cmde = 1.1;
  double Cmflap = -0.03;
  double CmStall = -0.08;
  double CmGround = -0.25;
  /** roll: dihedral effect, damping, from yaw rate (Clr0 + ClrCL·CL), aileron, rudder */
  double Clb = -0.065;
  double Clp = -0.48;
  double Clr0 = 0.02;
  double ClrCL = 0.2;
  double Clda = 0.115;
  double Cldr = -0.012;
  /** yaw: weathercock stability, from roll rate (·CL, adverse), damping, adverse yaw of the ailerons, rudder */
  double Cnb = 0.065;
  double CnpCL = -0.06;
  double Cnr = -0.095;
  double Cnda = -0.008;
  double Cndr = 0.065;
  /**
   * Rigging (a fin set a little off the centreline, a tab on an aileron):
   * rolling and yawing moment to the right that cancel the engine's torque
   * and the slipstream's swirl in cruise at 75 % power.  Climbing at full
   * power the propeller wins (a little right rudder), gliding at idle the
   * rigging does.  Scaled with the engine's power.
   */
  double rigRoll = 0.00047;
  double rigYaw = 0.0003;
  /** control surface travel (rad): elevator up and down, ailerons, rudder, and the trim range (elevator equivalent, nose down / up) */
  double elevUp = 25 * flight_detail::DEG;
  double elevDown = 20 * flight_detail::DEG;
  double aileron = 20 * flight_detail::DEG;
  double rudder = 16 * flight_detail::DEG;
  double trimDown = 3.5 * flight_detail::DEG;
  double trimUp = 3.8 * flight_detail::DEG;
  /** stall warning this far below the critical angle (rad) */
  double warn = 2 * flight_detail::DEG;
  /** flap travel (degrees per second) */
  double flapRate = 5;
} AERO{};

/**
 * Fixed-pitch propeller and engine.  Thrust and power coefficients over the
 * advance ratio J = V/(nD): CT = CT0·(1 − (J/J0)^1.5), CP = CP0·(1 − (J/Jp)²),
 * both negative where the air drives the propeller (windmilling).  They are
 * the values of a cruise propeller matched to the 160 PS engine (2300 rpm
 * static, about 2600 rpm in cruise) and scaled with the power for the
 * others, as if each engine had its own propeller.
 */
inline constexpr struct PropellerConstants {
  double CT0 = 0.092;
  double J0 = 1.05;
  double CP0 = 0.0587;
  double Jp = 1.089;
  /** beyond this advance ratio the curves are not extended (a slow or stopped propeller) */
  double Jmax = 1.6;
  /** share of the propeller's slipstream that reaches the tail surfaces */
  double slipstream = 0.35;
  /** yawing moment of the slipstream's swirl on the fin and of the P-factor (m, m per rad of α), both to the left */
  double swirl = 0.08;
  double pFactor = 0.25;
  /** engine and propeller (kg·m²) */
  double inertia = 2.2;
} PROP{};

inline constexpr struct EngineConstants {
  /** rpm at which the rated power is given */
  double ratedRpm = 2700;
  /** friction and pumping: a + b·ω (N·m, ω in rad/s) */
  std::array<double, 2> friction{10, 0.08};
  /** throttle opening at the idle stop */
  double idleOpen = 0.12;
  /** the dead engine: breakaway torque (compression), and friction and pumping once it turns (N·m) */
  double breakaway = 40;
  double deadFriction = 28;
  double starter = 150;
  /** rpm at which a cranked engine fires */
  double catchRpm = 350;
  /** below this a running engine dies (rpm) */
  double stallRpm = 180;
  /** fuel: specific consumption (kg/kWh of indicated power), density (kg/l), and what the idling engine burns anyway (l/h) */
  double bsfc = 0.24;
  double fuelDensity = 0.72;
  double idleFlow = 2.5;
} ENGINE{};

inline constexpr struct GearConstants {
  /** share of the travel the legs are pushed in when standing, and their damping ratio */
  double staticShare = 0.4;
  double damping = 0.4;
  /** beyond the travel: the tyre and the bottomed strut, stiffness as a multiple of the spring */
  double bump = 8;
  /** tyres: friction coefficient on dry asphalt, cornering stiffness per unit of static load (1/rad), relaxation length (m) */
  double mu = 0.8;
  double cornering = 8;
  double relax = 0.25;
  /** rolling resistance on asphalt (the ground's `rolling` multiplies it) */
  double rolling = 0.02;
  /** braking force per main wheel at full pedal (about 0.4 g in all), and of the parking brake (N per 1000 kg of aeroplane) */
  double brake = 1900;
  double parking = 3000;
  /** nose wheel: steering angle at full rudder (rad), halved at this ground speed (m/s) */
  double steer = 12 * flight_detail::DEG;
  double steerFade = 10;
} GEAR{};

/** What breaks the aeroplane. */
inline constexpr struct FlightLimits {
  /** sink rate of a wheel at touchdown (m/s) */
  double touchdown = 3.5;
  /**
   * Load on a leg as a multiple of its static load at which it folds: the
   * nose leg when the aeroplane is put down on it, a main leg only in a slam
   * that the touchdown rule did not catch (bouncing without leaving the ground).
   */
  double noseLeg = 7;
  double mainLeg = 15;
  /** airspeed as a multiple of vne */
  double overspeed = 1.2;
  /** load factor (g) at which the airframe fails */
  double gMax = 7;
  double gMin = -4;
  /** sink rate (m/s) at which the airframe hitting the ground is a crash whatever part touches */
  double hull = 1.5;
} LIMITS{};

/** ISA air density (kg/m³) at a height above sea level (m). */
double airDensity(double h);

/** Result of `liftCurve`. */
struct LiftCurve {
  double cl = 0;
  double stall = 0;
  double sep = 0;
};

/**
 * Lift coefficient over the angle of attack (rad, any angle) with the flaps
 * at `f` (0…1).  Straight up to `round` below the critical angle, a rounded
 * top, then down to `postStall` of the maximum and on to the flat plate.
 * Also gives `stall` (0…1: how far the stall has progressed) and `sep`
 * (0…1: how far the flow has become that of a flat plate).
 */
LiftCurve liftCurve(double alpha, double f);

struct MaxLift {
  double cl = 0;
  double alpha = 0;
};

/** Highest lift coefficient with the flaps at `f` (0…1), and the angle where it is reached. */
MaxLift maxLift(double f);

/** What steady level flight needs (Aircraft::levelFlight). */
struct LevelFlight {
  double throttle = 0;
  double trim = 0;
  double alpha = 0;
  double rpm = 0;
};

/** Control surface deflections as they are (Aircraft::surfaces). */
struct FlightSurfaces {
  double elevator = 0;
  double aileron = 0;
  double rudder = 0;
};

class Aircraft {
 public:
  const AircraftGeometry spec;
  /** PS as built */
  double power;
  /** centre of mass (world) */
  FlightVec pos;
  /** world m/s */
  FlightVec vel;
  /** body → world [x, y, z, w] */
  std::array<double, 4> q{0, 0, 0, 1};
  /** body rates (rad/s) about the body axes x (right: pitch, + nose up), y (up: yaw, + left), z (back: roll, + left wing down) */
  FlightVec omega;
  std::vector<WheelView> wheels;
  std::optional<CrashKind> crashed = std::nullopt;
  /** litres */
  double fuel;
  /** wind (world, m/s, the direction it blows towards) – or a field, sampled at the centre of mass once per step */
  FlightVec wind;
  std::function<void(double x, double y, double z, FlightVec& out)> windField = nullptr;
  /** the tail touched the ground since the last placement (not a crash when gentle) */
  bool tailStrike = false;

  const double mass;
  /** principal moments of inertia about the body x (pitch), y (yaw) and z (roll) axes (kg·m²) */
  std::array<double, 3> inertia;

  explicit Aircraft(const AircraftGeometry& spec = TRAINER, std::optional<double> powerPs = std::nullopt);

  // placing

  /**
   * At rest on its wheels at (x, z), facing `yaw` (game convention: 0 north,
   * + left), the legs pushed in by their static loads so that nothing moves
   * when the simulation starts.
   */
  void placeOnGround(double x, double z, double yaw, Ground& ground, bool engineRunning = false);

  /**
   * In level flight at (x, y, z) heading `yaw` with `speed` (true airspeed,
   * m/s): at the angle of attack that carries the weight, the propeller at
   * the rpm of level flight.  For hands-off flight the game sets throttle
   * and trim from levelFlight(speed).  With the ground given, `agl` is right
   * at once; otherwise it counts from sea level until the first step.
   */
  void placeInAir(double x, double y, double z, double yaw, double speed, Ground* ground = nullptr);

  /**
   * What steady level flight at `tas` (m/s) needs at the present altitude and
   * flap setting: throttle and trim (as the controls take them, the elevator
   * left in the middle), the angle of attack (rad) and the rpm.
   */
  LevelFlight levelFlight(double tas);
  LevelFlight levelFlight(double tas, double flaps);
  LevelFlight levelFlight(double tas, double flaps, double altitude);

  // simulation

  void step(double dt, const FlightControls& c, Ground& ground);

  // what the game reads

  /** indicated airspeed (m/s) */
  double ias() const;
  /** true airspeed (m/s) */
  double tas() const { return tasV; }
  double groundSpeed() const { return std::sqrt(vel.x * vel.x + vel.z * vel.z); }
  /** m above sea level */
  double altitude() const { return pos.y; }
  /** from the lowest wheel to the ground below (m) */
  double agl() const { return aglV; }
  double verticalSpeed() const { return vel.y; }
  /** compass degrees 0…360 (0 north, 90 east) */
  double heading() const;
  /** game convention: 0 north, + left (rad) */
  double yaw() const;
  /** rad, + nose up */
  double pitch() const;
  /** rad, + right wing down */
  double bank() const;
  /** inclinometer ball, -1…1, + = ball to the right */
  double slip() const { return ball; }
  /** angle of attack (rad) */
  double aoa() const { return alphaV; }
  /** sideslip (rad, + = the air comes from the right) */
  double sideslip() const { return betaV; }
  /** rad/s, + = turning right */
  double turnRate() const;
  double rpm() const;
  bool engineRunning() const { return running; }
  /** power at the propeller shaft (W) */
  double enginePower() const { return running ? qEngine * omegaE : 0; }
  /** thrust of the propeller (N, negative when it windmills) */
  double propThrust() const { return thrust; }
  /** litres per hour */
  double fuelFlow() const { return fuelFlowLh; }
  bool onGround() const { return contactAny; }
  bool stallWarning() const { return warnV; }
  /** the wing is past its critical angle */
  bool stalled() const { return tasV > 10 && stallV > 0; }
  /** g, 1 in level flight and standing */
  double loadFactor() const { return nzV; }
  /** accumulated rad */
  double propAngle() const { return propAng; }
  double flapsDeg() const { return flapDeg; }
  /**
   * Deflections as they are (rad): elevator + = trailing edge up (nose up),
   * trim included; aileron + = right aileron up, left down (rolling right);
   * rudder + = trailing edge to the right (yawing right).
   */
  const FlightSurfaces& surfaces();

 private:
  struct Leg {
    /** wheel centre with the strut extended (body) */
    double x = 0;
    double y = 0;
    double z = 0;
    double radius = 0;
    double travel = 0;
    bool steers = false;
    /** -1 left main wheel, +1 right, 0 nose */
    double side = 0;
    /** static load (N), spring (N/m), damper (N·s/m), tyre stiffness (N/m) and bristle damping (N·s/m) */
    double load = 0;
    double k = 0;
    double c = 0;
    double kTyre = 0;
    double cTyre = 0;
  };

  void readControls(const FlightControls& c);
  void reset(bool engineRunning);
  void crash(CrashKind kind);
  bool finite() const;
  void liftOff(int i);
  void updateRot();
  /** The ground below the centre of mass, taken as a plane for the step (wheels near it sample their own). */
  void sampleGround(Ground& ground);
  double planeY(double x, double z) const;
  /** Elevator deflection (rad, + = trailing edge up) from stick and trim. */
  double elevatorFromTrim(double trim, double elevator) const;
  /** Torque of the running engine at a throttle and crankshaft speed (rad/s), after its own friction. */
  double engineTorque(double t, double w) const;
  /** Thrust (N) and the torque the air puts on the propeller (N·m) at a speed (rad/s) and axial airspeed. */
  void propeller(double w, double va, double rho);
  /** Crankshaft speed (rad/s) at which the running engine and the propeller balance. */
  double steadyOmega(double t, double va, double rho);
  /** Engine and propeller over one substep; `va` is the airspeed along the propeller axis. */
  void engine(double h, double va);
  void substep(double h, Ground& ground, bool near);
  /** A wheel off the ground runs down slowly, or stops at once when braked. */
  void spinFree(int i, double h);
  /** Values for the instruments and the limits, once per step. */
  void derive(double dt);

  std::vector<Leg> legs;
  std::vector<double> hull;
  std::vector<uint8_t> hullKind;
  /** farthest point of the airframe or the wheels from the centre of mass (m) */
  double reach = 0;
  std::array<double, 9> R{};
  double kInd = 0;
  double qInd = 0;
  double propScale = 0;
  double diskArea = 0;
  // engine and propeller
  double omegaE = 0;
  bool running = false;
  double propAng = 0;
  double qEngine = 0;
  double thrust = 0;
  /** torque of the air on the propeller (N·m, negative when the air drives it) */
  double qAir = 0;
  double fuelFlowLh = 0;
  // controls as they are (surfaces lag a little, flaps run at their rate)
  double flapDeg = 0;
  double dE = 0;
  double dA = 0;
  double dR = 0;
  double steerAng = 0;
  FlightSurfaces surf;
  // inputs of the current step, cleaned
  double cThr = 0;
  double cElev = 0;
  double cAil = 0;
  double cRud = 0;
  double cFlap = 0;
  double cTrim = 0;
  double cBrakeL = 0;
  double cBrakeR = 0;
  bool cPark = false;
  bool cEngine = false;
  // air data
  double rho = 1.225;
  double tasV = 0;
  double alphaV = 0;
  double betaV = 0;
  double stallV = 0;
  bool warnV = false;
  // ground under the aeroplane, sampled once per step: a plane through (gx, gy, gz) with normal gn
  double gx = 0;
  double gy = 0;
  double gz = 0;
  double gnx = 0;
  double gny = 1;
  double gnz = 0;
  double aglV = 0;
  double phi = 1;
  // gear and tyres
  std::vector<double> zLong;
  std::vector<double> zLat;
  std::vector<uint8_t> wasDown;
  std::vector<double> wheelW;
  bool contactAny = false;
  // specific force (body, N) for the load factor and the ball
  double fsx = 0;
  double fsy = 0;
  double ball = 0;
  double nzV = 1;
  double gTime = 0;
};

}  // namespace fs

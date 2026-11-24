// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/autopilot.ts @ v1.7.7
#include "sim/autopilot.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "sim/airfield.h"

namespace fs {

namespace {

constexpr double DEG = M_PI / 180;
constexpr double G = 9.81;
constexpr double RHO0 = 1.225;
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
constexpr double Inf = std::numeric_limits<double>::infinity();

double clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

/** Math.sign */
double sign(double v) { return v > 0 ? 1 : v < 0 ? -1 : v; }

/** Compass degrees of a direction in the world (x east, z south). */
double compass(double dx, double dz) {
  const double d = std::atan2(dx, -dz) / DEG;
  return d < 0 ? d + 360 : d;
}

double wrap180(double d) {
  d = std::fmod(d, 360);
  return d > 180 ? d - 360 : d < -180 ? d + 360 : d;
}

double norm360(double d) {
  d = std::fmod(d, 360);
  return d < 0 ? d + 360 : d;
}

GroundSample smp;

/** Height of the ground (the top surface) at (x, z). */
double groundY(Ground& ground, double x, double z) {
  ground.sample(x, z, 1e4, smp);
  const double y = smp.y;
  ground.sample(x, z, y + 1, smp);
  return std::isfinite(smp.y) ? smp.y : y;
}

/** Angle of attack (rad) at which the wing gives the lift coefficient `cl` with the flaps at `f` (0…1); the critical angle if it cannot. */
double alphaFor(double cl, double f) {
  double lo = AERO.alphaCritNeg;
  double hi = maxLift(f).alpha;
  if (liftCurve(hi, f).cl <= cl) return hi;
  if (liftCurve(lo, f).cl >= cl) return lo;
  for (int i = 0; i < 24; i++) {
    const double m = (lo + hi) / 2;
    if (liftCurve(m, f).cl < cl) lo = m;
    else hi = m;
  }
  return (lo + hi) / 2;
}

/** Lift coefficient that carries the weight at `ias` (m/s) with `n` g. */
double liftNeeded(const Aircraft& a, double ias, double n) {
  return (n * a.mass * G) / (0.5 * RHO0 * std::pow(std::max(15.0, ias), 2) * a.spec.wingArea);
}

/**
 * The yaw rate (rad/s, body axes, + to the right) of a steady coordinated
 * turn at the present bank – not the measured one, which would also carry
 * the Dutch roll: what cancels the moments of the turn must not cancel the
 * aeroplane's own damping of that oscillation.
 */
double turnYawRate(const Aircraft& a) { return (G * std::sin(a.bank()) * std::cos(a.pitch())) / std::max(15.0, a.tas()); }

/**
 * Ailerons that hold a bank angle (rad): proportional, with the roll rate
 * damped – less the roll rate a steady turn has in the body axes, which is
 * not to be fought – and the rolling moment of the turn's yaw rate (the
 * outer wing is faster: the aeroplane tends to steepen its turns, more the
 * slower it flies) cancelled ahead with the flight model's own coefficients.
 * `integral` holds what is left: the engine's torque, which changes with the
 * power.
 */
double aileronFor(const Aircraft& a, double bank, double integral) {
  const AircraftGeometry& s = a.spec;
  const double rh = (turnYawRate(a) * s.span) / (2 * std::max(15.0, a.tas()));
  const double cl = std::min(1.6, liftNeeded(a, a.ias(), std::max(0.5, a.loadFactor())));
  const double ff = (-(AERO.Clr0 + AERO.ClrCL * cl) * rh) / AERO.Clda / AERO.aileron;
  const double p = -a.omega.z + a.turnRate() * std::sin(a.pitch());
  return clamp(3 * (bank - a.bank()) - 0.6 * p + integral + ff, -1, 1);
}

/**
 * The rudder (−1…1) that keeps the ball in the middle: ahead, what cancels
 * the yawing moments the flight model knows of – the ailerons' adverse yaw
 * (`aileron` as commanded), the roll rate's, the damping of the turn's yaw
 * rate, the rigging, the propeller's swirl and P-factor –, and behind it the
 * sideslip that is left, fed back.
 */
double rudderFor(const Aircraft& a, double aileron) {
  const AircraftGeometry& s = a.spec;
  const double V = std::max(15.0, a.tas());
  const double qbar = 0.5 * RHO0 * std::pow(std::max(15.0, a.ias()), 2);
  const double cl = std::min(1.6, liftNeeded(a, a.ias(), std::max(0.5, a.loadFactor())));
  const double ph = (-a.omega.z * s.span) / (2 * V);
  const double rh = (turnYawRate(a) * s.span) / (2 * V);
  const double cn = AERO.CnpCL * cl * ph + AERO.Cnr * rh + AERO.Cnda * aileron * AERO.aileron + AERO.rigYaw * (a.power / 160);
  const double tp = std::max(0.0, a.propThrust());
  const double disk = (M_PI * s.propDiameter * s.propDiameter) / 4;
  const double tail = 1 + (PROP.slipstream * tp) / (disk * qbar);
  const double yawLeft = tp * (PROP.swirl + PROP.pFactor * clamp(a.aoa(), -0.3, 0.3));
  const double dr = (yawLeft - qbar * s.wingArea * s.span * cn) / (tail * qbar * s.wingArea * s.span * AERO.Cndr);
  return clamp(dr / AERO.rudder + 3 * a.sideslip(), -1, 1);
}

/** The pitch attitude (rad) to hold, from a command, within the limits; the angle of attack kept `margin` below the critical one. */
double protectPitch(const Aircraft& a, double cmd, double lo, double hi, double margin) {
  double lim = clamp(cmd, lo, hi);
  if (!a.onGround()) {
    const double flap = a.spec.flapsMax > 0 ? clamp(a.flapsDeg() / a.spec.flapsMax, 0, 1) : 0;
    const double top = AERO.alphaCrit + AERO.flapAlpha * flap - margin;
    if (a.aoa() > top - 2 * DEG) lim = std::min(lim, a.pitch() - 1.5 * (a.aoa() - (top - 2 * DEG)));
  }
  return lim;
}

/** A stick deflection (−1…1, anything else counts as 0) through an expo curve: `k` of it cubed, less movement round the middle. */
double expo(double x, double k) {
  const double v = std::isfinite(x) ? clamp(x, -1, 1) : 0;
  return (1 - k) * v + k * v * v * v;
}

}  // namespace

AutopilotSpeeds autopilotSpeeds(const AircraftGeometry& s) {
  AutopilotSpeeds r;
  r.climb = s.vy;
  r.cruise = 0.9 * s.vcruise;
  r.circuit = 1.7 * s.vs1;
  r.flaps10 = 1.55 * s.vs1;
  r.flaps20 = 1.55 * s.vs0;
  r.final = 1.3 * s.vs0 + 3;
  r.ref = 1.3 * s.vs0;
  r.flapsOut = 2 * s.vs0;
  return r;
}

// ---------------------------------------------------------------------------
// LevelFlightCache

void LevelFlightCache::get(Aircraft& a, double ias, double flap, double t) {
  const double key = jsRound(ias * 2) * 1000 + jsRound(flap * 30);
  if (key == this->key && t < at) return;
  this->key = key;
  at = t + 2;
  const double tas = ias * std::sqrt(RHO0 / airDensity(a.altitude()));
  const LevelFlight lf = a.levelFlight(tas, flap, a.altitude());
  throttle = lf.throttle;
  trim = lf.trim;
}

void LevelFlightCache::clear() { at = -1; }

// ---------------------------------------------------------------------------
// FlightAutopilot: the plan's pieces

FlightAutopilot::Leg FlightAutopilot::makeLeg(double ax, double az, double bx, double bz, LegKind kind, double alt) {
  const double len = std::hypot(bx - ax, bz - az);
  const double ux = len > 1e-6 ? (bx - ax) / len : 0;
  const double uz = len > 1e-6 ? (bz - az) / len : -1;
  return Leg{ax, az, bx, bz, ux, uz, len, compass(ux, uz), kind, alt};
}

FlightAutopilot::Rwy FlightAutopilot::frame(const RunwayJson& r, bool second) {
  const double ax = second ? r.x2 : r.x1;
  const double az = second ? r.z2 : r.z1;
  const double bx = second ? r.x1 : r.x2;
  const double bz = second ? r.z1 : r.z2;
  const double len = std::max(1.0, std::hypot(bx - ax, bz - az));
  const double ux = (bx - ax) / len;
  const double uz = (bz - az) / len;
  const AutopilotConstants& A = AUTOPILOT;
  return Rwy{runwayEnd(r, second), ax, az, ux, uz, len, compass(ux, uz), clamp(A.aimShare * len, A.aimMin, A.aimMax), NaN};
}

/** A leg given in the runway's frame: `s` along it from the threshold, `v` to the right of the centre line. */
FlightAutopilot::Leg FlightAutopilot::runwayLeg(const Rwy& w, double s1, double v1, double s2, double v2, LegKind kind) {
  return makeLeg(w.tx + w.ux * s1 - w.uz * v1, w.tz + w.uz * s1 + w.ux * v1, w.tx + w.ux * s2 - w.uz * v2, w.tz + w.uz * s2 + w.ux * v2, kind);
}

const char* FlightAutopilot::legPhase(LegKind k) {
  switch (k) {
    case LegKind::route:
      return "route";
    case LegKind::out:
      return "out";
    case LegKind::upwind:
      return "upwind";
    case LegKind::crosswind:
      return "crosswind";
    case LegKind::downwind:
      return "downwind";
    case LegKind::base:
      return "base";
    case LegKind::final:
      return "final";
  }
  return "";
}

namespace {
template <class W>
double alongW(const W& w, double x, double z) { return (x - w.tx) * w.ux + (z - w.tz) * w.uz; }
template <class W>
double acrossW(const W& w, double x, double z) { return -(x - w.tx) * w.uz + (z - w.tz) * w.ux; }
}  // namespace

FlightAutopilot::FlightAutopilot(const AircraftGeometry& spec) : spec(spec), speeds(autopilotSpeeds(spec)) {
  controls.throttle = 0;
  controls.elevator = 0;
  controls.aileron = 0;
  controls.rudder = 0;
  controls.flaps = 1.0 / 3;
  controls.trim = 0;
  controls.brake = 0;
  controls.parkingBrake = false;
  controls.engineOn = true;
  double drop = 0;
  for (const GearLeg& g : spec.gear) drop = std::max(drop, -g.y + g.radius - 0.4 * g.travel);
  wheelDrop = drop;
  noseWheel = -1;
  for (size_t i = 0; i < spec.gear.size(); i++)
    if (spec.gear[i].steers) {
      noseWheel = static_cast<int>(i);
      break;
    }
}

// ---------------------------------------------------------------------------
// what it is told

void FlightAutopilot::off() {
  mode = "off";
  phase = "off";
  publish();
}

void FlightAutopilot::takeoff(const RunwayJson& runway, bool fromSecond, double altitude) {
  begin("takeoff");
  rwy = frame(runway, fromSecond);
  holdAlt = altitude;
}

void FlightAutopilot::hold(double heading, double altitude, double speed) {
  begin("hold");
  holdCourse = norm360(heading);
  holdAlt = altitude;
  holdSpeed = speed;
}

void FlightAutopilot::goto_(double x, double z, double altitude, const GotoOptions& opts) {
  begin("goto");
  cx = x;
  cz = z;
  holdAlt = altitude;
  holdSpeed = opts.speed.value_or(NaN);
  orbitR = std::max(300.0, opts.radius.value_or(AUTOPILOT.orbit));
  orbitDir = opts.right ? 1 : -1;
}

void FlightAutopilot::land(const RunwayJson& runway, bool fromSecond) {
  begin("land");
  rwy = frame(runway, fromSecond);
  landing = true;
}

void FlightAutopilot::demo(const RunwayJson& runway, bool fromSecond, const std::vector<AutopilotWaypoint>& route) {
  begin("demo");
  rwy = frame(runway, fromSecond);
  landing = true;
  this->route = route;
}

void FlightAutopilot::takeOver(const TakeOverControls& c) {
  handThrottle = c.throttle;
  handTrim = c.trim;
}

void FlightAutopilot::begin(const AutopilotMode& mode) {
  this->mode = mode;
  fresh = true;
  handThrottle = handTrim = NaN;
  rwy.reset();
  landing = false;
  route.clear();
  routeIdx = 0;
  legs.clear();
  leg = 0;
  holdAlt = NaN;
  holdSpeed = NaN;
  attempt = 0;
  goArounds = 0;
  gaveUp = false;
  rejected = false;
  gateDone = false;
  elevAt = -1;
  terrainAt = -1;
  terrainFloor = -Inf;
  dRem = NaN;
}

// ---------------------------------------------------------------------------
// every step

FlightControls& FlightAutopilot::update(Aircraft& a, double dt, Ground& ground) {
  FlightControls& c = controls;
  if (!(dt > 0) || mode == "off") return c;
  if (dt > 0.1) dt = 0.1;
  t += dt;
  phaseT += dt;
  flapT += dt;
  if (a.crashed) {
    setPhase("crashed");
    throttle = 0;
    c.throttle = 0;
    c.elevator = c.aileron = c.rudder = 0;
    c.brake = 0;
    publish();
    return c;
  }
  airT = a.onGround() ? 0 : airT + dt;
  track = a.groundSpeed() > 1 ? compass(a.vel.x, a.vel.z) : a.heading();
  posX = a.pos.x;
  posZ = a.pos.z;
  if (fresh) start(a, ground);
  if (rwy && t >= elevAt) {
    // (far from the field its ground may not be known yet: look again now and then)
    Rwy& w = *rwy;
    w.elev = groundY(ground, w.tx + w.ux * w.aim, w.tz + w.uz * w.aim);
    elevAt = t + 2;
  }
  guide(a, ground);
  loops(a, dt);
  publish();
  return c;
}

void FlightAutopilot::fly(Aircraft& a, double dt, Ground& ground) {
  if (!(dt > 0)) return;
  const double n = std::ceil(dt * 30 - 1e-9);
  const double h = dt / n;
  for (int i = 0; i < n; i++) a.step(h, update(a, h, ground), ground);
}

/** The first step of a task: take over from where the aeroplane is. */
void FlightAutopilot::start(Aircraft& a, Ground& ground) {
  fresh = false;
  if (rwy) {
    Rwy& w = *rwy;
    w.elev = groundY(ground, w.tx + w.ux * w.aim, w.tz + w.uz * w.aim);
    elevAt = t + 2;
  }
  const double flap = spec.flapsMax > 0 ? clamp(a.flapsDeg() / spec.flapsMax, 0, 1) : 0;
  notch = static_cast<int>(clamp(jsRound(flap * 3), 0, 3));
  flapT = 0;
  bankCmd = a.bank();
  ailI = 0;
  pitchCmd = a.pitch();
  pitchI = a.pitch();
  vsCmd = a.verticalSpeed();
  // (as if from the ground: whatever the first mode in the air is, it begins from the attitude the aeroplane has)
  vert = VertMode::ground;
  climbing = false;
  climbSpeedReached = false;
  thrFixed = true;
  thrI = 0;
  turnDir = 0;
  lf.clear();
  ffClimb = (a.mass * G) / (0.7 * a.power * 735.5);
  // the throttle and the trim: the pilot's, else those of level flight as it flies now (or of the take-off)
  if (!a.onGround()) lf.get(a, std::max(a.ias(), 1.2 * spec.vs1), flap, t);
  else lf.get(a, spec.vy, 1.0 / 3, t);
  throttle = std::isfinite(handThrottle) ? clamp(handThrottle, 0, 1) : a.onGround() ? 0 : lf.throttle;
  trim = std::isfinite(handTrim) ? clamp(handTrim, -1, 1) : lf.trim;
  if (mode == "takeoff") {
    if (a.onGround() && rwy) return lineupFrom(a, ground);
    // already flying: on along the runway's direction at the height it is at
    mode = "hold";
    holdCourse = rwy ? rwy->course : a.heading();
    if (!std::isfinite(holdAlt)) holdAlt = a.altitude();
    setPhase("cruise");
    return;
  }
  if (mode == "demo") {
    if (a.onGround() && rwy) return lineupFrom(a, ground);
    attempt = 1;
    if (!route.empty()) routeLegs(a);
    else joinCircuit(a);
    return;
  }
  if (mode == "land") {
    if (a.onGround()) setPhase("rollout");
    else joinCircuit(a);
    return;
  }
  if (mode == "hold") {
    if (!std::isfinite(holdAlt)) holdAlt = a.altitude();
    setPhase(a.onGround() ? "stopped" : "cruise");
    return;
  }
  if (mode == "goto") {
    setPhase(a.onGround() ? "stopped" : "enroute");
    return;
  }
}

/** On the runway: the take-off begins here – the height to climb to counts from where the aeroplane stands. */
void FlightAutopilot::lineupFrom(Aircraft& a, Ground& ground) {
  if (!std::isfinite(holdAlt) && mode == "takeoff") holdAlt = groundY(ground, a.pos.x, a.pos.z) + wheelDrop + AUTOPILOT.departHeight;
  setPhase("lineup");
}

void FlightAutopilot::setPhase(const AutopilotPhase& p) {
  if (phase != p) phaseT = 0;
  phase = p;
}

// ---------------------------------------------------------------------------
// guidance: what to fly this step

void FlightAutopilot::guide(Aircraft& a, Ground& ground) {
  tg.lat = LatMode::wings;
  tg.course = a.heading();
  tg.bankMax = AUTOPILOT.bankMax;
  tg.bankFF = 0;
  tg.vert = VertMode::ground;
  tg.pitch = 0;
  tg.vs = 0;
  tg.speed = speeds.cruise;
  tg.throttle = 0;
  tg.brake = 0;
  tg.park = false;
  tg.altitude = NaN;
  dRem = NaN;
  const std::string& p = phase;
  if (p == "lineup") return lineup(a);
  if (p == "takeoffRoll") return takeoffRoll(a);
  if (p == "rotate") return rotate(a);
  if (p == "rejected" || p == "rollout") return rollout(a);
  if (p == "stopped") return stopped(a);
  if (p == "initialClimb" || p == "goAround") return climbOut(a);
  if (p == "climb") return departureClimb(a, ground);
  if (p == "cruise") return cruise(a, ground);
  if (p == "enroute" || p == "orbit") return circle(a, ground);
  if (p == "flare") return flare(a);
  if (p == "route" || p == "upwind" || p == "crosswind" || p == "downwind" || p == "base" || p == "final") return followPlan(a, ground);
}

/** Flaps to 10°, engine running, on the brakes – then off. */
void FlightAutopilot::lineup(Aircraft& a) {
  notch = 1;
  onRunway(a);
  tg.brake = 1;
  const bool set = std::abs(a.flapsDeg() - spec.flapsMax / 3) < 0.5;
  if (set && a.engineRunning() && phaseT > 1) setPhase("takeoffRoll");
}

/** Full power along the centre line; rotate at vr – or stop while the runway left is still enough to. */
void FlightAutopilot::takeoffRoll(Aircraft& a) {
  const Rwy& w = *rwy;
  onRunway(a);
  tg.throttle = 1;
  const double left = w.len - alongW(w, a.pos.x, a.pos.z);
  const double gs = a.groundSpeed();
  // stopping from here takes about v²/(2·0.3 g) on the brakes
  if (a.ias() < spec.vr - 1 && left < (gs * gs) / (2 * 0.3 * G) + 40) {
    rejected = true;
    setPhase("rejected");
    return rollout(a);
  }
  if (a.ias() >= spec.vr) {
    setPhase("rotate");
    rotateFrom = a.pitch();
  }
}

/** Raise the nose firmly to the take-off attitude and let it fly off. */
void FlightAutopilot::rotate(Aircraft& a) {
  if (a.onGround()) onRunway(a);
  else alongRunway(a, AUTOPILOT.bankFlare);
  tg.throttle = 1;
  tg.vert = VertMode::pitch;
  tg.pitch = std::min(AUTOPILOT.rotatePitch, rotateFrom + AUTOPILOT.rotateRate * phaseT);
  if (airT > 0.5) {
    climbSpeedReached = false;
    setPhase("initialClimb");
  }
}

/**
 * After lift-off and in a go-around: full power, a gentle climb until the
 * climb speed is there, then the nose for the speed; the flaps come up a
 * step at a time; straight ahead along the runway.
 */
void FlightAutopilot::climbOut(Aircraft& a) {
  const AutopilotConstants& A = AUTOPILOT;
  const Rwy& w = *rwy;
  const double h = heightAboveField(a);
  alongRunway(a, h < A.low ? A.bankLow : A.bankMax);
  tg.throttle = 1;
  const bool ga = phase == "goAround";
  // flaps: in a go-around 20° at once and 10° once climbing away; none at the flaps-up height
  if (ga && notch > 2) setNotch(2);
  if (ga && notch > 1 && h > 50 && a.verticalSpeed() > 0.5 && flapT > 2) setNotch(1);
  if (notch > 0 && h > A.flapsUp && a.ias() > spec.vy - 4 && flapT > 2) setNotch(0);
  const AutopilotSpeeds& sp = speeds;
  const double v = notch >= 2 ? sp.flaps20 : notch == 1 ? sp.climb - 2 : sp.climb;
  if (!climbSpeedReached && a.ias() >= v - 4) climbSpeedReached = true;
  vertical = "climb";
  tg.speed = v;
  if (climbSpeedReached) tg.vert = VertMode::speed;
  else {
    // climbing a little more the more speed there is to spare – a weak engine needs its power to accelerate first
    const double flap = spec.flapsMax > 0 ? clamp(a.flapsDeg() / spec.flapsMax, 0, 1) : 0;
    const double vmin = 1.2 * (spec.vs1 + (spec.vs0 - spec.vs1) * flap);
    tg.vert = VertMode::vs;
    tg.vs = clamp(0.15 * (a.ias() - vmin), 0.2, A.liftoffClimb + (ga ? 1 : 0));
  }
  tg.altitude = w.elev + wheelDrop + (landing ? A.circuitHeight : A.departHeight);
  if (h > A.departed && notch == 0) {
    if (ga) circuitAgain(a);
    else afterDeparture(a);
  }
}

/** The take-off is over: what comes next. */
void FlightAutopilot::afterDeparture(Aircraft& a) {
  const Rwy& w = *rwy;
  if (mode == "takeoff") {
    legs = {runwayLeg(w, 0, 0, w.len + 100000, 0, LegKind::out)};
    leg = 0;
    setPhase("climb");
    return;
  }
  // the demonstration: the route, else the circuit
  attempt = 1;
  if (!route.empty()) return routeLegs(a);
  side = -1;
  legs = circuit(w, std::max(0.0, alongW(w, a.pos.x, a.pos.z)), side);
  leg = 0;
  setPhase("upwind");
}

/** The legs of the sightseeing route from where the aeroplane is. */
void FlightAutopilot::routeLegs(Aircraft& a) {
  const Rwy& w = *rwy;
  std::vector<Leg> out;
  double x = a.pos.x;
  double z = a.pos.z;
  const double alt0 = w.elev + wheelDrop + AUTOPILOT.circuitHeight;
  for (size_t i = routeIdx; i < route.size(); i++) {
    const AutopilotWaypoint& p = route[i];
    out.push_back(makeLeg(x, z, p.x, p.z, LegKind::route, p.altitude && std::isfinite(*p.altitude) ? *p.altitude : alt0));
    x = p.x;
    z = p.z;
  }
  legs = std::move(out);
  leg = 0;
  setPhase("route");
}

/** After a plain take-off: on along the runway's direction, climbing to the height asked for, then holding it. */
void FlightAutopilot::departureClimb(Aircraft& a, Ground& ground) {
  const Leg l = legs[0];
  tg.lat = LatMode::track;
  tg.course = legCourse(l, a);
  cleanUp(a);
  const double alt = std::max(holdAlt, floor(a, ground));
  holdHeight(a, alt, speeds.cruise);
  if (std::abs(alt - a.altitude()) < 15) {
    mode = "hold";
    holdCourse = l.course;
    holdAlt = alt;
    setPhase("cruise");
  }
}

/** Heading and height hold. */
void FlightAutopilot::cruise(Aircraft& a, Ground& ground) {
  tg.lat = LatMode::heading;
  tg.course = holdCourse;
  cleanUp(a);
  holdHeight(a, std::max(holdAlt, floor(a, ground)), std::isfinite(holdSpeed) ? holdSpeed : speeds.cruise);
}

/** To the place, then round it. */
void FlightAutopilot::circle(Aircraft& a, Ground& ground) {
  const double dx = a.pos.x - cx;
  const double dz = a.pos.z - cz;
  const double r = std::hypot(dx, dz);
  const double gs = std::max(20.0, a.groundSpeed());
  const double L = std::max(200.0, gs * 10);
  const double e = r - orbitR;
  // along the circle with the centre on the inside – and back onto it when off it
  tg.lat = LatMode::track;
  tg.course = norm360(compass(dx, dz) + orbitDir * (90 + std::atan2(e, L) / DEG));
  tg.bankFF = orbitDir * std::atan((gs * gs) / (G * orbitR)) * std::max(0.0, 1 - std::abs(e) / L);
  if (phase == "enroute" && r < orbitR + 150) setPhase("orbit");
  cleanUp(a);
  const double alt = std::max(holdAlt, floor(a, ground));
  holdHeight(a, alt, std::isfinite(holdSpeed) ? holdSpeed : speeds.cruise);
  // come down to land: once at the circuit's height (or as low as the ground around allows) and heading about downwind, join it
  if (landing && rwy && a.altitude() < alt + 30 && std::abs(wrap180(track - rwy->course - 180)) < 60) joinCircuit(a);
}

/** The legs of a route or of the circuit; on the final, the approach. */
void FlightAutopilot::followPlan(Aircraft& a, Ground& ground) {
  const AutopilotConstants& A = AUTOPILOT;
  sequence(a);
  // (the route ended far above the field: it circles down first)
  if (phase == "enroute" || phase == "orbit") return circle(a, ground);
  if (leg < 0 || leg >= static_cast<int>(legs.size())) return;
  const Leg l = legs[leg];
  setPhase(legPhase(l.kind));
  tg.lat = LatMode::track;
  const double gs = std::max(20.0, a.groundSpeed());
  const bool final = l.kind == LegKind::final;
  tg.course = legCourse(l, a, final ? std::max(150.0, gs * 9) : std::max(250.0, gs * 12));
  if (l.kind == LegKind::route) {
    cleanUp(a);
    holdHeight(a, std::max(l.alt, floor(a, ground)), speeds.cruise);
    return;
  }
  // the circuit: its height, and the glide path to the aiming point along the legs that are left
  const Rwy& w = *rwy;
  const double d = remaining(a);
  dRem = d;
  const double base = w.elev + wheelDrop;
  const double top = base + A.circuitHeight;
  const double path = base + std::max(0.0, d) * std::tan(A.glide);
  const AutopilotSpeeds& sp = speeds;
  // flaps and speed by the way left: 10° about abeam the threshold, 20° on base, 30° on the final
  const int want = d > 6000 ? 0 : d > 4000 ? 1 : d > 2800 ? 2 : 3;
  if (want > notch && a.ias() < sp.flapsOut && flapT > 2 && !climbing) setNotch(notch + 1);
  else if (want < notch && flapT > 2 && a.ias() > (notch == 1 ? spec.vy - 4 : sp.flaps10)) setNotch(notch - 1);
  const double h = heightAboveField(a);
  double v = notch == 0 ? sp.circuit : notch == 1 ? sp.flaps10 : notch == 2 ? sp.flaps20 : sp.final;
  if (final && notch == 3) v = sp.ref + (sp.final - sp.ref) * clamp((h - 10) / 30, 0, 1);
  if (path < top) holdHeight(a, path, v, -a.groundSpeed() * std::tan(A.glide), 0.15);
  else holdHeight(a, top, v);
  if (final) approach(a, h, path);
}

/** On the final: the gate at 100 ft, then the flare. */
void FlightAutopilot::approach(Aircraft& a, double h, double path) {
  const AutopilotConstants& A = AUTOPILOT;
  const Rwy& w = *rwy;
  if (h < 60) tg.bankMax = A.bankLow + (A.bankMax - A.bankLow) * clamp((h - A.low) / 30, 0, 1);
  if (!gateDone && h < A.gate) {
    gateDone = true;
    const double off = std::abs(acrossW(w, a.pos.x, a.pos.z));
    const double err = std::abs(wrap180(track - w.course));
    const AutopilotSpeeds& sp = speeds;
    const bool stable = off < 20 && err < 15 && std::abs(a.altitude() - path) < 15 && a.ias() > sp.ref - 4 && a.ias() < sp.final + 8 && notch == 3;
    if (!stable) return goAround(a);
  }
  if (a.agl() < A.flare && h < 2 * A.flare && alongW(w, a.pos.x, a.pos.z) > -400) {
    setPhase("flare");
    flare(a);
  }
}

/** Idle, the sink brought down with the height to some 0.3 m/s at the wheels; a go-around if it floats too far. */
void FlightAutopilot::flare(Aircraft& a) {
  const Rwy& w = *rwy;
  alongRunway(a, AUTOPILOT.bankFlare);
  vertical = "flare";
  tg.vert = VertMode::flare;
  tg.throttle = 0;
  tg.speed = speeds.ref;
  tg.altitude = w.elev + wheelDrop;
  if (mainContact(a)) {
    setPhase("rollout");
    return rollout(a);
  }
  // what is left of the runway must still stop it: about v²/(2·0.25 g) beyond the touchdown, which is some seconds away
  const double left = w.len - alongW(w, a.pos.x, a.pos.z);
  const double gs = a.groundSpeed();
  if (left < (gs * gs) / (2 * 0.25 * G) + gs * 3 + 30 || phaseT > 20) goAround(a);
}

/** On the runway after the touchdown (or stopping a take-off): the nose wheel down, the brakes, the centre line. */
void FlightAutopilot::rollout(Aircraft& a) {
  if (rwy && phase == "rollout" && !a.onGround() && airT > 1 && a.agl() > 2) {
    // bounced high: go round rather than come down hard
    return goAround(a);
  }
  if (rwy) onRunway(a);
  tg.throttle = 0;
  // the nose comes down gently
  tg.vert = VertMode::pitch;
  tg.pitch = std::max(0.0, std::min(pitchCmd, a.pitch()) - 1 * DEG);
  vertical = "ground";
  const bool nose = noseWheel >= 0 ? a.wheels[noseWheel].contact : a.onGround();
  if (nose || phase == "rejected") {
    const double gs = a.groundSpeed();
    const double left = rwy ? rwy->len - alongW(*rwy, a.pos.x, a.pos.z) - 30 : Inf;
    // firmly when the runway gets short, else as a pilot does
    tg.brake = left < (gs * gs) / (2 * 0.25 * G) ? 1 : 0.6;
  }
  if (a.groundSpeed() < 0.3 && a.onGround()) {
    setPhase("stopped");
    stopped(a);
  }
}

void FlightAutopilot::stopped(Aircraft& a) {
  tg.throttle = 0;
  tg.brake = 1;
  tg.park = true;
  notch = 0;
  vertical = "ground";
  if (rwy) onRunway(a);
}

/** Full power, climb away along the runway, then the circuit again. */
void FlightAutopilot::goAround(Aircraft& a) {
  goArounds++;
  attempt++;
  gateDone = false;
  climbSpeedReached = false;
  setPhase("goAround");
  climbOut(a);
}

/** Climbed away from a go-around: another circuit – or, after too many, circle the field. */
void FlightAutopilot::circuitAgain(Aircraft& a) {
  const Rwy& w = *rwy;
  if (attempt > AUTOPILOT.attempts) {
    gaveUp = true;
    landing = false;
    mode = "goto";
    cx = w.tx + w.ux * w.len * 0.5;
    cz = w.tz + w.uz * w.len * 0.5;
    holdAlt = w.elev + wheelDrop + AUTOPILOT.circuitHeight;
    orbitR = AUTOPILOT.orbit;
    orbitDir = side;
    setPhase("enroute");
    return;
  }
  legs = circuit(w, std::max(0.0, alongW(w, a.pos.x, a.pos.z)), side);
  leg = 0;
  setPhase("upwind");
}

/** Join the circuit from wherever the aeroplane is (land mode, the end of the route). */
void FlightAutopilot::joinCircuit(Aircraft& a) {
  const Rwy& w = *rwy;
  const AutopilotConstants& A = AUTOPILOT;
  const double s = alongW(w, a.pos.x, a.pos.z);
  const double v = acrossW(w, a.pos.x, a.pos.z);
  const double F = A.final;
  if (attempt == 0) attempt = 1;
  side = v > 0 ? 1 : -1;
  const double top = w.elev + wheelDrop + A.circuitHeight;
  if (a.altitude() > top + 150) {
    // far too high for the circuit: first down to its height circling beside the field, on the downwind side
    cx = w.tx + w.ux * w.len * 0.5 - w.uz * side * A.downwind;
    cz = w.tz + w.uz * w.len * 0.5 + w.ux * side * A.downwind;
    orbitR = A.orbit;
    orbitDir = side;
    holdAlt = top;
    holdSpeed = NaN;
    setPhase("enroute");
    return;
  }
  // straight in: well out on the approach side, near the extended centre line, low enough for the glide path
  if (s < -F - 1000 && std::abs(v) < 0.4 * -s && heightAboveField(a) < (w.aim - s) * std::tan(A.glide) + 150) {
    legs = {runwayLeg(w, s - 500, 0, w.aim, 0, LegKind::final)};
    leg = 0;
    setPhase("final");
    return;
  }
  // else onto the downwind leg on its side, abeam where it is (within the leg's ends)
  const double D = side * A.downwind;
  const double sj = clamp(s, -F + 1500, w.len + A.upwind);
  legs = {runwayLeg(w, sj, D, -F, D, LegKind::downwind), runwayLeg(w, -F, D, -F, 0, LegKind::base), runwayLeg(w, -F, 0, w.aim, 0, LegKind::final)};
  leg = 0;
  setPhase("downwind");
}

/** The circuit from a point `from` m along the runway: upwind, crosswind, downwind, base, final. */
std::vector<FlightAutopilot::Leg> FlightAutopilot::circuit(const Rwy& w, double from, double side) const {
  const AutopilotConstants& A = AUTOPILOT;
  const double sc = std::max(w.len + A.upwind, from + 1000);
  const double D = side * A.downwind;
  const double F = A.final;
  return {
      runwayLeg(w, 0, 0, sc, 0, LegKind::upwind),
      runwayLeg(w, sc, 0, sc, D, LegKind::crosswind),
      runwayLeg(w, sc, D, -F, D, LegKind::downwind),
      runwayLeg(w, -F, D, -F, 0, LegKind::base),
      runwayLeg(w, -F, 0, w.aim, 0, LegKind::final),
  };
}

/** On to the next leg once the turn onto it has to begin; at the end of the route, join the circuit. */
void FlightAutopilot::sequence(Aircraft& a) {
  const double gs = std::max(20.0, a.groundSpeed());
  const double r = (gs * gs) / (G * std::tan(AUTOPILOT.bankPlan));
  for (int guard = 0; guard < 16; guard++) {
    if (leg < 0 || leg >= static_cast<int>(legs.size())) return;
    const Leg l = legs[leg];
    if (l.kind == LegKind::final) return;
    const double s = (a.pos.x - l.ax) * l.ux + (a.pos.z - l.az) * l.uz;
    const bool hasNext = leg + 1 < static_cast<int>(legs.size());
    double lead;
    // the sights of a route are flown over; the corners of the circuit are cut by the turn
    if (l.kind == LegKind::route) lead = 100;
    else if (hasNext) {
      const double turn = std::abs(wrap180(legs[leg + 1].course - l.course)) * DEG;
      lead = std::min(r * std::tan(std::min(turn, 2.4) / 2), 2 * r);
    } else lead = std::min(r, 300.0);
    if (s < l.len - lead) return;
    if (l.kind == LegKind::route) routeIdx++;
    if (hasNext) {
      leg++;
      continue;
    }
    // the last waypoint of the route is behind: to the field
    joinCircuit(a);
    return;
  }
}

/** Metres left along the plan to the aiming point. */
double FlightAutopilot::remaining(Aircraft& a) const {
  if (leg < 0 || leg >= static_cast<int>(legs.size())) return NaN;
  const Leg& l = legs[leg];
  const double s = (a.pos.x - l.ax) * l.ux + (a.pos.z - l.az) * l.uz;
  double d = l.len - (l.kind == LegKind::final ? s : clamp(s, 0, l.len));
  for (size_t i = leg + 1; i < legs.size(); i++) d += legs[i].len;
  return d;
}

// ---- helpers of the guidance

/** The course that flies the aeroplane onto the leg's line: towards a point on it `look` m ahead. */
double FlightAutopilot::legCourse(const Leg& l, Aircraft& a, double look) const {
  if (std::isnan(look)) look = std::max(250.0, a.groundSpeed() * 12);
  const double dx = a.pos.x - l.ax;
  const double dz = a.pos.z - l.az;
  const double d = -dx * l.uz + dz * l.ux;
  return norm360(l.course - std::atan2(d, look) / DEG);
}

/** Steering on the runway: the nose wheel along the centre line, wings level. */
void FlightAutopilot::onRunway(Aircraft& a) {
  const Rwy& w = *rwy;
  tg.lat = LatMode::ground;
  tg.course = norm360(w.course - std::atan2(acrossW(w, a.pos.x, a.pos.z), std::max(20.0, 2 * a.groundSpeed())) / DEG);
  tg.vert = VertMode::ground;
  tg.pitch = 0;
  vertical = "ground";
}

/** In the air close to the runway: along its centre line (extended), with little bank. */
void FlightAutopilot::alongRunway(Aircraft& a, double bankMax) {
  const Rwy& w = *rwy;
  tg.lat = LatMode::track;
  tg.course = norm360(w.course - std::atan2(acrossW(w, a.pos.x, a.pos.z), std::max(100.0, a.groundSpeed() * 8)) / DEG);
  tg.bankMax = bankMax;
}

void FlightAutopilot::setNotch(int n) {
  notch = std::clamp(n, 0, 3);
  flapT = 0;
}

/** Flaps up a step at a time once fast enough (cruise, circling, the route). */
void FlightAutopilot::cleanUp(Aircraft& a) {
  if (notch > 0 && a.ias() > 1.35 * spec.vs1 && flapT > 2) setNotch(notch - 1);
}

/**
 * The height: a climb at full power and climb speed to a target far
 * above, else the nose for a vertical speed – `vsFF` (the glide path's) and
 * what brings it to the target – and the throttle for the speed.
 */
void FlightAutopilot::holdHeight(Aircraft& a, double alt, double speed, double vsFF, double gain) {
  const AutopilotConstants& A = AUTOPILOT;
  const double err = alt - a.altitude();
  if (climbing) {
    if (err < A.climbEnd) climbing = false;
  } else if (err > A.climbStart) climbing = true;
  tg.altitude = alt;
  if (climbing) {
    vertical = "climb";
    tg.vert = VertMode::speed;
    tg.speed = speeds.climb;
    tg.throttle = 1;
    return;
  }
  vertical = vsFF < 0 ? "glidePath" : err < -A.climbEnd ? "descend" : "hold";
  tg.vert = VertMode::vs;
  tg.vs = vsFF < 0 ? clamp(vsFF + clamp(gain * err, -2.5, 2), -5, 2) : clamp(gain * err, -A.vsDown, A.vsUp);
  tg.speed = speed;
  tg.throttle = -1;
}

/** The lowest height en route: some clearance above the highest ground under the way ahead (looked at twice a second). */
double FlightAutopilot::floor(Aircraft& a, Ground& ground) {
  if (t >= terrainAt) {
    terrainAt = t + 0.5;
    const AutopilotConstants& A = AUTOPILOT;
    const double tx = std::sin(track * DEG);
    const double tz = -std::cos(track * DEG);
    double top = -Inf;
    for (double d = 0; d <= A.lookAhead; d += 500) top = std::max(top, groundY(ground, a.pos.x + tx * d, a.pos.z + tz * d));
    terrainFloor = top + wheelDrop + A.clearance;
  }
  return terrainFloor;
}

double FlightAutopilot::heightAboveField(Aircraft& a) const {
  return rwy && std::isfinite(rwy->elev) ? a.altitude() - wheelDrop - rwy->elev : a.agl();
}

bool FlightAutopilot::mainContact(Aircraft& a) const {
  for (size_t i = 0; i < a.wheels.size(); i++)
    if (static_cast<int>(i) != noseWheel && a.wheels[i].contact) return true;
  return false;
}

// ---------------------------------------------------------------------------
// the loops: targets → controls

void FlightAutopilot::loops(Aircraft& a, double dt) {
  const AutopilotConstants& A = AUTOPILOT;
  FlightControls& c = controls;
  const double flap = spec.flapsMax > 0 ? clamp(a.flapsDeg() / spec.flapsMax, 0, 1) : 0;
  const double ias = a.ias();
  const double p = -a.omega.z;
  const double q = a.omega.x;

  // ---- sideways
  if (tg.lat == LatMode::ground) {
    // the rudder steers the nose wheel to the heading that leads back onto the centre line; the wings level
    const double e = wrap180(tg.course - a.heading());
    c.rudder = clamp(0.25 * e - 2 * a.turnRate(), -1, 1);
    bankCmd = 0;
    turnDir = 0;
    c.aileron = clamp(2 * (0 - a.bank()) - 0.4 * p, -1, 1);
  } else if (tg.lat == LatMode::wings) {
    bankCmd = 0;
    c.aileron = clamp(2 * (0 - a.bank()) - 0.4 * p, -1, 1);
    c.rudder = rudderFor(a, c.aileron);
  } else {
    const double now = tg.lat == LatMode::heading || a.groundSpeed() < 15 ? a.heading() : track;
    double e = wrap180(tg.course - now);
    // the shorter way round – but a turn of about half a circle, once begun, is kept
    if (std::abs(e) > 150 && turnDir != 0 && sign(e) != turnDir) e -= 360 * sign(e);
    turnDir = std::abs(e) > 10 ? sign(e) : 0;
    // (a degree short of the limit, so that what the bank overshoots by stays within it; eased in at the end of a roll)
    const double most = std::max(0.0, tg.bankMax - 1 * DEG);
    const double want = clamp(1.1 * e * DEG + tg.bankFF, -most, most);
    const double step = A.rollRate * dt;
    bankCmd = clamp(bankCmd + clamp((want - bankCmd) * 1.5 * dt, -step, step), -most, most);
    // (the integral only once the bank has arrived – while it rolls in, it would wind up and overshoot)
    const double be = bankCmd - a.bank();
    if (std::abs(be) < 2 * DEG && std::abs(want - bankCmd) < 2 * DEG) ailI = clamp(ailI + 2 * be * dt, -0.2, 0.2);
    c.aileron = aileronFor(a, bankCmd, ailI);
    // the ball in the middle
    c.rudder = rudderFor(a, c.aileron);
  }

  // ---- up and down: the pitch attitude
  const VertMode prev = vert;
  const VertMode mode = tg.vert;
  double cmd;
  if (mode == VertMode::ground || mode == VertMode::pitch) {
    cmd = tg.pitch;
  } else if (mode == VertMode::flare) {
    if (prev != VertMode::flare) flarePitch = pitchCmd;
    const double e = -std::max(0.3, 0.3 * a.agl()) - a.verticalSpeed();
    flarePitch = clamp(flarePitch + 0.03 * e * dt, -5 * DEG, 12 * DEG);
    cmd = flarePitch + 0.04 * e;
  } else if (mode == VertMode::speed) {
    // raise the nose when too fast
    const double e = ias - tg.speed;
    const double pTerm = 0.02 * clamp(e, -10, 10);
    if (prev != VertMode::speed) pitchI = pitchCmd - pTerm;
    pitchI += 0.003 * e * dt;
    cmd = pitchI + pTerm;
  } else {
    // the path: the vertical speed asked for, within what the airspeed allows
    const double vmin = 1.2 * (spec.vs1 + (spec.vs0 - spec.vs1) * flap);
    double vs = std::min(tg.vs, 0.5 * (ias - vmin));
    vs = std::max(vs, -0.5 * (0.85 * spec.vne - ias));
    if (prev != VertMode::vs) vsCmd = a.verticalSpeed();
    const double step = A.vsRate * dt;
    vsCmd += clamp(vs - vsCmd, -step, step);
    // the attitude for it: the path's angle plus the angle of attack that carries the weight (more in a bank)
    const double n = 1 / std::max(0.5, std::cos(a.bank()));
    const double ff = alphaFor(liftNeeded(a, ias, n), flap) + std::asin(clamp(vsCmd / std::max(15.0, a.tas()), -0.5, 0.5));
    const double e = vsCmd - a.verticalSpeed();
    const double pTerm = 0.012 * clamp(e, -5, 5);
    if (prev != VertMode::vs) pitchI = pitchCmd - pTerm - ff;
    pitchI = clamp(pitchI + 0.005 * e * dt, -10 * DEG, 10 * DEG);
    cmd = ff + pTerm + pitchI;
  }
  // limits: the attitude, and the angle of attack some degrees below the stall
  const double lim = mode == VertMode::ground ? cmd : protectPitch(a, cmd, A.pitchMin, A.pitchMax, A.aoaMargin);
  if (lim != cmd && (mode == VertMode::speed || mode == VertMode::vs)) pitchI += lim - cmd;
  pitchCmd = lim;
  vert = mode;
  if (phase == "rotate") c.elevator = clamp(4 * (lim - a.pitch()) - 1.5 * q, -1, 1);
  else {
    // (the turn's own pitch rate is not to be damped)
    const double qTurn = a.turnRate() * std::sin(a.bank());
    c.elevator = clamp(2.5 * (lim - a.pitch()) - (q - qTurn), -1, 1);
  }

  // ---- power
  double thr;
  if (tg.throttle >= 0) {
    thr = tg.throttle;
    thrFixed = true;
  } else {
    lf.get(a, tg.speed, flap, t);
    const double e = tg.speed - ias;
    const double ff = lf.throttle + ffClimb * vsCmd;
    const double pTerm = 0.06 * clamp(e, -10, 10);
    if (thrFixed) thrI = clamp(throttle - ff - pTerm, -0.5, 0.5);
    thrFixed = false;
    const double raw = ff + pTerm + thrI;
    // (no winding up against the stops)
    if ((raw < 1 || e < 0) && (raw > 0 || e > 0)) thrI = clamp(thrI + 0.012 * e * dt, -0.5, 0.5);
    thr = clamp(raw, 0, 1);
  }
  const double ts = A.throttleRate * dt;
  throttle += clamp(thr - throttle, -ts, ts);
  c.throttle = throttle;

  // ---- trim: what level flight at the target speed needs (kept on the runway and in the flare)
  if (!a.onGround() && mode != VertMode::flare) {
    lf.get(a, mode == VertMode::speed || mode == VertMode::vs ? tg.speed : std::max(ias, spec.vs1), flap, t);
    const double rs = A.trimRate * dt;
    trim += clamp(lf.trim - trim, -rs, rs);
  }
  c.trim = trim;

  c.flaps = notch / 3.0;
  c.brake = tg.brake;
  c.brakeLeft = 0;
  c.brakeRight = 0;
  c.parkingBrake = tg.park;
  c.engineOn = true;
}

// ---------------------------------------------------------------------------
// what it tells

void FlightAutopilot::publish() {
  AutopilotState& s = state;
  s.mode = mode;
  s.phase = phase;
  s.vertical = vertical;
  s.targetHeading = norm360(tg.course);
  s.targetAltitude = tg.altitude;
  s.targetSpeed = tg.vert == VertMode::ground || tg.vert == VertMode::pitch ? NaN : tg.speed;
  s.flapNotch = notch;
  s.runway = rwy ? rwy->name : "";
  s.attempt = attempt;
  s.goArounds = goArounds;
  s.done = phase == "stopped" && rwy.has_value();
  s.rejected = rejected;
  s.gaveUp = gaveUp;
  s.routeCount = static_cast<int>(route.size());
  s.routeIndex = std::min(routeIdx, static_cast<int>(route.size()));
  s.waypoint.reset();
  s.distance = NaN;
  if (phase == "enroute" || phase == "orbit") {
    wp.x = cx;
    wp.z = cz;
    s.waypoint = wp;
    s.distance = std::hypot(cx - posX, cz - posZ);
  } else if (rwy && std::isfinite(dRem)) {
    const Rwy& w = *rwy;
    wp.x = w.tx + w.ux * w.aim;
    wp.z = w.tz + w.uz * w.aim;
    s.waypoint = wp;
    s.distance = dRem;
  } else if (phase == "route") {
    if (leg >= 0 && leg < static_cast<int>(legs.size())) {
      const Leg& l = legs[leg];
      wp.x = l.bx;
      wp.z = l.bz;
      s.waypoint = wp;
      s.distance = std::hypot(l.bx - posX, l.bz - posZ);
    }
  }
}

// ---------------------------------------------------------------------------
// which runway

std::optional<RunwayAt> runwayAt(const std::vector<AirfieldJson>& fields, double x, double z, double yaw) {
  const double fx = -std::sin(yaw);
  const double fz = -std::cos(yaw);
  std::optional<RunwayAt> best;
  double score = Inf;
  for (const AirfieldJson& f : fields) {
    for (const RunwayJson& r : f.runways) {
      const double len = std::hypot(r.x2 - r.x1, r.z2 - r.z1);
      if (len < 1) continue;
      const double ux = (r.x2 - r.x1) / len;
      const double uz = (r.z2 - r.z1) / len;
      const double dx = x - r.x1;
      const double dz = z - r.z1;
      const double s = dx * ux + dz * uz;
      const double v = -dx * uz + dz * ux;
      if (s < -30 || s > len + 30 || std::abs(v) > r.w / 2 + 10) continue;
      const double dot = fx * ux + fz * uz;
      // the one it is lined up with
      const double k = std::abs(v) + 100 * (1 - std::abs(dot));
      if (k < score) {
        score = k;
        best = RunwayAt{&f, &r, dot < 0};
      }
    }
  }
  return best;
}

std::optional<LandingRunway> landingRunway(const std::vector<AirfieldJson>& fields, double x, double z, double minLength) {
  std::optional<LandingRunway> best;
  for (const AirfieldJson& f : fields) {
    const RunwayJson* rw = nullptr;
    double longest = 0;
    for (const RunwayJson& r : f.runways) {
      const double len = std::hypot(r.x2 - r.x1, r.z2 - r.z1);
      if (len >= minLength && len > longest) {
        longest = len;
        rw = &r;
      }
    }
    if (!rw) continue;
    const double mx = (rw->x1 + rw->x2) / 2;
    const double mz = (rw->z1 + rw->z2) / 2;
    const double d = std::hypot(x - mx, z - mz);
    if (best && d >= best->distance) continue;
    // nearer the first end: land from it (towards the second)
    const double toFirst = (x - mx) * (rw->x1 - mx) + (z - mz) * (rw->z1 - mz);
    best = LandingRunway{&f, rw, toFirst < 0, d};
  }
  return best;
}

// ---------------------------------------------------------------------------
// the assist

FlightAssist::FlightAssist(const AssistOptions& options, const AircraftGeometry& spec) : options(options), spec(spec) {}

void FlightAssist::reset(double trim) {
  sPitch = sRoll = sYaw = 0;
  pitchRef = NaN;
  bankRef = 0;
  ailI = 0;
  this->trim = trim;
  lastIas = NaN;
  iasRate = 0;
  lf.clear();
}

void FlightAssist::apply(Aircraft& a, const StickInput& stick, double dt, FlightControls& out) {
  if (!(dt > 0)) return;
  if (dt > 0.1) dt = 0.1;
  t += dt;
  const AssistOptions& o = options;
  const AssistConstants& S = ASSIST;
  // ---- a softer stick: an expo curve, and keys that move it over some tenths of a second
  const double k = 0.6 * clamp(o.soften, 0, 1);
  const double rate = (8 - 6 * clamp(o.soften, 0, 1)) * dt;
  sPitch += clamp(expo(stick.pitch, k) - sPitch, -rate, rate);
  sRoll += clamp(expo(stick.roll, k) - sRoll, -rate, rate);
  sYaw += clamp(expo(stick.yaw, k) - sYaw, -rate, rate);
  if (!std::isfinite(trim)) trim = clamp(std::isfinite(out.trim) ? out.trim : 0, -1, 1);
  const double p = -a.omega.z;
  const double q = a.omega.x;

  if (a.onGround() || a.crashed) {
    // on the runway: the stick as the game maps it (the nose wheel follows it), nothing held
    out.elevator = sPitch;
    out.aileron = sRoll;
    out.rudder = clamp(sYaw + sRoll * 0.9, -1, 1);
    out.trim = trim;
    pitchRef = NaN;
    bankRef = a.bank();
    return;
  }
  const double flap = spec.flapsMax > 0 ? clamp(a.flapsDeg() / spec.flapsMax, 0, 1) : 0;
  const double ias = a.ias();
  // how fast the airspeed changes (m/s², smoothed over half a second)
  if (std::isfinite(lastIas)) iasRate += ((ias - lastIas) / dt - iasRate) * std::min(1.0, dt / 0.5);
  lastIas = ias;
  double elevator;
  double aileron;
  if (o.stabilize) {
    if (std::isnan(pitchRef)) {
      pitchRef = clamp(a.pitch(), S.pitchDown, S.pitchUp);
      bankRef = clamp(a.bank(), -S.bank, S.bank);
    }
    // roll: the stick rolls at up to 30°/s; let go, the wings come level
    const bool rolling = std::abs(sRoll) > 0.02;
    if (rolling) bankRef = clamp(bankRef + sRoll * S.rollRate * dt, -S.bank, S.bank);
    else bankRef -= clamp(bankRef, -S.levelRate * dt, S.levelRate * dt);
    const double be = bankRef - a.bank();
    if (!rolling && std::abs(be) < 2 * DEG) ailI = clamp(ailI + 2 * be * dt, -0.2, 0.2);
    aileron = aileronFor(a, bankRef, ailI);
    // pitch: the stick moves the attitude; let go, it stays – a little more nose up in a bank, to keep the height
    if (std::abs(sPitch) > 0.02) pitchRef = clamp(pitchRef + sPitch * S.pitchRate * dt, S.pitchDown, S.pitchUp);
    const double n = 1 / std::max(0.5, std::cos(a.bank()));
    const double turn = n > 1.001 ? alphaFor(liftNeeded(a, ias, n), flap) - alphaFor(liftNeeded(a, ias, 1), flap) : 0;
    double cmd = pitchRef + turn;
    if (o.protect) {
      cmd = protectPitch(a, cmd, -90 * DEG, 90 * DEG, S.aoaMargin + 1 * DEG);
      // too fast: the nose comes up until the speed is back below the limit
      const double over = ias - S.vMax * spec.vne;
      if (over > -3) cmd = std::max(cmd, a.pitch() + 0.02 * (over + 3));
      // what the protection does is held, not fought when it ends
      if (cmd < pitchRef + turn - 1e-9 || cmd > pitchRef + turn + 1e-9) pitchRef = clamp(cmd - turn, S.pitchDown, S.pitchUp);
    }
    const double qTurn = a.turnRate() * std::sin(a.bank());
    elevator = 2.5 * (cmd - a.pitch()) - (q - qTurn);
  } else {
    // the stick as it is – guarded: above manoeuvring speed with less travel, so that a full stick asks for the same g at any speed
    const double va = spec.vs1 * std::sqrt(S.gMax);
    elevator = sPitch * (o.protect ? std::min(1.0, std::pow(va / std::max(ias, 1.0), 2)) : 1);
    aileron = sRoll;
  }
  if (o.protect) {
    // the angle of attack: no more pull than holds it some degrees below the stall – less still while the nose comes
    // up fast –, a push beyond; the same the other way round
    const double top = AERO.alphaCrit + AERO.flapAlpha * flap - S.aoaMargin;
    const double bottom = AERO.alphaCritNeg + S.aoaMargin;
    elevator = std::min(elevator, -20 * (a.aoa() - top) - q);
    elevator = std::max(elevator, -20 * (a.aoa() - bottom) - q);
    // the attitude: not beyond 30° up or down – and the faster it flies, the shallower the dive it allows
    const double vLim = S.vMax * spec.vne;
    const double dive = -S.pitchMax + (S.pitchMax - 10 * DEG) * clamp((ias - (vLim - 15)) / 15, 0, 1);
    elevator = std::min(elevator, 2.5 * (S.pitchMax - a.pitch()) - q);
    elevator = std::max(elevator, 2.5 * (dive - a.pitch()) - q);
    // the airframe: the load factor and the speed
    if (a.loadFactor() > S.gMax) elevator -= 0.5 * (a.loadFactor() - S.gMax);
    if (a.loadFactor() < S.gMin) elevator += 0.5 * (S.gMin - a.loadFactor());
    // nearing the speed limit (looked at 2 s ahead) the stick's push fades out over the last 10 m/s; beyond it the nose comes up
    const double over = ias + 2 * std::max(0.0, iasRate) - vLim;
    if (over > -10) elevator = std::max(elevator, over < 0 ? over / 10 : 0.06 * over);
    // bank: pushed back beyond 60°
    const double b = std::abs(a.bank()) - S.bankMax;
    if (b > 0) aileron -= sign(a.bank()) * (2 * b + 0.3 * std::max(0.0, sign(a.bank()) * p));
  }
  out.elevator = clamp(elevator, -1, 1);
  out.aileron = clamp(aileron, -1, 1);
  // the rudder: the pedals, and the ball kept in the middle (else a little rudder with the ailerons, as the game does)
  out.rudder = clamp(sYaw + (o.coordinate ? rudderFor(a, out.aileron) : 0.25 * sRoll), -1, 1);
  // the trim: what level flight at the speed flown needs – but never for less than 1.3 times the stall speed, so that
  // a slow aeroplane still wants to lower its nose
  if (o.autoTrim || o.stabilize) {
    lf.get(a, std::max(ias, 1.3 * (spec.vs1 + (spec.vs0 - spec.vs1) * flap)), flap, t);
    const double rs = 0.3 * dt;
    trim += clamp(lf.trim - trim, -rs, rs);
  } else trim = clamp(std::isfinite(out.trim) ? out.trim : trim, -1, 1);
  out.trim = trim;
}

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/flight.ts @ v1.7.7
#include "sim/flight.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fs {

namespace {

constexpr double G = 9.81;
constexpr double RHO0 = 1.225;
constexpr double DEG = M_PI / 180;
constexpr double RPM = 30 / M_PI;
constexpr double TWO_PI = 2 * M_PI;
/** longest internal integration step (s) */
constexpr double H_MAX = 1.0 / 240;

double clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

/** Clamp a control input; anything that is not a number counts as 0. */
double input(double v, double lo, double hi) { return v > lo ? (v < hi ? v : hi) : v <= lo ? lo : 0; }
double input(const std::optional<double>& v, double lo, double hi) { return v ? input(*v, lo, hi) : 0; }

double smooth(double t) { return t <= 0 ? 0 : t >= 1 ? 1 : t * t * (3 - 2 * t); }

/** Lift of the flaps (0…1 of their travel): most of it comes with the first half. */
double flapLift(double f) { return f * (1.35 - 0.35 * f); }

/** Drag coefficient for a lift coefficient at an angle of attack (flaps 0…1, `phi` the ground effect on the induced drag). */
double dragCoefficient(double cl, double alpha, double f, double kInd, double phi, double stall, double sep) {
  const auto& A = AERO;
  const double base = A.CD0 + A.CDgear;
  const double attached = base + A.flapCD * f * std::sqrt(f) + kInd * phi * cl * cl + A.stallCD * stall;
  const double s = std::sin(alpha);
  return attached + (base + A.flatCD * s * s - attached) * sep;
}

/**
 * Points of the airframe that can touch the ground, and what it means when
 * they do: a wing tip, a stabiliser tip or the propeller ('wing'); the tail
 * (a scrape, or an 'impact' when hard); belly and cowling (an 'impact' when
 * moving); fin and cabin roof (turned over: 'noseOver').
 */
constexpr uint8_t HULL_WING = 0;
constexpr uint8_t HULL_TAIL = 1;
constexpr uint8_t HULL_BELLY = 2;
constexpr uint8_t HULL_TOP = 3;

GroundSample smp{0, 0, 1, 0, 1, 1};

using Quat = std::array<double, 4>;

/** out = a · b (quaternions [x, y, z, w]); out may be a or b. */
void quatMul(const Quat& a, const Quat& b, Quat& out) {
  const double x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
  const double y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
  const double z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
  const double w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
  out[0] = x;
  out[1] = y;
  out[2] = z;
  out[3] = w;
}

}  // namespace

const char* crashKindName(CrashKind k) {
  switch (k) {
    case CrashKind::impact: return "impact";
    case CrashKind::wing: return "wing";
    case CrashKind::noseOver: return "noseOver";
    case CrashKind::overspeed: return "overspeed";
  }
  return "impact";
}

double airDensity(double h) {
  const double hh = h > -1000 ? (h < 11000 ? h : 11000) : -1000;
  return RHO0 * std::pow(1 - 2.25577e-5 * hh, 4.2559);
}

LiftCurve liftCurve(double alpha, double f) {
  const auto& A = AERO;
  const double a = A.CLa;
  const double r = A.round;
  const double cl0 = A.CL0 + A.flapCL * flapLift(f);
  const double ac = A.alphaCrit + A.flapAlpha * f;
  const double acn = A.alphaCritNeg;
  double cl;
  double stall = 0;
  double sep = 0;
  if (alpha > ac) {
    const double top = cl0 + a * (ac - r / 2);
    const double x = alpha - ac;
    stall = smooth(x / A.stallDrop);
    sep = smooth((x - 0.07) / 0.2);
    const double held = top * (1 - (1 - A.postStall) * stall);
    cl = held + (A.flatCL * std::sin(2 * alpha) - held) * sep;
  } else if (alpha < acn) {
    const double bottom = cl0 + a * (acn + r / 2);
    const double x = acn - alpha;
    stall = smooth(x / A.stallDrop);
    sep = smooth((x - 0.07) / 0.2);
    const double held = bottom * (1 - (1 - A.postStall) * stall);
    cl = held + (A.flatCL * std::sin(2 * alpha) - held) * sep;
  } else if (alpha > ac - r) {
    const double x = alpha - ac;
    cl = cl0 + a * (ac - r / 2) - (a * x * x) / (2 * r);
  } else if (alpha < acn + r) {
    const double x = alpha - acn;
    cl = cl0 + a * (acn + r / 2) + (a * x * x) / (2 * r);
  } else
    cl = cl0 + a * alpha;
  return {cl, stall, sep};
}

MaxLift maxLift(double f) {
  const double ac = AERO.alphaCrit + AERO.flapAlpha * f;
  return {AERO.CL0 + AERO.flapCL * flapLift(f) + AERO.CLa * (ac - AERO.round / 2), ac};
}

Aircraft::Aircraft(const AircraftGeometry& spec_, std::optional<double> powerPs)
    : spec(spec_), power(powerPs ? *powerPs : spec_.power), fuel(spec_.fuel), mass(spec_.mass) {
  const auto& s = spec;
  // moments of inertia of a trainer of this size, scaled with mass and dimensions
  const double m = s.mass / 1000;
  const double lr = s.length / 8.2;
  const double br = s.span / 10.9;
  inertia = {1800 * m * lr * lr, (2600 * m * (67.24 * lr * lr + 118.81 * br * br)) / 186.05, 1300 * m * br * br};
  kInd = 1 / (M_PI * AERO.oswald * ((s.span * s.span) / s.wingArea));
  // engine: indicated torque so that the rated power is left at the rated rpm
  const double wr = ENGINE.ratedRpm / RPM;
  qInd = (power * 735.5) / wr + ENGINE.friction[0] + ENGINE.friction[1] * wr;
  propScale = power / 160;
  diskArea = (M_PI * s.propDiameter * s.propDiameter) / 4;

  // legs: static loads from the lever arms, springs that carry them at `staticShare` of the travel
  const auto& gear = s.gear;
  const double W = s.mass * G;
  int nose = -1;
  for (int i = 0; i < (int)gear.size(); i++)
    if (gear[i].steers) {
      nose = i;
      break;
    }
  double zm = 0;
  int mains = 0;
  for (int i = 0; i < (int)gear.size(); i++) {
    if (i != nose) {
      zm += gear[i].z;
      mains++;
    }
  }
  zm /= std::max(1, mains);
  const double zn = nose >= 0 ? gear[nose].z : zm - 1;
  const double noseLoad = nose >= 0 ? (W * zm) / (zm - zn) : 0;
  for (int i = 0; i < (int)gear.size(); i++) {
    const auto& g = gear[i];
    const double load = i == nose ? noseLoad : (W - noseLoad) / std::max(1, mains);
    const double k = load / (GEAR.staticShare * g.travel);
    const double mEff = load / G;
    const double kTyre = (GEAR.cornering * load) / GEAR.relax;
    Leg l;
    l.x = g.x;
    l.y = g.y;
    l.z = g.z;
    l.radius = g.radius;
    l.travel = g.travel;
    l.steers = g.steers;
    l.side = i == nose ? 0 : g.x < 0 ? -1 : 1;
    l.load = load;
    l.k = k;
    l.c = GEAR.damping * 2 * std::sqrt(k * mEff);
    l.kTyre = kTyre;
    l.cTyre = 1.4 * std::sqrt(kTyre * mEff);
    legs.push_back(l);
  }
  const size_t n = gear.size();
  zLong.assign(n, 0);
  zLat.assign(n, 0);
  wasDown.assign(n, 0);
  wheelW.assign(n, 0);
  wheels.assign(n, WheelView{});

  // the airframe: wing tips, stabiliser tips, tail, belly and cowling, fin top and cabin roof
  const double tipY = s.wingY + (s.span / 2) * std::tan(s.dihedral * DEG);
  const double hs = s.span / 2;
  // belly: a little above the highest wheel centre
  double maxGearY = -std::numeric_limits<double>::infinity();
  for (const auto& g : gear) maxGearY = std::max(maxGearY, g.y);
  const double bellyY = maxGearY + 0.3;
  const std::vector<std::array<double, 4>> pts = {
      {-hs, tipY, s.wingLE, HULL_WING},
      {-hs, tipY, s.wingTE, HULL_WING},
      {hs, tipY, s.wingLE, HULL_WING},
      {hs, tipY, s.wingTE, HULL_WING},
      {-s.stabSpan / 2, s.stabY, s.stabZ, HULL_WING},
      {s.stabSpan / 2, s.stabY, s.stabZ, HULL_WING},
      {0, s.stabY - 0.4, s.stabZ - 0.4, HULL_TAIL},
      {0, bellyY, 0, HULL_BELLY},
      {0, s.propHub[1] - 0.6, s.propHub[2] + 0.4, HULL_BELLY},
      {0, s.finTop, s.finZ + 0.3, HULL_TOP},
      {-0.6, s.wingY + 0.25, 0, HULL_TOP},
      {0.6, s.wingY + 0.25, 0, HULL_TOP},
  };
  hull.assign(pts.size() * 3, 0);
  hullKind.assign(pts.size(), 0);
  for (size_t i = 0; i < pts.size(); i++) {
    hull[i * 3] = pts[i][0];
    hull[i * 3 + 1] = pts[i][1];
    hull[i * 3 + 2] = pts[i][2];
    hullKind[i] = (uint8_t)pts[i][3];
  }
  double r = std::hypot(s.propHub[0], s.propHub[1], s.propHub[2]) + s.propDiameter / 2;
  for (const auto& p : pts) r = std::max(r, std::hypot(p[0], p[1], p[2]));
  for (const auto& g : gear) r = std::max(r, std::hypot(g.x, g.y - g.radius, g.z));
  reach = r;
  updateRot();
}

// ---------------------------------------------------------------------------
// placing

void Aircraft::placeOnGround(double x, double z, double yaw, Ground& ground, bool engineRunning) {
  reset(engineRunning);
  ground.sample(x, z, 1e4, smp);
  ground.sample(x, z, smp.y + 1, smp);
  // pitch at rest: the line through the loaded nose and main tyres lies on the ground
  const Leg* nose = &legs[0];
  const Leg* main = &legs[legs.size() - 1];
  for (const auto& l : legs) {
    if (l.steers) nose = &l;
    else main = &l;
  }
  const double sn = GEAR.staticShare * nose->travel;
  const double sm = GEAR.staticShare * main->travel;
  const double yn = nose->y - nose->radius + sn;
  const double ym = main->y - main->radius + sm;
  const double theta = std::atan2(ym - yn, main->z - nose->z);
  // orientation: tilt onto the ground's normal · yaw · static pitch
  const double nx = smp.nx;
  const double ny = smp.ny;
  const double nz = smp.nz;
  const double ax = nz;
  const double az = -nx;
  const double al = std::hypot(ax, az);
  const double tilt = std::acos(clamp(ny, -1, 1));
  const double ts = al > 1e-9 ? std::sin(tilt / 2) / al : 0;
  const Quat tq = {ax * ts, 0, az * ts, std::cos(tilt / 2)};
  const Quat yq = {0, std::sin(yaw / 2), 0, std::cos(yaw / 2)};
  const Quat pq = {std::sin(theta / 2), 0, 0, std::cos(theta / 2)};
  quatMul(tq, yq, q);
  quatMul(q, pq, q);
  updateRot();
  // height: every leg at its static compression; take the mean over the legs
  double ySum = 0;
  for (size_t i = 0; i < legs.size(); i++) {
    const Leg& l = legs[i];
    const double s = GEAR.staticShare * l.travel;
    const double lx = R[0] * l.x + R[1] * l.y + R[2] * l.z;
    const double ly = R[3] * l.x + R[4] * l.y + R[5] * l.z;
    const double lz = R[6] * l.x + R[7] * l.y + R[8] * l.z;
    ground.sample(x + lx, z + lz, smp.y + 1, smp);
    const double un = R[1] * smp.nx + R[4] * smp.ny + R[7] * smp.nz;
    // distance of the extended wheel centre from the surface that leaves compression s
    const double d0 = l.radius - s * un;
    ySum += smp.y + d0 / smp.ny - ly;
    wheels[i].compression = s;
    wheels[i].contact = true;
    wasDown[i] = 1;
  }
  pos.x = x;
  pos.y = ySum / legs.size();
  pos.z = z;
  contactAny = true;
  rho = airDensity(pos.y);
  if (running) omegaE = steadyOmega(0, 0, rho);
  sampleGround(ground);
  derive(0);
}

void Aircraft::placeInAir(double x, double y, double z, double yaw, double speed, Ground* ground) {
  reset(true);
  pos.x = x;
  pos.y = y;
  pos.z = z;
  const double v = std::max(0.0, std::isfinite(speed) ? speed : 0);
  const LevelFlight lf = levelFlight(std::max(v, 1.0));
  const double theta = v > 1 ? lf.alpha : 0;
  const Quat yq = {0, std::sin(yaw / 2), 0, std::cos(yaw / 2)};
  const Quat pq = {std::sin(theta / 2), 0, 0, std::cos(theta / 2)};
  quatMul(yq, pq, q);
  updateRot();
  // flying straight ahead, level: the nose is above the flight path by α
  vel.x = -std::sin(yaw) * v;
  vel.y = 0;
  vel.z = -std::cos(yaw) * v;
  omegaE = lf.rpm / RPM;
  tasV = v;
  alphaV = theta;
  dE = elevatorFromTrim(lf.trim, 0);
  rho = airDensity(y);
  if (ground) sampleGround(*ground);
  else {
    gx = x;
    gy = 0;
    gz = z;
    gnx = gnz = 0;
    gny = 1;
  }
  derive(0);
}

LevelFlight Aircraft::levelFlight(double tas) { return levelFlight(tas, flapDeg / spec.flapsMax, pos.y); }
LevelFlight Aircraft::levelFlight(double tas, double flaps) { return levelFlight(tas, flaps, pos.y); }

LevelFlight Aircraft::levelFlight(double tas, double flaps, double altitude) {
  const auto& s = spec;
  const auto& A = AERO;
  const double rho_ = airDensity(altitude);
  const double V = std::max(1.0, tas);
  const double qbar = 0.5 * rho_ * V * V;
  const double clReq = (mass * G) / (qbar * s.wingArea);
  // (the propeller helpers below work on the aeroplane's own fields: keep them)
  const double thrustNow = thrust;
  const double qAirNow = qAir;
  // angle of attack on the attached part of the lift curve
  double lo = A.alphaCritNeg;
  double hi = maxLift(flaps).alpha;
  for (int i = 0; i < 40; i++) {
    const double mid = (lo + hi) / 2;
    if (liftCurve(mid, flaps).cl < clReq) lo = mid;
    else hi = mid;
  }
  const double alpha = (lo + hi) / 2;
  const LiftCurve c = liftCurve(alpha, flaps);
  const double drag = qbar * s.wingArea * dragCoefficient(c.cl, alpha, flaps, kInd, 1, c.stall, c.sep);
  // throttle whose steady thrust (along the nose, α above the path) balances the drag
  const double va = V * std::cos(alpha);
  double t0 = 0;
  double t1 = 1;
  double w = 0;
  for (int i = 0; i < 30; i++) {
    const double t = (t0 + t1) / 2;
    w = steadyOmega(t, va, rho_);
    propeller(w, va, rho_);
    if (thrust * std::cos(alpha) < drag) t0 = t;
    else t1 = t;
  }
  const double throttle = (t0 + t1) / 2;
  w = steadyOmega(throttle, va, rho_);
  propeller(w, va, rho_);
  // trim: the pitching moments balance with the elevator in the middle
  const double T = std::max(0.0, thrust);
  const double qs = qbar * s.wingArea * s.chord;
  const double cm = A.Cm0 + A.Cma * std::sin(alpha) + A.Cmflap * flaps - (s.propHub[1] * T) / qs;
  const double tail = 1 + (PROP.slipstream * T) / (diskArea * qbar);
  const double de = -cm / (A.Cmde * tail);
  const double trim = clamp(de >= 0 ? de / A.trimUp : de / A.trimDown, -1, 1);
  thrust = thrustNow;
  qAir = qAirNow;
  return {throttle, trim, alpha, w * RPM};
}

// ---------------------------------------------------------------------------
// simulation

void Aircraft::step(double dt, const FlightControls& c, Ground& ground) {
  if (!(dt > 0)) return;
  if (dt > 0.1) dt = 0.1;
  readControls(c);
  if (crashed) {
    derive(dt);
    return;
  }
  // what changes slowly enough for once per step: the air, the wind, the ground below
  rho = airDensity(pos.y);
  if (windField) windField(pos.x, pos.y, pos.z, wind);
  sampleGround(ground);
  const double sx = pos.x;
  const double sy = pos.y;
  const double sz = pos.z;
  const double q0 = q[0];
  const double q1 = q[1];
  const double q2 = q[2];
  const double q3 = q[3];
  const int n = (int)std::ceil(dt / H_MAX - 1e-9);
  const double h = dt / n;
  // can anything of the airframe reach the ground within this step?
  const double above = sy - gy - reach - std::abs(vel.y) * dt - 0.5;
  const bool near = !(above > 0);
  if (!near && contactAny)
    for (int i = 0; i < (int)legs.size(); i++) liftOff(i);
  for (int i = 0; i < n && !crashed; i++) substep(h, ground, near);
  if (!finite()) {
    // should never happen; if it does, the aeroplane stops where it was rather than vanishing
    pos.x = sx;
    pos.y = sy;
    pos.z = sz;
    q[0] = q0;
    q[1] = q1;
    q[2] = q2;
    q[3] = q3;
    crash(CrashKind::impact);
  }
  derive(dt);
}

void Aircraft::readControls(const FlightControls& c) {
  cThr = input(c.throttle, 0, 1);
  cElev = input(c.elevator, -1, 1);
  cAil = input(c.aileron, -1, 1);
  cRud = input(c.rudder, -1, 1);
  cFlap = input(c.flaps, 0, 1);
  cTrim = input(c.trim, -1, 1);
  const double b = input(c.brake, 0, 1);
  cBrakeL = std::min(1.0, b + input(c.brakeLeft, 0, 1));
  cBrakeR = std::min(1.0, b + input(c.brakeRight, 0, 1));
  cPark = c.parkingBrake;
  cEngine = c.engineOn;
}

void Aircraft::reset(bool engineRunning) {
  crashed = std::nullopt;
  tailStrike = false;
  vel.x = vel.y = vel.z = 0;
  omega.x = omega.y = omega.z = 0;
  running = engineRunning && fuel > 0;
  omegaE = running ? steadyOmega(0, 0, RHO0) : 0;
  thrust = 0;
  qEngine = 0;
  std::fill(zLong.begin(), zLong.end(), 0);
  std::fill(zLat.begin(), zLat.end(), 0);
  std::fill(wasDown.begin(), wasDown.end(), 0);
  std::fill(wheelW.begin(), wheelW.end(), 0);
  for (auto& w : wheels) {
    w.contact = false;
    w.compression = 0;
    w.steer = 0;
  }
  contactAny = false;
  steerAng = 0;
  dE = dA = dR = 0;
  fsx = 0;
  fsy = mass * G;
  ball = 0;
  nzV = 1;
  gTime = 0;
  tasV = alphaV = betaV = stallV = 0;
  warnV = false;
}

void Aircraft::crash(CrashKind kind) {
  if (crashed) return;
  crashed = kind;
  vel.x = vel.y = vel.z = 0;
  omega.x = omega.y = omega.z = 0;
  running = false;
  omegaE = 0;
  thrust = 0;
  qEngine = 0;
  std::fill(wheelW.begin(), wheelW.end(), 0);
}

bool Aircraft::finite() const {
  const auto& p = pos;
  const auto& v = vel;
  const auto& o = omega;
  return std::isfinite(p.x + p.y + p.z + v.x + v.y + v.z + q[0] + q[1] + q[2] + q[3] + o.x + o.y + o.z + omegaE);
}

void Aircraft::liftOff(int i) {
  wasDown[i] = 0;
  zLong[i] = 0;
  zLat[i] = 0;
  WheelView& w = wheels[i];
  w.contact = false;
  w.compression = 0;
}

void Aircraft::updateRot() {
  const double x = q[0];
  const double y = q[1];
  const double z = q[2];
  const double w = q[3];
  auto& r = R;
  r[0] = 1 - 2 * (y * y + z * z);
  r[1] = 2 * (x * y - z * w);
  r[2] = 2 * (x * z + y * w);
  r[3] = 2 * (x * y + z * w);
  r[4] = 1 - 2 * (x * x + z * z);
  r[5] = 2 * (y * z - x * w);
  r[6] = 2 * (x * z - y * w);
  r[7] = 2 * (y * z + x * w);
  r[8] = 1 - 2 * (x * x + y * y);
}

void Aircraft::sampleGround(Ground& ground) {
  ground.sample(pos.x, pos.z, pos.y, smp);
  gx = pos.x;
  gy = smp.y;
  gz = pos.z;
  const bool ok = smp.ny > 0.2 && std::isfinite(smp.y);
  gnx = ok ? smp.nx : 0;
  gny = ok ? smp.ny : 1;
  gnz = ok ? smp.nz : 0;
  if (!std::isfinite(gy)) gy = -1e4;
  // ground effect: induced drag falls within about half a span of the ground (McCormick)
  const double hw = std::max(0.3, pos.y + spec.wingY - gy);
  const double k = (16 * hw) / spec.span;
  phi = (k * k) / (1 + k * k);
}

double Aircraft::planeY(double x, double z) const { return gy - (gnx * (x - gx) + gnz * (z - gz)) / gny; }

double Aircraft::elevatorFromTrim(double trim, double elevator) const {
  const auto& A = AERO;
  const double d = (elevator > 0 ? elevator * A.elevUp : elevator * A.elevDown) + (trim > 0 ? trim * A.trimUp : trim * A.trimDown);
  return clamp(d, -A.elevDown, A.elevUp);
}

double Aircraft::engineTorque(double t, double w) const {
  const auto& E = ENGINE;
  return (E.idleOpen + (1 - E.idleOpen) * t) * qInd - E.friction[0] - E.friction[1] * w;
}

void Aircraft::propeller(double w, double va, double rho_) {
  const auto& P = PROP;
  const double D = spec.propDiameter;
  const double v = va > 0 ? va : 0;
  const double n = w / TWO_PI;
  // a slow or stopped propeller is taken at the highest advance ratio of the curves
  const double nEff = std::max(n, v / (P.Jmax * D));
  if (!(nEff > 1e-6)) {
    thrust = 0;
    qAir = 0;
    return;
  }
  const double J = v / (nEff * D);
  const double jt = J / P.J0;
  const double jp = J / P.Jp;
  const double ct = propScale * P.CT0 * (1 - jt * std::sqrt(jt));
  const double cp = propScale * P.CP0 * (1 - jp * jp);
  const double nn = rho_ * nEff * nEff * D * D * D * D;
  thrust = ct * nn;
  qAir = (cp * nn * D) / TWO_PI;
}

double Aircraft::steadyOmega(double t, double va, double rho_) {
  double lo = 5;
  double hi = 500;
  for (int i = 0; i < 40; i++) {
    const double mid = (lo + hi) / 2;
    propeller(mid, va, rho_);
    if (engineTorque(t, mid) > qAir) lo = mid;
    else hi = mid;
  }
  return (lo + hi) / 2;
}

void Aircraft::engine(double h, double va) {
  const auto& E = ENGINE;
  const double I = PROP.inertia;
  double w = omegaE;
  if (!cEngine || fuel <= 0) running = false;
  propeller(w, va, rho);
  double qq;
  if (running) {
    qq = engineTorque(cThr, w);
    w += ((qq - qAir) / I) * h;
    if (w * RPM < E.stallRpm) running = false;
    const double open = E.idleOpen + (1 - E.idleOpen) * cThr;
    fuelFlowLh = E.idleFlow + (E.bsfc * open * qInd * std::max(w, 0.0)) / 1000 / E.fuelDensity;
    fuel = std::max(0.0, fuel - (fuelFlowLh * h) / 3600);
  } else {
    fuelFlowLh = 0;
    // the starter turns the engine until it fires
    const double starter = cEngine && fuel > 0 ? E.starter * std::max(0.0, 1 - w / 60) : 0;
    w += ((starter - qAir) / I) * h;
    // compression and friction of the dead engine: high breakaway torque, less once it turns
    const double fr = E.deadFriction + E.friction[1] * std::abs(w) + (E.breakaway - E.deadFriction) / (1 + (w / 8) * (w / 8));
    const double dw = (fr * h) / I;
    w = w > dw ? w - dw : w < -dw ? w + dw : 0;
    qq = starter - (w != 0 ? fr * (w > 0 ? 1 : -1) : 0);
    if (cEngine && fuel > 0 && w * RPM > E.catchRpm) running = true;
  }
  if (w < 0) w = 0;
  if (w > 600) w = 600;
  omegaE = w;
  qEngine = qq;
  propAng += w * h;
}

void Aircraft::substep(double h, Ground& ground, bool near) {
  const auto& s = spec;
  const auto& A = AERO;
  updateRot();
  const auto& Rm = R;
  const double m = mass;
  const double px = pos.x;
  const double py = pos.y;
  const double pz = pos.z;
  const double vx = vel.x;
  const double vy = vel.y;
  const double vz = vel.z;
  const double ox = omega.x;
  const double oy = omega.y;
  const double oz = omega.z;

  // ---- controls: the surfaces follow the stick within some 0.05 s, the flaps run at their rate
  const double lag = h / (0.05 + h);
  dE += (elevatorFromTrim(cTrim, cElev) - dE) * lag;
  dA += (cAil * A.aileron - dA) * lag;
  dR += (cRud * A.rudder - dR) * lag;
  const double fStep = A.flapRate * h;
  flapDeg += clamp(cFlap * s.flapsMax - flapDeg, -fStep, fStep);
  const double flap = s.flapsMax > 0 ? clamp(flapDeg / s.flapsMax, 0, 1) : 0;

  // ---- the air as the aeroplane meets it (body axes)
  const double ax = vx - wind.x;
  const double ay = vy - wind.y;
  const double az = vz - wind.z;
  const double bx = Rm[0] * ax + Rm[3] * ay + Rm[6] * az;
  const double by = Rm[1] * ax + Rm[4] * ay + Rm[7] * az;
  const double bz = Rm[2] * ax + Rm[5] * ay + Rm[8] * az;
  const double u = -bz;
  const double w = -by;
  const double V2 = bx * bx + by * by + bz * bz;
  const double V = std::sqrt(V2);
  tasV = V;
  double Fx = 0;
  double Fy = 0;
  double Fz = 0;
  double Tx = 0;
  double Ty = 0;
  double Tz = 0;

  // ---- engine and propeller: thrust along the nose at the hub; the engine's torque rolls the aeroplane the other way
  engine(h, u);
  const double T = thrust;
  const double Tp = T > 0 ? T : 0;
  const auto& hub = s.propHub;
  Fz -= T;
  Tx -= hub[1] * T;
  Ty += hub[0] * T;
  // (the slipstream's swirl gives about half of the torque back on the wing roots)
  Tz += 0.5 * qEngine;

  // ---- aerodynamics: coefficients in stability axes, forces and moments turned into the body frame
  // (side force +x; rolling moment L → −z, pitching moment M → +x, yawing moment N → −y)
  const double S = s.wingArea;
  const double b = s.span;
  const double cbar = s.chord;
  const double qbar = 0.5 * rho * V2;
  double alpha = 0;
  double beta = 0;
  double stall = 0;
  if (V > 1e-3) {
    alpha = std::atan2(w, u);
    beta = std::asin(clamp(bx / V, -1, 1));
    const double iv = 1 / (2 * V);
    const double ph = clamp(-oz * b * iv, -1, 1);
    const double qh = clamp(ox * cbar * iv, -1, 1);
    const double rh = clamp(-oy * b * iv, -1, 1);
    const LiftCurve cv = liftCurve(alpha, flap);
    stall = cv.stall;
    const double ge = 1 - phi;
    const double cl = cv.cl * (1 + 0.35 * ge * (1 - cv.sep)) + A.CLq * qh;
    const double sb = std::sin(beta);
    const double cd = dragCoefficient(cl, alpha, flap, kInd, phi, stall, cv.sep) + A.CDbeta * sb * sb;
    const double cy = A.CYb * sb + A.CYr * rh;
    const double cm = A.Cm0 + A.Cma * std::sin(alpha) + A.Cmq * qh + A.Cmflap * flap + A.CmStall * stall * (alpha > 0 ? 1 : -1) + A.CmGround * ge;
    const double clc = clamp(cl, -1, 1.6);
    const double rig = propScale;
    // a stalled wing no longer damps the roll: it begins to autorotate
    const double cRoll = A.Clb * sb + A.Clp * (1 - 1.25 * stall) * ph + (A.Clr0 + A.ClrCL * clc) * rh + A.Clda * dA + A.rigRoll * rig;
    const double cYaw = A.Cnb * sb + A.CnpCL * clc * ph + A.Cnr * rh + A.Cnda * dA + A.rigYaw * rig;
    // lift lies across the flow in the plane of symmetry and comes from the flow in that plane
    const double uw2 = u * u + w * w;
    if (uw2 > 1e-8) {
      const double uw = std::sqrt(uw2);
      const double L = 0.5 * rho * uw2 * S * cl;
      Fy += (L * u) / uw;
      Fz -= (L * w) / uw;
    }
    const double D = (qbar * S * cd) / V;
    Fx -= D * bx;
    Fy -= D * by;
    Fz -= D * bz;
    Fx += qbar * S * cy;
    Tx += qbar * S * cbar * cm;
    Ty -= qbar * S * b * cYaw;
    Tz -= qbar * S * b * cRoll;
    // the slipstream's swirl on the fin and the P-factor yaw to the left
    Ty += Tp * (PROP.swirl + PROP.pFactor * clamp(alpha, -0.3, 0.3));
  }
  // elevator and rudder sit in part of the slipstream
  const double qTail = qbar + (PROP.slipstream * Tp) / diskArea;
  Tx += qTail * S * cbar * A.Cmde * dE;
  Fy += qTail * S * A.CLde * dE;
  Fx += qTail * S * A.CYdr * dR;
  Ty -= qTail * S * b * A.Cndr * dR;
  Tz -= qTail * S * b * A.Cldr * dR;
  alphaV = alpha;
  betaV = beta;
  stallV = stall;

  // ---- landing gear and airframe on the ground (world frame)
  double GX = 0;
  double GY = 0;
  double GZ = 0;
  double GTx = 0;
  double GTy = 0;
  double GTz = 0;
  bool any = false;
  const double gs = std::sqrt(vx * vx + vz * vz);
  const double steerMax = GEAR.steer / (1 + (gs / GEAR.steerFade) * (gs / GEAR.steerFade));
  steerAng += (cRud * steerMax - steerAng) * (h / (0.1 + h));
  if (near) {
    // angular velocity in the world
    const double wx = Rm[0] * ox + Rm[1] * oy + Rm[2] * oz;
    const double wy = Rm[3] * ox + Rm[4] * oy + Rm[5] * oz;
    const double wz = Rm[6] * ox + Rm[7] * oy + Rm[8] * oz;
    const double upX = Rm[1];
    const double upY = Rm[4];
    const double upZ = Rm[7];
    const double fwX = -Rm[2];
    const double fwY = -Rm[5];
    const double fwZ = -Rm[8];
    const double cs = std::cos(steerAng);
    const double sn = std::sin(steerAng);
    for (int i = 0; i < (int)legs.size(); i++) {
      const Leg& l = legs[i];
      const double lx = Rm[0] * l.x + Rm[1] * l.y + Rm[2] * l.z;
      const double ly = Rm[3] * l.x + Rm[4] * l.y + Rm[5] * l.z;
      const double lz = Rm[6] * l.x + Rm[7] * l.y + Rm[8] * l.z;
      const double Ax = px + lx;
      const double Ay = py + ly;
      const double Az = pz + lz;
      if (Ay - planeY(Ax, Az) > l.radius + 0.5) {
        if (wasDown[i]) liftOff(i);
        spinFree(i, h);
        continue;
      }
      ground.sample(Ax, Az, Ay, smp);
      const double nx = smp.nx;
      const double ny = smp.ny;
      const double nz = smp.nz;
      const double un = upX * nx + upY * ny + upZ * nz;
      // how far the strut is pushed in to put the tyre on the surface
      double sc = un > 0.3 ? (l.radius - (Ay - smp.y) * ny) / un : -1;
      if (!(sc > 0)) {
        if (wasDown[i]) liftOff(i);
        spinFree(i, h);
        continue;
      }
      if (sc > l.travel + 0.3) sc = l.travel + 0.3;
      // contact patch: lever and velocity
      const double rx = lx + upX * sc - nx * l.radius;
      const double ry = ly + upY * sc - ny * l.radius;
      const double rz = lz + upZ * sc - nz * l.radius;
      const double cvx = vx + wy * rz - wz * ry;
      const double cvy = vy + wz * rx - wx * rz;
      const double cvz = vz + wx * ry - wy * rx;
      const double vn = cvx * nx + cvy * ny + cvz * nz;
      if (!wasDown[i] && -vn > LIMITS.touchdown) {
        crash(CrashKind::impact);
        return;
      }
      double N = l.k * sc - (l.c * vn) / un;
      if (sc > l.travel) N += GEAR.bump * l.k * (sc - l.travel);
      if (N < 0) N = 0;
      if (N > (l.steers ? LIMITS.noseLeg : LIMITS.mainLeg) * l.load) {
        crash(l.steers ? CrashKind::noseOver : CrashKind::impact);
        return;
      }
      wasDown[i] = 1;
      WheelView& wv = wheels[i];
      wv.compression = sc < l.travel ? sc : l.travel;
      wv.contact = N > 0;
      if (N > 0) any = true;
      // rolling direction (the nose wheel steered) and the tyre's side, both in the ground plane
      double dx = fwX;
      double dy = fwY;
      double dz = fwZ;
      if (l.steers) {
        dx = fwX * cs + Rm[0] * sn;
        dy = fwY * cs + Rm[3] * sn;
        dz = fwZ * cs + Rm[6] * sn;
      }
      const double dn = dx * nx + dy * ny + dz * nz;
      dx -= nx * dn;
      dy -= ny * dn;
      dz -= nz * dn;
      double dl = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (!(dl != 0)) dl = 1;
      dx /= dl;
      dy /= dl;
      dz /= dl;
      const double tx = dy * nz - dz * ny;
      const double ty = dz * nx - dx * nz;
      const double tz = dx * ny - dy * nx;
      const double vl = cvx * dx + cvy * dy + cvz * dz;
      const double vt = cvx * tx + cvy * ty + cvz * tz;
      const double av = std::abs(vl);
      // Bristle tyre: sideways always (relaxing as the tyre rolls on), lengthways
      // only while a brake holds the wheel.  A spring at standstill, the usual
      // cornering stiffness when rolling.  The damping is needed (and acts) only
      // near standstill.
      double zt = zLat[i];
      if (av > 1e-3) {
        const double e = std::exp((-av * h) / GEAR.relax);
        zt = zt * e + ((vt * GEAR.relax) / av) * (1 - e);
      } else
        zt += vt * h;
      const double low = 1 / (1 + (av * av) / 2.25);
      double ft = -(l.kTyre * zt + l.cTyre * low * vt);
      const double brake = l.side < 0 ? cBrakeL : l.side > 0 ? cBrakeR : 0;
      const double cap = l.side != 0 ? (std::max(brake * GEAR.brake, cPark ? GEAR.parking : 0) * m) / 1000 : 0;
      double zl = 0;
      double fl = 0;
      if (cap > 0) {
        zl = zLong[i] + vl * h;
        fl = -(l.kTyre * zl + l.cTyre * low * vl);
        if (fl > cap || fl < -cap) {
          const double k = cap / std::abs(fl);
          fl *= k;
          zl *= k;
        }
      }
      const double fMax = GEAR.mu * smp.grip * N;
      const double fTot = std::sqrt(fl * fl + ft * ft);
      bool locked = false;
      if (fTot > fMax) {
        const double k = fMax / fTot;
        fl *= k;
        ft *= k;
        zl *= k;
        zt *= k;
        locked = cap >= fMax && av > 0.5;
      }
      zLat[i] = zt;
      zLong[i] = zl;
      const double fL = fl - GEAR.rolling * smp.rolling * N * std::tanh(vl / 0.3);
      const double gfx = dx * fL + tx * ft + nx * N;
      const double gfy = dy * fL + ty * ft + ny * N;
      const double gfz = dz * fL + tz * ft + nz * N;
      GX += gfx;
      GY += gfy;
      GZ += gfz;
      GTx += ry * gfz - rz * gfy;
      GTy += rz * gfx - rx * gfz;
      GTz += rx * gfy - ry * gfx;
      wheelW[i] = locked ? 0 : vl / l.radius;
      wv.spin += wheelW[i] * h;
    }

    // the airframe: points of the hull, and the lowest point of the propeller disc
    const int nh = (int)hullKind.size();
    const double nx = gnx;
    const double ny = gny;
    const double nz = gnz;
    // highest the ground plane gets within reach of the airframe: points above it are skipped early
    const double top = gy + (reach * std::sqrt(nx * nx + nz * nz)) / ny - py;
    for (int j = 0; j <= nh; j++) {
      double hx;
      double hy;
      double hz;
      uint8_t kind;
      if (j < nh) {
        hx = hull[j * 3];
        hy = hull[j * 3 + 1];
        hz = hull[j * 3 + 2];
        kind = hullKind[j];
      } else {
        // world down in the plane of the disc
        const double ex = -Rm[3];
        const double ey = -Rm[4];
        const double el = std::sqrt(ex * ex + ey * ey);
        const double rad = s.propDiameter / 2;
        hx = hub[0] + (el > 1e-6 ? (rad * ex) / el : 0);
        hy = hub[1] + (el > 1e-6 ? (rad * ey) / el : -rad);
        hz = hub[2];
        kind = HULL_WING;
      }
      const double ry = Rm[3] * hx + Rm[4] * hy + Rm[5] * hz;
      if (ry > top) continue;
      const double rx = Rm[0] * hx + Rm[1] * hy + Rm[2] * hz;
      const double rz = Rm[6] * hx + Rm[7] * hy + Rm[8] * hz;
      const double X = px + rx;
      const double Z = pz + rz;
      const double depth = (planeY(X, Z) - (py + ry)) * ny;
      if (!(depth > 0)) continue;
      const double cvx = vx + wy * rz - wz * ry;
      const double cvy = vy + wz * rx - wx * rz;
      const double cvz = vz + wx * ry - wy * rx;
      const double vn = cvx * nx + cvy * ny + cvz * nz;
      if (-vn > LIMITS.hull) {
        crash(CrashKind::impact);
        return;
      }
      if (kind == HULL_WING) {
        crash(CrashKind::wing);
        return;
      }
      if (kind == HULL_TOP) {
        crash(CrashKind::noseOver);
        return;
      }
      if (kind == HULL_BELLY && gs > 3) {
        crash(CrashKind::impact);
        return;
      }
      if (kind == HULL_TAIL) {
        if (depth > 0.15) {
          crash(CrashKind::impact);
          return;
        }
        tailStrike = true;
      }
      // scraping over the ground
      const double N = std::max(0.0, 2e5 * depth - 8000 * vn);
      const double tvx = cvx - vn * nx;
      const double tvy = cvy - vn * ny;
      const double tvz = cvz - vn * nz;
      const double fr = (-0.4 * N) / std::max(std::sqrt(tvx * tvx + tvy * tvy + tvz * tvz), 0.3);
      const double gfx = nx * N + fr * tvx;
      const double gfy = ny * N + fr * tvy;
      const double gfz = nz * N + fr * tvz;
      GX += gfx;
      GY += gfy;
      GZ += gfz;
      GTx += ry * gfz - rz * gfy;
      GTy += rz * gfx - rx * gfz;
      GTz += rx * gfy - ry * gfx;
      any = true;
    }
  } else
    for (int i = 0; i < (int)legs.size(); i++) spinFree(i, h);
  contactAny = any;

  // ---- sum up: body forces into the world, the gear's torques into the body
  const double FWx = Rm[0] * Fx + Rm[1] * Fy + Rm[2] * Fz + GX;
  const double FWy = Rm[3] * Fx + Rm[4] * Fy + Rm[5] * Fz + GY;
  const double FWz = Rm[6] * Fx + Rm[7] * Fy + Rm[8] * Fz + GZ;
  const double TBx = Tx + Rm[0] * GTx + Rm[3] * GTy + Rm[6] * GTz;
  const double TBy = Ty + Rm[1] * GTx + Rm[4] * GTy + Rm[7] * GTz;
  const double TBz = Tz + Rm[2] * GTx + Rm[5] * GTy + Rm[8] * GTz;
  // what the pilot feels (everything but gravity), in the body: load factor and ball
  fsx = Rm[0] * FWx + Rm[3] * FWy + Rm[6] * FWz;
  fsy = Rm[1] * FWx + Rm[4] * FWy + Rm[7] * FWz;

  // ---- integrate (semi-implicit Euler)
  // Euler's equations with the propeller's angular momentum: it turns clockwise
  // seen from the cockpit, i.e. about the nose (−z), which gives the gyroscopic
  // moments (pitching up yaws to the right).
  const auto& I = inertia;
  const double Lx = I[0] * ox;
  const double Ly = I[1] * oy;
  const double Lz = I[2] * oz - PROP.inertia * omegaE;
  double nox = ox + ((TBx - (oy * Lz - oz * Ly)) / I[0]) * h;
  double noy = oy + ((TBy - (oz * Lx - ox * Lz)) / I[1]) * h;
  double noz = oz + ((TBz - (ox * Ly - oy * Lx)) / I[2]) * h;
  const double ol = std::sqrt(nox * nox + noy * noy + noz * noz);
  if (ol > 12) {
    const double k = 12 / ol;
    nox *= k;
    noy *= k;
    noz *= k;
  }
  omega.x = nox;
  omega.y = noy;
  omega.z = noz;
  double nvx = vx + (FWx / m) * h;
  double nvy = vy + (FWy / m - G) * h;
  double nvz = vz + (FWz / m) * h;
  const double vl = std::sqrt(nvx * nvx + nvy * nvy + nvz * nvz);
  if (vl > 250) {
    const double k = 250 / vl;
    nvx *= k;
    nvy *= k;
    nvz *= k;
  }
  vel.x = nvx;
  vel.y = nvy;
  vel.z = nvz;
  pos.x = px + nvx * h;
  pos.y = py + nvy * h;
  pos.z = pz + nvz * h;
  // dq/dt = ½ q ⊗ (ω, 0) with body rates
  const double qx = q[0];
  const double qy = q[1];
  const double qz = q[2];
  const double qw = q[3];
  q[0] = qx + 0.5 * h * (qw * nox + qy * noz - qz * noy);
  q[1] = qy + 0.5 * h * (qw * noy + qz * nox - qx * noz);
  q[2] = qz + 0.5 * h * (qw * noz + qx * noy - qy * nox);
  q[3] = qw - 0.5 * h * (qx * nox + qy * noy + qz * noz);
  double ql = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  if (!(ql != 0)) ql = 1;
  q[0] /= ql;
  q[1] /= ql;
  q[2] /= ql;
  q[3] /= ql;
}

void Aircraft::spinFree(int i, double h) {
  const Leg& l = legs[i];
  const double brake = l.side < 0 ? cBrakeL : l.side > 0 ? cBrakeR : 0;
  const bool held = l.side != 0 && (brake > 0.05 || cPark);
  const double w = held ? 0 : wheelW[i] * (1 - std::min(1.0, 0.3 * h));
  wheelW[i] = w;
  wheels[i].spin += w * h;
}

void Aircraft::derive(double dt) {
  updateRot();
  // the lowest wheel above the ground below it
  double aglMin = std::numeric_limits<double>::infinity();
  for (size_t i = 0; i < legs.size(); i++) {
    const Leg& l = legs[i];
    const double y = l.y + wheels[i].compression;
    const double wx = pos.x + R[0] * l.x + R[1] * y + R[2] * l.z;
    const double wy = pos.y + R[3] * l.x + R[4] * y + R[5] * l.z - l.radius;
    const double wz = pos.z + R[6] * l.x + R[7] * y + R[8] * l.z;
    const double d = wy - planeY(wx, wz);
    if (d < aglMin) aglMin = d;
    wheels[i].steer = l.steers ? steerAng : 0;
  }
  aglV = aglMin > 0 ? aglMin : 0;
  if (crashed) {
    warnV = false;
    return;
  }
  // stall warning: the vane at the leading edge, some 2° before the critical angle
  const double flap = spec.flapsMax > 0 ? flapDeg / spec.flapsMax : 0;
  warnV = tasV > 12 && alphaV > AERO.alphaCrit + AERO.flapAlpha * flap - AERO.warn;
  // load factor, and the ball: it hangs in the direction of everything the pilot feels but gravity
  const double mg = mass * G;
  nzV = fsy / mg;
  const double lean = std::atan2(-fsx, std::abs(fsy) + 1e-3 * mg);
  ball += (clamp(lean / 0.26, -1, 1) - ball) * std::min(1.0, dt / 0.3);
  // what the airframe does not survive
  if (ias() > LIMITS.overspeed * spec.vne) crash(CrashKind::overspeed);
  else if (!contactAny && (nzV > LIMITS.gMax || nzV < LIMITS.gMin)) {
    gTime += dt;
    if (gTime > 0.15) crash(CrashKind::overspeed);
  } else
    gTime = 0;
}

// ---------------------------------------------------------------------------
// what the game reads

double Aircraft::ias() const { return tasV * std::sqrt(rho / RHO0); }

double Aircraft::heading() const {
  const double fx = -2 * (q[0] * q[2] + q[1] * q[3]);
  const double fz = -(1 - 2 * (q[0] * q[0] + q[1] * q[1]));
  const double d = std::atan2(fx, -fz) / DEG;
  return d < 0 ? d + 360 : d;
}

double Aircraft::yaw() const {
  const double fx = -2 * (q[0] * q[2] + q[1] * q[3]);
  const double fz = -(1 - 2 * (q[0] * q[0] + q[1] * q[1]));
  return std::atan2(-fx, -fz);
}

double Aircraft::pitch() const { return std::asin(clamp(-2 * (q[1] * q[2] - q[0] * q[3]), -1, 1)); }

double Aircraft::bank() const {
  const double rightY = 2 * (q[0] * q[1] + q[2] * q[3]);
  const double upY = 1 - 2 * (q[0] * q[0] + q[2] * q[2]);
  return std::atan2(-rightY, upY);
}

double Aircraft::turnRate() const {
  const auto& o = omega;
  const double r3 = 2 * (q[0] * q[1] + q[2] * q[3]);
  const double r4 = 1 - 2 * (q[0] * q[0] + q[2] * q[2]);
  const double r5 = 2 * (q[1] * q[2] - q[0] * q[3]);
  return -(r3 * o.x + r4 * o.y + r5 * o.z);
}

double Aircraft::rpm() const { return omegaE * RPM; }

const FlightSurfaces& Aircraft::surfaces() {
  surf.elevator = dE;
  surf.aileron = dA;
  surf.rudder = dR;
  return surf;
}

}  // namespace fs

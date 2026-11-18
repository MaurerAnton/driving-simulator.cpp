// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Vehicle dynamics: suspension, bristle tyres, engine/clutch/gearbox.
 *
 * Origin: src/sim/vehicle/vehicle.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).  The maths mirrors the JavaScript originals step for step: the
 * same operations on the same doubles in the same order.
 */
#include "sim/vehicle/vehicle.h"

#include <algorithm>
#include <cmath>

#include "core/geom.h"

namespace fs {

namespace {
constexpr double G = 9.81;
constexpr double AIR_DENSITY = 1.2;
constexpr double RPM = 30.0 / PI_;
/** suspension length at full droop (attachment to wheel centre) */
constexpr double L_MAX = 0.36;

double clampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

int signD(double v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }
}  // namespace

Vehicle::Vehicle(const VehicleSpec& spec_, Ground& ground_)
    : spec(spec_), ground(ground_) {
  const double b = spec.wheelBase - spec.cgToFront;
  const double loadF = (spec.mass * G * b) / spec.wheelBase / 2;
  const double loadR = (spec.mass * G * spec.cgToFront) / spec.wheelBase / 2;
  staticComp = {loadF / spec.springRate[0], loadR / spec.springRate[1]};
  const double yF = -(spec.cgHeight - spec.wheelRadius) + L_MAX - staticComp[0];
  const double yR = -(spec.cgHeight - spec.wheelRadius) + L_MAX - staticComp[1];
  const double t = spec.track / 2;
  attach = {{{-t, yF, -spec.cgToFront},
             {t, yF, -spec.cgToFront},
             {-t, yR, b},
             {t, yR, b}}};
  modelOrigin[0] = 0;
  modelOrigin[1] = -spec.cgHeight;
  modelOrigin[2] = spec.cgToFront - spec.wheelBase / 2;
  idleOmega = spec.idleRpm / RPM;
  for (int i = 0; i < 4; i++) susLen[i] = L_MAX;
  teleport(0, 0, 0, 0);
}

void Vehicle::updateRot() {
  const double x = quat[0], y = quat[1], z = quat[2], w = quat[3];
  rot[0] = 1 - 2 * (y * y + z * z);
  rot[1] = 2 * (x * y - z * w);
  rot[2] = 2 * (x * z + y * w);
  rot[3] = 2 * (x * y + z * w);
  rot[4] = 1 - 2 * (x * x + z * z);
  rot[5] = 2 * (y * z - x * w);
  rot[6] = 2 * (x * z - y * w);
  rot[7] = 2 * (y * z + x * w);
  rot[8] = 1 - 2 * (x * x + y * y);
}

void Vehicle::teleport(double x, double y, double z, double yaw) {
  double s = std::sin(yaw / 2);
  double c = std::cos(yaw / 2);
  quat[0] = 0;
  quat[1] = s;
  quat[2] = 0;
  quat[3] = c;
  updateRot();
  const double* mo = modelOrigin;
  pos[0] = x - (rot[0] * mo[0] + rot[1] * mo[1] + rot[2] * mo[2]);
  pos[1] = y - (rot[3] * mo[0] + rot[4] * mo[1] + rot[5] * mo[2]);
  pos[2] = z - (rot[6] * mo[0] + rot[7] * mo[1] + rot[8] * mo[2]);
  for (int i = 0; i < 3; i++) {
    vel[i] = 0;
    angVel[i] = 0;
    prevVel[i] = 0;
  }
  for (int i = 0; i < 4; i++) {
    int axle = i < 2 ? 0 : 1;
    comp[i] = compPrev[i] = staticComp[static_cast<size_t>(axle)];
    susLen[i] = L_MAX - comp[i];
    zx[i] = 0;
    zy[i] = 0;
    absMul[i] = 1;
    wheels[static_cast<size_t>(i)].omega = 0;
    wheels[static_cast<size_t>(i)].compression = comp[i];
  }
  speed = 0;
  accLong = 0;
  accLat = 0;
  derive(0);
}

void Vehicle::resetDrivetrain(bool engineRunning_) {
  engineRunning = engineRunning_;
  engineOmega = engineRunning ? idleOmega : 0;
  idleI = engineRunning ? 0.12 : 0;
  fireTimer = 0;
  shiftTimer = 0;
  limiterCut = false;
  startGrace = 0;
  rpm = engineOmega * RPM;
}

void Vehicle::applyImpulse(double px, double py, double pz, double jx, double jy, double jz) {
  double m = spec.mass;
  vel[0] += jx / m;
  vel[1] += jy / m;
  vel[2] += jz / m;
  double rx = px - pos[0];
  double ry = py - pos[1];
  double rz = pz - pos[2];
  addAngular(ry * jz - rz * jy, rz * jx - rx * jz, rx * jy - ry * jx, 1);
}

void Vehicle::translate(double dx, double dy, double dz) {
  pos[0] += dx;
  pos[1] += dy;
  pos[2] += dz;
}

void Vehicle::getWheelHeights(double out[4]) const {
  for (int i = 0; i < 4; i++)
    out[i] = attach[static_cast<size_t>(i)][1] - susLen[i] - modelOrigin[1];
}

void Vehicle::getModelPosition(double out[3]) const {
  const double* mo = modelOrigin;
  out[0] = pos[0] + rot[0] * mo[0] + rot[1] * mo[1] + rot[2] * mo[2];
  out[1] = pos[1] + rot[3] * mo[0] + rot[4] * mo[1] + rot[5] * mo[2];
  out[2] = pos[2] + rot[6] * mo[0] + rot[7] * mo[1] + rot[8] * mo[2];
}

void Vehicle::toWorld(double bx, double by, double bz, double out[3]) const {
  out[0] = pos[0] + rot[0] * bx + rot[1] * by + rot[2] * bz;
  out[1] = pos[1] + rot[3] * bx + rot[4] * by + rot[5] * bz;
  out[2] = pos[2] + rot[6] * bx + rot[7] * by + rot[8] * bz;
}

void Vehicle::addAngular(double tx, double ty, double tz, double h) {
  const double* r = rot;
  const auto& I = spec.inertia;
  double bx = (r[0] * tx + r[3] * ty + r[6] * tz) / I[0];
  double by = (r[1] * tx + r[4] * ty + r[7] * tz) / I[1];
  double bz = (r[2] * tx + r[5] * ty + r[8] * tz) / I[2];
  angVel[0] += (r[0] * bx + r[1] * by + r[2] * bz) * h;
  angVel[1] += (r[3] * bx + r[4] * by + r[5] * bz) * h;
  angVel[2] += (r[6] * bx + r[7] * by + r[8] * bz) * h;
}

double Vehicle::torqueAt(double rpm_) const {
  const auto& c = spec.torqueCurve;
  if (c.empty()) return 0;
  if (rpm_ <= c.front().first) return c.front().second;
  for (size_t i = 1; i < c.size(); i++) {
    if (rpm_ <= c[i].first) {
      double t = (rpm_ - c[i - 1].first) / (c[i].first - c[i - 1].first);
      return c[i - 1].second + (c[i].second - c[i - 1].second) * t;
    }
  }
  return 0;
}

double Vehicle::ratio(int g) const {
  if (g == 0 || g == -2) return 0;
  if (g < 0) return -spec.reverseRatio * spec.finalDrive;
  int idx = std::min(g, 6) - 1;
  if (idx < 0) idx = 0;
  return spec.gearRatios[static_cast<size_t>(idx)] * spec.finalDrive;
}

void Vehicle::step(double dt, const DriverInput& input) {
  stalledEvent = false;
  gearGrind = false;
  int n = substeps;
  double h = dt / n;
  for (int i = 0; i < n; i++) substep(h, input);
  derive(dt);
}

void Vehicle::substep(double h, const DriverInput& inp) {
  const VehicleSpec& s = spec;
  auto& w = wheels;
  updateRot();
  const double* r = rot;
  const double px = pos[0], py = pos[1], pz = pos[2];
  const double vx = vel[0], vy = vel[1], vz = vel[2];
  const double wx = angVel[0], wy = angVel[1], wz = angVel[2];
  // forward = -Z body axis
  const double fwdX = -r[2], fwdY = -r[5], fwdZ = -r[8];
  const double upX = r[1], upY = r[4], upZ = r[7];
  const double rightX = r[0], rightY = r[3], rightZ = r[6];
  const double vForward = vx * fwdX + vy * fwdY + vz * fwdZ;

  // ---- steering (Ackermann) ------------------------------------------------
  double delta = clampD(inp.steer, -1, 1) * s.maxWheelAngle;
  steerWheelAngle = delta * s.steeringRatio;
  if (std::fabs(delta) > 1e-4) {
    double R = s.wheelBase / std::tan(std::fabs(delta));
    double inner = std::atan(s.wheelBase / (R - s.track / 2));
    double outer = std::atan(s.wheelBase / (R + s.track / 2));
    if (delta > 0) {
      w[0].steer = outer;
      w[1].steer = inner;
    } else {
      w[0].steer = -inner;
      w[1].steer = -outer;
    }
  } else {
    w[0].steer = delta;
    w[1].steer = delta;
  }

  // ---- gearbox / clutch engagement ----------------------------------------
  double driveOmega = (w[0].omega + w[1].omega) / 2;
  double engagement = 0;
  int g = gear;
  bool parkLock = false;
  bool converter = false;
  if (s.transmission == "automatic") {
    converter = true;
    int sel = inp.gear;
    if (sel == -2) {
      g = 0;
      parkLock = std::fabs(vForward) < 1.2;
    } else if (sel == -1) {
      g = vForward < 1.5 ? -1 : 0;
    } else if (sel == 0) {
      g = 0;
    } else {
      if (vForward < -1.5) {
        g = 0;
      } else {
        // shift schedule on engine speed, later with more throttle
        int ag = autoGear;
        if (shiftTimer <= 0) {
          double up = 1900 + 4300 * inp.throttle;
          double down = 1050 + 1700 * inp.throttle;
          double rpmNow = engineOmega * RPM;
          double syncRpm = driveOmega * ratio(ag) * RPM;
          if (ag < 6 && syncRpm > up && rpmNow > up * 0.9) {
            ag++;
            shiftTimer = 0.38;
          } else if (ag > 1 && (syncRpm < down ||
                                (inp.throttle > 0.92 && driveOmega * ratio(ag - 1) * RPM < 5900))) {
            ag--;
            shiftTimer = 0.38;
          }
        }
        autoGear = ag;
        g = ag;
      }
    }
    if (g <= 0) autoGear = 1;
    engagement = 1;
  } else {
    if (inp.gear != g) {
      int target = static_cast<int>(clampD(std::lround(inp.gear), -1, 6));
      if (target == 0) {
        g = 0;
      } else {
        double mismatch = std::fabs(engineOmega - ratio(target) * driveOmega);
        bool wrongWay = (target == -1 && vForward > 1.5) || (target > 0 && vForward < -1.5);
        if (wrongWay) {
          gearGrind = true;
        } else if (s.autoClutch) {
          g = target;
          shiftTimer = 0.26;
        } else if (inp.clutch > 0.6 || mismatch < 12) {
          g = target;
        } else {
          gearGrind = true;
        }
      }
    }
    double pedal = inp.clutch;
    if (s.autoClutch) {
      // launch like a centrifugal clutch, open when the engine would otherwise stall
      double syncRpm = std::fabs(driveOmega * ratio(g)) * RPM;
      double rpmNow = engineOmega * RPM;
      double e = 1;
      if (syncRpm < 1150)
        e = inp.throttle > 0.02 ? clampD((rpmNow - 950) / 1100, 0, 1)
                                : clampD((syncRpm - 850) / 250, 0, 1);
      if (shiftTimer > 0 || !engineRunning) e = engineRunning ? 0 : e;
      if (inp.brake > 0.3 && syncRpm < 1000) e = 0;
      pedal = 0.75 - 0.45 * std::pow(e, 1.0 / 1.6);
    }
    clutchPedal = pedal;
    double t = clampD((0.75 - pedal) / 0.45, 0, 1);
    engagement = std::pow(t, 1.6);
  }
  if (shiftTimer > 0) shiftTimer -= h;
  gear = g;
  double rati = ratio(g);

  // ---- wheels: suspension and tyres ---------------------------------------
  double Fx = 0, Fy = -s.mass * G, Fz = 0;
  double Tx = 0, Ty = 0, Tz = 0;
  GroundSample& smp = sampleState;
  double frontLatSum = 0;
  for (int i = 0; i < 4; i++) {
    auto& wh = w[static_cast<size_t>(i)];
    int axle = i < 2 ? 0 : 1;
    const auto& a = attach[static_cast<size_t>(i)];
    // attachment point (world) and its lever arm
    double ax = r[0] * a[0] + r[1] * a[1] + r[2] * a[2];
    double ay = r[3] * a[0] + r[4] * a[1] + r[5] * a[2];
    double az = r[6] * a[0] + r[7] * a[1] + r[8] * a[2];
    double Ax = px + ax, Ay = py + ay, Az = pz + az;
    // rolling direction of this wheel
    double sd = std::sin(wh.steer), cd = std::cos(wh.steer);
    double fx = rightX * sd + fwdX * cd;
    double fy = rightY * sd + fwdY * cd;
    double fz = rightZ * sd + fwdZ * cd;
    // wheel centre at the previous suspension length
    double l0 = susLen[i];
    double cx0 = Ax - upX * l0;
    double cy0 = Ay - upY * l0;
    double cz0 = Az - upZ * l0;
    // ground envelope under the tyre
    double rad = s.wheelRadius;
    ground.sample(cx0, cz0, cy0, smp);
    double gy = smp.y;
    double nx = smp.nx, ny = smp.ny, nz = smp.nz;
    double grip = smp.grip, rolling = smp.rolling;
    double fl = std::hypot(fx, fz);
    if (fl == 0) fl = 1;
    double hx = fx / fl, hz = fz / fl;
    for (int k = 0; k < 4; k++) {
      double off = (k < 2 ? 0.45 : 0.75) * (k % 2 == 0 ? 1 : -1) * rad;
      double lift = (k < 2 ? 0.893 : 0.6614) * rad - rad;
      ground.sample(cx0 + hx * off, cz0 + hz * off, cy0, smp);
      if (smp.y + lift > gy) gy = smp.y + lift;
    }
    // suspension length that puts the tyre on the plane through (cx0, gy, cz0)
    double dn = -(upX * nx + upY * ny + upZ * nz);  // down axis · normal (negative)
    bool contact = false;
    double l = L_MAX;
    if (dn < -0.25) {
      double dist = (Ax - cx0) * nx + (Ay - gy) * ny + (Az - cz0) * nz;
      l = (dist - rad) / -dn;
      if (l < L_MAX) contact = true;
      else l = L_MAX;
    }
    double compValue = L_MAX - l;
    double N = 0;
    if (contact) {
      if (compValue > s.travel + 0.06) compValue = s.travel + 0.06;  // the bump stop is not infinitely soft
      l = L_MAX - compValue;
      double vc = (compValue - compPrev[i]) / h;
      vc = clampD(vc, -3, 3);
      double other = compPrev[i ^ 1];
      N = s.springRate[static_cast<size_t>(axle)] * compValue +
          (vc > 0 ? s.damperBump[static_cast<size_t>(axle)] : s.damperRebound[static_cast<size_t>(axle)]) * vc +
          s.antiRoll[static_cast<size_t>(axle)] * (compValue - other);
      if (compValue > s.travel - 0.035) N += 260000 * (compValue - (s.travel - 0.035));
      if (N < 0) N = 0;
    }
    comp[i] = compValue;
    susLen[i] = l;
    wh.compression = compValue;
    wh.contact = contact && N > 0;
    wh.load = N;
    wh.grip = grip;
    double Cx = Ax - upX * l, Cy = Ay - upY * l, Cz = Az - upZ * l;
    wh.hx = Cx;
    wh.hy = Cy;
    wh.hz = Cz;
    if (!wh.contact) {
      zx[i] *= 0.9;
      zy[i] *= 0.9;
      fxTyre[i] = 0;
      wh.slipRatio = 0;
      wh.slipAngle = 0;
      wh.skid = 0;
      continue;
    }
    // contact point and its velocity
    double Px = Cx - nx * rad, Py = Cy - ny * rad, Pz = Cz - nz * rad;
    double rx = Px - px, ry = Py - py, rz = Pz - pz;
    double vpx = vx + wy * rz - wz * ry;
    double vpy = vy + wz * rx - wx * rz;
    double vpz = vz + wx * ry - wy * rx;
    // tyre axes in the ground plane
    double fn = fx * nx + fy * ny + fz * nz;
    fx -= nx * fn;
    fy -= ny * fn;
    fz -= nz * fn;
    double fl2 = std::hypot(fx, fy, fz);
    if (fl2 == 0) fl2 = 1;
    fx /= fl2;
    fy /= fl2;
    fz /= fl2;
    // side = forward × normal (points to the wheel's right)
    double sx = fy * nz - fz * ny;
    double sy = fz * nx - fx * nz;
    double sz = fx * ny - fy * nx;
    double vLong = vpx * fx + vpy * fy + vpz * fz;
    double vLat = vpx * sx + vpy * sy + vpz * sz;
    // bristle deflection: dz/dt = slip velocity − |v|/L · z
    double av = std::fabs(vLong);
    double vsx = wh.omega * rad - vLong;
    double vsy = -vLat;
    // softer peaks on loose surfaces
    double soft = 1 / std::sqrt(std::max(grip, 0.3));
    double kPeak = s.slipRatioPeak * soft;
    double aPeak = std::tan(s.slipAnglePeak) * soft;
    double zxi = zx[i], zyi = zy[i];
    if (av > 1e-3) {
      double ex = std::exp((-av * h) / s.relaxLong);
      double ey = std::exp((-av * h) / s.relaxLat);
      zxi = zxi * ex + ((vsx * s.relaxLong) / av) * (1 - ex);
      zyi = zyi * ey + ((vsy * s.relaxLat) / av) * (1 - ey);
    } else {
      zxi += vsx * h;
      zyi += vsy * h;
    }
    // damping of the tyre spring, only needed (and only acting) near standstill
    double low = 1 / (1 + (av * av) / 2.25);
    double tau = 0.035 * low;
    double sgx = (zxi + tau * vsx) / s.relaxLong / kPeak;
    double sgy = (zyi + tau * vsy) / s.relaxLat / aPeak;
    double rho = std::hypot(sgx, sgy);
    const double RHO_MAX = 3.5;
    if (rho > RHO_MAX) {
      // anti wind-up: a sliding tyre does not store more deflection
      double kk = RHO_MAX / rho;
      zxi *= kk;
      zyi *= kk;
      sgx *= kk;
      sgy *= kk;
      rho = RHO_MAX;
    }
    zx[i] = zxi;
    zy[i] = zyi;
    double staticLoad = (s.mass * G) / 4;
    double mu = s.muPeak * grip *
                clampD(1 - s.loadSensitivity * (N / staticLoad - 1), 0.75, 1.12);
    double fxT = 0, fyT = 0;
    if (rho > 1e-6) {
      double f = (mu * N * pacejka(rho)) / rho;
      fxT = f * sgx;
      fyT = f * sgy;
    }
    fxTyre[i] = fxT;
    wh.slipRatio = zxi / s.relaxLong;
    wh.slipAngle = std::atan(zyi / s.relaxLat);
    wh.skid = clampD((rho - 1) / 1.5, 0, 1) * clampD(std::hypot(vsx, vsy) / 2, 0, 1);
    if (i < 2) frontLatSum += fyT / (mu * N + 1);
    // rolling resistance acts on the body as a force opposing the motion
    double frr = -s.rollingResistance * rolling * N * std::tanh(vLong / 0.4);
    double tfx = fx * (fxT + frr) + sx * fyT + nx * N;
    double tfy = fy * (fxT + frr) + sy * fyT + ny * N;
    double tfz = fz * (fxT + frr) + sz * fyT + nz * N;
    Fx += tfx;
    Fy += tfy;
    Fz += tfz;
    Tx += ry * tfz - rz * tfy;
    Ty += rz * tfx - rx * tfz;
    Tz += rx * tfy - ry * tfx;
    // ABS: release when the wheel decelerates into deep slip
    if (s.abs) {
      double kappa = (wh.omega * rad - vLong) / std::max(av, 0.6);
      if (inp.brake > 0.05 && vLong > 1.6 && kappa < -0.15) absMul[i] -= h * 16;
      else absMul[i] += h * 8;
      absMul[i] = clampD(absMul[i], 0.04, 1);
    }
  }
  frontLat = frontLatSum / 2;
  for (int i = 0; i < 4; i++) compPrev[i] = comp[i];

  // ---- engine ---------------------------------------------------------------
  const double Ie = s.engineInertia;
  const double Iw = s.wheelInertia;
  double we = engineOmega;
  if (!inp.ignition && engineRunning) engineRunning = false;
  double rpmNow = we * RPM;
  double Te = 0;
  double offFriction = 0;
  if (engineRunning) {
    double err = idleOmega - we;
    // Modern engine management defends the idle speed with a lot of torque
    // (anti-stall), but not with all of it: a careless clutch still kills it.
    idleI = clampD(idleI + err * h * 0.05, 0, 0.35);
    double idleThr = clampD(err * 0.035 + idleI, 0, 0.85);
    double thr = std::max(clampD(inp.throttle, 0, 1), idleThr);
    if (rpmNow > s.limiterRpm) limiterCut = true;
    else if (rpmNow < s.limiterRpm - 140) limiterCut = false;
    if (limiterCut) thr = 0;
    double fr = s.engineFriction[0] + s.engineFriction[1] * std::max(rpmNow, 0.0);
    Te = thr * (torqueAt(rpmNow) + fr) - fr;
    engineLoad = thr;
  } else {
    engineLoad = 0;
    idleI = 0;
    // Compression and friction of a dead engine act as a Coulomb brake:
    // high breakaway torque (holds the parked car in gear), less once it turns.
    double aw = std::fabs(we);
    offFriction = 25 + 35 / (1 + (aw / 3) * (aw / 3)) + 0.05 * aw;
    if (inp.ignition && inp.starter) {
      Te = 115 * std::max(0.0, 1 - we / 34);
      if (we > 19) fireTimer += h;
      else fireTimer = std::max(0.0, fireTimer - h);
      if (fireTimer > 0.5) {
        // it fires at cranking speed, well below the stall threshold: give it
        // a moment to run up to idle before stalling is judged again
        engineRunning = true;
        idleI = 0.14;
        fireTimer = 0;
        startGrace = 0.9;
      }
    } else {
      fireTimer = 0;
    }
  }

  // ---- clutch as a friction constraint ---------------------------------------
  double S = rollTorque(fxTyre[0], s.wheelRadius) + rollTorque(fxTyre[1], s.wheelRadius);
  // Gearbox and differential losses: less torque arrives when the engine
  // drives, more is taken when the wheels drive the engine.
  bool driving = lastTc * rati * driveOmega >= 0;
  double wheelRatio = rati * (driving ? s.drivelineEfficiency : 1 / s.drivelineEfficiency);
  double Tc = 0;
  double capacity = 0;
  if (rati != 0) {
    if (converter) {
      // fluid coupling: capacity grows with engine speed squared, locked up when cruising
      double sync = std::fabs(driveOmega * rati);
      capacity = 0.00356 * we * we;
      if (gear >= 2 && sync > 135 && shiftTimer <= 0)
        capacity = std::max(capacity, s.clutchTorque);
      if (shiftTimer > 0) capacity *= 0.35;
      capacity = std::min(capacity, s.clutchTorque * 1.1);
    } else {
      capacity = engagement * s.clutchTorque;
    }
    if (capacity > 0) {
      double win = rati * driveOmega;
      double est = offFriction > 0 && std::fabs(we) > 0.5 ? -signD(we) * offFriction : 0;
      double lock = ((we - win) / h + (Te + est) / Ie + (rati * S) / (2 * Iw)) /
                    (1 / Ie + (rati * wheelRatio) / (2 * Iw));
      Tc = clampD(lock, -capacity, capacity);
    }
  }
  lastTc = Tc;
  clutchLock = capacity > 0 && std::fabs(Tc) < capacity * 0.999 ? 1 : (capacity > 0 ? 0.5 : 0);

  we += ((Te - Tc) / Ie) * h;
  if (offFriction > 0) {
    double need = (std::fabs(we) * Ie) / h;
    if (offFriction >= need) {
      we = 0;
    } else {
      we -= (signD(we) * offFriction * h) / Ie;
    }
  }
  if (we < 0 && engineRunning) we = 0;
  if (startGrace > 0) startGrace -= h;
  if (engineRunning && startGrace <= 0 && we * RPM < s.stallRpm) {
    engineRunning = false;
    stalledEvent = true;
  }
  engineOmega = we;

  // ---- wheel rotation and brakes ----------------------------------------------
  bool absActive = false;
  for (int i = 0; i < 4; i++) {
    auto& wh = w[static_cast<size_t>(i)];
    bool front = i < 2;
    double T = -fxTyre[i] * s.wheelRadius;
    if (front) T += (Tc * wheelRatio) / 2;
    wh.omega += (T / Iw) * h;
    double brake = inp.brake * s.brakeTorque *
                   (front ? s.brakeBiasFront / 2 : (1 - s.brakeBiasFront) / 2) * absMul[i];
    if (!front) brake += inp.handbrake * (s.handbrakeTorque / 2);
    if (front && parkLock) brake += 6000;
    if (brake > 0) {
      double need = (std::fabs(wh.omega) * Iw) / h;
      if (brake >= need) {
        wh.omega = 0;
      } else {
        wh.omega -= (signD(wh.omega) * brake * h) / Iw;
      }
    }
    if (absMul[i] < 0.9) absActive = true;
    wh.spin += wh.omega * h;
  }
  this->absActive = absActive;

  // ---- aerodynamics, integration -------------------------------------------------
  double v2 = std::hypot(vx, vy, vz);
  double drag = 0.5 * AIR_DENSITY * s.dragArea * v2;
  Fx -= drag * vx;
  Fy -= drag * vy;
  Fz -= drag * vz;
  vel[0] += (Fx / s.mass) * h;
  vel[1] += (Fy / s.mass) * h;
  vel[2] += (Fz / s.mass) * h;
  addAngular(Tx, Ty, Tz, h);
  // a little angular damping keeps a parked car from accumulating numerical spin
  double ad = 1 - 0.05 * h;
  angVel[0] *= ad;
  angVel[1] *= ad;
  angVel[2] *= ad;
  double al = std::hypot(angVel[0], angVel[1], angVel[2]);
  if (al > 25) {
    double k = 25 / al;
    angVel[0] *= k;
    angVel[1] *= k;
    angVel[2] *= k;
  }
  pos[0] += vel[0] * h;
  pos[1] += vel[1] * h;
  pos[2] += vel[2] * h;
  double ox = angVel[0], oy = angVel[1], oz = angVel[2];
  double qx = quat[0], qy = quat[1], qz = quat[2], qw = quat[3];
  quat[0] += 0.5 * h * (ox * qw + oy * qz - oz * qy);
  quat[1] += 0.5 * h * (oy * qw + oz * qx - ox * qz);
  quat[2] += 0.5 * h * (oz * qw + ox * qy - oy * qx);
  quat[3] += 0.5 * h * (-ox * qx - oy * qy - oz * qz);
  double ql = std::sqrt(quat[0] * quat[0] + quat[1] * quat[1] + quat[2] * quat[2] + quat[3] * quat[3]);
  if (ql == 0) ql = 1;
  quat[0] /= ql;
  quat[1] /= ql;
  quat[2] /= ql;
  quat[3] /= ql;
  odometer += std::fabs(vForward) * h;
}

void Vehicle::derive(double dt) {
  updateRot();
  const double* r = rot;
  double fx = -r[2], fy = -r[5], fz = -r[8];
  speed = vel[0] * fx + vel[1] * fy + vel[2] * fz;
  yaw = std::atan2(-fx, -fz);
  rpm = engineOmega * RPM;
  if (dt > 0) {
    double ax = (vel[0] - prevVel[0]) / dt;
    double ay = (vel[1] - prevVel[1]) / dt;
    double az = (vel[2] - prevVel[2]) / dt;
    double k = std::min(1.0, dt * 12);
    accLong += (ax * fx + ay * fy + az * fz - accLong) * k;
    accLat += (ax * r[0] + ay * r[3] + az * r[6] - accLat) * k;
  }
  prevVel[0] = vel[0];
  prevVel[1] = vel[1];
  prevVel[2] = vel[2];
  // self-aligning feel: lateral utilisation of the front axle, fading as the tyres saturate
  steerTorque = clampD(-frontLat * 1.4, -1, 1);
}

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/vehicle/vehicle.ts @ v1.7.7
#include "sim/vehicle/vehicle.h"

#include <cmath>

#include "core/geom.h"
#include "core/js.h"
#include "sim/jsmath.h"

namespace fs {

namespace {

const double G = 9.81;
const double AIR_DENSITY = 1.2;
const double RPM = 30 / PI;
/** suspension length at full droop (attachment to wheel centre) */
const double L_MAX = 0.36;
/** Pacejka shape: peak at normalised slip 1, sliding level ≈ 0.71 of the peak */
const double PAC_B = 1.9;
const double PAC_C = 1.5;
const double PAC_E = 0.2;

/**
 * Corners of the body shell relative to the point on the ground below the
 * middle of the wheelbase (x right, y up, z back): bumpers/sills and roof.
 */
const std::vector<std::array<double, 3>> BODY_POINTS = {
    {-0.8, 0.32, -2.02},
    {0.8, 0.32, -2.02},
    {-0.8, 0.32, 2.02},
    {0.8, 0.32, 2.02},
    {-0.84, 0.86, -0.9},
    {0.84, 0.86, -0.9},
    {-0.84, 0.92, 1.7},
    {0.84, 0.92, 1.7},
    {-0.58, 1.42, -0.42},
    {0.58, 1.42, -0.42},
    {-0.58, 1.42, 1.2},
    {0.58, 1.42, 1.2},
};
const double BODY_STIFFNESS = 160000;  // N/m per corner
const double BODY_DAMPING = 9000;      // N·s/m
const double BODY_FRICTION = 0.55;

double pacejka(double rho) {
  const double u = PAC_B * rho;
  return jsSin(PAC_C * jsAtan(u - PAC_E * (u - jsAtan(u))));
}

/** Torque a longitudinal tyre force exerts against the wheel's rotation. */
double rollTorque(double fx, double radius) { return fx * radius; }

/** `x || 1` */
inline double orOne(double x) { return (x == 0 || x != x) ? 1.0 : x; }

}  // namespace

Vehicle::Vehicle(const VehicleSpec& spec_, Ground& ground_) : spec(spec_), ground(ground_) {
  susLen.fill(L_MAX);
  const double b = spec.wheelbase - spec.cgToFront;
  const double loadF = (spec.mass * G * b) / spec.wheelbase / 2;
  const double loadR = (spec.mass * G * spec.cgToFront) / spec.wheelbase / 2;
  staticComp = {loadF / spec.springRate[0], loadR / spec.springRate[1]};
  const double yF = -(spec.cgHeight - spec.wheelRadius) + L_MAX - staticComp[0];
  const double yR = -(spec.cgHeight - spec.wheelRadius) + L_MAX - staticComp[1];
  const double t = spec.track / 2;
  attach = {{
      {-t, yF, -spec.cgToFront},
      {t, yF, -spec.cgToFront},
      {-t, yR, b},
      {t, yR, b},
  }};
  modelOrigin = {0, -spec.cgHeight, spec.cgToFront - spec.wheelbase / 2};
  idleOmega = spec.idleRpm / RPM;
  teleport(0, 0, 0, 0);
}

void Vehicle::teleport(double x, double y, double z, double yaw_) {
  const double s = jsSin(yaw_ / 2);
  const double c = jsCos(yaw_ / 2);
  quat[0] = 0;
  quat[1] = s;
  quat[2] = 0;
  quat[3] = c;
  updateRot();
  const auto& r = rot;
  const auto& mo = modelOrigin;
  pos[0] = x - (r[0] * mo[0] + r[1] * mo[1] + r[2] * mo[2]);
  pos[1] = y - (r[3] * mo[0] + r[4] * mo[1] + r[5] * mo[2]);
  pos[2] = z - (r[6] * mo[0] + r[7] * mo[1] + r[8] * mo[2]);
  vel.fill(0);
  angVel.fill(0);
  prevVel.fill(0);
  for (int i = 0; i < 4; i++) {
    const int axle = i < 2 ? 0 : 1;
    comp[i] = compPrev[i] = staticComp[axle];
    susLen[i] = L_MAX - comp[i];
    zx[i] = 0;
    zy[i] = 0;
    absMul[i] = 1;
    wheels[i].omega = 0;
    wheels[i].compression = comp[i];
  }
  speed = 0;
  accLong = 0;
  accLat = 0;
  derive(0);
}

void Vehicle::resetDrivetrain(bool engineRunning_) {
  engineRunning = engineRunning_;
  engineOmega = engineRunning_ ? idleOmega : 0;
  idleI = engineRunning_ ? 0.12 : 0;
  fireTimer = 0;
  shiftTimer = 0;
  limiterCut = false;
  startGrace = 0;
  rpm = engineOmega * RPM;
}

void Vehicle::applyImpulse(double px, double py, double pz, double jx, double jy, double jz) {
  const double m = spec.mass;
  vel[0] += jx / m;
  vel[1] += jy / m;
  vel[2] += jz / m;
  const double rx = px - pos[0];
  const double ry = py - pos[1];
  const double rz = pz - pos[2];
  addAngular(ry * jz - rz * jy, rz * jx - rx * jz, rx * jy - ry * jx, 1);
}

void Vehicle::translate(double dx, double dy, double dz) {
  pos[0] += dx;
  pos[1] += dy;
  pos[2] += dz;
}

void Vehicle::getWheelHeights(double* out) const {
  for (int i = 0; i < 4; i++) out[i] = attach[i][1] - susLen[i] - modelOrigin[1];
}

void Vehicle::getModelPosition(double* out) const {
  const auto& r = rot;
  const auto& mo = modelOrigin;
  out[0] = pos[0] + r[0] * mo[0] + r[1] * mo[1] + r[2] * mo[2];
  out[1] = pos[1] + r[3] * mo[0] + r[4] * mo[1] + r[5] * mo[2];
  out[2] = pos[2] + r[6] * mo[0] + r[7] * mo[1] + r[8] * mo[2];
}

void Vehicle::toWorld(double bx, double by, double bz, double* out) const {
  const auto& r = rot;
  out[0] = pos[0] + r[0] * bx + r[1] * by + r[2] * bz;
  out[1] = pos[1] + r[3] * bx + r[4] * by + r[5] * bz;
  out[2] = pos[2] + r[6] * bx + r[7] * by + r[8] * bz;
}

void Vehicle::step(double dt, const DriverInput& input) {
  stalledEvent = false;
  gearGrind = false;
  const int n = substeps;
  const double h = dt / n;
  for (int i = 0; i < n; i++) substep(h, input);
  derive(dt);
}

// ---------------------------------------------------------------------------
void Vehicle::updateRot() {
  const auto& q = quat;
  const double x = q[0];
  const double y = q[1];
  const double z = q[2];
  const double w = q[3];
  auto& r = rot;
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

void Vehicle::addAngular(double tx, double ty, double tz, double h) {
  const auto& r = rot;
  const auto& I = spec.inertia;
  const double bx = (r[0] * tx + r[3] * ty + r[6] * tz) / I[0];
  const double by = (r[1] * tx + r[4] * ty + r[7] * tz) / I[1];
  const double bz = (r[2] * tx + r[5] * ty + r[8] * tz) / I[2];
  angVel[0] += (r[0] * bx + r[1] * by + r[2] * bz) * h;
  angVel[1] += (r[3] * bx + r[4] * by + r[5] * bz) * h;
  angVel[2] += (r[6] * bx + r[7] * by + r[8] * bz) * h;
}

double Vehicle::torqueAt(double rpm_) const {
  const auto& c = spec.torqueCurve;
  if (rpm_ <= c[0][0]) return c[0][1];
  for (size_t i = 1; i < c.size(); i++) {
    if (rpm_ <= c[i][0]) {
      const double t = (rpm_ - c[i - 1][0]) / (c[i][0] - c[i - 1][0]);
      return c[i - 1][1] + (c[i][1] - c[i - 1][1]) * t;
    }
  }
  return 0;
}

double Vehicle::ratio(int gear_) const {
  const VehicleSpec& s = spec;
  if (gear_ == 0 || gear_ == -2) return 0;
  if (gear_ < 0) return -s.reverseRatio * s.finalDrive;
  return s.gearRatios[std::min(gear_, static_cast<int>(s.gearRatios.size())) - 1] * s.finalDrive;
}

void Vehicle::substep(double h, const DriverInput& inp) {
  const VehicleSpec& s = spec;
  auto& w = wheels;
  updateRot();
  const auto& r = rot;
  const double px = pos[0];
  const double py = pos[1];
  const double pz = pos[2];
  const double vx = vel[0];
  const double vy = vel[1];
  const double vz = vel[2];
  const double wx = angVel[0];
  const double wy = angVel[1];
  const double wz = angVel[2];
  // forward = -Z body axis
  const double fwdX = -r[2];
  const double fwdY = -r[5];
  const double fwdZ = -r[8];
  const double upX = r[1];
  const double upY = r[4];
  const double upZ = r[7];
  const double rightX = r[0];
  const double rightY = r[3];
  const double rightZ = r[6];
  const double vForward = vx * fwdX + vy * fwdY + vz * fwdZ;

  // ---- steering (Ackermann) ------------------------------------------------
  const double delta = clamp(inp.steer, -1, 1) * s.maxWheelAngle;
  steerWheelAngle = delta * s.steeringRatio;
  if (std::abs(delta) > 1e-4) {
    const double R = s.wheelbase / jsTan(std::abs(delta));
    const double inner = jsAtan(s.wheelbase / (R - s.track / 2));
    const double outer = jsAtan(s.wheelbase / (R + s.track / 2));
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
  // the driven axle: wheels 0 and 1 (front) or 2 and 3 (rear)
  const int d0 = s.rearDrive ? 2 : 0;
  const int d1 = d0 + 1;
  const double driveOmega = (w[d0].omega + w[d1].omega) / 2;
  double engagement;
  int gear_ = gear;
  bool parkLock = false;
  bool converter = false;
  if (s.transmission == "automatic") {
    converter = true;
    const int sel = inp.gear;
    if (sel == -2) {
      gear_ = 0;
      parkLock = std::abs(vForward) < 1.2;
    } else if (sel == -1) {
      gear_ = vForward < 1.5 ? -1 : 0;
    } else if (sel == 0) {
      gear_ = 0;
    } else {
      if (vForward < -1.5) {
        gear_ = 0;
      } else {
        // shift schedule on engine speed, later with more throttle
        int g = autoGear;
        if (shiftTimer <= 0) {
          const auto& auto_ = s.auto_;
          const double up = auto_ ? auto_->up[0] + (auto_->up[1] - auto_->up[0]) * inp.throttle : 1900 + 4300 * inp.throttle;
          const double down = auto_ ? auto_->down[0] + (auto_->down[1] - auto_->down[0]) * inp.throttle : 1050 + 1700 * inp.throttle;
          const double rpmNow = engineOmega * RPM;
          const double syncRpm = driveOmega * ratio(g) * RPM;
          if (g < static_cast<int>(s.gearRatios.size()) && syncRpm > up && rpmNow > up * 0.9) {
            g++;
            shiftTimer = 0.38;
          } else if (g > 1 && (syncRpm < down || (inp.throttle > 0.92 && driveOmega * ratio(g - 1) * RPM < (auto_ ? auto_->kickdownRpm : 5900)))) {
            g--;
            shiftTimer = 0.38;
          }
        }
        autoGear = g;
        gear_ = g;
      }
    }
    if (gear_ <= 0) autoGear = 1;
    engagement = 1;
  } else {
    if (inp.gear != gear_) {
      const int target = std::max(-1, std::min(inp.gear, static_cast<int>(s.gearRatios.size())));
      if (target == 0) {
        gear_ = 0;
      } else {
        const double mismatch = std::abs(engineOmega - ratio(target) * driveOmega);
        const bool wrongWay = (target == -1 && vForward > 1.5) || (target > 0 && vForward < -1.5);
        if (wrongWay) {
          gearGrind = true;
        } else if (s.autoClutch) {
          gear_ = target;
          shiftTimer = 0.26;
        } else if (inp.clutch > 0.6 || mismatch < 12) {
          gear_ = target;
        } else {
          gearGrind = true;
        }
      }
    }
    double pedal = inp.clutch;
    if (s.autoClutch) {
      // launch like a centrifugal clutch, open when the engine would otherwise stall
      const double syncRpm = std::abs(driveOmega * ratio(gear_)) * RPM;
      const double rpmNow = engineOmega * RPM;
      double e = 1;
      if (syncRpm < 1150) e = inp.throttle > 0.02 ? clamp((rpmNow - 950) / 1100, 0, 1) : clamp((syncRpm - 850) / 250, 0, 1);
      if (shiftTimer > 0 || !engineRunning) e = engineRunning ? 0 : e;
      if (inp.brake > 0.3 && syncRpm < 1000) e = 0;
      pedal = 0.75 - 0.45 * std::pow(e, 1 / 1.6);
    }
    clutchPedal = pedal;
    const double t = clamp((0.75 - pedal) / 0.45, 0, 1);
    engagement = std::pow(t, 1.6);
  }
  if (shiftTimer > 0) shiftTimer -= h;
  gear = gear_;
  const double ratio_ = ratio(gear_);

  // ---- wheels: suspension and tyres ---------------------------------------
  double Fx = 0;
  double Fy = -s.mass * G;
  double Fz = 0;
  double Tx = 0;
  double Ty = 0;
  double Tz = 0;
  GroundSample& smp = sample;
  double frontLat_ = 0;
  for (int i = 0; i < 4; i++) {
    WheelState& wh = w[i];
    const int axle = i < 2 ? 0 : 1;
    const auto& a = attach[i];
    // attachment point (world) and its lever arm
    const double ax = r[0] * a[0] + r[1] * a[1] + r[2] * a[2];
    const double ay = r[3] * a[0] + r[4] * a[1] + r[5] * a[2];
    const double az = r[6] * a[0] + r[7] * a[1] + r[8] * a[2];
    const double Ax = px + ax;
    const double Ay = py + ay;
    const double Az = pz + az;
    // rolling direction of this wheel
    const double sd = jsSin(wh.steer);
    const double cd = jsCos(wh.steer);
    double fx = rightX * sd + fwdX * cd;
    double fy = rightY * sd + fwdY * cd;
    double fz = rightZ * sd + fwdZ * cd;
    // wheel centre at the previous suspension length
    const double l0 = susLen[i];
    const double cx0 = Ax - upX * l0;
    const double cy0 = Ay - upY * l0;
    const double cz0 = Az - upZ * l0;
    // ground envelope under the tyre
    const double rad = s.wheelRadius;
    ground.sample(cx0, cz0, cy0, smp);
    double gy = smp.y;
    const double nx = smp.nx;
    const double ny = smp.ny;
    const double nz = smp.nz;
    const double grip = smp.grip;
    const double rolling = smp.rolling;
    const double fl = orOne(jsHypot(fx, fz));
    const double hx = fx / fl;
    const double hz = fz / fl;
    for (int k = 0; k < 4; k++) {
      const double off = (k < 2 ? 0.45 : 0.75) * (k % 2 == 0 ? 1 : -1) * rad;
      const double lift = (k < 2 ? 0.893 : 0.6614) * rad - rad;
      ground.sample(cx0 + hx * off, cz0 + hz * off, cy0, smp);
      if (smp.y + lift > gy) gy = smp.y + lift;
    }
    // suspension length that puts the tyre on the plane through (cx0, gy, cz0)
    const double dn = -(upX * nx + upY * ny + upZ * nz);  // down axis · normal (negative)
    bool contact = false;
    double l = L_MAX;
    if (dn < -0.25) {
      const double dist = (Ax - cx0) * nx + (Ay - gy) * ny + (Az - cz0) * nz;
      l = (dist - rad) / -dn;
      if (l < L_MAX) contact = true;
      else l = L_MAX;
    }
    double comp_ = L_MAX - l;
    double N = 0;
    if (contact) {
      if (comp_ > s.travel + 0.06) comp_ = s.travel + 0.06;  // the bump stop is not infinitely soft
      l = L_MAX - comp_;
      double vc = (comp_ - compPrev[i]) / h;
      vc = clamp(vc, -3, 3);
      const double other = compPrev[i ^ 1];
      N = s.springRate[axle] * comp_ + (vc > 0 ? s.damperBump[axle] : s.damperRebound[axle]) * vc + s.antiRoll[axle] * (comp_ - other);
      if (comp_ > s.travel - 0.035) {
        const auto& stop = s.bumpStop;
        N += (stop ? stop->stiffness : 260000) * (comp_ - (s.travel - 0.035));
        // (a rubber bump stop swallows the blow instead of handing it back)
        if (stop) N += stop->damping * vc;
      }
      if (N < 0) N = 0;
    }
    comp[i] = comp_;
    susLen[i] = l;
    wh.compression = comp_;
    wh.contact = contact && N > 0;
    wh.load = N;
    wh.grip = grip;
    const double Cx = Ax - upX * l;
    const double Cy = Ay - upY * l;
    const double Cz = Az - upZ * l;
    wh.hx = Cx;
    wh.hy = Cy;
    wh.hz = Cz;
    if (!wh.contact) {
      zx[i] *= 0.9;
      zy[i] *= 0.9;
      fxTyre[i] = 0;
      wheelImp[i] = 1;
      wh.slipRatio = 0;
      wh.slipAngle = 0;
      wh.skid = 0;
      continue;
    }
    // contact point and its velocity
    const double Px = Cx - nx * rad;
    const double Py = Cy - ny * rad;
    const double Pz = Cz - nz * rad;
    const double rx = Px - px;
    const double ry = Py - py;
    const double rz = Pz - pz;
    const double vpx = vx + wy * rz - wz * ry;
    const double vpy = vy + wz * rx - wx * rz;
    const double vpz = vz + wx * ry - wy * rx;
    // tyre axes in the ground plane
    const double fn = fx * nx + fy * ny + fz * nz;
    fx -= nx * fn;
    fy -= ny * fn;
    fz -= nz * fn;
    const double fl2 = orOne(jsHypot(fx, fy, fz));
    fx /= fl2;
    fy /= fl2;
    fz /= fl2;
    // side = forward × normal (points to the wheel's right)
    const double sx = fy * nz - fz * ny;
    const double sy = fz * nx - fx * nz;
    const double sz = fx * ny - fy * nx;
    const double vLong = vpx * fx + vpy * fy + vpz * fz;
    const double vLat = vpx * sx + vpy * sy + vpz * sz;
    // bristle deflection: dz/dt = slip velocity − |v|/L · z  (exact exponential update)
    const double av = std::abs(vLong);
    const double vsx = wh.omega * rad - vLong;
    const double vsy = -vLat;
    // softer peaks on loose surfaces
    const double soft = 1 / std::sqrt(jsMax(grip, 0.3));
    const double kPeak = s.slipRatioPeak * soft;
    const double aPeak = jsTan(s.slipAnglePeak) * soft;
    double zx_ = zx[i];
    double zy_ = zy[i];
    if (av > 1e-3) {
      const double ex = jsExp((-av * h) / s.relaxLong);
      const double ey = jsExp((-av * h) / s.relaxLat);
      zx_ = zx_ * ex + ((vsx * s.relaxLong) / av) * (1 - ex);
      zy_ = zy_ * ey + ((vsy * s.relaxLat) / av) * (1 - ey);
    } else {
      zx_ += vsx * h;
      zy_ += vsy * h;
    }
    // damping of the tyre spring, only needed (and only acting) near standstill
    const double low = 1 / (1 + (av * av) / 2.25);
    const double tau = 0.035 * low;
    double sgx = (zx_ + tau * vsx) / s.relaxLong / kPeak;
    double sgy = (zy_ + tau * vsy) / s.relaxLat / aPeak;
    double rho = jsHypot(sgx, sgy);
    const double RHO_MAX = 3.5;
    if (rho > RHO_MAX) {
      // anti wind-up: a sliding tyre does not store more deflection
      const double k = RHO_MAX / rho;
      zx_ *= k;
      zy_ *= k;
      sgx *= k;
      sgy *= k;
      rho = RHO_MAX;
    }
    zx[i] = zx_;
    zy[i] = zy_;
    const double staticLoad = (s.mass * G) / 4;
    const double mu = s.muPeak * grip * clamp(1 - s.loadSensitivity * (N / staticLoad - 1), 0.75, 1.12);
    double fxT = 0;
    double fyT = 0;
    // secant stiffness of the tyre curve (force per unit of normalised slip)
    const double F = rho > 1e-6 ? (mu * N * pacejka(rho)) / rho : mu * N * PAC_B * PAC_C;
    if (rho > 1e-6) {
      fxT = F * sgx;
      fyT = F * sgy;
    }
    fxTyre[i] = fxT;
    // The standstill damping is far too stiff for the light wheel to be
    // integrated explicitly (it would flip-flop around the road speed and
    // shake itself loose from a gently applied brake): remember how strongly
    // the force reacts to the wheel speed, the wheel update divides by it.
    wheelImp[i] = 1 + (((F * tau) / (s.relaxLong * kPeak)) * rad * rad * h) / s.wheelInertia;
    wh.slipRatio = (zx_ / s.relaxLong);
    wh.slipAngle = jsAtan(zy_ / s.relaxLat);
    wh.skid = clamp((rho - 1) / 1.5, 0, 1) * clamp(jsHypot(vsx, vsy) / 2, 0, 1);
    if (i < 2) frontLat_ += fyT / (mu * N + 1);
    // rolling resistance acts on the body as a force opposing the motion
    const double frr = -s.rollingResistance * rolling * N * jsTanh(vLong / 0.4);
    const double tfx = fx * (fxT + frr) + sx * fyT + nx * N;
    const double tfy = fy * (fxT + frr) + sy * fyT + ny * N;
    const double tfz = fz * (fxT + frr) + sz * fyT + nz * N;
    Fx += tfx;
    Fy += tfy;
    Fz += tfz;
    Tx += ry * tfz - rz * tfy;
    Ty += rz * tfx - rx * tfz;
    Tz += rx * tfy - ry * tfx;
    // ABS: release when the wheel decelerates into deep slip
    if (s.abs) {
      const double kappa = (wh.omega * rad - vLong) / jsMax(av, 0.6);
      if (inp.brake > 0.05 && vLong > 1.6 && kappa < -0.15) absMul[i] -= h * 16;
      else absMul[i] += h * 8;
      absMul[i] = clamp(absMul[i], 0.04, 1);
    }
  }
  frontLat = frontLat_ / 2;
  compPrev = comp;

  // ---- body on the ground -----------------------------------------------------
  // Only the wheels carry the car in normal driving.  Once it leans far over
  // (rolled, or nose-down in a ditch) the corners of the body shell take over,
  // so that it comes to rest on its side or roof instead of sinking away.
  bodyContact = false;
  if (upY < 0.9) {
    const auto& mo = modelOrigin;
    // (a heavier vehicle needs a harder shell to be carried by it)
    const double shell = s.mass / 1375;
    for (const auto& b : s.bodyPoints ? *s.bodyPoints : BODY_POINTS) {
      const double bx = b[0] + mo[0];
      const double by = b[1] + mo[1];
      const double bz = b[2] + mo[2];
      const double rx = r[0] * bx + r[1] * by + r[2] * bz;
      const double ry = r[3] * bx + r[4] * by + r[5] * bz;
      const double rz = r[6] * bx + r[7] * by + r[8] * bz;
      ground.sample(px + rx, pz + rz, py + ry + 0.8, smp);
      const double depth = smp.y - (py + ry);
      if (!(depth > 0)) continue;
      const double vpx = vx + wy * rz - wz * ry;
      const double vpy = vy + wz * rx - wx * rz;
      const double vpz = vz + wx * ry - wy * rx;
      const double vn = vpx * smp.nx + vpy * smp.ny + vpz * smp.nz;
      const double N = clamp((BODY_STIFFNESS * jsMin(depth, 0.4) - BODY_DAMPING * vn) * shell, 0, 90000 * shell);
      if (N <= 0) continue;
      // sheet metal sliding on the ground
      const double tx = vpx - vn * smp.nx;
      const double ty = vpy - vn * smp.ny;
      const double tz = vpz - vn * smp.nz;
      const double f = (-BODY_FRICTION * N) / jsMax(jsHypot(tx, ty, tz), 0.4);
      const double bfx = N * smp.nx + f * tx;
      const double bfy = N * smp.ny + f * ty;
      const double bfz = N * smp.nz + f * tz;
      Fx += bfx;
      Fy += bfy;
      Fz += bfz;
      Tx += ry * bfz - rz * bfy;
      Ty += rz * bfx - rx * bfz;
      Tz += rx * bfy - ry * bfx;
      bodyContact = true;
    }
  }

  // ---- engine ---------------------------------------------------------------
  const double Ie = s.engineInertia;
  const double Iw = s.wheelInertia;
  double we = engineOmega;
  if (!inp.ignition && engineRunning) engineRunning = false;
  const double rpm_ = we * RPM;
  double Te = 0;
  double offFriction = 0;
  if (engineRunning) {
    const double err = idleOmega - we;
    // Modern engine management defends the idle speed with a lot of torque
    // (anti-stall), but not with all of it: a careless clutch still kills it.
    idleI = clamp(idleI + err * h * 0.05, 0, 0.35);
    const double idleThr = clamp(err * 0.035 + idleI, 0, 0.85);
    double thr = jsMax(clamp(inp.throttle, 0, 1), idleThr);
    if (rpm_ > s.limiterRpm) limiterCut = true;
    else if (rpm_ < s.limiterRpm - 140) limiterCut = false;
    if (limiterCut) thr = 0;
    const double fr = s.engineFriction[0] + s.engineFriction[1] * jsMax(rpm_, 0);
    Te = thr * (torqueAt(rpm_) + fr) - fr;
    engineLoad = thr;
  } else {
    engineLoad = 0;
    idleI = 0;
    // Compression and friction of a dead engine act as a Coulomb brake:
    // high breakaway torque (holds the parked car in gear), less once it turns.
    const double aw = std::abs(we);
    offFriction = 25 + 35 / (1 + (aw / 3) * (aw / 3)) + 0.05 * aw;
    if (inp.ignition && inp.starter) {
      Te = 115 * jsMax(0, 1 - we / 34);
      if (we > 19) fireTimer += h;
      else fireTimer = jsMax(0, fireTimer - h);
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
  // inverse inertia of the two driven wheels (implicit against the tyre damping, see above)
  const double inv0 = 1 / (s.wheelInertia * wheelImp[d0]);
  const double inv1 = 1 / (s.wheelInertia * wheelImp[d1]);
  const double S = rollTorque(fxTyre[d0], s.wheelRadius) * inv0 + rollTorque(fxTyre[d1], s.wheelRadius) * inv1;
  // Gearbox and differential losses: less torque arrives when the engine
  // drives, more is taken when the wheels drive the engine.
  const bool driving = lastTc * ratio_ * driveOmega >= 0;
  const double wheelRatio = ratio_ * (driving ? s.drivelineEfficiency : 1 / s.drivelineEfficiency);
  double Tc = 0;
  double capacity = 0;
  if (ratio_ != 0) {
    if (converter) {
      // fluid coupling: capacity grows with engine speed squared, locked up when cruising
      const double sync = std::abs(driveOmega * ratio_);
      capacity = (s.auto_ ? s.auto_->converter : 0.00356) * we * we;
      if (gear_ >= 2 && sync > (s.auto_ ? s.auto_->lockOmega : 135) && shiftTimer <= 0) capacity = jsMax(capacity, s.clutchTorque);
      if (shiftTimer > 0) capacity *= 0.35;
      capacity = jsMin(capacity, s.clutchTorque * 1.1);
    } else {
      capacity = engagement * s.clutchTorque;
    }
    if (capacity > 0) {
      const double win = ratio_ * driveOmega;
      const double est = offFriction > 0 && std::abs(we) > 0.5 ? -jsSign(we) * offFriction : 0;
      const double lock = ((we - win) / h + (Te + est) / Ie + (ratio_ * S) / 2) / (1 / Ie + (ratio_ * wheelRatio * (inv0 + inv1)) / 4);
      Tc = clamp(lock, -capacity, capacity);
    }
  }
  lastTc = Tc;
  clutchLock = capacity > 0 && std::abs(Tc) < capacity * 0.999 ? 1 : capacity > 0 ? 0.5 : 0;

  we += ((Te - Tc) / Ie) * h;
  if (offFriction > 0) {
    const double need = (std::abs(we) * Ie) / h;
    if (offFriction >= need) we = 0;
    else we -= (jsSign(we) * offFriction * h) / Ie;
  }
  if (we < 0 && engineRunning) we = 0;
  if (startGrace > 0) startGrace -= h;
  if (engineRunning && startGrace <= 0 && we * RPM < s.stallRpm) {
    engineRunning = false;
    stalledEvent = true;
  }
  engineOmega = we;

  // ---- wheel rotation and brakes ----------------------------------------------
  bool absActive_ = false;
  for (int i = 0; i < 4; i++) {
    WheelState& wh = w[i];
    const bool front = i < 2;
    const double Ii = Iw * wheelImp[i];
    double T = -fxTyre[i] * s.wheelRadius;
    if (i == d0 || i == d1) T += (Tc * wheelRatio) / 2;
    wh.omega += (T / Ii) * h;
    double brake = inp.brake * s.brakeTorque * (front ? s.brakeBiasFront / 2 : (1 - s.brakeBiasFront) / 2) * absMul[i];
    if (!front) brake += inp.handbrake * (s.handbrakeTorque / 2);
    if ((i == d0 || i == d1) && parkLock) brake += 6000 * jsMax(1, s.mass / 1375);
    if (brake > 0) {
      const double need = (std::abs(wh.omega) * Ii) / h;
      if (brake >= need) wh.omega = 0;
      else wh.omega -= (jsSign(wh.omega) * brake * h) / Ii;
    }
    if (absMul[i] < 0.9) absActive_ = true;
    wh.spin += wh.omega * h;
  }
  absActive = absActive_;

  // ---- aerodynamics, integration -------------------------------------------------
  const double v2 = jsHypot(vx, vy, vz);
  const double drag = 0.5 * AIR_DENSITY * s.dragArea * v2;
  Fx -= drag * vx;
  Fy -= drag * vy;
  Fz -= drag * vz;
  vel[0] += (Fx / s.mass) * h;
  vel[1] += (Fy / s.mass) * h;
  vel[2] += (Fz / s.mass) * h;
  addAngular(Tx, Ty, Tz, h);
  // a little angular damping keeps a parked car from accumulating numerical spin
  const double ad = 1 - 0.05 * h;
  angVel[0] *= ad;
  angVel[1] *= ad;
  angVel[2] *= ad;
  const double al = jsHypot(angVel[0], angVel[1], angVel[2]);
  if (al > 25) {
    const double k = 25 / al;
    angVel[0] *= k;
    angVel[1] *= k;
    angVel[2] *= k;
  }
  pos[0] += vel[0] * h;
  pos[1] += vel[1] * h;
  pos[2] += vel[2] * h;
  auto& q = quat;
  const double ox = angVel[0];
  const double oy = angVel[1];
  const double oz = angVel[2];
  const double qx = q[0];
  const double qy = q[1];
  const double qz = q[2];
  const double qw = q[3];
  q[0] += 0.5 * h * (ox * qw + oy * qz - oz * qy);
  q[1] += 0.5 * h * (oy * qw + oz * qx - ox * qz);
  q[2] += 0.5 * h * (oz * qw + ox * qy - oy * qx);
  q[3] += 0.5 * h * (-ox * qx - oy * qy - oz * qz);
  const double ql = orOne(jsHypot(q[0], q[1], q[2], q[3]));
  q[0] /= ql;
  q[1] /= ql;
  q[2] /= ql;
  q[3] /= ql;
  odometer += std::abs(vForward) * h;
}

void Vehicle::derive(double dt) {
  updateRot();
  const auto& r = rot;
  const double fx = -r[2];
  const double fy = -r[5];
  const double fz = -r[8];
  speed = vel[0] * fx + vel[1] * fy + vel[2] * fz;
  yaw = jsAtan2(-fx, -fz);
  upright = r[4];
  rpm = engineOmega * RPM;
  if (dt > 0) {
    const double ax = (vel[0] - prevVel[0]) / dt;
    const double ay = (vel[1] - prevVel[1]) / dt;
    const double az = (vel[2] - prevVel[2]) / dt;
    const double k = jsMin(1, dt * 12);
    accLong += (ax * fx + ay * fy + az * fz - accLong) * k;
    accLat += (ax * r[0] + ay * r[3] + az * r[6] - accLat) * k;
  }
  prevVel = vel;
  // self-aligning feel: lateral utilisation of the front axle, fading as the tyres saturate
  steerTorque = clamp(-frontLat * 1.4, -1, 1);
}

}  // namespace fs

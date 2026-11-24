// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/smoothpose.ts @ v1.7.7
#include "sim/smoothpose.h"

#include <cmath>

#include "core/js.h"
#include "sim/jsmath.h"

namespace fs {

namespace {
/** the drift towards the place the way gives: over this distance driven (m) – and, standing, this time (s) */
const double PLACE_DIST = 1.4;
const double PLACE_TIME = 0.3;
/** a jump of more than this (m) is taken at once */
const double SNAP = 4;
/** the turn besides its way eases in over this distance (m) and time (s) */
const double EXTRA_DIST = 0.9;
const double EXTRA_TIME = 0.2;
/** what is left of a jump in the heading itself (where two pieces of its way meet) eases out over this distance and time */
const double YAW_DIST = 0.35;
const double YAW_TIME = 0.1;
/** the steering of the front wheels follows within this time (s), up to this angle (rad) */
const double STEER_TIME = 0.12;
const double STEER_MAX = 0.62;
}  // namespace

double smoothpose::wrapAngle(double a) {
  a = std::fmod(a + PI, PI * 2);
  if (a < 0) a += PI * 2;
  return a - PI;
}

double chordYaw(double ax, double az, double bx, double bz) {
  const double dx = bx - ax;
  const double dz = bz - az;
  if (dx * dx + dz * dz < 1e-6) return JS_NAN;
  return jsAtan2(-dx, -dz);
}

double chordYawOn(const Polyline3& pl, double s, double h) {
  PolySample sa = newPolySample();
  PolySample sb = newPolySample();
  samplePolyline(pl, s - h, sa);
  samplePolyline(pl, s + h, sb);
  return chordYaw(sa.x, sa.z, sb.x, sb.z);
}

double chordHalf(double length) { return jsMax(1.1, jsMin(3.2, length * 0.28)); }

void easePose(EasedPose& p, double rawX, double rawZ, double wayYaw, double extra, double ds, double dt, double wheelbase) {
  using smoothpose::wrapAngle;
  const double ge = 1 - jsExp(-(ds / EXTRA_DIST + dt / EXTRA_TIME));
  const double yawX = p.yawX.value_or(0) + wrapAngle(extra - p.yawX.value_or(0)) * ge;
  p.yawX = yawX;
  const double target = wayYaw + yawX;
  const double old = p.yaw;
  double d = wrapAngle(target - old);
  // (a turn of more than ~70° at once is no corner of its way: it was put somewhere else)
  if (std::abs(d) < 1.2) d *= 1 - jsExp(-(ds / YAW_DIST + dt / YAW_TIME));
  const double yaw = wrapAngle(old + d);
  // its place: on along its heading, drawn towards where its way puts it
  const double fx = -jsSin(yaw);
  const double fz = -jsCos(yaw);
  const double px = p.x + fx * ds;
  const double pz = p.z + fz * ds;
  const double ex = rawX - px;
  const double ez = rawZ - pz;
  if (ex * ex + ez * ez > SNAP * SNAP) {
    p.x = rawX;
    p.z = rawZ;
  } else {
    const double gp = 1 - jsExp(-(ds / PLACE_DIST + dt / PLACE_TIME));
    p.x = px + ex * gp;
    p.z = pz + ez * gp;
  }
  // the front wheels: the curvature driven (turn per metre) times the wheelbase (standing: as they were)
  double steer = p.steer.value_or(0);
  if (ds > 1e-3 && std::abs(d) < 1.2) {
    const double want = jsMax(-STEER_MAX, jsMin(STEER_MAX, -jsAtan((wheelbase * d) / ds)));
    steer += (want - steer) * (1 - jsExp(-dt / STEER_TIME));
  }
  p.steer = steer;
  p.yaw = yaw;
}

}  // namespace fs

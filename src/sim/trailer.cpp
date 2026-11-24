// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/trailer.ts @ v1.7.7
#include "sim/trailer.h"

#include <cmath>

#include "core/geom.h"
#include "core/js.h"
#include "sim/jsmath.h"

namespace fs {

TrailerOutline trailerOutline(const TrailerSpec& t) {
  return {t.hitchToAxle - t.bodyFront, t.length - t.hitchToAxle, t.width / 2, t.hitchToAxle};
}

namespace {
const double G = 9.81;
}  // namespace

Trailer::Trailer(const TrailerSpec& spec_, Ground& ground_) : spec(spec_), ground(ground_) {}

double Trailer::hitchHeight() const { return (spec.hitch ? *spec.hitch : TRUCK_HITCH).height; }

void Trailer::hitchPoint(const Vehicle& v, double* out) const {
  const TruckHitch& h = spec.hitch ? *spec.hitch : TRUCK_HITCH;
  const auto& mo = v.modelOrigin;
  v.toWorld(mo[0], mo[1] + h.height, mo[2] + v.spec.wheelbase / 2 + h.behindRearAxle, out);
}

void Trailer::truckHitch(const Vehicle& truck, double* out) {
  const auto& mo = truck.modelOrigin;
  truck.toWorld(mo[0], mo[1] + TRUCK_HITCH.height, mo[2] + truck.spec.wheelbase / 2 + TRUCK_HITCH.behindRearAxle, out);
}

void Trailer::place(double x_, double z_, double yaw_) {
  coupled = false;
  parked = true;
  x = x_;
  z = z_;
  yaw = yaw_;
  speed = 0;
  accel = 0;
  settle();
}

void Trailer::coupleBehind(const Vehicle& truck) {
  hitchPoint(truck, hit.data());
  const double L = spec.hitchToAxle;
  yaw = truck.yaw;
  x = hit[0] + jsSin(truck.yaw) * L;
  z = hit[2] + jsCos(truck.yaw) * L;
  coupled = true;
  parked = false;
  speed = truck.speed;
  accel = 0;
  articulation = 0;
  settle(hit[1]);
}

void Trailer::couple() {
  coupled = true;
  accel = 0;
}

void Trailer::uncouple() {
  coupled = false;
  parked = true;
  speed = 0;
  settle();
}

Trailer::Offset Trailer::offsetFrom(const Vehicle& truck) {
  hitchPoint(truck, hit.data());
  const double d = jsHypot(hit[0] - hitchX, hit[2] - hitchZ);
  double a = truck.yaw - yaw;
  a -= PI * 2 * jsRound(a / (PI * 2));
  return {d, a};
}

void Trailer::settle(std::optional<double> hitchY_) {
  const double L = spec.hitchToAxle;
  ground.sample(x, z, y + 1, sample);
  y = sample.y;
  hitchX = x - jsSin(yaw) * L;
  hitchZ = z - jsCos(yaw) * L;
  if (!hitchY_) {
    // standing alone it rests on its support leg: level with the ground under the drawbar
    ground.sample(hitchX, hitchZ, y + 1, sample);
    hitchY_ = sample.y + hitchHeight();
  }
  hitchY = *hitchY_;
  pitch = jsAtan2(*hitchY_ - hitchHeight() - y, L);
}

void Trailer::step(double dt, Vehicle& truck, const DriverInput& input) {
  if (!coupled) return;
  const TrailerSpec& s = spec;
  const double L = s.hitchToAxle;
  hitchPoint(truck, hit.data());
  const double hx = hit[0];
  const double hz = hit[2];
  // The axle group cannot move sideways.  So it moves along the line to the coupling: the
  // trailer points from where its axle group was to where the coupling is now, and the axle
  // group is the drawbar's length behind the coupling.
  double ux = hx - x;
  double uz = hz - z;
  double l = jsHypot(ux, uz);
  if (l == 0 || l != l) l = 1;
  ux /= l;
  uz /= l;
  const double nx = hx - ux * L;
  const double nz = hz - uz * L;
  const double moved = (nx - x) * ux + (nz - z) * uz;
  x = nx;
  z = nz;
  yaw = jsAtan2(-ux, -uz);
  const double v = moved / dt;
  // its inertia: what it takes to change its speed (smoothed – the truck feels it one step late)
  accel += (clamp((v - speed) / dt, -30, 30) - accel) * 0.25;
  speed = v;
  wheelSpin += moved / s.wheelRadius;
  settle(hit[1]);
  double a = truck.yaw - yaw;
  a -= PI * 2 * jsRound(a / (PI * 2));
  articulation = a;

  // ---- what the truck feels at the coupling, along the trailer's axis (forwards positive)
  ground.sample(x, z, y + 1, sample);
  // downhill force: the part of gravity along the axis (the ground normal leans the way it falls)
  const double slope = sample.nx * ux + sample.nz * uz;
  double force = -s.mass * accel + s.mass * G * slope;
  const double dirOfTravel = std::abs(v) > 0.02 ? jsSign(v) : 0;
  const double braking = clamp(input.brake, 0, 1) * s.brake + (input.handbrake > 0.5 ? s.brake * 0.6 : 0);
  const double resist = s.mass * (G * s.rollingResistance * sample.rolling + braking);
  if (dirOfTravel != 0) {
    force -= dirOfTravel * resist;
  } else {
    // standing: brakes and rolling resistance hold against what pushes, up to what they can
    force -= clamp(force, -resist, resist);
  }
  truck.applyImpulse(hx, hit[1], hz, ux * force * dt, 0, uz * force * dt);
}

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The centre-axle trailer (origin: src/sim/trailer.ts @ ff37c5d, v0.7.0).
 */
#include "sim/trailer.h"

#include <cmath>

#include "sim/vehicle/vehicle.h"

namespace fs {

const TrailerSpec TANDEM_TRAILER = {6000, 5.4, 1.5, 7.9, 2.5, 3.6, 1.3, 2.04, 0.4, 6.2, 0.008};

namespace {
constexpr double G = 9.81;
double clampD(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
}  // namespace

void Trailer::truckHitch(const Vehicle& truck, double out[3]) {
  const double* mo = truck.modelOrigin;
  truck.toWorld(mo[0], mo[1] + TRUCK_HITCH.height,
                mo[2] + truck.spec.wheelBase / 2 + TRUCK_HITCH.behindRearAxle, out);
}

void Trailer::place(double xIn, double zIn, double yawIn) {
  coupled = false;
  parked = true;
  x = xIn;
  z = zIn;
  yaw = yawIn;
  speed = 0;
  accel_ = 0;
  settle();
}

void Trailer::coupleBehind(const Vehicle& truck) {
  truckHitch(truck, hit_);
  const double L = spec.hitchToAxle;
  yaw = truck.yaw;
  x = hit_[0] + std::sin(truck.yaw) * L;
  z = hit_[2] + std::cos(truck.yaw) * L;
  coupled = true;
  parked = false;
  speed = truck.speed;
  accel_ = 0;
  articulation = 0;
  settle(hit_[1]);
}

void Trailer::uncouple() {
  coupled = false;
  parked = true;
  speed = 0;
  settle();
}

void Trailer::offsetFrom(const Vehicle& truck, double& dist, double& angle) const {
  double hit[3];
  truckHitch(truck, hit);
  dist = std::hypot(hit[0] - hitchX, hit[2] - hitchZ);
  double a = truck.yaw - yaw;
  a -= M_PI * 2 * std::round(a / (M_PI * 2));
  angle = a;
}

void Trailer::settle(double hitchYIn) {
  const double L = spec.hitchToAxle;
  ground.sample(x, z, y + 1, sample_);
  y = sample_.y;
  hitchX = x - std::sin(yaw) * L;
  hitchZ = z - std::cos(yaw) * L;
  double hitchY = hitchYIn;
  if (std::isnan(hitchY)) {
    // standing alone it rests on its support leg: level with the ground under the drawbar
    ground.sample(hitchX, hitchZ, y + 1, sample_);
    hitchY = sample_.y + TRUCK_HITCH.height;
  }
  this->hitchY = hitchY;
  pitch = std::atan2(hitchY - TRUCK_HITCH.height - y, L);
}

void Trailer::step(double dt, const Vehicle& truck, const DriverInput& input) {
  if (!coupled) return;
  const TrailerSpec& s = spec;
  const double L = s.hitchToAxle;
  double hit[3];
  truckHitch(truck, hit);
  const double hx = hit[0];
  const double hz = hit[2];
  // The axle group cannot move sideways.  So it moves along the line to the coupling: the
  // trailer points from where its axle group was to where the coupling is now, and the axle
  // group is the drawbar's length behind the coupling.
  double ux = hx - x;
  double uz = hz - z;
  const double l = std::hypot(ux, uz);
  if (l > 0) {
    ux /= l;
    uz /= l;
  }
  const double nx = hx - ux * L;
  const double nz = hz - uz * L;
  const double moved = (nx - x) * ux + (nz - z) * uz;
  x = nx;
  z = nz;
  yaw = std::atan2(-ux, -uz);
  const double v = moved / dt;
  // its inertia: what it takes to change its speed (smoothed – the truck feels it one step late)
  accel_ += (clampD((v - speed) / dt, -30, 30) - accel_) * 0.25;
  speed = v;
  wheelSpin += moved / s.wheelRadius;
  settle(hit_[1]);
  double a = truck.yaw - yaw;
  a -= M_PI * 2 * std::round(a / (M_PI * 2));
  articulation = a;

  // ---- what the truck feels at the coupling, along the trailer's axis (forwards positive)
  ground.sample(x, z, y + 1, sample_);
  // downhill force: the part of gravity along the axis (the ground normal leans the way it falls)
  const double slope = sample_.nx * ux + sample_.nz * uz;
  double force = -s.mass * accel_ + s.mass * G * slope;
  const double dirOfTravel = std::fabs(v) > 0.02 ? (v > 0 ? 1.0 : -1.0) : 0.0;
  const double braking =
      clampD(input.brake, 0, 1) * s.brake + (input.handbrake > 0.5 ? s.brake * 0.6 : 0.0);
  const double resist = s.mass * (G * s.rollingResistance * sample_.rolling + braking);
  if (dirOfTravel != 0) {
    force -= dirOfTravel * resist;
  } else {
    // standing: brakes and rolling resistance hold against what pushes, up to what they can
    force -= clampD(force, -resist, resist);
  }
  const_cast<Vehicle&>(truck).applyImpulse(hx, hit_[1], hz, ux * force * dt, 0, uz * force * dt);
}

}  // namespace fs

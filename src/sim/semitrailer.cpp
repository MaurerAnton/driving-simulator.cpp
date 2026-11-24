// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/semitrailer.ts @ v1.7.7
#include "sim/semitrailer.h"

#include <cmath>

#include "core/js.h"
#include "sim/jsmath.h"

namespace fs {

namespace {
/** how fast the landing gear is wound up or down (share of its travel per second – the driver cranks it in a few seconds) */
const double LEG_RATE = 0.5;
const double G = 9.81;
double clampTo(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
}  // namespace

const SemiTrailerSpec* isSemiSpec(const TrailerSpec* t) {
  return t == &SEMI_TRAILER ? &SEMI_TRAILER : nullptr;
}

double steadyAxleRadius(double hitchRadius, double hitchToAxle) {
  return std::sqrt(std::max(0.0, hitchRadius * hitchRadius - hitchToAxle * hitchToAxle));
}

SemiTrailer::SemiTrailer(const SemiTrailerSpec& spec_, Ground& ground_) : Trailer(spec_, ground_), semi(spec_) {}

double SemiTrailer::kingpinLoad() const { return coupled ? semi.mass * G * semi.kingpinShare : 0; }

void SemiTrailer::place(double x_, double z_, double yaw_) {
  Trailer::place(x_, z_, yaw_);
  legsDown = true;
  legs = 1;
  parkBrake = true;
  supply = control = cables = false;
  jackknifed = false;
}

void SemiTrailer::coupleBehind(const Vehicle& truck) {
  Trailer::coupleBehind(truck);
  legsDown = false;
  legs = 0;
  parkBrake = false;
  supply = control = cables = true;
  jackknifed = false;
}

void SemiTrailer::step(double dt, Vehicle& truck, const DriverInput& input) {
  legs += clampTo((legsDown ? 1 : 0) - legs, -LEG_RATE * dt, LEG_RATE * dt);
  jackknifeEvent = false;
  if (!coupled) return;
  // (its spring brakes: as if the brake pedal were held down)
  const DriverInput* inp = &input;
  if (braked()) {
    held = input;
    held.brake = 1;
    inp = &held;
  }
  Trailer::step(dt, truck, *inp);

  // ---- it cannot swing round further than the cab and the frame let it
  const double a = articulation;
  if (std::abs(a) > JACKKNIFE_LIMIT) {
    const double lim = jsSign(a) * JACKKNIFE_LIMIT;
    const double L = spec.hitchToAxle;
    // the trailer is pushed round about the kingpin to the limit
    yaw = truck.yaw - lim;
    x = hit[0] + jsSin(yaw) * L;
    z = hit[2] + jsCos(yaw) * L;
    settle(hit[1]);
    articulation = lim;
    if (!jackknifed) jackknifeEvent = true;
    jackknifed = true;
  } else if (std::abs(a) < JACKKNIFE_LIMIT - 0.15) jackknifed = false;

  // ---- its weight on the fifth wheel, and so on the tractor's rear axle
  const double load = kingpinLoad() * jsCos(pitch);
  truck.applyImpulse(hit[0], hit[1], hit[2], 0, -load * dt, 0);
}

std::unique_ptr<Trailer> makeTrailer(const TrailerSpec& spec, Ground& ground) {
  if (const SemiTrailerSpec* s = isSemiSpec(&spec)) return std::make_unique<SemiTrailer>(*s, ground);
  return std::make_unique<Trailer>(spec, ground);
}

}  // namespace fs

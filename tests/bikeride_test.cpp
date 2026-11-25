// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim game world core platform geo
// Origin: tests/vehicle/bikeride.test.ts @ v1.7.7
/**
 * Mopeds and motorcycles over uneven ground.  A player found the moped
 * jumping and flipping "like a spring" on anything but a flat road: the
 * bump stop at the end of the suspension's travel was a car's (260 kN/m,
 * undamped), which off a kerb handed a 200 kg machine its blow straight
 * back and threw it a metre into the air.  Here each machine is ridden at a
 * steady speed over kerbs, a ramp with its crest, a cross slope, steps in
 * the ground and potholes: it must stay on its wheels, never rise more than
 * a few centimetres off the ground and keep going.
 */
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "sim/vehicle/fleet.h"
#include "sim/vehicle/index.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using namespace fs::harness;

namespace {

using Height = std::function<double(double x, double z)>;

/** A ground given by its height; the normal from small differences. */
class Profile : public Ground {
 public:
  Height f;
  explicit Profile(Height f_) : f(std::move(f_)) {}
  void sample(double x, double z, double, GroundSample& out) override {
    const double e = 0.02;
    const double y = f(x, z);
    const double nx = -(f(x + e, z) - y) / e;
    const double nz = -(f(x, z + e) - y) / e;
    const double l = jsHypot(nx, 1, nz);
    out.y = y;
    out.nx = nx / l;
    out.ny = 1 / l;
    out.nz = nz / l;
    out.grip = 1;
    out.rolling = 1;
  }
};

/** distance along the way (it rides north, towards −z) into the uneven part, which begins 25 m ahead */
double into(double z) { return -z - 25; }
double smooth(double t) { return t <= 0 ? 0 : t >= 1 ? 1 : t * t * (3 - 2 * t); }

const std::vector<std::pair<std::string, Height>> PROFILES = {
    {"a kerb up (12 cm)", [](double, double z) { return into(z) > 0 ? 0.12 : 0; }},
    {"a kerb down (12 cm)", [](double, double z) { return into(z) > 0 ? -0.12 : 0; }},
    // 10 % up over 12 m, a crest rounded over 14 m (gentle enough to stay on at 70 km/h), 10 % down
    {"a ramp and its crest",
     [](double, double z) {
       const double d = into(z);
       if (d <= 0) return 0.0;
       if (d < 12) return d * 0.1;
       if (d < 26) return 1.2 + 0.1 * (d - 12) - (0.2 / 28) * std::pow(d - 12, 2);
       return std::max(0.0, 1.2 - 0.1 * (d - 26));
     }},
    // the road falls 10 % to the side, and a 10 cm step across it
    {"a cross slope with a step", [](double x, double z) { return 0.1 * x * smooth(into(z) / 5) + (into(z) > 15 ? 0.1 : 0); }},
    // terrain tiles: steps of 6 cm along and 4 cm across every 2 m
    {"steps in the ground", [](double x, double z) { return into(z) <= 0 ? 0 : std::floor(into(z) / 2) * 0.06 + std::floor((x + 1) / 2) * 0.04; }},
    // 8 cm deep, 1.5 m long, every 4.5 m
    {"potholes", [](double, double z) { return into(z) > 0 && jsMod(std::floor(into(z) / 1.5), 3) == 1 ? -0.08 : 0; }},
    // rolling field: waves of 5 cm, 12 m long, short ripples of 2 cm and a tilt to the side
    {"a bumpy field",
     [](double x, double z) { return smooth(into(z) / 4) * (0.05 * std::sin(into(z) * 0.52) + 0.02 * std::sin(into(z) * 4.1 + x) + 0.04 * x); }},
};

struct Ride {
  double minUpright;
  /** highest the body rose above the ground under its wheels (m) */
  double rise;
  /** longest time with no wheel on the ground (s) */
  double airborne;
  double kmh;
};

/** Ride `spec` north at `kmh` over `f` for 9 s; a simple rider holds the speed. */
Ride ride(const VehicleSpec& spec, const Height& f, double kmh) {
  Profile g(f);
  Vehicle v(spec, g);
  v.teleport(0, 0, 0, 0);
  v.resetDrivetrain(true);
  // start rolling at the speed already, so that every machine reaches the uneven part as fast as asked
  v.vel[2] = -kmh / 3.6;
  for (WheelState& w : v.wheels) w.omega = kmh / 3.6 / spec.wheelRadius;
  double p[3] = {0, 0, 0};
  double wf[3] = {0, 0, 0};
  double wr[3] = {0, 0, 0};
  double minUpright = 1;
  double rise = 0;
  double air = 0;
  double airborne = 0;
  // the ground it could be above without having jumped: off a drop a body falls freely, the reference with it
  double ref = 0;
  double refV = 0;
  const int n = static_cast<int>(jsRound(9 / DT));
  for (int i = 0; i < n; i++) {
    const double err = kmh - v.speed * 3.6;
    v.step(DT, DriverInput{.throttle = std::max(0.0, std::min(1.0, err * 0.3)), .brake = std::max(0.0, std::min(1.0, -err * 0.1)), .gear = 1});
    minUpright = std::min(minUpright, v.upright);
    // the ground under the middle of each axle: the body's height is measured above the higher of the two
    v.getModelPosition(p);
    v.toWorld(0, -spec.cgHeight, -spec.cgToFront, wf);
    v.toWorld(0, -spec.cgHeight, spec.wheelbase - spec.cgToFront, wr);
    const double under = std::max({f(wf[0], wf[2]), f(wr[0], wr[2]), f(p[0], p[2])});
    refV -= 9.81 * DT;
    ref = std::max(under, ref + refV * DT);
    if (ref == under) refV = std::max(0.0, refV);
    if (i * DT > 0.5) rise = std::max(rise, p[1] - ref);
    if (std::any_of(v.wheels.begin(), v.wheels.end(), [](const WheelState& w) { return w.contact; })) air = 0;
    else airborne = std::max(airborne, (air += DT));
  }
  return {minUpright, rise, airborne, v.speed * 3.6};
}

struct Machine {
  std::string who;
  const VehicleSpec* spec;
  std::vector<double> speeds;
};

}  // namespace

int main() {
  const std::vector<Machine> MACHINES = {
      {"the moped", &MOPED.auto_, {20, 44}},
      {"the 125", &MOTORCYCLE_125.manual, {30, 60}},
      {"the big motorcycle", &MOTORCYCLE_AUTOMATIC, {30, 70}},
  };
  for (const Machine& m : MACHINES) {
    const std::string name = m.who + " rides over kerbs, crests, slopes and holes without jumping or tipping";
    test(name.c_str());
    for (const auto& [what, f] : PROFILES) {
      for (const double kmh : m.speeds) {
        const Ride r = ride(*m.spec, f, kmh);
        const std::string at = m.who + ", " + what + ", " + jsStr(kmh) + " km/h";
        ok(r.minUpright > 0.9, at + ": tipped to upright " + toFixed(r.minUpright, 3));
        ok(r.rise < 0.08, at + ": rose " + toFixed(r.rise, 3) + " m off the ground");
        ok(r.airborne < 0.15, at + ": " + toFixed(r.airborne, 2) + " s with no wheel on the ground");
        // (it keeps going: over the steps the moped's 3 kW lose some speed, as they would)
        ok(r.kmh > kmh * 0.4 && r.kmh < kmh + 8, at + ": came out at " + toFixed(r.kmh, 1) + " km/h");
      }
    }
  }

  test("the drawn lean follows the bend, not the bumps");
  {
    // 50 km/h round a bend of 50 m: v²/r = 3.86 m/s², about 21°
    const double v = 50 / 3.6;
    ok(std::abs(bikeLean(v, -v / 50) - std::atan(v * v / 50 / 9.81)) < 1e-9, "the lean of 50 km/h round 50 m");
    ok(bikeLean(v, v / 50) < 0, "a left bend leans to the left");
    ok(bikeLean(v, 0) == 0, "no bend, no lean");
    ok(bikeLean(JS_NAN, 1) == 0, "NaN: upright");
    ok(bikeLean(40, 10) > -0.7, "at most 39°");
  }
  std::printf("bikeride_test: ok\n");
  return 0;
}

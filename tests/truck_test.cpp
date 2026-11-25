// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim game world core platform geo
// Origin: tests/vehicle/truck.test.ts @ v1.7.7
/**
 * The truck and its trailer.  Expectations from how a 12 t distribution
 * truck with a 6 t centre-axle trailer behaves: slow to get going, long to
 * stop, the trailer cuts the corner when pulled and breaks out when pushed.
 */
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include "sim/trailer.h"
#include "sim/vehicle/index.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using namespace fs::harness;

namespace {

/** A truck, with its trailer coupled in line behind it if wanted. */
struct Rig {
  PlaneGround ground;
  Vehicle truck;
  Trailer trailer;
  explicit Rig(double grade = 0, bool withTrailer = true) : ground(grade), truck(makeCar(ground, TRUCK)), trailer(TANDEM_TRAILER, ground) {
    if (withTrailer) trailer.coupleBehind(truck);
  }
};

void drive(Rig& r, double seconds, const std::function<DriverInput(double t)>& d, const std::function<void(double t)>& each = nullptr) {
  const int n = static_cast<int>(jsRound(seconds / DT));
  for (int i = 0; i < n; i++) {
    const DriverInput inp = d(i * DT);
    r.truck.step(DT, inp);
    r.trailer.step(DT, r.truck, inp);
    if (each) each((i + 1) * DT);
  }
}

void alone() {
  test("the truck alone: stands, creeps, pulls, stops");
  Rig r(0, false);
  drive(r, 5, [](double) { return DriverInput{.handbrake = 1, .gear = -2}; });
  ok(std::abs(r.truck.speed) < 0.001, "stands");
  between(r.truck.rpm, 560, 640, "idle");
  const auto& w = r.truck.wheels;
  between((w[0].load + w[1].load) / (w[0].load + w[1].load + w[2].load + w[3].load), 0.38, 0.42, "front axle share");
  drive(r, 10, [](double) { return DriverInput{.gear = 1}; });
  between(kmh(r.truck), 2, 5, "creeps in D at idle");
  double t50 = -1;
  std::vector<int> shifts;
  drive(r, 40, [](double) { return DriverInput{.throttle = 1, .gear = 1}; }, [&](double t) {
    if (t50 < 0 && kmh(r.truck) >= 50) t50 = t;
    if (shifts.empty() || shifts.back() != r.truck.gear) shifts.push_back(r.truck.gear);
  });
  between(t50, 5, 12, "seconds to 50 km/h without a trailer");
  for (size_t i = 1; i < shifts.size(); i++) ok(shifts[i] > shifts[i - 1], "the gearbox does not hunt");
  between(r.truck.rpm, 900, 2100, "a diesel: never far above 2000 rpm");
  const double v0 = r.truck.speed;
  const double z0 = r.truck.pos[2];
  double stop = -1;
  drive(r, 10, [](double) { return DriverInput{.brake = 1, .gear = 1}; }, [&](double t) {
    if (stop < 0 && std::abs(r.truck.speed) < 0.1) stop = t;
  });
  const double decel = (v0 * v0) / (2 * std::abs(r.truck.pos[2] - z0));
  between(decel, 5.5, 7.8, "full braking (m/s²)");
}

void withTrailer() {
  test("with the trailer: slower to get going, the trailer follows in line");
  Rig solo(0, false);
  Rig combo;
  double tSolo = -1;
  double tCombo = -1;
  drive(solo, 30, [](double) { return DriverInput{.throttle = 1, .gear = 1}; }, [&](double t) {
    if (tSolo < 0 && kmh(solo.truck) >= 50) tSolo = t;
  });
  drive(combo, 30, [](double) { return DriverInput{.throttle = 1, .gear = 1}; }, [&](double t) {
    if (tCombo < 0 && kmh(combo.truck) >= 50) tCombo = t;
  });
  ok(tCombo > tSolo * 1.35 && tCombo < tSolo * 2, "the trailer's 6 t are felt: " + toFixed(tSolo, 1) + " s alone, " + toFixed(tCombo, 1) + " s with it");
  Vehicle& truck = combo.truck;
  Trailer& trailer = combo.trailer;
  ok(std::abs(trailer.articulation) < 0.01, "in line");
  between(trailer.speed / truck.speed, 0.99, 1.01, "same speed");
  // the axle group is the drawbar's length behind the coupling
  double hitch[3] = {0, 0, 0};
  Trailer::truckHitch(truck, hitch);
  between(jsHypot(hitch[0] - trailer.x, hitch[2] - trailer.z), TANDEM_TRAILER.hitchToAxle - 0.01, TANDEM_TRAILER.hitchToAxle + 0.01, "drawbar length");
  between(hitch[1] - trailer.y, TRUCK_HITCH.height - 0.15, TRUCK_HITCH.height + 0.15, "coupling height");
  // braking: the trailer brakes itself and does not shove the truck
  const double v0 = truck.speed;
  const double z0 = truck.pos[2];
  drive(combo, 12, [](double) { return DriverInput{.brake = 1, .gear = 1}; });
  ok(std::abs(truck.speed) < 0.05, "stopped");
  const double decel = (v0 * v0) / (2 * std::abs(truck.pos[2] - z0));
  between(decel, 5, 7.6, "full braking of the combination (m/s²)");
  ok(std::abs(trailer.articulation) < 0.02, "still in line after braking");
}

void slope() {
  test("on a slope: the handbrake holds truck and trailer, without it they roll back");
  Rig r(0.1);
  drive(r, 8, [](double) { return DriverInput{.handbrake = 1, .gear = 0}; });
  ok(std::abs(r.truck.speed) < 0.01, "held: " + jsStr(r.truck.speed));
  drive(r, 4, [](double) { return DriverInput{.gear = 0}; });
  ok(r.truck.speed < -0.8, "rolls back: " + jsStr(r.truck.speed));
}

void bend() {
  test("pulled round a bend the trailer cuts the corner");
  Rig r;
  drive(r, 6, [](double) { return DriverInput{.throttle = 0.5, .gear = 1}; });
  // a steady left turn at walking pace
  const auto turn = [&](double) { return DriverInput{.steer = -0.6, .throttle = kmh(r.truck) < 12 ? 0.35 : 0.0, .gear = 1}; };
  drive(r, 40, turn);
  // both turn about the same centre at the same rate: the radius of each path is its speed over that rate
  const double yaw0 = r.truck.yaw;
  double turned = 0;
  double last = yaw0;
  double vTruck = 0;
  double vTrailer = 0;
  int n = 0;
  drive(r, 3, turn, [&](double) {
    double d = r.truck.yaw - last;
    d -= PI * 2 * jsRound(d / (PI * 2));
    turned += d;
    last = r.truck.yaw;
    vTruck += r.truck.speed;
    vTrailer += r.trailer.speed;
    n++;
  });
  ok(turned > 0.5, "turning left: " + toFixed(turned, 2) + " rad in 3 s");
  const double rate = turned / 3;
  const double rTruck = vTruck / n / rate;
  const double rTrailer = vTrailer / n / rate;
  between(rTruck, 6, 30, "radius of the truck's circle (m)");
  ok(rTrailer < rTruck - 1, "the trailer's axle runs well inside the truck's track: " + toFixed(rTrailer, 2) + " m against " + toFixed(rTruck, 2) + " m");
  between(std::abs(r.trailer.articulation), 0.15, 1.3, "steady angle between truck and trailer");
  ok(r.trailer.articulation > 0, "turning left, the truck points left of the trailer");
}

void pushed() {
  test("pushed backwards the trailer breaks out – unless the driver steers against it");
  // hands off the wheel: a small angle grows
  Rig r;
  r.trailer.x += 0.15;  // the axle group 15 cm out of line
  drive(r, 1, [](double) { return DriverInput{.brake = 1, .handbrake = 1, .gear = -1}; });
  const double start = std::abs(r.trailer.articulation);
  between(start, 0.02, 0.04, "a small angle to begin with");
  drive(r, 14, [&](double) { return DriverInput{.throttle = kmh(r.truck) > -5 ? 0.3 : 0.0, .gear = -1}; });
  ok(r.truck.speed < -0.5, "reversing");
  ok(std::abs(r.trailer.articulation) > 0.3, "the angle grew from " + toFixed(start, 3) + " to " + toFixed(std::abs(r.trailer.articulation), 3));

  // a driver who turns the wheel against the angle as soon as it shows keeps the trailer in line
  Rig k;
  k.trailer.x += 0.15;
  double worst = 0;
  drive(k, 30, [&](double) { return DriverInput{.steer = jsMax(-1, jsMin(1, -k.trailer.articulation * 6)), .throttle = kmh(k.truck) > -4 ? 0.3 : 0.0, .gear = -1}; }, [&](double t) {
    if (t > 6) worst = jsMax(worst, std::abs(k.trailer.articulation));
  });
  ok(k.truck.pos[2] > 15, "reversed a good way: " + toFixed(k.truck.pos[2], 1) + " m");
  ok(worst < 0.1, "kept in line: " + toFixed(worst, 3));
}

void coupling() {
  test("coupling: the trailer stands by itself until the truck backs up to it");
  Rig r(0, false);
  // the trailer stands 12 m behind the truck's coupling, in line
  double hitch[3] = {0, 0, 0};
  Trailer::truckHitch(r.truck, hitch);
  r.trailer.place(hitch[0], hitch[2] + 12 + TANDEM_TRAILER.hitchToAxle, 0);
  ok(r.trailer.coupled == false, "r.trailer.coupled == false");
  const Trailer::Offset before = r.trailer.offsetFrom(r.truck);
  between(before.dist, 11.9, 12.1, "distance to the coupling eye");
  drive(r, 6, [](double) { return DriverInput{.throttle = 0.6, .gear = 1}; });
  drive(r, 5, [](double) { return DriverInput{.brake = 1, .gear = 1}; });
  ok(std::abs(r.trailer.x) < 1e-9 && r.trailer.speed == 0, "an uncoupled trailer stays where it is");
  // back up to it
  double t = 0;
  while (r.trailer.offsetFrom(r.truck).dist > 0.2 && t < 60) {
    const double far = r.trailer.offsetFrom(r.truck).dist;
    // quickly at first, at a crawl for the last metres
    const DriverInput inp{.throttle = kmh(r.truck) > -(far > 3 ? 5 : 1.2) ? 0.25 : 0.0, .brake = kmh(r.truck) < -(far > 3 ? 6 : 1.6) ? 0.3 : 0.0, .gear = -1};
    r.truck.step(DT, inp);
    r.trailer.step(DT, r.truck, inp);
    t += DT;
  }
  const Trailer::Offset at = r.trailer.offsetFrom(r.truck);
  ok(at.dist <= 0.2 && std::abs(at.angle) < 0.05, "backed up to the eye: " + toFixed(at.dist, 2) + " m, " + toFixed(at.angle, 3) + " rad");
  r.trailer.couple();
  drive(r, 2, [](double) { return DriverInput{.brake = 1, .gear = 1}; });
  drive(r, 12, [](double) { return DriverInput{.throttle = 0.7, .gear = 1}; });
  ok(r.trailer.coupled && r.trailer.speed > 3 && std::abs(r.trailer.articulation) < 0.05, "coupled, it comes along");
  r.trailer.uncouple();
  const double x = r.trailer.x;
  const double z = r.trailer.z;
  drive(r, 5, [](double) { return DriverInput{.throttle = 0.7, .gear = 1}; });
  ok(r.trailer.x == x && r.trailer.z == z && r.trailer.parked, "uncoupled, it stays behind on its own brake");
}

}  // namespace

int main() {
  std::printf("truck_test (tests/vehicle/truck.test.ts)\n");
  alone();
  withTrailer();
  slope();
  bend();
  pushed();
  coupling();
  std::printf("truck_test: all 6 cases passed\n");
  return 0;
}

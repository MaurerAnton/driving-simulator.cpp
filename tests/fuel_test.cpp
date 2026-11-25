// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim core
// Origin: tests/sim/fuel.test.ts @ v1.7.7
/**
 * The fuel model: what the engines burn, driven with the real vehicle
 * dynamics at a steady speed on level ground (a speed controller on the
 * throttle) – realistic figures per 100 km for a car, a truck (empty and
 * loaded), a bus, a tractor, motorcycles; idling, coasting with the fuel
 * cut off; the tank, the pump, the load in a truck's box.
 */
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

#include "sim/fuel.h"
#include "sim/payload.h"
#include "sim/vehicle/fleet.h"
#include "sim/vehicle/index.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using namespace fs::harness;

namespace {

struct Cruise {
  double per100;
  double perHour;
};

using GearFor = std::function<int(double)>;

/**
 * Drive at `kmh` on level ground and measure the consumption (l/100 km)
 * over the last 30 s, after 50 s to settle.  `gearFor`: the gear for a
 * speed (km/h) – a manual gearbox with the clutch working itself; for an
 * automatic: D.
 */
Cruise cruise(const VehicleSpec& spec, const TankSpec& tank, double kmh, GearFor gearFor = [](double) { return 1; }) {
  PlaneGround ground;
  Vehicle v = makeCar(ground, spec);
  const double target = kmh / 3.6;
  double integral = 0;
  double litres = 0;
  double metres = 0;
  const double settle = 50;
  const double measure = 30;
  const int steps = static_cast<int>(jsRound((settle + measure) / DT));
  for (int i = 0; i < steps; i++) {
    const double err = target - v.speed;
    integral = std::max(-0.2, std::min(1.0, integral + err * DT * 0.08));
    const double throttle = std::max(0.0, std::min(1.0, err * 0.6 + integral));
    v.step(DT, DriverInput{.throttle = throttle, .gear = gearFor(v.speed * 3.6)});
    if (i * DT >= settle) {
      litres += fuelFlow(v.spec, tank, v.rpm, v.engineLoad, v.engineRunning) * DT;
      metres += std::abs(v.speed) * DT;
    }
  }
  ok(std::abs(v.speed * 3.6 - kmh) < 3, "the controller holds " + jsStr(kmh) + " km/h (" + toFixed(v.speed * 3.6, 1) + ")");
  return {(litres / metres) * 100000, (litres / measure) * 3600};
}

/**
 * A driver's gear for the speed (km/h): up a gear above each of `ups`, down
 * again only 7 km/h below it (or a shift that costs a little speed would
 * hunt between two gears).
 */
GearFor shifter(std::vector<double> ups) {
  auto gear = std::make_shared<int>(1);
  return [gear, ups](double kmh) {
    int& g = *gear;
    while (g <= static_cast<int>(ups.size()) && kmh > ups[g - 1]) g++;
    while (g > 1 && kmh < ups[g - 2] - 7) g--;
    return g;
  };
}

/** a car's gears: up early, as one drives to save fuel */
GearFor carGear() { return shifter({18, 32, 45, 62, 80}); }

const TankSpec& tank(const char* kind) {
  const TankSpec* t = tankFor(kind);
  ok(t != nullptr, kind);
  return *t;
}

}  // namespace

int main() {
  test("the full-load torque follows the torque curve");
  ok(fullTorque(TRUCK, 1300) == 1050, "1300");
  ok(fullTorque(TRUCK, 2700) == 0, "2700");
  ok(std::abs(fullTorque(TRUCK, 750) - 860) < 1, "750");

  test("a compact car burns 5 to 8 l/100 km at 100 km/h, 3.5 to 6.5 at a steady 50");
  {
    const TankSpec& t = tank("car");
    const Cruise fast = cruise(COMPACT_CAR_AUTOCLUTCH, t, 100, carGear());
    const Cruise town = cruise(COMPACT_CAR_AUTOCLUTCH, t, 50, carGear());
    ok(fast.per100 > 5 && fast.per100 < 8, "100 km/h: " + toFixed(fast.per100, 2) + " l/100 km");
    ok(town.per100 > 3.5 && town.per100 < 6.5, "50 km/h: " + toFixed(town.per100, 2) + " l/100 km");
    // the weaker engine of the driving school's car is no thirstier
    const Cruise small = cruise(withPower(COMPACT_CAR_AUTOCLUTCH, 75), t, 100, carGear());
    ok(small.per100 > 4.5 && small.per100 < 8, "75 PS at 100 km/h: " + toFixed(small.per100, 2) + " l/100 km");
    std::printf("    car: %.1f l/100 km at 100 km/h, %.1f at 50; with 75 PS %.1f at 100\n", fast.per100, town.per100, small.per100);
  }

  test("a 12-t truck burns 18 to 32 l/100 km at 80 km/h – more with 2.4 t of goods");
  {
    const TankSpec& t = tank("truck");
    const Cruise empty = cruise(TRUCK, t, 80);
    ok(empty.per100 > 18 && empty.per100 < 32, "empty: " + toFixed(empty.per100, 1) + " l/100 km");
    const Payload load{.mass = 2400, .z = 0.2, .y = 1.8, .len = 3.7, .wid = 1.7, .hgt = 1.2};
    const Cruise loaded = cruise(withPayload(TRUCK, &load), t, 80);
    ok(loaded.per100 > empty.per100 * 1.02, "loaded: " + toFixed(loaded.per100, 1) + " l/100 km (empty " + toFixed(empty.per100, 1) + ")");
    const Cruise light = cruise(LIGHT_TRUCK, tank("lightTruck"), 80);
    ok(light.per100 > 12 && light.per100 < 26, "7.5 t: " + toFixed(light.per100, 1) + " l/100 km");
    std::printf("    12-t truck at 80 km/h: %.1f l/100 km empty, %.1f with 2.4 t; 7.5-t truck %.1f\n", empty.per100, loaded.per100, light.per100);
  }

  test("a city bus burns 18 to 40 l/100 km at a steady 50 km/h");
  {
    const Cruise r = cruise(BUS, tank("bus"), 50);
    ok(r.per100 > 18 && r.per100 < 40, toFixed(r.per100, 1) + " l/100 km");
    std::printf("    bus at 50 km/h: %.1f l/100 km\n", r.per100);
  }

  test("a tractor burns 15 to 40 l/100 km on the road at 35 km/h (some 6 to 14 l an hour)");
  {
    const Cruise r = cruise(TRACTOR, tank("tractorSlow"), 35);
    ok(r.per100 > 15 && r.per100 < 40, toFixed(r.per100, 1) + " l/100 km");
    ok(r.perHour > 5 && r.perHour < 14, toFixed(r.perHour, 1) + " l/h");
    std::printf("    tractor at 35 km/h: %.1f l/100 km, %.1f l/h\n", r.per100, r.perHour);
  }

  test("a motorcycle burns 3 to 7 l/100 km at 100 km/h, a moped 1.5 to 4 at 40");
  {
    VehicleSpec bike = MOTORCYCLE;
    bike.autoClutch = true;
    const Cruise big = cruise(bike, tank("motorcycle"), 100, shifter({20, 40, 60, 80, 95}));
    ok(big.per100 > 3 && big.per100 < 7, "motorcycle: " + toFixed(big.per100, 2) + " l/100 km");
    const Cruise moped = cruise(MOPED.auto_, tank("moped"), 40);
    ok(moped.per100 > 1.5 && moped.per100 < 4, "moped: " + toFixed(moped.per100, 2) + " l/100 km");
    std::printf("    motorcycle at 100 km/h: %.1f l/100 km; moped at 40: %.1f\n", big.per100, moped.per100);
  }

  test("idling costs a little, rolling in gear with the foot off nothing, a stopped engine nothing");
  {
    const TankSpec& t = tank("car");
    PlaneGround g1;
    Vehicle car = makeCar(g1, COMPACT_CAR_AUTOCLUTCH);
    for (int i = 0; i < 5 / DT; i++) car.step(DT, DriverInput{.gear = 0});
    const double idle = fuelFlow(car.spec, t, car.rpm, car.engineLoad, car.engineRunning) * 3600;
    ok(idle > 0.3 && idle < 1.4, "car idling: " + toFixed(idle, 2) + " l/h");
    PlaneGround g2;
    Vehicle truck = makeCar(g2, TRUCK);
    for (int i = 0; i < 5 / DT; i++) truck.step(DT, DriverInput{.gear = 0});
    const double idleTruck = fuelFlow(truck.spec, tank("truck"), truck.rpm, truck.engineLoad, truck.engineRunning) * 3600;
    ok(idleTruck > 0.8 && idleTruck < 4, "truck idling: " + toFixed(idleTruck, 2) + " l/h");
    std::printf("    idling: car %.2f l/h, truck %.2f l/h\n", idle, idleTruck);
    // coasting in 4th at 70 km/h, the foot off: the engine is driven by the wheels, the injection cut off
    car.vel[2] = -70 / 3.6;
    for (int i = 0; i < 2 / DT; i++) car.step(DT, DriverInput{.gear = 4});
    ok(car.rpm > 1500, "turning: " + toFixed(car.rpm, 0) + " rpm");
    ok(fuelFlow(car.spec, t, car.rpm, car.engineLoad, car.engineRunning) == 0, "coasting");
    ok(fuelFlow(car.spec, t, 2000, 0.5, false) == 0, "stopped");
  }

  test("the tank: burning, the reserve, empty, filling up; the litres per 100 km");
  {
    FuelTank t(tank("car"), 0.5);
    ok(t.litres == 25, "25 l");
    ok(!t.low() && !t.empty(), "not low");
    t.burn(0.001, 20000, 400000);
    ok(std::abs(t.litres - 5) < 1e-9, "5 l");
    ok(t.low() && !t.empty(), "low");
    ok(std::abs(t.average() - 5) < 1e-9, jsStr(t.average()));
    t.burn(1, 10);
    ok(t.empty(), "empty");
    ok(t.litres == 0, "0 l");
    ok(t.fill(80) == 50, "fill 50");
    ok(t.share() == 1, "full");
    // every road vehicle has a tank of its size; the tram, the aeroplane and on foot none
    ok(tankFor("truck")->fuel == "diesel", "truck diesel");
    ok(tankFor("car")->fuel == "petrol", "car petrol");
    ok(tankFor("tram") == nullptr, "tram");
    ok(tankFor("pedestrian") == nullptr, "pedestrian");
    ok(tankFor("truck")->capacity > tankFor("lightTruck")->capacity, "sizes");
    ok(ENERGY_PER_LITRE("diesel") > ENERGY_PER_LITRE("petrol"), "energy");
  }

  test("the pump: the litres and the price count up until the tank is full, slowly at first");
  {
    FuelTank t(tank("car"), 0.2);
    FillUp f(1.799, 1.6);
    f.step(0.25, t);
    const double first = f.litres;
    ok(first > 0 && first < 0.4 * 0.25 * 1.6 + 1e-9, "the first quarter second: " + jsStr(first));
    double time = 0.25;
    while (!f.done && time < 200) {
      f.step(0.1, t);
      time += 0.1;
    }
    ok(f.done, "done");
    ok(t.share() == 1, "full");
    ok(std::abs(f.litres - 40) < 0.05, jsStr(f.litres) + " l");
    ok(f.cost() == jsRound(f.litres * 1.799 * 100) / 100, "cost");
    ok(time > 25 && time < 40, toFixed(time, 1) + " s for 40 l");
  }

  test("a drive begins with some fuel: mostly a good part of the tank, now and then the reserve");
  ok(startShare(0.1, 0.5) < 0.2, "reserve");
  ok(startShare(0.5, 0) >= 0.3, "a third");
  ok(startShare(0.99, 1) <= 0.95, "not quite full");

  test("a load makes the truck heavier, its centre of mass higher and further back, and slower to pull away");
  {
    const Payload load{.mass = 2000, .z = 0.5, .y = 1.85, .len = 3.75, .wid = 1.7, .hgt = 1.3};
    const VehicleSpec s = withPayload(TRUCK, &load);
    ok(s.mass == TRUCK.mass + 2000, "mass");
    ok(s.cgHeight > TRUCK.cgHeight, "cg height");
    ok(s.cgToFront > TRUCK.cgToFront, "cg back");
    for (int i = 0; i < 3; i++) ok(s.inertia[i] > TRUCK.inertia[i], "inertia");
    ok(withPayload(TRUCK, nullptr).mass == TRUCK.mass, "none");
    // 0 to 40 km/h at full throttle
    const auto time = [](const VehicleSpec& spec) {
      PlaneGround g;
      Vehicle v = makeCar(g, spec);
      double t = 0;
      while (v.speed < 40 / 3.6 && t < 60) {
        v.step(DT, DriverInput{.throttle = 1, .gear = 1});
        t += DT;
      }
      return t;
    };
    const double empty = time(TRUCK);
    const double loaded = time(s);
    ok(loaded > empty * 1.1, "0–40 km/h: empty " + toFixed(empty, 1) + " s, loaded " + toFixed(loaded, 1) + " s");
  }
  std::printf("fuel_test: ok\n");
  return 0;
}

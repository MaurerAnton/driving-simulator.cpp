// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/fuel.ts @ v1.7.7
#include "sim/fuel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>

#include "core/js.h"

namespace fs {

double ENERGY_PER_LITRE(const FuelKind& fuel) { return fuel == "diesel" ? 35.9e6 : 31.8e6; }

namespace {

const TankSpec CAR = {.capacity = 50, .fuel = "petrol", .efficiency = 0.34, .reserve = 0.12, .flow = 1.6};

const std::map<std::string, TankSpec>& TANKS() {
  static const auto* t = new std::map<std::string, TankSpec>{
      {"car", CAR},
      {"carTrailer", CAR},
      {"moped", {.capacity = 6.5, .fuel = "petrol", .efficiency = 0.26, .reserve = 0.2, .flow = 0.5}},
      {"motorcycle125", {.capacity = 11, .fuel = "petrol", .efficiency = 0.3, .reserve = 0.18, .flow = 0.8}},
      {"motorcycle35", {.capacity = 14, .fuel = "petrol", .efficiency = 0.31, .reserve = 0.18, .flow = 0.8}},
      {"motorcycle", {.capacity = 16, .fuel = "petrol", .efficiency = 0.31, .reserve = 0.18, .flow = 0.8}},
      {"lightTruck", {.capacity = 150, .fuel = "diesel", .efficiency = 0.42, .reserve = 0.1, .flow = 3.5}},
      {"lightTruckTrailer", {.capacity = 150, .fuel = "diesel", .efficiency = 0.42, .reserve = 0.1, .flow = 3.5}},
      {"truckSolo", {.capacity = 300, .fuel = "diesel", .efficiency = 0.43, .reserve = 0.08, .flow = 5}},
      {"truck", {.capacity = 300, .fuel = "diesel", .efficiency = 0.43, .reserve = 0.08, .flow = 5}},
      {"semi", {.capacity = 600, .fuel = "diesel", .efficiency = 0.44, .reserve = 0.07, .flow = 6}},
      {"minibus", {.capacity = 90, .fuel = "diesel", .efficiency = 0.41, .reserve = 0.1, .flow = 3}},
      {"minibusTrailer", {.capacity = 90, .fuel = "diesel", .efficiency = 0.41, .reserve = 0.1, .flow = 3}},
      {"bus", {.capacity = 280, .fuel = "diesel", .efficiency = 0.42, .reserve = 0.08, .flow = 5}},
      {"busTrailer", {.capacity = 280, .fuel = "diesel", .efficiency = 0.42, .reserve = 0.08, .flow = 5}},
      {"tractorSlow", {.capacity = 160, .fuel = "diesel", .efficiency = 0.4, .reserve = 0.1, .flow = 3.5}},
      {"tractor", {.capacity = 280, .fuel = "diesel", .efficiency = 0.41, .reserve = 0.1, .flow = 5}},
  };
  return *t;
}

}  // namespace

const TankSpec* tankFor(const std::string& kind) {
  auto it = TANKS().find(kind);
  return it == TANKS().end() ? nullptr : &it->second;
}

double fullTorque(const VehicleSpec& spec, double rpm) {
  const auto& c = spec.torqueCurve;
  if (rpm <= c[0][0]) return c[0][1];
  for (size_t i = 1; i < c.size(); i++) {
    if (rpm <= c[i][0]) {
      const double t = (rpm - c[i - 1][0]) / (c[i][0] - c[i - 1][0]);
      return c[i - 1][1] + (c[i][1] - c[i - 1][1]) * t;
    }
  }
  return 0;
}

double fuelFlow(const VehicleSpec& spec, const TankSpec& tank, double rpm, double load, bool running) {
  if (!running || load <= 0 || rpm <= 0) return 0;
  const double omega = (rpm * std::numbers::pi) / 30;
  const double friction = spec.engineFriction[0] + spec.engineFriction[1] * rpm;
  const double work = std::min(1.0, load) * (fullTorque(spec, rpm) + friction) * omega;
  return work / (tank.efficiency * ENERGY_PER_LITRE(tank.fuel));
}

FuelTank::FuelTank(const TankSpec& specIn, double shareIn) : spec(specIn), litres(std::max(0.0, std::min(1.0, shareIn)) * specIn.capacity) {}

void FuelTank::burn(double flow, double dt, double metres) {
  const double l = std::max(0.0, flow * dt);
  litres = std::max(0.0, litres - l);
  used += l;
  distance += std::max(0.0, metres);
}

double FuelTank::fill(double l) {
  const double add = std::max(0.0, std::min(l, spec.capacity - litres));
  litres += add;
  return add;
}

double FuelTank::average() const { return distance > 1000 ? (used / distance) * 100000 : std::numeric_limits<double>::quiet_NaN(); }

double FillUp::cost() const { return jsRound(litres * price * 100) / 100; }

void FillUp::step(double dt, FuelTank& tank) {
  if (done) return;
  t += dt;
  const double room = tank.spec.capacity - tank.litres;
  const double start = std::min(1.0, t / 1.2);
  const double end = std::max(0.2, std::min(1.0, room / (flow_ * 1.5)));
  const double got = tank.fill(flow_ * start * end * dt);
  litres += got;
  if (room - got < 0.02) {
    done = true;
    tank.litres = tank.spec.capacity;
  }
}

double startShare(double rand, double rand2) {
  if (rand < 0.18) return 0.07 + rand2 * 0.12;
  return 0.3 + rand2 * 0.65;
}

}  // namespace fs

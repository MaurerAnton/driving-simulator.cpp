// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/vehicle/fleet.ts @ v1.7.7
#include "sim/vehicle/fleet.h"

#include <algorithm>
#include <cmath>

#include "sim/jsmath.h"

namespace fs {

/**
 * The smaller motorcycles: a moped built for 45 km/h (AM), a 125 (A1), one of 35 kW (A2) – the big one with
 * `k` times its engine; `size` (default k) says how much lighter the machine is.
 */
static VehicleSpec smaller(const std::string& name, double k, double finalDrive, double size) {
  // (a smaller engine: less torque – and less friction and inertia inside it, or that would eat its torque)
  const double f0 = MOTORCYCLE.engineFriction[0];
  const double f1 = MOTORCYCLE.engineFriction[1];
  VehicleSpec s = MOTORCYCLE;
  s.name = name;
  s.torqueCurve = fleet_detail::mapCurve(MOTORCYCLE.torqueCurve, [k](double, double t) { return jsRound(t * k * 10) / 10; });
  s.engineFriction = {f0 * k, f1 * k};
  // (through the moped's long gearing a heavy flywheel would weigh as much as three mopeds: a small engine has a small one)
  s.engineInertia = MOTORCYCLE.engineInertia * std::max(0.06, k);
  s.clutchTorque = MOTORCYCLE.clutchTorque * std::max(0.3, k);
  s.mass = MOTORCYCLE.mass - (1 - std::min(1.0, size)) * 120;
  s.finalDrive = finalDrive;
  return s;
}

Variants variants(const std::string& name, double k, double finalDrive) { return variants(name, k, finalDrive, k); }

Variants variants(const std::string& name, double k, double finalDrive, double size) {
  VehicleSpec manual = smaller(name, k, finalDrive, size);
  const VehicleSpec::Auto& base = *MOTORCYCLE_AUTOMATIC.auto_;
  const double coupling = k < 1 ? k * k : 1;
  VehicleSpec automatic = manual;
  automatic.name = name + ", Automatik";
  automatic.transmission = "automatic";
  automatic.auto_ = base;
  automatic.auto_->converter = base.converter * coupling;
  return {std::move(manual), std::move(automatic)};
}

double bikeLean(double speed, double yawRate) {
  const double a = -speed * yawRate;
  if (!std::isfinite(a)) return 0;
  return jsAtan(std::max(-8.0, std::min(8.0, a)) / 9.81);
}

}  // namespace fs

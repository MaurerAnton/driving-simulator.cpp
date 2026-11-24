// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/vehicle/spec.ts @ v1.7.7
#include "sim/vehicle/spec.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "sim/jsmath.h"

namespace fs {

VehicleSpec withPower(const VehicleSpec& spec, double ps) {
  const double k = ps / BASE_POWER_PS;
  if (std::abs(k - 1) < 0.01) return spec;
  const double low = std::max(k, std::min(1.0, 0.45 + 0.55 * k));
  std::vector<std::array<double, 2>> torqueCurve;
  torqueCurve.reserve(spec.torqueCurve.size());
  for (const auto& [rpm, t] : spec.torqueCurve) {
    const double blend = std::min(1.0, std::max(0.0, (rpm - 1000) / 900));
    torqueCurve.push_back({rpm, jsRound(t * (low + (k - low) * blend) * 10) / 10});
  }
  // a stronger engine needs a clutch that holds its torque
  VehicleSpec out = spec;
  out.name = spec.name + ", " + jsStr(ps) + " PS";
  out.torqueCurve = std::move(torqueCurve);
  out.clutchTorque = std::max(spec.clutchTorque, spec.clutchTorque * k * 1.1);
  return out;
}

double peakPowerPs(const VehicleSpec& spec) {
  double best = 0;
  for (const auto& [rpm, t] : spec.torqueCurve) best = std::max(best, (t * rpm * M_PI) / 30);
  return best / 735.5;
}

VehicleSpec atPower(const VehicleSpec& spec, double ps) {
  const double k = ps / peakPowerPs(spec);
  if (!std::isfinite(k) || k <= 0 || std::abs(k - 1) < 0.01) return spec;
  VehicleSpec out = spec;
  out.name = spec.name + ", " + jsStr(ps) + " PS";
  // (a stronger engine pulls harder from just above idle on: at idle it turns the converter no harder than before)
  out.torqueCurve.clear();
  for (const auto& [rpm, t] : spec.torqueCurve) {
    const double f = k <= 1 ? k : 1 + (k - 1) * std::min(1.0, std::max(0.0, (rpm - spec.idleRpm * 1.3) / (spec.idleRpm * 0.7)));
    out.torqueCurve.push_back({rpm, jsRound(t * f * 100) / 100});
  }
  out.clutchTorque = std::max(spec.clutchTorque, spec.clutchTorque * k * 1.1);
  if (spec.auto_) {
    out.auto_ = *spec.auto_;
    out.auto_->converter = spec.auto_->converter * std::min(1.0, k);
  }
  return out;
}

}  // namespace fs

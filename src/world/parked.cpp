// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/parked.ts @ v1.7.7
#include "world/parked.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "world/tilefmt.h"

namespace fs {

namespace {
const double YAW_SCALE = 1e-4;
const double PI_ = 3.14159265358979323846;
}  // namespace

PackedParked packParked(const std::vector<ParkedCar>& cars, double cx, double cz, double yBase) {
  PackedParked out;
  out.parked.assign(cars.size() * 4, 0);
  out.parkedK.assign(cars.size(), 0);
  const auto q = [](double v) { return static_cast<int16_t>(std::max(-32767.0, std::min(32767.0, jsRound(v)))); };
  for (size_t i = 0; i < cars.size(); i++) {
    const ParkedCar& c = cars[i];
    double yaw = std::fmod(c.yaw, PI_ * 2);
    if (yaw > PI_) yaw -= PI_ * 2;
    if (yaw < -PI_) yaw += PI_ * 2;
    out.parked[i * 4] = q((c.x - cx) / POS_XZ_SCALE);
    out.parked[i * 4 + 1] = q((c.z - cz) / POS_XZ_SCALE);
    out.parked[i * 4 + 2] = q((c.y - yBase) / POS_Y_SCALE);
    out.parked[i * 4 + 3] = q(yaw / YAW_SCALE);
    out.parkedK[i] = static_cast<uint8_t>((c.model & 15) * 16 + (c.colour & 15));
  }
  return out;
}

std::vector<ParkedCar> unpackParked(const std::vector<int16_t>* parked, const std::vector<uint8_t>* parkedK, double cx, double cz, double yBase) {
  std::vector<ParkedCar> out;
  if (!parked || !parkedK) return out;
  const size_t n = std::min(parked->size() >> 2, parkedK->size());
  out.reserve(n);
  for (size_t i = 0; i < n; i++) {
    ParkedCar c;
    c.x = cx + (*parked)[i * 4] * POS_XZ_SCALE;
    c.z = cz + (*parked)[i * 4 + 1] * POS_XZ_SCALE;
    c.y = yBase + (*parked)[i * 4 + 2] * POS_Y_SCALE;
    c.yaw = (*parked)[i * 4 + 3] * YAW_SCALE;
    c.model = std::min(PARKED_MODEL_COUNT - 1, (*parkedK)[i] >> 4);
    c.colour = std::min(PARKED_COLOUR_COUNT - 1, (*parkedK)[i] & 15);
    out.push_back(c);
  }
  return out;
}

std::vector<double> parkedOutline(const ParkedCar& c, double inset) {
  const ParkedModel& m = c.model >= 0 && c.model < PARKED_MODEL_COUNT ? PARKED_MODELS[c.model] : PARKED_MODELS[1];
  const double fx = -std::sin(c.yaw);
  const double fz = -std::cos(c.yaw);
  // right of forward
  const double rx = -fz;
  const double rz = fx;
  const double hl = m.len / 2 - inset;
  const double hw = m.wid / 2 - inset;
  return {
      c.x + fx * hl + rx * hw, c.z + fz * hl + rz * hw,
      c.x + fx * hl - rx * hw, c.z + fz * hl - rz * hw,
      c.x - fx * hl - rx * hw, c.z - fz * hl - rz * hw,
      c.x - fx * hl + rx * hw, c.z - fz * hl + rz * hw,
  };
}

}  // namespace fs

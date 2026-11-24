// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/flycam.ts @ v1.7.7
#include "game/flycam.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "core/js.h"

namespace fs {

namespace {
const double BASE = 1.5;
}  // namespace

double FlyCam::speed() const { return BASE * std::pow(std::numbers::sqrt2, jsRound(step)); }

void FlyCam::start(double xIn, double yIn, double zIn, double dx, double dy, double dz) {
  x = xIn;
  y = yIn;
  z = zIn;
  double l = std::hypot(dx, dy, dz);
  if (l == 0 || std::isnan(l)) l = 1;
  yaw = std::atan2(-dx, -dz);
  pitch = std::asin(std::max(-1.0, std::min(1.0, dy / l)));
  vx = vy = vz = 0;
  ready = true;
}

bool FlyCam::update(double dt, const FlyInput& t, const std::function<double(double x, double z)>& ground) {
  const double was = jsRound(step);
  step = std::max(FLY_MIN_STEP, std::min(FLY_MAX_STEP, step + t.speed));
  yaw += t.yaw;
  pitch = std::max(-1.55, std::min(1.45, pitch + t.pitch));
  const double sy = std::sin(yaw);
  const double cy = std::cos(yaw);
  const double cp = std::cos(pitch);
  const double sp = std::sin(pitch);
  // what the keys (or the stick) ask for: −1 … 1 each, from the key time of this picture
  const double k = 1 / std::max(dt, 1e-3);
  const double f = std::max(-1.0, std::min(1.0, t.keyFwd * k));
  const double r = std::max(-1.0, std::min(1.0, t.keyRight * k));
  const double u = std::max(-1.0, std::min(1.0, t.keyUp * k));
  const double n = std::max(1.0, std::hypot(f, r, u));
  const double v = (speed() * (t.fast ? 4 : 1)) / n;
  // forwards along the look (up or down with it), to the right level, up straight up
  const double wx = (-sy * cp * f + cy * r) * v;
  const double wy = (sp * f + u) * v;
  const double wz = (-cy * cp * f - sy * r) * v;
  // (eased: no jerk when a key goes down or up)
  const double ease = 1 - std::exp(-dt * 7);
  vx += (wx - vx) * ease;
  vy += (wy - vy) * ease;
  vz += (wz - vz) * ease;
  double g = ground(x, z);
  // dragged over the ground: a drag across the screen moves it about as far as it is high (metres near the ground, a block from high up)
  const double reach = std::max(4.0, std::min(400.0, (std::isnan(g) ? 30 : y - g) * 1.6 + 3));
  const double dragR = t.right * reach;
  const double dragF = t.fwd * reach;
  x += vx * dt + cy * dragR - sy * dragF;
  z += vz * dt - sy * dragR - cy * dragF;
  y += vy * dt + t.lift * std::max(8.0, reach * 0.6);
  g = ground(x, z);
  if (!std::isnan(g)) {
    if (y < g + 0.5) {
      y = g + 0.5;
      if (vy < 0) vy = 0;
    }
    y = std::min(y, g + 1500);
  }
  return jsRound(step) != was;
}

}  // namespace fs

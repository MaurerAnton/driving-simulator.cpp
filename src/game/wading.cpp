// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/wading.ts @ v1.7.7
#include "game/wading.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "core/js.h"
#include "core/random.h"
#include "sim/vehicle/vehicle.h"
#include "world/cover.h"

namespace fs {

namespace {

/** the rings looked at round a point for the shore (m) */
constexpr std::array<double, 6> RINGS = {1, 2, 3, 4.5, 6, 8};
/** the grid the shore distances are kept for (m) */
constexpr double GRID = 2;

}  // namespace

double bedDepth(double shore) { return std::min(DEEP_WATER, WATER_DROP + BANK_SLOPE * std::max(0.0, shore)); }

double washRate(double speed, double depth) {
  if (!(depth > 0)) return 0;
  const double v = std::min(std::abs(speed), 15.0);
  return (0.02 + 0.025 * v) * std::min(1.0, 0.3 + depth / 0.4);
}

double brakeFactor(double wet) { return 1 - 0.35 * std::min(1.0, std::max(0.0, wet)); }

double dryRate(double speed, double brake) { return 0.012 + 0.35 * std::min(1.0, std::max(0.0, brake)) * std::min(1.0, std::abs(speed) / 8); }

bool wetsBrakes(double depth, double speed) { return depth > 0.28 || (depth > 0.05 && std::abs(speed) > 2.5); }

double intakeHeight(const std::string& model) {
  if (model == "motorcycle") return 0.45;
  if (model == "car") return 0.55;
  if (model == "minibus") return 0.65;
  if (model == "truckLight") return 0.75;
  if (model == "tractor") return 0.95;
  if (model == "truck" || model == "truckHeavy" || model == "semi" || model == "bus" || model == "mobileCrane" || model == "loaderCrane") return 0.9;
  return 0.6;
}

double sprayRate(double speed, double depth) {
  const double v = std::abs(speed);
  if (!(depth > 0) || v < 0.4) return 0;
  return std::min(90.0, (6 + 7 * v) * std::min(1.0, 0.4 + depth / 0.25));
}

double wheelDepth(double d0, double ground, double wheelBottom) {
  if (!(d0 > 0)) return 0;
  if (!std::isfinite(ground)) return d0;
  return std::max(0.0, std::min(d0, ground - wheelBottom + 0.05));
}

// ------------------------------------------------------------------ the depth

double WaterDepth::at(double x, double z) {
  if (src_.coverAt(x, z) != COVER_WATER) return 0;
  const double gx = x / GRID;
  const double gz = z / GRID;
  const double i = std::floor(gx);
  const double j = std::floor(gz);
  const double tx = gx - i;
  const double tz = gz - j;
  const double d00 = corner(i, j);
  const double d10 = corner(i + 1, j);
  const double d01 = corner(i, j + 1);
  const double d11 = corner(i + 1, j + 1);
  const double d = (d00 * (1 - tx) + d10 * tx) * (1 - tz) + (d01 * (1 - tx) + d11 * tx) * tz;
  return bedDepth(d);
}

double WaterDepth::corner(double i, double j) {
  const double key = (i + 1000000) * 2000003 + (j + 1000000);
  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second;
  if (cache_.size() > 20000) cache_.clear();
  const double d = shore(i * GRID, j * GRID);
  cache_[key] = d;
  return d;
}

double WaterDepth::shore(double x, double z) const {
  if (src_.coverAt(x, z) != COVER_WATER) return 0;
  double prev = 0;
  for (double r : RINGS) {
    for (int k = 0; k < 8; k++) {
      const double a = (k * std::numbers::pi) / 4;
      if (src_.coverAt(x + std::cos(a) * r, z + std::sin(a) * r) != COVER_WATER) return (prev + r) / 2;
    }
    prev = r;
  }
  return RINGS.back();
}

// ------------------------------------------------------------------ the vehicle in the water

Wading::Wading(CoverSource src) : depth(src), src_(std::move(src)) {
  group->name = "wading";
  group->add(spray.points);
  p_.age = 0;
  p_.life = 1;
  p_.size0 = 0.1;
  p_.size1 = 0.3;
}

void Wading::reset() {
  wet = 0;
  wetTold_ = false;
  depthNow = 0;
  spray.clear();
}

WadingTip Wading::step(double dt, Vehicle* v, const std::string& model, double& dirt, double brake, bool effects, double light) {
  const double h = std::isfinite(dt) && dt > 0 ? std::min(dt, 0.25) : 0;
  clearT_ += h;
  if (clearT_ > 8) {
    clearT_ = 0;
    depth.clear();
  }
  WadingTip tip;
  double deepest = 0;
  bool wetting = false;
  if (v) {
    const double speed = v->speed;
    for (size_t i = 0; i < v->wheels.size() && i < 4; i++) {
      const WheelState& w = v->wheels[i];
      // The cover map says how deep the water is at that place, whatever stands
      // over it: on a bridge the wheel is metres above the river, and only the
      // water the wheel really reaches into counts (wheelDepth).
      const double d = wheelDepth(depth.at(w.hx, w.hz), src_.heightAt ? src_.heightAt(w.hx, w.hz) : JS_NAN, w.hy - v->spec.wheelRadius);
      if (d > deepest) deepest = d;
      if (d > 0 && wetsBrakes(d, speed)) wetting = true;
      if (effects && d > 0 && w.contact) splash(i, *v, d, h);
      else carry_[i] = 0;
    }
    // washed – the more of the car in the water and the faster, the cleaner
    if (deepest > 0) dirt = std::max(0.0, dirt - washRate(speed, deepest) * h);
    // the brakes: wet in the water, dried by braking
    if (wetting) {
      wet = 1;
      wetTold_ = false;
    } else if (wet > 0) {
      wet = std::max(0.0, wet - dryRate(speed, brake) * h);
      if (!wetTold_ && deepest == 0 && wet > 0.6 && brake > 0.2 && std::abs(speed) > 2) {
        wetTold_ = true;
        tip = "wetBrakes";
      }
    }
    // water up to the air intake: the engine stalls (and stays dead while it is that deep)
    if (deepest > intakeHeight(model) && v->engineRunning) {
      v->resetDrivetrain(false);
      tip = "waterStalled";
    }
    // the wake: foam spreading behind and beside the vehicle
    if (effects && deepest > 0.08 && std::abs(speed) > 1) wake(*v, deepest, h);
  }
  depthNow = deepest;
  spray.light = light;
  spray.update(h);
  return tip;
}

void Wading::splash(size_t i, const Vehicle& v, double d, double h) {
  const WheelState& w = v.wheels[i];
  const double speed = v.speed;
  carry_[i] += sprayRate(speed, d) * h;
  if (carry_[i] < 1) return;
  const double sy = std::sin(v.yaw);
  const double cy = std::cos(v.yaw);
  // forward (−sin yaw, −cos yaw); right (cos yaw, −sin yaw)
  const double side = (w.hx - v.pos[0]) * cy - (w.hz - v.pos[2]) * sy >= 0 ? 1 : -1;
  const double surface = w.hy - v.spec.wheelRadius + d;
  const double sp = std::min(std::abs(speed), 25.0);
  SprayParticle& p = p_;
  while (carry_[i] >= 1) {
    carry_[i] -= 1;
    const double out = (0.6 + 0.12 * sp) * (0.4 + rnd());
    const double back = -jsSign(speed) * sp * (0.15 + 0.25 * rnd());
    p.x = w.hx + (rnd() - 0.5) * 0.4;
    p.z = w.hz + (rnd() - 0.5) * 0.4;
    p.y = surface + 0.02;
    // (the vehicle's own motion, a little of its speed backwards, outwards and up)
    p.vx = -sy * speed + cy * side * out - sy * back;
    p.vz = -cy * speed - sy * side * out - cy * back;
    p.vy = (1.2 + 0.18 * sp) * (0.5 + rnd());
    p.age = 0;
    p.life = 0.7 + 0.6 * rnd();
    p.size0 = 0.08 + 0.06 * rnd();
    p.size1 = 0.25 + 0.02 * sp;
    p.floor = surface - 0.05;
    p.foam = false;
    spray.emit(p);
  }
}

void Wading::wake(const Vehicle& v, double d, double h) {
  const double sp = std::min(std::abs(v.speed), 20.0);
  wakeT_ += h * (2 + 1.2 * sp);
  if (wakeT_ < 1) return;
  const double sy = std::sin(v.yaw);
  const double cy = std::cos(v.yaw);
  // the water's surface: the lowest wheel's ground plus the depth
  double bed = JS_INF;
  for (size_t i = 0; i < v.wheels.size() && i < 4; i++) bed = std::min(bed, v.wheels[i].hy - v.spec.wheelRadius);
  if (!std::isfinite(bed)) return;
  // (from behind the rear wheels – the front ones when reversing – at one side or the other)
  const auto& ws = v.wheels;
  const size_t a = ws.size() >= 4 ? (v.speed >= 0 ? 2 : 0) : 0;
  const double bx = ws.size() >= 2 ? (ws[a].hx + ws[a + 1].hx) / 2 : v.pos[0];
  const double bz = ws.size() >= 2 ? (ws[a].hz + ws[a + 1].hz) / 2 : v.pos[2];
  SprayParticle& p = p_;
  while (wakeT_ >= 1) {
    wakeT_ -= 1;
    const double side = rnd() < 0.5 ? -1 : 1;
    const double out = (0.3 + 0.08 * sp) * (0.6 + 0.8 * rnd());
    const double behind = -(0.4 + rnd()) * jsSign(v.speed != 0 ? v.speed : 1);
    p.x = bx - sy * behind + cy * side * 0.9;
    p.z = bz - cy * behind - sy * side * 0.9;
    p.y = bed + d + 0.03;
    p.vx = cy * side * out;
    p.vz = -sy * side * out;
    p.vy = 0;
    p.age = 0;
    p.life = 2 + rnd();
    p.size0 = 0.5;
    p.size1 = 2.2 + 0.08 * sp;
    p.floor = -JS_INF;
    p.foam = true;
    spray.emit(p);
  }
}

}  // namespace fs

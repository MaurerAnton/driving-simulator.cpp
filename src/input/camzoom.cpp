// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/input/camzoom.ts @ v1.7.7
#include "input/camzoom.h"

#include <cmath>
#include <numbers>

#include "core/js.h"

namespace fs {

namespace {

/** one notch of the wheel or one press of a key: this much closer */
const double STEP = 1.25;
/** "auto": how long after the wheel or a key the zoom lets go (s) */
const double AUTO_DELAY = 4;
/** the wheel can not be "held": a zoom by it stays at least this long (s) */
const double WHEEL_MIN = 1.5;

}  // namespace

void CamZoom::pinch(double scale) {
  if (!(scale > 0) || !std::isfinite(scale)) return;
  fingers = true;
  source = "touch";
  set(target * scale);
}

void CamZoom::fingersOff() {
  if (!fingers) return;
  fingers = false;
  idle = 0;
}

void CamZoom::wheel(double step) {
  if (!std::isfinite(step)) return;
  source = "wheel";
  idle = 0;
  set(target * std::exp(-step * 1.6));
}

void CamZoom::press(double dir, bool down) {
  if (!down) {
    if (key) idle = 0;
    key = false;
    return;
  }
  key = true;
  source = "key";
  idle = 0;
  set(target * (dir > 0 ? STEP : 1 / STEP));
}

void CamZoom::reset() {
  factor = target = 1;
  fingers = key = false;
  idle = 0;
}

double CamZoom::delay(const ZoomRelease& release) const {
  if (release == "never") return JS_INF;
  if (release == "release") return source == "wheel" ? WHEEL_MIN : 0;
  if (release == "3" || release == "6" || release == "12") return jsNumber(release);
  return source == "touch" ? 0 : AUTO_DELAY;
}

void CamZoom::update(double dt, const ZoomRelease& release, bool active) {
  if (!active) {
    if (factor != 1 || target != 1 || held()) reset();
    return;
  }
  if (!held() && target != 1) {
    idle += dt;
    if (idle >= delay(release)) target = 1;
  }
  // (in steps of the logarithm: zooming in and out feel alike)
  const double a = std::log(factor);
  const double b = std::log(target);
  const double k = held() ? 1 - std::exp(-dt * 18) : 1 - std::exp(-dt * 7);
  const double next = a + (b - a) * k;
  factor = std::abs(next - b) < 0.002 ? target : std::exp(next);
}

double CamZoom::fov(double base) const {
  if (factor == 1) return base;
  const double half = (base * std::numbers::pi) / 360;
  return (std::atan(std::tan(half) / factor) * 360) / std::numbers::pi;
}

void CamZoom::set(double f) { target = jsMax(1, jsMin(ZOOM_MAX, f)); }

}  // namespace fs

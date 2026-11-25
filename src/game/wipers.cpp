// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/wipers.ts @ v1.7.7
#include "game/wipers.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>

#include "audio/waves.h"
#include "core/js.h"

namespace fs {

const std::vector<WiperMode> WIPER_MODES = {"off", "intermittent", "normal", "fast"};

WiperMode nextWiperMode(const WiperMode& m) {
  const auto it = std::find(WIPER_MODES.begin(), WIPER_MODES.end(), m);
  const int i = it == WIPER_MODES.end() ? -1 : static_cast<int>(it - WIPER_MODES.begin());
  return WIPER_MODES[static_cast<size_t>((i + 1) % static_cast<int>(WIPER_MODES.size()))];
}

double sweepAt(double phase) { return (1 - std::cos(2 * std::numbers::pi * phase)) / 2; }

WiperMode Wipers::cycle() {
  mode = nextWiperMode(mode);
  // (intermittent sweeps once at once, then waits)
  pause_ = 0;
  return mode;
}

void Wipers::set(const WiperMode& m) {
  if (m != mode) pause_ = 0;
  mode = m;
}

void Wipers::step(double dt, bool washer) {
  Swept& sw = swept;
  sw.any = false;
  if (washer) after_ = WASH_AFTER;
  const auto want = [&]() { return washer || after_ > 0 || mode == "normal" || mode == "fast" || (mode == "intermittent" && pause_ <= 0); };
  if (phase <= 0 && !want()) {
    if (mode == "intermittent") pause_ -= dt;
    return;
  }
  const double period = mode == "fast" && !washer ? SWEEP_SECONDS.fast : SWEEP_SECONDS.normal;
  const double p0 = phase;
  double p1 = p0 + std::max(0.0, dt) / period;
  double lo = std::min(sweepAt(p0), sweepAt(std::min(p1, 1.0)));
  double hi = std::max(sweepAt(p0), sweepAt(std::min(p1, 1.0)));
  if (p0 < 0.5 && p1 >= 0.5) hi = 1;
  if (p1 >= 1) {
    // back in the parked place: one sweep done
    lo = 0;
    if (!washer && after_ > 0) after_--;
    if (mode == "intermittent") pause_ = INTERVAL_PAUSE;
    p1 = want() ? std::min(p1 - 1, 0.49) : 0;
    if (p1 > 0) hi = std::max(hi, sweepAt(p1));
  }
  phase = p1;
  sw.lo = lo;
  sw.hi = hi;
  sw.any = true;
}

std::vector<WiperArm> tandemArms(double width, double height) {
  const double l1 = std::min(0.92 * height, 0.62 * width);
  const double l2 = std::min(0.84 * height, 0.5 * width);
  return {
      {.px = 0.2 * width, .py = -0.03 * height, .rIn = 0.28 * l1, .rOut = l1, .park = 0.05, .sweep = 2.6},
      {.px = 0.62 * width, .py = -0.03 * height, .rIn = 0.28 * l2, .rOut = l2, .park = 0.05, .sweep = 2.45},
  };
}

namespace {

double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

}  // namespace

Windscreen::Windscreen(double widthIn, double heightIn, int colsIn, int rowsIn, std::optional<std::vector<WiperArm>> armsIn, uint32_t seed)
    : cols(colsIn), rows(rowsIn), width(widthIn), height(heightIn), arms(armsIn ? std::move(*armsIn) : tandemArms(widthIn, heightIn)) {
  const size_t n = static_cast<size_t>(cols) * static_cast<size_t>(rows);
  drops.assign(n, 0);
  film.assign(n, 0);
  snow.assign(n, 0);
  dirt.assign(n, 0);
  smear.assign(n, 0);
  gen.assign(n, 0);
  patch_.assign(n, 0);
  Seeded rnd = seeded(seed);
  for (size_t i = 0; i < n; i++) patch_[i] = static_cast<float>(0.6 + 0.8 * rnd());
  for (const WiperArm& a : arms) {
    std::vector<float> r(n);
    for (int j = 0; j < rows; j++) {
      for (int i = 0; i < cols; i++) {
        const double x = ((i + 0.5) / cols) * width - a.px;
        const double y = ((j + 0.5) / rows) * height - a.py;
        const double d = std::hypot(x, y);
        double ang = std::atan2(y, x) - a.park;
        if (a.sweep < 0) ang = -ang;
        while (ang < -std::numbers::pi) ang += 2 * std::numbers::pi;
        // (half a blade's width beyond either end still gets wiped)
        const double t = ang / std::abs(a.sweep);
        r[static_cast<size_t>(j * cols + i)] = static_cast<float>(d >= a.rIn && d <= a.rOut && t >= -0.02 && t <= 1.02 ? clamp01(t) : -1);
      }
    }
    reach_.push_back(std::move(r));
  }
}

int Windscreen::cellAt(double x, double y) const {
  const double i = std::floor((x / width) * cols);
  const double j = std::floor((y / height) * rows);
  return i >= 0 && i < cols && j >= 0 && j < rows ? static_cast<int>(j) * cols + static_cast<int>(i) : -1;
}

bool Windscreen::wiped(int cell) const {
  if (cell < 0) return false;
  for (const std::vector<float>& r : reach_)
    if (r[static_cast<size_t>(cell)] >= 0) return true;
  return false;
}

double Windscreen::armAngle(int k) const {
  const WiperArm& a = arms[static_cast<size_t>(k)];
  return a.park + a.sweep * wipers.angle();
}

void Windscreen::step(double dt, const ScreenWeather& w, bool washer) {
  if (!(dt > 0)) return;
  dt = std::min(dt, 0.25);
  const double v = std::max(0.0, w.speed);
  const double rain = clamp01(w.rain);
  const double snowing = clamp01(w.snow);
  // driving into the rain catches more of it
  const double catchRain = 1 + v / 14;
  const double dropRate = rain * 0.22 * catchRain;
  // the drops run together into a sheet of water once they lie thick
  const double filmRate = rain * 0.045 * catchRain;
  const double snowRate = snowing * 0.016 * (1 + v / 12);
  const double melt = w.temperature > 0.5 ? std::min(0.05, 0.006 * w.temperature) : 0;
  // (the heater thaws the glass from its foot, slowly)
  const double thaw = w.heater ? 0.0035 : 0;
  // wind of driving fast: drops run up and to the sides, a sheet of water is blown thinner
  running = clamp01((v - 11) / 14);
  const double up = 0.0009 * v * v - 0.04;
  flow.x += w.crossWind * 0.03 * dt;
  flow.y += std::max(0.0, up) * running * dt;
  const double blowOff = running * 0.05;
  const double dry = rain == 0 && snowing == 0 ? 0.012 : 0;
  const double spray = clamp01(w.roadWet) * 0.0016 * std::min(1.0, v / 15);
  const int n = cols * rows;
  for (int k = 0; k < n; k++) {
    const size_t i = static_cast<size_t>(k);
    double d = drops[i] + dropRate * dt;
    double f = film[i] + (d > 0.55 ? filmRate * dt : filmRate * 0.2 * dt) - (blowOff + dry) * film[i] * dt;
    if (dry != 0) d -= dry * 1.5 * dt;
    double s = snow[i] + snowRate * patch_[i] * dt;
    const int j = k / cols;
    const double m = (melt + thaw * std::max(0.0, 1 - (static_cast<double>(j) / rows) * 2.5)) * dt;
    if (m > 0 && s > 0) {
      const double gone = std::min(s, m);
      s -= gone;
      f += gone * 0.6;
      d += gone * 0.6;
    }
    // (snow lying on the glass takes the rain into itself)
    if (s > 0.6) d *= 1 - dt;
    drops[i] = static_cast<float>(clamp01(d));
    film[i] = static_cast<float>(clamp01(f));
    snow[i] = static_cast<float>(clamp01(s));
    dirt[i] = static_cast<float>(clamp01(dirt[i] + spray * dt));
  }
  if (washer) {
    for (int k = 0; k < n; k++) {
      const size_t i = static_cast<size_t>(k);
      film[i] = static_cast<float>(clamp01(film[i] + 1.2 * dt));
      drops[i] = static_cast<float>(clamp01(drops[i] + 1.5 * dt));
    }
  }
  wipers.step(dt, washer);
  if (wipers.swept.any) wipe(wipers.swept.lo, wipers.swept.hi);
}

void Windscreen::wipe(double lo, double hi) {
  const size_t n = static_cast<size_t>(cols) * static_cast<size_t>(rows);
  for (const std::vector<float>& r : reach_) {
    for (size_t k = 0; k < n; k++) {
      const double t = r[k];
      if (t < 0 || t < lo - 0.01 || t > hi + 0.01) continue;
      const bool wet = film[k] > 0.15 || drops[k] > 0.5;
      if (wet) {
        // wiped wet: the dirt goes with the water, the streaks are washed away
        dirt[k] = static_cast<float>(dirt[k] * 0.35);
        smear[k] = static_cast<float>(smear[k] * 0.5);
      } else if (dirt[k] > 0.03) {
        // wiped dry: the dirt is smeared about
        smear[k] = static_cast<float>(clamp01(smear[k] + 0.25 * dirt[k]));
      }
      if (drops[k] > 0) gen[k] = static_cast<uint8_t>((gen[k] + 1) & 255);
      drops[k] = 0;
      film[k] = static_cast<float>(film[k] * 0.06);
      snow[k] = static_cast<float>(snow[k] * 0.08);
    }
  }
}

double Windscreen::obscured() const {
  double sum = 0;
  int cnt = 0;
  for (int j = static_cast<int>(std::floor(rows * 0.25)); j < static_cast<int>(std::ceil(rows * 0.8)); j++) {
    for (int i = static_cast<int>(std::floor(cols * 0.08)); i < static_cast<int>(std::ceil(cols * 0.66)); i++) {
      const size_t k = static_cast<size_t>(j * cols + i);
      sum += std::max({static_cast<double>(snow[k]), film[k] * 0.8, drops[k] * 0.45, std::min(1.0, dirt[k] * 0.8 + smear[k] * 0.7)});
      cnt++;
    }
  }
  return cnt ? sum / cnt : 0;
}

void Windscreen::clear() {
  std::fill(drops.begin(), drops.end(), 0.0f);
  std::fill(film.begin(), film.end(), 0.0f);
  std::fill(snow.begin(), snow.end(), 0.0f);
  std::fill(dirt.begin(), dirt.end(), 0.0f);
  std::fill(smear.begin(), smear.end(), 0.0f);
}

}  // namespace fs

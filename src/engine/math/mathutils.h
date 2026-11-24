// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The constants and small functions of three.js's MathUtils, and what the other
 * maths headers share.
 *
 * Origin: three.js r187 src/math/MathUtils.js
 */
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "core/js.h"
#include "core/random.h"

namespace fs {

constexpr double PI = 3.14159265358979323846;
constexpr double TAU = PI * 2.0;
constexpr double DEG = PI / 180.0;
constexpr double RAD = 180.0 / PI;

// (the draft's helpers, used by src/sim and src/world)
inline double clampD(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
inline double lerpD(double a, double b, double t) { return a + (b - a) * t; }
inline double smoothstepD(double e0, double e1, double x) {
  if (e1 == e0) return x < e0 ? 0.0 : 1.0;
  double t = clampD((x - e0) / (e1 - e0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

namespace mathdetail {
/** `x || 1` */
inline double orOne(double x) { return (x == 0 || x != x) ? 1.0 : x; }
inline double min3(double a, double b, double c) { return jsMin(jsMin(a, b), c); }
inline double max3(double a, double b, double c) { return jsMax(jsMax(a, b), c); }
}  // namespace mathdetail

namespace MathUtils {

constexpr double DEG2RAD = PI / 180.0;
constexpr double RAD2DEG = 180.0 / PI;

/** Math.max(min, Math.min(max, value)) */
inline double clamp(double value, double min, double max) { return jsMax(min, jsMin(max, value)); }
/** the remainder that is never negative: ((n % m) + m) % m */
inline double euclideanModulo(double n, double m) { return std::fmod(std::fmod(n, m) + m, m); }
inline double mapLinear(double x, double a1, double a2, double b1, double b2) { return b1 + (x - a1) * (b2 - b1) / (a2 - a1); }
inline double inverseLerp(double x, double y, double value) {
  if (x != y) return (value - x) / (y - x);
  return 0;
}
inline double lerp(double x, double y, double t) { return (1 - t) * x + t * y; }
inline double damp(double x, double y, double lambda, double dt) { return lerp(x, y, 1 - std::exp(-lambda * dt)); }
inline double pingpong(double x, double length = 1) { return length - std::fabs(euclideanModulo(x, length * 2) - length); }
inline double smoothstep(double x, double min, double max) {
  if (x <= min) return 0;
  if (x >= max) return 1;
  x = (x - min) / (max - min);
  return x * x * (3 - 2 * x);
}
inline double smootherstep(double x, double min, double max) {
  if (x <= min) return 0;
  if (x >= max) return 1;
  x = (x - min) / (max - min);
  return x * x * x * (x * (x * 6 - 15) + 10);
}
/** a whole number in [low, high] / a number in [low, high) / in (-range/2, range/2], from fs::rnd() */
inline double randInt(double low, double high) { return low + std::floor(rnd() * (high - low + 1)); }
inline double randFloat(double low, double high) { return low + rnd() * (high - low); }
inline double randFloatSpread(double range) { return range * (0.5 - rnd()); }
/** mulberry32 with MathUtils' own seed */
inline uint32_t& seededRandomState() {
  static uint32_t seed = 1234567;
  return seed;
}
inline double seededRandom() {
  uint32_t& seed = seededRandomState();
  seed += 0x6D2B79F5u;
  uint32_t t = seed;
  t = (t ^ (t >> 15)) * (t | 1u);
  t ^= t + ((t ^ (t >> 7)) * (t | 61u));
  return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
}
inline double seededRandom(uint32_t s) {
  seededRandomState() = s;
  return seededRandom();
}
inline double degToRad(double degrees) { return degrees * DEG2RAD; }
inline double radToDeg(double radians) { return radians * RAD2DEG; }
inline bool isPowerOfTwo(double value) {
  return value > 0 && std::floor(value) == value && std::isfinite(value) && std::pow(2.0, jsRound(std::log2(value))) == value;
}
inline double ceilPowerOfTwo(double value) { return std::pow(2.0, std::ceil(std::log(value) / 0.6931471805599453)); }
inline double floorPowerOfTwo(double value) { return std::pow(2.0, std::floor(std::log(value) / 0.6931471805599453)); }

}  // namespace MathUtils

}  // namespace fs

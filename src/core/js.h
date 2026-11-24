// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Where JavaScript's arithmetic and its way of writing numbers differ from C++'s:
 * the helpers that keep the port's numbers and texts the same as the original's.
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace fs {

inline constexpr double JS_NAN = std::numeric_limits<double>::quiet_NaN();
inline constexpr double JS_INF = std::numeric_limits<double>::infinity();

/** Math.round: halves go up (Math.round(-2.5) is -2, Math.round(2.5) is 3). */
inline double jsRound(double x) { return std::floor(x + 0.5); }
/** Math.sign */
inline double jsSign(double x) { return x > 0 ? 1.0 : x < 0 ? -1.0 : x; }
/** Math.trunc / Math.fround / Math.hypot have the same names in <cmath>. */
/** `x | 0` (ToInt32) */
inline int32_t jsInt32(double x) {
  if (!std::isfinite(x)) return 0;
  double t = std::trunc(x);
  double m = std::fmod(t, 4294967296.0);
  if (m < 0) m += 4294967296.0;
  return static_cast<int32_t>(static_cast<uint32_t>(m));
}
/** `x >>> 0` (ToUint32) */
inline uint32_t jsUint32(double x) { return static_cast<uint32_t>(jsInt32(x)); }
/** Math.imul */
inline int32_t jsImul(int32_t a, int32_t b) {
  return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}
/** `a % b` on numbers (the sign of the dividend, as std::fmod) */
inline double jsMod(double a, double b) { return std::fmod(a, b); }
/** Math.min/Math.max of two (NaN if either is NaN, as in JavaScript) */
inline double jsMin(double a, double b) { return (a != a || b != b) ? JS_NAN : (a < b ? a : b); }
inline double jsMax(double a, double b) { return (a != a || b != b) ? JS_NAN : (a > b ? a : b); }
/** clamp as `Math.min(hi, Math.max(lo, x))` */
inline double clampJs(double x, double lo, double hi) { return jsMin(hi, jsMax(lo, x)); }

/** String(x) / `${x}`: the shortest text that reads back as the same number. */
std::string jsStr(double x);
/** x.toFixed(digits) */
std::string toFixed(double x, int digits);
/** Number(text) / parseFloat(text): NaN when it is not a number (`strict`: Number's rules, the whole text must be one) */
double jsNumber(const std::string& text, bool strict = true);
/** parseInt(text, 10): NaN when it does not begin with digits */
double jsParseInt(const std::string& text);
/** x.toString(radix) for whole numbers (radix 2 … 36) */
std::string toRadix(int64_t x, int radix);

/** s.padStart(n, c) / s.padEnd(n, c) for one-byte fill characters */
std::string padStart(const std::string& s, size_t n, char c = ' ');
std::string padEnd(const std::string& s, size_t n, char c = ' ');
/** s.trim() */
std::string trim(const std::string& s);
/** s.split(sep) (an empty separator is not supported) */
std::vector<std::string> split(const std::string& s, const std::string& sep);
/** parts.join(sep) */
std::string join(const std::vector<std::string>& parts, const std::string& sep);
/** s.replaceAll(from, to) */
std::string replaceAll(std::string s, const std::string& from, const std::string& to);
bool startsWith(const std::string& s, const std::string& prefix);
bool endsWith(const std::string& s, const std::string& suffix);
/** s.toLowerCase() / s.toUpperCase() for ASCII letters (other bytes are kept) */
std::string toLowerAscii(std::string s);
std::string toUpperAscii(std::string s);
/** encodeURIComponent(s) */
std::string encodeURIComponent(const std::string& s);
/** decodeURIComponent(s) (malformed escapes are kept as they are) */
std::string decodeURIComponent(const std::string& s);

}  // namespace fs

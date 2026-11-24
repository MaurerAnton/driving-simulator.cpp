// SPDX-License-Identifier: AGPL-3.0-only
/**
 * For tests that print what the port makes of something as lines of numbers
 * and compare them with what the original printed for the same (a file under
 * tests/fixtures, written by a script under tools/diff): the printing, and
 * the comparison – whole numbers and words must be the same, fractions agree
 * to a relative 1e-12 (an angle may differ in its last digit).
 */
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/js.h"
#include "platform/json.h"

namespace fs::dump {

inline std::vector<std::string>& lines() {
  static std::vector<std::string> all;
  return all;
}

inline void put(std::string& line, const std::string& text) {
  line += ' ';
  line += text;
}
inline void fmt(std::string& line, double v) { put(line, jsNumberText(v)); }
inline void fmt(std::string& line, float v) { put(line, jsNumberText(static_cast<double>(v))); }
inline void fmt(std::string& line, int v) { put(line, std::to_string(v)); }
inline void fmt(std::string& line, long v) { put(line, std::to_string(v)); }
inline void fmt(std::string& line, long long v) { put(line, std::to_string(v)); }
inline void fmt(std::string& line, unsigned v) { put(line, std::to_string(v)); }
inline void fmt(std::string& line, unsigned long v) { put(line, std::to_string(v)); }
inline void fmt(std::string& line, unsigned long long v) { put(line, std::to_string(v)); }
inline void fmt(std::string& line, bool v) { put(line, v ? "1" : "0"); }
inline void fmt(std::string& line, const std::string& v) {
  // (a text is one value: what is white in it becomes one underscore)
  std::string s;
  bool space = false;
  for (char c : v) {
    const bool white = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    if (!white) s += c;
    else if (!space) s += '_';
    space = white;
  }
  put(line, s.empty() ? "\"\"" : s);
}
inline void fmt(std::string& line, const char* v) { fmt(line, std::string(v)); }
template <class T>
void fmt(std::string& line, const std::vector<T>& list) {
  for (const T& v : list) fmt(line, v);
}

/** a line: its name and its values (a list of values is written out one by one) */
template <class... T>
void out(const char* name, const T&... values) {
  std::string line = name;
  (fmt(line, values), ...);
  lines().push_back(std::move(line));
}

/** a number for a list of numbers that changes when one of them does, or their order: Σ v[i] · (i mod 13 + 1) */
template <class A>
double sumOf(const A& a) {
  double s = 0;
  for (size_t i = 0; i < a.size(); i++) s += static_cast<double>(a[i]) * static_cast<double>((i % 13) + 1);
  return s;
}
/** the same for a stride-3 polyline, taken around its first point */
inline double lineSum(const std::vector<double>& p) {
  double s = 0;
  for (size_t i = 0; i + 2 < p.size(); i += 3) s += ((p[i] - p[0]) + 2 * (p[i + 1] - p[1]) + 3 * (p[i + 2] - p[2])) * static_cast<double>(((i / 3) % 13) + 1);
  return s;
}

inline bool whole(const std::string& t) {
  size_t i = !t.empty() && t[0] == '-' ? 1 : 0;
  if (i == t.size()) return false;
  for (; i < t.size(); i++)
    if (t[i] < '0' || t[i] > '9') return false;
  return true;
}

/**
 * Compare the lines printed so far with those of the original (the file).  Says what differs
 * (the first few lines) and returns how many lines do.
 */
inline int against(const std::string& file, double rtol = 1e-12) {
  std::ifstream f(file);
  if (!f) {
    std::fprintf(stderr, "%s: cannot be read (tests are run from the root of the port)\n", file.c_str());
    return 1;
  }
  std::vector<std::string> want;
  for (std::string l; std::getline(f, l);) want.push_back(l);
  const std::vector<std::string>& got = lines();
  int bad = 0;
  const size_t n = std::min(got.size(), want.size());
  for (size_t i = 0; i < n; i++) {
    if (got[i] == want[i]) continue;
    std::istringstream a(want[i]), b(got[i]);
    std::string x, y;
    bool differs = false;
    for (;;) {
      const bool ax = static_cast<bool>(a >> x);
      const bool by = static_cast<bool>(b >> y);
      if (!ax && !by) break;
      if (ax != by) {
        differs = true;
        break;
      }
      if (x == y) continue;
      if (whole(x) && whole(y)) {
        differs = true;
        break;
      }
      char* ex = nullptr;
      char* ey = nullptr;
      const double fx = std::strtod(x.c_str(), &ex);
      const double fy = std::strtod(y.c_str(), &ey);
      if (*ex || *ey || !(std::fabs(fx - fy) <= rtol * std::max(std::fabs(fx), std::fabs(fy)) || std::fabs(fx - fy) <= 1e-12)) {
        differs = true;
        break;
      }
    }
    if (differs) {
      if (bad < 12) std::fprintf(stderr, "%s line %zu:\n  original: %s\n  port:     %s\n", file.c_str(), i + 1, want[i].c_str(), got[i].c_str());
      bad++;
    }
  }
  if (got.size() != want.size()) {
    std::fprintf(stderr, "%s: %zu lines in the original, %zu from the port\n", file.c_str(), want.size(), got.size());
    bad++;
  }
  return bad;
}

}  // namespace fs::dump

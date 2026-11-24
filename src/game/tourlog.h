// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/tourlog.ts @ v1.7.7
/**
 * The tours driven, kept in the browser: when, how long the tour was meant
 * to be and how much of it was driven, whether it was driven to its end, and
 * its line for the map – also of a tour given up half-way.
 */
#pragma once

#include <cmath>
#include <string>
#include <vector>

#include "core/js.h"

namespace fs {

/** What a tour is to lead along: which roads it may not use, and which kind of street it is to look for. */
struct TourPrefs {
  bool noMotorway = false;
  bool noExpress = false;
  bool noMain = false;
  /** mostly along: any streets, small houses, blocks and town centres, the open country ('any' | 'small' | 'town' | 'country') */
  std::string mostly = "any";
};

inline const TourPrefs DEFAULT_TOUR_PREFS = {.noMotorway = false, .noExpress = false, .noMain = false, .mostly = "any"};

struct TourRecord {
  /** when it was driven (ISO time) */
  std::string date;
  /** metres planned, driven, and of them along streets new to the tour */
  double planned = 0;
  double driven = 0;
  double fresh = 0;
  /** driven to its end */
  bool done = false;
  /** its line (x, z, … whole metres) */
  std::vector<double> line;
  TourPrefs prefs;
};

/** The tours kept, the newest first. */
std::vector<TourRecord> loadTours();

void saveTour(const TourRecord& t);

/**
 * A line of at most `max` points (every n-th, the last always), in whole metres – small enough to keep many.
 * (`line`: any array of numbers, x, z, …)
 */
template <class Line>
std::vector<double> thinLine(const Line& line, double max = 500) {
  const double n = std::floor(static_cast<double>(line.size()) / 2);
  const double step = jsMax(1, std::ceil(n / max));
  std::vector<double> out;
  for (double i = 0; i < n; i += step) {
    const size_t k = static_cast<size_t>(i);
    out.push_back(jsRound(line[k * 2]));
    out.push_back(jsRound(line[k * 2 + 1]));
  }
  if (n > 0 && std::fmod(n - 1, step) != 0) {
    const size_t k = static_cast<size_t>(n - 1);
    out.push_back(jsRound(line[k * 2]));
    out.push_back(jsRound(line[k * 2 + 1]));
  }
  return out;
}

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The towns the comparison tools drive in – the same as `townOptions` of tools/diff/sim_towns.ts
 * (keep the two alike): plain (4 × 4 streets), lights (two junctions with lights, a zebra and a
 * pedestrian light), wide (two lanes each way on the middle row, one-way streets, a cul-de-sac).
 */
#pragma once

#include <string>

#include "tests/town.h"

namespace fs::simdiff {

inline TownOptions townOptions(const std::string& which) {
  TownOptions o;
  if (which == "lights") {
    o.cols = 4;
    o.rows = 4;
    o.block = 100;
    o.pavement = 2;
    o.lights = {{1, 1}, {2, 2}};
    o.crossings = {{{0, 1, 1, 1}, 0.5, XING_ZEBRA}, {{2, 1, 3, 1}, 0.4, XING_SIGNAL}};
    return o;
  }
  if (which == "wide") {
    o.cols = 5;
    o.rows = 4;
    o.block = 120;
    o.wide = {{0, 1, 1, 1}, {1, 1, 2, 1}, {2, 1, 3, 1}, {3, 1, 4, 1}};
    o.oneWay = {{0, 2, 1, 2}, {1, 2, 2, 2}};
    o.missing = {{3, 0, 4, 0}};
    o.deadEnd = 60;
    return o;
  }
  o.cols = 4;
  o.rows = 4;
  o.block = 100;
  return o;
}

}  // namespace fs::simdiff

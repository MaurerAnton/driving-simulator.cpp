// LINK: game core platform
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The tours driven (src/game/tourlog.h): kept in the browser under the original's key as the
 * JSON the original writes – the newest first, forty at most – and a tour's line thinned for it.
 * (The original has no test of its own for game/tourlog.ts.)
 */
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "game/tourlog.h"
#include "platform/plat.h"

using namespace fs;

static const char* const KEY = "fahrsim.tours";

int main() {
  plat::localStorage().removeItem(KEY);
  assert(loadTours().empty());

  // a tour is stored as the original stores it, and read back
  TourRecord t;
  t.date = "2026-11-24T08:15:30.000Z";
  t.planned = 5200;
  t.driven = 3417;
  t.fresh = 2980;
  t.done = false;
  t.line = {10, -20, 35, -40.5};
  t.prefs = {.noMotorway = true, .noExpress = false, .noMain = true, .mostly = "small"};
  saveTour(t);
  assert(*plat::localStorage().getItem(KEY) ==
         R"([{"date":"2026-11-24T08:15:30.000Z","planned":5200,"driven":3417,"fresh":2980,"done":false,"line":[10,-20,35,-40.5],"prefs":{"noMotorway":true,"noExpress":false,"noMain":true,"mostly":"small"}}])");
  std::vector<TourRecord> list = loadTours();
  assert(list.size() == 1);
  assert(list[0].date == t.date && list[0].planned == 5200 && list[0].driven == 3417 && list[0].fresh == 2980 && !list[0].done);
  assert(list[0].line == t.line && list[0].prefs.noMotorway && !list[0].prefs.noExpress && list[0].prefs.noMain && list[0].prefs.mostly == "small");

  // the newest first; forty are kept
  for (int i = 0; i < 45; i++) {
    TourRecord r;
    r.date = "tour " + std::to_string(i);
    r.done = true;
    saveTour(r);
  }
  list = loadTours();
  assert(list.size() == 40);
  assert(list[0].date == "tour 44" && list[0].done && list[39].date == "tour 5");
  assert(list[0].prefs.mostly == DEFAULT_TOUR_PREFS.mostly && !list[0].prefs.noMotorway);

  // what is no list of tours is none; an entry that is no tour is left out
  plat::localStorage().setItem(KEY, "{\"date\":\"x\"}");
  assert(loadTours().empty());
  plat::localStorage().setItem(KEY, "not json");
  assert(loadTours().empty());
  plat::localStorage().setItem(KEY, R"([null,5,{"date":"a","line":[1,2]},{"date":7,"line":[]},{"date":"b"},{"date":"c","line":[3,4],"done":true}])");
  list = loadTours();
  assert(list.size() == 2 && list[0].date == "a" && list[1].date == "c" && list[1].done && list[1].line == std::vector<double>({3, 4}));
  plat::localStorage().removeItem(KEY);

  // a line of at most `max` points – every n-th, the last always – in whole metres
  const std::vector<double> line = {0.4, 0.6, 1.5, -1.5, 2.49, 2.5, 3, 3, 4.2, 4.8};
  assert(thinLine(line) == std::vector<double>({0, 1, 2, -1, 2, 3, 3, 3, 4, 5}));
  assert(thinLine(line, 2) == std::vector<double>({0, 1, 3, 3, 4, 5}));
  assert(thinLine(line, 3) == std::vector<double>({0, 1, 2, 3, 4, 5}));
  assert(thinLine(std::vector<float>{1.5f, 2.5f, 7}) == std::vector<double>({2, 3}));
  assert(thinLine(std::vector<double>{}).empty());

  std::printf("game_tourlog_test: ok\n");
  return 0;
}

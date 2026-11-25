// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The bus on a line: serving a stop – front door at the pole, doors open,
 * people through, doors closed, indicator on to pull out – and what the
 * instructor says when one of it goes wrong.
 *
 * Origin: tests/game/busline.test.ts @ v1.7.7
 */
// LINK: game core platform
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "game/busline.h"

using namespace fs;

namespace {

constexpr double DT = 1.0 / 30;
const std::vector<BusStopAhead> STOPS = {{7, 120, "Rathaus"}};

/** drive the bus's front from `from` to `to` (m along the lane) at `v` m/s with the indicator as given */
double drive(BusService& bs, double from, double to, double v, int indicator, std::vector<BusEvent>& ev) {
  double front = from;
  while (front < to) {
    front = std::min(to, front + v * DT);
    bs.step(DT, front, STOPS, v, indicator, ev);
  }
  return front;
}

std::string faults(const std::vector<BusEvent>& ev) {
  std::string out;
  for (const BusEvent& e : ev) out += (out.empty() ? "" : ",") + e.fault;
  return out;
}

}  // namespace

int main() {
  // ---- a stop served: stand at the pole, open, everybody through, close, indicate, pull out – nothing to say
  {
    BusService bs([] { return 0.6; });
    std::vector<BusEvent> ev;
    double front = drive(bs, 0, 119, 8, 0, ev);
    assert(bs.next(front, STOPS) && bs.next(front, STOPS)->name == "Rathaus");
    assert(bs.atStop(front, STOPS, 0));   // standing at the stop
    bs.toggleDoors(front, STOPS, 0, ev);
    assert(bs.doorsOpen && bs.passengers() > 0);   // people get off and on
    for (double t = 0; t < 30; t += DT) bs.step(DT, front, STOPS, 0, 0, ev);
    assert(bs.passengers() == 0);
    bs.toggleDoors(front, STOPS, 0, ev);
    assert(bs.served == 1);
    for (double t = 0; t < 2; t += DT) bs.step(DT, front, STOPS, 0, -1, ev);
    front = drive(bs, front, 200, 5, -1, ev);
    assert(ev.empty());
    assert(!bs.next(front, STOPS));   // no stop ahead any more
  }

  // ---- driving past, the doors away from the stop or open while moving, pulling out without indicating: faults
  {
    BusService past([] { return 0.6; });
    std::vector<BusEvent> ev1;
    drive(past, 0, 200, 10, 0, ev1);
    assert(faults(ev1) == "busStopMissed");

    BusService away([] { return 0.6; });
    std::vector<BusEvent> ev2;
    const double front = drive(away, 0, 60, 8, 0, ev2);
    away.toggleDoors(front, STOPS, 0, ev2);
    drive(away, front, 70, 3, 0, ev2);
    assert(faults(ev2) == "busStopPlace,busDoorsMoving");

    BusService blind([] { return 0.0; });
    std::vector<BusEvent> ev3;
    const double at = drive(blind, 0, 120, 8, 0, ev3);
    blind.toggleDoors(at, STOPS, 0, ev3);
    blind.toggleDoors(at, STOPS, 0, ev3);
    drive(blind, at, 150, 4, 0, ev3);
    assert(faults(ev3) == "busPullOut");
  }

  std::printf("busline: all checks passed\n");
  return 0;
}

// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/busline.ts @ v1.7.7
#include "game/busline.h"

#include <cmath>

#include "core/random.h"

namespace fs {

BusService::BusService(std::function<double()> random) : random_(random ? std::move(random) : std::function<double()>(rnd)) {}

const BusStopAhead* BusService::next(double front, const std::vector<BusStopAhead>& stops) const {
  for (const BusStopAhead& st : stops)
    if (!done_.count(st.id) && st.s > front - BUS_STOP_PAST) return &st;
  return nullptr;
}

const BusStopAhead* BusService::atStop(double front, const std::vector<BusStopAhead>& stops, double speed) const {
  if (std::abs(speed) > 0.2) return nullptr;
  const BusStopAhead* st = next(front, stops);
  return st && front >= st->s - BUS_STOP_BEFORE && front <= st->s + BUS_STOP_PAST ? st : nullptr;
}

void BusService::toggleDoors(double front, const std::vector<BusStopAhead>& stops, double speed, std::vector<BusEvent>& events) {
  if (std::abs(speed) > 0.2) return;
  if (!doorsOpen) {
    doorsOpen = true;
    movingOpen_ = false;
    const BusStopAhead* st = atStop(front, stops, speed);
    if (st) {
      at_ = st->id;
      alighting = static_cast<int>(std::floor(random_() * 5));
      boarding = static_cast<int>(std::floor(random_() * 6));
      nextPassenger_ = 1.4;
    } else {
      at_ = -1;
      events.push_back(BusEvent{"busStopPlace", "minor"});
    }
    return;
  }
  if (passengers() > 0) events.push_back(BusEvent{"busDoorsEarly", "minor"});
  doorsOpen = false;
  if (at_ >= 0) {
    if (passengers() == 0) served++;
    done_.insert(at_);
    leaving_ = Leaving{front, false};
  }
  boarding = alighting = 0;
  at_ = -1;
}

void BusService::step(double dt, double front, const std::vector<BusStopAhead>& stops, double speed, int indicator, std::vector<BusEvent>& events) {
  // people through the open doors
  if (doorsOpen && at_ >= 0 && passengers() > 0) {
    nextPassenger_ -= dt;
    if (nextPassenger_ <= 0) {
      if (alighting > 0) alighting--;
      else boarding--;
      nextPassenger_ = 1.0 + random_() * 0.8;
    }
  }
  // driving off with the doors open
  if (doorsOpen && std::abs(speed) > 0.5 && !movingOpen_) {
    movingOpen_ = true;
    events.push_back(BusEvent{"busDoorsMoving", "major"});
  }
  // pulling out of the stop: the indicator to the left first
  if (leaving_) {
    Leaving& l = *leaving_;
    if (indicator < 0) l.indicated = true;
    if (std::abs(speed) > 1.5 && !l.indicated) {
      events.push_back(BusEvent{"busPullOut", "minor"});
      leaving_.reset();
    } else if (front - l.from > 25 || front < l.from - 5) leaving_.reset();
  }
  // a stop driven past: its front well beyond the place, the doors never opened there
  for (const BusStopAhead& st : stops) {
    if (done_.count(st.id) || at_ == st.id) continue;
    if (front > st.s + BUS_STOP_PAST + 3 && front < st.s + 40) {
      done_.insert(st.id);
      events.push_back(BusEvent{"busStopMissed", "major"});
    }
  }
}

}  // namespace fs

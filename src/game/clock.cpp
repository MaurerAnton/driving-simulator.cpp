// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/clock.ts @ v1.7.7
#include "game/clock.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "platform/plat.h"

namespace fs {

GameClock::GameClock(std::function<double()> realNowIn) : realNow(std::move(realNowIn)) {
  if (!realNow) realNow = [] { return plat::dateMs(); };
}

double GameClock::ms() const { return own ? *own : realNow(); }

void GameClock::tick(double dt) {
  if (own && dt > 0) *own += dt * 1000;
}

void GameClock::set(double ms) {
  if (!std::isfinite(ms)) return;
  own = ms;
  chosen = true;
}

void GameClock::setRate(double rate) {
  const double r = std::isfinite(rate) ? std::max(1.0, std::min(MAX_RATE, rate)) : 1;
  if (r > 1 && !own) own = realNow();
  speed = r;
  if (r == 1 && !chosen) own.reset();
}

void GameClock::reset() {
  own.reset();
  chosen = false;
  speed = 1;
}

SunPlace sunAt(double ms, double latDeg, double lonDeg) {
  const double rad = std::numbers::pi / 180;
  // days since J2000.0
  const double d = ms / 86400000 - 10957.5;
  // the sun's mean longitude and mean anomaly, its ecliptic longitude
  const double L = std::fmod(280.46 + 0.9856474 * d, 360);
  const double g = std::fmod(357.528 + 0.9856003 * d, 360) * rad;
  const double lambda = (L + 1.915 * std::sin(g) + 0.02 * std::sin(2 * g)) * rad;
  const double eps = (23.439 - 0.0000004 * d) * rad;
  // right ascension and declination
  const double ra = std::atan2(std::cos(eps) * std::sin(lambda), std::cos(lambda));
  const double dec = std::asin(std::sin(eps) * std::sin(lambda));
  // the local sidereal time, the hour angle
  const double gmst = std::fmod(18.697374558 + 24.06570982441908 * d, 24);
  const double lst = (gmst * 15 + lonDeg) * rad;
  const double ha = lst - ra;
  const double lat = latDeg * rad;
  const double sinEl = std::sin(lat) * std::sin(dec) + std::cos(lat) * std::cos(dec) * std::cos(ha);
  const double el = std::asin(std::max(-1.0, std::min(1.0, sinEl)));
  // azimuth from north, clockwise
  const double az = std::atan2(-std::sin(ha), std::tan(dec) * std::cos(lat) - std::sin(lat) * std::cos(ha));
  return {.elevation = el / rad, .azimuth = std::fmod(std::fmod(az / rad, 360) + 360, 360)};
}

Season seasonOf(double date) {
  const int m = plat::localFields(date).month;
  return m == 11 || m <= 1 ? "winter" : m <= 4 ? "spring" : m <= 7 ? "summer" : "autumn";
}

}  // namespace fs

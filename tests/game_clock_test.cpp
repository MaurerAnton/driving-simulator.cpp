// LINK: game core platform
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The game's clock: the real time unless the arcade mode sets another
 * moment or lets it run faster – and where the sun stands at a moment.
 *
 * Origin: tests/game/clock.test.ts @ v1.7.7
 */
#include <cassert>
#include <cmath>
#include <cstdio>

#include "core/date.h"
#include "game/clock.h"
#include "platform/plat.h"

using namespace fs;

/** what the original works out for the moments of the sun's case below (node, v1.7.7) */
static const double SUMMER_ELEVATION = 62.132347768782374;
static const double SUMMER_AZIMUTH = 179.0085125215778;
static const double WINTER_ELEVATION = 15.234727382307417;
static const double NIGHT_ELEVATION = -57.58574688914396;
static const double MORNING_AZIMUTH = 59.47310506429994;

/** `new Date(year, month, day)`: the beginning of a day of the device's own calendar, in milliseconds since 1970 */
static double localDay(int year, int month, int day) {
  const double utc = utcMs(year, month, day);
  // (the offset at that day, looked up twice: it is the one of the local midnight, not of the UTC one)
  double ms = utc + plat::timezoneOffsetMin(utc) * 60000;
  ms = utc + plat::timezoneOffsetMin(ms) * 60000;
  return ms;
}

int main() {
  // the clock is the real one until the arcade mode changes it
  {
    double real = utcMs(2026, 10, 16, 12, 0, 0);
    GameClock c([&real] { return real; });
    assert(c.real() == true);
    assert(c.ms() == real);
    c.tick(10);
    assert(c.ms() == real);   // the real clock is not moved by the frames
    real += 5000;
    assert(c.ms() == real);
    assert(c.now() == real);
  }

  // run faster: the time goes on rate times as fast, and back at 1× it is the real time again
  {
    double real = utcMs(2026, 10, 16, 12, 0, 0);
    GameClock c([&real] { return real; });
    c.setRate(4);
    assert(c.real() == false);
    assert(c.rate() == 4);
    c.tick(40);
    assert(c.ms() - real == 40000);   // the world went on by what was simulated (10 s of frames at 4×)
    real += 10000;
    c.setRate(1);
    assert(c.real() == true);   // only sped up: back to the real time (the timetables catch up)
    assert(c.ms() == real);
    c.setRate(100);
    assert(c.rate() == MAX_RATE);
  }

  // another moment chosen: it runs on from there, also after running faster
  {
    const double real = utcMs(2026, 10, 16, 12, 0, 0);
    GameClock c([real] { return real; });
    const double evening = utcMs(2026, 10, 20, 17, 30, 0);
    c.set(evening);
    assert(c.travelled() == true);
    c.tick(60);
    assert(c.ms() == evening + 60000);
    c.setRate(2);
    c.tick(60);
    c.setRate(1);
    assert(c.real() == false);   // a chosen moment stays
    assert(c.ms() == evening + 120000);
    c.reset();
    assert(c.real() == true);
    assert(c.travelled() == false);
    assert(c.ms() == real);
  }

  // the sun over northern Hesse: high at midsummer noon, low at midwinter noon, below the horizon at midnight
  {
    // solar noon there is about 11:22 UTC
    const SunPlace summer = sunAt(utcMs(2026, 5, 21, 11, 22));
    assert(std::fabs(summer.elevation - 62.1) < 1);
    assert(std::fabs(summer.azimuth - 180) < 5);   // due south
    const SunPlace winter = sunAt(utcMs(2026, 11, 21, 11, 30));
    assert(std::fabs(winter.elevation - 15.3) < 1);
    const SunPlace night = sunAt(utcMs(2026, 10, 16, 23, 0));
    assert(night.elevation < -30);
    const SunPlace morning = sunAt(utcMs(2026, 5, 21, 4, 0));
    assert(morning.azimuth > 45 && morning.azimuth < 90);   // the morning sun stands in the north-east
    // (the original's numbers for these moments, to a millionth of a degree)
    assert(std::fabs(summer.elevation - SUMMER_ELEVATION) < 1e-6 && std::fabs(summer.azimuth - SUMMER_AZIMUTH) < 1e-6);
    assert(std::fabs(winter.elevation - WINTER_ELEVATION) < 1e-6 && std::fabs(night.elevation - NIGHT_ELEVATION) < 1e-6 && std::fabs(morning.azimuth - MORNING_AZIMUTH) < 1e-6);
  }

  // the seasons by the months
  {
    assert(seasonOf(localDay(2026, 0, 15)) == "winter");
    assert(seasonOf(localDay(2026, 3, 15)) == "spring");
    assert(seasonOf(localDay(2026, 6, 15)) == "summer");
    assert(seasonOf(localDay(2026, 9, 15)) == "autumn");
    assert(seasonOf(localDay(2026, 11, 1)) == "winter");
  }

  // the game's one clock: the real time
  {
    assert(gameClock.real() && !gameClock.travelled() && gameClock.rate() == 1);
    assert(std::fabs(gameNow() - plat::dateMs()) < 2000);
    assert(RATES.size() == 4 && RATES.back() == MAX_RATE);
  }

  std::printf("game_clock_test: ok\n");
  return 0;
}

// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/timetable.ts @ v1.7.7 (the clock in Kassel: berlinOffset, berlinClock, easter, isHoliday)
/**
 * What the signs with times and the roadworks use of the timetables' file: the
 * clocks in Kassel, and the public holidays of Hesse.  (In a namespace of its
 * own while sim/timetable is another part's: when that is merged, these give way
 * to its functions of the same names.)
 */
#pragma once

#include <map>
#include <set>
#include <utility>

#include "core/date.h"

namespace fs::kassel {

/** The day of the month of the last Sunday of a month (0–11) in a year. */
inline int lastSunday(int y, int month) {
  const DateFields end = utcFields(utcMs(y, month + 1, 0));
  return end.date - end.day;
}

/** How many seconds the clocks in Kassel are ahead of UTC at a moment (ms since 1970): 7200 in summer time, else 3600. */
inline double berlinOffset(double ms) {
  const int y = utcFields(ms).year;
  const double start = utcMs(y, 2, lastSunday(y, 2), 1);
  const double end = utcMs(y, 9, lastSunday(y, 9), 1);
  return ms >= start && ms < end ? 7200 : 3600;
}

struct BerlinClock {
  int y = 0;
  int m = 0;
  int d = 0;
  int weekday = 0;
  double s = 0;
};

/** The date and time on the clocks in Kassel at a moment: year, month (1–12), day, weekday (0 Sunday … 6 Saturday), seconds after midnight. */
inline BerlinClock berlinClock(double ms) {
  const DateFields local = utcFields(ms + berlinOffset(ms) * 1000);
  const double s = local.hours * 3600 + local.minutes * 60 + local.seconds + local.milliseconds / 1000.0;
  return {local.year, local.month + 1, local.date, local.day, s};
}

/** Easter Sunday of a year (Gregorian calendar, the anonymous algorithm in Meeus' form): [month 1–12, day]. */
inline std::pair<int, int> easter(int y) {
  const int a = y % 19;
  const int b = y / 100;
  const int c = y % 100;
  const int d = b / 4;
  const int e = b % 4;
  const int f = (b + 8) / 25;
  const int g = (b - f + 1) / 3;
  const int h = (19 * a + b - d - g + 15) % 30;
  const int i = c / 4;
  const int k = c % 4;
  const int l = (32 + 2 * e + 2 * i - h - k) % 7;
  const int m = (a + 11 * h + 22 * l) / 451;
  const int n = h + l - 7 * m + 114;
  return {n / 31, (n % 31) + 1};
}

/** The public holidays of Hesse in a year, as month * 100 + day. */
inline const std::set<int>& holidays(int y) {
  static auto* cache = new std::map<int, std::set<int>>();
  auto it = cache->find(y);
  if (it != cache->end()) return it->second;
  const auto [em, ed] = easter(y);
  const double sunday = utcMs(y, em - 1, ed);
  const auto after = [&](int days) {
    const DateFields t = utcFields(sunday + days * 86400.0 * 1000);
    return (t.month + 1) * 100 + t.date;
  };
  // New Year, Good Friday, Easter Monday, Labour Day, Ascension, Whit Monday, Corpus Christi, German Unity, Christmas
  return (*cache)[y] = std::set<int>{101, after(-2), after(1), 501, after(39), after(50), after(60), 1003, 1225, 1226};
}

/** Whether a date (month 1–12) is a public holiday in Hesse. */
inline bool isHoliday(int y, int m, int d) { return holidays(y).count(m * 100 + d) > 0; }


}  // namespace fs::kassel

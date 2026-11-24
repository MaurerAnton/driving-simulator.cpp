// SPDX-License-Identifier: AGPL-3.0-only
/**
 * `new Date(ms)` of the original: the calendar fields of a moment, in UTC or in
 * the local time of the device (the host says how far that is from UTC:
 * plat::timezoneOffsetMin).
 */
#pragma once

#include <cmath>
#include <cstdint>

namespace fs {

struct DateFields {
  /** getFullYear(), getMonth() (0 … 11), getDate() (1 … 31), getDay() (0 = Sunday) */
  int year = 1970, month = 0, date = 1, day = 4;
  /** getHours(), getMinutes(), getSeconds(), getMilliseconds() */
  int hours = 0, minutes = 0, seconds = 0, milliseconds = 0;
};

/** The fields of a moment given as milliseconds since 1970, read as UTC (getUTC…). */
inline DateFields utcFields(double ms) {
  DateFields f;
  const double dayMs = 86400000.0;
  const double days = std::floor(ms / dayMs);
  double rest = ms - days * dayMs;
  f.hours = static_cast<int>(rest / 3600000.0);
  rest -= f.hours * 3600000.0;
  f.minutes = static_cast<int>(rest / 60000.0);
  rest -= f.minutes * 60000.0;
  f.seconds = static_cast<int>(rest / 1000.0);
  f.milliseconds = static_cast<int>(rest - f.seconds * 1000.0);
  // (days since 1970-01-01 → the civil date: Howard Hinnant's algorithm)
  const int64_t z = static_cast<int64_t>(days) + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  f.year = static_cast<int>(static_cast<int64_t>(yoe) + era * 400 + (m <= 2 ? 1 : 0));
  f.month = static_cast<int>(m) - 1;
  f.date = static_cast<int>(d);
  const int64_t wd = (static_cast<int64_t>(days) % 7 + 11) % 7;   // 1970-01-01 was a Thursday
  f.day = static_cast<int>(wd);
  return f;
}

/** Milliseconds since 1970 of a UTC calendar date (Date.UTC(year, month, date, hours, minutes, seconds, ms)). */
inline double utcMs(int year, int month, int date, int hours = 0, int minutes = 0, int seconds = 0, int ms = 0) {
  int64_t y = year + static_cast<int64_t>(std::floor(month / 12.0));
  int mo = ((month % 12) + 12) % 12 + 1;
  y -= mo <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153u * static_cast<unsigned>(mo + (mo > 2 ? -3 : 9)) + 2) / 5;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const double days = static_cast<double>(era * 146097 + static_cast<int64_t>(doe) - 719468) + (date - 1);
  return days * 86400000.0 + hours * 3600000.0 + minutes * 60000.0 + seconds * 1000.0 + ms;
}

}  // namespace fs

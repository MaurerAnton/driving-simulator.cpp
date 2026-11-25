// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/signtime.ts @ v1.7.7
/**
 * When a sign counts.
 *
 * Many signs hold only at certain times: "22–6 h" under a speed limit (the
 * quiet of the night), "Mo–Fr 7–17 h" in front of a school, "werktags" under
 * a no-stopping sign.  The time stands on an additional panel below the sign
 * (Zusatzzeichen 1040-30, 1042-33 …); OpenStreetMap writes the same as a
 * condition – maxspeed:conditional = "30 @ (Mo-Fr 07:00-17:00)" – and puts it
 * into the code of the panel: "DE:274[30],1042-33[Mo-Fr 07:00-17:00]".
 *
 * This file reads such times – the part of OpenStreetMap's opening_hours
 * syntax that conditions use: weekdays, public holidays, hours, "off" –,
 * says whether one holds at a moment of the game's clock, and writes it the
 * way the panels show it ("Mo–Fr 7–17 h").  What lies beyond that part
 * (months, dates, sunset, "wet", "weight>7.5") is not understood: such a
 * condition is left out rather than guessed at.
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace fs {

/** A moment as the signs see it (in Hesse): weekday (0 Sunday … 6 Saturday), minutes after midnight, whether today and yesterday are public holidays. */
struct SignTime {
  int day = 0;
  double min = 0;
  bool holiday = false;
  bool holidayBefore = false;
};

/** bit of a day set for the public holidays (bits 0–6 are Sunday … Saturday) */
inline constexpr int PH = 1 << 7;

/**
 * One span of a time window: on which days (bits 0–6 Sunday … Saturday, PH:
 * public holidays) it begins, from when to when (minutes after midnight of
 * that day; `from` < `to`).  Beyond 24 h it runs into the next day:
 * "22:00-06:00" is 1320 … 1800.
 */
struct TimeSpan {
  int days = 0;
  double from = 0;
  double to = 0;
  bool operator==(const TimeSpan&) const = default;
};

/** When something holds: in one of its spans, and not on the days it is off ("PH off": not on public holidays). */
struct TimeWindow {
  std::vector<TimeSpan> spans;
  int off = 0;
  bool operator==(const TimeWindow&) const = default;
};

/** The moment `ms` (ms since 1970) on the clocks in Kassel, as the signs see it. */
SignTime signTimeOf(double ms);

/**
 * Read a time as conditions and panels write it: "22:00-06:00",
 * "Mo-Fr 07:00-17:00", "Mo-Fr 06:00-10:00, 19:00-21:00; Sa 06:00-10:00",
 * "Su,PH", "Mo-Sa 06:00-12:00; PH off", "werktags 18-19", "22-6" (in
 * brackets or not).  None for anything else.
 */
std::optional<TimeWindow> parseTimeWindow(const std::string& raw);

/** Does the window hold at moment `t`? */
bool holds(const TimeWindow& w, const SignTime& t);

/**
 * The lines of a panel for a window, as German panels write them:
 * ["22–6 h"], ["Mo–Fr", "7–17 h"], ["werktags", "18–19 h"],
 * ["Mo–Fr 6–10 h, 19–21 h", "Sa 6–10 h"].  One line per group of days that
 * has its own hours – the first one parted into days and hours when there is
 * only one.
 */
std::vector<std::string> windowLines(const TimeWindow& w);

/** The same in one line ("Mo–Fr 7–17 h") – for the speed-limit display. */
std::string windowText(const TimeWindow& w);

/** Does a window name days at all (a panel 1042, "with weekdays") – or only hours (1040)? */
bool hasDays(const TimeWindow& w);

/** The panel for a time: 1042-33 where it names weekdays ("Mo–Fr 7–17 h"), 1040-30 where it gives hours alone ("22–6 h"). */
std::string panelCodeFor(const TimeWindow& w);

/**
 * A window written back in OpenStreetMap's way ("Mo-Fr 07:00-17:00; PH off")
 * – how the sign codes carry it (`1042-33:Mo-Fr 07:00-17:00`), and the same
 * text for the same time however it was written.
 */
std::string windowSpec(const TimeWindow& w);

/** One alternative of a conditional tag: the value, and when it holds – a time window, or a word the game knows of ('wet'). */
struct Conditional {
  std::string value;
  /** when `wet` is false */
  TimeWindow when;
  /** the original's `when === 'wet'` */
  bool wet = false;
};

/**
 * The alternatives of a conditional tag of OpenStreetMap
 * ("30 @ (22:00-06:00); 50 @ (06:00-22:00)", "80 @ wet").  Those whose
 * condition is not a plain time (or "wet") are left out: "none @
 * destination", "30 @ (weight>7.5)", "no @ (2026 Mar 21-2026 Jun 14)".
 */
std::vector<Conditional> parseConditional(const std::string& tag);

}  // namespace fs

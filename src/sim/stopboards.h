// SPDX-License-Identifier: AGPL-3.0-only
/**
 * What the departure displays at the stops show, and where there is one.
 *
 * A display at a stop place shows the next trams and buses that leave there
 * by the timetables (src/sim/timetable.ts): rows of line, destination and
 * minutes, and the time of day – at the moment the game says, since its
 * clock need not be the real one.  It is asked by the stop place (its number
 * in stops.json.gz), not by the name: there is a "Rathaus" in every town.
 *
 * Which pole gets which display: a big board (three rows and the clock) where
 * OpenStreetMap says there is one, at a stop with a shelter and at the trams'
 * stops; a small one (two rows) at a simple pole – and none where no tram or
 * bus of the timetables calls.  The drawing is in src/render/stopmodels.ts
 * (DepartureBoard) and src/render/transit.ts; this part is kept apart so that
 * it can be tested without a browser.
 *
 * Origin: src/sim/stopboards.ts @ v1.7.7
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "sim/timetable.h"

namespace fs {

/** One row of a display: the line, where it goes, and in how many minutes it leaves (0: now) – or, an hour and more ahead, at what time ("14:35"). */
struct BoardRow {
  std::string line;
  std::string destination;
  /** the minutes (when `at` is empty), else the time of day */
  int minutes = 0;
  std::string at;
  TransitKind kind;
  /** the line's colour ('' where it has none) */
  std::string colour;
};

/** A stop place as the displays need it: its number in stops.json.gz and its name. */
struct BoardPlace {
  int id = 0;
  std::string name;
};

/** The sizes of display: the big board on a post of its own, and the small one beside a simple pole ('board' | 'small'). */
using DisplaySize = std::string;

/** Does the timetable know trams or buses that call at this stop place?  (A file of version 1 knows the trams' stops by name only.) */
bool servedAt(const Timetable& tt, const BoardPlace& place);

/**
 * The rows a display at a stop place shows at a moment: the next `n`
 * departures of its trams and buses within `horizon` seconds, soonest first
 * – null where the timetable knows nothing of the place (its display shows
 * what it can without).
 */
std::optional<std::vector<BoardRow>> boardRows(const Timetable& tt, const BoardPlace& place, const ServiceTime& at, int n = 3, double horizon = 2 * 3600);

/** A time of day as a display shows it, seconds after midnight → "07:05" (past midnight too: 25:10 is "01:10"). */
std::string clockText(double s);

/**
 * The display a pole of a stop gets (by its STOP_FLAG bits and its place's
 * kind), where trams or buses of the timetables call (`served`): the big
 * board where OpenStreetMap maps one (with or without a timetable: it shows
 * the clock at least), at a pole with a shelter and at a stop of the trams;
 * the small one at any other pole; none ('') where nothing calls.
 */
DisplaySize displayAt(int poleFlags, int placeKind, bool served);

}  // namespace fs

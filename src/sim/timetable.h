// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The timetables of the trams and buses (timetable.json.gz, made by
 * tools/build-timetable.ts from a GTFS feed) and what the game asks of them:
 * what leaves a stop next (the departure boards – trams and buses
 * together), where the trips of a line are at a time of day (so that the
 * trams and buses of the traffic run where and when the timetable says, and
 * the map can show every one under way), and which trip a vehicle standing
 * at a stop belongs to (to tell whether it is on time).
 *
 * A tram's line is named by its place in lines.json.gz, a bus's by its place
 * in buslines.json.gz; a stop by its name, or by its stop place (its number
 * in stops.json.gz) – names repeat from town to town ("Rathaus", "Bahnhof"),
 * places do not.
 *
 * Times are seconds after midnight of a service day, local time in Kassel
 * (Europe/Berlin).  As in GTFS, the trips of a day that run after midnight
 * go on counting – 25:10 is ten past one in the night after – so a moment
 * looks at three service days: yesterday's late trips, today's, and
 * tomorrow's first ones, each with its own day type (Monday–Friday,
 * Saturday, Sunday or holiday).
 *
 * Origin: src/sim/timetable.ts @ v1.7.7
 */
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "world/types.h"

namespace fs {

/** The newest version of timetable.json.gz this reads (1: the trams alone; 2: with the buses, the lines' colours and the stop places of the trams' stops). */
inline constexpr int TIMETABLE_VERSION = 2;

/** DAY.WEEKDAY, SATURDAY, SUNDAY */
using DayType = int;
inline constexpr DayType DAY_WEEKDAY = 0;
inline constexpr DayType DAY_SATURDAY = 1;
inline constexpr DayType DAY_SUNDAY = 2;

inline constexpr double DAY_SECONDS = 86400;

/** What runs a line: 'tram' – a line of lines.json.gz (the trams and RegioTrams), 'bus' – a line of buslines.json.gz. */
using TransitKind = std::string;

/** A moment as the timetable sees it. */
struct ServiceTime {
  /** seconds after local midnight, 0 … 86 400 */
  double s = 0;
  /** the day types of today, yesterday and tomorrow */
  DayType day = 0;
  DayType prev = 0;
  DayType next = 0;
};

struct TimetableLine;

/** One way the trips of a line run: the same stops at the same intervals. */
struct TimetablePattern {
  TimetableLine* line = nullptr;
  /** the stops it calls at, in order, by name: as the tracks name them (a tram), as its stop place is named (a bus) */
  std::vector<std::string> stops;
  /**
   * the same stops by their place in the line's stops – a tram's in
   * lines.json.gz `stops` (where their piece of track and mark are), a bus's
   * in BusLines `stops` (src/world/buslines.ts: where on the line's ways) –
   * −1: not known
   */
  std::vector<int> lineStops;
  /** the same stops as stop places: their number in stops.json.gz (−1 where not known – a tram's stop in a file without them) */
  std::vector<int> places;
  /** the departure from each and the arrival at each, seconds after the departure from the first */
  std::vector<double> dep;
  std::vector<double> arr;
  /** where its trips go, as shown at its first stop (a tram's: the last stop of the whole trip, which may lie beyond the map) */
  std::string to;
  /** where they go as shown at each of its stops – a bus may change it on the way (a loop names its far end, then where it ends) */
  std::vector<std::string> dests;
  /** its trips come from further back than its first stop / go on beyond its last (beyond the line's stops on the map) */
  bool pre = false;
  bool post = false;
  /** when its trips leave the first stop, seconds after midnight of their service day, sorted – by day type */
  std::array<std::vector<double>, 3> starts;
};

struct TimetableLine {
  TransitKind kind;
  /** its place in lines.json.gz `lines` (a tram) or in buslines.json.gz `lines` (a bus) */
  int index = 0;
  std::string ref;
  /** its colour ("#00954B", or a name OpenStreetMap uses: "red") – OpenStreetMap's, else the feed's; '' where neither has one */
  std::string colour;
  /** (reserved for all of the file's patterns before the first is added: they stay in place) */
  std::vector<TimetablePattern> patterns;
};

/** A pattern calling at a stop, and where in it (index into its `stops`). */
struct StopCall {
  const TimetablePattern* pattern = nullptr;
  int k = 0;
};

struct Timetable {
  /** the trams' lines, by their place in lines.json.gz `lines` – null where a line has none */
  std::vector<std::unique_ptr<TimetableLine>> lines;
  /** the buses' lines, by their place in buslines.json.gz `lines` – null where a line has none (every one, in a file without buses) */
  std::vector<std::unique_ptr<TimetableLine>> buses;
  std::string attribution;
  /** the days the feed was valid for, and the week the trips were taken from (YYYY-MM-DD) */
  std::array<std::string, 2> valid;
  std::array<std::string, 2> week;
  /**
   * for each stop name: the patterns that call there, and where in them –
   * the trams' stops of that name, and the buses' stop places of that name
   * (where trams call under the name, only the places that are their stop:
   * the buses at the Rathaus of another town are not here)
   */
  std::unordered_map<std::string, std::vector<StopCall>> calls;
  /** for each stop place (its number in stops.json.gz): the patterns of the buses and trams that call there */
  std::unordered_map<int, std::vector<StopCall>> placeCalls;
  /** the bus lines the buses' timetables belong to: their build (buslines.json.gz `map`) and how many stop places they count on – '' and 0 without buses */
  std::string busMap;
  int busPlaces = 0;
  /** lines that came twice in the file: the first, which the calls may still name, kept alive here */
  std::vector<std::unique_ptr<TimetableLine>> retired;

  /** lines[i] / buses[i], null where there is none */
  const TimetableLine* tram(int i) const { return i >= 0 && i < static_cast<int>(lines.size()) ? lines[static_cast<size_t>(i)].get() : nullptr; }
  const TimetableLine* bus(int i) const { return i >= 0 && i < static_cast<int>(buses.size()) ? buses[static_cast<size_t>(i)].get() : nullptr; }
};

/** One trip: its pattern, and when it leaves the pattern's first stop. */
struct TripRef {
  const TimetablePattern* pattern = nullptr;
  /** seconds after midnight of the day asked about (negative for a trip of yesterday's service, past 86 400 for tomorrow's) */
  double t0 = 0;
};

struct Departure {
  /** a tram or a bus */
  TransitKind kind;
  /** the line: its place in lines.json.gz (a tram) or buslines.json.gz (a bus), its number and colour ('' where it has none) */
  int line = 0;
  std::string ref;
  std::string colour;
  /** where it goes */
  std::string to;
  /** seconds until it leaves */
  double wait = 0;
  /** when it leaves, seconds after midnight of the day asked about (past 86 400: after midnight) */
  double time = 0;
  TripRef trip;
  /** the stop's place in the trip's pattern */
  int k = 0;
  /** the stop place it leaves from (its number in stops.json.gz; −1 where not known) */
  int place = -1;
};

struct TripState : TripRef {
  /** the stop it stands at or last left (index into pattern.stops), and the one it runs to next (from + 1; the same at the last) */
  int from = 0;
  int to = 0;
  /** how far it is from `from` to `to` by the clock, 0 … 1 (0 while it stands) */
  double frac = 0;
  /** standing at `from` (between arrival and departure, or before its first departure) */
  bool standing = false;
  /** when it leaves `from` (standing) or arrives at `to` (running), seconds after midnight of the day asked about */
  double until = 0;
};

/** A trip under way, with all a map needs to show it: what it is, where it goes, and where between which two stops it is. */
struct Running : TripState {
  TransitKind kind;
  /** its line: the place in lines.json.gz (a tram) or buslines.json.gz (a bus), the number and the colour ('' where none) */
  int line = 0;
  std::string ref;
  std::string colour;
  /** where it goes, as its front says now */
  std::string dest;
  /**
   * the stop it stands at or last left and the one it runs to next (its
   * `from` and `to`; `frac` is how far it is between them) by their place in
   * the line's stops: lines.json.gz `stops` of a tram (piece of track,
   * mark), BusLines `stops` of a bus (way, metres along it) – −1 where not
   * known – and by name and stop place (−1 where not known)
   */
  int fromStop = -1;
  int toStop = -1;
  std::string fromName;
  std::string toName;
  int fromPlace = -1;
  int toPlace = -1;
};

struct ScheduledStop : TripRef {
  /** the stop's place in the pattern */
  int k = 0;
  /** when the trip is due to leave there (its arrival, at the end of its way), seconds after midnight of the day asked about */
  double time = 0;
  /** how late a vehicle there now is by that: seconds, negative when it is early */
  double late = 0;
};

/** What parseTimetable needs of the bus lines (BusLinesJson, or BusLines of src/world/buslines.ts) to see that the timetables belong to them. */
struct BusLinesRef {
  struct Line {
    std::string ref;
    std::optional<int64_t> osm;
  };
  std::vector<Line> lines;
};

/**
 * Reads timetable.json.gz (version 1 or 2).  With the lines given
 * (lines.json.gz), drops the timetables of tram lines that do not match;
 * with the bus lines given (buslines.json.gz, or BusLines), those of bus
 * lines that do not match – files of different builds.  (Throws on a version it does not know.)
 */
std::unique_ptr<Timetable> parseTimetable(const TimetableJson& json, const LinesJson* lines = nullptr, const BusLinesRef* buslines = nullptr);

/** a stop asked about: its name, or its stop place (a number) */
using StopKey = std::variant<std::string, int>;

/**
 * The next departures from a stop, all lines – trams and buses – soonest
 * first: at most `n`, within `horizon` seconds.  The stop is a stop place
 * (its number in stops.json.gz: the buses that call there and the trams of
 * the tram stop it is) or a name (the trams' stop of that name, and the
 * buses' stop places of that name – where no tram stop has the name, those
 * of every town: ask by place to have one town's).
 */
std::vector<Departure> departures(const Timetable& tt, const StopKey& stop, const ServiceTime& at, int n = 4, double horizon = 2 * 3600);

/**
 * The trips of a tram line (by its place in lines.json.gz) under way at a
 * moment: from their first departure (or `lead` seconds before it, standing
 * at the first stop) until they arrive at their last stop – each with the
 * stop it stands at or last left, the next, and how far between the two it
 * is by the clock.
 */
std::vector<TripState> tripsAt(const Timetable& tt, int line, const ServiceTime& at, double lead = 0);

/** The same for a bus line (by its place in buslines.json.gz): its trips under way, where `pattern.lineStops` names the stops by their place in BusLines `stops`. */
std::vector<TripState> busTripsAt(const Timetable& tt, int line, const ServiceTime& at, double lead = 0);

/**
 * Every trip under way at a moment, trams and buses – each with its line,
 * number, colour and destination, and where it is: between which two of
 * its line's stops, and how far from the one to the other (`frac`) – for a
 * map that shows them all.  `lead` as in tripsAt.
 */
std::vector<Running> runningAt(const Timetable& tt, const ServiceTime& at, double lead = 0);

/**
 * The trip a tram of a line standing at a stop now belongs to: the scheduled
 * call there nearest to the moment, within `window` seconds either way – an
 * early call counting one and a half times its distance, since trams run
 * late more often than early.  Null if none is that near.
 */
std::optional<ScheduledStop> scheduledAt(const Timetable& tt, int line, const std::string& stop, const ServiceTime& at, double window = 600);

/** The same for a bus of a bus line (by its place in buslines.json.gz) standing at a stop place (its number in stops.json.gz). */
std::optional<ScheduledStop> busScheduledAt(const Timetable& tt, int line, int place, const ServiceTime& at, double window = 600);

// ---- the clock in Kassel ----

/** How many seconds the clocks in Kassel are ahead of UTC at a moment (ms since 1970): 7200 in summer time (from 01:00 UTC on the last Sunday of March to 01:00 UTC on the last Sunday of October), else 3600. */
double berlinOffset(double ms);

/** The date and time on the clocks in Kassel at a moment: year, month (1–12), day, weekday (0 Sunday … 6 Saturday), seconds after midnight. */
struct BerlinClock {
  int y = 0;
  int m = 0;
  int d = 0;
  int weekday = 0;
  double s = 0;
};
BerlinClock berlinClock(double ms);

/** Easter Sunday of a year (Gregorian calendar, the anonymous algorithm in Meeus' form): [month 1–12, day]. */
std::array<int, 2> easter(int y);

/** Whether a date (month 1–12) is a public holiday in Hesse. */
bool isHoliday(int y, int m, int d);

/**
 * The day type of a date in Hesse: Sundays and public holidays run the
 * Sunday timetable, Saturdays the Saturday one – and so do Christmas Eve and
 * New Year's Eve when they fall on a working day, the common rule in Germany.
 */
DayType dayTypeOf(int y, int m, int d);

/** A moment (ms since 1970: the original's Date) as the timetable sees it: the time on the clocks in Kassel and the day types around it, holidays included. */
ServiceTime serviceTime(double ms);

/** A moment by weekday (0 Sunday … 6 Saturday) and seconds after midnight alone, holidays aside – for a game clock without a date. */
ServiceTime weekTime(int weekday, double s);

}  // namespace fs

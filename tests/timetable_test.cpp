// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The timetables of the trams and buses as the game asks them (src/sim/timetable.ts, src/sim/stopboards.ts):
 * departures, trips under way, the trip a vehicle at a stop belongs to, the clock and the day types in Kassel,
 * the departure displays.
 *
 * Origin: tests/world/timetable.test.ts @ v1.7.7 – the cases of the game's side.  The timetable they ask is the
 * one tools/build-timetable.ts builds from the test's feed (not ported: it is a build tool): written once by it
 * into tests/fixtures/timetable-built.json, and with the hand-made bus section into timetable-combined.json.
 */
// LINK: sim game world core platform geo
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/date.h"
#include "core/js.h"
#include "platform/json.h"
#include "sim/stopboards.h"
#include "sim/timetable.h"
#include "world/worldjson.h"

using namespace fs;

namespace {

double hms(double h, double m, double s = 0) { return h * 3600 + m * 60 + s; }

TimetableJson read(const char* path) {
  std::ifstream f(path);
  assert(f && "the fixture is there");
  std::stringstream ss;
  ss << f.rdbuf();
  TimetableJson json;
  const bool ok = jsonParse(ss.str(), json);
  assert(ok);
  return json;
}

const TimetableJson& built() {
  static auto* j = new TimetableJson(read("tests/fixtures/timetable-built.json"));
  return *j;
}
const TimetableJson& combined() {
  static auto* j = new TimetableJson(read("tests/fixtures/timetable-combined.json"));
  return *j;
}

using Row = std::vector<std::string>;
std::string s(double v) { return jsStr(v); }

bool sameST(const ServiceTime& a, double sec, DayType day, DayType prev, DayType next) { return a.s == sec && a.day == day && a.prev == prev && a.next == next; }

}  // namespace

int main() {
  // ---- departures from a stop: all lines, soonest first, not where trips end
  {
    const std::unique_ptr<Timetable> tt = parseTimetable(built());
    const ServiceTime wed8 = weekTime(3, hms(8, 0));
    const std::vector<Departure> d = departures(*tt, StopKey(std::string("Am Stern")), wed8, 10);
    assert(d.size() == 3);
    assert(d[0].ref == "1" && d[0].to == "Vellmar Nord" && d[0].wait == 120 && d[0].line == 0);
    assert(d[1].ref == "1" && d[1].to == "Vellmar Nord" && d[1].wait == 720 && d[1].line == 0);
    assert(d[2].ref == "1" && d[2].to == "Holländischer Platz/Universität" && d[2].wait == 3720 && d[2].line == 2);
    assert(departures(*tt, StopKey(std::string("Am Stern")), wed8, 1).size() == 1);
    // Vellmar Nord: the trips out end there, the trips back begin there
    const std::vector<Departure> v = departures(*tt, StopKey(std::string("Vellmar Nord")), weekTime(3, hms(5, 50)));
    assert(v.size() == 3 && v[0].line == 1 && v[0].wait == 600 && v[1].wait == 1800 && v[2].wait == 3000);
    // the RegioTram leaves the last stop the map has of it: it goes on beyond
    const std::vector<Departure> h = departures(*tt, StopKey(std::string("Hauptbahnhof")), weekTime(2, hms(7, 0)));
    assert(h.size() == 1 && h[0].ref == "RT4" && h[0].to == "Wolfhagen" && h[0].wait == 720);
    // Saturday has its own trips
    const std::vector<Departure> sat = departures(*tt, StopKey(std::string("Königsplatz")), weekTime(6, hms(9, 0)));
    assert(sat.size() == 1 && sat[0].wait == hms(1, 0));
    assert(departures(*tt, StopKey(std::string("Nowhere")), wed8).empty());
  }

  // ---- departures after midnight: the trips of the day before, past 24:00
  {
    const std::unique_ptr<Timetable> tt = parseTimetable(built());
    // Thursday 00:30 – Wednesday's trip of 24:30 leaves Königsplatz now and Am Stern in two minutes
    const ServiceTime thu = weekTime(4, hms(0, 30));
    const std::vector<Departure> k = departures(*tt, StopKey(std::string("Königsplatz")), thu);
    assert(k.size() == 1 && k[0].wait == 0 && k[0].time == hms(0, 30));
    const std::vector<Departure> a = departures(*tt, StopKey(std::string("Am Stern")), thu);
    assert(a.size() == 1 && a[0].wait == 120 && a[0].trip.t0 == hms(0, 30));
    // Saturday 00:30: Friday's trip of 24:30, a working day's
    assert(departures(*tt, StopKey(std::string("Königsplatz")), weekTime(6, hms(0, 30))).size() == 1);
    // Monday 00:30: Sunday's trips are not the working days'
    assert(departures(*tt, StopKey(std::string("Königsplatz")), weekTime(1, hms(0, 30))).empty());
    // Tuesday 23:00, looking eight hours ahead: tonight's 24:30, and Wednesday's first trips back
    const std::vector<Departure> late = departures(*tt, StopKey(std::string("Vellmar Nord")), weekTime(2, hms(23, 0)), 2, 8 * 3600);
    assert(late.size() == 2 && late[0].wait == hms(7, 0) && late[0].time == hms(30, 0) && late[1].wait == hms(7, 20) && late[1].time == hms(30, 20));
    const std::vector<Departure> kn = departures(*tt, StopKey(std::string("Königsplatz")), weekTime(2, hms(23, 0)), 1, 8 * 3600);
    assert(kn.size() == 1 && kn[0].wait == hms(1, 30));
  }

  // ---- trips under way: standing, between two stops, after midnight, about to leave
  {
    const std::unique_ptr<Timetable> tt = parseTimetable(built());
    // 08:05:30: the 08:00 stands at Holländischer Platz until 08:06; the 08:10 has not left yet
    const std::vector<TripState> at = tripsAt(*tt, 0, weekTime(3, hms(8, 5, 30)));
    assert(at.size() == 1);
    assert(at[0].t0 == hms(8, 0) && at[0].from == 2 && at[0].to == 3 && at[0].standing && at[0].frac == 0 && at[0].until == hms(8, 6));
    assert(at[0].pattern->stops[2] == "Holländischer Platz/Universität");
    // (where that is on the line: its third stop)
    assert(at[0].pattern->lineStops[2] == 2);
    // asked with a lead of five minutes, the 08:10 waits at its first stop
    std::vector<TripState> lead = tripsAt(*tt, 0, weekTime(3, hms(8, 5, 30)), 300);
    std::sort(lead.begin(), lead.end(), [](const TripState& a, const TripState& b) { return a.t0 < b.t0; });
    assert(lead.size() == 2);
    assert(lead[0].t0 == hms(8, 0) && lead[0].from == 2 && lead[0].standing && lead[0].until == hms(8, 6));
    assert(lead[1].t0 == hms(8, 10) && lead[1].from == 0 && lead[1].standing && lead[1].until == hms(8, 10));
    // 08:10: the 08:00 half way from Holländischer Platz (08:06) to Stadtmitte (08:14); the 08:10 just leaving
    std::vector<TripState> ten = tripsAt(*tt, 0, weekTime(3, hms(8, 10)));
    std::sort(ten.begin(), ten.end(), [](const TripState& a, const TripState& b) { return a.t0 < b.t0; });
    assert(ten[0].from == 2 && ten[0].to == 3 && !ten[0].standing && ten[0].frac == 0.5 && ten[0].until == hms(8, 14));
    assert(ten[1].from == 0 && ten[1].to == 1 && !ten[1].standing && ten[1].frac == 0);
    // Thursday 00:31: Wednesday's 24:30 between Königsplatz and Am Stern
    const std::vector<TripState> night = tripsAt(*tt, 0, weekTime(4, hms(0, 31)));
    assert(night.size() == 1 && night[0].t0 == hms(0, 30) && night[0].from == 0 && night[0].to == 1 && night[0].frac == 0.5);
    // at its last stop it is there until the minute it arrives, then gone
    int there = 0;
    for (const TripState& t : tripsAt(*tt, 0, weekTime(3, hms(8, 20))))
      if (t.t0 == hms(8, 0)) {
        assert(t.from == 4 && t.to == 4 && t.standing);
        there++;
      }
    assert(there == 1);
    for (const TripState& t : tripsAt(*tt, 0, weekTime(3, hms(8, 20, 1)))) assert(t.t0 != hms(8, 0));
    assert(tripsAt(*tt, 5, weekTime(3, hms(8, 0))).empty());
  }

  // ---- the trip a tram standing at a stop belongs to, and how late it is
  {
    const std::unique_ptr<Timetable> tt = parseTimetable(built());
    const std::string hol = "Holländischer Platz/Universität";
    // due away 08:06 and 08:16
    const std::optional<ScheduledStop> late = scheduledAt(*tt, 0, hol, weekTime(3, hms(8, 8)));
    assert(late && late->t0 == hms(8, 0) && late->k == 2 && late->time == hms(8, 6) && late->late == 120);
    const std::optional<ScheduledStop> early = scheduledAt(*tt, 0, hol, weekTime(3, hms(8, 15)));
    assert(early && early->t0 == hms(8, 10) && early->late == -60);
    // half way between the two: late is likelier than early
    assert(scheduledAt(*tt, 0, hol, weekTime(3, hms(8, 11)))->t0 == hms(8, 0));
    // nothing due within the window
    assert(!scheduledAt(*tt, 0, hol, weekTime(3, hms(12, 0))));
    assert(!scheduledAt(*tt, 0, hol, weekTime(3, hms(8, 30)), 120));
    // at the end of its way the arrival counts
    assert(scheduledAt(*tt, 0, "Vellmar Nord", weekTime(3, hms(8, 21)))->late == 60);
    // after midnight
    assert(scheduledAt(*tt, 0, "Am Stern", weekTime(4, hms(0, 33)))->late == 60);
  }

  // ---- parsing: a timetable of another build of the lines loses the lines that do not match
  {
    const TimetableJson& json = built();
    LinesJson lines;
    lines.v = 1;
    for (const char* ref : {"1", "1", "1", "RT4", "RT4", "5"}) {
      LineJson l;
      l.ref = ref;
      lines.lines.push_back(l);
    }
    const auto count = [](const Timetable& tt) {
      int n = 0;
      for (const auto& l : tt.lines) n += l ? 1 : 0;
      return n;
    };
    assert(count(*parseTimetable(json, &lines)) == 5);
    lines.lines[3].ref = "9";
    const std::unique_ptr<Timetable> tt = parseTimetable(json, &lines);
    assert(!tt->tram(3));
    assert(count(*tt) == 4);
    TimetableJson newer = json;
    newer.version = TIMETABLE_VERSION + 1;
    bool threw = false;
    try {
      parseTimetable(newer);
    } catch (const std::exception&) {
      threw = true;
    }
    assert(threw);
    // (a file of version 1 – the trams alone – is read as it was)
    TimetableJson old = json;
    old.version = 1;
    const std::unique_ptr<Timetable> v1 = parseTimetable(old);
    assert(count(*v1) == 5);
    assert(v1->buses.empty() && v1->placeCalls.empty() && v1->busMap.empty());
  }

  // ---- the clock in Kassel: summer time and its changes
  {
    assert(berlinOffset(utcMs(2026, 0, 15, 12)) == 3600);
    assert(berlinOffset(utcMs(2026, 6, 1, 12)) == 7200);
    // the clocks go forward on 29 March 2026 at 01:00 UTC, back on 25 October at 01:00 UTC
    const BerlinClock a = berlinClock(utcMs(2026, 2, 29, 0, 59, 59));
    assert(a.y == 2026 && a.m == 3 && a.d == 29 && a.weekday == 0 && a.s == hms(1, 59, 59));
    assert(berlinClock(utcMs(2026, 2, 29, 1)).s == hms(3, 0));
    assert(berlinClock(utcMs(2026, 9, 25, 0, 59, 59)).s == hms(2, 59, 59));
    assert(berlinClock(utcMs(2026, 9, 25, 1)).s == hms(2, 0));
    // late evening in UTC is already the next day in Kassel
    const BerlinClock b = berlinClock(utcMs(2026, 10, 17, 23, 30));
    assert(b.y == 2026 && b.m == 11 && b.d == 18 && b.weekday == 3 && b.s == hms(0, 30));
  }

  // ---- day types: weekends, the holidays of Hesse, Christmas Eve
  {
    assert((easter(2026) == std::array<int, 2>{4, 5}));
    assert((easter(2027) == std::array<int, 2>{3, 28}));
    assert((easter(2024) == std::array<int, 2>{3, 31}));
    assert(dayTypeOf(2026, 11, 18) == DAY_WEEKDAY);
    assert(dayTypeOf(2026, 11, 21) == DAY_SATURDAY);
    assert(dayTypeOf(2026, 11, 22) == DAY_SUNDAY);
    for (const auto& [m, d] : std::vector<std::pair<int, int>>{{1, 1}, {4, 3}, {4, 6}, {5, 1}, {5, 14}, {5, 25}, {6, 4}, {10, 3}, {12, 25}, {12, 26}}) {
      assert(isHoliday(2026, m, d));
      assert(dayTypeOf(2026, m, d) == DAY_SUNDAY);
    }
    // Repentance Day is a holiday in Saxony only
    assert(dayTypeOf(2026, 11, 18) == DAY_WEEKDAY);
    assert(dayTypeOf(2026, 12, 24) == DAY_SATURDAY);
    assert(dayTypeOf(2026, 12, 31) == DAY_SATURDAY);
  }

  // ---- a moment as the timetable sees it: the day types around it
  {
    // Wednesday 18 November 2026, 08:00 in Kassel
    assert(sameST(serviceTime(utcMs(2026, 10, 18, 7)), hms(8, 0), DAY_WEEKDAY, DAY_WEEKDAY, DAY_WEEKDAY));
    // New Year's Day 2027, half past midnight: a holiday after New Year's Eve, before a Saturday
    assert(sameST(serviceTime(utcMs(2026, 11, 31, 23, 30)), hms(0, 30), DAY_SUNDAY, DAY_SATURDAY, DAY_SATURDAY));
    // Monday after Easter 2026, in summer time
    assert(sameST(serviceTime(utcMs(2026, 3, 6, 10)), hms(12, 0), DAY_SUNDAY, DAY_SUNDAY, DAY_WEEKDAY));
    assert(sameST(weekTime(1, 3600), 3600, DAY_WEEKDAY, DAY_SUNDAY, DAY_WEEKDAY));
    assert(sameST(weekTime(0, 0), 0, DAY_SUNDAY, DAY_SATURDAY, DAY_WEEKDAY));
  }

  // ---- parsing the buses: the steps counted up, the fronts for each stop, the lines checked against buslines.json
  {
    const TimetableJson& json = combined();
    const std::unique_ptr<Timetable> tt = parseTimetable(json);
    assert(tt->busMap == "m1" && tt->busPlaces == 10);
    const TimetableLine* l0 = tt->bus(0);
    assert(l0 && l0->kind == "bus" && l0->index == 0 && l0->ref == "52" && l0->colour == "#8A1478");
    const TimetablePattern& p = l0->patterns[0];
    assert((p.stops == std::vector<std::string>{"Rathaus", "Stadtmitte", "Parkstadion"}));
    assert((p.places == std::vector<int>{0, 1, 2}) && (p.lineStops == std::vector<int>{0, 1, 2}));
    assert((p.dep == std::vector<double>{0, 180, 420}) && (p.arr == std::vector<double>{0, 120, 420}));
    assert((p.dests == std::vector<std::string>{"Parkstadion", "Parkstadion", "Parkstadion"}) && p.to == "Parkstadion" && !p.pre && !p.post);
    assert((p.starts[0] == std::vector<double>{hms(7, 0), hms(7, 30)}) && (p.starts[1] == std::vector<double>{hms(9, 0)}) && p.starts[2].empty());
    assert((tt->bus(3)->patterns[0].dests == std::vector<std::string>{"Stadtmitte", "Königsplatz", "Königsplatz"}));
    assert(!tt->bus(1));
    // the trams keep their own, with their colour ('' here) and their stop places
    assert(tt->tram(0)->kind == "tram" && tt->tram(0)->colour.empty());
    assert(tt->tram(0)->patterns[0].places[0] == 7);
    // checked against the bus lines of the map: a line whose relation differs is dropped
    BusLinesRef ref;
    ref.lines = {{"52", 1052}, {"x", std::nullopt}, {"x", std::nullopt}, {"100", 9}};
    const std::unique_ptr<Timetable> checked = parseTimetable(json, nullptr, &ref);
    assert(checked->bus(0));
    assert(!checked->bus(3));
    BusLinesRef one;
    one.lines = {{"52", 1052}};
    assert(!parseTimetable(json, nullptr, &one)->bus(3));
  }

  // ---- departures of trams and buses together: by stop place, or by name – where the trams call, their place only
  {
    const std::unique_ptr<Timetable> tt = parseTimetable(combined());
    const auto rows = [](const std::vector<Departure>& list) {
      std::vector<Row> out;
      for (const Departure& d : list) out.push_back({d.kind, d.ref, d.colour, d.to, s(d.wait), s(d.place)});
      return out;
    };
    assert((rows(departures(*tt, StopKey(0), weekTime(3, hms(6, 58)))) == std::vector<Row>{{"bus", "52", "#8A1478", "Parkstadion", "120", "0"}, {"bus", "52", "#8A1478", "Parkstadion", "1920", "0"}}));
    // after midnight: Wednesday's 24:10 – at the trams' Königsplatz (place 7) with the tram of 24:30; the other Königsplatz (8) only when asked by its place
    const ServiceTime thu = weekTime(4, hms(0, 5));
    const std::vector<Row> kp = {{"bus", "100", "", "Stadtmitte", "300", "7"}, {"tram", "1", "", "Vellmar Nord", "1500", "7"}};
    assert(rows(departures(*tt, StopKey(std::string("Königsplatz")), thu)) == kp);
    assert(rows(departures(*tt, StopKey(7), thu)) == kp);
    assert((rows(departures(*tt, StopKey(8), thu)) == std::vector<Row>{{"bus", "100", "", "Königsplatz", "900", "8"}}));
    // (the front as shown at the stop: 100 says "Königsplatz" from the Stadtmitte on – and the trams' "Stadtmitte", whose place the file does not know, comes by name)
    assert((rows(departures(*tt, StopKey(std::string("Stadtmitte")), thu)) == std::vector<Row>{{"bus", "100", "", "Königsplatz", "600", "1"}, {"tram", "1", "", "Vellmar Nord", "2220", "-1"}}));
    assert((rows(departures(*tt, StopKey(1), thu)) == std::vector<Row>{{"bus", "100", "", "Königsplatz", "600", "1"}}));
    assert(departures(*tt, StopKey(5), thu).empty());
    // the old question still has its answer: the trams of a stop by name
    const std::vector<Departure> am = departures(*tt, StopKey(std::string("Am Stern")), weekTime(3, hms(8, 0)), 2);
    assert(am.size() == 2 && am[0].kind == "tram" && am[0].wait == 120 && am[1].wait == 720);
  }

  // ---- buses under way, every trip under way for the map, and how late a bus at a stop is
  {
    const std::unique_ptr<Timetable> tt = parseTimetable(combined());
    const std::vector<TripState> b = busTripsAt(*tt, 0, weekTime(3, hms(7, 1)));
    const TripState& a = b[0];
    assert(a.t0 == hms(7, 0) && a.from == 0 && a.to == 1 && !a.standing && a.until == hms(7, 2));
    assert(std::abs(a.frac - 0.5) < 1e-9);
    assert(busTripsAt(*tt, 2, weekTime(3, hms(7, 1))).empty());
    const std::vector<Running> running = runningAt(*tt, weekTime(3, hms(7, 2, 30)));
    const Running* bus = nullptr;
    for (const Running& r : running)
      if (r.kind == "bus" && !bus) bus = &r;
    assert(bus);
    assert(bus->line == 0 && bus->ref == "52" && bus->colour == "#8A1478" && bus->dest == "Parkstadion" && bus->from == 1 && bus->to == 2 && bus->fromStop == 1 && bus->toStop == 2);
    assert(bus->fromName == "Stadtmitte" && bus->toName == "Parkstadion" && bus->fromPlace == 1 && bus->toPlace == 2 && bus->standing && bus->frac == 0 && bus->until == hms(7, 3));
    // the trams of the same moment: RegioTram 4 out from Holländische Straße (07:00) towards Am Stern (07:07)
    std::vector<const Running*> tram;
    for (const Running& r : running)
      if (r.kind == "tram") tram.push_back(&r);
    assert(tram.size() == 1);
    const Running& t = *tram[0];
    assert(t.line == 4 && t.ref == "RT4" && t.colour.empty() && t.dest == "Wolfhagen" && t.fromName == "Holländische Straße" && t.toName == "Am Stern" && t.fromStop == 0 && t.toStop == 1 && t.fromPlace == -1);
    assert(std::abs(t.frac - 150.0 / 420) < 1e-9);
    // Thursday 00:12: Wednesday's 100 between the Stadtmitte and the other Königsplatz, its front now "Königsplatz"
    std::vector<const Running*> night;
    const std::vector<Running> later = runningAt(*tt, weekTime(4, hms(0, 17)));
    for (const Running& r : later)
      if (r.kind == "bus") night.push_back(&r);
    assert(night.size() == 1 && night[0]->line == 3 && night[0]->dest == "Königsplatz" && night[0]->fromPlace == 1 && night[0]->toPlace == 8 && night[0]->t0 == hms(24, 10) - 86400);
    // late by a minute at the Stadtmitte (due away 07:03); at its last stop the arrival counts
    const std::optional<ScheduledStop> late = busScheduledAt(*tt, 0, 1, weekTime(3, hms(7, 4)));
    assert(late && late->t0 == hms(7, 0) && late->k == 1 && late->time == hms(7, 3) && late->late == 60);
    assert(busScheduledAt(*tt, 0, 2, weekTime(3, hms(7, 8)))->late == 60);
    assert(!busScheduledAt(*tt, 0, 6, weekTime(3, hms(7, 8))));
  }

  // ---- what the departure displays show: the next trams and buses of their place, minutes or the time, and where they stand
  {
    const std::unique_ptr<Timetable> tt = parseTimetable(combined());
    const BoardPlace rathaus{0, "Rathaus"};
    assert(servedAt(*tt, rathaus));
    assert(!servedAt(*tt, BoardPlace{5, "Nowhere"}));
    const std::optional<std::vector<BoardRow>> r = boardRows(*tt, rathaus, weekTime(3, hms(6, 58)));
    assert(r && r->size() == 2);
    assert((*r)[0].line == "52" && (*r)[0].destination == "Parkstadion" && (*r)[0].minutes == 2 && (*r)[0].at.empty() && (*r)[0].kind == "bus" && (*r)[0].colour == "#8A1478");
    assert((*r)[1].minutes == 32);
    // an hour and more ahead: the time; under a minute: now (0)
    const std::optional<std::vector<BoardRow>> early = boardRows(*tt, rathaus, weekTime(3, hms(5, 50)));
    assert(early && early->size() == 2 && (*early)[0].at == "07:00" && (*early)[1].at == "07:30");
    const std::optional<std::vector<BoardRow>> now = boardRows(*tt, rathaus, weekTime(3, hms(6, 59, 30)));
    assert(now && (*now)[0].at.empty() && (*now)[0].minutes == 0);
    assert(boardRows(*tt, rathaus, weekTime(3, hms(6, 0)), 1)->size() == 1);
    assert(!boardRows(*tt, BoardPlace{5, "Nowhere"}, weekTime(3, hms(7, 0))));
    // the trams' place shows its trams too
    const std::optional<std::vector<BoardRow>> kp = boardRows(*tt, BoardPlace{7, "Königsplatz"}, weekTime(4, hms(0, 5)));
    assert(kp && kp->size() == 2 && (*kp)[0].kind == "bus" && (*kp)[0].line == "100" && (*kp)[0].minutes == 5 && (*kp)[1].kind == "tram" && (*kp)[1].line == "1" && (*kp)[1].minutes == 25);
    // a file of version 1 knows the stops by name
    TimetableJson old = built();
    old.version = 1;
    const std::unique_ptr<Timetable> v1 = parseTimetable(old);
    assert(servedAt(*v1, BoardPlace{99, "Am Stern"}));
    const std::optional<std::vector<BoardRow>> am = boardRows(*v1, BoardPlace{99, "Am Stern"}, weekTime(3, hms(8, 0)));
    assert(am && am->size() == 3 && (*am)[0].minutes == 2 && (*am)[1].minutes == 12 && (*am)[2].at == "09:02");
    assert(clockText(hms(7, 5)) == "07:05");
    assert(clockText(hms(25, 10)) == "01:10");
    assert(clockText(-60) == "23:59");
    // which pole gets which display
    assert(displayAt(STOP_FLAG_BOARD | STOP_FLAG_BUS, STOP_FLAG_BUS, false) == "board");
    assert(displayAt(STOP_FLAG_SHELTER | STOP_FLAG_BUS, STOP_FLAG_BUS, true) == "board");
    assert(displayAt(STOP_FLAG_BUS, STOP_FLAG_TRAM | STOP_FLAG_BUS, true) == "board");
    assert(displayAt(STOP_FLAG_BUS, STOP_FLAG_BUS, true) == "small");
    assert(displayAt(STOP_FLAG_SHELTER | STOP_FLAG_BUS, STOP_FLAG_BUS, false).empty());
  }

  std::printf("timetable: all checks passed\n");
  return 0;
}

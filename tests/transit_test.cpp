// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The trams and buses on the map (src/ui/transitmap.ts): the colours of the lines, where a bus is along its line,
 * the departures at a stop – and the trams and buses of the traffic keep to the timetables: a tram is at a stop when
 * the departure board there says, a bus stands at each stop of its trip until its time (src/game/timedtransit.ts
 * with src/sim/rail.ts and src/sim/traffic.ts).
 *
 * Origin: tests/ui/transitmap.test.ts and tests/game/timedtransit.test.ts @ v1.7.7 (the case on the real tracks and
 * timetables of Kassel is not here: it needs the map's timetable).
 */
// LINK: game ui reality i18n sim audio world geo core platform
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>

#include "core/date.h"
#include "core/js.h"
#include "game/timedtransit.h"
#include "sim/rail.h"
#include "sim/signals.h"
#include "sim/stopboards.h"
#include "sim/timetable.h"
#include "sim/traffic.h"
#include "town.h"
#include "ui/transitmap.h"
#include "world/buslines.h"

using namespace fs;

namespace {

/** A Wednesday morning in Kassel, 08:00 (07:00 UTC in winter time). */
const double START = utcMs(2026, 10, 18, 7, 0, 0);

struct Stand {
  std::string trip;
  /** when it came to stand and when it left again (s after START) */
  double from = 0;
  double to = INFINITY;
  int path = 0;
  double mark = 0;
};

/** Run the trams for `seconds`, the clock with them; note every stand of a tram of the timetable at a stop. */
std::vector<Stand> simulate(RailTraffic& rail, double& clock, const RailPlayer& player, double seconds, double dt = 0.2) {
  std::vector<Stand> stands;
  std::map<const RailVehicle*, Stand> open;
  for (double t = 0; t < seconds; t += dt) {
    rail.update(dt, player);
    clock += dt * 1000;
    const double now = (clock - START) / 1000;
    for (const auto& vp : rail.vehicles) {
      const RailVehicle* v = vp.get();
      const bool standing = v->speed == 0 && v->dwell > 0 && !v->trip.empty();
      auto st = open.find(v);
      if (standing && st == open.end()) {
        const RailPath& p = rail.net.paths[static_cast<size_t>(v->path)];
        for (double m : p.stops)
          if (std::abs(m - v->s) < 4) {
            open[v] = Stand{v->trip, now, INFINITY, v->path, m};
            break;
          }
      } else if (!standing && st != open.end()) {
        st->second.to = now;
        stands.push_back(st->second);
        open.erase(st);
      }
    }
    for (auto it = open.begin(); it != open.end();) {
      bool still = false;
      for (const auto& vp : rail.vehicles)
        if (vp.get() == it->first) still = true;
      if (!still) {
        it->second.to = now;
        stands.push_back(it->second);
        it = open.erase(it);
      } else ++it;
    }
  }
  for (auto& [v, st] : open) stands.push_back(st);
  return stands;
}

std::shared_ptr<const std::vector<float>> courseOf(std::vector<float> c) { return std::make_shared<const std::vector<float>>(std::move(c)); }

TimetableJson baseJson(int version) {
  TimetableJson json;
  json.version = version;
  json.valid = {"2026-01-01", "2026-12-31"};
  json.week = {"2026-11-16", "2026-11-22"};
  json.days = {"2026-11-18", "2026-11-21", "2026-11-22"};
  return json;
}

/** The source of the bus test: the bus lines and the courses of the town's roads. */
class TownSource : public TransitSource {
 public:
  TownSource(const Timetable& tt, BusLines& lines, World& world, const double& clock) : tt_(tt), lines_(lines), world_(world), clock_(clock) {}
  const Timetable* timetable() override { return &tt_; }
  ServiceTime now() override { return serviceTime(clock_); }
  const LineJson* tramLine(int) override { return nullptr; }
  std::optional<Spot> tramSpot(int, const std::pair<int, double>&, const std::pair<int, double>&, double) override { return std::nullopt; }
  TransitCourse tramCourse(int) override { return nullptr; }
  BusLines* busLines() override { return &lines_; }
  TransitCourse wayCourse(int w) override {
    const Edge* e = world_.edges[w >> 1];
    if (!e) return nullptr;
    const std::vector<double>& p = e->pl.pts;
    std::vector<float> pts;
    for (size_t i = 0; i + 2 < p.size(); i += 3) {
      pts.push_back(static_cast<float>(p[i]));
      pts.push_back(static_cast<float>(p[i + 2]));
    }
    std::vector<float> out(pts.size());
    if ((w & 1) == 0) out = pts;
    else
      for (size_t i = 0; i < pts.size(); i += 2) {
        out[i] = pts[pts.size() - 2 - i];
        out[i + 1] = pts[pts.size() - 1 - i];
      }
    return courseOf(std::move(out));
  }
  Stops* stops() override { return nullptr; }

 private:
  const Timetable& tt_;
  BusLines& lines_;
  World& world_;
  const double& clock_;
};

}  // namespace

int main() {
  // ---- colours as the data gives them: with or without #, short, or by name
  {
    assert(parseColour("#00954B") == "#00954b");
    // (GTFS route_color comes without the #)
    assert(parseColour("00954B") == "#00954b");
    assert(parseColour("#0a5") == "#00aa55");
    assert(parseColour("red") == "#ff0000");
    assert(parseColour(" LightGreen ") == "#90ee90");
    assert(!parseColour("nonsense"));
    assert(!parseColour("#12345"));
    assert(!parseColour(""));
  }

  // ---- a line's own colour first, then the KVG's tram colours, then the palette
  {
    // the timetable's or the line file's colour wins – the first that is one
    assert(lineColour("tram", "1", {"", "not a colour", "#123456"}) == "#123456");
    assert(lineColour("bus", "52", {"blue"}) == "#0000ff");
    assert(lineColour("bus", "52", {"1F6E43"}) == "#1f6e43");
    // a Kassel tram line without one in the data: the KVG's colour
    assert(lineColour("tram", "1") == KVG_TRAM_COLOURS().at("1"));
    assert(lineColour("tram", "4", {""}) == "#f08100");
    // (a bus of the same number is not a tram line)
    assert(lineColour("bus", "1") != KVG_TRAM_COLOURS().at("1"));
    // others: from the palette, always the same for a line, different for neighbouring numbers
    const std::vector<std::string>& pal = LINE_PALETTE();
    const auto inPal = [&](const std::string& c) { return std::find(pal.begin(), pal.end(), c) != pal.end(); };
    assert(inPal(lineColour("bus", "52")));
    assert(lineColour("bus", "52") == lineColour("bus", "52", {"", ""}));
    for (int n = 10; n < 10 + static_cast<int>(pal.size()) - 1; n++) assert(paletteColour("bus", std::to_string(n)) != paletteColour("bus", std::to_string(n + 1)));
    assert(paletteColour("tram", "RT4") == paletteColour("tram", "RT4"));
    assert(inPal(lineColour("tram", "RT4")));
    assert(paletteColour("tram", "RT1") != paletteColour("tram", "RT4"));
  }

  // ---- writing on a colour: dark on light ones, white on dark ones
  {
    assert(textOn("#fbba00") == "#111111");
    assert(textOn("#ffffff") == "#111111");
    assert(textOn("#0069b4") == "#ffffff");
    assert(textOn("#7f5ca3") == "#ffffff");
    assert(textOn("red") == "#ffffff");
  }

  // ---- a point along a course, and its heading
  {
    const std::vector<float> c = {0, 0, 100, 0, 100, 50};
    assert(courseLength(c) == 150);
    const std::optional<Spot> a = pointAlong(c, 30);
    assert(a && std::abs(a->x - 30) < 1e-9 && a->z == 0);
    // heading east: yaw −90°
    assert(std::abs(a->yaw + M_PI / 2) < 1e-9);
    const std::optional<Spot> b = pointAlong(c, 120);
    assert(b && b->x == 100 && std::abs(b->z - 20) < 1e-9);
    // heading south: yaw 180°
    assert(std::abs(std::abs(b->yaw) - M_PI) < 1e-9);
    // beyond its end: its end
    const std::optional<Spot> e = pointAlong(c, 999);
    assert(e && e->x == 100 && e->z == 50);
    assert(!pointAlong({5, 5}, 1));
  }

  // ---- where a bus is between two stops of its line: along the ways of the line
  {
    // three ways of 100 m each: east, east, south
    std::map<int, TransitCourse> courses = {{10, courseOf({0, 0, 100, 0})}, {12, courseOf({100, 0, 200, 0})}, {14, courseOf({200, 0, 200, 100})}};
    BusLine line;
    line.ways = {10, 12, 14};
    line.stops = {{0, 0, 20}, {0, 2, 30}, {0, 2, 90}};
    const auto course = [&](int w) -> TransitCourse {
      auto it = courses.find(w);
      return it != courses.end() ? it->second : nullptr;
    };
    // 80 + 100 + 30 = 210 m from the first stop to the second
    const std::optional<Spot> at0 = alongBusLine(line, 0, 1, 0, course);
    assert(at0 && std::abs(at0->x - 20) < 1e-9 && at0->z == 0);
    const std::optional<Spot> half = alongBusLine(line, 0, 1, 0.5, course);
    assert(half && std::abs(half->x - 125) < 1e-9 && half->z == 0);
    const std::optional<Spot> there = alongBusLine(line, 0, 1, 1, course);
    assert(there && there->x == 200 && std::abs(there->z - 30) < 1e-9);
    // on one way: between its two stops
    const std::optional<Spot> same = alongBusLine(line, 1, 2, 0.5, course);
    assert(same && same->x == 200 && std::abs(same->z - 60) < 1e-9);
    // a way whose course is not known: no place
    BusLine other = line;
    other.ways = {10, 99, 14};
    assert(!alongBusLine(other, 0, 1, 0.5, course));
    assert(!alongBusLine(line, 7, 8, 0.5, course));
  }

  // ---- departures at a tram stop of a timetable that knows the stops by name only
  {
    TimetableJson json = baseJson(1);
    json.valid = {"2026-01-01", "2026-12-31"};
    json.week = {"2026-11-02", "2026-11-08"};
    json.attribution = "test";
    json.stops = {"Hauptbahnhof", "Rathaus", "Wilhelmshöhe"};
    TimetableLineJson l;
    l.line = 0;
    l.ref = "1";
    TimetablePatternJson p;
    p.s = {0, 1, 2};
    p.j = {0, 1, 2};
    p.d = {0, 120, 300};
    p.to = "Wilhelmshöhe";
    p.t = {std::vector<double>{8 * 3600, 8 * 3600 + 600, 8 * 3600 + 1200}, {}, {}};
    l.patterns.push_back(p);
    json.lines.push_back(l);
    const std::unique_ptr<Timetable> tt = parseTimetable(json);
    const ServiceTime at{8 * 3600 + 100, 0, 0, 0};
    StopPlace place;
    place.id = 41;
    place.name = "Rathaus";
    place.town = "Kassel";
    place.kind = STOP_FLAG_TRAM;
    const std::vector<Departure> list = stopDepartures(*tt, place, at, 3);
    assert(list.size() == 3);
    assert(list[0].ref == "1");
    assert(list[0].to == "Wilhelmshöhe");
    // (the first leaves the Hauptbahnhof at 8:00 and the Rathaus two minutes later: 20 s from now)
    assert(list[0].wait == 20);
    // a bus stop of the same name: no trams there
    StopPlace bus = place;
    bus.kind = STOP_FLAG_BUS;
    assert(stopDepartures(*tt, bus, at, 3).empty());
  }

  // ---- the speed to keep: the distance in the time, starting and stopping included
  {
    assert(paceFor(500, 0) == INFINITY);   // no time left: as fast as it may
    assert(paceFor(5000, 60) == INFINITY);   // too far to make it
    const double v = paceFor(600, 120);
    assert(v > 5 && v < 7);   // 600 m in two minutes: a little faster than 5 m/s
    assert(std::abs(600 / v + v * (0.5 + 0.5 / 1.04) - 120) < 1e-6);
  }

  // ---- a made-up line: the tram stands at each stop of its trip when the board there says, passes the one it does not call at
  {
    // a straight track east, 3 km; stops at 300 (A), 900 (X: not called at), 1500 (B), 2400 (C)
    RailJson r;
    r.k = RAIL_TRAM;
    for (int x = 0; x <= 3000; x += 10) r.p.insert(r.p.end(), {static_cast<double>(x), 100, 0});
    r.st = {300, 900, 1500, 2400};
    r.sn = {"A", "X", "B", "C"};
    LinesJson lines;
    lines.v = 1;
    LineJson lj;
    lj.ref = "1";
    lj.name = "1";
    lj.from = "A";
    lj.to = "C";
    lj.paths = {{0, 1}};
    lj.stops = {{0, 300}, {0, 900}, {0, 1500}, {0, 2400}};
    lines.lines.push_back(lj);
    // trips every 10 minutes from 08:02 on: A → B in 4 minutes (fast for 1.2 km: the tram has to get going), B → C in 2 (900 m: it must hurry)
    std::vector<double> starts;
    for (int i = 0; i < 6; i++) starts.push_back(8 * 3600 + 120 + i * 600);
    TimetableJson json = baseJson(2);
    json.stops = {"A", "X", "B", "C"};
    TimetableLineJson tl;
    tl.line = 0;
    tl.ref = "1";
    TimetablePatternJson p;
    p.s = {0, 2, 3};
    p.j = {0, 2, 3};
    p.d = {0, 240, 360};
    p.to = "C";
    p.t = {starts, {}, {}};
    tl.patterns.push_back(p);
    json.lines.push_back(tl);
    const std::unique_ptr<Timetable> tt = parseTimetable(json, &lines);
    RailTraffic rail({r}, [] { return 0.5; });
    rail.setLines({RailLine{"1", "A", "C", "", 0, {PathDir{0, 1}}}});
    double clock = START;
    TramScheduleHost h;
    h.timetable = [&] { return tt.get(); };
    h.lines = [&] { return &lines; };
    h.rail = [&]() -> RailTraffic& { return rail; };
    h.nowMs = [&] { return clock; };
    const std::unique_ptr<RailSchedule> sch = tramSchedule(h);
    rail.schedule = sch.get();
    const RailPlayer player{1500, 100, 40, 0, 4, 2};
    const std::vector<Stand> stands = simulate(rail, clock, player, 1500);
    // every trip of the hour: at B, from a little before its departure on the board until it
    for (size_t i = 0; i < 2; i++) {
      const double dep = starts[i] + 240 - 8 * 3600;
      const Stand* at = nullptr;
      for (const Stand& s : stands)
        if (s.mark == 1500 && s.from > dep - 120 && s.from < dep + 60) {
          at = &s;
          break;
        }
      if (!at) std::printf("no tram at B for the departure at %g s\n", dep);
      assert(at);
      std::printf("at B for %g: from %.1f to %.1f\n", dep, at->from, at->to);
      assert(at->from <= dep && at->from >= dep - ARRIVE_EARLY - 45);   // in a little before its time
      assert(std::abs(at->to - dep) < 3);   // leaves on time
    }
    for (const Stand& s : stands) assert(s.mark != 900);   // X is passed
    // the board at B, two minutes before a departure: it counts down to the tram that comes
    // (the clock jumped, as when the player sets another time: the trams are put anew – game.ts clockJumped)
    clock = START + (starts[3] + 240 - 8 * 3600 - 120) * 1000;
    rail.resync();
    const std::optional<std::vector<BoardRow>> rows = boardRows(*tt, BoardPlace{-1, "B"}, serviceTime(clock));
    assert(rows && !rows->empty() && (*rows)[0].at.empty() && (*rows)[0].minutes == 2);
    const std::vector<Stand> more = simulate(rail, clock, player, 150);
    const double dep = starts[3] + 240 - 8 * 3600;
    const Stand* at = nullptr;
    for (const Stand& s : more)
      if (s.mark == 1500 && s.trip == "0:0:" + jsStr(starts[3])) at = &s;
    assert(at && at->from <= dep && at->from > dep - 75 && std::abs(at->to - dep) < 3);
  }

  // ---- a bus of the timetable: put where its trip is, it stands at each stop of its trip until the time the board says
  {
    // a town of 4 × 3 junctions, 120 m apart; line 5 east along the middle row, south at the third column, east along the bottom row
    TownOptions o;
    o.cols = 4;
    o.rows = 3;
    o.block = 120;
    Town town = makeTown(o);
    const auto way = [&](int i0, int j0, int i1, int j1) {
      const int e = town.street(i0, j0, i1, j1);
      return e * 2 + (town.json.edges[static_cast<size_t>(e)].a == town.node(i0, j0) ? 0 : 1);
    };
    const std::vector<int32_t> ways = {way(0, 1, 1, 1), way(1, 1, 2, 1), way(2, 1, 2, 2), way(2, 2, 3, 2)};
    BusLinesJson blj;
    blj.v = 1;
    blj.places = 30;
    BusLineJson bl;
    bl.ref = "5";
    bl.name = "Bus 5";
    bl.to = "Ostfriedhof";
    bl.w = encodeWays(ways);
    bl.st = {10, 1, 60, 11, 2, 60, 12, 3, 70};
    blj.lines.push_back(bl);
    BusLines lines(blj);
    SignalSystem signals(town.world->signals, town.world->lightsOut);
    Traffic traffic(*town.world, *town.lanes, signals);
    traffic.setBusLines(&lines);
    traffic.busStops = [](const Lane&) { return std::vector<BusStopOnLane>(); };
    traffic.busShare = 0;
    traffic.targetCount = 1;
    // trips at 08:02 and 08:12: a minute from stop to stop
    TimetableJson json = baseJson(2);
    TimetableBusJson bus;
    bus.places = 30;
    bus.stops = {{10, "Mitte"}, {11, "Ecke"}, {12, "Süd"}};
    bus.dests = {"Ostfriedhof"};
    TimetableBusLineJson tbl;
    tbl.line = 0;
    tbl.ref = "5";
    tbl.s = {10, 11, 12};
    TimetableBusPatternJson bp;
    bp.j = {0, 1, 1};
    bp.d = {0, 60, 60};
    bp.to = {0, 0};
    bp.t = {std::vector<double>{8 * 3600 + 120, 600}, {}, {}};
    tbl.patterns.push_back(bp);
    bus.lines.push_back(tbl);
    json.bus = bus;
    const std::unique_ptr<Timetable> tt = parseTimetable(json);
    double clock = START;
    TownSource src(*tt, lines, *town.world, clock);
    BusScheduleHost h;
    h.timetable = [&] { return tt.get(); };
    h.source = [&]() -> TransitSource* { return &src; };
    h.traffic = [&]() -> Traffic& { return traffic; };
    h.nowMs = [&] { return clock; };
    const std::unique_ptr<BusSchedule> sch = busSchedule(h);
    traffic.busSchedule = sch.get();
    PlayerState player;
    player.x = 180;
    player.y = 160;
    player.z = 120;
    player.length = 0.5;
    player.width = 0.5;
    // where each bus of a trip stood, and from when to when (s after 08:00)
    struct Stood {
      std::string trip;
      int place;
      double from, to;
    };
    std::vector<Stood> stood;
    std::map<int64_t, Stood> open;
    for (double t = 0; t < 900; t += 1.0 / 15) {
      traffic.update(1.0 / 15, player);
      clock += 1000.0 / 15;
      const double now = (clock - START) / 1000;
      for (const TrafficCar* c : traffic.cars) {
        if (!c->bus || (c->bus->trip.empty() && !c->bus->retired)) continue;
        const TrafficCar::Bus& b = *c->bus;
        auto st = open.find(c->id);
        if (b.dwell > 0 && st == open.end()) open[c->id] = Stood{b.trip, stopPlaceOf(b.served), now, INFINITY};
        else if (b.dwell <= 0 && st != open.end()) {
          st->second.to = now;
          stood.push_back(st->second);
          open.erase(st);
        }
      }
    }
    const auto at = [&](int place, double dep) -> const Stood* {
      for (const Stood& s : stood)
        if (s.place == place && s.from <= dep && s.to >= dep - 5) return &s;
      return nullptr;
    };
    for (const auto& [place, dep] : std::vector<std::pair<int, double>>{{10, 120}, {11, 180}, {10, 720}, {11, 780}}) {
      const Stood* st = at(place, dep);
      if (!st) {
        std::printf("no bus at stop %d for %g s; stood:", place, dep);
        for (const Stood& s : stood) std::printf(" [%s %d %.1f–%.1f]", s.trip.c_str(), s.place, s.from, s.to);
        std::printf("\n");
      }
      assert(st);
      assert(std::abs(st->to - dep) < 3);   // leaves on time
    }
    // the board at stop 10 counted down to that bus
    const std::vector<Departure> rows = departures(*tt, StopKey(10), serviceTime(START + 60000), 2);
    assert(rows[0].time - 8 * 3600 == 120);
    int n = 0;
    for (const TrafficCar* c : traffic.cars)
      if (c->bus && !c->bus->trip.empty()) n++;
    assert(n <= 2);   // one bus per trip
  }

  std::printf("transit: all checks passed\n");
  return 0;
}

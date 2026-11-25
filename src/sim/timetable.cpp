// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/timetable.ts @ v1.7.7
#include "sim/timetable.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

#include "core/date.h"

namespace fs {

namespace {

constexpr double INF = std::numeric_limits<double>::infinity();

std::vector<double> sorted(const std::vector<double>& list) {
  std::vector<double> out = list;
  std::stable_sort(out.begin(), out.end());
  return out;
}

/** A list kept as steps (the first as it is) counted up again – its first `n` (all, without n). */
template <class T>
std::vector<T> counted(const std::vector<T>& steps, int n = -1) {
  if (n < 0) n = static_cast<int>(steps.size());
  std::vector<T> out;
  T v = 0;
  for (int k = 0; k < n; k++) {
    const T step = k < static_cast<int>(steps.size()) ? steps[static_cast<size_t>(k)] : 0;
    v = k == 0 ? step : v + step;
    out.push_back(v);
  }
  return out;
}

/** Where a bus pattern goes as shown at each of its `n` stops, from the file's [k, index into dests] pairs. */
std::vector<std::string> destinations(const std::vector<int>& to, const std::vector<std::string>& dests, int n) {
  std::vector<std::string> out;
  std::string cur;
  size_t i = 0;
  for (int k = 0; k < n; k++) {
    while (i + 1 < to.size() && to[i] <= k) {
      const int d = to[i + 1];
      if (d >= 0 && d < static_cast<int>(dests.size())) cur = dests[static_cast<size_t>(d)];
      i += 2;
    }
    out.push_back(cur);
  }
  return out;
}

/** The first place in a sorted list whose value is not below v. */
size_t lowerBound(const std::vector<double>& list, double v) {
  size_t lo = 0;
  size_t hi = list.size();
  while (lo < hi) {
    const size_t mid = (lo + hi) >> 1;
    if (list[mid] < v) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

/** The three service days a moment touches: how far each one's midnight lies from today's, and its day type. */
std::array<std::pair<double, DayType>, 3> serviceDays(const ServiceTime& at) {
  return {{{-DAY_SECONDS, at.prev}, {0, at.day}, {DAY_SECONDS, at.next}}};
}

/** Where a trip is `e` seconds after it left its first stop. */
TripState position(const TimetablePattern& p, double t0, double e) {
  const int last = static_cast<int>(p.stops.size()) - 1;
  TripState t;
  t.pattern = &p;
  t.t0 = t0;
  const auto L = static_cast<size_t>(last);
  if (e < p.arr[0] || last == 0) {
    t.from = 0;
    t.to = std::min(1, last);
    t.frac = 0;
    t.standing = true;
    t.until = t0 + (e < p.dep[0] ? p.dep[0] : p.arr[L]);
    return t;
  }
  // the last stop it has arrived at
  int lo = 0;
  int hi = last;
  while (lo < hi) {
    const int mid = (lo + hi + 1) >> 1;
    if (p.arr[static_cast<size_t>(mid)] <= e) lo = mid;
    else hi = mid - 1;
  }
  const int i = lo;
  const auto I = static_cast<size_t>(i);
  if (i == last) {
    t.from = last;
    t.to = last;
    t.frac = 0;
    t.standing = true;
    t.until = t0 + p.arr[L];
    return t;
  }
  if (e < p.dep[I]) {
    t.from = i;
    t.to = i + 1;
    t.frac = 0;
    t.standing = true;
    t.until = t0 + p.dep[I];
    return t;
  }
  const double run = p.arr[I + 1] - p.dep[I];
  t.from = i;
  t.to = i + 1;
  t.frac = run > 0 ? std::min(1.0, std::max(0.0, (e - p.dep[I]) / run)) : 1;
  t.standing = false;
  t.until = t0 + p.arr[I + 1];
  return t;
}

/** The trips of a line under way at a moment (see tripsAt). */
std::vector<TripState>& lineTrips(const TimetableLine* tl, const ServiceTime& at, double lead, std::vector<TripState>& out) {
  if (!tl) return out;
  for (const TimetablePattern& p : tl->patterns) {
    const size_t last = p.stops.size() - 1;
    for (const auto& [off, day] : serviceDays(at)) {
      const std::vector<double>& starts = p.starts[static_cast<size_t>(day)];
      // under way: t0 + arr[0] − lead ≤ s ≤ t0 + arr[last]
      const double lo = at.s - off - p.arr[last];
      const double hi = at.s - off - p.arr[0] + lead;
      for (size_t i = lowerBound(starts, lo); i < starts.size() && starts[i] <= hi; i++) {
        const double t0 = starts[i] + off;
        out.push_back(position(p, t0, at.s - t0));
      }
    }
  }
  return out;
}

/** The scheduled call of a line's trips at the stops `here` picks nearest to the moment (see scheduledAt). */
template <class Here>
std::optional<ScheduledStop> scheduledOn(const TimetableLine* tl, const Here& here, const ServiceTime& at, double window) {
  std::optional<ScheduledStop> best;
  double bestCost = INF;
  if (!tl) return best;
  for (const TimetablePattern& p : tl->patterns) {
    const int last = static_cast<int>(p.stops.size()) - 1;
    for (int k = 0; k <= last; k++) {
      if (!here(p, k)) continue;
      // (at the end of its way it does not leave again: its arrival counts)
      const double tk = k == last && !p.post ? p.arr[static_cast<size_t>(k)] : p.dep[static_cast<size_t>(k)];
      for (const auto& [off, day] : serviceDays(at)) {
        const std::vector<double>& starts = p.starts[static_cast<size_t>(day)];
        const double lo = at.s - off - tk - window;
        for (size_t i = lowerBound(starts, lo); i < starts.size() && starts[i] <= lo + 2 * window; i++) {
          const double t0 = starts[i] + off;
          const double late = at.s - (t0 + tk);
          const double cost = late >= 0 ? late : -late * 1.5;
          if (cost < bestCost) {
            bestCost = cost;
            ScheduledStop s;
            s.pattern = &p;
            s.t0 = t0;
            s.k = k;
            s.time = t0 + tk;
            s.late = late;
            best = s;
          }
        }
      }
    }
  }
  return best;
}

/** The day of the month of the last Sunday of a month (0–11) in a year. */
int lastSunday(int y, int month) {
  const DateFields end = utcFields(utcMs(y, month + 1, 0));
  return end.date - end.day;
}

/** The public holidays of Hesse in a year, as month * 100 + day. */
const std::set<int>& holidays(int y) {
  static auto* cache = new std::map<int, std::set<int>>();
  auto it = cache->find(y);
  if (it != cache->end()) return it->second;
  const std::array<int, 2> e = easter(y);
  const double sunday = utcMs(y, e[0] - 1, e[1]);
  const auto after = [&](int days) {
    const DateFields t = utcFields(sunday + days * DAY_SECONDS * 1000);
    return (t.month + 1) * 100 + t.date;
  };
  // New Year, Good Friday, Easter Monday, Labour Day, Ascension, Whit Monday, Corpus Christi, German Unity, Christmas
  return cache->emplace(y, std::set<int>{101, after(-2), after(1), 501, after(39), after(50), after(60), 1003, 1225, 1226}).first->second;
}

}  // namespace

std::unique_ptr<Timetable> parseTimetable(const TimetableJson& json, const LinesJson* lines, const BusLinesRef* buslines) {
  if (json.version < 1 || json.version > TIMETABLE_VERSION) throw std::runtime_error("timetable.json: version " + std::to_string(json.version) + " is not known");
  auto tt = std::make_unique<Timetable>();
  tt->attribution = json.attribution;
  tt->valid = json.valid;
  tt->week = json.week;
  // (lines dropped because another of the same place came later: kept, as the calls still point into them)
  std::vector<std::unique_ptr<TimetableLine>> dropped;
  const auto place = [](std::vector<std::unique_ptr<TimetableLine>>& list, int i, std::unique_ptr<TimetableLine> line, std::vector<std::unique_ptr<TimetableLine>>& old) {
    if (i < 0) return;
    if (static_cast<int>(list.size()) <= i) list.resize(static_cast<size_t>(i) + 1);
    if (list[static_cast<size_t>(i)]) old.push_back(std::move(list[static_cast<size_t>(i)]));
    list[static_cast<size_t>(i)] = std::move(line);
  };
  // the stop places of the trams' stops, by name (a file of version 1 has none)
  std::unordered_map<std::string, std::vector<int>> tramPlaces;
  for (size_t i = 0; i < json.stops.size(); i++) {
    if (!json.stopPlaces || i >= json.stopPlaces->size()) continue;
    const std::vector<int>& places = (*json.stopPlaces)[i];
    if (!places.empty()) {
      std::vector<int>& list = tramPlaces[json.stops[i]];
      list.insert(list.end(), places.begin(), places.end());
    }
  }
  const auto stopPlace = [&](int i) {
    if (!json.stopPlaces || i < 0 || i >= static_cast<int>(json.stopPlaces->size())) return -1;
    const std::vector<int>& list = (*json.stopPlaces)[static_cast<size_t>(i)];
    return list.empty() ? -1 : list[0];
  };

  for (const TimetableLineJson& l : json.lines) {
    if (lines && (l.line < 0 || l.line >= static_cast<int>(lines->lines.size()) || lines->lines[static_cast<size_t>(l.line)].ref != l.ref)) continue;
    auto line = std::make_unique<TimetableLine>();
    line->kind = "tram";
    line->index = l.line;
    line->ref = l.ref;
    line->colour = l.colour.value_or("");
    line->patterns.reserve(l.patterns.size());
    for (const TimetablePatternJson& p : l.patterns) {
      const size_t n = std::min(p.s.size(), p.d.size());
      if (n == 0) continue;
      TimetablePattern pattern;
      pattern.line = line.get();
      for (size_t k = 0; k < n; k++) {
        const int i = p.s[k];
        pattern.stops.push_back(i >= 0 && i < static_cast<int>(json.stops.size()) ? json.stops[static_cast<size_t>(i)] : "");
        pattern.lineStops.push_back(k < p.j.size() ? p.j[k] : -1);
        pattern.places.push_back(stopPlace(i));
      }
      pattern.dep.assign(p.d.begin(), p.d.begin() + static_cast<long>(n));
      const std::vector<double>& a = p.a ? *p.a : p.d;
      pattern.arr.assign(a.begin(), a.begin() + static_cast<long>(std::min(n, a.size())));
      pattern.to = p.to;
      pattern.dests.assign(n, p.to);
      pattern.pre = p.pre == 1;
      pattern.post = p.post == 1;
      pattern.starts = {sorted(p.t[0]), sorted(p.t[1]), sorted(p.t[2])};
      line->patterns.push_back(std::move(pattern));
      const TimetablePattern* pp = &line->patterns.back();
      for (size_t k = 0; k < pp->stops.size(); k++) {
        const std::string& name = pp->stops[k];
        tt->calls[name].push_back(StopCall{pp, static_cast<int>(k)});
        auto own = tramPlaces.find(name);
        if (own != tramPlaces.end())
          for (int pl : own->second) tt->placeCalls[pl].push_back(StopCall{pp, static_cast<int>(k)});
      }
    }
    if (!line->patterns.empty()) place(tt->lines, l.line, std::move(line), dropped);
  }

  const TimetableBusJson* bus = json.version >= 2 && json.bus ? &*json.bus : nullptr;
  if (bus) {
    tt->busMap = bus->map;
    tt->busPlaces = bus->places;
    std::unordered_map<int, std::string> names;
    for (const auto& [num, name] : bus->stops) names[num] = name;
    for (const TimetableBusLineJson& l : bus->lines) {
      if (buslines) {
        const BusLinesRef::Line* known = l.line >= 0 && l.line < static_cast<int>(buslines->lines.size()) ? &buslines->lines[static_cast<size_t>(l.line)] : nullptr;
        if (!known || known->ref != l.ref || (known->osm && *known->osm != l.osm)) continue;
      }
      auto line = std::make_unique<TimetableLine>();
      line->kind = "bus";
      line->index = l.line;
      line->ref = l.ref;
      line->colour = l.colour.value_or("");
      line->patterns.reserve(l.patterns.size());
      for (const TimetableBusPatternJson& p : l.patterns) {
        const int n = static_cast<int>(std::min(p.j.size(), p.d.size()));
        if (n == 0) continue;
        TimetablePattern pattern;
        pattern.line = line.get();
        pattern.lineStops = counted(p.j, n);
        for (int j : pattern.lineStops) pattern.places.push_back(j >= 0 && j < static_cast<int>(l.s.size()) ? l.s[static_cast<size_t>(j)] : -1);
        pattern.dep = counted(p.d, n);
        const std::vector<std::string> dests = destinations(p.to, bus->dests, n);
        for (int pl : pattern.places) {
          auto it = names.find(pl);
          pattern.stops.push_back(it != names.end() ? it->second : "");
        }
        for (size_t k = 0; k < pattern.dep.size(); k++) pattern.arr.push_back(pattern.dep[k] - (p.w && k < p.w->size() ? (*p.w)[k] : 0));
        pattern.to = dests[0];
        pattern.dests = dests;
        pattern.pre = p.pre == 1;
        pattern.post = p.post == 1;
        pattern.starts = {sorted(counted(p.t[0])), sorted(counted(p.t[1])), sorted(counted(p.t[2]))};
        line->patterns.push_back(std::move(pattern));
        const TimetablePattern* pp = &line->patterns.back();
        for (size_t k = 0; k < pp->places.size(); k++) {
          const int pl = pp->places[k];
          if (pl < 0) continue;
          tt->placeCalls[pl].push_back(StopCall{pp, static_cast<int>(k)});
          // (by its name only where that is the trams' stop of the name, or the name is no tram stop's)
          const std::string& name = pp->stops[k];
          auto own = tramPlaces.find(name);
          if (!name.empty() && (own == tramPlaces.end() || std::find(own->second.begin(), own->second.end(), pl) != own->second.end())) tt->calls[name].push_back(StopCall{pp, static_cast<int>(k)});
        }
      }
      if (!line->patterns.empty()) place(tt->buses, l.line, std::move(line), dropped);
    }
  }
  // (the dropped ones live on: calls may point into them)
  for (auto& d : dropped) tt->retired.push_back(std::move(d));
  return tt;
}

std::vector<Departure> departures(const Timetable& tt, const StopKey& stop, const ServiceTime& at, int n, double horizon) {
  std::vector<Departure> out;
  const std::vector<StopCall>* calls = nullptr;
  if (const int* place = std::get_if<int>(&stop)) {
    auto it = tt.placeCalls.find(*place);
    if (it != tt.placeCalls.end()) calls = &it->second;
  } else {
    auto it = tt.calls.find(std::get<std::string>(stop));
    if (it != tt.calls.end()) calls = &it->second;
  }
  if (calls) {
    for (const StopCall& c : *calls) {
      const TimetablePattern& p = *c.pattern;
      const int k = c.k;
      // (where its trips end nobody gets on)
      if (k == static_cast<int>(p.stops.size()) - 1 && !p.post) continue;
      for (const auto& [off, day] : serviceDays(at)) {
        const std::vector<double>& starts = p.starts[static_cast<size_t>(day)];
        const double lo = at.s - off - p.dep[static_cast<size_t>(k)];
        int found = 0;
        for (size_t i = lowerBound(starts, lo); i < starts.size() && starts[i] <= lo + horizon && found < n; i++, found++) {
          const double t0 = starts[i] + off;
          const double time = t0 + p.dep[static_cast<size_t>(k)];
          const TimetableLine& l = *p.line;
          Departure d;
          d.kind = l.kind;
          d.line = l.index;
          d.ref = l.ref;
          d.colour = l.colour;
          d.to = static_cast<size_t>(k) < p.dests.size() ? p.dests[static_cast<size_t>(k)] : p.to;
          d.wait = time - at.s;
          d.time = time;
          d.trip = TripRef{&p, t0};
          d.k = k;
          d.place = static_cast<size_t>(k) < p.places.size() ? p.places[static_cast<size_t>(k)] : -1;
          out.push_back(std::move(d));
        }
      }
    }
  }
  std::stable_sort(out.begin(), out.end(), [](const Departure& a, const Departure& b) {
    if (a.time != b.time) return a.time < b.time;
    if (a.ref != b.ref) return a.ref < b.ref;
    if (a.kind == b.kind) return false;
    return a.kind == "tram";
  });
  if (static_cast<int>(out.size()) > n) out.resize(static_cast<size_t>(std::max(0, n)));
  return out;
}

std::vector<TripState> tripsAt(const Timetable& tt, int line, const ServiceTime& at, double lead) {
  std::vector<TripState> out;
  return lineTrips(tt.tram(line), at, lead, out);
}

std::vector<TripState> busTripsAt(const Timetable& tt, int line, const ServiceTime& at, double lead) {
  std::vector<TripState> out;
  return lineTrips(tt.bus(line), at, lead, out);
}

std::vector<Running> runningAt(const Timetable& tt, const ServiceTime& at, double lead) {
  std::vector<Running> out;
  std::vector<TripState> trips;
  for (const auto* list : {&tt.lines, &tt.buses}) {
    for (const auto& tl : *list) {
      if (!tl) continue;
      trips.clear();
      for (const TripState& t : lineTrips(tl.get(), at, lead, trips)) {
        const TimetablePattern& p = *t.pattern;
        Running r;
        static_cast<TripState&>(r) = t;
        r.kind = tl->kind;
        r.line = tl->index;
        r.ref = tl->ref;
        r.colour = tl->colour;
        const auto F = static_cast<size_t>(t.from);
        const auto T = static_cast<size_t>(t.to);
        r.dest = F < p.dests.size() ? p.dests[F] : p.to;
        r.fromStop = F < p.lineStops.size() ? p.lineStops[F] : -1;
        r.toStop = T < p.lineStops.size() ? p.lineStops[T] : -1;
        r.fromName = F < p.stops.size() ? p.stops[F] : "";
        r.toName = T < p.stops.size() ? p.stops[T] : "";
        r.fromPlace = F < p.places.size() ? p.places[F] : -1;
        r.toPlace = T < p.places.size() ? p.places[T] : -1;
        out.push_back(std::move(r));
      }
    }
  }
  return out;
}

std::optional<ScheduledStop> scheduledAt(const Timetable& tt, int line, const std::string& stop, const ServiceTime& at, double window) {
  return scheduledOn(tt.tram(line), [&](const TimetablePattern& p, int k) { return p.stops[static_cast<size_t>(k)] == stop; }, at, window);
}

std::optional<ScheduledStop> busScheduledAt(const Timetable& tt, int line, int place, const ServiceTime& at, double window) {
  return scheduledOn(tt.bus(line), [&](const TimetablePattern& p, int k) { return static_cast<size_t>(k) < p.places.size() && p.places[static_cast<size_t>(k)] == place; }, at, window);
}

// ---- the clock in Kassel ----

double berlinOffset(double ms) {
  const int y = utcFields(ms).year;
  const double start = utcMs(y, 2, lastSunday(y, 2), 1);
  const double end = utcMs(y, 9, lastSunday(y, 9), 1);
  return ms >= start && ms < end ? 7200 : 3600;
}

BerlinClock berlinClock(double ms) {
  const DateFields local = utcFields(ms + berlinOffset(ms) * 1000);
  const double s = local.hours * 3600 + local.minutes * 60 + local.seconds + local.milliseconds / 1000.0;
  return BerlinClock{local.year, local.month + 1, local.date, local.day, s};
}

std::array<int, 2> easter(int y) {
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

bool isHoliday(int y, int m, int d) { return holidays(y).count(m * 100 + d) > 0; }

DayType dayTypeOf(int y, int m, int d) {
  const int weekday = utcFields(utcMs(y, m - 1, d)).day;
  if (weekday == 0 || isHoliday(y, m, d)) return DAY_SUNDAY;
  if (weekday == 6 || (m == 12 && (d == 24 || d == 31))) return DAY_SATURDAY;
  return DAY_WEEKDAY;
}

ServiceTime serviceTime(double ms) {
  const BerlinClock c = berlinClock(ms);
  const double at = utcMs(c.y, c.m - 1, c.d);
  const auto typeAt = [&](int days) {
    const DateFields t = utcFields(at + days * DAY_SECONDS * 1000);
    return dayTypeOf(t.year, t.month + 1, t.date);
  };
  return ServiceTime{c.s, typeAt(0), typeAt(-1), typeAt(1)};
}

ServiceTime weekTime(int weekday, double s) {
  const auto type = [](int w) {
    const int k = ((w % 7) + 7) % 7;
    return k == 0 ? DAY_SUNDAY : k == 6 ? DAY_SATURDAY : DAY_WEEKDAY;
  };
  return ServiceTime{s, type(weekday), type(weekday - 1), type(weekday + 1)};
}

}  // namespace fs

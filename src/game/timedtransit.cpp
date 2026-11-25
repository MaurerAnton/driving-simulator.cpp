// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/timedtransit.ts @ v1.7.7
#include "game/timedtransit.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_map>

#include "core/geom.h"
#include "core/js.h"
#include "world/lanes.h"

namespace fs {

namespace {

constexpr double INF = std::numeric_limits<double>::infinity();
/** the world's clock ran on this much more than between two looks for trips (ms): the vehicles are put anew */
constexpr double JUMP_MS = 30000;

/** a tram's: where on the tracks its stops are (piece → [mark, place in the pattern]) – null where one of them is not known */
using Marks = std::optional<std::unordered_map<int, std::vector<std::pair<double, int>>>>;

/** A trip as the vehicle running it knows it: its pattern, when it leaves its first stop, and the midnight that counts from. */
struct TripEntry {
  const TimetablePattern* pattern = nullptr;
  double t0 = 0;
  /** ms since 1970 of the midnight (local time in Kassel) its times count from */
  double day = 0;
  /** the stop of the pattern it stood at last (−1: none yet) – until its vehicle is under way (`bound`), the one the clock had it leave last (`from`) */
  int last = -1;
  int from = 0;
  bool bound = false;
  /** its vehicle has come to the end of it: it is not put on again */
  bool done = false;
  Marks marks;
};

/** Seconds after the midnight a trip counts from, at the moment `ms`. */
double since(const TripEntry& e, double ms) { return (ms - e.day) / 1000; }

/** The vehicle of a trip is under way: from here on the stops it stood at count, not the clock. */
void bind(TripEntry& e) {
  if (e.bound) return;
  e.bound = true;
  e.last = e.from;
}

/**
 * Of the stops `ks` of a trip, the one it is at or runs to: the next after
 * the one it stood at last, if that is among them – else the one whose
 * departure lies nearest to the moment (a line that loops passes a place twice).
 */
int nearestCall(const TripEntry& e, const std::vector<int>& ks, double now) {
  if (e.bound && std::find(ks.begin(), ks.end(), e.last + 1) != ks.end()) return e.last + 1;
  int best = -1;
  double bd = INF;
  for (int k : ks) {
    const double d = std::abs(e.t0 + e.pattern->dep[static_cast<size_t>(k)] - now);
    if (d < bd) {
      bd = d;
      best = k;
    }
  }
  return best;
}

/** When a vehicle is to be at stop k of its trip: at its arrival, ARRIVE_EARLY before it leaves at the latest – at the end of its trip, at its arrival. */
double arriveBy(const TripEntry& e, int k) {
  const TimetablePattern& p = *e.pattern;
  const auto K = static_cast<size_t>(k);
  const bool end = k == static_cast<int>(p.stops.size()) - 1 && !p.post;
  return e.t0 + (end ? p.arr[K] : std::min(p.arr[K], p.dep[K] - ARRIVE_EARLY));
}

/** The index of a pattern in its line's patterns (indexOf). */
int patternIndex(const TimetablePattern& p) {
  const std::vector<TimetablePattern>& list = p.line->patterns;
  for (size_t i = 0; i < list.size(); i++)
    if (&list[i] == &p) return static_cast<int>(i);
  return -1;
}

/** The trips the vehicles run, by their names (shared by the trams and the buses: the names differ). */
class TripBook {
 public:
  explicit TripBook(std::function<const Timetable*()> tt) : tt_(std::move(tt)) {}

  /** The name for a trip under way (the one it already has, if it has one), and its entry. */
  std::string name(const std::string& kind, int line, const TripState& trip, double day, const std::function<Marks()>& marks) {
    const TimetablePattern& p = *trip.pattern;
    const std::string abs = kind + ":" + std::to_string(line) + ":" + std::to_string(patternIndex(p)) + ":" + jsStr(jsRound(day / 1000 + trip.t0));
    std::string nm;
    auto it = names_.find(abs);
    if (it != names_.end() && !it->second.empty()) nm = it->second;
    else {
      nm = tripName(kind, line, p, trip.t0);
      names_[abs] = nm;
    }
    auto e = trips_.find(nm);
    if (e == trips_.end()) {
      TripEntry n;
      n.pattern = &p;
      n.t0 = trip.t0;
      n.day = day;
      n.last = -1;
      n.from = trip.from;
      n.bound = false;
      n.marks = marks();
      trips_.emplace(nm, std::move(n));
    } else if (!e->second.bound) e->second.from = trip.from;
    return nm;
  }

  /** The entry of a trip by its name – one named elsewhere (src/game/spectate.ts puts a vehicle there itself) is read from the name. */
  TripEntry* get(const std::string& name, double ms, const std::function<Marks(const TimetablePattern& p)>& marks) {
    if (name.empty()) return nullptr;
    auto have = trips_.find(name);
    if (have != trips_.end()) return &have->second;
    const Timetable* tt = tt_();
    // ^(b:)?(\d+):(\d+):(-?\d+(?:\.\d+)?)$
    if (!tt) return nullptr;
    std::string rest = name;
    const bool bus = startsWith(rest, "b:");
    if (bus) rest = rest.substr(2);
    const std::vector<std::string> parts = split(rest, ":");
    if (parts.size() != 3) return nullptr;
    const auto digits = [](const std::string& s) {
      if (s.empty()) return false;
      for (char c : s)
        if (c < '0' || c > '9') return false;
      return true;
    };
    const std::string& t = parts[2];
    {
      std::string u = t;
      if (!u.empty() && u[0] == '-') u = u.substr(1);
      const size_t dot = u.find('.');
      if (!digits(parts[0]) || !digits(parts[1]) || !digits(dot == std::string::npos ? u : u.substr(0, dot)) || (dot != std::string::npos && !digits(u.substr(dot + 1)))) return nullptr;
    }
    const TimetableLine* line = bus ? tt->bus(static_cast<int>(jsNumber(parts[0]))) : tt->tram(static_cast<int>(jsNumber(parts[0])));
    const int pi = static_cast<int>(jsNumber(parts[1]));
    const TimetablePattern* p = line && pi >= 0 && pi < static_cast<int>(line->patterns.size()) ? &line->patterns[static_cast<size_t>(pi)] : nullptr;
    if (!p) return nullptr;
    const ServiceTime now = serviceTime(ms);
    const double t0 = jsNumber(t);
    // (the stop it left last by the clock)
    int from = 0;
    while (from + 1 < static_cast<int>(p->stops.size()) && t0 + p->dep[static_cast<size_t>(from) + 1] <= now.s) from++;
    TripEntry e;
    e.pattern = p;
    e.t0 = t0;
    e.day = ms - now.s * 1000;
    e.last = -1;
    e.from = from;
    e.bound = false;
    e.marks = marks(*p);
    return &trips_.emplace(name, std::move(e)).first->second;
  }

  /** Has the vehicle of a trip come to its end? */
  bool done(const std::string& name) const {
    auto it = trips_.find(name);
    return it != trips_.end() && it->second.done;
  }

  /** Forget the trips that ended more than an hour ago (now and then). */
  void prune(double ms) {
    if (trips_.size() < 3000) return;
    for (auto it = trips_.begin(); it != trips_.end();) {
      const TripEntry& e = it->second;
      if (e.t0 + e.pattern->arr[e.pattern->stops.size() - 1] + 3600 < since(e, ms)) it = trips_.erase(it);
      else ++it;
    }
    if (names_.size() > 6000) names_.clear();
  }

 private:
  std::unordered_map<std::string, TripEntry> trips_;
  /** a trip by when it really leaves (line, pattern, ms): the name it was first given (past midnight its t0 is counted from another day) */
  std::unordered_map<std::string, std::string> names_;
  std::function<const Timetable*()> tt_;
};

// ---------------------------------------------------------------- the trams

class TramSchedule : public RailSchedule {
 public:
  explicit TramSchedule(TramScheduleHost h) : h_(std::move(h)), book_([t = h_.timetable] { return t(); }) {}

  std::vector<RailDue> due(double x, double z, double r0, double r1, bool all) override {
    const Timetable* tt = h_.timetable();
    const LinesJson* lines = h_.lines();
    RailTraffic& rail = h_.rail();
    std::vector<RailDue> out;
    if (!tt || !lines) return out;
    const double ms = h_.nowMs();
    const bool jumped = lastMs_ > 0 && std::abs(ms - lastMs_) > JUMP_MS;
    lastMs_ = ms;
    if (jumped) {
      // (they come again at once where their trips are now)
      rail.resync();
      return out;
    }
    book_.prune(ms);
    const ServiceTime now = serviceTime(ms);
    const double day = ms - now.s * 1000;
    for (const auto& line : tt->lines) {
      if (!line || line->index < 0 || line->index >= static_cast<int>(lines->lines.size())) continue;
      const std::vector<std::pair<int, double>>& stops = lines->lines[static_cast<size_t>(line->index)].stops;
      for (const TripState& trip : tripsAt(*tt, line->index, now, LEAD)) {
        const TimetablePattern& p = *trip.pattern;
        const auto stopAt = [&](int k) -> const std::pair<int, double>* {
          const int j = k >= 0 && k < static_cast<int>(p.lineStops.size()) ? p.lineStops[static_cast<size_t>(k)] : -1;
          return j >= 0 && j < static_cast<int>(stops.size()) ? &stops[static_cast<size_t>(j)] : nullptr;
        };
        const std::pair<int, double>* a = stopAt(trip.from);
        const std::pair<int, double>* b = stopAt(trip.to);
        if (!a || !b) continue;
        const std::optional<LineSpot> spot = rail.lineSpot(line->index, PathMark{a->first, a->second}, PathMark{b->first, b->second}, trip.standing ? 0 : trip.frac);
        if (!spot) continue;
        rail.net.sample(spot->path, spot->s, smp_);
        const double d = std::hypot(smp_.x - x, smp_.z - z);
        // (one standing at its first stop until it leaves may appear close by: it has come in to start its trip there)
        const bool starting = trip.standing && trip.from == 0;
        if (d > r1 || (!all && !starting && d < r0)) continue;
        const std::string name = book_.name("tram", line->index, trip, day, [&] { return marksOf(p); });
        if (book_.done(name)) continue;
        RailDue due;
        due.trip = name;
        due.line = line->index;
        due.path = spot->path;
        due.s = spot->s;
        due.dir = spot->dir;
        due.k = spot->k;
        due.speed = trip.standing ? 0 : 9;
        if (trip.standing) due.dwell = trip.until - now.s;
        out.push_back(std::move(due));
      }
    }
    return out;
  }

  double wait(const RailVehicle& v, int path, double mark) override {
    TripEntry* e = entry(v);
    if (!e) return 0;
    bind(*e);
    const std::optional<std::vector<int>> ks = callsAt(*e, path, mark);
    const TimetablePattern& p = *e->pattern;
    // (where its stops are not known: no more than the doors take)
    const double now = since(*e, h_.nowMs());
    const int k = ks ? nearestCall(*e, *ks, now) : -1;
    if (k < 0) return 0;
    e->last = k;
    if (k == static_cast<int>(p.stops.size()) - 1 && !p.post) return 0;
    return e->t0 + p.dep[static_cast<size_t>(k)] - now;
  }

  bool hasCalls() const override { return true; }
  bool calls(const RailVehicle& v, int path, double mark) override {
    TripEntry* e = entry(v);
    const std::optional<std::vector<int>> ks = e ? callsAt(*e, path, mark) : std::nullopt;
    return !ks || !ks->empty();
  }

  bool hasPace() const override { return true; }
  double pace(const RailVehicle& v, int path, double mark, double dist) override {
    TripEntry* e = entry(v);
    const std::optional<std::vector<int>> ks = e ? callsAt(*e, path, mark) : std::nullopt;
    if (!e || !ks || ks->empty()) return INF;
    const double now = since(*e, h_.nowMs());
    const int k = nearestCall(*e, *ks, now);
    return paceFor(dist, arriveBy(*e, k) - now);
  }

  bool hasToward() const override { return true; }
  std::optional<PathMark> toward(const RailVehicle& v) override {
    TripEntry* e = entry(v);
    const LinesJson* lines = h_.lines();
    const LineJson* line = e && lines && e->pattern->line->index >= 0 && e->pattern->line->index < static_cast<int>(lines->lines.size()) ? &lines->lines[static_cast<size_t>(e->pattern->line->index)] : nullptr;
    if (!e || !line) return std::nullopt;
    bind(*e);
    for (int k = e->last + 1; k < static_cast<int>(e->pattern->stops.size()); k++) {
      const int j = e->pattern->lineStops[static_cast<size_t>(k)];
      if (j >= 0 && j < static_cast<int>(line->stops.size())) return PathMark{line->stops[static_cast<size_t>(j)].first, line->stops[static_cast<size_t>(j)].second};
    }
    return std::nullopt;
  }

  bool hasEnds() const override { return true; }
  bool ends(const RailVehicle& v) override {
    TripEntry* e = entry(v);
    if (!e || e->last != static_cast<int>(e->pattern->stops.size()) - 1 || e->pattern->post) return false;
    e->done = true;
    return true;
  }

 private:
  TramScheduleHost h_;
  TripBook book_;
  RailSample smp_;
  /** the world's moment at the last look for trips: when the clock ran on while the world stood still (a pause, the big map open), the trams are put anew */
  double lastMs_ = 0;

  Marks marksOf(const TimetablePattern& p) const {
    const LinesJson* lines = h_.lines();
    const int li = p.line->index;
    if (!lines || li < 0 || li >= static_cast<int>(lines->lines.size())) return std::nullopt;
    const std::vector<std::pair<int, double>>& stops = lines->lines[static_cast<size_t>(li)].stops;
    std::unordered_map<int, std::vector<std::pair<double, int>>> out;
    for (size_t k = 0; k < p.lineStops.size(); k++) {
      const int j = p.lineStops[k];
      if (j < 0 || j >= static_cast<int>(stops.size())) return std::nullopt;
      const std::pair<int, double>& st = stops[static_cast<size_t>(j)];
      out[st.first].emplace_back(st.second, static_cast<int>(k));
    }
    return out;
  }

  TripEntry* entry(const RailVehicle& v) {
    return book_.get(v.trip, h_.nowMs(), [this](const TimetablePattern& p) { return marksOf(p); });
  }

  /** the stops of the trip at the mark `mark` of piece `path` (null: not known where its stops are) */
  static std::optional<std::vector<int>> callsAt(const TripEntry& e, int path, double mark) {
    if (!e.marks) return std::nullopt;
    std::vector<int> out;
    auto it = e.marks->find(path);
    if (it != e.marks->end())
      for (const auto& [m, k] : it->second)
        if (std::abs(m - mark) < 1.5) out.push_back(k);
    return out;
  }
};

// ---------------------------------------------------------------- the buses

/** how many buses of the timetables may be about the player at once */
constexpr int BUS_CAP = 10;
/** the least a bus of the timetable stands at a stop (s) */
constexpr double BUS_DWELL = 8;
/** a bus this far from where its trip is by the timetable (m) is put there anew, out of the player's sight */
constexpr double BUS_MISPLACED = 300;

class TimedBuses : public BusSchedule {
 public:
  explicit TimedBuses(BusScheduleHost h) : h_(std::move(h)), book_([t = h_.timetable] { return t(); }) {}

  bool timed(int line) override {
    const Timetable* tt = h_.timetable();
    return tt && tt->bus(line);
  }

  void reset() override {
    fresh_ = true;
    timer_ = 0;
  }

  void step(double dt, const PlayerState& player) override {
    timer_ -= dt;
    if (timer_ > 0) return;
    timer_ = 2;
    const Timetable* tt = h_.timetable();
    TransitSource* src = h_.source();
    BusLines* lines = src ? src->busLines() : nullptr;
    Traffic& traffic = h_.traffic();
    if (!tt || !src || !lines || traffic.targetCount <= 0) return;
    const double ms = h_.nowMs();
    // (the clock ran on while the world stood still – a pause, the big map open: the buses are put anew where their trips are now)
    if (lastMs_ > 0 && std::abs(ms - lastMs_) > JUMP_MS) resyncBuses(traffic);
    lastMs_ = ms;
    const bool all = fresh_;
    fresh_ = false;
    book_.prune(ms);
    const ServiceTime now = serviceTime(ms);
    const double day = ms - now.s * 1000;
    const double r0 = 170;
    const double r1 = traffic.spawnRadius;
    Stops* stops = src->stops();
    struct Found {
      std::string name;
      double d;
      int line;
      TripState trip;
      double x, z;
      int k;
    };
    std::vector<Found> found;
    for (size_t li = 0; li < tt->buses.size(); li++) {
      const TimetableLine* tl = tt->buses[li].get();
      const BusLine* line = li < lines->lines.size() ? lines->lines[li] : nullptr;
      if (!tl || !line) continue;
      for (const TripState& trip : busTripsAt(*tt, static_cast<int>(li), now, LEAD)) {
        const TimetablePattern& p = *trip.pattern;
        const auto at = [](const std::vector<int>& v, int k) { return k >= 0 && k < static_cast<int>(v.size()) ? v[static_cast<size_t>(k)] : -1; };
        // (far off by its stops: not worked out further)
        const int pf = at(p.places, trip.from);
        const StopPlace* pa = stops && pf >= 0 && pf < static_cast<int>(stops->places.size()) ? &stops->places[static_cast<size_t>(pf)] : nullptr;
        if (pa && std::hypot(pa->x - player.x, pa->z - player.z) > r1 + 1500) continue;
        const std::optional<Spot> spot = spotOf(*src, TripSpot{"bus", static_cast<int>(li), at(p.lineStops, trip.from), at(p.lineStops, trip.to), at(p.places, trip.from), at(p.places, trip.to), trip.standing, trip.frac});
        if (!spot || !spot->k) continue;
        const double d = std::hypot(spot->x - player.x, spot->z - player.z);
        // (one standing at its first stop until it leaves may appear close by: it has come in to start its trip there)
        if (d > r1 || (!all && d < r0 && !(trip.standing && trip.from == 0))) continue;
        const std::string name = book_.name("bus", static_cast<int>(li), trip, day, [] { return Marks(); });
        if (!book_.done(name)) found.push_back(Found{name, d, static_cast<int>(li), trip, spot->x, spot->z, *spot->k});
      }
    }
    std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.d < b.d; });
    int n = 0;
    for (const TrafficCar* c : traffic.cars)
      if (c->bus && !c->bus->trip.empty()) n++;
    for (const Found& f : found) {
      TrafficCar* had = nullptr;
      for (TrafficCar* c : traffic.cars)
        if (c->bus && c->bus->trip == f.name) {
          had = c;
          break;
        }
      if (had) {
        // far from where its trip is, and the player close to neither: put there anew
        if (std::hypot(had->x - f.x, had->z - f.z) < BUS_MISPLACED || std::hypot(had->x - player.x, had->z - player.z) < r0 || f.d < r0) continue;
        traffic.removeCar(had);
        n--;
      }
      if (n >= BUS_CAP) break;
      const BusLine& line = *lines->lines[static_cast<size_t>(f.line)];
      const int w = f.k >= 0 && f.k < static_cast<int>(line.ways.size()) ? line.ways[static_cast<size_t>(f.k)] : -1;
      if (w < 0) continue;
      const LaneGraph::EdgeLanes* el = traffic.graph.edgeLanes[w >> 1];
      const std::vector<Lane*>* lanes = el ? ((w & 1) == 0 ? &el->fwd : &el->back) : nullptr;
      Lane* lane = nullptr;
      if (lanes) {
        for (Lane* l : *lanes)
          if (l->idx == 0) {
            lane = l;
            break;
          }
        if (!lane && !lanes->empty()) lane = (*lanes)[0];
      }
      if (!lane) continue;
      projectOnPolyline(lane->edge->pl, f.x, f.z, proj_);
      // (its front a few metres short of where the timetable has it: one standing at its stop pulls up to the pole)
      const double along = std::max(6.0, std::min(lane->len - 6, (lane->dir > 0 ? proj_.s - lane->s0 : lane->s0 - proj_.s) - 10));
      if (lane->len < 14) continue;
      // (room on the lane)
      bool full = false;
      if (const std::vector<TrafficCar*>* on = traffic.carsOn(*lane))
        for (const TrafficCar* o : *on)
          if (std::abs(o->s - along) < 22) full = true;
      if (full) continue;
      const TimetablePattern& p = *f.trip.pattern;
      const std::string to = static_cast<size_t>(f.trip.from) < p.dests.size() && !p.dests[static_cast<size_t>(f.trip.from)].empty() ? p.dests[static_cast<size_t>(f.trip.from)] : line.to;
      TrafficCar* car = traffic.addCar(*lane, along, true, BusOnLine{line.ref, to, f.line, f.k, 0});
      if (!car || !car->bus) continue;
      car->bus->trip = f.name;
      if (f.trip.standing) car->v = 0;
      n++;
    }
  }

  double dwell(TrafficCar& car, int pole, double least) override {
    TripEntry* e = car.bus ? book_.get(car.bus->trip, h_.nowMs(), [](const TimetablePattern&) { return Marks(); }) : nullptr;
    if (!car.bus || !e) return least;
    TrafficCar::Bus& bus = *car.bus;
    bind(*e);
    const TimetablePattern& p = *e->pattern;
    const int place = stopPlaceOf(pole);
    std::vector<int> ks;
    for (size_t k = 0; k < p.places.size(); k++)
      if (p.places[k] == place) ks.push_back(static_cast<int>(k));
    const double now = since(*e, h_.nowMs());
    const int k = nearestCall(*e, ks, now);
    if (k < 0) return std::min(least, BUS_DWELL * 1.5);
    e->last = k;
    if (k == static_cast<int>(p.stops.size()) - 1 && !p.post) {
      // the end of its trip: everybody gets off, and it goes
      bus.retired = true;
      e->done = true;
      return BUS_DWELL * 1.5;
    }
    return std::max(BUS_DWELL, std::min(600.0, e->t0 + p.dep[static_cast<size_t>(k)] - now));
  }

 private:
  BusScheduleHost h_;
  TripBook book_;
  double timer_ = 0;
  bool fresh_ = true;
  double lastMs_ = 0;
  PolyProjection proj_;
};

}  // namespace

double paceFor(double dist, double time, double ramp) {
  if (time <= 1 || dist <= 0) return INF;
  // dist / v + ramp · v = time
  const double disc = time * time - 4 * ramp * dist;
  if (disc < 0) return INF;
  return (time - std::sqrt(disc)) / (2 * ramp);
}

std::unique_ptr<RailSchedule> tramSchedule(TramScheduleHost h) { return std::make_unique<TramSchedule>(std::move(h)); }

void resyncBuses(Traffic& traffic) {
  for (size_t i = traffic.cars.size(); i-- > 0;)
    if (traffic.cars[i]->bus && !traffic.cars[i]->bus->trip.empty()) traffic.removeCar(traffic.cars[i]);
  if (traffic.busSchedule) traffic.busSchedule->reset();
}

std::unique_ptr<BusSchedule> busSchedule(BusScheduleHost h) { return std::make_unique<TimedBuses>(std::move(h)); }

}  // namespace fs

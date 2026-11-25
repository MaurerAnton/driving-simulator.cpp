// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The lines of the city buses: the file and its index (src/world/buslines.ts),
 * the buses of the traffic that run them – along the line's roads at the
 * junctions, calling at its stops in their order, over to another lane where
 * the line turns, onto the line back at the end – on a small town made for
 * the purpose.
 *
 * Origin: tests/game/buslines.test.ts @ v1.7.7 (not here: loadBusLines through a stand-in loader, and the cases
 * of the build's matching of OpenStreetMap's ways – tools/build-buslines.ts is a build tool, not ported)
 */
// LINK: sim game world core platform geo
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "sim/signals.h"
#include "sim/traffic.h"
#include "town.h"
#include "world/buslines.h"
#include "world/routegraph.h"

using namespace fs;

namespace {

constexpr double DT = 1.0 / 30;

/** The way along the street from grid point (i0, j0) to (i1, j1): road * 2 + direction. */
int way(const Town& town, int i0, int j0, int i1, int j1) {
  const int e = town.street(i0, j0, i1, j1);
  return e * 2 + (town.json.edges[static_cast<size_t>(e)].a == town.node(i0, j0) ? 0 : 1);
}

/** A lane of a way (`idx` 0: the right-most). */
Lane* laneOf(const Town& town, int w, int idx = 0) {
  const LaneGraph::EdgeLanes* el = town.lanes->edgeLanes[w >> 1];
  return ((w & 1) == 0 ? el->fwd : el->back)[static_cast<size_t>(idx)];
}

BusLineJson lineJson(const std::string& ref, const std::string& to, const std::vector<int32_t>& ways, const std::vector<std::array<double, 3>>& stops, std::optional<int> end = std::nullopt,
                     std::optional<int> back = std::nullopt) {
  BusLineJson l;
  l.ref = ref;
  l.name = "Bus " + ref + ": … => " + to;
  l.to = to;
  l.operator_ = "Test";
  l.w = encodeWays(ways);
  for (const auto& s : stops) l.st.insert(l.st.end(), s.begin(), s.end());
  l.end = end;
  l.back = back;
  return l;
}

struct BusTown {
  Town town;
  SignalSystem signals;
  Traffic traffic;
  BusLinesJson json;
  PlayerState player;
  explicit BusTown(Town t) : town(std::move(t)), signals(town.world->signals, town.world->lightsOut), traffic(*town.world, *town.lanes, signals) {
    traffic.targetCount = 0;
    // (the anchor of the traffic floats high above the middle of the town: nobody brakes for it)
    player.x = 180;
    player.y = 160;
    player.z = 120;
    player.length = 0.5;
    player.width = 0.5;
  }
};

Town grid(TownOptions o = {}) {
  o.cols = 4;
  o.rows = 3;
  o.block = 120;
  return makeTown(o);
}

/**
 * A town of 4 × 3 junctions, 120 m apart.  Line 5 runs east along the middle
 * row, south at the third column, east again along the bottom row – and from
 * its end round the corner north, where the line back begins: west along
 * the middle row.
 */
std::unique_ptr<BusTown> busTown() {
  auto b = std::make_unique<BusTown>(grid());
  const Town& town = b->town;
  const std::vector<int32_t> A = {way(town, 0, 1, 1, 1), way(town, 1, 1, 2, 1), way(town, 2, 1, 2, 2), way(town, 2, 2, 3, 2)};
  const std::vector<int32_t> link = {way(town, 3, 2, 3, 1)};
  const std::vector<int32_t> B = {way(town, 3, 1, 2, 1), way(town, 2, 1, 1, 1), way(town, 1, 1, 0, 1)};
  std::vector<int32_t> al = A;
  al.insert(al.end(), link.begin(), link.end());
  b->json.v = 1;
  b->json.places = 30;
  b->json.lines = {lineJson("5", "Ostfriedhof", al, {{10, 1, 60}, {11, 3, 70}}, static_cast<int>(A.size()), 1), lineJson("5", "Westbahnhof", B, {{12, 1, 50}})};
  return b;
}

struct Ride {
  std::vector<int> ways;
  std::vector<int> served;
  std::vector<std::string> shown;
  std::vector<double> at;
  bool slid = false;
  bool lost = false;
};

/** Let the traffic run with one bus in it: the ways it drives, the stop places it calls at, what it shows, and where it stood at each stop. */
Ride ride(Traffic& traffic, TrafficCar* bus, const PlayerState& player, double seconds) {
  Ride r;
  int last = bus->bus->served;
  for (double t = 0; t < seconds; t += DT) {
    traffic.update(DT, player);
    if (std::find(traffic.cars.begin(), traffic.cars.end(), bus) == traffic.cars.end()) break;
    if (bus->lane && (r.ways.empty() || wayOf(*bus->lane) != r.ways.back())) r.ways.push_back(wayOf(*bus->lane));
    if (bus->bus->served != last) {
      last = bus->bus->served;
      if (last >= 0) {
        r.served.push_back(stopPlaceOf(last));
        r.at.push_back(bus->s + bus->length / 2);
      }
    }
    const std::optional<BusOnLine>& line = bus->bus->line;
    const std::string text = line ? line->ref + " " + line->to : "–";
    if (r.shown.empty() || text != r.shown.back()) r.shown.push_back(text);
    if (bus->slide != 0) r.slid = true;
    if (line && line->off > 0) r.lost = true;
    if (!line && r.shown.size() > 1) break;
  }
  return r;
}

}  // namespace

int main() {
  // ---- the lines of the file: their ways decoded, the lines by way, the stops by way, the line back
  {
    BusLinesJson json;
    json.v = 1;
    json.map = "build-1";
    json.places = 3;
    json.lines = {lineJson("7", "Bahnhof", {10, 12, 14, 31}, {{0, 0, 20}, {2, 2, 5}, {1, 1, 40}}, std::nullopt, 1), lineJson("7", "Markt", {30, 13, 11}, {}, 2)};
    assert((json.lines[0].w == std::vector<int>{10, 2, 2, 17}));   // stored as steps from the way before
    BusLines bl(json);
    const BusLine& a = *bl.lines[0];
    const BusLine& b = *bl.lines[1];
    assert((a.ways == std::vector<int32_t>{10, 12, 14, 31}));
    assert(a.stops.size() == 3 && a.stops[0].place == 0 && a.stops[1].place == 1 && a.stops[2].place == 2);   // the stops in the order of their ways
    assert(a.back == &b);
    assert(b.back == nullptr);
    assert(b.end == 2);   // the last way leads round to the line back
    assert(a.end == 4);
    std::vector<BusLines::At> at14 = bl.at(14);
    assert(at14.size() == 1 && at14[0].line->index == 0 && at14[0].k == 2);
    std::vector<BusLines::At> at30 = bl.at(30);
    assert(at30.size() == 1 && at30[0].line->index == 1 && at30[0].k == 0);
    assert(bl.has(13) && !bl.has(15) && !bl.has(-1) && !bl.has(10000000));
    std::vector<BusLineStop> out;
    bl.stopsOn(a, 1, 2, out);
    assert(out.size() == 2 && out[0].place == 1 && out[1].place == 2);
    assert(bl.stopsOn(a, 3, 3, out).empty());
    assert(bl.fits("build-1", 3) && bl.fits("build-1") && !bl.fits("build-2") && !bl.fits("build-1", 4));
  }

  // ---- a bus on a line follows its roads at the junctions, calls at its stops in order and goes on as the line back at its end
  {
    std::unique_ptr<BusTown> t = busTown();
    BusLines lines(t->json);
    t->traffic.setBusLines(&lines);
    const BusLine& A = *lines.lines[0];
    // the map's poles: one of a stop the line does not call at, on its first road; the pole of its second stop, 40 m along that road
    const Lane* first = laneOf(t->town, A.ways[0]);
    const Lane* fourth = laneOf(t->town, A.ways[3]);
    t->traffic.busStops = [&](const Lane& lane) {
      return &lane == first ? std::vector<BusStopOnLane>{{50, 20 * 64}} : &lane == fourth ? std::vector<BusStopOnLane>{{40, 11 * 64 + 1}} : std::vector<BusStopOnLane>();
    };
    TrafficCar* bus = t->traffic.addCar(*laneOf(t->town, A.ways[0]), 10, true);
    assert(bus->bus->line && bus->bus->line->ref == "5");   // it takes the line that runs here
    assert(bus->bus->line->to == "Ostfriedhof");
    const Ride r = ride(t->traffic, bus, t->player, 400);
    std::vector<int> expected(A.ways.begin(), A.ways.end());
    expected.insert(expected.end(), lines.lines[1]->ways.begin(), lines.lines[1]->ways.end());
    assert(r.ways.size() >= expected.size() && std::equal(expected.begin(), expected.end(), r.ways.begin()));   // the line, the way round, the line back
    assert((r.served == std::vector<int>{10, 11, 12}));   // its own stops, in their order – not the other one on its way
    assert(std::abs(r.at[1] - 40) < 1.5);   // at the pole the map has for its second stop
    assert((r.shown == std::vector<std::string>{"5 Ostfriedhof", "5 Westbahnhof", "–"}));
    assert(!r.lost);
  }

  // ---- a bus without a line keeps calling at every pole on its way; where no line runs, a bus gets none
  {
    std::unique_ptr<BusTown> t = busTown();
    BusLines lines(t->json);
    t->traffic.setBusLines(&lines);
    const Lane* first = laneOf(t->town, t->json.lines[0].w[0]);
    t->traffic.busStops = [&](const Lane& lane) { return &lane == first ? std::vector<BusStopOnLane>{{50, 20 * 64}} : std::vector<BusStopOnLane>(); };
    TrafficCar* free = t->traffic.addCar(*laneOf(t->town, t->json.lines[0].w[0]), 10, true, std::nullopt);
    assert(!free->bus->line);
    const Ride r = ride(t->traffic, free, t->player, 40);
    assert(!r.served.empty() && r.served[0] == 20);   // the pole of a stop no line of its own calls at
    // where no line runs: a bus as before
    TrafficCar* other = t->traffic.addCar(*laneOf(t->town, way(t->town, 0, 0, 0, 1)), 10, true);
    assert(!other->bus->line);   // no line runs down the first column
  }

  // ---- buses appear with a line where one runs along their lane
  {
    std::unique_ptr<BusTown> t = busTown();
    BusLines lines(t->json);
    t->traffic.setBusLines(&lines);
    t->traffic.busStops = [](const Lane&) { return std::vector<BusStopOnLane>(); };
    t->traffic.busShare = 1;
    t->traffic.targetCount = 8;
    int checked = 0;
    std::set<int64_t> seen;
    for (double s = 0; s < 20; s += DT) {
      t->traffic.update(DT, t->player);
      for (const TrafficCar* c : t->traffic.cars) {
        if (seen.count(c->id) || !c->bus || !c->lane) continue;
        seen.insert(c->id);
        std::vector<BusLines::At> here;
        for (const BusLines::At& h : lines.at(wayOf(*c->lane)))
          if (h.k < h.line->end) here.push_back(h);
        assert(c->bus->line.has_value() == !here.empty());   // a line where one runs, none elsewhere
        if (c->bus->line) {
          bool found = false;
          for (const BusLines::At& h : here) found = found || (h.line->index == c->bus->line->index && h.k == c->bus->line->k);
          assert(found);
        }
        checked++;
      }
    }
    assert(checked > 0);   // buses came
  }

  // ---- where its line turns off and only the other lane leads there, the bus moves over to it in time
  {
    // the middle row has two lanes each way; the line turns left (north) at the third column – from the left lane only
    TownOptions o;
    o.wide = {{0, 1, 1, 1}, {1, 1, 2, 1}, {2, 1, 3, 1}};
    BusTown t(grid(o));
    const std::vector<int32_t> ways = {way(t.town, 0, 1, 1, 1), way(t.town, 1, 1, 2, 1), way(t.town, 2, 1, 2, 0), way(t.town, 2, 0, 3, 0)};
    for (const Connector* c : laneOf(t.town, ways[1], 0)->out) assert(wayOf(*c->to) != ways[2]);   // no left turn from the right lane
    BusLinesJson j;
    j.v = 1;
    j.lines = {lineJson("9", "Nordpark", ways, {})};
    BusLines lines(j);
    t.traffic.setBusLines(&lines);
    TrafficCar* bus = t.traffic.addCar(*laneOf(t.town, ways[0], 0), 10, true);
    const Ride r = ride(t.traffic, bus, t.player, 120);
    assert(r.ways.size() >= ways.size() && std::equal(ways.begin(), ways.end(), r.ways.begin()));
    assert(r.slid);   // it moved over (and slid across)
    assert(!r.lost);   // it never lost its way
  }

  // ---- where the map forbids the turn of its line, the bus finds the shortest way back onto it and calls at its stops on from there
  {
    // the line turns right at (2, 1) – which the map forbids – then west along the bottom row
    TownOptions o;
    o.bans = {{1, 1, 2, 1, 2, 2}};
    BusTown t(grid(o));
    const Town& town = t.town;
    const std::vector<int32_t> ways = {way(town, 0, 1, 1, 1), way(town, 1, 1, 2, 1), way(town, 2, 1, 2, 2), way(town, 2, 2, 1, 2), way(town, 1, 2, 0, 2)};
    for (const Connector* c : laneOf(town, ways[1])->out) assert(wayOf(*c->to) != ways[2]);   // the turn is not there
    BusLinesJson j;
    j.v = 1;
    j.places = 20;
    j.lines = {lineJson("3", "Südring", ways, {{13, 3, 60}})};
    BusLines lines(j);
    t.traffic.setBusLines(&lines);
    TrafficCar* bus = t.traffic.addCar(*laneOf(town, ways[0]), 10, true);
    const Ride r = ride(t.traffic, bus, t.player, 200);
    assert(r.lost);   // it lost its way at the forbidden turn
    // round the block: on east, south at the last column, west along the bottom row – onto its line again
    const std::vector<int> round = {ways[0], ways[1], way(town, 2, 1, 3, 1), way(town, 3, 1, 3, 2), way(town, 3, 2, 2, 2), ways[3]};
    assert(r.ways.size() >= 6 && std::equal(round.begin(), round.end(), r.ways.begin()));
    assert((r.served == std::vector<int>{13}));   // its stop after the way round
    assert(r.shown[0] == "3 Südring");
  }

  std::printf("buslines: all checks passed\n");
  return 0;
}

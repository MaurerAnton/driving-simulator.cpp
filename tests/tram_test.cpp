// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Driving a tram: power and brake, the stop with its doors and passengers,
 * the way at a switch as the indicator asks, and what the instructor says.
 *
 * Origin: tests/game/tram.test.ts @ v1.7.7
 */
// LINK: sim world core platform geo
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "sim/rail.h"
#include "sim/signals.h"
#include "sim/tramdrive.h"
#include "world/world.h"

using namespace fs;

namespace {

constexpr double DT = 1.0 / 30;

/** A straight track east for 2 km (a stop at 300 m), forking at its end: one branch to the north-east, one to the south-east. */
constexpr double END = 2000;

std::vector<double> line(const std::vector<std::pair<double, double>>& pts) {
  std::vector<double> out;
  for (const auto& [x, z] : pts) out.insert(out.end(), {x, 100, z});
  return out;
}

RailJson piece(int k, std::vector<double> p, std::vector<int> a, std::vector<int> b, std::vector<double> st = {}) {
  RailJson r;
  r.k = k;
  r.p = std::move(p);
  r.a = std::move(a);
  r.b = std::move(b);
  r.st = std::move(st);
  return r;
}

std::vector<RailJson> network() {
  std::vector<double> main;
  for (double x = 0; x <= END; x += 10) main.insert(main.end(), {x, 100, 0});
  return {
      piece(RAIL_TRAM, main, {}, {1 * 2, 2 * 2}, {300}),
      // (z grows to the south: the north-east branch bends to the left of a tram running east)
      piece(RAIL_TRAM, line({{END, 0}, {END + 30, -3}, {END + 60, -12}, {END + 100, -30}}), {0 * 2 + 1, 2 * 2}, {}),
      piece(RAIL_TRAM, line({{END, 0}, {END + 30, 3}, {END + 60, 12}, {END + 100, 30}}), {0 * 2 + 1, 1 * 2}, {}),
  };
}

WorldJson emptyWorld() {
  WorldJson json;
  json.version = 1;
  json.meta.name = "Test";
  json.meta.minX = -100;
  json.meta.maxX = 800;
  json.meta.minZ = -100;
  json.meta.maxZ = 100;
  json.meta.tileSize = 500;
  return json;
}

double half() { return 0.5; }

struct Setup {
  std::unique_ptr<World> world;
  std::unique_ptr<SignalSystem> signals;
  std::unique_ptr<RailTraffic> rail;
  std::unique_ptr<TramDriver> tram;
};

Setup setup(std::vector<RailJson> rails = network(), int path = 0, double s = 60, const TramSet& set = "single") {
  Setup u;
  u.world = std::make_unique<World>(emptyWorld());
  u.signals = std::make_unique<SignalSystem>(Sparse<SignalControllerJson>());
  u.rail = std::make_unique<RailTraffic>(rails, half);
  u.rail->target = 0;
  RailVehicle* v = u.rail->spawnAt(path, s, 1, 0, set);
  assert(v);
  u.tram = std::make_unique<TramDriver>(*u.rail, *v, *u.world, *u.signals, half);
  return u;
}

std::vector<TramEvent>& run(TramDriver& tram, double seconds, std::vector<TramEvent>& events, const std::function<void()>& each = nullptr) {
  for (double t = 0; t < seconds; t += DT) {
    if (each) each();
    tram.step(DT, events);
  }
  return events;
}

void run(TramDriver& tram, double seconds) {
  std::vector<TramEvent> ev;
  run(tram, seconds, ev);
}

std::string faults(const std::vector<TramEvent>& ev) {
  std::string out;
  for (const TramEvent& e : ev) out += (out.empty() ? "" : ",") + e.fault;
  return out;
}

}  // namespace

int main() {
  {
    Setup u = setup();
    assert(tramStart(*u.rail, 0, 0, {RAIL_TRAM}));   // a stop to start at
  }

  // ---- the controller: full power pulls away, the speed stays under 70 km/h, the brake stops it
  {
    Setup u = setup();
    TramDriver& tram = *u.tram;
    tram.lever = 1;
    run(tram, 5);
    assert(tram.speed() > 4 && tram.speed() < 6.5);   // about 1.2 m/s²
    run(tram, 40);
    assert(tram.speed() <= TRAM.vmax + 0.01);   // not faster than 70 km/h
    tram.lever = -1;
    const double v0 = tram.speed();
    run(tram, 2);
    assert(v0 - tram.speed() > 2.8);   // the service brake
    tram.lever = 0;
    tram.emergency = true;
    const double v1 = tram.speed();
    run(tram, 1);
    assert(v1 - tram.speed() > 2.5);   // the emergency brake is stronger
  }

  // ---- at the stop: stand at the mark, open the doors, passengers get off and on, close the doors – a stop served
  {
    Setup u = setup();
    TramDriver& tram = *u.tram;
    std::vector<TramEvent> events;
    // drive up to the stop at 300 m and stop with the front at its mark
    bool done = false;
    run(tram, 90, events, [&] {
      // aim at a speed that comes down to nothing at the mark
      const double d = tram.ahead.stop;
      if (done || d < 0.6) {
        done = true;
        tram.lever = -1;
        return;
      }
      const double want = std::min(10.0, std::sqrt(2 * 0.9 * std::max(d - 0.5, 0.0)));
      tram.lever = std::max(-1.0, std::min(1.0, (want - tram.speed()) * 1.5));
    });
    assert(tram.speed() < 0.05);   // standing
    assert(std::abs(tram.v.s - 300) < 5);   // at the mark
    tram.lever = 0;
    tram.toggleDoors(events);
    assert(tram.doorsOpen);
    assert(tram.standingAtStop());
    assert(tram.passengers() > 0);   // people get off and on
    // power with the doors open: held
    tram.lever = 1;
    run(tram, 1, events);
    assert(tram.speed() < 0.01);   // the doors hold the tram
    tram.lever = 0;
    run(tram, 30, events);
    assert(tram.passengers() == 0);   // everybody through
    tram.toggleDoors(events);
    assert(tram.served == 1);
    assert(events.empty());   // nothing to say
    // and on to the next: the stop is not "missed" afterwards
    tram.lever = 1;
    run(tram, 15, events);
    if (!events.empty()) std::printf("said: %s\n", faults(events).c_str());
    assert(events.empty());
  }

  // ---- closing the doors while passengers still get on, and driving past a stop, are faults
  {
    Setup a = setup();
    std::vector<TramEvent> ev;
    a.tram->v.s = 297;
    a.rail->place(a.tram->v);
    a.tram->toggleDoors(ev);
    run(*a.tram, 1, ev);
    a.tram->toggleDoors(ev);
    assert(faults(ev) == "tramDoorsEarly");

    Setup b = setup();
    std::vector<TramEvent> ev2;
    b.tram->lever = 1;
    run(*b.tram, 40, ev2);
    assert(b.tram->v.s > 330 || b.tram->v.path != 0);   // past the stop
    bool missed = false;
    for (const TramEvent& e : ev2) missed = missed || e.fault == "tramStopMissed";
    assert(missed);
  }

  // ---- at the switch the tram takes the way the indicator asks for
  for (const auto& [wish, side] : std::vector<std::pair<int, int>>{{1, -1}, {-1, 1}}) {
    Setup u = setup();
    TramDriver& tram = *u.tram;
    tram.v.s = END - 40;
    u.rail->place(tram.v);
    tram.wish = wish;
    tram.lever = 0.5;
    run(tram, 25);
    const RailCar& car = tram.v.cars[0];
    assert(car.x > END + 10);   // over the switch
    assert((car.z > 0 ? 1 : car.z < 0 ? -1 : 0) == side);
  }

  // ---- by itself (a demonstration drive): it comes to stand at the stop, lets the people through and drives on – without a fault
  {
    Setup u = setup();
    TramDriver& tram = *u.tram;
    std::vector<TramEvent> events;
    bool stood = false;
    run(tram, 150, events, [&] {
      tram.drive(events);
      if (tram.doorsOpen) stood = true;
    });
    assert(stood);   // the doors were opened at the stop
    assert(tram.served == 1);   // the stop was served
    if (!events.empty()) std::printf("said: %s\n", faults(events).c_str());
    assert(events.empty());   // nothing to say
    assert(tram.v.s > 600 || tram.v.path != 0);   // and on its way again
  }

  // ---- a tram of the traffic runs its line: at the switch the branch its line takes, whichever that is
  for (const auto& [branch, side] : std::vector<std::pair<int, int>>{{1, -1}, {2, 1}}) {
    RailTraffic rail(network(), half);
    rail.target = 0;
    rail.setLines({RailLine{"5", "A", "B", "", 0, {PathDir{0, 1}, PathDir{branch, 1}}}});
    RailVehicle* v = rail.spawnAt(0, END - 80, 1, 8);
    assert(v);
    assert(rail.assignLine(*v, "5"));   // it runs line 5
    // (somebody standing a little way off: near enough that the tram is kept, not in its way)
    const RailPlayer player{END, 100, 120, 0, 4, 2};
    for (double t = 0; t < 30; t += DT) rail.update(DT, player);
    assert(v->path == branch);   // onto the line's branch
    assert((v->cars[0].z > 0 ? 1 : -1) == side);
  }

  // ---- the pantograph: down, the controller does nothing; raised it takes a few seconds; lowered while pulling is a fault
  {
    Setup u = setup();
    TramDriver& tram = *u.tram;
    tram.panto = 0;
    tram.pantoUp = false;
    tram.lever = 1;
    run(tram, 3);
    assert(tram.speed() == 0);   // no power with the pantograph down
    std::vector<TramEvent> events;
    tram.togglePanto(events);
    run(tram, PANTO_UP_TIME * 0.5);
    assert(tram.speed() == 0);   // still rising
    run(tram, PANTO_UP_TIME * 0.5 + 0.2);
    run(tram, 3);
    assert(tram.speed() > 2);   // against the wire it pulls
    assert(tram.v.panto > 0.99);   // drawn raised
    tram.togglePanto(events);
    assert(faults(events) == "tramPantoLoad");   // lowered under load
    const double v0 = tram.speed();
    run(tram, PANTO_DOWN_TIME + 0.5);
    assert(tram.speed() < v0);   // only rolling now
    assert(tram.v.panto < 0.01);   // drawn lowered
  }

  // ---- where the wire ends: a tram with its pantograph up is told and it comes down; the regional tram goes on with its engine
  {
    std::vector<double> main;
    for (double x = 0; x <= 400; x += 10) main.insert(main.end(), {x, 100, 0});
    std::vector<double> rest;
    for (double x = 400; x <= 1400; x += 10) rest.insert(rest.end(), {x, 100, 0});
    // (a regional line: wire on the first 400 m, none after them)
    RailJson a = piece(RAIL_LIGHT, main, {}, {1 * 2});
    a.e = 1;
    RailJson b = piece(RAIL_LIGHT, rest, {0 * 2 + 1}, {});
    b.e = 0;
    Setup u = setup({a, b}, 0, 120);
    TramDriver& tram = *u.tram;
    assert(tram.hybrid());
    tram.lever = 1;
    std::vector<TramEvent> events;
    double warned = INFINITY;
    run(tram, 40, events, [&] {
      if (tram.ahead.wireEndIn < INFINITY) warned = std::min(warned, tram.ahead.wireEndIn);
    });
    assert(warned < 300);   // the end of the wire is seen ahead
    bool told = false;
    for (const TramEvent& e : events) told = told || e.fault == "tramPantoWireEnd";
    assert(told);   // run under the end of the wire with the pantograph up
    assert(!tram.pantoUp);   // lowered
    const double at = tram.speed();
    run(tram, 5);
    assert(tram.powered() && tram.speed() >= at - 0.05);   // on its engine
  }

  // ---- two units coupled pull like one; with a trailer car the tram pulls away more slowly – and is laid out with its cabs where they belong
  {
    const auto pull = [](const TramSet& set) {
      Setup u = setup(network(), 0, 200, set);
      u.tram->lever = 1;
      run(*u.tram, 5);
      return u.tram->speed();
    };
    const double single = pull("single");
    assert(std::abs(pull("double") - single) < 0.05);   // double traction
    assert(pull("trailer") < single * 0.85);   // with a trailer car
    const std::vector<CarRole>& roles = carLayout(*consistOf(RAIL_TRAM, "double"));
    std::string shape;
    for (const CarRole& r : roles) shape += std::string(r.front ? "F" : "-") + (r.back ? "B" : "-") + (r.panto ? "P" : "-") + " ";
    assert(shape == "F-- --P -B- F-- --P -B- ");
    assert(roles[0].lampsFront && !roles[2].lampsBack && !roles[3].lampsFront && roles[5].lampsBack);   // lit only at the ends
    const std::vector<CarRole>& trailer = carLayout(*consistOf(RAIL_TRAM, "trailer"));
    assert(trailer[3].trailer && !trailer[3].panto && trailer[3].lampsBack && !trailer[2].lampsBack);
    assert(consistLength(*consistOf(RAIL_TRAM, "double")) > 2 * consistLength(*consistOf(RAIL_TRAM, "single")));
  }

  // ---- by the timetable: a trip is put on its line where the clock has it, once, and stands at a stop until its time
  {
    std::vector<double> main;
    for (double x = 0; x <= 2000; x += 10) main.insert(main.end(), {x, 100, 0});
    RailTraffic rail({piece(RAIL_TRAM, main, {}, {}, {300, 900})}, half);
    rail.setLines({RailLine{"1", "A", "B", "", 0, {PathDir{0, 1}}}});
    const std::optional<LineSpot> h = rail.lineSpot(0, PathMark{0, 300}, PathMark{0, 900}, 0.5);
    assert(h && h->path == 0 && h->s == 600 && h->dir == 1 && h->k == 0);
    assert(!rail.lineSpot(0, PathMark{0, 900}, PathMark{0, 300}, 0.5));   // not backwards along the line
    struct Sch : RailSchedule {
      int asked = 0;
      std::vector<RailDue> due(double, double, double, double, bool) override {
        RailDue d;
        d.trip = "T1";
        d.line = 0;
        d.path = 0;
        d.s = 250;
        d.dir = 1;
        d.k = 0;
        d.speed = 6;
        return {d};
      }
      double wait(const RailVehicle&, int, double) override {
        asked++;
        return 40;
      }
    } sch;
    rail.schedule = &sch;
    rail.target = 4;
    const RailPlayer player{0, 100, 300, 0, 4, 2};
    rail.update(0.1, player);
    assert(rail.vehicles.size() == 1);
    const RailVehicle& v = *rail.vehicles[0];
    assert(v.trip == "T1");
    assert(v.line && v.line->index == 0);
    // it runs to the stop at 300 m and stands there until its time – and the same trip is not put on twice
    for (double t = 0; t < 30; t += 0.1) rail.update(0.1, player);
    assert(rail.vehicles.size() == 1);   // one tram for the trip
    assert(sch.asked > 0 && v.dwell > 20);   // waiting for its time
  }

  std::printf("tram: all checks passed\n");
  return 0;
}

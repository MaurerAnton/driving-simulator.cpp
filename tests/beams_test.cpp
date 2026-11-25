// SPDX-License-Identifier: AGPL-3.0-only
// Origin: tests/sim/beams.test.ts @ v1.7.7
/**
 * The main beam and the rules (§ 17 StVO): dipped in time for a vehicle
 * coming towards one (the tip far off, the fault once it is dazzled), when
 * following closely, for cyclists, not at all on lit streets in town; the
 * lights on in the dark.  And the traffic's own main beam: dipped for the
 * player, flashed back at a player who dazzles.
 */
// LINK: sim core
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "sim/beams.h"

using namespace fs;

namespace {

/** The player at the origin facing north (−z) at 25 m/s with the main beam on, at night on a country road. */
BeamScene scene() {
  BeamScene s;
  s.x = 0;
  s.z = 0;
  s.yaw = 0;
  s.speed = 25;
  s.low = true;
  s.high = true;
  s.dark = true;
  s.lit = false;
  return s;
}
/** a car `d` m ahead on the oncoming lane (3.5 m to the left), facing south */
BeamUser oncomingAt(double d, int64_t id = 1) { return {id, -3.5, -d, M_PI}; }
/** a car `d` m ahead in our lane, going our way */
BeamUser aheadAt(double d, int64_t id = 2) { return {id, 0, -d, 0}; }

/** runs the watch for `seconds` in steps of 0.1 s; every fault found */
std::vector<BeamFault> run(BeamWatch& w, const BeamScene& s, double seconds) {
  std::vector<BeamFault> all;
  for (double t = 0; t < seconds - 1e-9; t += 0.1)
    for (const BeamFault& f : w.update(0.1, s)) all.push_back(f);
  return all;
}

bool some(const std::vector<BeamFault>& fs_, const std::string& id, const std::string& severity = "") {
  for (const BeamFault& f : fs_)
    if (f.id == id && (severity.empty() || f.severity == severity)) return true;
  return false;
}

}  // namespace

int main() {
  {
    // relate: ahead, to the left, coming towards one
    const Relation r = relate(0, 0, 0, oncomingAt(100));
    assert(std::abs(r.along - 100) < 1e-9);
    assert(std::abs(r.side - 3.5) < 1e-9);
    assert(r.facing < -0.99);
    const Relation e = relate(0, 0, -M_PI / 2, {0, 50, 0, M_PI / 2});
    assert(std::abs(e.along - 50) < 1e-9 && std::abs(e.side) < 1e-9 && e.facing < -0.99);
  }
  {
    // darkness: none by day, full at night
    assert(darkness(30) == 0);
    assert(darkness(-10) == 1);
    assert(darkness(1) > 0 && darkness(1) < 1);
  }
  {
    // oncoming far off: the tip only, no fault
    BeamWatch w;
    BeamScene s = scene();
    s.cars = {oncomingAt(350)};
    assert(run(w, s, 5).empty());
    assert(w.tip == "beam.tipOncoming");
    assert(w.dazzling == -1);
  }
  {
    // oncoming within 200 m with the main beam on: dazzled – a fault after the reaction time
    BeamWatch w;
    BeamScene s = scene();
    s.cars = {oncomingAt(180)};
    assert(run(w, s, 0.5).empty());
    assert(some(run(w, s, 1), "dazzleOncoming"));
    assert(w.dazzling == 1);
  }
  {
    // oncoming within 200 m, dipped: nothing
    BeamWatch w;
    BeamScene s = scene();
    s.high = false;
    s.cars = {oncomingAt(150)};
    assert(run(w, s, 3).empty());
    assert(w.tip.empty());
  }
  {
    // oncoming behind a hill crest (out of sight): not dazzled
    BeamWatch w;
    BeamScene s = scene();
    s.cars = {oncomingAt(150)};
    s.sees = [](double, double, double, double) { return false; };
    assert(run(w, s, 3).empty());
  }
  {
    // a car going away, or one far to the side: not oncoming
    BeamWatch w;
    BeamScene s = scene();
    s.cars = {{3, -3.5, -150, 0}, {4, -120, -150, M_PI}, {5, -3.5, 150, M_PI}};
    assert(!some(run(w, s, 3), "dazzleOncoming"));
  }
  {
    // following closely with the main beam: tip at 150 m, fault after a while within 80 m
    BeamWatch w;
    BeamScene s = scene();
    s.cars = {aheadAt(120)};
    assert(run(w, s, 5).empty());
    assert(w.tip == "beam.tipAhead");
    BeamWatch w2;
    s.cars = {aheadAt(60)};
    assert(run(w2, s, 2).empty());
    assert(some(run(w2, s, 2), "dazzleAhead"));
  }
  {
    // a cyclist coming towards one: dip for him too
    BeamWatch w;
    BeamScene s = scene();
    s.riders = {{9, -1.5, -80, M_PI}};
    assert(some(run(w, s, 2), "dazzleRider"));
  }
  {
    // main beam on a lit street in town: a hint after a few seconds
    BeamWatch w;
    BeamScene s = scene();
    s.lit = true;
    s.speed = 13;
    assert(run(w, s, 3).empty());
    assert(w.tip == "beam.tipLit");
    assert(some(run(w, s, 3), "beamLit", "hint"));
  }
  {
    // by day the main beam counts for nothing
    BeamWatch w;
    BeamScene s = scene();
    s.dark = false;
    s.lit = true;
    s.cars = {oncomingAt(100), aheadAt(40)};
    assert(run(w, s, 5).empty());
    assert(w.tip.empty());
  }
  {
    // no lights in the dark: tip at once, fault while driving on
    BeamWatch w;
    BeamScene s = scene();
    s.low = false;
    s.high = false;
    assert(run(w, s, 2).empty());
    assert(w.tip == "beam.tipLightsOn");
    assert(some(run(w, s, 3), "noLights", "major"));
    // standing still (parked) is not driving; in fog by day the lights are required too
    BeamWatch a;
    BeamScene p = s;
    p.speed = 0;
    assert(run(a, p, 6).empty());
    BeamWatch b;
    BeamScene f = s;
    f.dark = false;
    f.poorSight = true;
    assert(some(run(b, f, 6), "noLights"));
  }
  {
    // the assist: main beam on the empty country road, off for oncoming traffic and in town
    BeamWatch w;
    BeamScene s = scene();
    s.high = false;
    w.update(0.1, s);
    assert(w.assistHigh == true);
    BeamScene o = s;
    o.cars = {oncomingAt(380)};
    w.update(0.1, o);
    assert(w.assistHigh == false);
    BeamScene l = s;
    l.lit = true;
    w.update(0.1, l);
    assert(w.assistHigh == false);
    BeamScene slow = s;
    slow.speed = 5;
    w.update(0.1, slow);
    assert(w.assistHigh == false);
  }
  {
    // the traffic: main beam on the empty road, dipped in time for the player, flashed back when dazzled
    TrafficBeams tb;
    const int64_t id = 10;
    assert(TrafficBeams::dipDistance(id) >= BEAM.ONCOMING_FAULT + 100);
    TrafficBeamCar car;
    car.id = id;
    car.x = 0;
    car.z = -1000;
    car.yaw = M_PI;
    car.v = 25;
    car.rural = true;
    const auto step = [&](const BeamPlayer* player, int64_t dazzling = -1, double secs = 3) {
      for (double t = 0; t < secs; t += 0.1) tb.update(0.1, {car}, player, dazzling, true);
    };
    step(nullptr);
    assert(tb.high(id) == true);
    BeamPlayer coming;
    coming.id = -1;
    coming.x = -3.5;
    coming.z = -700;
    coming.yaw = 0;
    coming.high = false;
    step(&coming);
    assert(tb.high(id) == false);
    assert(tb.glare == 0);
    BeamPlayer p = coming;
    p.z = -850;
    p.high = true;
    step(&p, id, 0.5);
    assert(tb.flashing(id) == false);
    step(&p, id, 0.7);
    assert(tb.flashing(id) == true);
    assert(tb.high(id) == true);
    assert(tb.glare > 0);
    // in town, or by day: never
    TrafficBeams town;
    TrafficBeamCar townCar = car;
    townCar.rural = false;
    for (double t = 0; t < 3; t += 0.1) town.update(0.1, {townCar}, nullptr, -1, true);
    assert(town.high(id) == false);
    TrafficBeams day;
    for (double t = 0; t < 3; t += 0.1) day.update(0.1, {car}, nullptr, -1, false);
    assert(day.high(id) == false);
  }
  std::printf("beams: ok\n");
  return 0;
}

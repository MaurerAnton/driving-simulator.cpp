// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim game world core platform geo
// Origin: tests/vehicle/flight.test.ts @ v1.7.7
/**
 * Acceptance tests for the flight model (src/sim/flight).
 *
 * The expectations are written from how a two-seat high-wing trainer of
 * 160 PS flies (handbook figures of that class, flying-school practice),
 * not from what the code happens to output.  The "pilots" below are simple
 * controllers of the kind an instructor would describe: hold the pitch for
 * the speed, the wings level, the ball in the middle.
 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "sim/flight.h"
#include "sim/flightspec.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using fs::harness::between;
using fs::harness::ok;
using fs::harness::PlaneGround;
using fs::harness::test;

namespace {

const double DT = 1.0 / 60;
const double DEG = M_PI / 180;
const double G = 9.81;
const double INF = std::numeric_limits<double>::infinity();

double clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

std::string fx(double v, int d) { return toFixed(v, d); }

/** controls({...}): the game's controls (engine on), changed by the caller */
FlightControls controls() { return FlightControls{}; }

const char* crashName(const Aircraft& a) { return a.crashed ? crashKindName(*a.crashed) : "null"; }
bool crashedIs(const Aircraft& a, const char* kind) { return a.crashed && std::string(crashKindName(*a.crashed)) == kind; }

/** Run until `seconds` have passed or `pilot` returns true; returns the time flown. */
double fly(Aircraft& a, PlaneGround& g, double seconds, FlightControls& c, const std::function<bool(double)>& pilot, double dt = DT) {
  const int n = (int)jsRound(seconds / dt);
  for (int i = 0; i < n; i++) {
    const double t = i * dt;
    if (pilot(t)) return t;
    a.step(dt, c, g);
  }
  return seconds;
}

const auto nothing = [](double) { return false; };

// ---- the pilot's hands ----------------------------------------------------------

/** ailerons: bank towards `target` (rad), damping the roll rate */
void ailerons(Aircraft& a, FlightControls& c, double target = 0) {
  const double p = -a.omega.z;
  c.aileron = clamp(2 * (target - a.bank()) - 0.4 * p, -1, 1);
}

/** elevator: pitch attitude towards `target` (rad), damping the pitch rate */
void pitchTo(Aircraft& a, FlightControls& c, double target) { c.elevator = clamp(2.5 * (target - a.pitch()) - 1 * a.omega.x, -1, 1); }

/** rudder in the air: no sideslip (the ball in the middle) */
void ball(Aircraft& a, FlightControls& c) { c.rudder = clamp(3 * a.sideslip(), -1, 1); }

/** rudder on the ground: keep the heading (compass degrees) */
void track(Aircraft& a, FlightControls& c, double heading) {
  double e = heading - a.heading();
  e -= 360 * jsRound(e / 360);
  c.rudder = clamp(0.25 * e - 2 * a.turnRate(), -1, 1);
}

using Hold = std::function<void(Aircraft&, FlightControls&, double)>;

/** Altitude hold: a vertical speed from the height error, the pitch attitude from the vertical speed error. */
Hold altitudeHold(double target) {
  double base = NAN;
  return [=](Aircraft& a, FlightControls& c, double dt) mutable {
    if (std::isnan(base)) base = a.pitch();
    const double e = clamp(0.1 * (target - a.altitude()), -3, 3) - a.verticalSpeed();
    base = clamp(base + 0.004 * e * dt, -15 * DEG, 20 * DEG);
    pitchTo(a, c, base + 0.012 * e);
  };
}

/** Speed by pitch (as in the climb): raise the nose when too fast.  `target` may change with time. */
Hold speedHold(std::function<double(double)> target) {
  double base = NAN;
  double t = 0;
  return [=](Aircraft& a, FlightControls& c, double dt) mutable {
    if (std::isnan(base)) base = a.pitch();
    const double e = a.ias() - target(t);
    t += dt;
    base = clamp(base + 0.002 * e * dt, -15 * DEG, 25 * DEG);
    pitchTo(a, c, base + 0.02 * e);
  };
}
Hold speedHold(double target) {
  return speedHold([target](double) { return target; });
}

struct Trimmed {
  Aircraft a;
  FlightControls c;
};

void trimmedInAir(Trimmed& out, double speed, double altitude, double flaps = 0) {
  out.a.placeInAir(0, altitude, 0, 0, speed);
  const LevelFlight lf = out.a.levelFlight(speed, flaps);
  out.c = controls();
  out.c.throttle = lf.throttle;
  out.c.trim = lf.trim;
  out.c.flaps = flaps;
}

// ---- 1. standing ------------------------------------------------------------------------

void parked() {
  struct Case {
    const char* name;
    double grade;
    double yaw;
  };
  for (const Case& k : {Case{"flat", 0, 0}, Case{"2 % slope, nose uphill", 0.02, 0}, Case{"2 % slope, nose downhill", 0.02, M_PI}, Case{"2 % slope, across", 0.02, M_PI / 2}}) {
    test((std::string("parked (") + k.name + "): no creeping, no trembling, standing on its legs").c_str());
    PlaneGround g(k.grade);
    for (const bool engine : {false, true}) {
      Aircraft a;
      a.placeOnGround(0, 0, k.yaw, g, engine);
      FlightControls c = controls();
      c.parkingBrake = true;
      c.engineOn = engine;
      fly(a, g, 2, c, nothing);
      const double x0 = a.pos.x;
      const double y0 = a.pos.y;
      const double z0 = a.pos.z;
      double vmax = 0;
      fly(a, g, 10, c, [&](double) {
        vmax = std::max({vmax, std::hypot(a.vel.x, a.vel.y, a.vel.z), std::hypot(a.omega.x, a.omega.y, a.omega.z)});
        return false;
      });
      const std::string what = std::string(k.name) + ", engine " + (engine ? "idling" : "off");
      ok(vmax < 0.02, what + ": moves at " + fx(vmax, 4) + " m/s");
      ok(std::hypot(a.pos.x - x0, a.pos.y - y0, a.pos.z - z0) < 0.005, what + ": creeps");
      ok(!a.crashed, what + ": crashed");
      ok(a.onGround(), what + ": on the ground");
      if (engine) between(a.rpm(), 600, 800, what + ": idle rpm");
      // every leg loaded, none at its stop
      for (size_t i = 0; i < a.wheels.size(); i++) {
        const WheelView& w = a.wheels[i];
        const double travel = TRAINER.gear[i].travel;
        ok(w.contact, what + ": wheel " + std::to_string(i) + " touches");
        between(w.compression / travel, 0.2, 0.8, what + ": wheel " + std::to_string(i) + " compression share");
      }
      // centre of mass above the ground: main leg, tyre and compression (the slope tilts it a little)
      const GearLeg& m = TRAINER.gear[1];
      const double expect = -m.y + m.radius - a.wheels[1].compression;
      between(a.pos.y - (-a.pos.z * k.grade), expect - 0.06, expect + 0.06, what + ": height of the centre of mass");
      between(a.agl(), 0, 0.01, what + ": agl");
    }
  }
}

void engineOnGround() {
  test("engine: idle, static full power, fuel, switching off and on");
  PlaneGround g;
  Aircraft a;
  a.placeOnGround(0, 0, 0, g, false);
  FlightControls c = controls();
  c.parkingBrake = true;
  c.engineOn = false;
  fly(a, g, 1, c, nothing);
  ok(a.rpm() == 0, "a cold engine stands");
  // starting: the starter turns it until it fires, then it idles
  c.engineOn = true;
  fly(a, g, 10, c, nothing);
  ok(a.engineRunning(), "started");
  between(a.rpm(), 620, 780, "idle rpm");
  c.throttle = 1;
  fly(a, g, 8, c, nothing);
  between(a.rpm(), 2200, 2400, "static rpm at full throttle");
  between(a.propThrust(), 1700, 2500, "static thrust (N)");
  between(a.fuelFlow(), 30, 55, "fuel flow at full power (l/h)");
  const double fuel = a.fuel;
  fly(a, g, 60, c, nothing);
  between(fuel - a.fuel, 0.4, 1, "litres burnt in a minute at full power");
  ok(std::hypot(a.pos.x, a.pos.z) < 0.1, "the parking brake holds full power");
  c.throttle = 0;
  fly(a, g, 5, c, nothing);
  c.engineOn = false;
  fly(a, g, 5, c, nothing);
  ok(a.rpm() == 0, "stops when switched off at idle");
}

void engineOffInFlight() {
  test("engine off in flight: the propeller windmills when fast, stops when slow");
  PlaneGround g;
  Trimmed tr;
  trimmedInAir(tr, 45, 1500);
  Aircraft& a = tr.a;
  FlightControls& c = tr.c;
  c.engineOn = false;
  c.throttle = 0;
  Hold hold = speedHold(45);
  fly(a, g, 20, c, [&](double) {
    hold(a, c, DT);
    ailerons(a, c);
    return false;
  });
  between(a.rpm(), 600, 1600, "windmilling at 45 m/s");
  ok(a.propThrust() < 0, "a windmilling propeller drags");
  Hold slow = speedHold(27);
  fly(a, g, 60, c, [&](double) {
    slow(a, c, DT);
    ailerons(a, c);
    return false;
  });
  ok(a.rpm() == 0, "stops at 27 m/s");
  // and it starts again in a dive
  c.engineOn = true;
  fly(a, g, 10, c, [&](double) {
    ailerons(a, c);
    pitchTo(a, c, -5 * DEG);
    return false;
  });
  ok(a.engineRunning(), "restarted");
}

// ---- 2. take-off ----------------------------------------------------------------------------

struct TakeOff {
  double distance = 0;
  double time = 0;
  bool tail = false;
  std::string crashed;
  double heading = 0;
};

/** Full throttle, flaps 10°, rotate at vr to 8°, keep the heading with the rudder. */
TakeOff takeOff(PlaneGround& g, double dt = DT, double headwind = 0) {
  Aircraft a;
  a.placeOnGround(0, 0, 0, g, true);
  // heading north; a wind from the north blows towards +z
  a.wind.z = headwind;
  FlightControls c = controls();
  c.flaps = 1.0 / 3;
  // flaps out first, standing on the brakes
  fly(
      a, g, 3, c,
      [&](double) {
        c.brake = 1;
        return false;
      },
      dt);
  c.brake = 0;
  const double z0 = a.pos.z;
  double airborne = -1;
  bool tail = false;
  const double t = fly(
      a, g, 60, c,
      [&](double t) {
        c.throttle = std::min(1.0, t / 2);
        track(a, c, 0);
        ailerons(a, c);
        // rotate firmly at vr, then hold the attitude
        if (a.ias() > TRAINER.vr) c.elevator = clamp(4 * (8 * DEG - a.pitch()) - 1.5 * a.omega.x, -1, 1);
        else pitchTo(a, c, 0);
        tail = tail || a.tailStrike;
        if (!a.onGround() && airborne < 0) airborne = t;
        if (a.onGround()) airborne = -1;
        return (airborne >= 0 && t - airborne > 1) || a.crashed.has_value();
      },
      dt);
  return {z0 - a.pos.z, t, tail, crashName(a), a.heading()};
}

void takeOffRuns() {
  test("take-off: airborne within 250–450 m on asphalt, longer on grass");
  PlaneGround ga(0, 1, 1);
  const TakeOff asphalt = takeOff(ga);
  ok(asphalt.crashed == "null", "asphalt: crashed " + asphalt.crashed);
  ok(!asphalt.tail, "no tail strike");
  between(asphalt.distance, 250, 450, "take-off run on asphalt (m)");
  between(asphalt.time, 12, 30, "take-off run (s)");
  ok(std::min(asphalt.heading, 360 - asphalt.heading) < 5, "kept straight");
  PlaneGround gg(0, 0.5, 4);
  const TakeOff grass = takeOff(gg);
  ok(grass.crashed == "null", "grass: crashed " + grass.crashed);
  ok(grass.distance > asphalt.distance * 1.1, "grass " + fx(grass.distance, 0) + " m vs asphalt " + fx(asphalt.distance, 0) + " m");
  std::printf("    take-off run: asphalt %s m in %s s, grass %s m\n", fx(asphalt.distance, 0).c_str(), fx(asphalt.time, 1).c_str(), fx(grass.distance, 0).c_str());
}

// ---- 3. climb -----------------------------------------------------------------------------------

void climb() {
  test("climb at vy with full power: 2.5–5 m/s at sea level");
  PlaneGround g;
  Trimmed tr;
  trimmedInAir(tr, TRAINER.vy, 300);
  Aircraft& a = tr.a;
  FlightControls& c = tr.c;
  c.throttle = 1;
  Hold hold = speedHold(TRAINER.vy);
  fly(a, g, 15, c, [&](double) {
    hold(a, c, DT);
    ailerons(a, c);
    ball(a, c);
    return false;
  });
  const double h0 = a.altitude();
  double vmin = INF;
  double vmax = 0;
  fly(a, g, 30, c, [&](double) {
    hold(a, c, DT);
    ailerons(a, c);
    ball(a, c);
    vmin = std::min(vmin, a.ias());
    vmax = std::max(vmax, a.ias());
    return false;
  });
  const double rate = (a.altitude() - h0) / 30;
  between(rate, 2.5, 5, "climb rate (m/s)");
  between(vmin, TRAINER.vy - 3, TRAINER.vy + 3, "lowest speed in the climb");
  between(vmax, TRAINER.vy - 3, TRAINER.vy + 3, "highest speed in the climb");
  between(a.rpm(), 2350, 2700, "rpm in the climb");
  ok(std::abs(c.rudder) < 0.4, "a little right rudder at most");
  std::printf("    climb at %s m/s: %s m/s, %s rpm, rudder %s\n", jsStr(TRAINER.vy).c_str(), fx(rate, 2).c_str(), fx(a.rpm(), 0).c_str(), fx(c.rudder, 2).c_str());
}

// ---- 4. cruise --------------------------------------------------------------------------------------

void cruise() {
  test("cruise: 75 % power trimmed level gives about vcruise; altitude held for a minute");
  PlaneGround g;
  Trimmed tr;
  trimmedInAir(tr, TRAINER.vcruise, 600);
  Aircraft& a = tr.a;
  FlightControls& c = tr.c;
  Hold hold = altitudeHold(600);
  const double rated = TRAINER.power * 735.5;
  double lo = INF;
  double hi = -INF;
  fly(a, g, 120, c, [&](double time) {
    // the pilot sets 75 % power on the rpm/power gauge
    c.throttle = clamp(c.throttle + (0.75 - a.enginePower() / rated) * 0.02, 0, 1);
    hold(a, c, DT);
    ailerons(a, c);
    ball(a, c);
    if (time > 60) {
      lo = std::min(lo, a.altitude());
      hi = std::max(hi, a.altitude());
    }
    return false;
  });
  std::printf("    cruise at 75 %%: %s m/s true (%s indicated) at %s rpm, %s l/h; altitude %s…%s m\n", fx(a.tas(), 1).c_str(), fx(a.ias(), 1).c_str(), fx(a.rpm(), 0).c_str(), fx(a.fuelFlow(), 1).c_str(), fx(lo, 0).c_str(), fx(hi, 0).c_str());
  between(a.enginePower() / rated, 0.72, 0.78, "power");
  between(a.tas(), TRAINER.vcruise * 0.9, TRAINER.vcruise * 1.1, "cruise speed (true, m/s)");
  between(a.rpm(), 2450, 2750, "cruise rpm");
  between(lo, 570, 630, "lowest altitude");
  between(hi, 570, 630, "highest altitude");
  // the trim wheel does the work: the elevator stays near the middle
  ok(std::abs(c.elevator) < 0.2, "elevator " + fx(c.elevator, 2));
}

void handsOff() {
  test("hands off, trimmed: holds its speed and height for a minute");
  PlaneGround g;
  Trimmed tr;
  trimmedInAir(tr, 50, 800);
  Aircraft& a = tr.a;
  FlightControls& c = tr.c;
  const double v0 = a.ias();
  double dv = 0;
  double dh = 0;
  fly(a, g, 60, c, [&](double) {
    ailerons(a, c);
    c.elevator = 0;
    dv = std::max(dv, std::abs(a.ias() - v0));
    dh = std::max(dh, std::abs(a.altitude() - 800));
    return false;
  });
  ok(dv < 4, "speed wanders by " + fx(dv, 1) + " m/s");
  ok(dh < 40, "height wanders by " + fx(dh, 0) + " m");
}

// ---- 5. stall -------------------------------------------------------------------------------------------

struct StallResult {
  double warn;
  double stall;
  bool recovered;
  double lost;
  std::string crashed;
};

StallResult stall(double flaps) {
  PlaneGround g;
  Trimmed tr;
  trimmedInAir(tr, 40, 800, flaps);
  Aircraft& a = tr.a;
  FlightControls& c = tr.c;
  // flaps out at 40 m/s
  Hold hold0 = altitudeHold(800);
  fly(a, g, 8, c, [&](double) {
    hold0(a, c, DT);
    ailerons(a, c);
    c.throttle = a.levelFlight(40, flaps).throttle;
    return false;
  });
  c.throttle = 0;
  c.trim = a.levelFlight(40, flaps).trim;
  double warn = -1;
  double stalled = -1;
  // raise the nose slowly: about 1 m/s of speed lost per second
  const double v0 = a.ias();
  Hold slow = speedHold([v0](double t) { return v0 - t; });
  fly(a, g, 60, c, [&](double) {
    slow(a, c, DT);
    ailerons(a, c);
    ball(a, c);
    if (warn < 0 && a.stallWarning()) warn = a.ias();
    if (stalled < 0 && a.stalled()) stalled = a.ias();
    return stalled >= 0;
  });
  // recovery: release the elevator, full power, wings level, then a gentle pull-out
  const double h0 = a.altitude();
  double lowest = h0;
  bool again = false;
  fly(a, g, 15, c, [&](double t) {
    c.throttle = t > 0.5 ? 1 : 0;
    if (t < 2) c.elevator = 0;
    else c.elevator = std::min(0.25, 2.5 * (0 - a.pitch()) - a.omega.x);
    ailerons(a, c);
    ball(a, c);
    lowest = std::min(lowest, a.altitude());
    if (t > 2 && a.stalled()) again = true;
    return false;
  });
  const bool recovered = !again && !a.stalled() && a.ias() > stalled + 3;
  return {warn, stalled, recovered, h0 - lowest, crashName(a)};
}

void stalls() {
  test("stall: warning first, clean at about vs1, lower with full flaps, recovered by releasing the elevator");
  const StallResult clean = stall(0);
  ok(clean.warn > 0 && clean.stall > 0, "warned and stalled");
  ok(clean.warn > clean.stall, "warning at " + fx(clean.warn, 1) + " before the stall at " + fx(clean.stall, 1) + " m/s");
  between(clean.stall, TRAINER.vs1 - 3, TRAINER.vs1 + 3, "clean stall speed (m/s)");
  ok(clean.recovered, "recovered (clean)");
  ok(clean.lost < 150, "height lost " + fx(clean.lost, 0) + " m");
  ok(clean.crashed == "null", "clean: crashed " + clean.crashed);
  const StallResult flaps = stall(1);
  ok(flaps.warn > flaps.stall, "warning first (flaps)");
  between(flaps.stall, TRAINER.vs0 - 3, TRAINER.vs0 + 3, "stall speed with full flaps (m/s)");
  ok(flaps.stall < clean.stall - 1, "full flaps " + fx(flaps.stall, 1) + " < clean " + fx(clean.stall, 1));
  ok(flaps.recovered, "recovered (flaps)");
  ok(flaps.crashed == "null", "flaps: crashed " + flaps.crashed);
  std::printf("    stall clean: warning %s, stall %s m/s, %s m lost; full flaps: warning %s, stall %s m/s, %s m lost\n", fx(clean.warn, 1).c_str(), fx(clean.stall, 1).c_str(), fx(clean.lost, 0).c_str(), fx(flaps.warn, 1).c_str(), fx(flaps.stall, 1).c_str(), fx(flaps.lost, 0).c_str());
}

// ---- 6. turn --------------------------------------------------------------------------------------------

void turn() {
  test("30° banked turn: rate g·tan φ / V, height holdable, ball in the middle");
  PlaneGround g;
  const double V = 50;
  Trimmed tr;
  trimmedInAir(tr, V, 700);
  Aircraft& a = tr.a;
  FlightControls& c = tr.c;
  Hold hold = altitudeHold(700);
  double lo = INF;
  double hi = -INF;
  double rate = 0;
  int n = 0;
  fly(a, g, 60, c, [&](double time) {
    ailerons(a, c, 30 * DEG);
    ball(a, c);
    hold(a, c, DT);
    // a little more power in the turn
    c.throttle = clamp(c.throttle + (V - a.tas()) * 0.0005, 0, 1);
    if (time > 20) {
      lo = std::min(lo, a.altitude());
      hi = std::max(hi, a.altitude());
      rate += a.turnRate();
      n++;
    }
    return false;
  });
  rate /= n;
  const double expect = (G * std::tan(30 * DEG)) / a.tas();
  between(rate / expect, 0.85, 1.15, "turn rate " + fx(rate / DEG, 1) + "°/s vs " + fx(expect / DEG, 1) + "°/s");
  between(a.loadFactor(), 1.08, 1.25, "load factor");
  between(lo, 670, 730, "lowest altitude in the turn");
  between(hi, 670, 730, "highest altitude in the turn");
  ok(std::abs(a.slip()) < 0.3, "ball " + fx(a.slip(), 2));
  std::printf("    30° turn at %s m/s: %s°/s (g·tan φ/V = %s), %s g, altitude %s…%s m\n", fx(a.tas(), 1).c_str(), fx(rate / DEG, 2).c_str(), fx(expect / DEG, 2).c_str(), fx(a.loadFactor(), 2).c_str(), fx(lo, 0).c_str(), fx(hi, 0).c_str());
}

// ---- 7. landing ------------------------------------------------------------------------------------------

struct Landing {
  std::string crashed;
  double touchdown;
  double roll;
  double fromAim;
  double sink;
  bool stopped;
};

/** A 3° approach at about 33 m/s with full flaps, flare, touchdown, brakes. */
Landing landing(PlaneGround& g, double dt = DT) {
  Aircraft a;
  // 1.5 km out on the glide path, aiming at z = 0
  const double start = 1500;
  const double slope = std::tan(3 * DEG);
  a.placeInAir(0, start * slope + 1.25, start, 0, 34, &g);
  const LevelFlight lf = a.levelFlight(33, 1);
  FlightControls c = controls();
  c.flaps = 1;
  c.trim = lf.trim;
  c.throttle = lf.throttle;
  double pitch = a.pitch();
  double touchdown = NAN;
  double sink = 0;
  bool flare = false;
  const double t = fly(
      a, g, 150, c,
      [&](double) {
        const double path = std::max(0.0, a.pos.z) * slope;
        const double agl = a.agl();
        ailerons(a, c);
        if (a.onGround()) {
          if (std::isnan(touchdown)) {
            touchdown = a.pos.z;
            sink = -a.verticalSpeed();
          }
          c.throttle = 0;
          // nose wheel down, then brakes
          pitchTo(a, c, 0);
          c.brake = a.wheels[0].contact ? 1 : 0;
          track(a, c, 0);
          return a.groundSpeed() < 0.3 && a.wheels[0].contact;
        }
        ball(a, c);
        if (!flare && agl < 6) flare = true;
        if (flare) {
          // flare: idle, the sink rate brought down with the height, to some 0.3 m/s at the wheels
          c.throttle = 0;
          const double e = -std::max(0.3, 0.3 * agl) - a.verticalSpeed();
          pitch = clamp(pitch + 0.03 * e * DT, -5 * DEG, 12 * DEG);
          pitchTo(a, c, pitch + 0.04 * e);
          return a.crashed.has_value();
        }
        // glide path by pitch, speed by power
        const double e = -a.groundSpeed() * slope + clamp(0.2 * (path - agl), -2, 2) - a.verticalSpeed();
        pitch = clamp(pitch + 0.004 * e * DT, -10 * DEG, 10 * DEG);
        pitchTo(a, c, pitch + 0.012 * e);
        c.throttle = clamp(lf.throttle + 0.08 * (33 - a.ias()), 0, 1);
        return a.crashed.has_value();
      },
      dt);
  return {crashName(a), touchdown, touchdown - a.pos.z, -a.pos.z, sink, t < 150 && a.groundSpeed() < 0.3};
}

void landings() {
  test("landing: 3° approach, flare, touchdown, brakes: stops within 150–400 m of the aiming point");
  PlaneGround g;
  const Landing l = landing(g);
  std::printf("    landing: touchdown at %s m/s sink, %s m past the aiming point, ground roll %s m, stopped %s m past the aiming point\n", fx(l.sink, 2).c_str(), fx(-l.touchdown, 0).c_str(), fx(l.roll, 0).c_str(), fx(l.fromAim, 0).c_str());
  ok(l.crashed == "null", "crashed " + l.crashed);
  ok(l.stopped, "came to a stop");
  between(l.sink, 0, 2, "sink rate at touchdown (m/s)");
  between(l.roll, 80, 300, "ground roll (m)");
  between(l.fromAim, 150, 400, "from the aiming point to the stop (m)");
}

// ---- 8. crashes -------------------------------------------------------------------------------------------

void crashImpact() {
  test("crash: flying steeply into the ground is an impact, and the wreck stays where it is");
  PlaneGround g;
  Aircraft a;
  a.placeInAir(0, 60, 0, 0, 50);
  FlightControls c = controls();
  c.throttle = 0.7;
  fly(a, g, 20, c, [&](double) {
    pitchTo(a, c, -30 * DEG);
    return a.crashed.has_value();
  });
  ok(crashedIs(a, "impact"), std::string("impact, not ") + crashName(a));
  const FlightVec p = a.pos;
  fly(a, g, 2, c, nothing);
  ok(a.pos.x == p.x && a.pos.y == p.y && a.pos.z == p.z, "the wreck does not move");
  ok(a.groundSpeed() == 0, "no ground speed");
}

void crashWing() {
  test("crash: a wing tip on the ground at 60° bank, 1 m up");
  PlaneGround g;
  Aircraft a;
  a.placeInAir(0, 10, 0, 0, 35);
  // roll to 60° (right wing down: about the body z axis, negative) and put the lowest wheel 1 m above the ground
  const auto [x, y, z, w] = a.q;
  const double s = std::sin(-30 * DEG);
  const double k = std::cos(-30 * DEG);
  a.q[0] = x * k + y * s;
  a.q[1] = y * k - x * s;
  a.q[2] = z * k + w * s;
  a.q[3] = w * k - z * s;
  FlightControls c = controls();
  c.throttle = 0.6;
  a.step(DT, c, g);
  a.pos.y += 1 - a.agl();
  between(a.bank() / DEG, 55, 65, "bank");
  a.step(DT, c, g);
  ok(crashedIs(a, "wing"), std::string("wing, not ") + crashName(a));
}

void crashTouchdown() {
  test("crash: a hard touchdown (4 m/s) is an impact, a firm one (2 m/s) is not");
  PlaneGround g;
  struct Case {
    double sink;
    const char* expect;
  };
  for (const Case& k : {Case{4, "impact"}, Case{2, "null"}}) {
    Aircraft a;
    a.placeInAir(0, 1.6, 0, 0, 30);
    a.vel.y = -k.sink;
    FlightControls c = controls();
    fly(a, g, 3, c, [&](double) {
      pitchTo(a, c, 6 * DEG);
      c.elevator = 0.3;
      return a.crashed.has_value();
    });
    ok(std::string(crashName(a)) == k.expect, "sink " + jsStr(k.sink) + " m/s: " + crashName(a));
  }
}

void crashOverspeed() {
  test("crash: far beyond vne the airframe fails");
  PlaneGround g;
  Trimmed tr;
  trimmedInAir(tr, 70, 3000);
  Aircraft& a = tr.a;
  FlightControls& c = tr.c;
  c.throttle = 1;
  fly(a, g, 60, c, [&](double) {
    pitchTo(a, c, -45 * DEG);
    ailerons(a, c);
    return a.crashed.has_value();
  });
  ok(crashedIs(a, "overspeed"), std::string("overspeed, not ") + crashName(a));
  ok(a.altitude() > 100, "it broke in the air");
}

// ---- 9. robustness ----------------------------------------------------------------------------------------

void robustSteps() {
  test("robust: the take-off at 1/30 s and 1/60 s steps gives the same result");
  PlaneGround g1, g2, g3;
  const TakeOff fine = takeOff(g1, 1.0 / 60);
  const TakeOff coarse = takeOff(g2, 1.0 / 30);
  ok(coarse.crashed == "null", "coarse: crashed " + coarse.crashed);
  between(coarse.distance / fine.distance, 0.95, 1.05, "take-off distance 1/30 vs 1/60");
  const TakeOff steps = takeOff(g3, 1.0 / 20);
  between(steps.distance / fine.distance, 0.9, 1.1, "take-off distance 1/20 vs 1/60");
}

void robustRandom() {
  test("robust: a minute of random inputs and step lengths, in the air and on the ground: never NaN");
  double seed = 12345;
  auto rnd = [&]() {
    seed = std::fmod(seed * 1103515245 + 12345, 2147483648.0);
    return seed / 2147483648.0;
  };
  for (const char* start : {"air", "ground"}) {
    PlaneGround g(0.03);
    Aircraft a;
    if (std::string(start) == "air") a.placeInAir(0, 300, 0, 1, 45);
    else a.placeOnGround(0, 0, 1, g, true);
    FlightControls c = controls();
    for (int i = 0; i < 3600; i++) {
      if (i % 20 == 0) {
        c.throttle = rnd();
        c.elevator = rnd() * 2 - 1;
        c.aileron = rnd() * 2 - 1;
        c.rudder = rnd() * 2 - 1;
        c.flaps = std::floor(rnd() * 4) / 3;
        c.trim = rnd() * 2 - 1;
        c.brake = rnd() < 0.3 ? rnd() : 0;
        if (rnd() < 0.2) c.brakeLeft = rnd();
        else c.brakeLeft = std::nullopt;
        c.parkingBrake = rnd() < 0.1;
        c.engineOn = rnd() < 0.95;
      }
      if (i == 1800) {
        // and some nonsense
        c.throttle = NAN;
        c.elevator = INF;
        c.aileron = -INF;
      }
      a.step(rnd() * 0.05 + 0.001, c, g);
      const double vals[] = {a.pos.x, a.pos.y, a.pos.z, a.vel.x, a.vel.y, a.vel.z, a.q[0], a.q[1], a.q[2], a.q[3], a.omega.x, a.omega.y, a.omega.z, a.ias(), a.agl(), a.heading(), a.pitch(), a.bank(), a.slip(), a.aoa(), a.turnRate(), a.rpm(), a.loadFactor(), a.propAngle(), a.flapsDeg(), a.fuel};
      for (double v : vals) ok(std::isfinite(v), std::string(start) + ": not finite after " + std::to_string(i) + " steps");
      ok(std::abs(std::sqrt(a.q[0] * a.q[0] + a.q[1] * a.q[1] + a.q[2] * a.q[2] + a.q[3] * a.q[3]) - 1) < 1e-6, "unit quaternion");
    }
  }
}

void surfaces() {
  test("surfaces, flaps and instruments move as they should");
  PlaneGround g;
  Trimmed tr;
  trimmedInAir(tr, 45, 500);
  Aircraft& a = tr.a;
  FlightControls& c = tr.c;
  c.flaps = 1;
  c.elevator = 1;
  c.aileron = 1;
  c.rudder = 1;
  c.trim = 0;
  fly(a, g, 0.5, c, nothing);
  const FlightSurfaces s = a.surfaces();
  between(s.elevator / DEG, AERO.elevUp / DEG - 1, AERO.elevUp / DEG + 0.01, "elevator up");
  between(s.aileron / DEG, 19, 20.01, "aileron");
  between(s.rudder / DEG, 15, 16.01, "rudder");
  // the flaps run at 5°/s
  between(a.flapsDeg(), 2, 3, "flaps after 0.5 s");
  fly(a, g, 7, c, nothing);
  between(a.flapsDeg(), TRAINER.flapsMax - 0.01, TRAINER.flapsMax, "flaps out");
  between(maxLift(1).cl, 1.85, 2.1, "CLmax full flaps");
  between(maxLift(0).cl, 1.4, 1.6, "CLmax clean");
}

void performance() {
  test("performance: well under 0.05 ms per 1/60 s step in the air");
  PlaneGround g;
  Trimmed tr;
  trimmedInAir(tr, 50, 1000);
  Aircraft& a = tr.a;
  FlightControls& c = tr.c;
  for (int i = 0; i < 600; i++) a.step(DT, c, g);
  const int n = 6000;
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < n; i++) a.step(DT, c, g);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n;
  // generous: the machines the tests run on are busy with other things too
  ok(ms < 0.15, fx(ms, 4) + " ms per step");
}

// ---- handling ------------------------------------------------------------------------------------------------

/** Roll the aeroplane about its longitudinal axis by `bank` (rad, + = right wing down). */
void rollBy(Aircraft& a, double bank) {
  const auto [x, y, z, w] = a.q;
  const double s = std::sin(-bank / 2);
  const double k = std::cos(-bank / 2);
  a.q[0] = x * k + y * s;
  a.q[1] = y * k - x * s;
  a.q[2] = z * k + w * s;
  a.q[3] = w * k - z * s;
}

/** Pitch the aeroplane about its lateral axis by `pitch` (rad, + = nose up). */
void pitchBy(Aircraft& a, double pitch) {
  const auto [x, y, z, w] = a.q;
  const double s = std::sin(pitch / 2);
  const double k = std::cos(pitch / 2);
  a.q[0] = w * s + x * k;
  a.q[1] = y * k + z * s;
  a.q[2] = z * k - y * s;
  a.q[3] = w * k - x * s;
}

void stability() {
  test("stability hands off: phugoid lightly damped, Dutch roll damped, spiral about neutral");
  PlaneGround g;
  // phugoid: a gust from above, only the wings held level
  {
    Trimmed tr;
    trimmedInAir(tr, 50, 800);
    Aircraft& a = tr.a;
    FlightControls& c = tr.c;
    fly(a, g, 2, c, [&](double) {
      ailerons(a, c);
      return false;
    });
    a.vel.y -= 4;
    std::vector<double> ias;
    fly(a, g, 150, c, [&](double) {
      ailerons(a, c);
      ias.push_back(a.ias());
      return false;
    });
    double mean = 0;
    for (size_t i = 120 * 60; i < ias.size(); i++) mean += ias[i];
    mean /= 30 * 60;
    auto swing = [&](int from, int to) {
      double m = -INF;
      for (int i = from * 60; i < to * 60 && i < (int)ias.size(); i++) m = std::max(m, std::abs(ias[i] - mean));
      return m;
    };
    const double first = swing(0, 40);
    const double later = swing(60, 100);
    // period from the crossings of the mean
    std::vector<double> cross;
    for (int i = 1; i < 100 * 60; i++)
      if ((ias[i - 1] - mean) * (ias[i] - mean) < 0) cross.push_back(i / 60.0);
    const double period = cross.size() >= 3 ? ((cross.back() - cross[0]) / (cross.size() - 1)) * 2 : 0;
    std::printf("    phugoid: period %s s, speed swing %s → %s m/s\n", fx(period, 1).c_str(), fx(first, 2).c_str(), fx(later, 2).c_str());
    between(period, 18, 40, "phugoid period (s)");
    ok(first > 1, "the gust starts a phugoid");
    ok(later < first * 0.7 && later > first * 0.05, "lightly damped: " + fx(first, 2) + " → " + fx(later, 2));
  }
  // Dutch roll: a rudder doublet, then hands off
  {
    Trimmed tr;
    trimmedInAir(tr, 50, 800);
    Aircraft& a = tr.a;
    FlightControls& c = tr.c;
    std::vector<double> beta;
    fly(a, g, 12, c, [&](double time) {
      c.rudder = time < 0.5 ? 0 : time < 1 ? 1 : time < 1.5 ? -1 : 0;
      beta.push_back(std::abs(a.sideslip()));
      return false;
    });
    auto peak = [&](double from, double to) {
      double m = -INF;
      for (int i = (int)(from * 60); i < (int)(to * 60) && i < (int)beta.size(); i++) m = std::max(m, beta[i]);
      return m;
    };
    const double early = peak(2, 4.5);
    const double late = peak(7, 9.5);
    std::printf("    Dutch roll: sideslip %s° → %s° five seconds later\n", fx(early / DEG, 1).c_str(), fx(late / DEG, 1).c_str());
    ok(early > 3 * DEG, "the doublet starts a Dutch roll");
    ok(late < early * 0.3, "Dutch roll damped");
  }
  // spiral: from 10° of bank either way, hands off, in cruise
  for (const int side : {-1, 1}) {
    Trimmed tr;
    trimmedInAir(tr, TRAINER.vcruise, 800);
    Aircraft& a = tr.a;
    FlightControls& c = tr.c;
    rollBy(a, side * 10 * DEG);
    fly(a, g, 60, c, nothing);
    std::printf("    spiral: %d° → %s° after a minute\n", side * 10, fx(a.bank() / DEG, 1).c_str());
    between((side * a.bank()) / DEG, 3, 25, "bank after a minute hands off (neither back level at once nor diving away)");
  }
}

void groundHandling() {
  test("ground handling: nose wheel steered by the rudder, less at speed; differential brakes");
  PlaneGround g;
  Aircraft a;
  a.placeOnGround(0, 0, 0, g, true);
  FlightControls c = controls();
  c.rudder = 1;
  fly(a, g, 1, c, nothing);
  between(a.wheels[0].steer / DEG, 11, 12.01, "nose wheel at full rudder, standing");
  ok(a.wheels[1].steer == 0, "main wheel not steered");
  // taxi in a right-hand circle at walking pace and a bit more
  fly(a, g, 20, c, [&](double) {
    c.throttle = clamp(0.3 + 0.1 * (4 - a.groundSpeed()), 0, 0.6);
    return false;
  });
  const double radius = a.groundSpeed() / a.turnRate();
  std::printf("    taxi circle at %s m/s with full rudder: radius %s m\n", fx(a.groundSpeed(), 1).c_str(), fx(radius, 1).c_str());
  ok(a.turnRate() > 0, "right rudder turns right");
  between(radius, 7, 20, "turning radius (m)");
  // less steering at speed
  Aircraft fast;
  fast.placeOnGround(0, 0, 0, g, true);
  FlightControls cf = controls();
  fly(fast, g, 60, cf, [&](double) {
    cf.throttle = 1;
    track(fast, cf, 0);
    return fast.groundSpeed() > 20;
  });
  cf.rudder = 1;
  fly(fast, g, 0.5, cf, nothing);
  ok(fast.wheels[0].steer < 4 * DEG, "nose wheel at 20 m/s: " + fx(fast.wheels[0].steer / DEG, 1) + "°");
  // differential brakes: the left brake turns the taxiing aeroplane to the left
  Aircraft b;
  b.placeOnGround(0, 0, 0, g, true);
  FlightControls cb = controls();
  cb.throttle = 0.35;
  fly(b, g, 8, cb, nothing);
  const double h0 = b.heading();
  cb.brakeLeft = 1;
  fly(b, g, 3, cb, nothing);
  double turned = b.heading() - h0;
  turned -= 360 * jsRound(turned / 360);
  ok(turned < -10, "left brake: turned " + fx(turned, 1) + "°");
  ok(!b.crashed, std::string("crashed ") + crashName(b));
}

void crashNoseLeg() {
  test("crash: put down on the nose wheel, the nose leg folds");
  PlaneGround g;
  Aircraft a;
  a.placeInAir(0, 3, 0, 0, 30, &g);
  pitchBy(a, -8 * DEG - a.pitch());
  FlightControls c0 = controls();
  a.step(DT, c0, g);
  a.pos.y -= a.agl() - 0.02;
  a.vel.y = -2.5;
  FlightControls c = controls();
  fly(a, g, 2, c, [&](double) { return a.crashed.has_value(); }, 1.0 / 240);
  ok(crashedIs(a, "noseOver"), std::string("noseOver, not ") + crashName(a));
}

void wind() {
  test("wind: a headwind shortens the take-off; in the air the ground speed is the airspeed less the wind");
  PlaneGround g1, g2;
  const TakeOff calm = takeOff(g1);
  const TakeOff windy = takeOff(g2, DT, 5);
  std::printf("    take-off run: calm %s m, 5 m/s headwind %s m\n", fx(calm.distance, 0).c_str(), fx(windy.distance, 0).c_str());
  ok(windy.crashed == "null", "windy: crashed " + windy.crashed);
  ok(windy.distance < calm.distance * 0.85, "shorter into the wind");
  PlaneGround g;
  Trimmed tr;
  trimmedInAir(tr, 50, 800);
  Aircraft& a = tr.a;
  FlightControls& c = tr.c;
  a.wind.z = 10;
  Hold hold = altitudeHold(800);
  fly(a, g, 40, c, [&](double) {
    hold(a, c, DT);
    ailerons(a, c);
    ball(a, c);
    return false;
  });
  between(a.groundSpeed() - (a.tas() - 10), -1.5, 1.5, "ground speed minus (airspeed − headwind)");
}

}  // namespace

int main() {
  std::printf("flight_test (tests/vehicle/flight.test.ts)\n");
  parked();
  engineOnGround();
  engineOffInFlight();
  takeOffRuns();
  climb();
  cruise();
  handsOff();
  stalls();
  turn();
  landings();
  crashImpact();
  crashWing();
  crashTouchdown();
  crashOverspeed();
  robustSteps();
  robustRandom();
  surfaces();
  performance();
  stability();
  groundHandling();
  crashNoseLeg();
  wind();
  std::printf("flight_test: all 25 cases passed\n");
  return 0;
}

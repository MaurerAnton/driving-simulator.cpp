// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim game world core platform geo
// Origin: tests/vehicle/dynamics.test.ts @ v1.7.7
/**
 * Acceptance tests for the vehicle dynamics.
 *
 * The expectations are written from how a real compact car behaves (published
 * figures for 110 kW compact hatchbacks, driving-school experience), not from
 * what the code happens to output.  If one fails, fix the model.
 */
#include <chrono>
#include <cmath>
#include <vector>

#include "sim/vehicle/index.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using namespace fs::harness;

namespace {

/** Bring a fresh car to `target` km/h with the sprint driver, then hand over. */
void cruiseTo(Vehicle& v, double target, double shiftRpm = 5500) {
  sprintDriver d(shiftRpm);
  double t = 0;
  while (kmh(v) < target && t < 90) {
    v.step(DT, d(t, v));
    t += DT;
  }
  ok(kmh(v) >= target, "could not reach " + jsStr(target) + " km/h");
}

void restStandsStill() {
  test("rest: stands still, correct static loads");
  PlaneGround g;
  Vehicle v = makeCar(g);
  run(v, 3, [] { return DriverInput{.handbrake = 1}; });
  const double x0 = v.pos[0];
  const double z0 = v.pos[2];
  const double y0 = v.pos[1];
  run(v, 60, [] { return DriverInput{.handbrake = 1}; });
  ok(jsHypot(v.pos[0] - x0, v.pos[2] - z0) < 0.005, "horizontal drift");
  ok(std::abs(v.speed) < 0.001, "residual speed");
  ok(std::abs(v.pos[1] - y0) < 0.001, "ride height drifts");
  between(v.pos[1], 0.53, 0.55, "cg height at rest");
  double sum = 0;
  for (const WheelState& w : v.wheels) sum = sum + w.load;
  between(sum / (v.spec.mass * 9.81), 0.99, 1.01, "sum of wheel loads / weight");
  between((v.wheels[0].load + v.wheels[1].load) / sum, 0.58, 0.62, "front load share");
}

void idle() {
  test("idle: holds ~800 rpm, revs freely, returns to idle");
  PlaneGround g;
  Vehicle v = makeCar(g);
  run(v, 8, [] { return input(); });
  between(v.rpm, 750, 850, "idle rpm");
  double reached = -1;
  run(v, 1.5, [] { return DriverInput{.throttle = 1}; }, [&](double t, Vehicle& c) {
    if (reached < 0 && c.rpm > 5500) reached = t;
  });
  ok(reached > 0 && reached < 1.5, "engine should rev past 5500 rpm within 1.5 s");
  ok(v.rpm < 6800, "rev limiter");
  run(v, 5, [] { return input(); });
  ok(v.engineRunning, "must not stall when the throttle is released");
  between(v.rpm, 740, 860, "idle after blip");
}

void clutch() {
  test("clutch: dumping it stalls, feeding it in slowly creeps without throttle");
  PlaneGround g;
  Vehicle v = makeCar(g);
  run(v, 1, [] { return DriverInput{.clutch = 1, .gear = 1}; });
  const double z0 = v.pos[2];
  bool stalled = false;
  run(v, 3, [](double t) { return DriverInput{.clutch = jsMax(0, 1 - t / 0.1), .gear = 1}; }, [&](double, Vehicle& c) {
    if (c.stalledEvent) stalled = true;
  });
  ok(stalled, "dumping the clutch at idle must stall the engine");
  ok(!v.engineRunning, "!v.engineRunning");
  ok(z0 - v.pos[2] < 1.5, "the lurch stays short");

  Vehicle w = makeCar(g);
  run(w, 1, [] { return DriverInput{.clutch = 1, .gear = 1}; });
  bool stalled2 = false;
  run(w, 9, [](double t) { return DriverInput{.clutch = jsMax(0, 1 - t / 3), .gear = 1}; }, [&](double, Vehicle& c) {
    if (c.stalledEvent) stalled2 = true;
  });
  ok(!stalled2, "slow release on the flat must not stall a modern petrol engine");
  between(kmh(w), 5, 9, "idle creep speed in 1st");

  Vehicle u = makeCar(g);
  run(u, 1, [] { return DriverInput{.clutch = 1, .gear = 3}; });
  bool stalled3 = false;
  run(u, 5, [](double t) { return DriverInput{.clutch = jsMax(0, 1 - t / 2), .gear = 3}; }, [&](double, Vehicle& c) {
    if (c.stalledEvent) stalled3 = true;
  });
  ok(stalled3, "pulling away in 3rd without throttle stalls");
}

void normalLaunch() {
  test("normal launch: smooth, no stall");
  PlaneGround g;
  Vehicle v = makeCar(g);
  run(v, 1, [] { return DriverInput{.clutch = 1, .gear = 1}; });
  double minAcc = JS_INF;
  bool stalled = false;
  double t15 = -1;
  run(v, 5, [](double t) { return DriverInput{.throttle = 0.35, .clutch = jsMax(0, 1 - t / 1.5), .gear = 1}; }, [&](double t, Vehicle& c) {
    if (t > 0.4 && t < 3) minAcc = jsMin(minAcc, c.accLong);
    if (c.stalledEvent) stalled = true;
    if (t15 < 0 && kmh(c) > 15) t15 = t;
  });
  ok(!stalled, "!stalled");
  ok(t15 > 0 && t15 < 4, "15 km/h within 4 s (took " + toFixed(t15, 2) + ")");
  ok(minAcc > -0.3, "no jerking backwards during the launch (min " + toFixed(minAcc, 2) + " m/s²)");
}

void performance() {
  test("performance: 0–100, top speed, elasticity");
  PlaneGround g;
  Vehicle v = makeCar(g);
  sprintDriver d;
  double t100 = -1;
  run(v, 30, d, [&](double t, Vehicle& c) {
    if (t100 < 0 && kmh(c) >= 100) t100 = t;
  });
  between(t100 - 0.25, 8.0, 10.5, "0–100 km/h");
  run(v, 90, d);
  const double top = kmh(v);
  run(v, 15, d);
  // a 110 kW compact reaches about 215–225 km/h
  between(kmh(v), 205, 228, "top speed");
  ok(kmh(v) - top < 1.5, "top speed is approached asymptotically");

  Vehicle w = makeCar(g);
  cruiseTo(w, 50, 3200);
  // settle in 4th gear just below 60
  run(w, 1, [] { return DriverInput{.clutch = 1, .gear = 4}; });
  run(w, 5, [](double, Vehicle& c) { return DriverInput{.throttle = kmh(c) < 56 ? 0.6 : 0.05, .gear = 4}; });
  ok(w.gear == 4, "w.gear == 4");
  double t60 = -1;
  double t100b = -1;
  run(w, 30, [] { return DriverInput{.throttle = 1, .gear = 4}; }, [&](double t, Vehicle& c) {
    if (t60 < 0 && kmh(c) >= 60) t60 = t;
    if (t100b < 0 && kmh(c) >= 100) t100b = t;
  });
  between(t100b - t60, 6, 10, "60–100 km/h in 4th");
}

void coasting() {
  test("coasting and engine braking");
  PlaneGround g;
  Vehicle v = makeCar(g);
  cruiseTo(v, 104);
  run(v, 0.5, [] { return DriverInput{.gear = 0}; });
  const double a = v.speed;
  run(v, 2, [] { return DriverInput{.gear = 0}; });
  between((a - v.speed) / 2, 0.25, 0.6, "coast-down deceleration at 100 km/h");

  Vehicle w = makeCar(g);
  cruiseTo(w, 84, 4500);
  run(w, 0.4, [] { return DriverInput{.clutch = 1, .gear = 3}; });
  run(w, 1.0, [] { return DriverInput{.throttle = 0.2, .gear = 3}; });
  ok(w.gear == 3, "w.gear == 3");
  const double b = w.speed;
  run(w, 2, [] { return DriverInput{.gear = 3}; });
  between((b - w.speed) / 2, 0.5, 1.5, "engine braking in 3rd around 80 km/h");
}

struct Stop {
  double dist;
  double lock;
  double lateral;
};

Stop stopWith(bool abs) {
  PlaneGround g;
  VehicleSpec spec = COMPACT_CAR;
  spec.abs = abs;
  Vehicle v = makeCar(g, spec);
  cruiseTo(v, 100.5);
  run(v, 0.3, [] { return DriverInput{.clutch = 1, .gear = 0}; });
  const double v0 = kmh(v);
  const double z0 = v.pos[2];
  const double x0 = v.pos[0];
  double lock = 0;
  double longest = 0;
  run(v, 8, [] { return DriverInput{.brake = 1, .clutch = 1, .gear = 0}; }, [&](double, Vehicle& c) {
    if (c.speed > 3 && c.wheels[0].omega * c.spec.wheelRadius < c.speed * 0.2) lock += DT;
    else lock = 0;
    longest = jsMax(longest, lock);
  });
  ok(std::abs(v.speed) < 0.05, "car should have stopped");
  return {(z0 - v.pos[2]) * std::pow(100 / v0, 2), longest, std::abs(v.pos[0] - x0)};
}

void braking() {
  test("braking: ABS stops in under 40 m from 100 km/h and stays straight");
  const Stop withAbs = stopWith(true);
  between(withAbs.dist, 34, 40, "stopping distance 100–0 with ABS");
  ok(withAbs.lock < 0.15, "ABS must not let a front wheel stay locked (" + toFixed(withAbs.lock, 2) + " s)");
  ok(withAbs.lateral < 0.3, "stays in lane");
  const Stop without = stopWith(false);
  ok(without.lock > 0.5, "without ABS the front wheels lock");
  ok(without.dist > withAbs.dist, "locked wheels need more distance");
}

double stopOn(double grip) {
  PlaneGround g(0, grip);
  Vehicle v = makeCar(g);
  sprintDriver d(3000);
  double t = 0;
  while (kmh(v) < 51 && t < 60) {
    DriverInput i = d(t, v);
    i.throttle = jsMin(i.throttle, grip < 1 ? 0.25 : 1);
    v.step(DT, i);
    t += DT;
  }
  const double v0 = kmh(v);
  const double z0 = v.pos[2];
  run(v, 15, [] { return DriverInput{.brake = 1, .clutch = 1, .gear = 0}; });
  return (z0 - v.pos[2]) * std::pow(50 / v0, 2);
}

void lowGrip() {
  test("low grip: stopping distance at least doubles");
  const double dry = stopOn(1);
  const double slick = stopOn(0.4);
  between(dry, 8, 11, "dry 50–0");
  ok(slick > dry * 2, "grass-like surface: " + toFixed(slick, 1) + " m vs dry " + toFixed(dry, 1) + " m");
}

void cornering() {
  test("cornering: ~0.9 g limit, understeer, turning circle");
  PlaneGround g;
  Vehicle v = makeCar(g);
  sprintDriver d(4000);
  double maxLat = 0;
  double trim = 0;
  // circle of 40 m radius around a centre to the right of the start
  run(v, 70, [&](double t, Vehicle& c) {
    const double target = jsMin(27, 6 + t * 0.35);
    DriverInput base = c.speed < target ? d(t, c) : DriverInput{.throttle = 0.12, .gear = c.gear != 0 ? c.gear : 2};
    const double rr = jsHypot(c.pos[0] - 40, c.pos[2]);
    trim += ((rr - 40) * 0.02 + 0.066 - trim) * 0.02;
    base.steer = jsMax(-1, jsMin(1, trim + (rr - 40) * 0.08));
    return base;
  }, [&](double t, Vehicle& c) {
    if (t > 5) maxLat = jsMax(maxLat, std::abs(c.accLat));
  });
  between(maxLat / 9.81, 0.85, 1.0, "peak lateral acceleration");
  const double front = (std::abs(v.wheels[0].slipAngle) + std::abs(v.wheels[1].slipAngle)) / 2;
  const double rear = (std::abs(v.wheels[2].slipAngle) + std::abs(v.wheels[3].slipAngle)) / 2;
  ok(front > rear, "at the limit the front slides first (understeer)");

  Vehicle w = makeCar(g);
  double minX = JS_INF;
  double maxX = -JS_INF;
  run(w, 32, [](double t, Vehicle& c) { return DriverInput{.steer = 1, .throttle = kmh(c) < 7 ? 0.25 : 0.06, .clutch = t < 2.5 ? jsMax(0, 1 - t / 2) : 0, .gear = 1}; },
      [&](double t, Vehicle& c) {
        if (t > 8) {
          minX = jsMin(minX, c.wheels[0].hx);
          maxX = jsMax(maxX, c.wheels[0].hx);
        }
      });
  between(maxX - minX, 10, 11.5, "kerb-to-kerb turning circle");
}

void hills() {
  test("hills: handbrake holds, neutral rolls, parked in gear, hill start");
  PlaneGround g(0.15);
  Vehicle v = makeCar(g);
  run(v, 3, [] { return DriverInput{.handbrake = 1}; });
  const double z0 = v.pos[2];
  run(v, 20, [] { return DriverInput{.handbrake = 1}; });
  ok(std::abs(v.pos[2] - z0) < 0.005, "handbrake creep " + toFixed(std::abs(v.pos[2] - z0) * 1000, 1) + " mm");
  run(v, 4, [] { return input(); });
  ok(v.speed < -2, "released in neutral the car rolls back and gathers speed");

  Vehicle p = makeCar(g, COMPACT_CAR, false);
  run(p, 2, [] { return DriverInput{.handbrake = 1, .gear = 1, .ignition = false}; });
  const double z1 = p.pos[2];
  run(p, 10, [] { return DriverInput{.gear = 1, .ignition = false}; });
  ok(std::abs(p.pos[2] - z1) < 0.3, "left in 1st gear the dead engine holds the car");

  Vehicle h = makeCar(g);
  run(h, 2, [] { return DriverInput{.clutch = 1, .handbrake = 1, .gear = 1}; });
  const double z2 = h.pos[2];
  double back = 0;
  bool stalled = false;
  run(h, 8, [](double t) { return DriverInput{.throttle = t < 0.5 ? 0.25 : 0.42, .clutch = jsMax(0, 1 - jsMax(0, t - 0.3) / 2.2), .handbrake = t < 1.5 ? 1.0 : 0.0, .gear = 1}; },
      [&](double, Vehicle& c) {
        back = jsMax(back, c.pos[2] - z2);
        if (c.stalledEvent) stalled = true;
      });
  ok(!stalled, "hill start must not stall");
  ok(back < 0.1, "rolled back " + toFixed(back, 2) + " m");
  ok(kmh(h) > 5, "drives off up the hill");
}

void lightFoot() {
  test("a light foot on the brake stops the car completely and holds it, on any slope");
  // Found by the virtual learner driver: the car kept crawling at 0.3 m/s
  // against a half-pressed brake and rolled over a red stop line.
  struct Case {
    const char* name;
    const VehicleSpec* spec;
    int gear;
  };
  const Case cases[] = {
      {"automatic in D", &COMPACT_CAR_AUTOMATIC, 1},
      {"manual in neutral", &COMPACT_CAR, 0},
      {"clutch assist in 1st", &COMPACT_CAR_AUTOCLUTCH, 1},
  };
  for (const Case& cs : cases) {
    const int gear = cs.gear;
    for (const double grade : {0.0, -0.1, 0.1}) {
      for (const double brake : {0.25, 0.5}) {
        PlaneGround g(grade);
        Vehicle v = makeCar(g, *cs.spec);
        run(v, 3, [&] { return DriverInput{.throttle = 0.12, .gear = gear}; });  // roll (or creep) first
        run(v, 4, [&] { return DriverInput{.brake = brake, .gear = gear}; });
        const double x0 = v.pos[0];
        const double z0 = v.pos[2];
        run(v, 5, [&] { return DriverInput{.brake = brake, .gear = gear}; });
        const double moved = jsHypot(v.pos[0] - x0, v.pos[2] - z0);
        const std::string what = std::string(cs.name) + ", grade " + jsStr(grade) + ", brake " + jsStr(brake);
        ok(moved < 0.01, what + ": crept " + toFixed(moved * 100, 1) + " cm in 5 s");
        ok(std::abs(v.speed) < 0.005, what + ": still moving at " + toFixed(v.speed, 3) + " m/s");
        for (const WheelState& wh : v.wheels) ok(wh.omega == 0, what + ": wheel not held");
      }
    }
  }
}

void rollover() {
  test("rollover: the body shell carries the car, it does not sink into the ground");
  // Found by the virtual learner driver: once the car lay on its side the
  // wheels no longer reached the ground and it fell through the world.
  struct Case {
    const char* what;
    double angle;
    double lo;
    double hi;
  };
  const Case cases[] = {
      {"on the roof", PI, 0.7, 1.0},
      {"on its side", PI / 2, 0.4, 1.0},
  };
  for (const Case& cs : cases) {
    PlaneGround g;
    Vehicle v = makeCar(g);
    v.quat[0] = 0;
    v.quat[1] = 0;
    v.quat[2] = jsSin(cs.angle / 2);  // roll about the longitudinal axis
    v.quat[3] = jsCos(cs.angle / 2);
    v.pos[1] = 1.15;
    v.vel.fill(0);
    v.angVel.fill(0);
    bool touched = false;
    run(v, 5, [] { return DriverInput{.gear = 0}; }, [&] {
      if (v.bodyContact) touched = true;
    });
    ok(touched, std::string(cs.what) + ": the body never touched the ground");
    between(v.pos[1], cs.lo, cs.hi, std::string(cs.what) + ": height of the centre of mass at rest");
    ok(jsHypot(v.vel[0], v.vel[1], v.vel[2]) < 0.05, std::string(cs.what) + ": still moving");
  }
  // and on its wheels the shell stays out of it
  PlaneGround g;
  Vehicle v = makeCar(g);
  bool touched = false;
  run(v, 6, [](double t) { return DriverInput{.steer = t > 3 ? 1.0 : 0.0, .throttle = 0.5, .clutch = t < 0.3 ? 1.0 : 0.0, .gear = 1}; }, [&] {
    if (v.bodyContact) touched = true;
  });
  ok(touched == false, "body contact while driving normally");
  ok(v.upright > 0.97, "leaning " + toFixed(v.upright, 3) + " in a tight turn");
}

void reverseGear() {
  test("reverse gear");
  PlaneGround g;
  Vehicle v = makeCar(g);
  run(v, 1, [] { return DriverInput{.clutch = 1, .gear = -1}; });
  run(v, 25, [](double t) { return DriverInput{.throttle = 1, .clutch = jsMax(0, 1 - t / 1.5), .gear = -1}; });
  between(kmh(v), -62, -40, "reverse top speed at the limiter");
}

void gearboxRefuses() {
  test("gearbox refuses a shift without the clutch");
  PlaneGround g;
  Vehicle v = makeCar(g);
  cruiseTo(v, 30, 9000);  // stays in 1st
  ok(v.gear == 1, "v.gear == 1");
  bool grind = false;
  // pull out of gear (always possible), then try 2nd with the engine at idle speed
  run(v, 0.3, [] { return DriverInput{.throttle = 0, .clutch = 0, .gear = 0}; });
  run(v, 0.5, [] { return DriverInput{.throttle = 0, .clutch = 0, .gear = 2}; }, [&](double, Vehicle& c) {
    if (c.gearGrind) grind = true;
  });
  ok(grind, "gearGrind is reported");
  ok(v.gear == 0, "the gear does not go in");
  run(v, 0.3, [] { return DriverInput{.clutch = 1, .gear = 2}; });
  ok(v.gear == 2, "with the clutch pressed it does");
}

void starter() {
  test("starter: cranks, fires, and moves the car when left in gear");
  PlaneGround g;
  Vehicle v = makeCar(g, COMPACT_CAR, false);
  run(v, 0.5, [] { return input(); });
  ok(!v.engineRunning, "!v.engineRunning");
  double fired = -1;
  run(v, 2, [] { return DriverInput{.starter = true}; }, [&](double t, Vehicle& c) {
    if (fired < 0 && c.engineRunning) fired = t;
  });
  between(fired, 0.4, 1.3, "time until the engine fires");
  run(v, 3, [] { return input(); });
  between(v.rpm, 740, 860, "idle after start");

  Vehicle w = makeCar(g, COMPACT_CAR, false);
  run(w, 0.5, [] { return DriverInput{.gear = 1}; });
  const double z0 = w.pos[2];
  run(w, 0.8, [] { return DriverInput{.gear = 1, .starter = true}; });
  ok(z0 - w.pos[2] > 0.15, "starting in gear with the clutch engaged makes the car lurch forward");
}

void automatic() {
  test("automatic gearbox: creep, kick-down, park");
  PlaneGround g;
  Vehicle v = makeCar(g, COMPACT_CAR_AUTOMATIC);
  run(v, 12, [] { return DriverInput{.gear = 1}; });
  between(kmh(v), 4, 8, "creep speed in D");
  ok(v.engineRunning, "v.engineRunning");

  Vehicle w = makeCar(g, COMPACT_CAR_AUTOMATIC);
  double t100 = -1;
  run(w, 30, [] { return DriverInput{.throttle = 1, .gear = 1}; }, [&](double t, Vehicle& c) {
    if (t100 < 0 && kmh(c) >= 100) t100 = t;
  });
  // modern automatics are about as quick as the manual version of the same car
  between(t100, 8.0, 11.5, "automatic 0–100 km/h");

  PlaneGround slope(0.15);
  Vehicle p = makeCar(slope, COMPACT_CAR_AUTOMATIC);
  run(p, 2, [] { return DriverInput{.brake = 1, .gear = -2}; });
  const double z0 = p.pos[2];
  run(p, 10, [] { return DriverInput{.gear = -2}; });
  ok(std::abs(p.pos[2] - z0) < 0.05, "P holds on a 15 % slope");
}

void clutchAssist() {
  test("clutch assist: launches on throttle alone, never stalls when stopping");
  PlaneGround g;
  Vehicle v = makeCar(g, COMPACT_CAR_AUTOCLUTCH);
  bool stalled = false;
  run(v, 5, [] { return DriverInput{.throttle = 0.5, .gear = 1}; }, [&](double, Vehicle& c) {
    if (c.stalledEvent) stalled = true;
  });
  ok(kmh(v) > 20, "pulls away");
  run(v, 6, [] { return DriverInput{.brake = 0.6, .gear = 1}; }, [&](double, Vehicle& c) {
    if (c.stalledEvent) stalled = true;
  });
  ok(std::abs(v.speed) < 0.05, "Math.abs(v.speed) < 0.05");
  ok(!stalled && v.engineRunning, "no stall");
}

void kerb() {
  test("kerb: 12 cm step at 20 km/h");
  PlaneGround g;
  g.stepZ = -30;
  g.stepHeight = 0.12;
  Vehicle v = makeCar(g);
  cruiseTo(v, 20, 3000);
  double maxPitch = 0;
  run(v, 8, [](double, Vehicle& c) { return DriverInput{.throttle = kmh(c) < 20 ? 0.3 : 0.08, .gear = c.gear}; }, [&](double, Vehicle& c) {
    ok(std::isfinite(c.pos[1]) && std::isfinite(c.vel[0]), "state must stay finite");
    const double pitch = std::asin(jsMax(-1, jsMin(1, 2 * (c.quat[3] * c.quat[0] - c.quat[1] * c.quat[2]))));
    maxPitch = jsMax(maxPitch, std::abs(pitch));
  });
  ok(v.pos[2] < -32, "car continues past the kerb");
  ok((maxPitch * 180) / PI < 12, "pitch stays moderate");
  between(v.pos[1], 0.64, 0.68, "settles 12 cm higher");
}

void stability() {
  test("stability: straight at 180 km/h, damped response to a steering step");
  PlaneGround g;
  Vehicle v = makeCar(g);
  cruiseTo(v, 180);
  const double x0 = v.pos[0];
  double drift = 0;
  run(v, 30, [](double, Vehicle& c) { return DriverInput{.throttle = kmh(c) < 180 ? 0.9 : 0.5, .gear = c.gear}; }, [&](double, Vehicle& c) {
    drift = jsMax(drift, std::abs(c.pos[0] - x0));
  });
  ok(drift < 0.5, "lateral drift " + toFixed(drift, 3) + " m");

  Vehicle w = makeCar(g);
  cruiseTo(w, 100);
  std::vector<double> trace;
  run(w, 3, [](double, Vehicle& c) { return DriverInput{.steer = 0.05, .throttle = 0.3, .gear = c.gear}; }, [&](double, Vehicle& c) { trace.push_back(c.angVel[1]); });
  // steering right → yaw decreases → negative yaw rate
  double sum = 0;
  for (size_t i = trace.size() - 120; i < trace.size(); i++) sum = sum + trace[i];
  const double settled = sum / 120;
  between(-settled, 0.08, 0.35, "steady yaw rate for 0.05 steering at 100 km/h");
  double hi = -JS_INF;
  double lo = JS_INF;
  for (size_t i = static_cast<size_t>(jsRound(2 / DT)); i < trace.size(); i++) {
    hi = jsMax(hi, trace[i]);
    lo = jsMin(lo, trace[i]);
  }
  const double ripple = hi - lo;
  ok(ripple < 0.02, "yaw rate settles within 2 s (ripple " + toFixed(ripple, 4) + ")");
}

std::vector<double> runOnce() {
  PlaneGround g;
  Vehicle v = makeCar(g);
  sprintDriver d;
  run(v, 12, [&](double t, Vehicle& c) {
    DriverInput i = d(t, c);
    i.steer = jsSin(t * 0.7) * 0.1;
    return i;
  });
  std::vector<double> out;
  out.insert(out.end(), v.pos.begin(), v.pos.end());
  out.insert(out.end(), v.quat.begin(), v.quat.end());
  out.insert(out.end(), v.vel.begin(), v.vel.end());
  out.insert(out.end(), v.angVel.begin(), v.angVel.end());
  out.push_back(v.rpm);
  return out;
}

void deterministic() {
  test("deterministic");
  ok(runOnce() == runOnce(), "two runs give the same state");
}

void fastEnough() {
  test("performance: fast enough for 240 Hz");
  PlaneGround g;
  Vehicle v = makeCar(g);
  const DriverInput inp{.throttle = 0.5, .gear = 2};
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < 120000; i++) v.step(DT, inp);
  const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 120000;
  // budget: 240 steps/s must stay far below one frame; generous for slow CI machines
  ok(us < 250, toFixed(us, 1) + " µs per step");
}

}  // namespace

int main() {
  std::printf("vehicle_test (tests/vehicle/dynamics.test.ts)\n");
  restStandsStill();
  idle();
  clutch();
  normalLaunch();
  performance();
  coasting();
  braking();
  lowGrip();
  cornering();
  hills();
  lightFoot();
  rollover();
  reverseGear();
  gearboxRefuses();
  starter();
  automatic();
  clutchAssist();
  kerb();
  stability();
  deterministic();
  fastEnough();
  std::printf("vehicle_test: all 21 cases passed\n");
  return 0;
}

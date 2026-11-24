// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim world core platform geo
/**
 * The vehicle model on scripted drives, for comparison with the original (tools/diff/vehicle.ts):
 *
 *   node tools/diff/vehicle.ts /home/user/driving-simulator > a.txt
 *   tools/t.sh vehicle_dump > b.txt
 *   tools/diff/compare.py a.txt b.txt
 *
 * The scripts are the ones of vehicle.ts, line by line: nine vehicles, ten drives each, a line of
 * state every 0.1 s with full precision.
 */
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/core/js.h"
#include "src/sim/jsmath.h"
#include "src/sim/vehicle/fleet.h"
#include "src/sim/vehicle/index.h"

using namespace fs;

namespace {

const double DT = 1.0 / 240;

/** Inclined plane rising towards north (-Z) with the given grade (0.15 = 15 %): tests/vehicle/harness.ts */
class PlaneGround : public Ground {
 public:
  double grade;
  double grip;
  double rolling;
  /** optional step: ground is `stepHeight` higher for z < stepZ */
  double stepZ = -JS_INF;
  double stepHeight = 0;

  explicit PlaneGround(double grade_ = 0, double grip_ = 1, double rolling_ = 1) : grade(grade_), grip(grip_), rolling(rolling_) {}

  void sample(double, double z, double, GroundSample& out) override {
    const double l = jsHypot(1, grade);
    out.y = -z * grade + (z < stepZ ? stepHeight : 0);
    out.nx = 0;
    out.ny = 1 / l;
    out.nz = grade / l;
    out.grip = grip;
    out.rolling = rolling;
  }
};

struct In : DriverInput {
  In& g(int v) { gear = v; return *this; }
  In& th(double v) { throttle = v; return *this; }
  In& br(double v) { brake = v; return *this; }
  In& cl(double v) { clutch = v; return *this; }
  In& hb(double v) { handbrake = v; return *this; }
  In& st(double v) { steer = v; return *this; }
  In& starterOn() { starter = true; return *this; }
  In& ignitionOff() { ignition = false; return *this; }
};
In input() { return In{}; }

struct Entry {
  std::string id;
  const VehicleSpec* spec;
  /** from which speed (km/h) the brake test brakes, and at which the slalom is driven */
  double vBrake;
  double vSlalom;
};

/** The test driver: pulls away and shifts up at full throttle, holds a gear, brakes. */
class Driver {
 public:
  int gear = 1;
  double shiftT = -1;
  const VehicleSpec& spec;
  const bool automatic;
  const double shiftRpm;

  explicit Driver(const VehicleSpec& s) : spec(s), automatic(s.transmission == "automatic"), shiftRpm(s.limiterRpm - 300) {}

  DriverInput sprint(double t, const Vehicle& v) {
    if (automatic) return input().g(1).th(1);
    if (t < 0.25) return input().g(1).cl(1).th(0.55);
    if (shiftT >= 0) {
      if (t - shiftT < 0.25) return input().g(gear).cl(1).th(0);
      shiftT = -1;
    }
    if (v.rpm > shiftRpm && gear < static_cast<int>(spec.gearRatios.size())) {
      gear++;
      shiftT = t;
      return input().g(gear).cl(1).th(0);
    }
    // slip the clutch off the line, then full power
    const double launch = t < 1.6 ? jsMax(0, 0.62 - (t - 0.25) * 0.5) : 0;
    return input().g(gear).cl(launch).th(1);
  }

  DriverInput hold(double throttle) { return input().g(automatic ? 1 : gear).th(throttle); }

  DriverInput brake(double b) { return input().g(automatic ? 1 : gear).br(b).cl(automatic ? 0 : 1); }
};

struct Script {
  std::string name;
  double seconds;
  PlaneGround ground;
  bool running;
  std::function<DriverInput(double t, Vehicle& v)> drive;
  /** something done to the vehicle before step `i` */
  std::function<void(int i, Vehicle& v)> before = nullptr;
};

std::vector<Script> scripts(const Entry& car) {
  const VehicleSpec& s = *car.spec;
  const bool automatic = s.transmission == "automatic";
  std::vector<Script> list;
  {
    auto d = std::make_shared<Driver>(s);
    list.push_back({"launch", 20, PlaneGround(), true, [d](double t, Vehicle& v) { return d->sprint(t, v); }});
  }
  {
    auto d = std::make_shared<Driver>(s);
    auto braking = std::make_shared<bool>(false);
    const double vBrake = car.vBrake;
    list.push_back({"brake", 30, PlaneGround(), true, [d, braking, vBrake](double t, Vehicle& v) {
                      if (!*braking && v.speed * 3.6 >= vBrake) *braking = true;
                      return *braking ? d->brake(1) : d->sprint(t, v);
                    }});
  }
  {
    auto d = std::make_shared<Driver>(s);
    auto t0 = std::make_shared<double>(-1);
    const double vSlalom = car.vSlalom;
    list.push_back({"slalom", 25, PlaneGround(), true, [d, t0, vSlalom](double t, Vehicle& v) {
                      if (*t0 < 0 && v.speed * 3.6 >= vSlalom) *t0 = t;
                      if (*t0 < 0) return d->sprint(t, v);
                      DriverInput inp = d->hold(0.3);
                      inp.steer = (t - *t0 < 8 ? 0.2 : 0.6) * jsSin((t - *t0) * 2.2);
                      return inp;
                    }});
  }
  list.push_back({"hill", 10, PlaneGround(0.12), true, [automatic](double t, Vehicle&) -> DriverInput {
                    if (automatic) return t < 1 ? input().g(1).br(1) : input().g(1).th(0.5);
                    if (t < 1) return input().g(1).cl(1).hb(1);
                    if (t < 4) return input().g(1).cl(jsMax(0, 1 - (t - 1) * 0.4)).th(0.45).hb(t < 1.8 ? 1 : 0);
                    return input().g(1).th(0.5);
                  }});
  list.push_back({"reverse", 8, PlaneGround(), true, [automatic](double t, Vehicle&) -> DriverInput {
                    const double steer = t > 2 ? 0.4 : 0;
                    if (t >= 5) return input().g(-1).br(0.6).cl(automatic ? 0 : 1).st(steer);
                    if (automatic) return input().g(-1).th(0.3).st(steer);
                    if (t < 0.3) return input().g(-1).cl(1);
                    return input().g(-1).cl(jsMax(0, 0.7 - (t - 0.3) * 0.5)).th(0.3).st(steer);
                  }});
  {
    auto d = std::make_shared<Driver>(s);
    PlaneGround ground;
    ground.stepZ = -12;
    ground.stepHeight = 0.1;
    list.push_back({"kerb", 6, ground, true, [d](double t, Vehicle& v) {
                      DriverInput inp = d->sprint(t, v);
                      inp.steer = 0.03;
                      return inp;
                    }});
  }
  {
    auto d = std::make_shared<Driver>(s);
    const double mass = s.mass;
    list.push_back({"roll", 10, PlaneGround(), true,
                    [d, automatic](double t, Vehicle& v) -> DriverInput { return t < 4 ? d->sprint(t, v) : input().g(automatic ? 1 : 0); },
                    [mass](int i, Vehicle& v) {
                      // a blow from the left, above the centre of mass
                      if (i == 960) v.applyImpulse(v.pos[0], v.pos[1] + 0.6, v.pos[2], mass * 9, 0, 0);
                    }});
  }
  list.push_back({"start", 6, PlaneGround(), false, [automatic](double t, Vehicle&) -> DriverInput {
                    if (t < 0.2) return input();
                    if (t < 1.6) return input().starterOn();
                    if (automatic) return t < 4 ? input().g(1) : input().g(1).ignitionOff();
                    if (t < 3) return input();
                    if (t < 3.5) return input().g(3).cl(1);
                    return input().g(3).hb(1);
                  }});
  list.push_back({"park", 7, PlaneGround(0.1), automatic, [automatic](double t, Vehicle&) -> DriverInput {
                    if (automatic) return t < 1 ? input().g(-2) : t < 3 ? input().g(-1).th(0.3) : input().g(-2);
                    if (t < 2) return input().g(1);
                    if (t < 4) return input().g(1).cl(1);
                    return input().g(1).cl(1).hb(1);
                  }});
  {
    auto d = std::make_shared<Driver>(s);
    list.push_back({"ice", 12, PlaneGround(0, 0.3), true, [d](double t, Vehicle& v) {
                      if (t < 6) return d->sprint(t, v);
                      DriverInput inp = d->brake(1);
                      inp.steer = 0.3;
                      return inp;
                    }});
  }
  return list;
}

}  // namespace

int main() {
  const std::vector<Entry> CARS = {
      {"compact", &COMPACT_CAR, 100, 50},
      {"compact-autoclutch", &COMPACT_CAR_AUTOCLUTCH, 100, 50},
      {"compact-automatic", &COMPACT_CAR_AUTOMATIC, 100, 50},
      {"truck", &TRUCK, 60, 35},
      {"bus", &BUS, 60, 35},
      {"tractor", &TRACTOR, 30, 25},
      {"motorcycle", &MOTORCYCLE, 100, 50},
      {"motorcycle-automatic", &MOTORCYCLE_AUTOMATIC, 100, 50},
      {"moped", &MOPED.manual, 40, 30},
  };
  std::string out;
  for (const Entry& car : CARS) {
    for (Script& sc : scripts(car)) {
      Vehicle v(*car.spec, sc.ground);
      v.teleport(0, 0, 0, 0);
      v.resetDrivetrain(sc.running);
      const int n = static_cast<int>(jsRound(sc.seconds / DT));
      for (int i = 0; i < n; i++) {
        const double t = i * DT;
        if (sc.before) sc.before(i, v);
        v.step(DT, sc.drive(t, v));
        if ((i + 1) % 24 != 0) continue;
        const auto& w = v.wheels;
        const double f[] = {
            static_cast<double>(i + 1),
            v.pos[0], v.pos[1], v.pos[2],
            v.vel[0], v.vel[1], v.vel[2],
            v.yaw, v.rpm, static_cast<double>(v.gear),
            w[0].load, w[1].load, w[2].load, w[3].load,
            v.quat[0], v.quat[1], v.quat[2], v.quat[3],
            v.angVel[0], v.angVel[1], v.angVel[2],
            w[0].omega, w[1].omega, w[2].omega, w[3].omega,
            w[0].compression, w[1].compression, w[2].compression, w[3].compression,
            v.speed, v.accLong, v.accLat, v.steerTorque, v.clutchLock, v.engineLoad, v.clutchPedal, v.odometer, v.upright,
            v.engineRunning ? 1.0 : 0.0, v.absActive ? 1.0 : 0.0, v.bodyContact ? 1.0 : 0.0, v.stalledEvent ? 1.0 : 0.0, v.gearGrind ? 1.0 : 0.0,
        };
        out += car.id + "/" + sc.name;
        for (double x : f) {
          out += ' ';
          out += jsStr(x);
        }
        out += '\n';
      }
    }
  }
  std::fputs(out.c_str(), stdout);
  return 0;
}

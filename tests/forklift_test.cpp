// SPDX-License-Identifier: AGPL-3.0-only
// Origin: tests/sim/forklift.test.ts @ v1.7.7
/**
 * The forklift (src/sim/forklift.h): the load moment and the load chart,
 * tipping forwards (too heavy, braked hard with the load high) and sideways
 * (turned hard with the load high), the forks sliding into a pallet and
 * lifting it, a misaligned or too high approach that does not take it, and
 * setting a pallet down on a rack's beams.
 */
// LINK: sim core
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>

#include "sim/forklift.h"

using namespace fs;

namespace {

ForkWorld world(const std::vector<Pallet*>& pallets, const std::vector<Shelf>& shelves = {}) {
  ForkWorld w;
  w.ground = [](double, double) { return 0.0; };
  w.shelves = shelves;
  w.pallets = pallets;
  return w;
}

Pallet pallet(const std::string& id, double x, double z, double yaw = 0, double mass = 600, double y = 0) {
  Pallet p;
  p.id = id;
  p.def = euroPallet(1.0, mass);
  p.x = x;
  p.y = y;
  p.z = z;
  p.yaw = yaw;
  p.on = "ground";
  return p;
}

/** Steps for a while (1/120 s), with the command made each step (idle but for what `cmd` sets); stops early when `until` says so. */
void run(Forklift& f, const ForkWorld& w, double seconds, const std::function<void(ForkCommand&)>& cmd, const std::function<bool()>& until = nullptr) {
  const double dt = 1.0 / 120;
  for (double t = 0; t < seconds; t += dt) {
    ForkCommand c{.throttle = 0, .brake = 0, .dir = 1, .hold = false, .steer = 0, .lift = 0, .tilt = 0, .shift = 0};
    cmd(c);
    f.step(dt, c, w);
    if (until && until()) return;
  }
}

/** The pallet's middle this far ahead of a forklift at the origin facing north (its heel at z = −1.295). */
const double heelZ = -(FORKLIFT.wheelbase / 2 + FORKLIFT.heelAhead);

void loadChart() {
  // the load chart: the rated load at the rated centre, less further out
  assert(std::round(capacityAt(0.5)) == 2500);
  assert(capacityAt(0.6) < 2500 && capacityAt(0.6) > 2200);
  assert(std::round(capacityAt(0.3)) == 2500);
}

void forwardStability() {
  // the rated load stands, braked gently low; a load far over it lifts the rear wheels
  const double ahead = FORKLIFT.heelAhead + 0.5;
  const ForkStability rated = stability({.mass = 2500, .ahead = ahead, .height = 0.6, .decel = 0, .lateral = 0});
  assert(rated.forward < 0.75);
  const ForkStability braked = stability({.mass = 2500, .ahead = ahead, .height = 0.6, .decel = 2, .lateral = 0});
  assert(braked.forward < 1);
  const ForkStability high = stability({.mass = 2500, .ahead = ahead, .height = 3.6, .decel = 2, .lateral = 0});
  assert(high.forward > 1);
  const ForkStability heavy = stability({.mass = 3800, .ahead = FORKLIFT.heelAhead + 0.6, .height = 0.6, .decel = 0, .lateral = 0});
  assert(heavy.forward > 1);
}

void sideStability() {
  // sideways: a load high up makes it tip at less lateral acceleration than empty
  const ForkStability empty = stability({.mass = 0, .ahead = 0, .height = 0, .decel = 0, .lateral = 3.2});
  const ForkStability high = stability({.mass = 1500, .ahead = 1.07, .height = 3.6, .decel = 0, .lateral = 3.2});
  assert(empty.side < 1);
  assert(high.side > 1);
}

void pickCarrySetDown() {
  // the forks slide into a pallet square ahead, lift it, carry it and set it down on a rack
  Pallet p = pallet("a", 0, heelZ - 0.6 - 2.0);
  // a rack's beams 1.6 m up, a little further on
  Shelf shelf;
  shelf.id = "rack";
  shelf.x = 0;
  shelf.z = heelZ - 6;
  shelf.hx = 1.1;
  shelf.hz = 0.55;
  shelf.yaw = 0;
  shelf.y0 = 1.48;
  shelf.y1 = 1.6;
  shelf.y = 1.6;
  const ForkWorld w = world({&p}, {shelf});
  Forklift f;
  f.place(0, 0, 0, 0);
  // forwards slowly with the forks down, until it stops at the pallet
  run(f, w, 12, [](ForkCommand& c) { c.throttle = 0.3; }, [&] { return engagement(f.forks(), p).depth > 0.97; });
  const Engagement e = engagement(f.forks(), p);
  assert(e.fits && e.depth > 0.9);
  // stop, lift: it is taken
  run(f, w, 2, [](ForkCommand& c) { c.brake = 1; });
  run(f, w, 1, [](ForkCommand& c) { c.lift = 1; });
  assert(f.load == &p);
  assert(p.y > 0.2);
  // up over the beams, forwards over them, lowered: set down on them
  run(f, w, 6, [](ForkCommand& c) { c.lift = 1; }, [&] { return p.y > 1.75; });
  const double z0 = f.z;
  run(f, w, 20, [](ForkCommand& c) { c.throttle = 0.2; }, [&] { return p.z < shelf.z + 0.05; });
  assert(f.z < z0 - 1);
  run(f, w, 2, [](ForkCommand& c) { c.brake = 1; });
  run(f, w, 3, [](ForkCommand& c) { c.lift = -1; });
  assert(f.load == nullptr);
  assert(p.on == "rack");
  assert(std::abs(p.y - 1.6) < 1e-9);
  // the forks go on down, out of the pallet: it stays
  assert(f.lift < 1.6 - 0.1);
  run(f, w, 4, [](ForkCommand& c) {
    c.dir = -1;
    c.throttle = 0.3;
  });
  assert(p.on == "rack");
  assert(engagement(f.forks(), p).depth == 0);
}

void notTaken() {
  // a pallet at an angle or forks too high: it is not taken, the forks run into it
  for (const auto& [yaw, lift] : {std::pair<double, double>{0.35, FORKLIFT.liftMin}, {0, 0.4}}) {
    Pallet p = pallet("b", 0, heelZ - 0.6 - 1.5, yaw);
    const ForkWorld w = world({&p});
    Forklift f;
    f.place(0, 0, 0, 0);
    f.lift = lift;
    double hit = 0;
    run(f, w, 8, [](ForkCommand& c) { c.throttle = 0.3; }, [&] {
      hit = std::max(hit, f.ev.hit);
      return f.ev.hit > 0;
    });
    if (!(hit > 0)) std::printf("yaw %g lift %g: not stopped\n", yaw, lift);
    assert(hit > 0);
    run(f, w, 2, [](ForkCommand& c) { c.lift = 1; });
    assert(f.load == nullptr);
  }
}

void tooHeavy() {
  // too heavy: lifted, the rear comes up and it tips forwards
  Pallet p = pallet("h", 0, heelZ - 0.6 - 0.5, 0, 3900);
  const ForkWorld w = world({&p});
  Forklift f;
  f.place(0, 0, 0, 0);
  run(f, w, 6, [](ForkCommand& c) { c.throttle = 0.3; }, [&] { return engagement(f.forks(), p).depth > 0.97; });
  run(f, w, 1, [](ForkCommand& c) { c.brake = 1; });
  bool rear = false;
  run(f, w, 8, [](ForkCommand& c) { c.lift = 1; }, [&] {
    rear = rear || f.ev.rearLift;
    return !f.tipped.empty();
  });
  assert(rear);
  assert(f.tipped == "forward");
}

void brakedHigh() {
  // a light load stays down; braked hard with the forks high it tips forwards, low it does not
  const auto trial = [](bool high) {
    Pallet p = pallet("c", 0, heelZ - 0.6 - 0.3, 0, 2000);
    const ForkWorld w = world({&p});
    Forklift f;
    f.place(0, 0, 0, 0);
    run(f, w, 6, [](ForkCommand& c) { c.throttle = 0.3; }, [&] { return engagement(f.forks(), p).depth > 0.97; });
    run(f, w, 1, [](ForkCommand& c) { c.brake = 1; });
    run(f, w, 8, [](ForkCommand& c) { c.lift = 1; }, [&] { return f.lift >= (high ? FORKLIFT.liftMax - 0.01 : 0.3); });
    assert(f.load == &p);
    assert(f.pitch == 0);
    // backwards up to speed, then the brake hard (the load is thrown forwards: away from the way it went)
    run(f, w, 6, [](ForkCommand& c) {
      c.dir = 1;
      c.throttle = 1;
    }, [&] { return f.v > 4.5; });
    run(f, w, 4, [](ForkCommand& c) {
      c.brake = 1;
      c.throttle = 0;
    }, [&] { return !f.tipped.empty(); });
    return f.tipped;
  };
  assert(trial(false).empty());
  assert(trial(true) == "forward");
}

void fullLock() {
  // turning at full lock at walking pace: empty it stays up; with a load high up it goes over sideways
  struct Out {
    std::string tipped;
    double aLat;
  };
  const auto spin = [](bool withLoad) {
    Pallet p = pallet("d", 0, heelZ - 0.6 - 0.3, 0, 2000);
    const ForkWorld w = world(withLoad ? std::vector<Pallet*>{&p} : std::vector<Pallet*>{});
    Forklift f;
    f.place(0, 0, 0, 0);
    if (withLoad) {
      run(f, w, 6, [](ForkCommand& c) { c.throttle = 0.3; }, [&] { return engagement(f.forks(), p).depth > 0.97; });
      run(f, w, 1, [](ForkCommand& c) { c.brake = 1; });
      run(f, w, 10, [](ForkCommand& c) { c.lift = 1; }, [&] { return f.lift >= FORKLIFT.liftMax - 0.01; });
    }
    run(f, w, 5, [](ForkCommand& c) { c.throttle = 0.6; }, [&] { return f.v > 1.0; });
    // (the curve speed control holds it to about 1 m/s at full lock)
    run(f, w, 6, [](ForkCommand& c) {
      c.throttle = 0.6;
      c.steer = 1;
    }, [&] { return !f.tipped.empty(); });
    return Out{f.tipped, f.aLat};
  };
  const Out empty = spin(false);
  assert(empty.tipped.empty());
  assert(std::abs(empty.aLat) > 2.5);
  assert(spin(true).tipped == "side");
}

void fullSpeedTurn() {
  // turning sharply at full speed throws even an empty forklift over
  Forklift f;
  const ForkWorld w = world({});
  f.place(0, 0, 0, 0);
  run(f, w, 6, [](ForkCommand& c) { c.throttle = 1; }, [&] { return f.v > 5.2; });
  run(f, w, 3, [](ForkCommand& c) {
    c.throttle = 1;
    c.steer = -1;
  }, [&] { return !f.tipped.empty(); });
  assert(f.tipped == "side");
  // turning left, it falls outwards: to the right
  assert(f.rollSide == 1);
}

}  // namespace

int main() {
  loadChart();
  forwardStability();
  sideStability();
  pickCarrySetDown();
  notTaken();
  tooHeavy();
  brakedHigh();
  fullLock();
  fullSpeedTurn();
  std::printf("forklift: ok\n");
  return 0;
}

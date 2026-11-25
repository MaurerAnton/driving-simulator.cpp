// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim game world core platform geo
// Origin: tests/sim/wildlife.test.ts @ v1.7.7
/**
 * Deer and wild boar on the country roads (sim/wildlife): only at dusk
 * and at night, only outside towns where a forest meets the road ahead;
 * out of the forest, a moment at its edge, across and away – obstacles for
 * the traffic while they are at the road; gone again, and a while before
 * the next.
 */
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>

#include "sim/wildlife.h"
#include "tests/vehicle_harness.h"
#include "world/cover.h"

using namespace fs;
using fs::harness::ok;
using fs::harness::test;

namespace {

struct HostOpts {
  double sun = 0;
  bool forest = true;
  bool town = false;
};

/** A straight road north–south along x = 0, 7 m wide; forest on the east side (x > 6) where `forest`; a town where `town`. */
WildHost host(HostOpts o) {
  WildHost h;
  h.sun = [o] { return o.sun; };
  h.coverAt = [o](double x, double) { return o.forest && x > 6 ? COVER_FOREST : COVER_FARMLAND; };
  h.heightAt = [](double, double) { return 200.0; };
  h.road = [o](double x, double z, double reach, WildRoad& out) {
    if (std::abs(x) > reach + 3.5) return false;
    out.x = 0;
    out.z = z;
    out.dx = 0;
    out.dz = -1;
    out.half = 3.5;
    out.rural = !o.town;
    return true;
  };
  return h;
}

PlayerState northbound() {
  PlayerState p;
  p.x = 1.75;
  p.z = 0;
  p.yaw = 0;
  p.speed = 20;
  return p;
}

struct Seen {
  int spawned = 0;
  size_t boxes = 0;
  int crossed = 0;
  double maxX = -std::numeric_limits<double>::infinity();
  double minX = std::numeric_limits<double>::infinity();
};

/** Driving north along the road at 20 m/s for `secs` seconds; what was seen. */
Seen drive(Wildlife& w, double secs) {
  PlayerState p = northbound();
  Seen r;
  size_t had = 0;
  for (double t = 0; t < secs; t += 0.05) {
    w.update(0.05, p);
    p.z -= p.speed * 0.05;
    if (w.animals.size() > had) r.spawned += static_cast<int>(w.animals.size() - had);
    had = w.animals.size();
    r.boxes = std::max(r.boxes, w.boxes.size());
    for (const WildAnimal& a : w.animals) {
      r.maxX = std::max(r.maxX, a.x);
      r.minX = std::min(r.minX, a.x);
      if (a.phase == "away") r.crossed++;
    }
  }
  return r;
}

/** A fixed sequence of "random" numbers: always below the chance, so every look finds animals. */
double always() { return 0.01; }

void noneByDay() {
  test("none by day, none in town, none without a forest");
  {
    Wildlife w(host({.sun = 30}), always);
    ok(drive(w, 60).spawned == 0, "by day");
  }
  {
    Wildlife w(host({.sun = -3, .town = true}), always);
    ok(drive(w, 60).spawned == 0, "in town");
  }
  {
    Wildlife w(host({.sun = -3, .forest = false}), always);
    ok(drive(w, 60).spawned == 0, "without a forest");
  }
}

void atDusk() {
  test("at dusk by a forest: out of it, across the road and away to the other side – obstacles meanwhile");
  Wildlife w(host({.sun = -3}), always);
  const Seen r = drive(w, 40);
  ok(r.spawned >= 1, "animals came");
  ok(r.maxX > 10, "out of the forest on the east");
  ok(r.minX < -10, "over to the west");
  ok(r.crossed > 0, "and away");
  ok(r.boxes >= 1, "the traffic sees them");
}

void aheadOfTheCar() {
  test("they come ahead of the car, stand a moment at the edge, and run");
  Wildlife w(host({.sun = -10}), always);
  PlayerState p = northbound();
  std::optional<double> first;
  double waited = 0;
  double fastest = 0;
  for (double t = 0; t < 20; t += 0.05) {
    w.update(0.05, p);
    p.z -= p.speed * 0.05;
    if (w.animals.empty()) continue;
    const WildAnimal& a = w.animals[0];
    if (!first) first = a.z - p.z;
    if (a.phase == "wait") waited += 0.05;
    fastest = std::max(fastest, a.speed);
  }
  ok(first && *first < -60 && *first > -160, "ahead of the car (" + (first ? toFixed(*first, 0) : std::string("none")) + " m)");
  ok(waited > 0.15, "a moment at the edge");
  ok(fastest > 3.5 && fastest < 10.5, "running at " + toFixed(fastest, 1) + " m/s");
}

void rarely() {
  test("rarely: with ordinary chance, a long drive by a forest at night sees few groups; a pause after each");
  double seed = 7;
  Wildlife w(host({.sun = -12}), [&seed] {
    seed = std::fmod(seed * 16807, 2147483647);
    return seed / 2147483647;
  });
  const Seen r = drive(w, 600);
  // (ten minutes of forest at night: a look every 2.5 s, 5 % each, at least 100 s apart – a handful)
  ok(r.spawned <= 3 * 6, std::to_string(r.spawned) + " animals in ten minutes");
  Wildlife off(host({.sun = -3}), always);
  off.enabled = false;
  ok(drive(off, 30).spawned == 0, "switched off");
}

void goneAndCleared() {
  test("gone when the car is far away, and cleared with the traffic");
  Wildlife w(host({.sun = -3}), always);
  PlayerState p = northbound();
  for (double t = 0; t < 6 && w.animals.empty(); t += 0.05) w.update(0.05, p);
  ok(!w.animals.empty(), "they came");
  PlayerState far = p;
  far.z = 2000;
  w.update(0.05, far);
  ok(w.animals.empty(), "gone when far");
  for (double t = 0; t < 6 && w.animals.empty(); t += 0.05) w.update(0.05, p);
  ok(w.animals.empty(), "a while before the next");
  w.clear();
  ok(w.boxes.empty(), "cleared");
}

}  // namespace

int main() {
  std::printf("wildlife_test (tests/sim/wildlife.test.ts)\n");
  noneByDay();
  atDusk();
  aheadOfTheCar();
  rarely();
  goneAndCleared();
  std::printf("wildlife_test: all 5 cases passed\n");
  return 0;
}

// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim world core platform geo
// Origin: tests/sim/signals-timer.test.ts @ v1.7.7
/**
 * The timers over the traffic lights: how long a light still shows what it
 * shows, and a tap that puts its programme forward – through amber and
 * red-amber as always, never two crossing streams on green at once, never a
 * green cut to less than a few seconds.
 *
 * (And, kept from this file's first draft, a check of the scenery collision – the original has
 * no test file of its own for src/sim/collision.ts; tests/game/clearlanes.test.ts has one case,
 * mirrored in clearlanes_test.cpp.)
 */
#include <cmath>
#include <map>
#include <utility>
#include <vector>

#include "src/sim/collision.h"
#include "src/sim/signals.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using namespace fs::harness;

namespace {

/** A junction of two streams, as the map build makes it (tools/mapgen/control.ts), its programme starting at 0. */
SignalControllerJson junction(double off = 0) {
  SignalControllerJson c;
  c.x = 0;
  c.z = 0;
  c.groups = 2;
  c.off = off;
  c.phases = {{28, "GR"}, {3, "AR"}, {1, "RR"}, {1, "RY"}, {18, "RG"}, {3, "RA"}, {1, "RR"}, {1, "YR"}};
  return c;
}

/** A pedestrian crossing's light: green for the vehicles until somebody presses. */
SignalControllerJson crossing() {
  SignalControllerJson c;
  c.x = 0;
  c.z = 0;
  c.groups = 1;
  c.off = 0;
  c.ped = true;
  c.phases = {{48, "G"}, {3, "A"}, {13, "R"}, {1, "Y"}};
  return c;
}

const double SDT = 0.05;
bool near(double a, double b, double eps = 0.06) { return std::abs(a - b) <= eps; }

void timeLeft() {
  test("the time a group still shows its light, and what comes next");
  const std::vector<SignalControllerJson> list = {junction()};
  SignalSystem s(list);
  ok(s.get(0, 0) == SIG_GREEN, "green at first");
  SignalChange c = *s.change(0, 0);
  ok(near(c.left, 28), jsStr(c.left));
  ok(c.next == SIG_AMBER, "amber next");
  c = *s.change(0, 1);
  // the cross stream: red through the green, the amber and the moment of red for all of the main one
  ok(near(c.left, 32), jsStr(c.left));
  ok(c.next == SIG_RED_AMBER, "red-amber next");
  for (double t = 0; t < 10; t += SDT) s.update(SDT);
  ok(near(s.change(0, 0)->left, 18, 0.1), "18 s left");
  // through the amber: red next, in 3 s
  for (double t = 0; t < 19; t += SDT) s.update(SDT);
  ok(s.get(0, 0) == SIG_AMBER, "amber");
  c = *s.change(0, 0);
  ok(c.next == SIG_RED, "red next");
  ok(c.left > 0 && c.left <= 3, "within 3 s");
  // a controller that is out of order, or not there, has no timer
  SignalSystem dark(list, [](int) { return true; });
  ok(!dark.change(0, 0).has_value(), "out of order: no timer");
  ok(!s.change(7, 0).has_value(), "not there: no timer");
}

void tapEndsGreen() {
  test("a tap ends the green at once – then amber, red for all, red-amber, green for the others, as programmed");
  const std::vector<SignalControllerJson> list = {junction()};
  SignalSystem s(list);
  for (double t = 0; t < 10; t += SDT) s.update(SDT);
  ok(s.skip(0) == true, "the tap goes through");
  s.update(0);
  ok(s.get(0, 0) == SIG_AMBER, "the main stream gets amber at once");
  ok(s.get(0, 1) == SIG_RED, "the cross stream still red");
  struct Seen {
    double t;
    int a;
    int b;
  };
  std::vector<Seen> seen;
  for (double t = 0; t < 8; t += SDT) {
    s.update(SDT);
    if (seen.empty() || seen.back().a != s.get(0, 0) || seen.back().b != s.get(0, 1)) seen.push_back({t, s.get(0, 0), s.get(0, 1)});
  }
  const std::vector<std::pair<int, int>> want = {{SIG_AMBER, SIG_RED}, {SIG_RED, SIG_RED}, {SIG_RED, SIG_RED_AMBER}, {SIG_RED, SIG_GREEN}};
  ok(seen.size() == want.size(), "four states");
  for (size_t i = 0; i < want.size(); i++) ok(seen[i].a == want[i].first && seen[i].b == want[i].second, "state " + std::to_string(i));
  // the amber had its full three seconds
  ok(near(seen[1].t, 3, 0.1), "amber for " + jsStr(seen[1].t) + " s");
}

void leastGreen() {
  test("a green that has only just begun runs its least time first; amber and clearance are never cut");
  const std::vector<SignalControllerJson> list = {junction()};
  SignalSystem s(list);
  for (double t = 0; t < 2; t += SDT) s.update(SDT);
  ok(s.skip(0) == true, "the tap goes through");
  const SignalChange c = *s.change(0, 0);
  ok(near(c.left, SKIP_LEAST - 2, 0.1), "still " + jsStr(c.left) + " s of green");
  // a second tap changes nothing more
  ok(s.skip(0) == false, "a second tap changes nothing");
  for (double t = 0; t < SKIP_LEAST - 2 + 0.2; t += SDT) s.update(SDT);
  ok(s.get(0, 0) == SIG_AMBER, "amber");
  // during the amber a tap does nothing
  const double before = s.change(0, 0)->left;
  ok(s.skip(0) == false, "no tap during amber");
  ok(near(s.change(0, 0)->left, before), "the time is as before");
}

void tappedAtRandom() {
  test("tapped at random for ten minutes: never two streams moving at once, every change in the German order");
  const std::vector<SignalControllerJson> list = {junction(17)};
  SignalSystem s(list);
  const std::map<int, int> order = {{SIG_GREEN, SIG_AMBER}, {SIG_AMBER, SIG_RED}, {SIG_RED, SIG_RED_AMBER}, {SIG_RED_AMBER, SIG_GREEN}};
  int prev[2] = {s.get(0, 0), s.get(0, 1)};
  double began[2] = {0, 0};
  double rnd = 12345;
  // (rnd = (rnd * 1103515245 + 12345) & 0x7fffffff, in the numbers of JavaScript: the product is rounded to 53 bits first)
  const auto random = [&rnd]() {
    rnd = static_cast<double>(jsInt32(rnd * 1103515245 + 12345) & 0x7fffffff);
    return rnd / 0x7fffffff;
  };
  int skips = 0;
  for (double t = 0; t < 600; t += SDT) {
    if (random() < 0.01 && s.skip(0)) skips++;
    s.update(SDT);
    const int now[2] = {s.get(0, 0), s.get(0, 1)};
    const int moving = (now[0] != SIG_RED ? 1 : 0) + (now[1] != SIG_RED ? 1 : 0);
    ok(moving <= 1, "at " + toFixed(t, 2) + " s both streams may move");
    for (int g = 0; g < 2; g++) {
      if (now[g] == prev[g]) continue;
      ok(now[g] == order.at(prev[g]), "group " + std::to_string(g) + ": " + std::to_string(prev[g]) + " → " + std::to_string(now[g]) + " at " + toFixed(t, 2) + " s");
      // green and amber run their least time
      if (prev[g] == SIG_GREEN) ok(t - began[g] >= SKIP_LEAST - 0.1, "green for only " + toFixed(t - began[g], 2) + " s");
      if (prev[g] == SIG_AMBER) ok(t - began[g] >= 3 - 0.1, "amber for only " + toFixed(t - began[g], 2) + " s");
      began[g] = t;
    }
    prev[0] = now[0];
    prev[1] = now[1];
  }
  ok(skips > 20, std::to_string(skips) + " taps went through");
}

void timeAgrees() {
  test("the time left agrees with what the lights then do");
  const std::vector<SignalControllerJson> list = {junction(5)};
  SignalSystem s(list);
  for (int k = 0; k < 12; k++) {
    const SignalChange c = *s.change(0, 1);
    const int was = s.get(0, 1);
    double t = 0;
    while (s.get(0, 1) == was && t < 120) {
      s.update(SDT);
      t += SDT;
    }
    ok(near(t, c.left, 0.1), "said " + toFixed(c.left, 2) + " s, took " + toFixed(t, 2) + " s");
    ok(s.get(0, 1) == c.next, "and shows what was said");
    if (k % 3 == 1) s.skip(0);
  }
}

void crossingAtRest() {
  test("a pedestrian crossing at rest has no time; a tap asks for the pedestrians as the button does");
  const std::vector<SignalControllerJson> list = {crossing()};
  SignalSystem s(list);
  for (double t = 0; t < 20; t += SDT) s.update(SDT);
  ok(s.get(0, 0) == SIG_GREEN, "green for the vehicles");
  ok(s.change(0, 0)->left == JS_INF, "no time at rest");
  ok(s.change(0, 0)->next == SIG_AMBER, "amber next");
  // the pedestrians wait for the button too
  ok(s.pedestrianChange(0, 0)->left == JS_INF, "the pedestrians wait for the button");
  ok(s.skip(0) == true, "a tap asks");
  ok(s.requested(0) == true, "the request is taken");
  s.update(SDT);
  ok(s.get(0, 0) == SIG_AMBER, "after a long green the request is served at once");
  // red for the vehicles: the pedestrians' green comes a moment later, and lasts until the vehicles' red-amber
  for (double t = 0; t < 3.2; t += SDT) s.update(SDT);
  ok(s.get(0, 0) == SIG_RED, "red for the vehicles");
  SignalChange p = *s.pedestrianChange(0, 0);
  ok(p.next == SIG_GREEN, "green for the people next");
  ok(p.left <= 1.5, "within 1.5 s");
  for (double t = 0; t < 2; t += SDT) s.update(SDT);
  ok(s.pedestrian(0, 0) == "green", "the people have green");
  p = *s.pedestrianChange(0, 0);
  ok(p.next == SIG_RED, "red for them next");
  ok(near(p.left, s.change(0, 0)->left), "when the vehicles' light changes");
}

void crossingLeastGreen() {
  test("a tap on a crossing that has just turned green for the vehicles waits for their least green");
  const std::vector<SignalControllerJson> list = {crossing()};
  SignalSystem s(list);
  s.update(SDT);
  s.request(0);
  // round once: amber, red, red-amber, green again
  double t = 0;
  while (!(s.get(0, 0) == SIG_GREEN && s.age(0, 0) > 0 && t > 5) && t < 60) {
    s.update(SDT);
    s.pedestrian(0, 0);
    t += SDT;
  }
  ok(s.get(0, 0) == SIG_GREEN, "green again");
  for (int k = 0; k < 20; k++) s.update(SDT);
  ok(s.skip(0) == true, "the tap asks");
  const SignalChange c = *s.change(0, 0);
  ok(c.left > 5 && c.left <= 8, "served after the least green: " + jsStr(c.left));
}

void greenArrow() {
  test("the green arrow: when it comes on and goes out, also after a tap");
  const std::vector<SignalControllerJson> list = {junction()};
  SignalSystem s(list);
  // an arrow beside the light of the main stream: lit while the main stream has red and the cross stream green
  Arm arm;
  Arrow a;
  a.controller = 0;
  a.own = 0;
  a.friends = {1};
  a.state = -1;
  a.since = 0;
  s.arrows.set(&arm, a);
  s.update(0);
  SignalChange c = *s.arrowChange(&arm);
  ok(c.next == SIG_GREEN, "green next");
  // it comes on once the main stream has had red for a moment (and the cross stream has green)
  double t = 0;
  while (s.arrow(&arm) != SIG_GREEN && t < 120) {
    s.update(SDT);
    t += SDT;
  }
  ok(near(t, c.left, 0.35), "said " + toFixed(c.left, 2) + " s, came on after " + toFixed(t, 2) + " s");
  c = *s.arrowChange(&arm);
  ok(c.next == SIG_AMBER, "amber next");
  // a tap ends the cross stream's green: the arrow goes out sooner, through its amber
  ok(s.skip(0), "the tap goes through");
  const SignalChange sooner = *s.arrowChange(&arm);
  ok(sooner.left < c.left, jsStr(sooner.left) + " < " + jsStr(c.left));
  t = 0;
  while (s.arrow(&arm) == SIG_GREEN && t < 60) {
    s.update(SDT);
    t += SDT;
  }
  ok(s.arrow(&arm) == SIG_AMBER, "amber");
  ok(near(t, sooner.left, 0.35), "said " + toFixed(sooner.left, 2) + " s, went out after " + toFixed(t, 2) + " s");
  const SignalChange amber = *s.arrowChange(&arm);
  ok(amber.next == -1, "dark next");
  ok(amber.left <= 3, "within 3 s");
  Arm other;
  ok(!s.arrowChange(&other).has_value(), "no arrow, no timer");
}

void replay() {
  test("a replay puts a tapped programme back as it was");
  const std::vector<SignalControllerJson> list = {junction()};
  SignalSystem s(list);
  for (double t = 0; t < 10; t += SDT) s.update(SDT);
  const SignalSnapshot before = s.snapshot(0, 0, 100);
  s.skip(0);
  for (double t = 0; t < 1; t += SDT) s.update(SDT);
  const SignalSnapshot after = s.snapshot(0, 0, 100);
  ok(s.get(0, 0) == SIG_AMBER, "amber after the tap");
  s.restore(before);
  s.update(SDT);
  ok(s.get(0, 0) == SIG_GREEN, "back before the tap");
  s.restore(after);
  s.update(SDT);
  ok(s.get(0, 0) == SIG_AMBER, "and after it again");
}

void sceneryCollision() {
  test("(collision) the car is pushed out of a wall and loses its speed into it; without the wall it drives on");
  PlaneGround g;
  Vehicle v = makeCar(g);
  sprintDriver d;
  // a wall running east–west 40 m north of the car (free-standing: its base at the height of the road)
  SceneryCollider col;
  const std::vector<float> wall = {-20, -40, 20, -40, 0};
  CollisionSource src;
  src.key = "wall";
  src.segments = &wall;
  col.add(src);
  double impact = 0;
  double fastest = 0;
  double t = 0;
  for (int i = 0; i < 240 * 12; i++) {
    v.step(DT, d(t, v));
    t += DT;
    fastest = jsMax(fastest, v.speed);
    impact = jsMax(impact, col.resolve(v));
  }
  ok(fastest > 8, "it got going (" + toFixed(fastest, 1) + " m/s)");
  ok(impact > 4, "it hit the wall (" + toFixed(impact, 1) + " m/s)");
  ok(col.lastBuilding == 0, "a free-standing wall, not a building");
  double mpos[3];
  v.getModelPosition(mpos);
  ok(mpos[2] > -40, "never through the wall (z " + toFixed(mpos[2], 2) + ")");
  ok(col.blocked(0, 0, -39.5, 0), "the place at the wall is taken");
  ok(!col.blocked(0, 0, -30, 0), "ten metres before it is free");
  ok(col.circleBlocked(0, 0, -39.8, 0.3) && !col.circleBlocked(0, 0, -38, 0.3), "somebody on foot: at the wall, not before it");
  // the tile goes: the car drives on
  col.remove("wall");
  // (pushing against the wall in gear has stalled the engine: started again)
  v.resetDrivetrain(true);
  sprintDriver again;
  t = 0;
  for (int i = 0; i < 240 * 6; i++) {
    v.step(DT, again(t, v));
    t += DT;
    col.resolve(v);
  }
  v.getModelPosition(mpos);
  ok(mpos[2] < -42, "past the wall's line (z " + toFixed(mpos[2], 2) + ")");
}

}  // namespace

int main() {
  std::printf("sim_test (tests/sim/signals-timer.test.ts)\n");
  timeLeft();
  tapEndsGreen();
  leastGreen();
  tappedAtRandom();
  timeAgrees();
  crossingAtRest();
  crossingLeastGreen();
  greenArrow();
  replay();
  sceneryCollision();
  std::printf("sim_test: all 10 cases passed\n");
  return 0;
}

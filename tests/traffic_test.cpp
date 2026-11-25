// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim game world core platform geo
// Origin: tests/game/signals.test.ts @ v1.7.7
/**
 * Traffic lights beyond the three lamps: lights that are out of order
 * (flashing amber – the signs count) and the green arrow for turning right –
 * what the traffic and the rule monitor make of them.
 *
 */
#include <cmath>
#include <functional>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

#include "core/geom.h"
#include "sim/arrows.h"
#include "sim/rules.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "world/lanes.h"
#include "tests/sim_roads.h"
#include "tests/town.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using fs::harness::ok;
using fs::harness::test;

namespace {

const double TDT = 1.0 / 30;
const double BLOCK = 160;

/** The lane of a street that arrives at the junction (i, j), coming from (i0, j0). */
Lane* laneTo(Town& town, int i0, int j0, int i, int j) {
  const LaneGraph::EdgeLanes& el = *town.lanes->edgeLanes[town.street(i0, j0, i, j)];
  const Node* node = town.world->nodes[town.node(i, j)];
  for (Lane* l : el.fwd)
    if (l->to == node) return l;
  for (Lane* l : el.back)
    if (l->to == node) return l;
  return nullptr;
}

/**
 * A town whose middle junction has lights.  `tee`: without the street to the
 * north of it – the main road (west–east) with a side road joining from the
 * south.
 */
struct LitTown {
  Town town;
  SignalSystem signals;
  explicit LitTown(bool tee = false, bool out = false, bool noSigns = false) : town(make(tee, out, noSigns)), signals(town.world->signals, town.world->lightsOut) {}
  static Town make(bool tee, bool out, bool noSigns) {
    TownOptions o;
    o.cols = 3;
    o.rows = 3;
    o.block = BLOCK;
    o.pavement = 2;
    o.lights = {{1, 1}};
    if (tee) o.missing = {{1, 0, 1, 1}};
    if (out) o.lightsOut = [](int) { return true; };
    o.lightsUnsigned = noSigns;
    return makeTown(o);
  }
  std::vector<Node*> allNodes() {
    std::vector<Node*> out;
    for (Node* n : town.world->allNodes) out.push_back(n);
    return out;
  }
  Node& node(int i, int j) { return *town.world->nodes[town.node(i, j)]; }
};

Connector* outOf(Lane* lane, int turn) {
  for (Connector* c : lane->out)
    if (c->turn == turn) return c;
  return nullptr;
}

void whichLightsAreOut() {
  test("which lights are out of order: a share of them, the same all day, others the next");
  const auto today = lightsOutBy(20261116, 0.06);
  const auto again = lightsOutBy(20261116, 0.06);
  const auto tomorrow = lightsOutBy(20261117, 0.06);
  int n = 0;
  int same = 0;
  int both = 0;
  for (int i = 0; i < 4000; i++) {
    if (today(i)) n++;
    if (today(i) == again(i)) same++;
    if (today(i) && tomorrow(i)) both++;
  }
  ok(same == 4000, "the same all day");
  ok(n > 4000 * 0.04 && n < 4000 * 0.08, "about six in a hundred (" + std::to_string(n) + " of 4000)");
  ok(both < n * 0.25, "mostly others the next day (" + std::to_string(both) + " of " + std::to_string(n) + " again)");
  const auto none = lightsOutBy(1, 0);
  const auto all = lightsOutBy(1, 1);
  int out0 = 0;
  int out1 = 0;
  for (int i = 0; i < 500; i++) {
    if (none(i)) out0++;
    if (all(i)) out1++;
  }
  ok(out0 == 0, "a share of nothing: none");
  ok(out1 == 500, "a share of all: every one");
}

/** whether `from` gives way to `to` where their paths meet (−1: they do not meet) */
int yieldsTo(const Connector* from, const Connector* to) {
  for (const Conflict& k : from->conflicts)
    if (k.other == to) return k.yield ? 1 : 0;
  return -1;
}

void lightsOutSigns() {
  test("a junction whose lights are out is ruled by its signs: the heads stand and flash, the side road gives way");
  LitTown t(false, true);
  Node& node = t.node(1, 1);
  ok(node.dark == true, "its lights are out");
  ok(node.ctrl == CTRL_SIGNS, "ruled by its signs");
  for (const Arm* arm : node.arms) {
    ok(arm->sg == -1, "no light rules the approach");
    ok(arm->lamp >= 0, "but its head still stands there");
  }
  ok(t.signals.isDark(node.sc) == true, "the controller is dark");
  for (double time = 0; time < 70; time += 0.5) {
    t.signals.update(0.5);
    ok(t.signals.get(node.sc, 0) == SIG_FLASH, "the main road's light flashes");
    ok(t.signals.get(node.sc, 1) == SIG_FLASH, "the side road's light flashes");
  }
  // who gives way to whom, as at any junction with signs
  t.town.lanes->resolveNode(node.id);
  const Connector* side = outOf(laneTo(t.town, 1, 0, 1, 1), TURN_STRAIGHT);
  const Connector* main = outOf(laneTo(t.town, 0, 1, 1, 1), TURN_STRAIGHT);
  ok(yieldsTo(side, main) == 1, "from the side road: wait");
  ok(yieldsTo(main, side) == 0, "on the main road: go");

  // the same junction with its lights working: the lights keep the two apart, nobody gives way
  LitTown fine;
  Node& n2 = fine.node(1, 1);
  ok(n2.dark == false, "its lights work");
  ok(n2.ctrl == CTRL_SIGNALS, "ruled by its lights");
  fine.town.lanes->resolveNode(n2.id);
  const Connector* side2 = outOf(laneTo(fine.town, 1, 0, 1, 1), TURN_STRAIGHT);
  const Connector* main2 = outOf(laneTo(fine.town, 0, 1, 1, 1), TURN_STRAIGHT);
  ok(yieldsTo(side2, main2) == 0, "nobody gives way");
  for (const Arm* a : n2.arms) ok(a->sg == a->lamp && a->sg >= 0, "each approach has its light, and it rules");
}

void lightsOutNoSigns() {
  test("lights out and no signs under them: right before left");
  LitTown t(false, true, true);
  Node& node = t.node(1, 1);
  ok(node.ctrl == CTRL_RBL, "right before left");
  for (const Arm* a : node.arms) ok(a->role == ROLE_RBL, "on every approach");
}

void trafficWithLightsOut() {
  test("the traffic gets through a junction whose lights are out – the side road waits, nobody collides");
  LitTown t(false, true);
  Traffic traffic(*t.town.world, *t.town.lanes, t.signals);
  traffic.targetCount = 16;
  const Node& node = t.node(1, 1);
  const PlayerState player{.x = node.x, .y = node.y + 40, .z = node.z, .yaw = 0, .speed = 0, .length = 4.3, .width = 1.8};
  int main = 0;
  int side = 0;
  std::unordered_set<int64_t> inside;
  int overlaps = 0;
  for (int step = 0; step < 30 * 360; step++) {
    t.signals.update(TDT);
    traffic.update(TDT, player);
    for (const TrafficCar* car : traffic.cars) {
      if (car->conn && car->conn->node == &node && !inside.count(car->id)) {
        inside.insert(car->id);
        if (car->conn->from->armTo->role == ROLE_PRIORITY) main++;
        else side++;
      } else if (!car->conn) {
        inside.erase(car->id);
      }
    }
    if (step % 5 != 0) continue;
    const std::vector<TrafficCar*>& cars = traffic.cars;
    for (size_t i = 0; i < cars.size(); i++) {
      for (size_t k = i + 1; k < cars.size(); k++) {
        const TrafficCar& a = *cars[i];
        const TrafficCar& b = *cars[k];
        if (std::abs(a.x - b.x) > 6 || std::abs(a.z - b.z) > 6) continue;
        if (boxesOverlap(a.x, a.z, a.yaw, a.length / 2 - 0.3, a.width / 2 - 0.25, b.x, b.z, b.yaw, b.length / 2 - 0.3, b.width / 2 - 0.25)) overlaps++;
      }
    }
  }
  ok(main >= 5 && side >= 3, "both streets get through (" + std::to_string(main) + " from the main road, " + std::to_string(side) + " from the side road)");
  ok(overlaps == 0, "no car in another");
}

void whereAnArrowHangs() {
  test("a green arrow hangs where turning right is in nobody's way: on the main road of a T-junction, towards the side road");
  LitTown t(true);
  Node& node = t.node(1, 1);
  ok(node.arms.size() == 3, "a T-junction");
  ok(markArrows(*t.town.world, *t.town.lanes, t.signals, t.allNodes()) == 1, "one arrow");
  Lane* east = laneTo(t.town, 0, 1, 1, 1);
  const Arrow* arrow = t.signals.arrows.get(east->armTo);
  ok(arrow != nullptr, "from the west, the side road is on the right");
  // the only ones in the way: those who come the other way and turn left into the side road – the same light
  ok(arrow->foes == std::vector<int>{0}, "its foes: the main road's own group");
  ok(arrow->own == 0, "beside the main road's light");
  ok(arrow->friends.empty(), "no friends");
  // not from the east (nothing to the right there), not from the side road (its right turn joins the main road's traffic)
  ok(!findArrow(*t.town.world, *t.town.lanes, t.signals, node, *laneTo(t.town, 2, 1, 1, 1)->armTo).has_value(), "none from the east");
  ok(!findArrow(*t.town.world, *t.town.lanes, t.signals, node, *laneTo(t.town, 1, 2, 1, 1)->armTo).has_value(), "none from the side road");
  // (asked again, nothing is put up twice)
  ok(markArrows(*t.town.world, *t.town.lanes, t.signals, t.allNodes()) == 0, "nothing is put up twice");

  // at a crossing of two streets the right turn is never free: no arrow anywhere
  LitTown cross;
  ok(markArrows(*cross.town.world, *cross.town.lanes, cross.signals, cross.allNodes()) == 0, "no arrow at a crossing of two streets");
}

void whenTheArrowIsLit() {
  test("the arrow is lit while the main light is red and the side road has its turn – never with the full green");
  LitTown t(true);
  markArrows(*t.town.world, *t.town.lanes, t.signals, t.allNodes());
  const Arm* arm = laneTo(t.town, 0, 1, 1, 1)->armTo;
  const int sc = t.node(1, 1).sc;
  double lit = 0;
  double amber = 0;
  double first = -1;
  for (double time = 0; time < 56; time += 0.1) {
    t.signals.update(0.1);
    const int a = t.signals.arrow(arm);
    const int main = t.signals.get(sc, 0);
    if (a == SIG_GREEN) {
      lit += 0.1;
      if (first < 0) first = t.signals.time;
      ok(main == SIG_RED || main == SIG_RED_AMBER, "only while the main light is red");
      ok(main == SIG_RED_AMBER || t.signals.age(sc, 0) >= 1.5 - 1e-6, "and has been for a moment: the junction is clear");
    } else if (a == SIG_AMBER) {
      amber += 0.1;
    }
  }
  ok(std::abs(lit - 23.5) < 0.3, "for the rest of the red (" + toFixed(lit, 1) + " s of a cycle of 56 s)");
  ok(std::abs(first - (28 + 3 + 1.5)) < 0.25, "from a moment after the main light turned red (at " + toFixed(first, 1) + " s)");
  ok(amber < 0.05, "the full green follows: no amber arrow before it");
}

void amberFirst() {
  test("an arrow that ends while the main light stays red shows amber first");
  LitTown t(true);
  const Arm* arm = laneTo(t.town, 0, 1, 1, 1)->armTo;
  const int sc = t.node(1, 1).sc;
  // (as where people cross the road the turn leads into: lit only while the vehicles there have green)
  Arrow made;
  made.controller = sc;
  made.own = 0;
  made.friends = {1};
  made.state = -1;
  made.since = 0;
  t.signals.arrows.set(arm, made);
  std::vector<int> seen;
  double green = 0;
  double amber = 0;
  for (double time = 0; time < 56; time += 0.1) {
    t.signals.update(0.1);
    const int a = t.signals.arrow(arm);
    if (seen.empty() || seen.back() != a) seen.push_back(a);
    if (a == SIG_GREEN) {
      green += 0.1;
      ok(t.signals.get(sc, 1) == SIG_GREEN, "lit only while the side road has green");
    } else if (a == SIG_AMBER) {
      amber += 0.1;
    }
  }
  ok(seen == (std::vector<int>{-1, SIG_GREEN, SIG_AMBER, -1}), "dark, green, amber, dark");
  ok(std::abs(green - 18) < 0.3 && std::abs(amber - 3) < 0.3, toFixed(green, 1) + " s green, " + toFixed(amber, 1) + " s amber");
  ok(std::abs(t.signals.arrowTime(sc, 0, {}, {1}) - 18) < 0.6, "the time it would be lit");
}

/** a lane or a path through a junction */
struct Part {
  Lane* lane = nullptr;
  Connector* conn = nullptr;
  double len() const { return lane ? lane->len : conn->len; }
};

/** Drive a car along `parts` (a lane, a path through the junction, a lane) under the eyes of the instructor. */
std::vector<std::string> drive(Town& town, SignalSystem& signals, const std::vector<Part>& parts, double from, int indicator) {
  Traffic traffic(*town.world, *town.lanes, signals);
  traffic.targetCount = 0;
  RuleMonitor rules(*town.world, *town.lanes, signals, traffic);
  PolySample smp = newPolySample();
  double total = 0;
  for (const Part& p : parts) total = total + p.len();
  const double speed = 6;
  for (double d = from; d < total - 4; d += speed * TDT) {
    double rest = d;
    Part part = parts[0];
    for (const Part& p : parts) {
      part = p;
      if (rest <= p.len()) break;
      rest -= p.len();
    }
    if (part.conn) smp = samplePolyline(part.conn->pl, jsMin(rest, part.len()), smp);
    else smp = lanePoint(*part.lane, jsMin(rest, part.len()), smp);
    // (indicator set and the look over the shoulder done in good time: only the light is in question here)
    PlayerSnapshot snap;
    snap.x = smp.x;
    snap.y = smp.y;
    snap.z = smp.z;
    snap.yaw = jsAtan2(-smp.tx, -smp.tz);
    snap.speed = speed;
    snap.indicator = indicator;
    snap.hazard = false;
    snap.lookYaw = indicator > 0 && d - from > 8 && d - from < 20 ? -1.3 : 0;
    snap.seatbelt = true;
    snap.handbrake = 0;
    snap.stalled = false;
    snap.hitScenery = 0;
    snap.length = 4.3;
    rules.update(TDT, snap, {});
  }
  std::vector<std::string> out;
  for (const Violation& v : rules.log) out.push_back(v.severity + ":" + v.id);
  return out;
}

bool includes(const std::vector<std::string>& list, const std::string& what) {
  for (const std::string& s : list)
    if (s == what) return true;
  return false;
}

std::string joined(const std::vector<std::string>& list) {
  std::string out;
  for (const std::string& s : list) out += (out.empty() ? "" : ", ") + s;
  return out;
}

void overTheLineOnTheArrow() {
  test("over the line on red with the green arrow lit: fine for whoever turns right – a red-light offence for whoever drives straight on");
  LitTown t(true);
  markArrows(*t.town.world, *t.town.lanes, t.signals, t.allNodes());
  Lane* east = laneTo(t.town, 0, 1, 1, 1);
  Connector* right = outOf(east, TURN_RIGHT);
  Connector* straight = outOf(east, TURN_STRAIGHT);
  ok(right && straight, "a right turn and a way straight on");
  const int sc = east->to->sc;
  const auto reach = [&](double when) {
    while (std::fmod(t.signals.time, 56) < when || std::fmod(t.signals.time, 56) > when + 0.2) t.signals.update(0.05);
  };
  // the side road's green has just begun: the main light is red, the arrow is lit for a good while
  reach(35);
  ok(t.signals.get(sc, 0) == SIG_RED, "the main light is red");
  ok(t.signals.arrow(east->armTo) == SIG_GREEN, "the arrow is lit");
  const double from = east->stopAt - 30;
  const std::vector<std::string> turned = drive(t.town, t.signals, {Part{east, nullptr}, Part{nullptr, right}, Part{right->to, nullptr}}, from, 1);
  bool light = false;
  for (const std::string& f : turned)
    if (f.find("redLight") != std::string::npos || f.find("stopLine") != std::string::npos || f.find("amberLight") != std::string::npos) light = true;
  ok(!light, "turning right on the arrow: the light is no matter (" + joined(turned) + ")");
  reach(35);
  const std::vector<std::string> faults = drive(t.town, t.signals, {Part{east, nullptr}, Part{nullptr, straight}, Part{straight->to, nullptr}}, from, 0);
  ok(includes(faults, "fail:redLight"), "straight on, the arrow is not for this car (" + (faults.empty() ? std::string("no fault") : joined(faults)) + ")");
  // without an arrow the same right turn on red is what it always was
  LitTown plain(true);
  Lane* e2 = laneTo(plain.town, 0, 1, 1, 1);
  while (plain.signals.time < 35) plain.signals.update(0.05);
  Connector* r2 = outOf(e2, TURN_RIGHT);
  ok(includes(drive(plain.town, plain.signals, {Part{e2, nullptr}, Part{nullptr, r2}, Part{r2->to, nullptr}}, e2->stopAt - 30, 1), "fail:redLight"), "without an arrow: a red-light offence");
}

void trafficOnTheArrow() {
  test("the traffic turns right on the green arrow – and only then goes on red");
  LitTown t(true);
  markArrows(*t.town.world, *t.town.lanes, t.signals, t.allNodes());
  Traffic traffic(*t.town.world, *t.town.lanes, t.signals);
  traffic.targetCount = 18;
  const Node& node = t.node(1, 1);
  const PlayerState player{.x = node.x, .y = node.y + 40, .z = node.z, .yaw = 0, .speed = 0, .length = 4.3, .width = 1.8};
  std::unordered_set<int64_t> inside;
  int onArrow = 0;
  int onRed = 0;
  for (int step = 0; step < 30 * 1200; step++) {
    t.signals.update(TDT);
    traffic.update(TDT, player);
    for (const TrafficCar* car : traffic.cars) {
      const Connector* c = car->conn;
      if (!c || c->node != &node) {
        inside.erase(car->id);
        continue;
      }
      if (inside.count(car->id)) continue;
      inside.insert(car->id);
      const Arm* arm = c->from->armTo;
      const int st = t.signals.get(node.sc, arm->sg);
      if (st != SIG_RED) continue;
      // (into the junction on red: only with the arrow – lit, or amber for whoever was too near to stop)
      if (c->turn == TURN_RIGHT && t.signals.arrow(arm) >= 0) onArrow++;
      else if (t.signals.age(node.sc, arm->sg) > 1) onRed++;
    }
  }
  ok(onRed == 0, "nobody runs the red light (" + std::to_string(onRed) + ")");
  ok(onArrow >= 2, "cars turned right on the arrow (" + std::to_string(onArrow) + " in twenty minutes)");
}

void carsAppear() {
  test("(kept from this file's first draft) on two roads from a junction cars appear, drive along their lanes within the limit, and keep apart");
  // two roads of 400 m from a junction at the origin, east and south – as the draft's test had them
  roads::Net net = roads::makeNet({{0, 0}, {400, 0}, {0, 400}}, {roads::Road{.a = 0, .b = 1, .cls = RESIDENTIAL}, roads::Road{.a = 0, .b = 2, .cls = RESIDENTIAL}});
  const Sparse<SignalControllerJson> none;
  SignalSystem signals(none);
  Traffic traffic(*net.world, *net.lanes, signals);
  const PlayerState player{.x = 0, .y = 100, .z = 0, .yaw = 0, .speed = 0, .length = 4.3, .width = 1.8};
  for (int i = 0; i < 600; i++) traffic.update(1.0 / 60, player);
  ok(!traffic.cars.empty(), "cars appear (" + std::to_string(traffic.cars.size()) + ")");
  for (const TrafficCar* c : traffic.cars) {
    ok(c->lane != nullptr || c->conn != nullptr, "each on a lane or a path");
    ok(c->v >= 0 && c->v < 15.5, "within the limit of 50 km/h and what its driver adds (" + jsStr(c->v) + " m/s)");
    ok(std::isfinite(c->x) && std::isfinite(c->z) && c->yaw == c->yaw, "its place is a number");
  }
  for (size_t i = 0; i < traffic.cars.size(); i++) {
    for (size_t j = i + 1; j < traffic.cars.size(); j++) {
      const TrafficCar& a = *traffic.cars[i];
      const TrafficCar& b = *traffic.cars[j];
      ok(!boxesOverlap(a.x, a.z, a.yaw, a.length / 2 - 0.25, a.width / 2 - 0.2, b.x, b.z, b.yaw, b.length / 2 - 0.25, b.width / 2 - 0.2), "no car in another");
    }
  }
}

}  // namespace

int main() {
  std::printf("traffic_test (tests/game/signals.test.ts)\n");
  whichLightsAreOut();
  lightsOutSigns();
  lightsOutNoSigns();
  trafficWithLightsOut();
  whereAnArrowHangs();
  whenTheArrowIsLit();
  amberFirst();
  overTheLineOnTheArrow();
  trafficOnTheArrow();
  carsAppear();
  std::printf("traffic_test: all 10 cases passed\n");
  return 0;
}

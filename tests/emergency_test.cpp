// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim game world core platform geo audio
// Origin: tests/sim/emergency.test.ts @ v1.7.7
/**
 * Emergency vehicles and the giving way to them: who pulls over and how,
 * the corridor on a road with two lanes in each direction, the junction an
 * emergency vehicle crosses on red, the player's faults, and the calls that
 * come by themselves.
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "audio/siren.h"
#include "core/geom.h"
#include "core/js.h"
#include "sim/emergency.h"
#include "sim/rules.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "tests/town.h"
#include "tests/vehicle_harness.h"
#include "world/lanes.h"

using namespace fs;
using fs::harness::ok;
using fs::harness::test;

namespace {

const double DT = 1.0 / 30;

/** The lane of the street from grid point (i0, j0) to (i1, j1) that runs that way. */
Lane* laneOf(Town& town, int i0, int j0, int i1, int j1) {
  const LaneGraph::EdgeLanes& el = *town.lanes->edgeLanes[town.street(i0, j0, i1, j1)];
  const Node* to = town.world->nodes[town.node(i1, j1)];
  for (const std::vector<Lane*>* side : {&el.fwd, &el.back})
    for (Lane* l : *side)
      if (l->to == to && l->idx == 0) return l;
  return nullptr;
}

struct Way {
  Lane* start = nullptr;
  std::vector<Connector*> route;
};

/** The connectors from lane to lane along the grid points given (the first lane's end onwards). */
Way wayAlong(Town& town, const std::vector<std::array<int, 2>>& pts) {
  std::vector<Lane*> lanes;
  for (size_t k = 0; k + 1 < pts.size(); k++) lanes.push_back(laneOf(town, pts[k][0], pts[k][1], pts[k + 1][0], pts[k + 1][1]));
  Way w;
  for (size_t k = 0; k + 1 < lanes.size(); k++) {
    Connector* c = nullptr;
    for (Connector* q : lanes[k]->out)
      if (q->to->edge == lanes[k + 1]->edge) {
        c = q;
        break;
      }
    ok(c != nullptr, "a way from lane " + std::to_string(k) + " on");
    w.route.push_back(c);
  }
  w.start = lanes[0];
  return w;
}

/** A town with traffic but no cars appearing by themselves, and emergency vehicles only when sent. */
struct SetUp {
  Town town;
  SignalSystem signals;
  Traffic traffic;
  Emergencies em;
  explicit SetUp(const TownOptions& opt) : town(makeTown(opt)), signals(town.world->signals, town.world->lightsOut), traffic(*town.world, *town.lanes, signals), em(traffic) {
    traffic.targetCount = 0;
    em.autoCalls = false;
    em.patrolShare = 0;
    traffic.emergency = &em;
  }
};

TownOptions row(int cols, double block) {
  TownOptions o;
  o.cols = cols;
  o.rows = 1;
  o.block = block;
  return o;
}

std::vector<std::array<int, 4>> streetsOfRow() {
  std::vector<std::array<int, 4>> streets;
  for (int i = 0; i < 5; i++) streets.push_back({i, 0, i + 1, 0});
  return streets;
}

/** Somewhere above the town: the traffic keeps clear of nobody, nothing is spawned or removed for being far. */
PlayerState anchor(double x, double z) {
  PlayerState p;
  p.x = x;
  p.y = 130;
  p.z = z;
  p.length = 4.3;
  p.width = 1.8;
  return p;
}

bool overlap(const TrafficCar& a, const TrafficCar& b) {
  return std::abs(a.y - b.y) < 2.5 && boxesOverlap(a.x, a.z, a.yaw, a.length / 2 - 0.2, a.width / 2 - 0.1, b.x, b.z, b.yaw, b.length / 2 - 0.2, b.width / 2 - 0.1);
}

/** How far `b` is ahead of `a` along the street running east (m). */
double eastOf(const TrafficCar& a, const TrafficCar& b) { return b.x - a.x; }

bool has(const std::vector<TrafficCar*>& cars, const TrafficCar* car) { return std::find(cars.begin(), cars.end(), car) != cars.end(); }

void styles() {
  test("styles: what an emergency vehicle is, and whether its lights are on, by its style alone");
  for (const int kind : {EM_POLICE, EM_AMBULANCE, EM_FIRE, EM_DOCTOR}) {
    for (const bool lit : {false, true}) {
      const int s = emStyle(kind, lit);
      ok(s >= EM_STYLE, "above the others");
      ok(emKind(s) == kind, "the kind");
      ok(emLit(s) == lit, "the lights");
    }
  }
  for (const int s : {0, 1, 2, 3}) {
    ok(emKind(s) == -1, "the ordinary cars and the bus are none");
    ok(!emLit(s), "no lights");
  }
}

void pullsOver() {
  test("the car ahead pulls over to the right and stops, the ambulance passes it – then it drives on");
  auto u = std::make_unique<SetUp>(row(6, 150));
  Traffic& traffic = u->traffic;
  const Way w = wayAlong(u->town, {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}});
  TrafficCar* car = traffic.addCar(*w.route[1]->to, 50);
  // (and one coming the other way)
  TrafficCar* oncoming = traffic.addCar(*laneOf(u->town, 4, 0, 3, 0), 60);
  TrafficCar* amb = u->em.dispatch(EM_AMBULANCE, *w.start, 40, w.route);
  ok(amb->em && amb->em->call, "on a call");
  ok(emLit(amb->style), "lights on");
  const PlayerState player = anchor(375, 0);
  double minSide = 0;
  double oncomingSide = 0;
  bool stoppedNear = false;
  bool passed = false;
  double maxSpeed = 0;
  for (double t = 0; t < 70 && !(passed && eastOf(*car, *amb) > 60); t += DT) {
    traffic.update(DT, player);
    ok(has(traffic.cars, amb), "the ambulance stays");
    ok(!overlap(*amb, *car), "no touching the car (t=" + toFixed(t, 1) + ")");
    ok(!overlap(*amb, *oncoming), "no touching the oncoming car (t=" + toFixed(t, 1) + ")");
    minSide = std::min(minSide, car->side);
    oncomingSide = std::min(oncomingSide, oncoming->side);
    const double behind = eastOf(*amb, *car);
    if (behind > 0 && behind < 25 && car->v < 0.3) stoppedNear = true;
    if (behind < -6) passed = true;
    maxSpeed = std::max(maxSpeed, amb->v);
  }
  ok(passed, "the ambulance got past");
  ok(minSide < -0.6, "the car pulled over to the right (" + toFixed(minSide, 2) + " m)");
  ok(stoppedNear, "the car stood while the ambulance came up behind it");
  ok(oncomingSide < -0.5, "the oncoming car kept right (" + toFixed(oncomingSide, 2) + " m)");
  ok(maxSpeed > 50 / 3.6 + 2, "faster than the limit (" + toFixed(maxSpeed * 3.6, 0) + " km/h)");
  // once it is well past, the car goes back to the middle of its lane and drives on
  for (double t = 0; t < 12; t += DT) traffic.update(DT, player);
  ok(std::abs(car->side) < 0.05, "back in its lane (" + toFixed(car->side, 2) + " m)");
  ok(car->v > 3, "and drives on");
}

void narrowOneWay() {
  test("in a one-way street too narrow to pass, the car ahead drives on to clear the way – nobody gets stuck");
  TownOptions o = row(6, 150);
  o.oneWay = streetsOfRow();
  auto u = std::make_unique<SetUp>(o);
  Traffic& traffic = u->traffic;
  const Way w = wayAlong(u->town, {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}});
  ok(w.start->edge->width < 4, "a narrow street");
  TrafficCar* car = traffic.addCar(*w.route[0]->to, 30);
  const std::vector<Connector*> three(w.route.begin(), w.route.begin() + 3);
  TrafficCar* fire = u->em.dispatch(EM_FIRE, *w.start, 40, three);
  const PlayerState player = anchor(375, 0);
  const double x0 = fire->x;
  double waited = 0;
  for (double t = 0; t < 45; t += DT) {
    traffic.update(DT, player);
    ok(!overlap(*fire, *car), "no touching (t=" + toFixed(t, 1) + ")");
    if (fire->v < 0.3) waited += DT;
  }
  ok(fire->x - x0 > 250, "it got on (" + toFixed(fire->x - x0, 0) + " m)");
  ok(waited < 15, "not stuck for long (" + toFixed(waited, 1) + " s standing)");
}

void horn() {
  test("the horn: two tones a fourth apart in turn, higher as it comes nearer, quieter far away");
  ok(sirenTone(0.1) == SIREN_HIGH, "the high one first");
  ok(sirenTone(0.1 + SIREN_TONE) == SIREN_LOW, "then the low one");
  ok(sirenTone(0.1 + 2 * SIREN_TONE) == SIREN_HIGH, "round and round");
  ok(std::abs(SIREN_HIGH / SIREN_LOW - 4.0 / 3) < 0.002, "a pure fourth");
  ok(doppler(20, 0) > 1.05 && doppler(-20, 0) < 0.95, "higher coming, lower going");
  ok(std::abs(doppler(0, 0) - 1) < 1e-9, "standing: as it is");
  ok(doppler(0, 15) > 1, "driving towards it, too");
  ok(sirenLevel(5) == 1, "near: full");
  ok(std::abs(sirenLevel(48) - 0.25) < 1e-9, "a quarter at four times the distance");
}

void corridor() {
  test("on a road with two lanes in each direction the cars leave the corridor: the left lane moves left, the right one right");
  TownOptions o = row(6, 150);
  o.wide = streetsOfRow();
  auto u = std::make_unique<SetUp>(o);
  Traffic& traffic = u->traffic;
  const Way w = wayAlong(u->town, {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}});
  const LaneGraph::EdgeLanes& el = *u->town.lanes->edgeLanes[w.route[1]->to->edge->id];
  const std::vector<Lane*>& lanes = w.route[1]->to->dir > 0 ? el.fwd : el.back;
  ok(lanes.size() == 2, "two lanes");
  // slow traffic side by side
  TrafficCar* right = traffic.addCar(*lanes[0], 60);
  TrafficCar* left = traffic.addCar(*lanes[1], 62);
  for (TrafficCar* c : {right, left}) {
    c->v = 2;
    c->temper = 0.3;
  }
  TrafficCar* fire = u->em.dispatch(EM_FIRE, *w.start, 40, w.route);
  const PlayerState player = anchor(375, 0);
  double leftMost = 0;
  double rightMost = 0;
  bool passed = false;
  for (double t = 0; t < 60 && !passed; t += DT) {
    traffic.update(DT, player);
    for (TrafficCar* c : {right, left}) ok(!overlap(*fire, *c), "no touching (t=" + toFixed(t, 1) + ")");
    leftMost = std::max(leftMost, left->side);
    rightMost = std::min(rightMost, right->side);
    if (eastOf(*fire, *right) < -8 && eastOf(*fire, *left) < -8) passed = true;
  }
  ok(passed, "the fire engine got through");
  ok(leftMost > 0.5, "the left lane moved to the left (" + toFixed(leftMost, 2) + " m)");
  ok(rightMost < -0.5, "the right lane moved to the right (" + toFixed(rightMost, 2) + " m)");
}

TownOptions litTown() {
  TownOptions o;
  o.cols = 3;
  o.rows = 3;
  o.block = 160;
  o.lights = {{1, 1}};
  return o;
}

Connector* straightOn(Lane* lane) {
  for (Connector* c : lane->out)
    if (c->turn == TURN_STRAIGHT) return c;
  return nullptr;
}

void onRed() {
  test("at a junction it crosses on red – slowly, after a look – while the traffic with green waits for it");
  auto u = std::make_unique<SetUp>(litTown());
  Traffic& traffic = u->traffic;
  SignalSystem& signals = u->signals;
  const Node* node = u->town.world->nodes[u->town.node(1, 1)];
  // from the north, through the junction to the south: red for it (the street across has green)
  const Way w = wayAlong(u->town, {{1, 0}, {1, 1}, {1, 2}});
  Lane* across = laneOf(u->town, 0, 1, 1, 1);
  TrafficCar* car = traffic.addCar(*across, across->len - 75);
  if (Connector* s = straightOn(across)) car->next = s;
  TrafficCar* police = u->em.dispatch(EM_POLICE, *w.start, 30, w.route);
  const PlayerState player = anchor(160, 160);
  double atLine = JS_INF;
  bool carOver = false;
  bool crossed = false;
  for (double t = 0; t < 40 && !crossed; t += DT) {
    signals.update(DT);
    traffic.update(DT, player);
    ok(signals.get(node->sc, w.start->armTo->sg) == SIG_RED, "red for the police car all the while");
    ok(!overlap(*police, *car), "no touching (t=" + toFixed(t, 1) + ")");
    if (police->lane == w.start) {
      const double d = w.start->stopAt - police->s - police->length / 2;
      if (d < 3 && d > -1) atLine = std::min(atLine, police->v);
      // the car on green has not gone into the junction meanwhile
      if (car->lane == across && car->s + car->length / 2 > across->stopAt + 0.5) carOver = true;
    }
    if (police->lane == w.route[0]->to) crossed = true;
  }
  ok(crossed, "the police car got through the junction on red");
  ok(atLine < 6.5, "slowly at the line (" + toFixed(atLine * 3.6, 0) + " km/h)");
  ok(!carOver, "the car with green waited before its line");
  // and goes on once it has passed
  bool moved = false;
  for (double t = 0; t < 20 && !moved; t += DT) {
    signals.update(DT);
    traffic.update(DT, player);
    if (car->conn || car->lane != across) moved = true;
  }
  ok(moved, "the car went on once the police car had passed");
}

/** The player's car along the street running east, driven by hand: where it is, how fast, how far left of the middle of its lane. */
struct Driver {
  double x = 0;
  double z = 0;
  double v = 0;
  double off = 0;
};

/**
 * Drive the player (east along row 0) for `secs` seconds, with the traffic
 * and the rule monitor, `steer` deciding speed and place across the lane
 * each step.  Returns the faults found.
 */
std::vector<std::string> drivePlayer(Town& town, Traffic& traffic, SignalSystem& signals, Driver& me, double secs, const std::function<void(double)>& steer,
                                     const std::function<bool()>& until = nullptr) {
  RuleMonitor rules(*town.world, *town.lanes, signals, traffic);
  PolySample tmp = newPolySample();
  const double y = town.world->nodes[0]->y;
  PlayerState p;
  p.x = me.x;
  p.y = y;
  p.z = me.z;
  p.yaw = -PI / 2;
  p.speed = me.v;
  p.length = 4.3;
  p.width = 1.8;
  for (double t = 0; t < secs; t += DT) {
    steer(t);
    me.x += me.v * DT;
    p.x = me.x;
    p.speed = me.v;
    // (its place across: off the middle of the lane it is on)
    Lane* lane = rules.laneNow();
    p.lane = lane;
    p.s = lane ? rules.laneFront() - p.length / 2 : 0;
    if (lane) {
      lanePoint(*lane, p.s.value_or(0), tmp);
      p.z = tmp.z - tmp.tx * me.off;
    } else p.z = me.z;
    signals.update(DT);
    traffic.update(DT, p);
    PlayerSnapshot snap;
    snap.x = p.x;
    snap.y = p.y;
    snap.z = p.z;
    snap.yaw = p.yaw;
    snap.speed = me.v;
    snap.indicator = 0;
    snap.hazard = false;
    snap.lookYaw = 0;
    snap.seatbelt = true;
    snap.handbrake = 0;
    snap.stalled = false;
    snap.hitScenery = 0;
    snap.length = 4.3;
    rules.update(DT, snap, traffic.events);
    if (until && until()) break;
  }
  std::vector<std::string> ids;
  for (const Violation& v : rules.log) ids.push_back(v.id);
  return ids;
}

bool includes(const std::vector<std::string>& list, const std::string& id) { return std::find(list.begin(), list.end(), id) != list.end(); }

std::string joined(const std::vector<std::string>& list) {
  std::string s;
  for (const std::string& id : list) s += (s.empty() ? "" : ", ") + id;
  return s;
}

void blocking() {
  test("the player who does not make way is told so – and the one who pulls over and stops is not");
  for (const auto& [pullOver, demo] : {std::pair{false, false}, std::pair{true, false}, std::pair{false, true}}) {
    auto u = std::make_unique<SetUp>(row(6, 150));
    const Way w = wayAlong(u->town, {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}});
    TrafficCar* amb = u->em.dispatch(EM_AMBULANCE, *w.start, 30, w.route);
    // (on a demonstration drive the automatic driver is not judged)
    u->em.judging = !demo;
    Driver me{190, 0, 7, 0};
    std::string tip;
    const std::vector<std::string> faults = drivePlayer(
        u->town, u->traffic, u->signals, me, 45,
        [&](double) {
          if (tip.empty()) tip = u->em.tip();
          if (!pullOver || me.x - amb->x > 80) return;
          // to the right, and stop
          me.off = std::max(-1.0, me.off - 0.6 * DT);
          me.v = std::max(0.0, me.v - 3 * DT);
        },
        [&] { return amb->x > me.x + 40; });
    ok(tip == "emTipBehind", "the instructor says what to do (" + tip + ")");
    ok(!includes(faults, "crash"), "it never runs into the player");
    if (pullOver || demo) {
      ok(!includes(faults, "emergencyBlock"), "no fault when making way – or on a demonstration drive (" + joined(faults) + ")");
      ok(amb->x > me.x + 40, "and the ambulance got past");
    } else ok(includes(faults, "emergencyBlock"), "held up the ambulance (" + joined(faults) + ")");
  }
}

void slowCorridor() {
  test("in slow traffic on a road with two lanes each way: no corridor is a fault, the corridor lets it through");
  for (const bool corridor_ : {false, true}) {
    TownOptions o = row(6, 150);
    o.wide = streetsOfRow();
    auto u = std::make_unique<SetUp>(o);
    const Way w = wayAlong(u->town, {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}});
    TrafficCar* police = u->em.dispatch(EM_POLICE, *w.start, 30, w.route);
    // the player in the left lane, at walking pace
    Lane* leftLane = u->town.lanes->edgeLanes[w.route[0]->to->edge->id]->fwd[1];
    Driver me{190, 0, 1.5, 0};
    PolySample tmp = newPolySample();
    lanePoint(*leftLane, 40, tmp);
    me.z = tmp.z;
    std::string tip;
    const std::vector<std::string> faults = drivePlayer(
        u->town, u->traffic, u->signals, me, 40,
        [&](double) {
          if (tip.empty()) tip = u->em.tip();
          if (corridor_ && me.x - police->x < 150) me.off = std::min(0.85, me.off + 0.5 * DT);
        },
        [&] { return police->x > me.x + 30; });
    ok(tip == "emTipCorridor", "the instructor says what to do (" + tip + ")");
    if (corridor_) {
      ok(!includes(faults, "emergencyCorridor") && !includes(faults, "emergencyBlock"), "no fault in the corridor (" + joined(faults) + ")");
      ok(police->x > me.x + 30, "the police car got through");
    } else ok(includes(faults, "emergencyCorridor"), "no corridor (" + joined(faults) + ")");
  }
}

void intoTheJunction() {
  test("into the junction in front of it – on green – is a fault; waiting before the line is not");
  for (const bool wait : {false, true}) {
    auto u = std::make_unique<SetUp>(litTown());
    // the fire engine from the north, on red; the player from the west on green, about the same time
    const Way w = wayAlong(u->town, {{1, 0}, {1, 1}, {1, 2}});
    TrafficCar* fire = u->em.dispatch(EM_FIRE, *w.start, 30, w.route);
    Lane* across = laneOf(u->town, 0, 1, 1, 1);
    PolySample tmp = newPolySample();
    lanePoint(*across, across->len - 105, tmp);
    Driver me{tmp.x, tmp.z, 11, 0};
    lanePoint(*across, across->stopAt, tmp);
    const double line = tmp.x;
    std::string tip;
    const std::vector<std::string> faults = drivePlayer(
        u->town, u->traffic, u->signals, me, 40,
        [&](double) {
          if (tip.empty()) tip = u->em.tip();
          if (!wait) return;
          // braking to stand before the line until the fire engine is through (the player's front is 2.15 m ahead of its middle)
          if (fire->lane == w.route[0]->to && fire->s > 15) me.v = std::min(11.0, me.v + 2 * DT);
          else me.v = std::max(0.0, std::min(me.v, std::sqrt(2 * 3 * std::max(0.0, line - 1 - (me.x + 2.15)))));
        },
        [&] { return me.x > 230; });
    ok(tip == "emTipJunction", "the instructor says what to do (" + tip + ")");
    ok(!includes(faults, "crash"), "nobody runs into anybody");
    if (wait) ok(!includes(faults, "emergencyJunction"), "no fault when waiting (" + joined(faults) + ")");
    else ok(includes(faults, "emergencyJunction"), "into the junction in front of the fire engine (" + joined(faults) + ")");
  }
}

void callsByThemselves() {
  test("calls come by themselves – every few minutes in town – and end; police cars go on patrol without lights");
  TownOptions o;
  o.cols = 8;
  o.rows = 3;
  o.block = 150;
  Town town = makeTown(o);
  SignalSystem signals(town.world->signals, town.world->lightsOut);
  Traffic traffic(*town.world, *town.lanes, signals);
  traffic.targetCount = 10;
  Emergencies em(traffic);
  em.patrolShare = 0.25;
  traffic.emergency = &em;
  // the player stands in the street in the middle of the town (above it: nobody brakes for it)
  Lane* lane = laneOf(town, 3, 1, 4, 1);
  PolySample tmp = newPolySample();
  lanePoint(*lane, 60, tmp);
  PlayerState player;
  player.x = tmp.x;
  player.y = tmp.y + 30;
  player.z = tmp.z;
  player.yaw = std::atan2(-tmp.tx, -tmp.tz);
  player.length = 4.3;
  player.width = 1.8;
  player.lane = lane;
  player.s = 60;
  std::set<int64_t> calls;
  std::set<int64_t> ended;
  std::set<int64_t> patrols;
  bool patrolMoving = false;
  bool twoPatrols = false;
  const double dt = 1.0 / 20;
  for (double t = 0; t < 12 * 60; t += dt) {
    signals.update(dt);
    traffic.update(dt, player);
    int onPatrol = 0;
    for (const TrafficCar* c : traffic.cars) {
      if (!c->em) continue;
      ok(emKind(c->style) == c->em->kind, "its kind by its style");
      ok(emLit(c->style) == c->em->call, "its lights by its style");
      if (c->em->call) calls.insert(c->id);
      else if (calls.count(c->id)) ended.insert(c->id);
      else {
        patrols.insert(c->id);
        onPatrol++;
        if (c->v > 5) patrolMoving = true;
      }
    }
    if (onPatrol > 1) twoPatrols = true;
  }
  ok(calls.size() >= 3, "a call every few minutes (" + std::to_string(calls.size()) + " in 12 minutes)");
  ok(ended.size() >= 1, "calls end: lights off on arrival (" + std::to_string(ended.size()) + ")");
  ok(!patrols.empty() && patrolMoving, "police cars on patrol drive with the traffic");
  ok(!twoPatrols, "one on patrol at a time");
}

}  // namespace

int main() {
  std::printf("emergency_test (tests/sim/emergency.test.ts)\n");
  styles();
  pullsOver();
  narrowOneWay();
  horn();
  corridor();
  onRed();
  blocking();
  slowCorridor();
  intoTheJunction();
  callsByThemselves();
  std::printf("emergency_test: all 10 cases passed\n");
  return 0;
}

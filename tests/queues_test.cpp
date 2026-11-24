// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim world core platform geo
// Origin: tests/sim/queues.test.ts @ v1.7.7
/**
 * Queues in the traffic and getting past what blocks the way, on made-up
 * roads: how a queue is measured (src/sim/queues) – back over the
 * junction behind it, with the wait at a red light – and when a car of the
 * traffic goes round something standing in its lane (src/sim/pastblock):
 * into the lane beside, over the oncoming lane when it is clear, half onto
 * the verge – never over a solid line for something that will move again,
 * never just before a junction, never round a queue.
 */
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "src/core/geom.h"
#include "src/sim/autodriver.h"
#include "src/sim/ground.h"
#include "src/sim/queues.h"
#include "src/sim/rules.h"
#include "src/sim/signals.h"
#include "src/sim/traffic.h"
#include "src/sim/vehicle/index.h"
#include "src/world/lanes.h"
#include "tests/town.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using fs::harness::ok;
using fs::harness::test;
using fs::town::Net;
using fs::town::Road;
using fs::town::straight;

namespace {

const double QDT = 1.0 / 20;

/** the player out of the way: high above the road, so that nobody brakes for it (but the cars stay around it) */
const PlayerState AWAY{.x = 350, .y = 160, .z = 200, .yaw = 0, .speed = 0, .length = 4.3, .width = 1.8};

const std::vector<SignalControllerJson> NO_LIGHTS;

struct Set {
  Net net;
  SignalSystem signals;
  Traffic traffic;
  Lane* east;
  Lane* west;
  explicit Set(Net net_) : net(std::move(net_)), signals(NO_LIGHTS), traffic(*net.world, *net.lanes, signals) {
    traffic.targetCount = 0;
    const LaneGraph::EdgeLanes& el = net.lanes->edgeLanes[0];
    east = el.fwd[0];
    west = el.back[0];
  }
};

/** A car of the traffic that has broken down where it stands (it stays: crashed for good). */
TrafficCar* brokenDown(Traffic& traffic, Lane& lane, double s) {
  TrafficCar* c = traffic.addCar(lane, s);
  c->crashed = 1e6;
  c->v = 0;
  return c;
}

struct Box {
  double x, z, yaw, length, width;
};
Box boxOf(const TrafficCar& c) { return {c.x, c.z, c.yaw, c.length, c.width}; }
Box boxOf(const PlayerState& p) { return {p.x, p.z, p.yaw, p.length, p.width}; }
bool overlap(const Box& a, const Box& b) { return boxesOverlap(a.x, a.z, a.yaw, a.length / 2 - 0.1, a.width / 2 - 0.1, b.x, b.z, b.yaw, b.length / 2 - 0.1, b.width / 2 - 0.1); }

bool has(const Traffic& traffic, const TrafficCar* car) { return std::find(traffic.cars.begin(), traffic.cars.end(), car) != traffic.cars.end(); }

/** Run the traffic; `each` is called after every step. */
void run(Traffic& traffic, double seconds, const PlayerState& player = AWAY, const std::function<void()>& each = nullptr) {
  for (double t = 0; t < seconds; t += QDT) {
    traffic.update(QDT, player);
    if (each) each();
  }
}

// ------------------------------------------------------------------------------------ queues

void aQueue() {
  test("a queue: counted from its head back to its end, cars that drive are none");
  Set s(straight());
  QueueMeter meter;
  // five cars standing 9 m apart (about 4.5 m between them), the first at 400 m – and two more 60 m further back
  std::vector<TrafficCar*> q;
  for (int i = 0; i < 5; i++) q.push_back(s.traffic.addCar(*s.east, 400 - i * 9));
  for (TrafficCar* c : q) c->v = 0;
  const std::vector<TrafficCar*> loose = {s.traffic.addCar(*s.east, 300), s.traffic.addCar(*s.east, 292)};
  for (TrafficCar* c : loose) c->v = 0;
  std::vector<Queue> found = meter.measure(s.traffic.cars, nullptr);
  ok(found.size() == 1, "one queue (two cars are no queue)");
  ok(found[0].cars == 5, "five cars");
  ok(found[0].head == q[0], "its head");
  ok(found[0].tail == q[4], "its tail");
  // from the front of the first to the back of the last: 4 × 9 m + half the first car and half the last
  ok(std::abs(found[0].length - (36 + q[0]->length / 2 + q[4]->length / 2)) < 0.01, "length " + jsStr(found[0].length));
  ok(std::isnan(found[0].wait), "no light at its head: the wait cannot be told");
  // the third one drives off: two queues of two – none
  q[2]->v = 8;
  found = meter.measure(s.traffic.cars, nullptr);
  ok(found.empty(), "two queues of two: none");
  ok(QUEUE_MIN >= 3, "QUEUE_MIN >= 3");
}

void overTheJunction() {
  test("a queue reaches back over the junction behind it");
  Set s(straight());
  // the road north from (700, 0) to (700, 400) is road 1 – its lane towards the south ends at (700, 0); the queue begins at the start of road 0's lane west?
  Lane* into = s.net.lanes->edgeLanes[1].back[0];  // from (700, 400) to (700, 0)
  Lane* out = s.net.lanes->edgeLanes[0].back[0];   // from (700, 0) west
  Connector* c = nullptr;
  for (Connector* k : into->out)
    if (k->to == out) c = k;
  ok(c != nullptr, "a path from the one into the other");
  // three cars at the start of the lane west, two on the lane before the junction
  const std::vector<TrafficCar*> ahead = {s.traffic.addCar(*out, 22), s.traffic.addCar(*out, 14), s.traffic.addCar(*out, 6)};
  const std::vector<TrafficCar*> behind = {s.traffic.addCar(*into, into->len - 4), s.traffic.addCar(*into, into->len - 12)};
  for (TrafficCar* k : behind) k->next = c;
  for (TrafficCar* k : ahead) k->v = 0.5;
  for (TrafficCar* k : behind) k->v = 0.5;
  QueueMeter meter;
  const std::vector<Queue> found = meter.measure(s.traffic.cars, nullptr);
  ok(found.size() == 1, "one queue");
  ok(found[0].cars == 5, "five cars");
  ok(found[0].tail == behind[1], "its tail before the junction");
}

void waitAtRed() {
  test("the wait at a red light: until green, and a couple of seconds for each car ahead");
  Set s(straight());
  // (the original hands the meter a made-up light: red, 20 s left, red-amber next – here a real one with that programme)
  SignalControllerJson red;
  red.groups = 1;
  red.phases = {{20, "R"}, {1, "Y"}, {30, "G"}, {3, "A"}};
  const std::vector<SignalControllerJson> list = {red};
  SignalSystem lights(list);
  ok(lights.get(0, 0) == SIG_RED && lights.change(0, 0)->left == 20 && lights.change(0, 0)->next == SIG_RED_AMBER, "red, 20 s left, red-amber next");
  // (a lane that ends at a light)
  Arm arm = *s.east->armTo;
  arm.sg = 0;
  Node node = *s.east->to;
  node.sc = 0;
  Lane lane = *s.east;
  lane.armTo = &arm;
  lane.to = &node;
  std::vector<TrafficCar*> cars;
  for (int i = 0; i < 6; i++) {
    TrafficCar* c = s.traffic.addCar(*s.east, s.east->stopAt - 3 - i * 7);
    c->v = 0;
    c->lane = &lane;
    cars.push_back(c);
  }
  QueueMeter meter;
  const std::vector<Queue> found = meter.measure(cars, &lights);
  ok(found.size() == 1, "one queue");
  ok(found[0].wait > 20 + 5 * 1.5 && found[0].wait < 20 + 5 * 2.5 + 3, "wait " + jsStr(found[0].wait));
  // (the cars go back to the lane the traffic has them on, before it is taken down)
  for (TrafficCar* c : cars) c->lane = s.east;
}

// ------------------------------------------------------------------------------------ getting past

void overTheOncomingLane() {
  test("over the oncoming lane round a broken-down car, and back – nobody touched");
  Set s(straight());
  TrafficCar* broken = brokenDown(s.traffic, *s.east, 300);
  TrafficCar* car = s.traffic.addCar(*s.east, 240);
  double most = 0;
  bool touched = false;
  run(s.traffic, 60, AWAY, [&] {
    if (car->past) most = jsMax(most, car->past->off);
    if (overlap(boxOf(*car), boxOf(*broken))) touched = true;
  });
  ok(most > 2, "it moved out to the left (" + toFixed(most, 2) + " m)");
  ok(!touched, "never into the broken-down car");
  ok(has(s.traffic, car), "it is still there");
  ok(car->lane != s.east || car->s > 320, "past it (s " + toFixed(car->s, 0) + ")");
  ok((car->past ? car->past->off : 0) == 0, "back in its lane");
}

void waitsForOncoming() {
  test("it waits while somebody comes the other way, and goes once they are past");
  Set s(straight());
  TrafficCar* broken = brokenDown(s.traffic, *s.east, 300);
  TrafficCar* car = s.traffic.addCar(*s.east, 285);
  car->v = 0;
  // oncoming: one car after another
  const std::vector<TrafficCar*> others = {s.traffic.addCar(*s.west, s.west->len - 300 - 120), s.traffic.addCar(*s.west, s.west->len - 300 - 260)};
  bool touched = false;
  bool outWhileComing = false;
  run(s.traffic, 70, AWAY, [&] {
    if (has(s.traffic, broken) && overlap(boxOf(*car), boxOf(*broken))) touched = true;
    for (TrafficCar* o : others)
      if (has(s.traffic, o) && overlap(boxOf(*car), boxOf(*o))) touched = true;
    // (out over the middle while one of them is still within 60 m ahead, coming)
    if (car->past && car->past->off > 1.6) {
      for (TrafficCar* o : others) {
        if (!has(s.traffic, o) || o->lane != s.west) continue;
        const double ahead = (o->x - car->x) * 1;
        if (ahead > 0 && ahead < 60) outWhileComing = true;
      }
    }
  });
  ok(!touched, "nobody touched");
  ok(!outWhileComing, "not out while one was coming close");
  ok(car->lane != s.east || car->s > 320, "past in the end (s " + toFixed(car->s, 0) + ")");
}

void solidLine() {
  test("a solid centre line: not over it for the player standing – onto the verge outside town instead");
  for (const bool urban : {false, true}) {
    Set s(straight(Road{.urban = urban, .flags = EF_SOLID_CENTER}));
    const PolySample p = lanePoint(*s.east, 300, newPolySample());
    // the player's car standing in the lane, facing east
    const PlayerState player{.x = p.x, .y = 100, .z = p.z, .yaw = jsAtan2(-p.tx, -p.tz), .speed = 0, .length = 4.3, .width = 1.8};
    TrafficCar* car = s.traffic.addCar(*s.east, 250);
    double left = 0;
    double right = 0;
    bool touched = false;
    run(s.traffic, 50, player, [&] {
      if (car->past) {
        left = jsMax(left, car->past->off);
        right = jsMin(right, car->past->off);
      }
      if (overlap(boxOf(*car), boxOf(player))) touched = true;
    });
    ok(!touched, "not touched");
    ok(left < 0.5, "never over the solid line (" + toFixed(left, 2) + " m)");
    if (urban) {
      ok(right > -0.5 && car->s < 300, "in town: it waits");
    } else {
      ok(right < -1.2, "half onto the verge (" + toFixed(right, 2) + " m)");
      ok(car->lane != s.east || car->s > 310, "and past");
    }
  }
}

void parkedForGood() {
  test("a solid centre line, but a parked vehicle stands there for good: over it");
  Set s(straight(Road{.urban = true, .flags = EF_SOLID_CENTER}));
  const PolySample p = lanePoint(*s.east, 300, newPolySample());
  PlayerState parked{.x = p.x, .y = 100, .z = p.z, .yaw = jsAtan2(-p.tx, -p.tz), .speed = 0, .length = 4.4, .width = 1.8};
  parked.parked = true;
  s.traffic.obstacles = {&parked};
  TrafficCar* car = s.traffic.addCar(*s.east, 250);
  double most = 0;
  run(s.traffic, 50, AWAY, [&] {
    if (car->past) most = jsMax(most, car->past->off);
  });
  ok(most > 2, "out to the left (" + toFixed(most, 2) + " m)");
  ok(car->lane != s.east || car->s > 310, "and past");
}

void laneBeside() {
  test("two lanes each way: into the lane beside");
  Set s(straight(Road{.lf = 2, .lb = 2}));
  const LaneGraph::EdgeLanes& el = s.traffic.graph.edgeLanes[0];
  Lane* right = el.fwd[0];
  brokenDown(s.traffic, *right, 300);
  TrafficCar* car = s.traffic.addCar(*right, 250);
  bool moved = false;
  run(s.traffic, 40, AWAY, [&] {
    if (car->lane == el.fwd[1]) moved = true;
  });
  ok(moved, "over into the left lane");
}

void noQueueNoJunction() {
  test("no going round a queue, nor just before a junction");
  // a queue: the one in front of it waits behind another car (not broken down)
  Set s(straight());
  TrafficCar* a = s.traffic.addCar(*s.east, 300);
  TrafficCar* b = s.traffic.addCar(*s.east, 292);
  TrafficCar* c = s.traffic.addCar(*s.east, 284);
  for (TrafficCar* k : {a, b, c}) k->v = 0;
  ok(!s.traffic.pastBlocks.plan(*c, &AWAY).has_value(), "behind a car that waits: no way round");
  // a broken-down car right before the end of the road
  Set s2(straight());
  brokenDown(s2.traffic, *s2.east, s2.east->stopAt - 6);
  TrafficCar* d = s2.traffic.addCar(*s2.east, s2.east->stopAt - 15);
  d->v = 0;
  s2.traffic.update(QDT, AWAY);
  ok(!s2.traffic.pastBlocks.plan(*d, &AWAY).has_value(), "not just before the junction");
}

void neverThroughPeople() {
  test("never through people: a person standing in the way out");
  // (in town: no verge to go onto instead)
  Set s(straight(Road{.urban = true}));
  brokenDown(s.traffic, *s.east, 300);
  TrafficCar* car = s.traffic.addCar(*s.east, 291);
  car->v = 0;
  const PolySample p = lanePoint(*s.east, 300, newPolySample());
  // somebody on foot on the oncoming lane beside the broken-down car (an obstacle that is no vehicle)
  PlayerState person{.x = p.x + p.tz * 3.2, .y = 100, .z = p.z - p.tx * 3.2, .yaw = 0, .speed = 0, .length = 0.6, .width = 0.6};
  s.traffic.obstacles = {&person};
  s.traffic.update(QDT, AWAY);
  ok(!s.traffic.pastBlocks.plan(*car, &AWAY).has_value(), "no way past through somebody");
  s.traffic.obstacles = {};
  s.traffic.update(QDT, AWAY);
  const std::optional<PastPlan> plan = s.traffic.pastBlocks.plan(*car, &AWAY);
  ok(plan.has_value() && plan->kind == "oncoming", "with nobody there: over the oncoming lane");
}

/** flat land at the height of the roads */
class Flat : public TerrainSource {
 public:
  double heightAt(double, double) override { return 100; }
  int coverAt(double, double) override { return 0; }
};

void theAutopilot() {
  test("the autopilot goes round a broken-down car the same way – slowly, so without a fault");
  Set s(straight());
  TrafficCar* broken = brokenDown(s.traffic, *s.east, 300);
  RuleMonitor rules(*s.net.world, *s.net.lanes, s.signals, s.traffic);
  Flat terrain;
  WorldGround ground(*s.net.world, terrain);
  Vehicle v(COMPACT_CAR_AUTOMATIC, ground);
  // (east along the road, in its lane: right of the middle is +z)
  v.teleport(200, 100.02, 1.6, -PI / 2);
  v.resetDrivetrain(true);
  const double FRONT = 2.2;
  AutoDriverOptions opts;
  opts.spec = COMPACT_CAR_AUTOMATIC;
  opts.front = FRONT;
  AutoDriver driver(*s.net.world, s.signals, rules, &s.traffic, opts);
  double mpos[3] = {0, 0, 0};
  const double H = 1.0 / 60;
  bool touched = false;
  double left = 0;
  const std::vector<Connector*> noRoute;
  for (double t = 0; t < 120 && mpos[0] < 360; t += H) {
    v.getModelPosition(mpos);
    const AutoCommand& cmd = driver.step(H, v, mpos, noRoute);
    v.step(H, DriverInput{.steer = cmd.steer, .throttle = cmd.throttle, .brake = cmd.brake, .clutch = 0, .handbrake = 0, .gear = 1, .ignition = true, .starter = false});
    s.signals.update(H);
    v.getModelPosition(mpos);
    Lane* lane = rules.laneNow();
    PlayerState me{.x = mpos[0], .y = mpos[1], .z = mpos[2], .yaw = v.yaw, .speed = v.speed, .length = 4.28, .width = 1.79};
    me.lane = lane;
    me.s = lane ? rules.laneFront() - 2.14 : 0;
    s.traffic.update(H, me);
    PlayerSnapshot snap;
    snap.x = mpos[0];
    snap.y = mpos[1];
    snap.z = mpos[2];
    snap.yaw = v.yaw;
    snap.speed = v.speed;
    snap.indicator = cmd.indicator;
    snap.hazard = false;
    snap.lookYaw = cmd.look;
    snap.seatbelt = true;
    snap.handbrake = 0;
    snap.stalled = false;
    snap.hitScenery = 0;
    snap.length = 4.28;
    snap.front = FRONT;
    rules.update(H, snap, s.traffic.events);
    if (overlap(boxOf(me), boxOf(*broken))) touched = true;
    left = jsMax(left, -mpos[2]);
  }
  ok(mpos[0] >= 360, "got past (x " + toFixed(mpos[0], 0) + ")");
  ok(!touched, "never into the broken-down car");
  ok(left > 0.3, "over the middle of the road (" + toFixed(left, 2) + " m)");
  std::string faults;
  for (const Violation& x : rules.log) faults += x.id + " (" + x.severity + ") ";
  ok(rules.log.empty(), "no fault (" + faults + ")");
}

}  // namespace

int main() {
  std::printf("queues_test (tests/sim/queues.test.ts)\n");
  aQueue();
  overTheJunction();
  waitAtRed();
  overTheOncomingLane();
  waitsForOncoming();
  solidLine();
  parkedForGood();
  laneBeside();
  noQueueNoJunction();
  neverThroughPeople();
  theAutopilot();
  std::printf("queues_test: all 11 cases passed\n");
  return 0;
}

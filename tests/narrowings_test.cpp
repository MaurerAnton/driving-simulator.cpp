// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim world core platform geo game
// Origin: tests/sim/narrowings.test.ts @ v1.7.7
/**
 * Kerb build-outs narrowing a road (src/world/narrowing, src/sim/narrowings):
 * on a straight two-way street without lane markings, a car of the traffic
 * passes one without touching its bed and slows down; where it is on its
 * side it waits for an oncoming car (§ 6 StVO), the other direction does
 * not; the autopilot passes it without a fault; the player who drives in
 * while somebody comes the other way makes one; the beds are solid for the
 * player's car.
 */
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "core/geom.h"
#include "sim/autodriver.h"
#include "sim/collision.h"
#include "sim/ground.h"
#include "sim/narrowings.h"
#include "sim/rules.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "sim/vehicle/index.h"
#include "tests/sim_roads.h"
#include "tests/vehicle_harness.h"
#include "world/lanes.h"
#include "world/narrowing.h"

using namespace fs;
using fs::harness::ok;
using fs::harness::test;

namespace {

const double DT = 1.0 / 20;
/** the bed: 6 m long, its middle 300 m along the first road */
const double AT = 300;
const double LEN = 6;

struct NNet {
  WorldJson json;
  std::unique_ptr<World> world;
  std::unique_ptr<LaneGraph> lanes;
};

/**
 * Four unmarked residential streets 5.5 m wide round a block of 600 × 150 m;
 * on the first (z = 0, x 0 → 600) a choker on one side (`side` as the map
 * says it: left | right of the way's direction).
 */
NNet loop(const std::string& side) {
  const std::vector<std::pair<double, double>> pts = {{0, 0}, {600, 0}, {600, 150}, {0, 150}};
  std::vector<NodeJson> nodes;
  for (const auto& [x, z] : pts) {
    NodeJson n;
    n.x = x;
    n.y = 100;
    n.z = z;
    n.t = CTRL_NONE;
    nodes.push_back(n);
  }
  std::vector<EdgeJson> edges;
  for (int i = 0; i < 4; i++) {
    EdgeJson e;
    e.a = i;
    e.b = (i + 1) % 4;
    e.c = RESIDENTIAL;
    e.n = "Road " + std::to_string(i);
    e.p = roads::straightPoints(nodes[e.a], nodes[e.b]);
    e.lf = 1;
    e.lb = 1;
    e.lw = 2.75;
    e.w = 5.5;
    e.mf = 50;
    e.mb = 50;
    e.f = EF_URBAN;
    e.ca = 0;
    e.cb = 0;
    if (i == 0) {
      BuildOutFeature f;
      f.kind = "bump";
      f.s = AT;
      f.tags = {{"traffic_calming", "choker"}, {"side", side}, {"length", jsStr(LEN)}, {"beacon", "yes"}};
      e.bo = buildOutEntries({f});
    }
    edges.push_back(e);
  }
  roads::linkArms(nodes, edges, [](size_t) { return ROLE_NONE; }, false);
  NNet net;
  net.json.version = 1;
  net.json.meta.name = "Test roads";
  net.json.meta.minX = -3000;
  net.json.meta.maxX = 14000;
  net.json.meta.minZ = -3000;
  net.json.meta.maxZ = 14000;
  net.json.meta.tileSize = 500;
  net.json.nodes = nodes;
  net.json.edges = edges;
  computeAllCuts(net.json);
  net.world = std::make_unique<World>(net.json);
  net.lanes = std::make_unique<LaneGraph>(*net.world);
  return net;
}

struct Setup {
  SignalSystem signals;
  Traffic traffic;
  // (the player stands off the roads, watching)
  PlayerState player{.x = 300, .y = 100, .z = 420, .yaw = 0, .speed = 0, .length = 4.3, .width = 1.8};
  explicit Setup(NNet& net) : signals(net.world->signals), traffic(*net.world, *net.lanes, signals) {
    traffic.targetCount = 0;
    traffic.spawnRadius = 5000;
  }
};

/** A lane's distance of a station. */
double along(const Lane& lane, double s) { return (s - lane.s0) * lane.dir; }

/** The bed as a box (a → b is +x; left of it is −z): `side` +1 left, −1 right. */
bool bedTouched(NNet& net, double x, double z, double yaw, double hl, double hw) {
  const Narrowing& n = (*net.world->edges[0]->narrow)[0];
  const double half = net.world->edges[0]->width / 2;
  const double mid = n.side * (half - n.depth / 2);
  return boxesOverlap(x, z, yaw, hl, hw, (n.s0 + n.s1) / 2, -mid, -PI / 2, (n.s1 - n.s0) / 2, n.depth / 2);
}

/** Step the traffic until `done` (at most `secs`); `each` sees every step. */
void run(Traffic& traffic, const PlayerState& player, double secs, const std::function<bool()>& done, const std::function<void()>& each) {
  for (double t = 0; t < secs && !done(); t += DT) {
    traffic.update(DT, player);
    each();
  }
}

void theBed() {
  test("a build-out on a narrow street: one lane left, on the side mapped, as deep as it may be");
  NNet net = loop("right");
  const std::vector<Narrowing>& list = *net.world->edges[0]->narrow;
  FS_OK(list.size() == 1);
  const Narrowing& n = list[0];
  FS_OK(n.side == -1);
  FS_OK(std::abs(n.depth - 1.6) < 1e-9);
  FS_OK(std::abs(n.s0 - (AT - LEN / 2)) < 1e-9 && std::abs(n.s1 - (AT + LEN / 2)) < 1e-9);
  ok(n.single, "one vehicle at a time");
  FS_OK(std::abs(n.lo - (-2.75 + 1.6)) < 1e-9 && n.hi == 2.75);
}

void passesInTheGap() {
  test("a car with the build-out on its side: slows down, passes it in the gap without touching the bed");
  NNet net = loop("right");
  Setup s(net);
  Lane& fwd = *net.lanes->edgeLanes[0]->fwd[0];
  TrafficCar& car = *s.traffic.addCar(fwd, 200);
  car.v = 12;
  bool touched = false;
  double slowest = JS_INF;
  run(
      s.traffic, s.player, 60, [&] { return car.lane != &fwd || car.s > 340; },
      [&] {
        if (bedTouched(net, car.x, car.z, car.yaw, car.length / 2, car.width / 2)) touched = true;
        if (car.x > AT - LEN / 2 - 3 && car.x < AT + LEN / 2) slowest = std::min(slowest, car.v);
      });
  ok(car.x > 330, "got past (x " + toFixed(car.x, 1) + ")");
  ok(!touched, "never in the bed");
  ok(slowest < 6.5 && slowest > 3, "about 20 km/h through it (" + toFixed(slowest * 3.6, 0) + " km/h)");
}

void waitsForOncoming() {
  test("§ 6 StVO: the car on the side of the build-out waits for the oncoming one; the oncoming one does not wait");
  NNet net = loop("right");
  Setup s(net);
  Lane& fwd = *net.lanes->edgeLanes[0]->fwd[0];
  Lane& back = *net.lanes->edgeLanes[0]->back[0];
  TrafficCar& mine = *s.traffic.addCar(fwd, 230);
  mine.v = 10;
  // (it comes to the gap just after this one has come to its line)
  TrafficCar& other = *s.traffic.addCar(back, along(back, 430));
  other.v = 10;
  const double wait = along(fwd, AT - LEN / 2) - NARROW_WAIT;
  double stood = 0;
  double otherSlowest = JS_INF;
  bool both = false;
  bool touched = false;
  run(
      s.traffic, s.player, 90, [&] { return mine.x > 330; },
      [&] {
        const double front = mine.s + mine.length / 2;
        if (mine.lane == &fwd && mine.v < 0.1 && front < wait + 0.5 && front > wait - 4) stood += DT;
        if (other.lane == &back && other.x > AT - LEN / 2 - 4 && other.x < AT + 40) otherSlowest = std::min(otherSlowest, other.v);
        // never both in the stretch of the bed at once
        const bool inMine = mine.x + mine.length / 2 > AT - LEN / 2 - 1 && mine.x - mine.length / 2 < AT + LEN / 2 + 1;
        const bool inOther = other.lane == &back && other.x - other.length / 2 < AT + LEN / 2 + 1 && other.x + other.length / 2 > AT - LEN / 2 - 1;
        if (inMine && inOther) both = true;
        if (bedTouched(net, mine.x, mine.z, mine.yaw, mine.length / 2, mine.width / 2)) touched = true;
      });
  ok(stood > 0.5, "waited before the build-out (" + toFixed(stood, 1) + " s)");
  ok(!both, "one after the other through the gap");
  ok(!touched, "never in the bed");
  ok(otherSlowest > 3, "the oncoming car went on (slowest " + toFixed(otherSlowest * 3.6, 0) + " km/h)");
  ok(mine.x > 330, "then through");
}

void otherSide() {
  test("the build-out on the other side: the car goes on, the oncoming one waits");
  NNet net = loop("left");
  Setup s(net);
  Lane& fwd = *net.lanes->edgeLanes[0]->fwd[0];
  Lane& back = *net.lanes->edgeLanes[0]->back[0];
  TrafficCar& mine = *s.traffic.addCar(fwd, 250);
  mine.v = 10;
  TrafficCar& other = *s.traffic.addCar(back, along(back, 340));
  other.v = 10;
  const double otherWait = along(back, AT + LEN / 2) - NARROW_WAIT;
  double slowest = JS_INF;
  double otherSlowest = JS_INF;
  bool both = false;
  run(
      s.traffic, s.player, 60, [&] { return mine.x > 330 && other.x < 270; },
      [&] {
        if (mine.x > AT - 40 && mine.x < AT + LEN / 2) slowest = std::min(slowest, mine.v);
        // (before its line, while this one goes through)
        if (other.lane == &back && other.s + other.length / 2 < otherWait + 0.5) otherSlowest = std::min(otherSlowest, other.v);
        const bool inMine = mine.x + mine.length / 2 > AT - LEN / 2 - 1 && mine.x - mine.length / 2 < AT + LEN / 2 + 1;
        const bool inOther = other.lane == &back && other.x - other.length / 2 < AT + LEN / 2 + 1 && other.x + other.length / 2 > AT - LEN / 2 - 1;
        if (inMine && inOther) both = true;
      });
  ok(slowest > 3, "did not wait (slowest " + toFixed(slowest * 3.6, 0) + " km/h)");
  ok(otherSlowest < 1.5, "the oncoming car held back before its line (slowest " + toFixed(otherSlowest * 3.6, 0) + " km/h)");
  ok(!both, "one after the other through the gap");
  ok(other.x < 270, "then it went on");
}

/** flat land at the height of the roads */
class Flat : public TerrainSource {
 public:
  double heightAt(double, double) override { return 100; }
  int coverAt(double, double) override { return 0; }
};

void theAutopilot() {
  test("the autopilot passes the build-out in the gap without a fault; the beds are solid for the player");
  NNet net = loop("right");
  Setup s(net);
  RuleMonitor rules(*net.world, *net.lanes, s.signals, s.traffic);
  Flat terrain;
  WorldGround ground(*net.world, terrain);
  Vehicle v(COMPACT_CAR_AUTOMATIC, ground);
  // (on the right half of the road going +x: z > 0)
  v.teleport(200, 100.02, 1.375, -PI / 2);
  v.resetDrivetrain(true);
  const double FRONT = 2.2;
  AutoDriverOptions opts;
  opts.spec = COMPACT_CAR_AUTOMATIC;
  opts.front = FRONT;
  AutoDriver driver(*net.world, s.signals, rules, &s.traffic, opts);
  double mpos[3] = {0, 0, 0};
  const double H = 1.0 / 120;
  bool touched = false;
  double slowest = JS_INF;
  const std::vector<Connector*> noRoute;
  for (double t = 0; t < 120 && mpos[0] < 360; t += H) {
    v.getModelPosition(mpos);
    const AutoCommand& cmd = driver.step(H, v, mpos, noRoute);
    v.step(H, DriverInput{.steer = cmd.steer, .throttle = cmd.throttle, .brake = cmd.brake, .clutch = 0, .handbrake = 0, .gear = 1, .ignition = true, .starter = false});
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
    snap.lookYaw = cmd.look;
    snap.seatbelt = true;
    snap.length = 4.28;
    snap.front = FRONT;
    rules.update(H, snap, s.traffic.events);
    if (bedTouched(net, mpos[0], mpos[2], v.yaw, 2.14, 0.9)) touched = true;
    if (mpos[0] > AT - LEN / 2 - 2 && mpos[0] < AT + LEN / 2) slowest = std::min(slowest, v.speed);
  }
  ok(mpos[0] >= 360, "got through (x " + toFixed(mpos[0], 0) + ")");
  ok(!touched, "never in the bed");
  ok(slowest < 7, "slowly through it (" + toFixed(slowest * 3.6, 0) + " km/h)");
  std::string faults;
  for (const Violation& x : rules.log) faults += x.id + " ";
  ok(rules.log.empty(), "no fault (" + faults + ")");
  // the collider: the bed's kerb and its beacon, not the gap
  SceneryCollider col;
  narrowCollide(col, net.world->allEdges, true);
  ok(col.blocked(AT, 100, 1.9, -PI / 2, 0), "on the bed");
  ok(!col.blocked(AT, 100, -0.6, -PI / 2, 0), "in the gap");
  narrowCollide(col, net.world->allEdges, false);
  ok(!col.blocked(AT, 100, 1.9, -PI / 2, 0), "taken out again");
}

void thePlayer() {
  test("§ 6 StVO for the player: into the narrowing on the side of the build-out while an oncoming car is in it is a minor fault");
  NNet net = loop("right");
  Setup s(net);
  RuleMonitor rules(*net.world, *net.lanes, s.signals, s.traffic);
  Lane& back = *net.lanes->edgeLanes[0]->back[0];
  // an oncoming car in the gap (it stays there)
  TrafficCar& other = *s.traffic.addCar(back, along(back, AT + 1));
  other.crashed = 1000;
  other.v = 0;
  for (double x = 240; x < 300; x += 6 * DT) {
    PlayerSnapshot p;
    p.x = x;
    p.y = 100;
    p.z = 1.375;
    p.yaw = -PI / 2;
    p.speed = 6;
    p.seatbelt = true;
    p.length = 4.3;
    Lane* lane = rules.laneNow();
    PlayerState me{.x = p.x, .y = 100, .z = p.z, .yaw = p.yaw, .speed = 6, .length = 4.3, .width = 1.8};
    me.lane = lane;
    me.s = lane ? rules.laneFront() - 2.15 : 0;
    s.traffic.update(DT, me);
    rules.update(DT, p, {});
  }
  const Violation* f = nullptr;
  std::string all;
  for (const Violation& v : rules.log) {
    all += v.id + " ";
    if (v.id == "narrowYield" && !f) f = &v;
  }
  ok(f != nullptr, "(" + all + ")");
  FS_OK(f->severity == "minor");
}

}  // namespace

int main() {
  std::printf("narrowings_test (tests/sim/narrowings.test.ts)\n");
  theBed();
  passesInTheGap();
  waitsForOncoming();
  otherSide();
  theAutopilot();
  thePlayer();
  std::printf("ok\n");
  return 0;
}

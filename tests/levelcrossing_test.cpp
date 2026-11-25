// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim world core platform geo game
// Origin: tests/sim/levelcrossing.test.ts @ v1.7.7
/**
 * Level crossings (src/world/levelxing, src/sim/levelcrossing): what a
 * crossing is equipped with (the map's tags, else a guess from the road);
 * the cars of the traffic wait before the cross while it is closed, and
 * never stop on the tracks when there is no room behind them (§ 19 Abs. 3
 * StVO); the autopilot stops before it too; the player's faults.
 *
 * (The original's cases with trains – a train switches the crossing on, a
 * train waits before one not yet secured – need sim/rail at v1.7.7, and its
 * case of the two theory lessons game/theory: both are other parts.)
 */
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/geom.h"
#include "sim/autodriver.h"
#include "sim/ground.h"
#include "sim/levelcrossing.h"
#include "sim/rules.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "sim/vehicle/index.h"
#include "tests/sim_roads.h"
#include "tests/vehicle_harness.h"
#include "world/lanes.h"
#include "world/levelxing.h"

using namespace fs;
using fs::harness::ok;
using fs::harness::test;

namespace {

const double DT = 1.0 / 20;
/** the track crosses the first road 300 m along it */
const double AT = 300;

struct LNet {
  WorldJson json;
  std::unique_ptr<World> world;
  std::unique_ptr<LaneGraph> lanes;
};

/** Four roads round a block of 600 × 150 m (the first: z = 0, x 0 → 600), a railway crossing the first at x = 300. */
LNet loop(int cls) {
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
    e.c = i == 0 ? cls : RESIDENTIAL;
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
    edges.push_back(e);
  }
  roads::linkArms(nodes, edges, [](size_t) { return ROLE_NONE; }, false);
  LNet net;
  net.json.version = 1;
  net.json.meta.name = "Test roads";
  net.json.meta.minX = -3000;
  net.json.meta.maxX = 3000;
  net.json.meta.minZ = -3000;
  net.json.meta.maxZ = 3000;
  net.json.meta.tileSize = 500;
  net.json.nodes = nodes;
  net.json.edges = edges;
  CrossingJson x;
  x.e = 0;
  x.s = AT;
  x.k = XING_RAIL;
  x.w = 6;
  net.json.crossings = {x};
  computeAllCuts(net.json);
  net.world = std::make_unique<World>(net.json);
  net.lanes = std::make_unique<LaneGraph>(*net.world);
  return net;
}

/** where the player stands while it only watches: beside the road, off the tracks */
const PlayerState WATCHER{.x = 250, .y = 100, .z = 40, .yaw = 0, .speed = 0, .length = 4.3, .width = 1.8};

double along(const Lane& lane, double s) { return (s - lane.s0) * lane.dir; }

/** The installations, held in one phase when the test says so (the trains do not count) – the original's `hold`. */
class HeldCrossings : public LevelCrossings {
 public:
  using LevelCrossings::LevelCrossings;
  std::optional<Phase> held;
  void step(LevelCrossing& c, double dt) override {
    if (!held) {
      LevelCrossings::step(c, dt);
      return;
    }
    c.phase = *held;
    c.t = 10;
    c.lower = *held == "closed" ? 1 : 0;
  }
};

struct Setup {
  LNet net;
  SignalSystem signals;
  Traffic traffic;
  HeldCrossings lx;
  LevelCrossing* c = nullptr;
  explicit Setup(int cls = TERTIARY) : net(loop(cls)), signals(net.world->signals), traffic(*net.world, *net.lanes, signals), lx(*net.world, traffic) {
    traffic.targetCount = 0;
    traffic.spawnRadius = 5000;
    traffic.levelCrossings = &lx;
    lx.refresh();
    c = lx.list[0].get();
  }
};

void equipment() {
  test("what a crossing has: the map's tags where it has them, else a guess from the road");
  // (no tags where the test's crossing lies)
  FS_OK(!taggedAt(AT, 0));
  // the crossings of the line through Baunatal: half barriers with lights; one with full barriers
  const Equipment half = equipmentFor(-230, 97, true, 3);
  FS_OK(half.barrier == "half" && half.light == "yellowRed" && half.mapped);
  FS_OK(equipmentFor(983, -28, true, 2).barrier == "full");
  // the guesses: barriers and lights on the main roads, the flashing light on the small ones, the cross only on tracks; the trams' nothing
  FS_OK(equipmentFor(AT, 0, true, 3).barrier == "half" && equipmentFor(AT, 0, true, 3).light == "yellowRed");
  FS_OK(equipmentFor(AT, 0, true, 2).barrier == "none" && equipmentFor(AT, 0, true, 2).light == "blink");
  FS_OK(equipmentFor(AT, 0, true, 0).light == "none");
  FS_OK(equipmentFor(AT, 0, false, 4).light == "none");
}

void oneInstallation() {
  test("one installation per road; the cross, stop point and danger area where they belong");
  Setup s;
  FS_OK(s.lx.list.size() == 1);
  FS_OK(entryOf(*s.c, 1) == AT - LX.CROSS);
  FS_OK(exitOf(*s.c, 1) == AT + LX.ZONE);
  FS_OK(entryOf(*s.c, -1) == AT + LX.CROSS);
}

void carWaits() {
  test("a car of the traffic waits before the cross while the barriers are down, then goes over");
  Setup s;
  s.lx.held = "closed";
  Lane& fwd = *s.net.lanes->edgeLanes[0]->fwd[0];
  TrafficCar& car = *s.traffic.addCar(fwd, along(fwd, 200));
  car.v = 12;
  const double stop = along(fwd, AT - LX.STOP);
  double furthest = 0;
  for (double t = 0; t < 30; t += DT) {
    s.traffic.update(DT, WATCHER);
    if (car.lane == &fwd) furthest = std::max(furthest, car.s + car.length / 2);
  }
  ok(furthest <= stop + 0.6 && furthest > stop - 3, "front stopped at " + toFixed(furthest, 1) + " (stop point " + jsStr(stop) + ")");
  ok(car.v < 0.2, "standing");
  s.lx.held = "open";
  for (double t = 0; t < 30 && car.lane == &fwd; t += DT) s.traffic.update(DT, WATCHER);
  ok(car.lane != &fwd || car.s > along(fwd, AT + 20), "then over");
}

void noRoomBehind() {
  test("§ 19 Abs. 3 StVO: with no room behind the tracks a car waits before the cross, not on the rails");
  Setup s;
  s.lx.held = "open";
  Lane& fwd = *s.net.lanes->edgeLanes[0]->fwd[0];
  // the player stands in the queue beyond the tracks: its rear 2 m past the danger area – too little room for a car
  const double rearAt = AT + LX.ZONE + 2;
  PlayerState player{.x = rearAt + 2.15, .y = 100, .z = 1.375, .yaw = -PI / 2, .speed = 0, .length = 4.3, .width = 1.8};
  player.lane = &fwd;
  player.s = along(fwd, rearAt + 2.15);
  TrafficCar& car = *s.traffic.addCar(fwd, along(fwd, 220));
  car.v = 11;
  double deepest = 0;
  for (double t = 0; t < 30; t += DT) {
    s.traffic.update(DT, player);
    if (car.lane == &fwd) deepest = std::max(deepest, car.s + car.length / 2);
  }
  const double stop = along(fwd, AT - LX.STOP);
  ok(deepest <= stop + 0.6, "never onto the tracks (front at most " + toFixed(deepest, 1) + ", stop point " + jsStr(stop) + ")");
  ok(car.v < 0.2, "waiting");
  // the queue moves off: room again
  for (double t = 0; t < 20 && car.lane == &fwd && car.s < along(fwd, AT + 20); t += DT) s.traffic.update(DT, WATCHER);
  ok(car.lane != &fwd || car.s > along(fwd, AT + 10), "then over");
}

/** flat land at the height of the roads */
class Flat : public TerrainSource {
 public:
  double heightAt(double, double) override { return 100; }
  int coverAt(double, double) override { return 0; }
};

void theAutopilot() {
  test("the autopilot stops before the cross while the lights are red, and makes no fault");
  Setup s;
  s.lx.held = "red";
  RuleMonitor rules(*s.net.world, *s.net.lanes, s.signals, s.traffic);
  Flat terrain;
  WorldGround ground(*s.net.world, terrain);
  Vehicle v(COMPACT_CAR_AUTOMATIC, ground);
  v.teleport(220, 100.02, 1.375, -PI / 2);
  v.resetDrivetrain(true);
  const double FRONT = 2.2;
  AutoDriverOptions opts;
  opts.spec = COMPACT_CAR_AUTOMATIC;
  opts.front = FRONT;
  AutoDriver driver(*s.net.world, s.signals, rules, &s.traffic, opts);
  double mpos[3] = {0, 0, 0};
  const double H = 1.0 / 60;
  double furthest = 0;
  const std::vector<Connector*> noRoute;
  const auto drive = [&](double secs) {
    for (double t = 0; t < secs; t += H) {
      v.getModelPosition(mpos);
      const AutoCommand& cmd = driver.step(H, v, mpos, noRoute);
      v.step(H, DriverInput{.steer = cmd.steer, .throttle = cmd.throttle, .brake = cmd.brake, .clutch = 0, .handbrake = 0, .gear = 1, .ignition = true, .starter = false});
      v.getModelPosition(mpos);
      furthest = std::max(furthest, mpos[0] + FRONT);
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
    }
  };
  drive(25);
  ok(furthest < AT - LX.CROSS + 0.5, "stopped before the cross (front at most " + toFixed(furthest, 1) + ")");
  ok(std::abs(v.speed) < 0.2, "standing");
  s.lx.held = "open";
  drive(15);
  ok(mpos[0] > AT + 10, "then over (x " + toFixed(mpos[0], 0) + ")");
  std::string lx;
  for (const Violation& x : rules.log)
    if (startsWith(x.id, "levelCrossing")) lx += x.id + " ";
  ok(lx.empty(), lx);
}

/** The player's car moved along the first road (z: its lane; + x is east), the rule monitor watching. */
void drivePlayer(Setup& s, RuleMonitor& rules, double x0, double x1, double speed, double z = 1.375, double secs = 0) {
  const auto step = [&](double x, double v) {
    PlayerSnapshot p;
    p.x = x;
    p.y = 100;
    p.z = z;
    p.yaw = -PI / 2;
    p.speed = v;
    p.seatbelt = true;
    p.length = 4.3;
    Lane* lane = rules.laneNow();
    PlayerState me{.x = x, .y = 100, .z = z, .yaw = p.yaw, .speed = v, .length = 4.3, .width = 1.8};
    me.lane = lane;
    me.s = lane ? rules.laneFront() - 2.15 : 0;
    s.traffic.update(DT, me);
    rules.update(DT, p, s.traffic.events);
  };
  if (speed > 0)
    for (double x = x0; x < x1; x += speed * DT) step(x, speed);
  else
    for (double t = 0; t < secs; t += DT) step(x0, 0);
}

using Faults = std::vector<std::string>;

Faults faults(const Phase& phase, const std::function<void(Setup&, RuleMonitor&)>& run, int cls = TERTIARY) {
  Setup s(cls);
  s.lx.held = phase;
  RuleMonitor rules(*s.net.world, *s.net.lanes, s.signals, s.traffic);
  run(s, rules);
  Faults out;
  for (const Violation& v : rules.log)
    if (startsWith(v.id, "levelCrossing")) out.push_back(v.id + ":" + v.severity);
  return out;
}

std::string joined(const Faults& f) {
  std::string s;
  for (const std::string& x : f) s += x + " ";
  return s;
}

void playerFaults() {
  test("the player's faults: over on red, at closed barriers, stopped on the tracks, overtaking before it, too fast where there are no barriers");
  Faults f = faults("red", [](Setup& s, RuleMonitor& r) { drivePlayer(s, r, 270, 330, 8); });
  ok(f == Faults{"levelCrossingRed:major"}, joined(f));
  f = faults("closed", [](Setup& s, RuleMonitor& r) { drivePlayer(s, r, 270, 330, 8); });
  ok(f == Faults{"levelCrossingBarrier:major"}, joined(f));
  f = faults("open", [](Setup& s, RuleMonitor& r) { drivePlayer(s, r, 270, 330, 8); });
  ok(f.empty(), joined(f));
  f = faults("open", [](Setup& s, RuleMonitor& r) { drivePlayer(s, r, AT, AT, 0, 1.375, 4); });
  ok(f == Faults{"levelCrossingStop:major"}, joined(f));
  // a car of the traffic ahead in the zone of the warning sign, passed on the other side of the road
  f = faults("open", [](Setup& s, RuleMonitor& r) {
    Lane& fwd = *s.net.lanes->edgeLanes[0]->fwd[0];
    TrafficCar& car = *s.traffic.addCar(fwd, along(fwd, 240));
    car.v = 4;
    car.temper = 4 / 13.9;
    drivePlayer(s, r, 215, 290, 13, -1.375);
  });
  ok(f == Faults{"levelCrossingOvertake:major"}, joined(f));
  // a residential road: the flashing light, no barriers – 80 km/h over it is too fast
  f = faults("open", [](Setup& s, RuleMonitor& r) { drivePlayer(s, r, 240, 330, 22); }, RESIDENTIAL);
  ok(f == Faults{"levelCrossingSpeed:minor"}, joined(f));
}

void signs() {
  test("the signs at an installation: the cross on the masts of the lights in place of the map's own");
  Setup s;
  const std::vector<SignPost> posts = s.lx.signsFor(*s.c, [](double, double, double) { return 100.0; });
  FS_OK(posts.size() == 2);
  for (const SignPost& p : posts) FS_OK(p.s == std::vector<std::string>{"201-50"} && p.h == 2.45);
  SignPost own;
  own.x = AT - 4.5;
  own.z = 4;
  own.s = {"201-50", "1000-10"};
  const std::optional<SignPost> left = s.lx.editPost(own);
  FS_OK(left && left->s == std::vector<std::string>{"1000-10"});
  own.s = {"201-50"};
  FS_OK(!s.lx.editPost(own));
}

}  // namespace

int main() {
  std::printf("levelcrossing_test (tests/sim/levelcrossing.test.ts)\n");
  equipment();
  oneInstallation();
  carWaits();
  noRoomBehind();
  theAutopilot();
  playerFaults();
  signs();
  std::printf("ok\n");
  return 0;
}

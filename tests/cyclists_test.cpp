// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim game world core platform geo
// Origin: tests/game/cyclists.test.ts @ v1.7.7
/**
 * The people on bicycles: where they ride, what they stop for, when the
 * player is to blame – and how one is sent along when the player's vehicle
 * comes up to a right turn.
 */
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "core/js.h"
#include "sim/cyclists.h"
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
const double BLOCK = 220;
const double INF = std::numeric_limits<double>::infinity();

PlayerState box(double x, double z, double yaw, double speed, double length, double width) {
  PlayerState p;
  p.x = x;
  p.z = z;
  p.yaw = yaw;
  p.speed = speed;
  p.length = length;
  p.width = width;
  return p;
}

/** in the middle of a block: far from every street, near enough for the cyclists to stay */
const PlayerState NOWHERE = box(110, 110, 0, 0, 4.3, 1.8);

std::vector<Lane*> lanesOf(Town& town, int e) {
  const LaneGraph::EdgeLanes& el = *town.lanes->edgeLanes[e];
  std::vector<Lane*> out = el.fwd;
  out.insert(out.end(), el.back.begin(), el.back.end());
  return out;
}

Lane* laneTo(Town& town, int e, const Node* to) {
  for (Lane* l : lanesOf(town, e))
    if (l->to == to) return l;
  return nullptr;
}

struct Setup {
  Town town;
  SignalSystem signals;
  Cyclists cyclists;
  Lane* east = nullptr;
  Lane* south = nullptr;
  explicit Setup(const std::vector<std::array<int, 2>>& lights)
      : town(make(lights)), signals(town.world->signals, town.world->lightsOut), cyclists(*town.world, *town.lanes, signals) {
    // the lane from the west towards the junction in the middle of the town, and the one from the north
    const Node* mid = town.world->nodes[town.node(1, 1)];
    east = laneTo(town, town.street(0, 1, 1, 1), mid);
    south = laneTo(town, town.street(1, 0, 1, 1), mid);
  }
  static Town make(const std::vector<std::array<int, 2>>& lights) {
    TownOptions o;
    o.cols = 3;
    o.rows = 3;
    o.block = BLOCK;
    o.pavement = 2;
    o.lights = lights;
    return makeTown(o);
  }
};

std::unique_ptr<Setup> setup(const std::vector<std::array<int, 2>>& lights = {}) { return std::make_unique<Setup>(lights); }

std::vector<TrafficEvent> run(Cyclists& c, SignalSystem& signals, double seconds, const PlayerState& player = NOWHERE, const std::vector<PlayerState*>& obstacles = {},
                              const std::function<void(double)>& each = nullptr) {
  std::vector<TrafficEvent> events;
  static const std::vector<TrafficCar*> noCars;
  for (double t = 0; t < seconds; t += DT) {
    signals.update(DT);
    c.update(DT, player, obstacles, noCars, events);
    if (each) each(t);
  }
  return events;
}

int count(const std::vector<TrafficEvent>& events, bool hit) {
  int n = 0;
  for (const TrafficEvent& e : events)
    if (e.hit == hit) n++;
  return n;
}

void ridesBeside() {
  test("a cyclist rides on the pavement beside the rightmost lane, straight on through the junction");
  auto s = setup();
  Lane* east = s->east;
  ok(rideable(*east), "a street in town with a pavement");
  Cyclist* c = s->cyclists.add(*east, 20, 5.5);
  const double kerb = BLOCK + east->edge->width / 2;
  double least = INF;
  double most = -INF;
  run(s->cyclists, s->signals, 30, NOWHERE, {}, [&](double) {
    if (c->lane != east) return;
    least = std::min(least, c->z - kerb);
    most = std::max(most, c->z - kerb);
  });
  ok(least > 0.9 && most < 1.4, "between the posts at the kerb and the lamps at the back of the pavement (" + toFixed(least, 2) + " … " + toFixed(most, 2) + " m from the kerb)");
  ok(std::abs(cycleStrip(*east) - (east->edge->width / 2 - std::abs(east->off) + 1.15)) < 1e-9, "the strip");
  run(s->cyclists, s->signals, 30);
  ok(c->lane && c->lane->edge->id == s->town.street(1, 1, 2, 1), "straight on, on to the next piece of the street");
  ok(c->v > 4.5, "and still riding");
  ok(std::abs(c->y - 100) < 0.4, "on the ground");
}

void redLight() {
  test("at a red light the cyclist waits at the line, and rides on at green");
  auto s = setup({{1, 1}});
  Lane* south = s->south;
  // the side direction has red for the first 33 seconds
  Cyclist* c = s->cyclists.add(*south, south->stopAt - 60, 5.5);
  run(s->cyclists, s->signals, 25);
  ok(c->lane == south, "still before the junction");
  ok(c->v < 0.05, "standing");
  ok(south->stopAt - c->s > 0.4 && south->stopAt - c->s < 3, "at the line (" + toFixed(south->stopAt - c->s, 2) + " m before it)");
  ok(s->cyclists.list.size() == 1, "whoever waits at a light is not taken away");
  run(s->cyclists, s->signals, 25);
  ok(c->lane != south, "gone on at green");
}

void toBlame() {
  test("a vehicle that turns across the path of a cyclist at short notice is to blame – one that has been standing there is not");
  auto s = setup();
  Lane* east = s->east;
  const double strip = BLOCK + east->edge->width / 2 + 1.15;
  // --- at short notice: the vehicle is on the cycle track nine metres ahead of somebody riding at 22 km/h ---
  Cyclist* c = s->cyclists.add(*east, 60, 6);
  // (turning right off the street the cyclist rides along: the instructor still has it on that lane)
  PlayerState across = box(c->x + 9, strip, PI, 2, 4.3, 1.8);
  across.lane = east;
  across.s = c->s + 9;
  const std::vector<TrafficEvent> events = run(s->cyclists, s->signals, 6, across);
  ok(count(events, false) == 1, "the cyclist had to brake for it: noted once");
  ok(count(events, true) == 0, "but could still stop");
  ok(c->v < 0.05 && c->x < across.x - 0.9, "stands before the vehicle");

  // --- standing there all along: the cyclist sees it from afar and waits behind it ---
  s->cyclists.clear();
  Cyclist* d = s->cyclists.add(*east, 60, 6);
  PlayerState parked = box(d->x + 70, strip, PI, 0, 4.3, 1.8);
  parked.lane = east;
  parked.s = d->s + 70;
  const std::vector<TrafficEvent> none = run(s->cyclists, s->signals, 25, parked);
  ok(none.empty(), "nobody is to blame");
  ok(d->v < 0.05 && d->x < parked.x - 0.9 && d->x > parked.x - 6, "waits behind it (" + toFixed(parked.x - d->x, 1) + " m before its middle)");
  // … and after a while gets off and pushes the bicycle round (is gone)
  run(s->cyclists, s->signals, 40, parked);
  ok(s->cyclists.list.empty(), "gone after a while");
}

void trailer() {
  test("a trailer of the player's counts like the vehicle itself, a tram does not");
  auto s = setup();
  Lane* east = s->east;
  const double strip = BLOCK + east->edge->width / 2 + 1.15;
  Cyclist* c = s->cyclists.add(*east, 60, 6);
  PlayerState own = box(c->x + 9, strip, PI, 2, 7, 2.5);
  own.own = true;
  PlayerState truck = NOWHERE;
  truck.lane = east;
  truck.s = c->s + 20;
  ok(run(s->cyclists, s->signals, 5, truck, {&own}).size() == 1, "the trailer's");
  s->cyclists.clear();
  Cyclist* d = s->cyclists.add(*east, 60, 6);
  PlayerState tram = box(d->x + 9, strip, PI, 2, 7, 2.5);
  ok(run(s->cyclists, s->signals, 5, truck, {&tram}).empty(), "what is not the player's is not the player's fault");
  ok(d->v < 0.05, "they stop for it all the same");
}

void sideStreet() {
  test("a cyclist from another street who stops for the vehicle is not cut off by it");
  auto s = setup();
  Cyclist* c = s->cyclists.add(*s->south, s->south->stopAt - 30, 6);
  // the vehicle drives along the main street across the cyclist's way, close in front of them
  PlayerState passing = box(c->x, c->z + 9, -PI / 2, 8, 4.3, 1.8);
  passing.lane = s->east;
  passing.s = 200;
  ok(run(s->cyclists, s->signals, 3, passing).empty(), "nobody's fault");
  ok(c->v < 1, "they stopped for it all the same");
}

void knockedOver() {
  test("a vehicle that drives into a cyclist knocks them over");
  auto s = setup();
  Cyclist* c = s->cyclists.add(*s->east, 60, 5);
  const PlayerState onTop = box(c->x + 1.2, c->z, -PI / 2, 8, 4.3, 1.8);
  const std::vector<TrafficEvent> events = run(s->cyclists, s->signals, 0.2, onTop);
  bool hit = false;
  for (const TrafficEvent& e : events)
    if (e.hit && e.cyclist == c) hit = true;
  ok(hit, "knocked over");
  ok(c->down > 0 && c->v == 0, "lies on the ground");
  ok(!s->cyclists.blocksRightTurn(*s->east, c->s, 2.2), "nobody waits for somebody who lies there");
  run(s->cyclists, s->signals, 7);
  ok(s->cyclists.list.empty(), "picked up after a while");
}

void rightTurn() {
  test("whoever wants to turn right waits for the cyclist beside, behind and just ahead");
  auto s = setup();
  Lane* east = s->east;
  Cyclist* c = s->cyclists.add(*east, 100, 6);
  const double half = 2.15;
  ok(s->cyclists.blocksRightTurn(*east, 104, half), "beside the rear of the car");
  ok(s->cyclists.blocksRightTurn(*east, 98, half), "beside its front");
  ok(s->cyclists.blocksRightTurn(*east, 120, half), "coming up behind: there in three seconds");
  ok(!s->cyclists.blocksRightTurn(*east, 160, half), "far behind: time enough to turn");
  ok(!s->cyclists.blocksRightTurn(*east, 90, half), "well ahead of the car: it turns behind them");
  // the cyclist on the other side of the street is nobody's business here
  Lane* back = nullptr;
  for (Lane* l : lanesOf(s->town, east->edge->id))
    if (l != east) {
      back = l;
      break;
    }
  ok(!s->cyclists.blocksRightTurn(*back, 100, half), "the other side");
  // riding into the junction just ahead of the car: wait until they are across
  c->s = east->len - 0.5;
  int inJunction = 0;
  int cleared = 0;
  run(s->cyclists, s->signals, 6, NOWHERE, {}, [&](double) {
    if (!c->conn) return;
    if (s->cyclists.blocksRightTurn(*east, east->stopAt - 2.5, half)) inJunction++;
    else cleared++;
  });
  ok(inJunction > 20, "waited for while they cross the mouth of the side street");
  ok(cleared > 0 || c->lane != nullptr, "and not for longer than that");
}

void ridesAhead() {
  test("now and then a cyclist rides ahead of a vehicle that comes up to a right turn – and is at the junction a moment after it");
  auto s = setup();
  Lane* east = s->east;
  s->cyclists.target = 1;
  s->cyclists.chance = 1;
  bool right = false;
  bool straight = false;
  for (const Connector* c : east->out) {
    if (c->turn == TURN_RIGHT) right = true;
    if (c->turn == TURN_STRAIGHT) straight = true;
  }
  ok(right && straight, "a right turn and a way straight on");
  // the car: 36 km/h along the street, slowing down for the turn, at the line after about 24 seconds
  PlayerState car = box(0, BLOCK + 1.375, -PI / 2, 10, 4.3, 1.8);
  car.lane = east;
  car.s = 8;
  double firstSeen = -1;
  double gapAtLine = JS_NAN;
  const std::vector<TrafficEvent> events = run(s->cyclists, s->signals, 40, car, {}, [&](double) {
    const double toLine = east->stopAt - (*car.s + car.length / 2);
    if (toLine <= 0) {
      if (std::isnan(gapAtLine) && !s->cyclists.list.empty()) gapAtLine = s->cyclists.list[0]->lane == east ? east->stopAt - s->cyclists.list[0]->s : -1;
      car.speed = 0;
      return;
    }
    car.speed = toLine > 35 ? 10 : std::max(3.5, 10 - (35 - toLine) * 0.25);
    *car.s += car.speed * DT;
    car.x += car.speed * DT;
    if (firstSeen < 0 && !s->cyclists.list.empty()) firstSeen = s->cyclists.list[0]->s - (*car.s + car.length / 2);
  });
  ok(firstSeen >= 54, "turned up far ahead of the car (" + toFixed(firstSeen, 0) + " m), not before its bonnet");
  ok(gapAtLine > 1 && gapAtLine < 26, "beside or just behind the car when it is at the line (" + toFixed(gapAtLine, 1) + " m before the line)");
  ok(events.empty(), "the car kept to its lane: nothing happened");
  for (const Cyclist* c : s->cyclists.list) ok(c->staged != nullptr || c->lane != east, "every one of them sent along, or gone on");
}

void fromBehind() {
  test("one comes up from far behind while the vehicle waits at the line");
  auto s = setup();
  Lane* east = s->east;
  s->cyclists.target = 1;
  s->cyclists.chance = 1;
  const double at = east->stopAt - 2 - 2.15;
  PlayerState car = box(at, BLOCK + 1.375, -PI / 2, 0, 4.3, 1.8);
  car.lane = east;
  car.s = at;
  double firstSeen = JS_NAN;
  double passed = -1;
  run(s->cyclists, s->signals, 30, car, {}, [&](double t) {
    if (s->cyclists.list.empty()) return;
    const Cyclist* c = s->cyclists.list[0];
    if (std::isnan(firstSeen)) firstSeen = *car.s - car.length / 2 - c->s;
    if (passed < 0 && c->lane != east) passed = t;
  });
  ok(firstSeen >= 44 && firstSeen <= 71, "far behind the car, small in its mirrors (" + toFixed(firstSeen, 0) + " m)");
  ok(passed > 8 && passed < 24, "and past it a few seconds later (after " + toFixed(passed, 1) + " s)");
}

void noneSent() {
  test("no cyclist is sent along where the vehicle cannot turn right, on a fast road or without traffic");
  auto s = setup();
  Lane* east = s->east;
  s->cyclists.chance = 1;
  PlayerState car = box(0, BLOCK + 1.375, -PI / 2, 10, 4.3, 1.8);
  car.lane = east;
  car.s = 8;
  // (no traffic asked for: nobody on a bicycle either)
  s->cyclists.target = 0;
  run(s->cyclists, s->signals, 12, car);
  ok(s->cyclists.list.empty(), "no traffic");
  // the lane towards the edge of the town along its southern side: no street to the right there
  s->cyclists.target = 1;
  const Node* corner = s->town.world->nodes[s->town.node(1, 2)];
  Lane* rim = laneTo(s->town, s->town.street(0, 2, 1, 2), corner);
  bool right = false;
  for (const Connector* c : rim->out)
    if (c->turn == TURN_RIGHT) right = true;
  ok(!right, "the town ends there");
  PlayerState onRim = car;
  onRim.lane = rim;
  onRim.s = 8;
  run(s->cyclists, s->signals, 12, onRim);
  ok(s->cyclists.list.empty(), "no turn to the right");
  // out of town
  east->edge->flags &= ~256;
  run(s->cyclists, s->signals, 12, car);
  ok(s->cyclists.list.empty(), "out of town");
}

void withTraffic() {
  test("the traffic's cars wait for a cyclist before they turn right, and nobody drives through anybody");
  TownOptions o;
  o.cols = 3;
  o.rows = 3;
  o.block = 120;
  o.pavement = 2;
  Town town = makeTown(o);
  SignalSystem signals(town.world->signals, town.world->lightsOut);
  Traffic traffic(*town.world, *town.lanes, signals);
  traffic.targetCount = 14;
  PlayerState player = box(120, 120, 0, 0, 4.3, 1.8);
  player.y = 140;
  size_t cyclistsSeen = 0;
  int overlaps = 0;
  int endangered = 0;
  for (int step = 0; step < 30 * 240; step++) {
    signals.update(DT);
    traffic.update(DT, player);
    cyclistsSeen = std::max(cyclistsSeen, traffic.cyclists->list.size());
    if (step % 3 != 0) continue;
    for (const Cyclist* c : traffic.cyclists->list) {
      for (const TrafficCar* car : traffic.cars) {
        const double dx = c->x - car->x;
        const double dz = c->z - car->z;
        const double lon = -dx * std::sin(car->yaw) - dz * std::cos(car->yaw);
        const double lat = dx * std::cos(car->yaw) - dz * std::sin(car->yaw);
        if (std::abs(lon) < car->length / 2 && std::abs(lat) < car->width / 2) overlaps++;
      }
    }
  }
  for (const TrafficEvent& e : traffic.events)
    if (e.type == "cyclistEndangered") endangered++;
  ok(cyclistsSeen >= 1, "where there is traffic there are cyclists");
  ok(overlaps == 0, "no cyclist inside a car (" + std::to_string(overlaps) + ")");
  ok(endangered == 0, "nobody endangered");
}

}  // namespace

int main() {
  std::printf("cyclists_test (tests/game/cyclists.test.ts)\n");
  ridesBeside();
  redLight();
  toBlame();
  trailer();
  sideStreet();
  knockedOver();
  rightTurn();
  ridesAhead();
  fromBehind();
  noneSent();
  withTraffic();
  std::printf("cyclists_test: all 11 cases passed\n");
  return 0;
}

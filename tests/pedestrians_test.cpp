// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim game world core platform geo
// Origin: tests/sim/pedestrians.test.ts @ v1.7.7
/**
 * The people on foot in the town (src/sim/pedestrians): they wait for the
 * green at a pedestrian light and hurry when it turns red, wait at a zebra
 * crossing until the cars stop, cross elsewhere only in a gap, keep out of
 * each other's way, step back from a car that comes too close – and keep to
 * the pavements while they walk.
 *
 * (The case that holds the look of render/people and the age together stands in render_people_test.)
 */
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/geom.h"
#include "sim/pedestrians.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "tests/town.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using fs::harness::ok;
using fs::harness::test;

namespace {

const double TDT = 1.0 / 30;
const double M_PI_ = 3.14159265358979323846;

/** Somebody on foot far away, out of everything: the player as the tests need no player. */
PlayerState nobody() {
  PlayerState p;
  p.x = 5000;
  p.y = 100;
  p.z = 5000;
  p.length = 0.6;
  p.width = 0.6;
  p.onFoot = true;
  return p;
}

PlayerState car(double x, double z, double yaw, double speed) {
  PlayerState p;
  p.x = x;
  p.y = 100;
  p.z = z;
  p.yaw = yaw;
  p.speed = speed;
  p.length = 4.3;
  p.width = 1.8;
  return p;
}

struct Street {
  Town town;
  std::unique_ptr<SignalSystem> signals;
  std::unique_ptr<Traffic> traffic;
  Edge* edge = nullptr;
};

/** A street of `block` m east–west at z = 0 (from x = 0 to x = block), 5.5 m wide, with 2 m pavements; crossings as asked. */
std::unique_ptr<Street> street(double block, const std::vector<std::pair<double, int>>& crossings = {}) {
  auto s = std::make_unique<Street>();
  TownOptions o;
  o.cols = 2;
  o.rows = 1;
  o.block = block;
  o.pavement = 2;
  for (const auto& [at, k] : crossings) o.crossings.push_back({{0, 0, 1, 0}, at, k});
  s->town = makeTown(o);
  s->signals = std::make_unique<SignalSystem>(s->town.world->signals);
  s->traffic = std::make_unique<Traffic>(*s->town.world, *s->town.lanes, *s->signals);
  s->traffic->targetCount = 0;
  s->traffic->crowd->clock = [] { return 12.0; };
  s->edge = s->town.world->edges[s->town.street(0, 0, 1, 0)];
  return s;
}

// (the street runs a→b eastwards: its left side, +1, is the north pavement (z < 0), its right side the south one)
const int NORTH = 1;
const int SOUTH = -1;

WalkerOptions opts(int look, int age, double pace, Edge* goal, int side, double s) {
  WalkerOptions o;
  o.look = look;
  o.age = age;
  o.bold = false;
  o.pace = pace;
  o.idle = 0;
  o.goal = WalkerOptions::Goal{goal, side, s, ""};
  return o;
}

std::string f(double v, int d = 2) { return toFixed(v, d); }

void pedestrianLight() {
  test("at a pedestrian light: the button, the wait for the green – and hurrying when it turns red on the way");
  auto st = street(300, {{0.5, XING_SIGNAL}});
  SignalSystem& signals = *st->signals;
  Traffic& traffic = *st->traffic;
  const CrossingJson* c = st->town.world->crossings[0];
  const int sc = *c->sc;
  // (the light has rested on green for the vehicles a while)
  for (double t = 0; t < 30; t += 0.1) signals.update(0.1);
  Walker* w = traffic.crowd->add(*st->edge, NORTH, 138, 1, opts(7, AGE::ADULT, 1.3, st->edge, SOUTH, 175));
  const PlayerState player = nobody();
  double waited = 0;
  bool pressed = false;
  bool crossedOnGreen = false;
  int wrong = 0;
  bool over = false;
  for (double t = 0; t < 60 && !over; t += TDT) {
    signals.update(TDT);
    const std::string before = w->state;
    traffic.update(TDT, player);
    if (w->state == "wait") {
      waited += TDT;
      ok(w->crossing == c, "waiting at the crossing of the map");
      if (signals.requested(sc)) pressed = true;
    }
    if (before != "cross" && w->state == "cross") {
      if (signals.pedestrian(sc, c->g ? *c->g : 0) == "green") crossedOnGreen = true;
      else wrong++;
    }
    if (w->z > 3.5 && w->state == "walk") over = true;
  }
  ok(pressed, "the button was pressed (\"Signal kommt\")");
  ok(waited > 2, "waited for the green (" + f(waited, 1) + " s)");
  ok(wrong == 0, "never stepped out on red");
  ok(crossedOnGreen, "crossed on the green");
  ok(over, "and is on the other pavement");

  // somebody slow, caught by the red half way over: hurries
  auto s2 = street(300, {{0.5, XING_SIGNAL}});
  for (double t = 0; t < 30; t += 0.1) s2->signals->update(0.1);
  WalkerOptions o = opts(7, AGE::ADULT, 0.3, s2->edge, SOUTH, 175);
  Walker* slow = s2->traffic->crowd->add(*s2->edge, NORTH, 146, 1, o);
  const CrossingJson* c2 = s2->town.world->crossings[0];
  double before = 0;
  double after = 0;
  for (double t = 0; t < 120; t += TDT) {
    s2->signals->update(TDT);
    s2->traffic->update(TDT, player);
    if (slow->state != "cross") continue;
    const std::string sp = s2->signals->pedestrian(*c2->sc, 0);
    if (sp == "green") before = slow->speed;
    else after = std::max(after, slow->speed);
  }
  ok(before > 0.2, "on the road on the green (" + f(before) + " m/s)");
  ok(after > before * 1.3, "faster once it turned red (" + f(before) + " → " + f(after) + " m/s)");
}

void zebra() {
  test("at a zebra crossing: wait at the kerb, look left and right, step out once the cars stop – and they do");
  auto st = street(500, {{0.5, XING_ZEBRA}});
  SignalSystem& signals = *st->signals;
  Traffic& traffic = *st->traffic;
  traffic.targetCount = 8;
  const CrossingJson* zebraC = st->town.world->crossings[0];
  const PlayerState player = nobody();
  // the cars first
  for (double t = 0; t < 40; t += TDT) {
    signals.update(TDT);
    traffic.update(TDT, player);
  }
  Walker* w = traffic.crowd->add(*st->edge, NORTH, 236, 1, opts(7, AGE::ADULT, 1.3, st->edge, SOUTH, 270));
  double left = 0;
  double right = 0;
  double stepped = -1;
  int fastNear = 0;
  int overWhileOn = 0;
  std::map<int64_t, double> last;
  double atKerb = 0;
  bool over = false;
  for (double t = 0; t < 120 && !over; t += TDT) {
    signals.update(TDT);
    const std::string was = w->state;
    traffic.update(TDT, player);
    if (traffic.crowd->modeOf(*w) == "kerb") {
      atKerb += TDT;
      if (stepped < 0) {
        left = std::max(left, w->head);
        right = std::min(right, w->head);
      }
      if (w->state == "wait") ok(traffic.pedestrianAt(*zebraC) == w, "the traffic sees somebody waiting at the zebra crossing");
    }
    if (was != "cross" && w->state == "cross") {
      stepped = t;
      // nobody still coming fast and close
      for (const TrafficCar* c : traffic.cars) {
        const double dx = 250 - c->x;
        const bool towards = c->v > 0 && (dx > 0) == (-std::sin(c->yaw) > 0);
        if (towards && std::abs(dx) < 25 && c->v > 3) fastNear++;
      }
    }
    for (const TrafficCar* c : traffic.cars) {
      auto b = last.find(c->id);
      if (b != last.end() && ((b->second - 250) > 0) != ((c->x - 250) > 0) && std::abs(c->x - 250) < 6 && w->state == "cross") overWhileOn++;
      last[c->id] = c->x;
    }
    if (w->z > 3.5 && w->state == "walk") over = true;
  }
  ok(atKerb > 0.8, "stood at the kerb (" + f(atKerb, 1) + " s)");
  ok(left > 0.5 && right < -0.5, "looked left (" + f(left) + ") and right (" + f(right) + ")");
  ok(stepped > 0, "stepped out");
  ok(fastNear == 0, "not in front of a car still coming fast");
  ok(overWhileOn == 0, "no car drove over the crossing while they were on it");
  ok(over, "and is on the other side");
}

void zebraLate() {
  test("a zebra crossing: whoever comes up to it while a car is too close to stop lets it pass first (the driver is not to blame)");
  auto st = street(500, {{0.5, XING_ZEBRA}});
  const CrossingJson* zebraC = st->town.world->crossings[0];
  Walker* w = st->traffic->crowd->add(*st->edge, NORTH, 243, 1, opts(7, AGE::ADULT, 1.3, st->edge, SOUTH, 270));
  // the player's car comes along the south lane eastwards at 50 km/h
  PlayerState c = car(160, 1.4, -M_PI_ / 2, 13.9);
  int registeredClose = 0;
  double registered = -1;
  for (double t = 0; t < 30; t += TDT) {
    st->signals->update(TDT);
    c.x += c.speed * TDT;
    st->traffic->update(TDT, c);
    const Walker* at = st->traffic->pedestrianAt(*zebraC);
    const double front = 250 - 2 - (c.x + 2.15);
    if (at == w && w->waited > 0.3 && front > 0 && front < 30) registeredClose++;
    if (at == w && registered < 0) registered = c.x;
  }
  ok(registeredClose == 0, "never came up to the crossing just in front of the car");
  ok(registered > 250, "came up once the car had passed (car at x = " + f(registered, 0) + ")");
}

void gap() {
  test("elsewhere only in a gap of the traffic: the car passes first, then over");
  auto st = street(400);
  WalkerOptions o = opts(7, AGE::ADULT, 1.3, st->edge, SOUTH, 200);
  o.margin = 1.8;
  Walker* w = st->traffic->crowd->add(*st->edge, NORTH, 196, 1, o);
  // the player's car in the south lane, eastwards at 50 km/h, 110 m off
  PlayerState c = car(90, 1.4, -M_PI_ / 2, 13.9);
  double stepped = -1;
  double carAt = 0;
  bool over = false;
  bool looked = false;
  for (double t = 0; t < 40 && !over; t += TDT) {
    st->signals->update(TDT);
    c.x += c.speed * TDT;
    const std::string was = w->state;
    st->traffic->update(TDT, c);
    if (st->traffic->crowd->modeOf(*w) == "kerb" && std::abs(w->head) > 0.5) looked = true;
    if (was != "cross" && w->state == "cross" && stepped < 0) {
      stepped = t;
      carAt = c.x;
    }
    if (w->z > 3.5 && w->state == "walk") over = true;
  }
  ok(looked, "looked before crossing");
  ok(stepped > 0, "crossed");
  const double xw = w->x;
  ok(carAt > xw + 2, "stepped out after the car had passed (car at x = " + f(carAt, 1) + ", crossing at " + f(xw, 1) + ")");
  ok(over, "on the other side");

  // nothing coming (or far away and slow): straight over after a look
  auto s2 = street(400);
  Walker* v = s2->traffic->crowd->add(*s2->edge, NORTH, 196, 1, opts(7, AGE::ADULT, 1.3, s2->edge, SOUTH, 200));
  double t2 = -1;
  for (double t = 0; t < 20 && t2 < 0; t += TDT) {
    s2->signals->update(TDT);
    s2->traffic->update(TDT, nobody());
    if (v->state == "cross") t2 = t;
  }
  ok(t2 > 0 && t2 < 9, "over the empty street at once (" + f(t2, 1) + " s)");
}

void passing() {
  test("two people meeting on a pavement pass each other, each on their right; a quicker one overtakes on the left");
  auto st = street(400);
  Walker* a = st->traffic->crowd->add(*st->edge, NORTH, 150, 1, opts(7, AGE::ADULT, 1.3, st->edge, NORTH, 260));
  Walker* b = st->traffic->crowd->add(*st->edge, NORTH, 190, -1, opts(9, AGE::ADULT, 1.3, st->edge, NORTH, 80));
  double mn = JS_INF;
  double passedA = 0;
  double passedB = 0;
  for (double t = 0; t < 40; t += TDT) {
    st->signals->update(TDT);
    st->traffic->update(TDT, nobody());
    mn = std::min(mn, std::hypot(a->x - b->x, a->z - b->z));
    // as they pass: a walks east (its right is south, the kerb side here), b walks west (its right is north)
    if (std::abs(a->x - b->x) < 0.5) {
      passedA = a->z;
      passedB = b->z;
    }
  }
  ok(mn > 0.5, "never bumped into each other (" + f(mn) + " m)");
  ok(passedA > passedB, "each kept to the right (" + f(passedA) + " south of " + f(passedB) + ")");

  auto s2 = street(400);
  WalkerOptions os = opts(7, AGE::OLD, 0.7, s2->edge, NORTH, 300);
  os.bold.reset();
  Walker* slow = s2->traffic->crowd->add(*s2->edge, NORTH, 120, 1, os);
  WalkerOptions oq = opts(9, AGE::ADULT, 1.8, s2->edge, NORTH, 300);
  oq.bold.reset();
  oq.hurry = true;
  Walker* quick = s2->traffic->crowd->add(*s2->edge, NORTH, 108, 1, oq);
  double gapM = JS_INF;
  bool overtaken = false;
  for (double t = 0; t < 40; t += TDT) {
    s2->signals->update(TDT);
    s2->traffic->update(TDT, nobody());
    gapM = std::min(gapM, std::hypot(slow->x - quick->x, slow->z - quick->z));
    if (quick->x > slow->x + 2) overtaken = true;
  }
  ok(overtaken, "the quicker one got past");
  ok(gapM > 0.5, "without bumping into the slower one (" + f(gapM) + " m)");
}

void stepBack() {
  test("a car passing too close to the kerb, fast: the one waiting there starts and steps back");
  auto st = street(400);
  Walker* w = st->traffic->crowd->add(*st->edge, NORTH, 196, 1, opts(7, AGE::ADULT, 1.3, st->edge, SOUTH, 200));
  const PlayerState player = nobody();
  // to the kerb
  for (double t = 0; t < 8 && st->traffic->crowd->modeOf(*w) != "kerb"; t += TDT) {
    st->signals->update(TDT);
    st->traffic->update(TDT, player);
  }
  ok(st->traffic->crowd->modeOf(*w) == "kerb", "waits at the kerb");
  const double kerbZ = w->z;
  // the player's car westwards in the north lane, its side a hand's breadth from the kerb, at 55 km/h
  PlayerState c = car(260, -1.65, M_PI_ / 2, 15.3);
  double startle = 0;
  double back = 0;
  for (double t = 0; t < 8; t += TDT) {
    st->signals->update(TDT);
    c.x -= c.speed * TDT;
    st->traffic->update(TDT, c);
    startle = std::max(startle, w->startle);
    back = std::max(back, kerbZ - w->z);
  }
  ok(startle > 0.5, "started (" + f(startle) + ")");
  ok(back > 0.25, "stepped back from the kerb (" + f(back) + " m)");
}

void horn() {
  test("the horn: those in front of the vehicle start and stop short");
  auto st = street(400);
  Walker* w = st->traffic->crowd->add(*st->edge, NORTH, 150, 1, opts(7, AGE::ADULT, 1.3, st->edge, NORTH, 300));
  PlayerState c = car(140, 1.4, -M_PI_ / 2, 0);
  for (double t = 0; t < 2; t += TDT) {
    st->signals->update(TDT);
    st->traffic->update(TDT, c);
  }
  ok(w->speed > 1, "walking");
  c.horn = true;
  double startle = 0;
  bool stood = false;
  for (double t = 0; t < 0.5; t += TDT) {
    st->traffic->update(TDT, c);
    startle = std::max(startle, w->startle);
    if (w->phase < 0) stood = true;
  }
  ok(startle > 0.8, "started (" + f(startle) + ")");
  ok(stood, "stopped short");
}

PlayerState townPlayer() {
  PlayerState p;
  p.x = 120;
  p.y = 100;
  p.z = 120;
  p.length = 0.6;
  p.width = 0.6;
  p.onFoot = true;
  return p;
}

void nightAndRain() {
  test("at night fewer are out; in the rain they hurry, under umbrellas");
  ok(outAt(3) < 0.1 && outAt(12) == 1 && outAt(17.5) > 0.9, "the hours of the day");
  ok(rainOf(RainWeather{"clear", 0}) == 0, "clear: no rain");
  ok(rainOf(RainWeather{"rain", 0.6}) > 0.6 && rainOf(RainWeather{"drizzle", 0.5}) > 0.2, "rain and drizzle");
  auto count = [](double hour) {
    TownOptions o;
    o.cols = 3;
    o.rows = 3;
    o.block = 120;
    o.pavement = 2.2;
    Town town = makeTown(o);
    SignalSystem signals(town.world->signals);
    Traffic traffic(*town.world, *town.lanes, signals);
    traffic.targetCount = 0;
    traffic.crowd->clock = [hour] { return hour; };
    traffic.crowd->target = 80;
    const PlayerState player = townPlayer();
    double sum = 0;
    for (double t = 0; t < 20; t += TDT) {
      traffic.update(TDT, player);
      if (t > 10) sum += static_cast<double>(traffic.crowd->people().size()) * TDT;
    }
    return sum / 10;
  };
  const double noon = count(12);
  const double night = count(3);
  ok(noon > 30, "people about at noon (" + f(noon, 0) + ")");
  ok(night < noon * 0.2, "few at three in the morning (" + f(night, 1) + " against " + f(noon, 0) + ")");

  // the same walk, dry and in the rain
  struct R {
    double t;
    bool umbrella;
  };
  auto walk = [](bool wet) -> R {
    auto st = street(400);
    st->traffic->crowd->weather = [wet] { return std::optional<RainWeather>(wet ? RainWeather{"rain", 0.8} : RainWeather{"clear", 0}); };
    WalkerOptions o = opts(7, AGE::ADULT, 1.3, st->edge, NORTH, 300);
    o.bold.reset();
    Walker* w = st->traffic->crowd->add(*st->edge, NORTH, 100, 1, o);
    bool umbrella = false;
    for (double t = 0; t < 60; t += TDT) {
      st->signals->update(TDT);
      st->traffic->update(TDT, nobody());
      if (w->umbrella) umbrella = true;
      if (w->x > 160) return {t, umbrella};
    }
    return {60, umbrella};
  };
  const R dry = walk(false);
  const R wet = walk(true);
  ok(wet.t < dry.t * 0.88, "quicker in the rain (" + f(wet.t, 1) + " s against " + f(dry.t, 1) + " s for 60 m)");
  ok(!dry.umbrella, "no umbrella in the dry");
}

void paces() {
  test("who they are follows from how they look: the old walk slower, children quicker");
  TownOptions o;
  o.cols = 3;
  o.rows = 3;
  o.block = 120;
  o.pavement = 2.2;
  Town town = makeTown(o);
  SignalSystem signals(town.world->signals);
  Traffic traffic(*town.world, *town.lanes, signals);
  traffic.targetCount = 0;
  traffic.crowd->clock = [] { return 12.0; };
  traffic.crowd->target = 150;
  const PlayerState player = townPlayer();
  std::map<int, std::pair<double, int>> pace;
  for (double t = 0; t < 40; t += TDT) {
    traffic.update(TDT, player);
    for (const Walker* w : traffic.crowd->people()) {
      if (traffic.crowd->modeOf(*w) != "walk" || w->phase < 0) continue;
      auto& l = pace[ageOfLook(w->colour)];
      l.first += w->speed;
      l.second++;
    }
  }
  auto mean = [&](int a) { return pace[a].first / std::max(1, pace[a].second); };
  ok(mean(AGE::OLD) > 0 && mean(AGE::OLD) < mean(AGE::ADULT) * 0.85, "the old slower (" + f(mean(AGE::OLD)) + " against " + f(mean(AGE::ADULT)) + " m/s)");
  ok(mean(AGE::CHILD) > mean(AGE::ADULT), "children quicker (" + f(mean(AGE::CHILD)) + " m/s)");
}

void sideStreet() {
  test("crossing a side street at a junction: a car that turns into it lets them over first");
  // a T: the main street east–west along the row, the side street south from the middle junction
  TownOptions o;
  o.cols = 3;
  o.rows = 2;
  o.block = 120;
  o.pavement = 2;
  o.missing = {{0, 1, 1, 1}, {1, 1, 2, 1}, {0, 0, 0, 1}, {2, 0, 2, 1}};
  Town town = makeTown(o);
  SignalSystem signals(town.world->signals);
  Traffic traffic(*town.world, *town.lanes, signals);
  traffic.targetCount = 0;
  traffic.crowd->clock = [] { return 12.0; };
  Edge* mainE = town.world->edges[town.street(0, 0, 1, 0)];
  Edge* side = town.world->edges[town.street(1, 0, 1, 1)];
  // somebody on the south pavement of the main street walking east, past the mouth of the side street
  Walker* w = traffic.crowd->add(*mainE, -1, 100, 1, opts(7, AGE::ADULT, 1.3, town.world->edges[town.street(1, 0, 2, 0)], -1, 40));
  // the lane of the side street that leaves the junction (southwards)
  const LaneGraph::EdgeLanes* el = town.lanes->edgeLanes[side->id];
  const Lane* lane = el && !el->fwd.empty() ? el->fwd[0] : nullptr;
  ok(lane != nullptr, "the side street has a lane leaving the junction");
  double crossing = 0;
  bool seen = false;
  for (double t = 0; t < 60; t += TDT) {
    signals.update(TDT);
    traffic.update(TDT, nobody());
    if (w->state == "cross" && std::abs(w->x - 120) < 6) {
      crossing += TDT;
      if (traffic.crowd->crossingLane(*lane)) seen = true;
    }
  }
  ok(crossing > 1, "crossed the mouth of the side street (" + f(crossing, 1) + " s on it)");
  ok(seen, "a car turning into the side street sees somebody crossing it there");
}

void wholeTown() {
  test("a whole town: nobody walks on the carriageway but to cross it, and no car runs into anybody");
  TownOptions o;
  o.cols = 3;
  o.rows = 3;
  o.block = 120;
  o.pavement = 2.2;
  o.lights = {{1, 1}};
  o.crossings = {{{0, 0, 1, 0}, 0.5, XING_ZEBRA}, {{0, 2, 1, 2}, 0.5, XING_SIGNAL}};
  Town town = makeTown(o);
  SignalSystem signals(town.world->signals);
  Traffic traffic(*town.world, *town.lanes, signals);
  traffic.targetCount = 14;
  traffic.crowd->clock = [] { return 12.0; };
  traffic.crowd->target = 70;
  const PlayerState player = townPlayer();
  SurfaceHit hit;
  int walking = 0;
  int onRoad = 0;
  int touched = 0;
  std::set<int> crossers;
  for (double t = 0; t < 150; t += TDT) {
    signals.update(TDT);
    traffic.update(TDT, player);
    for (const Walker* w : traffic.crowd->people()) {
      if (w->state == "cross") crossers.insert(w->id);
      if (w->state != "walk" || traffic.crowd->modeOf(*w) == "follow") continue;
      walking++;
      if (town.world->surfaceAt(w->x, w->z, w->y + 1, hit) && hit.zone == 0) onRoad++;
    }
    for (const TrafficCar* c : traffic.cars) {
      const double fx = -std::sin(c->yaw);
      const double fz = -std::cos(c->yaw);
      for (const Walker* w : traffic.crowd->people()) {
        const double dx = w->x - c->x;
        const double dz = w->z - c->z;
        if (std::abs(dx) > 6 || std::abs(dz) > 6) continue;
        const double lf = dx * fx + dz * fz;
        const double lr = dx * -fz + dz * fx;
        if (std::abs(lf) < c->length / 2 + 0.1 && std::abs(lr) < c->width / 2 + 0.1 && c->v > 0.5) touched++;
      }
    }
  }
  const int crossed = static_cast<int>(crossers.size());
  ok(walking > 10000, "people walked (" + std::to_string(walking) + " samples)");
  ok(onRoad == 0, "nobody walking along on the carriageway (" + std::to_string(onRoad) + ")");
  ok(crossed > 15, std::to_string(crossed) + " crossed a street");
  ok(touched == 0, "no car drove into anybody (" + std::to_string(touched) + ")");
}

}  // namespace

int main() {
  std::printf("pedestrians_test (tests/sim/pedestrians.test.ts)\n");
  pedestrianLight();
  zebra();
  zebraLate();
  gap();
  passing();
  stepBack();
  horn();
  nightAndRain();
  paces();
  sideStreet();
  wholeTown();
  std::printf("ok\n");
  return 0;
}

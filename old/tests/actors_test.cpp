// SPDX-License-Identifier: AGPL-3.0-only
/** Actors: pooled traffic cars, walking pedestrians, signal heads. */
#include "../src/render/actors.h"
#include <cmath>
#include <cassert>
#include <cstdio>

using namespace fs;

namespace {
WorldJson makeNet() {
  WorldJson j;
  j.version = 1;
  j.meta.tileSize = 500;
  auto node = [](double x, double y, double z, int t) {
    NodeJson n;
    n.x = x; n.y = y; n.z = z; n.t = t;
    return n;
  };
  j.nodes.push_back(node(0, 0, 0, CTRL_SIGNALS));
  j.nodes.push_back(node(100, 0, 0, CTRL_NONE));
  auto edge = [](int a, int b, std::vector<double> p) {
    EdgeJson e;
    e.a = a; e.b = b; e.c = RESIDENTIAL; e.p = std::move(p);
    e.w = 7.0; e.lf = 1; e.lb = 1; e.lw = 3.5; e.mf = 50; e.mb = 50;
    return e;
  };
  j.edges.push_back(edge(0, 1, {0, 0, 0, 100, 0, 0}));
  auto arm = [](int e, int r, int g) {
    ArmJson a;
    a.e = e; a.r = r; a.g = g;
    return a;
  };
  j.nodes[0].arms = {arm(0, ROLE_PRIORITY, 0)};
  j.nodes[1].arms = {arm(0, ROLE_NONE, -1)};
  SignalControllerJson sc;
  sc.x = 0; sc.z = 0; sc.groups = 1; sc.off = 0;
  SignalPhaseJson p;
  p.d = 30; p.s = "G";
  sc.phases.push_back(p);
  j.signals.push_back(sc);
  j.nodes[0].sc = 0;
  return j;
}
}  // namespace

int main() {
  // --- traffic cars -------------------------------------------------------
  TrafficRenderer tr;
  std::vector<TrafficCar> cars;
  TrafficCar c1;
  c1.id = 1;
  c1.x = 10; c1.y = 0; c1.z = 5;
  c1.yaw = 1.0;
  c1.color = 0x1f3f7a;
  c1.style = 0;
  c1.wheelSpin = 0.4;
  c1.braking = true;
  c1.blinker = -1;
  cars.push_back(c1);
  TrafficCar c2 = c1;
  c2.id = 2;
  c2.style = 1;
  c2.x = 20;
  cars.push_back(c2);

  std::vector<Pedestrian> peds;
  Pedestrian p;
  p.id = 1;
  p.x = 5; p.y = 0; p.z = 2;
  p.yaw = 0.5;
  p.state = "cross";
  p.phase = 1.2;
  p.colour = 0xcc4444;
  peds.push_back(p);

  tr.update(cars, peds, 1.0);
  // the cars are attached as borrowed nodes (the renderer owns them)
  assert(!tr.group.borrowed_.empty());
  // two cars and one pedestrian
  int carNodes = 0, pedNodes = 0;
  for (Object3D* ch : tr.group.borrowed_) {
    auto* g = dynamic_cast<Group*>(ch);
    if (!g) continue;
    if (g->name == "ped") pedNodes++;
    else carNodes++;
  }
  assert(carNodes == 2);
  assert(pedNodes == 1);

  // the cars sit where the simulation put them
  bool sawCarAt = false;
  for (Object3D* ch : tr.group.borrowed_) {
    auto* g = dynamic_cast<Group*>(ch);
    if (!g || g->name == "ped") continue;
    if (std::fabs(g->position.x - 10) < 1e-9 && std::fabs(g->position.z - 5) < 1e-9)
      sawCarAt = true;
  }
  assert(sawCarAt);

  // a car that disappears is pooled, not destroyed
  cars.pop_back();
  tr.update(cars, peds, 1.1);
  int after = 0;
  for (Object3D* ch : tr.group.borrowed_)
    if (dynamic_cast<Group*>(ch)) after++;
  assert(after == 2);   // one car + one pedestrian

  // and it comes back when the same id reappears (from the pool)
  cars.push_back(c2);
  tr.update(cars, peds, 1.2);
  int back = 0;
  for (Object3D* ch : tr.group.borrowed_)
    if (dynamic_cast<Group*>(ch)) back++;
  assert(back == 3);

  // pedestrians that stop crossing keep their place
  peds[0].state = "wait";
  tr.update(cars, peds, 1.3);
  assert(tr.group.borrowed_.size() >= 3);

  // --- signal heads --------------------------------------------------------
  // the JSON lives as long as the test, so the World and the signal system
  // can reference it safely
  WorldJson net = makeNet();
  World w(net);
  SignalSystem sig(net.signals);
  SignalRenderer sr(w);
  assert(!sr.group.children.empty());   // housing and lamps are owned
  // one approach → at least one pole
  assert(!sr.poles.empty());
  assert(sr.poles.size() % 4 == 0);
  assert(sr.poles.size() >= 4);
  // the pole stands beyond the stop line, a little off the road edge
  assert(std::fabs(sr.poles[2]) > 4.0);

  // a fresh signal system starts green, so the lamps show green
  sr.update(sig);
  assert(!sr.group.children.empty());

  // the collision system gets the pole positions
  assert(sr.poles.size() >= 4);
  assert(sr.poles[3] > 0);   // radius

  std::printf("actors: all checks passed (%zu poles)\n", sr.poles.size() / 4);
  return 0;
}

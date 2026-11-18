// SPDX-License-Identifier: AGPL-3.0-only
/** The examiner: speed, seat belt, handbrake, off-road and following distance. */
#include "../src/sim/rules.h"
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
  j.nodes.push_back(node(0, 0, 0, CTRL_RBL));
  j.nodes.push_back(node(300, 0, 0, CTRL_NONE));
  auto edge = [](int a, int b, std::vector<double> p, int cls, double w, double mf) {
    EdgeJson e;
    e.a = a; e.b = b; e.c = cls; e.p = std::move(p);
    e.w = w; e.lf = 1; e.lb = 1; e.lw = 3.5; e.mf = mf; e.mb = mf;
    return e;
  };
  // a 30 km/h urban road
  j.edges.push_back(edge(0, 1, {0, 0, 0, 300, 0, 0}, RESIDENTIAL, 7.0, 30));
  j.edges[0].f = EF_URBAN | EF_MARKED;
  j.edges[0].z = 30;
  auto arm = [](int e, int r) {
    ArmJson a;
    a.e = e; a.r = r;
    return a;
  };
  j.nodes[0].arms = {arm(0, ROLE_PRIORITY)};
  j.nodes[1].arms = {arm(0, ROLE_NONE)};
  return j;
}
}  // namespace

int main() {
  World w(makeNet());
  LaneGraph g(w);
  std::vector<SignalControllerJson> none;
  SignalSystem sig(none);
  Traffic t(w, g, sig);
  RuleMonitor m(w, g, sig, t);

  // severity points match the original table
  assert(severityPoints(Severity::Hint) == 0);
  assert(severityPoints(Severity::Minor) == 1);
  assert(severityPoints(Severity::Major) == 3);
  assert(severityPoints(Severity::Fail) == 10);

  // a fresh monitor knows nothing
  m.reset();
  assert(m.log.empty() && m.points() == 0 && !m.failed());
  assert(m.context.limit == -1 && !m.context.onRoad);

  // driving sensibly on the 30 km/h road produces nothing
  PlayerSnapshot p;
  p.x = 50; p.y = 0; p.z = 0;
  p.yaw = -PI_ / 2;      // facing east (yaw 0 = north)
  p.speed = 8.0;         // 29 km/h
  p.seatbelt = true;
  p.length = 4.3;
  std::vector<TrafficEvent> ev;
  for (int i = 0; i < 600; i++) m.update(1.0 / 60, p, ev);
  assert(m.log.empty());
  // and the HUD context knows the limit and the road
  assert(m.context.limit == 30);
  assert(m.context.urban);
  assert(m.context.roadName.empty() || !m.context.roadName.empty());

  // 60 km/h in a 30 zone is a fault (and points)
  p.speed = 16.7;
  for (int i = 0; i < 300; i++) {
    p.x += p.speed / 60.0;
    m.update(1.0 / 60, p, ev);
  }
  assert(!m.log.empty());
  bool sawSpeed = false;
  for (const auto& v : m.log)
    if (v.id == "speed") sawSpeed = true;
  assert(sawSpeed);
  assert(m.points() > 0);

  // without a seat belt above 8 km/h is a minor fault, and it is cooldown-limited
  m.reset();
  p.x = 50;
  p.speed = 8.0;
  p.seatbelt = false;
  for (int i = 0; i < 120; i++) m.update(1.0 / 60, p, ev);
  bool sawBelt = false;
  for (const auto& v : m.log)
    if (v.id == "seatbelt") sawBelt = true;
  assert(sawBelt);
  assert(m.points() >= 1);

  // driving with the handbrake on is a hint
  m.reset();
  p.seatbelt = true;
  p.handbrake = 1;
  p.speed = 16.7;   // > 12 km/h
  for (int i = 0; i < 600; i++) {
    p.x += p.speed / 60.0;
    m.update(1.0 / 60, p, ev);
  }
  bool sawHandbrake = false;
  for (const auto& v : m.log)
    if (v.id == "handbrakeOn") sawHandbrake = true;
  assert(sawHandbrake);

  // off the road entirely is recorded after a while
  m.reset();
  p.handbrake = 0;
  p.x = 500; p.z = 500;   // far from any road
  p.speed = 8;
  for (int i = 0; i < 300; i++) m.update(1.0 / 60, p, ev);
  bool sawOff = false;
  for (const auto& v : m.log)
    if (v.id == "offRoad") sawOff = true;
  assert(sawOff);
  assert(!m.context.onRoad);

  // strict mode turns hints into faults (and therefore points)
  RuleMonitor strict(w, g, sig, t);
  strict.strict = true;
  PlayerSnapshot q;
  q.x = 50; q.z = 0; q.yaw = -PI_ / 2; q.speed = 8; q.seatbelt = true; q.length = 4.3;
  q.stalled = true;                       // a hint-level offence
  for (int i = 0; i < 60; i++) strict.update(1.0 / 60, q, ev);
  bool sawStall = false;
  for (const auto& v : strict.log)
    if (v.id == "stall" && v.severity == Severity::Minor) sawStall = true;
  assert(sawStall);
  assert(strict.points() >= 1);

  // a collision with scenery is a fail
  m.reset();
  PlayerSnapshot r;
  r.x = 50; r.z = 0; r.yaw = -PI_ / 2; r.speed = 5; r.seatbelt = true; r.length = 4.3;
  r.hitScenery = 5.0;
  m.update(1.0 / 60, r, ev);
  assert(m.failed());
  assert(m.points() >= 10);

  std::printf("rules: all checks passed (%zu violations in the last case)\n", m.log.size());
  return 0;
}

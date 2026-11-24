// SPDX-License-Identifier: AGPL-3.0-only
// LINK: game sim world core platform geo i18n
// Origin: tests/game/ce.test.ts @ v1.7.7
/**
 * Class CE on the practice yard: the layouts of the two basic manoeuvres,
 * their judging, and the order of coupling and uncoupling.
 * (The original's checks of the dictionaries and the citations are i18n's: tests/i18n_test.cpp.)
 */
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "game/ce.h"
#include "game/nav.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using namespace fs::harness;

namespace {

const double EAST = -PI / 2;
const double SOUTH = PI;
bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }

/** the combination standing straight at a pose */
RigState straight(const Pose& truck, double speed = 0, bool handbrake = true) { return RigState{truck, trailerBehind(truck), speed, handbrake}; }

bool has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

void frames() {
  test("the yard's frame: there and back, for a yard along and one across");
  for (const int rot : {0, 1}) {
    const YardFrame f(YardJson{305, 1485, 150, 66, rot});
    const auto [x, z] = f.toWorld(40, -10);
    const auto [u, w] = f.toLocal(x, z);
    FS_OK(near(u, 40) && near(w, -10));
    FS_OK(near(std::hypot(x - 305, z - 1485), std::hypot(40, 10)));
    // a vehicle heading along +u moves along +u
    const Pose p = f.poseToWorld({0, 0, EAST});
    const auto ahead = f.toLocal(p.x - std::sin(p.yaw) * 5, p.z - std::cos(p.yaw) * 5);
    FS_OK(near(ahead[0], 5) && near(ahead[1], 0));
    FS_OK(near(f.yawToLocal(p.yaw), EAST));
  }
  const YardFrame along(YardJson{0, 0, 150, 66, 0});
  FS_OK((along.toWorld(10, 0) == std::array<double, 2>{10, 0}));
  const YardFrame across(YardJson{0, 0, 150, 66, 1});
  const auto north = across.toWorld(10, 0);
  FS_OK(near(north[0], 0) && near(north[1], -10));
}

void layouts() {
  test("the layouts fit the yard, and the combination starts on the paved area");
  for (const char* id : {"corner", "ramp", "couple"}) {
    const TaskLayout lay = taskLayout(id);
    std::vector<Rect> all = lay.allowed;
    all.push_back(lay.goal);
    if (lay.ramp) all.push_back(*lay.ramp);
    for (const Rect& r : all) FS_OK(r.u0 < r.u1 && r.w0 < r.w1 && r.u0 >= -75 && r.u1 <= 75 && r.w0 >= -33 && r.w1 <= 33);
    const Pose trailer = trailerBehind(lay.start);
    std::vector<double> pts;
    for (const auto& c : {corners(lay.start, TRUCK_OUTLINE.front, TRUCK_OUTLINE.rear, TRUCK_OUTLINE.hw), corners(trailer, TRAILER_OUTLINE.front, TRAILER_OUTLINE.rear, TRAILER_OUTLINE.hw)}) pts.insert(pts.end(), c.begin(), c.end());
    for (size_t i = 0; i < pts.size(); i += 2)
      FS_OK(std::any_of(lay.allowed.begin(), lay.allowed.end(), [&](const Rect& r) { return pts[i] >= r.u0 && pts[i] <= r.u1 && pts[i + 1] >= r.w0 && pts[i + 1] <= r.w1; }));
    // 15.7 m from the front of the truck to the rear end of the trailer
    const double len = TRUCK_OUTLINE.front + TRUCK_OUTLINE.hitch + TRAILER_OUTLINE.hitch + TRAILER_OUTLINE.rear;
    FS_OK(near(len, 15.7, 0.01));
    double lo = 1e9, hi = -1e9;
    for (size_t i = 0; i < pts.size(); i += 2) lo = std::min(lo, pts[i]), hi = std::max(hi, pts[i]);
    FS_OK(near(hi - lo, len, 0.01));
  }
}

void corner() {
  test("round the corner: done when the whole combination stands in the side road, straight and secured");
  ManoeuvreJudge j("corner");
  const TaskLayout& lay = j.layout;
  for (int i = 0; i < 100; i++) j.update(straight(lay.start), 0.02);
  FS_OK(j.status.state == "run");
  FS_OK(j.status.excursions == 0);
  const Pose inSide{-20.5, -8, SOUTH};
  FS_OK(j.arrived(straight(inSide)));
  FS_OK(!j.arrived(straight({inSide.x, 2, inSide.yaw})));
  FS_OK(!j.arrived(straight({inSide.x, inSide.z, SOUTH + 0.25})));
  FS_OK(!j.arrived(straight({-17.8, inSide.z, inSide.yaw})));
  for (int i = 0; i < 100; i++) j.update(straight(inSide, -0.5, false), 0.02);
  FS_OK(j.status.state == "run");
  for (int i = 0; i < 100; i++) j.update(straight(inSide, 0, false), 0.02);
  FS_OK(j.status.state == "run");
  for (int i = 0; i < 100; i++) j.update(straight(inSide, 0, true), 0.02);
  FS_OK(j.status.state == "done");
}

void excursions() {
  test("leaving the paved area is a fault each time, three times ends the task");
  ManoeuvreJudge j("corner");
  const RigState on = straight(j.layout.start);
  const RigState off = straight({j.layout.start.x, j.layout.start.z + 3, j.layout.start.yaw});
  int faults = 0;
  for (const RigState* rig : {&on, &off, &off, &on, &off, &on}) {
    const ManoeuvreStatus& st = j.update(*rig, 0.02);
    faults += static_cast<int>(std::count(st.faults.begin(), st.faults.end(), "ceLine"));
  }
  FS_OK(faults == 2);
  FS_OK(j.status.state == "run");
  j.update(off, 0.02);
  j.update(on, 0.02);
  j.update(off, 0.02);
  FS_OK(j.status.state == "failed");
  ManoeuvreJudge far("corner");
  far.update(straight({far.layout.start.x, far.layout.start.z + 8, far.layout.start.yaw}), 0.02);
  FS_OK(far.status.state == "failed");
}

void corrections() {
  test("corrections: pulling forward twice is allowed, a third time is not");
  ManoeuvreJudge j("ramp");
  const RigState rig = straight(j.layout.start);
  const auto go = [&](double speed, double seconds) {
    for (int i = 0; i < seconds * 50; i++) j.update(RigState{rig.truck, rig.trailer, speed, false}, 0.02);
  };
  go(1, 3);
  FS_OK(j.status.corrections == 0);
  for (int k = 1; k <= MAX_CORRECTIONS; k++) {
    go(-1, 3);
    go(1, 2);
    FS_OK(j.status.corrections == k);
    FS_OK(j.status.state == "run");
  }
  go(-1, 3);
  go(0.2, 1);
  FS_OK(j.status.corrections == MAX_CORRECTIONS);
  go(1, 2);
  FS_OK(j.status.state == "failed");
}

void ramp() {
  test("the ramp: close to it, in the bay, square – and never touching it");
  ManoeuvreJudge j("ramp");
  const RampRect ramp = *j.layout.ramp;
  const auto at = [&](double gap, double w = -12, double yaw = -PI / 2) {
    const double reach = TRUCK_OUTLINE.hitch + TRAILER_OUTLINE.hitch + TRAILER_OUTLINE.rear;
    return straight({ramp.u1 + gap + reach * std::cos(yaw - EAST), w, yaw});
  };
  FS_OK(j.arrived(at(0.5)));
  FS_OK(!j.arrived(at(1.6)));
  FS_OK(!j.arrived(at(0.5, -13.2)));
  FS_OK(!j.arrived(at(0.5, -12, EAST + 0.12)));
  for (int i = 0; i < 60; i++) j.update(at(0.5), 0.02);
  FS_OK(j.status.state == "done");
  FS_OK(near(j.status.gap, 0.5, 0.01));
  ManoeuvreJudge hit("ramp");
  const ManoeuvreStatus& st = hit.update(at(-0.05), 0.02);
  FS_OK(st.state == "failed");
  FS_OK(st.faults == std::vector<std::string>{"ceRamp"});
}

void couplingInOrder() {
  test("coupling and uncoupling in the right order, from parking the combination to the brake test");
  CouplingProcedure p;
  CouplingState s{0, false, true, 0, 0};
  const auto tick = [&](int n = 5) {
    CouplingStatus st = p.update(s, 0.02);
    for (int i = 1; i < n; i++) st = p.update(s, 0.02);
    return st;
  };
  const auto act = [&](std::initializer_list<const char*> ids) {
    for (const char* id : ids) {
      FS_OK(has(p.choices(), id));
      FS_OK(!p.act(id, s));
    }
  };
  FS_OK(tick().todo == "uPark");
  FS_OK(p.choices().empty());
  s.handbrake = true;
  FS_OK(!tick().todo);
  FS_OK((p.choices() == std::vector<std::string>{"uBrake", "uChocks", "uLeg", "uRed", "uYellow", "uCable", "uOpen"}));
  act({"uChocks", "uBrake", "uLeg", "uCable", "uRed", "uYellow", "uOpen"});
  CouplingStatus st = tick();
  FS_OK(st.todo == "uPull");
  FS_OK(st.uncouple);
  s.coupled = false;
  s.handbrake = false;
  s.speed = 1;
  s.dist = 3;
  FS_OK(!tick().uncouple);
  FS_OK(p.choices() == std::vector<std::string>{"cCheck"});
  s.speed = 0;
  s.handbrake = true;
  s.dist = 9;
  act({"cCheck"});
  FS_OK(tick().todo == "cApproach");
  s.dist = 0.9;
  FS_OK(!tick().todo);
  act({"cHeight"});
  FS_OK(tick().todo == "cLatch");
  s.handbrake = false;
  s.speed = -0.4;
  s.dist = 0.15;
  st = tick(1);
  FS_OK(st.couple);
  FS_OK(st.faults.empty());
  s.coupled = true;
  s.speed = 0;
  s.handbrake = true;
  s.dist = 0;
  tick();
  act({"cLocked", "cYellow", "cRed", "cCable", "cLeg", "cChocks", "cBrake", "cLights"});
  FS_OK(tick().todo == "cTest");
  s.handbrake = false;
  s.speed = 3;
  tick(120);
  FS_OK(p.status.state == "run");
  s.speed = 0;
  FS_OK(tick().state == "done");
  FS_OK(p.done.size() == COUPLING_STEPS.size());
}

void order() {
  test("the order counts: lines only when the trailer is secured, red and yellow each in their turn");
  const CouplingState s{0, true, true, 0, 0};
  CouplingProcedure p;
  p.update(s, 0.02);
  FS_OK(p.act("uRed", s) == "ceSecure");
  FS_OK(p.act("uOpen", s) == "ceOpen");
  FS_OK(!p.done.count("uRed"));
  for (const char* id : {"uBrake", "uChocks", "uLeg"}) FS_OK(!p.act(id, s));
  FS_OK(p.act("uYellow", s) == "ceAir");
  FS_OK(!p.act("uRed", s));
  FS_OK(!p.act("uYellow", s));
  FS_OK(!p.act("uRed", s));
  FS_OK(!p.onFoot({0.5, true, true, 0, 0}));
  FS_OK(!p.onFoot({0, false, true, 0, 0}));
  FS_OK(!p.act("uCable", {0, false, true, 0, 0}));
  FS_OK(!p.done.count("uCable"));

  CouplingProcedure q;
  for (const char* id : {"uPark", "uBrake", "uChocks", "uLeg", "uRed", "uYellow", "uCable", "uOpen", "uPull"}) q.done.insert(id);
  const CouplingStatus st = q.update({-1.5, false, false, 0.1, 0.02}, 0.02);
  FS_OK(st.couple);
  FS_OK((st.faults == std::vector<std::string>{"ceHeight", "ceFast"}));
  const CouplingState t{0, true, true, 0, 0};
  q.update(t, 0.02);
  FS_OK(q.act("cYellow", t) == "ceLocked");
  FS_OK(!q.act("cLocked", t));
  FS_OK(q.act("cRed", t) == "ceAir");
  FS_OK(q.act("cChocks", t) == "ceRelease");
  FS_OK(q.act("cLights", t) == "ceLights");
}

void lessons() {
  test("the lessons of the truck: three on the yard, then the test with all of them before the drive");
  std::vector<const LessonDef*> truck, semi;
  for (const LessonDef& l : LESSONS) {
    const std::vector<VehicleKind> v = l.vehicles ? *l.vehicles : l.vehicle ? std::vector<VehicleKind>{*l.vehicle} : std::vector<VehicleKind>{};
    if (has(v, "truck")) truck.push_back(&l);
    if (l.vehicles && has(*l.vehicles, "semi")) semi.push_back(&l);
  }
  FS_OK(semi == truck);
  FS_OK(truck.size() == 4 && truck[0]->id == "ceCouple" && truck[1]->id == "ceCorner" && truck[2]->id == "ceRamp" && truck[3]->id == "ceExam");
  FS_OK(truck[3]->kind == "exam" && truck[3]->tasks && (*truck[3]->tasks == std::vector<std::string>{"couple", "corner", "ramp"}));
  FS_OK(truck[3]->targets.size() >= 4);
}

void wholeRun() {
  test("a whole coupling run in the right order is without fault, whatever order the list is shown in");
  CouplingProcedure p;
  CouplingState s{0, true, true, 0, 0};
  std::vector<std::string> faults;
  const auto tick = [&](int n = 1) {
    for (int i = 0; i < n; i++) {
      const CouplingStatus& st = p.update(s, 0.05);
      faults.insert(faults.end(), st.faults.begin(), st.faults.end());
      if (st.uncouple) s.coupled = false;
      if (st.couple) s.coupled = true;
    }
  };
  const auto act = [&](std::initializer_list<const char*> ids) {
    for (const char* id : ids) {
      FS_OK(has(p.choices(), id));
      FS_OK(!p.act(id, s));
    }
  };
  tick();
  act({"uChocks", "uBrake", "uLeg", "uCable", "uRed", "uYellow", "uOpen"});
  s.handbrake = false;
  s.speed = 1;
  tick();
  s.dist = 3;
  tick();
  s.speed = 0;
  s.handbrake = true;
  tick();
  act({"cCheck"});
  s.dist = 1;
  tick();
  act({"cHeight"});
  s.handbrake = false;
  s.speed = -0.3;
  s.dist = 0.1;
  tick();
  FS_OK(s.coupled);
  s.speed = 0;
  s.handbrake = true;
  tick();
  act({"cLocked", "cLeg", "cYellow", "cRed", "cCable", "cLights", "cChocks", "cBrake"});
  s.handbrake = false;
  s.speed = 2;
  tick(60);
  s.speed = 0;
  tick();
  FS_OK(faults.empty());
  FS_OK(p.status.state == "done");
  FS_OK(p.done.size() == COUPLING_STEPS.size());
}

void chamfers() {
  test("the junction of the corner task has cut-off corners: they are road, beyond the cut is not");
  const TaskLayout lay = taskLayout("corner");
  FS_OK(lay.chamfers.size() == 2);
  FS_OK(offArea(lay, -16, 5) == 0);
  FS_OK(offArea(lay, -25, 5) == 0);
  FS_OK(offArea(lay, -14, 3) > 1.3);
  const double d = offArea(lay, -15 + 0.5 / std::sqrt(2.0), 4 - 0.5 / std::sqrt(2.0));
  FS_OK(near(d, 0.5));
  FS_OK(offArea(lay, 0, 9.5) == 0);
  FS_OK(near(offArea(lay, 0, 14), 1));
  FS_OK(offArea(lay, 0, 14, 1.2) == 0);
  FS_OK(taskLayout("ramp").chamfers.empty());
  FS_OK(!taskLayout("couple").goalMarked);
}

void kerbs() {
  test("the lines are kerbs: a wheel over them is a fault, the body swinging over them is not");
  const Pose trailer{-18.5, 5.5, EAST - 0.803};
  const Pose truck{-10.8, 10.29, EAST - 0.227};
  ManoeuvreJudge clean("corner");
  FS_OK(clean.update({truck, trailer, -0.5, false}, 0.05).faults.empty());
  const Pose across{-4, 10.9, EAST - 0.5};
  const auto nose = corners(across, TRUCK_OUTLINE.front, TRUCK_OUTLINE.rear, TRUCK_OUTLINE.hw);
  FS_OK(nose[3] > 13.4);
  ManoeuvreJudge swing("corner");
  FS_OK(swing.update({across, {-13.2, 8.4, EAST - 0.05}, -0.5, false}, 0.05).faults.empty());
  ManoeuvreJudge over("corner");
  FS_OK(over.update({{across.x, 11.6, across.yaw}, {-13.2, 8.4, EAST - 0.05}, -0.5, false}, 0.05).faults == std::vector<std::string>{"ceLine"});
}

}  // namespace

int main() {
  frames();
  layouts();
  corner();
  excursions();
  corrections();
  ramp();
  couplingInOrder();
  order();
  lessons();
  wholeRun();
  chamfers();
  kerbs();
  std::puts("ce_test: ok");
  return 0;
}

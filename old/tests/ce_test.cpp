// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Class CE on the practice yard: the layouts of the two basic manoeuvres,
 * their judging, the order of coupling and uncoupling, and the texts and
 * citations the tasks are shown with.
 *
 * Origin: tests/game/ce.test.ts (MaurerAnton/driving-simulator @ ff37c5d, v0.7.0).
 */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../src/game/ce.h"
#include "../src/game/nav.h"
#include "../src/i18n/index.h"
#include "../src/i18n/violations.h"

using namespace fs;

namespace {

constexpr double EAST = -M_PI / 2;
constexpr double SOUTH = M_PI;

bool nearD(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

/** assert with a message (the C preprocessor's assert() cannot carry one) */
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      std::abort();                                                    \
    }                                                                  \
  } while (0)

/** the combination standing straight at a pose */
RigState straight(const Pose& truck, double speed = 0, bool handbrake = true) {
  RigState r;
  r.truck = truck;
  r.trailer = trailerBehind(truck);
  r.speed = speed;
  r.handbrake = handbrake;
  return r;
}

/** the key exists in the language's dictionary */
bool hasKey(const std::string& code, const std::string& key) {
  const Dict& d = rawDictOf(code);
  for (const auto& kv : d)
    if (kv.first == key) return true;
  return false;
}

std::string refOf(const std::string& id) {
  for (const auto& kv : VIOLATION_REFS)
    if (kv.first == id) return kv.second;
  return "";
}

}  // namespace

static void frameTest() {
  for (int rot = 0; rot < 2; rot++) {
    YardJson y;
    y.x = 305;
    y.z = 1485;
    y.len = 150;
    y.wid = 66;
    y.rot = rot;
    YardFrame f(y);
    auto xz = f.toWorld(40, -10);
    auto uw = f.toLocal(xz[0], xz[1]);
    CHECK(nearD(uw[0], 40) && nearD(uw[1], -10));
    CHECK(nearD(std::hypot(xz[0] - 305, xz[1] - 1485), std::hypot(40.0, 10.0)));
    // a vehicle heading along +u moves along +u
    Pose p = f.poseToWorld({0, 0, EAST});
    auto ahead = f.toLocal(p.x - std::sin(p.yaw) * 5, p.z - std::cos(p.yaw) * 5);
    CHECK(nearD(ahead[0], 5) && nearD(ahead[1], 0));
    CHECK(nearD(f.yawToLocal(p.yaw), EAST));
  }
  YardFrame along({0, 0, 150, 66, 0});
  auto e = along.toWorld(10, 0);
  CHECK(nearD(e[0], 10) && nearD(e[1], 0));
  YardFrame across({0, 0, 150, 66, 1});
  auto n = across.toWorld(10, 0);
  CHECK(nearD(n[0], 0) && nearD(n[1], -10));
}

static void layoutsFit() {
  for (const char* id : {"corner", "ramp", "couple"}) {
    TaskLayout lay = taskLayout(id);
    std::vector<Rect> all = lay.allowed;
    all.push_back(lay.goal);
    if (lay.hasRamp) all.push_back(lay.ramp);
    for (const Rect& r : all)
      CHECK(r.u0 < r.u1 && r.w0 < r.w1 && r.u0 >= -75 && r.u1 <= 75 && r.w0 >= -33 && r.w1 <= 33);
    Pose trailer = trailerBehind(lay.start);
    auto pts = corners(lay.start, TRUCK_OUTLINE.front, TRUCK_OUTLINE.rear, TRUCK_OUTLINE.hw);
    auto tp = corners(trailer, TRAILER_OUTLINE.front, TRAILER_OUTLINE.rear, TRAILER_OUTLINE.hw);
    pts.insert(pts.end(), tp.begin(), tp.end());
    for (size_t i = 0; i < pts.size(); i += 2) {
      bool on = false;
      for (const Rect& r : lay.allowed)
        if (pts[i] >= r.u0 && pts[i] <= r.u1 && pts[i + 1] >= r.w0 && pts[i + 1] <= r.w1) on = true;
      CHECK(on);
    }
    // 15.7 m from the front of the truck to the rear end of the trailer
    double len = TRUCK_OUTLINE.front + TRUCK_OUTLINE.hitch + TRAILER_OUTLINE.hitch +
                 TRAILER_OUTLINE.rear;
    CHECK(nearD(len, 15.7, 0.01));
    double mx = pts[0], mn = pts[0];
    for (size_t i = 0; i < pts.size(); i += 2) {
      mx = std::max(mx, pts[i]);
      mn = std::min(mn, pts[i]);
    }
    CHECK(nearD(mx - mn, len, 0.01));  // standing in line
  }
}

static void cornerJudging() {
  // at the start: on the main road, nothing achieved
  ManoeuvreJudge j("corner");
  for (int i = 0; i < 100; i++) j.update(straight(j.layout.start), 0.02);
  CHECK(j.status.state == "run");
  CHECK(j.status.excursions == 0);
  // standing in the side road, front towards the main road
  Pose inSide{-20.5, -8, SOUTH};
  CHECK(j.arrived(straight(inSide)));
  CHECK(!j.arrived(straight({inSide.x, 2, inSide.yaw})));      // the front still on the main road
  CHECK(!j.arrived(straight({inSide.x, inSide.z, SOUTH + 0.25})));  // askew
  CHECK(!j.arrived(straight({-17.8, inSide.z, inSide.yaw})));  // half a metre over the edge
  // rolling it is not done, without the handbrake neither
  for (int i = 0; i < 100; i++) j.update(straight(inSide, -0.5, false), 0.02);
  CHECK(j.status.state == "run");
  for (int i = 0; i < 100; i++) j.update(straight(inSide, 0, false), 0.02);
  CHECK(j.status.state == "run");
  for (int i = 0; i < 100; i++) j.update(straight(inSide, 0, true), 0.02);
  CHECK(j.status.state == "done");
}

static void excursions() {
  ManoeuvreJudge j("corner");
  Pose on = j.layout.start;
  Pose off{on.x, on.z + 3, on.yaw};  // 0.75 m over the southern edge
  const bool flags[6] = {false, true, true, false, true, false};
  int faults = 0;
  for (int k = 0; k < 6; k++) {
    ManoeuvreStatus st = j.update(straight(flags[k] ? off : on), 0.02);
    for (const std::string& f : st.faults)
      if (f == "ceLine") faults++;
  }
  CHECK(faults == 2);  // one fault for each time the line is crossed, not one per moment
  CHECK(j.status.state == "run");
  j.update(straight(off), 0.02);
  j.update(straight(on), 0.02);
  j.update(straight(off), 0.02);
  CHECK(j.status.state == "failed");
  ManoeuvreJudge far("corner");
  Pose farOff{far.layout.start.x, far.layout.start.z + 8, far.layout.start.yaw};
  far.update(straight(farOff), 0.02);
  CHECK(far.status.state == "failed");  // far off the area: over at once
}

static void corrections() {
  ManoeuvreJudge j("ramp");
  Pose rig = j.layout.start;
  auto go = [&](double speed, double seconds) {
    for (int i = 0; i < static_cast<int>(seconds * 50); i++)
      j.update(straight(rig, speed, false), 0.02);
  };
  go(1, 3);  // driving forward before ever reversing is no correction
  CHECK(j.status.corrections == 0);
  for (int k = 1; k <= MAX_CORRECTIONS; k++) {
    go(-1, 3);
    go(1, 2);
    CHECK(j.status.corrections == k);
    CHECK(j.status.state == "run");
  }
  go(-1, 3);
  go(0.2, 1);  // rolling forward a hand's width is none
  CHECK(j.status.corrections == MAX_CORRECTIONS);
  go(1, 2);
  CHECK(j.status.state == "failed");
}

static void rampJudging() {
  ManoeuvreJudge j("ramp");
  const TaskLayout& lay = j.layout;
  const Rect& ramp = lay.ramp;
  auto at = [&](double gap, double w = -12, double yaw = EAST) {
    double reach = TRUCK_OUTLINE.hitch + TRAILER_OUTLINE.hitch + TRAILER_OUTLINE.rear;
    return straight(Pose{ramp.u1 + gap + reach * std::cos(yaw - EAST), w, yaw});
  };
  CHECK(j.arrived(at(0.5)));
  CHECK(!j.arrived(at(1.6)));          // too far from the ramp
  CHECK(!j.arrived(at(0.5, -13.2)));   // beside the bay
  CHECK(!j.arrived(at(0.5, -12, EAST + 0.12)));  // askew
  for (int i = 0; i < 60; i++) j.update(at(0.5), 0.02);
  CHECK(j.status.state == "done");
  CHECK(nearD(j.status.gap, 0.5, 0.01));
  ManoeuvreJudge hit("ramp");
  ManoeuvreStatus st = hit.update(at(-0.05), 0.02);
  CHECK(st.state == "failed");
  CHECK(st.faults.size() == 1 && st.faults[0] == "ceRamp");
}

static void chamfers() {
  TaskLayout lay = taskLayout("corner");
  CHECK(lay.chamfers.size() == 2);
  // a metre along each leg from the east corner of the junction (-17, 6)
  CHECK(offArea(lay, -16, 5) == 0);
  CHECK(offArea(lay, -25, 5) == 0);  // the west corner as well
  // beyond the cut
  CHECK(offArea(lay, -14, 3) > 1.3);
  // half a metre beyond the middle of the cut, at right angles to it
  double d = offArea(lay, -15 + 0.5 / std::sqrt(2.0), 4 - 0.5 / std::sqrt(2.0));
  CHECK(nearD(d, 0.5));
  // the roads themselves, and the strip of asphalt beyond their lines
  CHECK(offArea(lay, 0, 9.5) == 0);
  CHECK(nearD(offArea(lay, 0, 14), 1));
  CHECK(offArea(lay, 0, 14, 1.2) == 0);
  // the other tasks have no junction
  CHECK(taskLayout("ramp").chamfers.empty());
  CHECK(taskLayout("couple").goalMarked == false);
}

static void kerbs() {
  // half way round the corner (a pose from a clean run): everything on the road
  Pose trailer{-18.5, 5.5, EAST - 0.803};
  Pose truck{-10.8, 10.29, EAST - 0.227};
  ManoeuvreJudge clean("corner");
  RigState cleanRig{truck, trailer, -0.5, false};
  CHECK(clean.update(cleanRig, 0.05).faults.empty());
  // the truck turned across the road: its front corner hangs over the southern line
  Pose across{-4, 10.9, EAST - 0.5};
  auto nose = corners(across, TRUCK_OUTLINE.front, TRUCK_OUTLINE.rear, TRUCK_OUTLINE.hw);
  CHECK(nose[3] > 13.4);  // the front right corner is over the line at 13
  ManoeuvreJudge swing("corner");
  RigState swingRig{across, {-13.2, 8.4, EAST - 0.05}, -0.5, false};
  CHECK(swing.update(swingRig, 0.05).faults.empty());
  // half a metre further and the front wheel is over the kerb
  ManoeuvreJudge over("corner");
  Pose further{across.x, 11.6, across.yaw};
  RigState overRig{further, {-13.2, 8.4, EAST - 0.05}, -0.5, false};
  ManoeuvreStatus st = over.update(overRig, 0.05);
  CHECK(st.faults.size() == 1 && st.faults[0] == "ceLine");
}

static void couplingOrder() {
  CouplingProcedure p;
  CouplingState s;  // speed 0, no handbrake, coupled, dist 0, angle 0
  s.coupled = true;
  auto tick = [&](int n = 5) {
    CouplingStatus st = p.update(s, 0.02);
    for (int i = 1; i < n; i++) st = p.update(s, 0.02);
    return st;
  };
  auto has = [](const std::vector<std::string>& v, const std::string& id) {
    return std::find(v.begin(), v.end(), id) != v.end();
  };
  auto act = [&](std::initializer_list<const char*> ids) {
    for (const char* id : ids) {
      CHECK(has(p.choices(), id));
      CHECK(p.act(id, s).empty());
    }
  };
  CHECK(tick().todo == "uPark");
  CHECK(p.choices().empty());  // nothing to do on foot before the combination is parked
  s.handbrake = true;
  CHECK(tick().todo == "");    // parked: now on foot
  CHECK((p.choices() == std::vector<std::string>{"uBrake", "uChocks", "uLeg", "uRed", "uYellow",
                                                "uCable", "uOpen"}));
  act({"uChocks", "uBrake", "uLeg", "uCable", "uRed", "uYellow", "uOpen"});
  CouplingStatus st = tick();
  CHECK(st.todo == "uPull");
  CHECK(st.uncouple);  // the open coupling lets the trailer go
  s.coupled = false;
  s.handbrake = false;
  s.speed = 1;
  s.dist = 3;
  CHECK(!tick().uncouple);
  CHECK((p.choices() == std::vector<std::string>{"cCheck"}));  // only what can be done before backing up
  s.speed = 0;
  s.handbrake = true;
  s.dist = 9;
  act({"cCheck"});
  CHECK(tick().todo == "cApproach");
  s.dist = 0.9;
  CHECK(tick().todo == "");  // stopped a metre before the eye: out to compare the heights
  act({"cHeight"});
  CHECK(tick().todo == "cLatch");
  s.handbrake = false;
  s.speed = -0.4;
  s.dist = 0.15;
  st = tick(1);
  CHECK(st.couple);
  CHECK(st.faults.empty());
  s.coupled = true;
  s.speed = 0;
  s.handbrake = true;
  s.dist = 0;
  tick();
  act({"cLocked", "cYellow", "cRed", "cCable", "cLeg", "cChocks", "cBrake", "cLights"});
  CHECK(tick().todo == "cTest");
  s.handbrake = false;
  s.speed = 3;
  tick(120);
  CHECK(p.status.state == "run");  // driving is not yet the brake test
  s.speed = 0;
  CHECK(tick().state == "done");
  CHECK(p.done.size() == COUPLING_STEPS.size());
  // the test knows every step on foot
  std::vector<std::string> order = {"uBrake", "uChocks", "uLeg", "uRed", "uYellow", "uCable",
                                    "uOpen", "cCheck", "cHeight", "cLocked", "cYellow", "cRed",
                                    "cCable", "cLeg", "cChocks", "cBrake", "cLights"};
  std::sort(order.begin(), order.end());
  std::vector<std::string> acts;
  for (const CouplingStep& c : COUPLING_STEPS)
    if (std::string(c.kind) == "act") acts.push_back(c.id);
  std::sort(acts.begin(), acts.end());
  CHECK(order == acts);
}

static void orderCounts() {
  CouplingState s;
  s.handbrake = true;
  s.coupled = true;
  CouplingProcedure p;
  p.update(s, 0.02);
  CHECK(p.act("uRed", s) == "ceSecure");   // air lines off an unsecured trailer
  CHECK(p.act("uOpen", s) == "ceOpen");    // the coupling opened with the lines still on
  CHECK(!std::count(p.done.begin(), p.done.end(), "uRed"));  // out of turn: not done
  CHECK(p.act("uBrake", s).empty());
  CHECK(p.act("uChocks", s).empty());
  CHECK(p.act("uLeg", s).empty());
  CHECK(p.act("uYellow", s) == "ceAir");   // uncoupling: red first
  CHECK(p.act("uRed", s).empty());
  CHECK(p.act("uYellow", s).empty());
  CHECK(p.act("uRed", s).empty());         // a step already done is neither done again nor a fault
  CouplingState rolling = s;
  rolling.speed = 0.5;
  CHECK(!p.onFoot(rolling));
  CouplingState loose = s;
  loose.handbrake = false;
  CHECK(!p.onFoot(loose));
  CHECK(p.act("uCable", loose).empty());
  CHECK(!std::count(p.done.begin(), p.done.end(), "uCable"));

  // coupling: straight on to the trailer without stopping to look is a fault, and so is bumping
  CouplingProcedure q;
  for (const char* id : {"uPark", "uBrake", "uChocks", "uLeg", "uRed", "uYellow", "uCable",
                         "uOpen", "uPull"})
    q.done.push_back(id);
  CouplingState rush;
  rush.speed = -1.5;
  rush.coupled = false;
  rush.dist = 0.1;
  rush.angle = 0.02;
  CouplingStatus st = q.update(rush, 0.02);
  CHECK(st.couple);
  CHECK((st.faults == std::vector<std::string>{"ceHeight", "ceFast"}));
  CouplingState t;
  t.handbrake = true;
  t.coupled = true;
  q.update(t, 0.02);
  CHECK(q.act("cYellow", t) == "ceLocked");  // lines on before the coupling was checked
  CHECK(q.act("cLocked", t).empty());
  CHECK(q.act("cRed", t) == "ceAir");         // coupling: yellow first – red never alone
  CHECK(q.act("cChocks", t) == "ceRelease");  // chocks away before the brakes have air
  CHECK(q.act("cLights", t) == "ceLights");   // lights checked before the cable is in
}

static void textsAndRefs() {
  for (const char* code : {"en", "de"}) {
    for (const CouplingStep& step : COUPLING_STEPS) {
      std::string key = std::string(step.kind) == "act" ? "ce.step." + std::string(step.id)
                                                        : "ce.do." + std::string(step.id);
      CHECK(hasKey(code, key));
    }
    for (const char* task : {"corner", "ramp"}) CHECK(hasKey(code, std::string("ce.do.") + task));
    const char* faults[] = {"ceLine", "ceCorrections", "ceRamp", "ceSecure", "ceAir", "ceOpen",
                            "ceLocked", "ceRelease", "ceLights", "ceHeight", "ceFast",
                            "truckRightTurn", "truckSpeed"};
    for (const char* id : faults) {
      CHECK(hasKey(code, std::string("v.") + id + ".title"));
      CHECK(hasKey(code, std::string("v.") + id + ".why"));
      CHECK(!refOf(id).empty());
    }
    // the faults the catalogue itself names
    for (const CouplingStep& step : COUPLING_STEPS)
      if (step.why) {
        bool known = false;
        for (const char* id : faults)
          if (std::string(step.why) == id) known = true;
        CHECK(known);
      }
  }
}

static void truckCitations() {
  auto fev = citationParts(refOf("ceAir"));
  CHECK(!fev.empty());
  CHECK(fev[0].text == "Anlage 7");
  CHECK(fev[0].url == "https://www.gesetze-im-internet.de/fev_2010/anlage_7.html");
  std::string joined;
  for (const auto& p : fev) joined += p.text;
  CHECK(joined == refOf("ceAir"));
  // an annex of the StVO still leads to the StVO
  auto annex = citationParts("Anlage 3 StVO, Zeichen 325.1");
  CHECK(!annex.empty());
  CHECK(annex[0].url == "https://www.gesetze-im-internet.de/stvo_2013/anlage_3.html");
  std::vector<std::string> speedUrls;
  for (const auto& p : citationParts(refOf("truckSpeed")))
    if (!p.url.empty()) speedUrls.push_back(p.url);
  CHECK((speedUrls == std::vector<std::string>{"https://www.gesetze-im-internet.de/stvo_2013/__3.html",
                                               "https://www.gesetze-im-internet.de/stvo_2013/__18.html"}));
  auto turn = citationParts(refOf("truckRightTurn"));
  CHECK(!turn.empty());
  CHECK(turn[0].url == "https://www.gesetze-im-internet.de/stvo_2013/__9.html");
}

static void truckLessons() {
  std::vector<const LessonDef*> truck;
  for (const auto& l : lessons())
    if (l.vehicle == "truck") truck.push_back(&l);
  CHECK(truck.size() == 4);
  CHECK(truck[0]->id == "ceCouple" && truck[1]->id == "ceCorner" && truck[2]->id == "ceRamp" &&
        truck[3]->id == "ceExam");
  CHECK((truck[0]->tasks == std::vector<std::string>{"couple"}));
  CHECK((truck[1]->tasks == std::vector<std::string>{"corner"}));
  CHECK((truck[2]->tasks == std::vector<std::string>{"ramp"}));
  CHECK(truck[3]->kind == "exam");
  CHECK((truck[3]->tasks == std::vector<std::string>{"couple", "corner", "ramp"}));
  CHECK(truck[3]->targets.size() >= 4);  // a drive in traffic follows the yard
  for (const auto& l : lessons()) {
    for (const char* code : {"en", "de"}) {
      CHECK(hasKey(code, "lesson." + l.id + ".title"));
      CHECK(hasKey(code, "lesson." + l.id + ".intro"));
    }
    for (const std::string& t : l.tasks) (void)taskLayout(t);  // never throws / returns a layout
  }
  // the car keeps its own lessons; the routes are for both
  bool carExam = false, sharedRoute = false;
  for (const auto& l : lessons()) {
    if (l.vehicle == "car" && l.kind == "exam") carExam = true;
    if (l.vehicle.empty() && l.kind == "route") sharedRoute = true;
  }
  CHECK(carExam);
  CHECK(sharedRoute);
}

static void wholeCouplingRun() {
  CouplingProcedure p;
  CouplingState s;
  s.handbrake = true;
  s.coupled = true;
  std::vector<std::string> faults;
  auto tick = [&](int n = 1) {
    for (int i = 0; i < n; i++) {
      CouplingStatus st = p.update(s, 0.05);
      faults.insert(faults.end(), st.faults.begin(), st.faults.end());
      if (st.uncouple) s.coupled = false;
      if (st.couple) s.coupled = true;
    }
  };
  auto act = [&](std::initializer_list<const char*> ids) {
    for (const char* id : ids) {
      CHECK(std::find(p.choices().begin(), p.choices().end(), std::string(id)) !=
            p.choices().end());
      CHECK(p.act(id, s).empty());
    }
  };
  tick();
  act({"uChocks", "uBrake", "uLeg", "uCable", "uRed", "uYellow", "uOpen"});
  // pull away from the trailer
  s.handbrake = false;
  s.speed = 1;
  tick();
  s.dist = 3;
  tick();
  s.speed = 0;
  s.handbrake = true;
  tick();
  act({"cCheck"});
  // back to a metre before the eye, stop, look
  s.dist = 1;
  tick();
  act({"cHeight"});
  // the last metre at a crawl
  s.handbrake = false;
  s.speed = -0.3;
  s.dist = 0.1;
  tick();
  CHECK(s.coupled);
  s.speed = 0;
  s.handbrake = true;
  tick();
  act({"cLocked", "cLeg", "cYellow", "cRed", "cCable", "cLights", "cChocks", "cBrake"});
  // the brake test
  s.handbrake = false;
  s.speed = 2;
  tick(60);
  s.speed = 0;
  tick();
  CHECK(faults.empty());
  CHECK(p.status.state == "done");
  CHECK(p.done.size() == COUPLING_STEPS.size());
}

int main() {
  frameTest();
  layoutsFit();
  cornerJudging();
  excursions();
  corrections();
  rampJudging();
  chamfers();
  kerbs();
  couplingOrder();
  orderCounts();
  textsAndRefs();
  truckCitations();
  truckLessons();
  wholeCouplingRun();
  std::printf("ce: all checks passed (%zu coupling steps)\n", COUPLING_STEPS.size());
  return 0;
}

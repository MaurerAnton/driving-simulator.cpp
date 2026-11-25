// SPDX-License-Identifier: AGPL-3.0-only
// Origin: tests/game/drills.test.ts @ v1.7.7
/**
 * The basic tasks of class B (src/game/drills.h): each judge driven
 * through its task with scripted poses – done well, and done wrong in the
 * ways the test punishes – and the hill start and the emergency stop with
 * the real car's dynamics.
 */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "game/ce.h"
#include "game/drills.h"
#include "game/nav.h"
#include "sim/vehicle/index.h"
#include "vehicle_harness.h"

using namespace fs;
using namespace fs::harness;

namespace {

const double STEP = 0.05;

struct Run {
  std::unique_ptr<DrillJudge> j;
  Pose pose;
  std::vector<std::string> faults;
  std::vector<std::string> remarks;
  int stops = 0;
  const DrillStatus& status() const { return j->status; }
};

Run start(const DrillId& id, std::function<double()> rand = [] { return 0.5; }) {
  Run r;
  r.j = std::make_unique<DrillJudge>(id, CAR_SHAPE, rand);
  r.pose = r.j->layout.start;
  return r;
}

struct Feed {
  double speed = 0;
  bool handbrake = false;
  double brake = 0;
  bool engine = true;
};

void feed(Run& r, const Feed& s) {
  const DrillStatus& st = r.j->update({.pose = r.pose, .speed = s.speed, .handbrake = s.handbrake, .brake = s.brake, .engine = s.engine}, STEP);
  for (const DrillFault& f : st.faults) r.faults.push_back(f.id + ":" + f.severity);
  if (!st.remark.empty()) r.remarks.push_back(st.remark);
  if (st.stop) r.stops++;
}

double lerpYaw(double a, double b, double t) { return a + std::atan2(std::sin(b - a), std::cos(b - a)) * t; }

/** Move to a pose, forwards (1) or backwards (−1), at about walking pace; then stand. */
void go(Run& r, std::array<double, 3> to, int dir, double brake = 0) {
  const Pose from = r.pose;
  const double dist = std::hypot(to[0] - from.x, to[1] - from.z) + std::abs(lerpYaw(from.yaw, to[2], 1) - from.yaw) * 1.5;
  const int n = std::max(4, int(std::ceil(dist / (1.2 * STEP))));
  const double speed = (dir * dist) / (n * STEP);
  const Pose a = from;
  for (int i = 1; i <= n && r.status().state == "run"; i++) {
    const double t = double(i) / n;
    r.pose = {a.x + (to[0] - a.x) * t, a.z + (to[1] - a.z) * t, lerpYaw(a.yaw, to[2], t)};
    feed(r, {.speed = speed, .brake = brake});
  }
}

/** Stand where it is for a while (with the handbrake or without). */
void stand(Run& r, double seconds, bool handbrake = true) {
  for (double t = 0; t < seconds && r.status().state == "run"; t += STEP) feed(r, {.speed = 0, .handbrake = handbrake});
}

bool has(const Run& r, const std::string& f) { return std::find(r.faults.begin(), r.faults.end(), f) != r.faults.end(); }
bool said(const Run& r, const std::string& k) { return std::find(r.remarks.begin(), r.remarks.end(), k) != r.remarks.end(); }

std::string param(const TrParams& p, const std::string& name) {
  for (const TrParam& x : p)
    if (x.name == name) return x.value;
  return "";
}

std::string joined(const std::vector<std::string>& v) {
  std::string s;
  for (const std::string& x : v) s += x + " ";
  return s;
}

#define CHECK(cond, ...)                                       \
  do {                                                         \
    if (!(cond)) {                                             \
      std::printf("FAIL %s:%d %s – ", __FILE__, __LINE__, #cond); \
      std::printf(__VA_ARGS__);                                \
      std::printf("\n");                                       \
      failures++;                                              \
    }                                                          \
  } while (0)
int failures = 0;

// ---- the layouts

void layouts() {
  for (const DrillId& id : DRILL_IDS) {
    Run r = start(id);
    stand(r, 0.3);
    CHECK(r.faults.empty(), "%s: nothing wrong standing at the start (%s)", id.c_str(), joined(r.faults).c_str());
    CHECK(r.status().state == "run", "%s", id.c_str());
  }
  const DrillLayout p = drillLayout("parallel");
  CHECK(p.goal.u1 - p.goal.u0 >= (CAR_SHAPE.front + CAR_SHAPE.rear) * 1.4, "the gap");
  CHECK(p.parked.size() == 3, "parked");
  const DrillLayout b = drillLayout("bayFwd");
  CHECK(BAYS.width - 2 * CAR_SHAPE.hw > 0.8, "room");
  CHECK(BAYS.depth > CAR_SHAPE.front + CAR_SHAPE.rear + 0.8, "depth");
  CHECK(b.goalMarked, "marked");
}

void ramp() {
  const Hill h = *drillLayout("hill").hill;
  CHECK(hillRise(h, h.u0 - 1, 0) == 0, "before");
  const double grade = (hillRise(h, h.u1 - 1, 0) - hillRise(h, h.u1 - 3, 0)) / 2;
  CHECK(std::abs(grade - 0.1) < 1e-9, "grade %g", grade);
  CHECK(hillRise(h, (h.u1 + h.u2) / 2, 0) == h.h, "top");
  CHECK(hillRise(h, (h.u1 + h.u2) / 2, h.w1 + h.side) == 0, "toe");
  CHECK(hillRise(h, (h.u1 + h.u2) / 2, h.w1 + h.side / 2) > 0, "side");
  CHECK(hillRise(h, HILL_LINE, 0) > 0.5, "the line is on the slope");
}

void rects() {
  const std::array<double, 8> a = {0, 0, 2, 0, 2, 1, 0, 1};
  CHECK(rectsOverlap(a, {1, 0.5, 3, 0.5, 3, 2, 1, 2}), "overlap");
  CHECK(!rectsOverlap(a, {2.1, 0, 4, 0, 4, 1, 2.1, 1}), "apart");
  const double d = 0.75;
  CHECK(!rectsOverlap(a, {2 + d + 0.05, 0.5, 2 + 2 * d + 0.05, 0.5 - d, 2 + 3 * d + 0.05, 0.5, 2 + 2 * d + 0.05, 0.5 + d}), "turned");
}

// ---- parallel parking

const double K = PARALLEL.kerb;
double gapEnd() { return drillLayout("parallel").goal.u1; }

void parallelIn(Run& r, double finalW = K - 0.15 - CAR_SHAPE.hw) {
  const double G = gapEnd();
  go(r, {G + CAR_SHAPE.rear, r.pose.z, EAST}, 1);
  stand(r, 0.4, false);
  go(r, {5.0, 3.0, EAST + 0.7}, -1);
  go(r, {3.6, 4.1, EAST + 0.6}, -1);
  go(r, {3.1, finalW, EAST}, -1);
}

void parallel() {
  {
    Run r = start("parallel");
    parallelIn(r);
    CHECK(r.status().step >= 4, "step %d", r.status().step);
    stand(r, 1.2);
    CHECK(r.status().state == "done", "%s", joined(r.remarks).c_str());
    CHECK(r.faults.empty(), "%s", joined(r.faults).c_str());
    CHECK(std::abs(r.status().kerb - 0.15) < 0.01, "kerb %g", r.status().kerb);
    CHECK(r.j->result().stars == 3, "stars");
    CHECK(r.status().corrections == 0, "corrections");
  }
  {
    Run r = start("parallel");
    parallelIn(r, K - 0.62 - CAR_SHAPE.hw);
    stand(r, 2.5);
    CHECK(r.status().state == "run", "far: running");
    CHECK(said(r, "drill.r.kerbFar"), "%s", joined(r.remarks).c_str());
    Run r2 = start("parallel");
    parallelIn(r2, K - 0.4 - CAR_SHAPE.hw);
    stand(r2, 1.2);
    CHECK(r2.status().state == "done", "40 cm");
    CHECK(has(r2, "drillKerbFar:minor"), "noted");
    CHECK(r2.j->result().stars < 3, "stars");
  }
  {
    Run r = start("parallel");
    const double G = gapEnd();
    go(r, {G + CAR_SHAPE.rear, r.pose.z, EAST}, 1);
    go(r, {G + 1, 4.8, EAST}, -1);
    CHECK(r.status().state == "failed", "touch: failed");
    CHECK(has(r, "drillTouch:fail"), "touch");
    CHECK(r.stops > 0, "stopped");
    CHECK(r.j->result().stars == 0, "no stars");
  }
  {
    Run r = start("parallel");
    parallelIn(r, K + 0.5 - CAR_SHAPE.hw);
    CHECK(has(r, "drillKerbTouch:minor"), "kerb touch");
    CHECK(has(r, "drillKerbOver:fail"), "kerb over");
    CHECK(r.status().state == "failed", "failed");
    Run c = start("parallel");
    parallelIn(c);
    for (int k = 0; k < 3; k++) {
      go(c, {c.pose.x + 0.6, c.pose.z, EAST}, 1);
      go(c, {c.pose.x - 0.6, c.pose.z, EAST}, -1);
    }
    CHECK(has(c, "drillCorrections:fail"), "%s", joined(c.faults).c_str());
    CHECK(said(c, "drill.r.correction"), "said");
  }
}

// ---- bay parking

void bays() {
  {
    Run r = start("bayFwd");
    go(r, {-4.5, r.pose.z, EAST}, 1);
    go(r, {-1.0, 5.5, EAST - M_PI / 4}, 1);
    go(r, {0, 9.9, SOUTH}, 1);
    CHECK(r.status().step == 3, "step %d", r.status().step);
    stand(r, 1.2);
    CHECK(r.status().state == "done", "%s %s", joined(r.faults).c_str(), joined(r.remarks).c_str());
    CHECK(r.faults.empty(), "faults");
    CHECK(r.j->result().stars == 3, "stars");
  }
  {
    Run r = start("bayFwd");
    go(r, {-4.5, r.pose.z, EAST}, 1);
    go(r, {-1.0, 5.5, EAST - M_PI / 4}, 1);
    go(r, {0, 8.0, SOUTH}, 1);
    stand(r, 2);
    CHECK(r.status().state == "run", "half out");
    CHECK(said(r, "drill.r.notInBay"), "notInBay");
    Run w = start("bayFwd");
    w.pose = {0, 9.9, NORTH};
    stand(w, 2);
    CHECK(said(w, "drill.r.noseIn"), "noseIn");
  }
  {
    Run r = start("bayBack");
    go(r, {5.0, r.pose.z, EAST}, 1);
    stand(r, 0.3, false);
    go(r, {2.6, 4.4, -1.1}, -1);
    go(r, {0.5, 6.4, -0.3}, -1);
    go(r, {0, 9.9, NORTH}, -1);
    stand(r, 1.2);
    CHECK(r.status().state == "done", "%s %s", joined(r.faults).c_str(), joined(r.remarks).c_str());
    CHECK(r.faults.empty(), "faults");
    CHECK(r.status().corrections == 0, "corrections");
  }
  {
    Run r = start("bayBack");
    go(r, {5.0, r.pose.z, EAST}, 1);
    go(r, {BAYS.width, 9.9, NORTH}, -1);
    CHECK(has(r, "drillTouch:fail"), "touch");
  }
}

// ---- reversing

void reversing() {
  {
    Run r = start("backStraight");
    go(r, {-27, 0, EAST}, -1);
    stand(r, 1.2);
    CHECK(r.status().state == "done", "straight");
    CHECK(r.faults.empty(), "faults");
    Run off = start("backStraight");
    go(off, {-10, 1.0, EAST}, -1);
    CHECK(has(off, "drillLine:major"), "line");
    go(off, {-20, 4.5, EAST}, -1);
    CHECK(has(off, "drillLine:fail"), "far");
  }
  const auto cornerPath = [](Run& r, std::array<double, 3> end) {
    go(r, {8, r.pose.z, EAST}, 1);
    stand(r, 0.3, false);
    go(r, {5, r.pose.z, EAST}, -1);
    go(r, {2.4, -0.8, -M_PI / 3}, -1);
    go(r, {0.6, 1.0, -M_PI / 6}, -1);
    go(r, {end[0], 3.0, -0.08}, -1);
    go(r, end, -1);
  };
  {
    Run r = start("backCorner");
    cornerPath(r, {-0.895 - 0.3, 6.5, NORTH});
    stand(r, 1.2);
    CHECK(r.status().state == "done", "%s %s", joined(r.faults).c_str(), joined(r.remarks).c_str());
    CHECK(r.faults.empty(), "faults");
    CHECK(r.j->result().stars >= 2, "stars");
  }
  {
    Run r = start("backCorner");
    go(r, {8, r.pose.z, EAST}, 1);
    go(r, {3.5, 1.5, -M_PI / 4}, -1);
    CHECK(has(r, "drillKerbOver:fail"), "%s", joined(r.faults).c_str());
    Run far = start("backCorner");
    cornerPath(far, {-0.895 - 1.6, 6.5, NORTH});
    stand(far, 2);
    CHECK(far.status().state == "run", "far");
    CHECK(said(far, "drill.r.kerbFar"), "kerbFar");
  }
}

// ---- turning round

void turning() {
  {
    Run r = start("turn");
    go(r, {-12.2, 2.2, EAST + 1.1}, 1);
    CHECK(r.status().step == 0, "step 0");
    go(r, {-13.8, 4.6, EAST + 2.1}, -1);
    CHECK(r.status().step == 1, "step 1");
    go(r, {-17, 1.8, WEST}, 1);
    stand(r, 0.5, false);
    CHECK(r.status().state == "done", "%s %s", joined(r.faults).c_str(), joined(r.remarks).c_str());
    CHECK(r.faults.empty(), "faults");
    CHECK(r.j->moveCount() == 3, "moves");
  }
  {
    Run r = start("turn");
    go(r, {-11, 0.6, EAST + 1.4}, 1);
    CHECK(has(r, "drillKerbOver:fail"), "%s", joined(r.faults).c_str());
  }
}

// ---- the hill start (scripted)

const double atLine = HILL_LINE - CAR_SHAPE.front - 0.4;

void hillScripted() {
  {
    Run r = start("hill");
    go(r, {atLine, 0, EAST}, 1);
    stand(r, 0.2, false);
    CHECK(r.status().step == 1, "at line");
    stand(r, 0.7, true);
    CHECK(r.status().step == 2, "secured");
    go(r, {atLine - 0.1, 0, EAST}, -1);
    go(r, {atLine + 4.5, 0, EAST}, 1);
    CHECK(r.status().state == "done", "done");
    CHECK(r.faults.empty(), "%s", joined(r.faults).c_str());
    CHECK(std::abs(r.status().rollback - 0.1) < 0.02, "rollback %g", r.status().rollback);
    CHECK(r.j->result().stars == 3, "stars");
  }
  {
    Run r = start("hill");
    go(r, {atLine, 0, EAST}, 1);
    stand(r, 0.2, false);
    stand(r, 0.7, true);
    go(r, {atLine - 0.5, 0, EAST}, -1);
    CHECK(has(r, "drillRollback:major"), "major");
    go(r, {atLine - 1.3, 0, EAST}, -1);
    CHECK(has(r, "drillRollback:fail"), "fail");
    Run n = start("hill");
    go(n, {atLine, 0, EAST}, 1);
    stand(n, 0.2, false);
    go(n, {atLine + 4.5, 0, EAST}, 1);
    CHECK(has(n, "drillHillBrake:major"), "no handbrake");
    CHECK(n.status().state == "done", "done");
    CHECK(n.j->result().stars < 3, "stars");
  }
  {
    Run r = start("hill");
    feed(r, {.engine = false});
    feed(r, {.engine = true});
    CHECK(r.faults.empty(), "not started");
    feed(r, {.engine = false});
    CHECK(has(r, "drillStall:minor"), "stalled");
  }
}

// ---- the emergency stop (scripted)

Run brakeRun(double react, double pedal, double speed = 30 / 3.6) {
  Run r = start("brake", [] { return 0.0; });
  while (!r.status().signal && r.pose.x < 60) {
    r.pose.x += speed * STEP;
    feed(r, {.speed = speed});
    if (r.status().signal) break;
  }
  CHECK(r.status().step == 1, "the signal was given");
  double v = speed;
  double t = 0;
  while (v > 0.05 && t < 10) {
    t += STEP;
    const bool on = t >= react;
    if (on) v = std::max(0.0, v - 9 * pedal * STEP);
    r.pose.x += v * STEP;
    feed(r, {.speed = v, .brake = on ? pedal : 0});
  }
  feed(r, {.speed = 0, .brake = pedal});
  return r;
}

void brakeScripted() {
  {
    Run r = brakeRun(0.6, 1);
    CHECK(r.status().state == "done", "done");
    CHECK(r.faults.empty(), "%s", joined(r.faults).c_str());
    CHECK(said(r, "drill.r.braked"), "braked");
    const DrillResult res = r.j->result();
    CHECK(res.stars == 3, "stars %d score %g", res.stars, res.score);
    CHECK(param(res.measureData, "v") == "30", "v %s", param(res.measureData, "v").c_str());
  }
  {
    Run r = brakeRun(1.6, 0.4);
    CHECK(has(r, "drillBrakeLate:major"), "late");
    CHECK(has(r, "drillBrakeSoft:major"), "soft");
    CHECK(has(r, "drillBrakeLong:minor"), "long");
    CHECK(r.j->result().stars <= 1, "stars");
    Run f = brakeRun(0.6, 1, 50 / 3.6);
    CHECK(has(f, "drillBrakeSpeed:minor"), "speed");
  }
}

// ---- with the real car's dynamics

/** A yard turned so that its u runs north – up the plane of the harness. */
const YardFrame FRAME(YardJson{.x = 0, .z = 0, .len = 150, .wid = 66, .rot = 1});

std::vector<std::string> drive(Vehicle& v, DrillJudge& j, double seconds, double at, const std::function<DriverInput(double)>& d, const std::function<bool()>& until = [] { return false; }) {
  std::vector<std::string> faults;
  double pos[3] = {0, 0, 0};
  for (int i = 0; i < int(jsRound(seconds / DT)); i++) {
    const DriverInput inp = d(i * DT);
    v.step(DT, inp);
    v.getModelPosition(pos);
    Pose pose = FRAME.poseToLocal({pos[0], pos[2], v.yaw});
    pose.x += at;
    const DrillStatus& st = j.update({.pose = pose, .speed = v.speed, .handbrake = inp.handbrake > 0.5, .brake = inp.brake, .engine = v.engineRunning}, DT);
    for (const DrillFault& f : st.faults) faults.push_back(f.id + ":" + f.severity);
    if (st.state != "run" || until()) break;
  }
  return faults;
}

void hillWithCar() {
  for (bool technique : {false, true}) {
    PlaneGround ground(0.1);
    Vehicle v = makeCar(ground, COMPACT_CAR_AUTOMATIC);
    DrillJudge j("hill");
    std::vector<std::string> faults = drive(v, j, 2, atLine, [](double) { return DriverInput{.brake = 1, .handbrake = 0, .gear = 1}; }, [&j] { return j.status.step == 1; });
    for (const std::string& f : drive(v, j, 1.5, atLine, [](double) { return DriverInput{.brake = 0.5, .handbrake = 1, .gear = 1}; }, [&j] { return j.status.step == 2; })) faults.push_back(f);
    CHECK(j.status.step == 2, "secured");
    for (const std::string& f : drive(v, j, 12, atLine, [technique](double t) {
           return technique ? DriverInput{.throttle = 0.45, .handbrake = t < 0.6 ? 1.0 : 0.0, .gear = 1} : DriverInput{.handbrake = 0, .gear = 0};
         }))
      faults.push_back(f);
    if (technique) {
      CHECK(j.status.state == "done", "%s rolled %.2f", joined(faults).c_str(), j.status.rollback);
      CHECK(j.status.rollback < 0.2, "rolled back %g", j.status.rollback);
    } else {
      CHECK(j.status.state == "failed", "rolled away: failed");
      CHECK(std::count(faults.begin(), faults.end(), "drillRollback:major") && std::count(faults.begin(), faults.end(), "drillRollback:fail"), "%s", joined(faults).c_str());
    }
  }
}

void brakeWithCar() {
  PlaneGround ground(0);
  Vehicle v = makeCar(ground, COMPACT_CAR_AUTOMATIC);
  DrillJudge j("brake", CAR_SHAPE, [] { return 0.0; });
  double signalAt = -1;
  double t = 0;
  const std::vector<std::string> faults = drive(v, j, 40, -68, [&](double) {
    t += DT;
    if (j.status.step == 1 && signalAt < 0) signalAt = t;
    if (signalAt >= 0) return DriverInput{.brake = t - signalAt > 0.5 ? 1.0 : 0.0, .gear = 1};
    return DriverInput{.throttle = v.speed < 30 / 3.6 ? 0.6 : 0.15, .gear = 1};
  });
  CHECK(signalAt > 0, "the signal was given");
  CHECK(j.status.state == "done", "%s", joined(faults).c_str());
  CHECK(faults.empty(), "%s", joined(faults).c_str());
  const DrillResult res = j.result();
  CHECK(std::stod(param(res.measureData, "d")) < std::stod(param(res.measureData, "ideal")) * 1.3, "d %s ideal %s", param(res.measureData, "d").c_str(), param(res.measureData, "ideal").c_str());
}

// ---- the lessons

void lessons() {
  std::vector<std::string> ids;
  for (const LessonDef& l : LESSONS)
    if (l.drills) {
      ids.push_back(l.id);
      CHECK(l.kind == "yard", "%s", l.id.c_str());
      CHECK(l.targets.empty(), "%s", l.id.c_str());
      CHECK((l.vehicle && *l.vehicle == "car") || (l.vehicles && std::count(l.vehicles->begin(), l.vehicles->end(), "car")), "%s", l.id.c_str());
    }
  CHECK((ids == std::vector<std::string>{"bParallel", "bBay", "bReverse", "bTurn", "bHill", "bBrake", "bDrills"}), "%s", joined(ids).c_str());
  std::set<std::string> seen;
  for (double r : {0.0, 0.3, 0.6, 0.99}) {
    const std::vector<DrillId> d = examDrills([r] { return r; });
    CHECK(d.size() == 3, "three");
    CHECK(d[0] == "backCorner" || d[0] == "parallel", "%s", joined(d).c_str());
    std::vector<std::string> b;
    for (size_t i = 1; i < d.size(); i++) b.push_back(d[i] == "bayFwd" || d[i] == "bayBack" ? "bay" : d[i]);
    for (const std::string& id : b) CHECK(id == "turn" || id == "bay" || id == "brake", "%s", joined(d).c_str());
    CHECK(b[0] != b[1], "different");
    if (std::count(b.begin(), b.end(), "brake")) CHECK(b[1] == "brake", "brake last");
    for (const DrillId& id : d) seen.insert(id);
  }
  double x = 0;
  for (int i = 0; i < 200; i++)
    for (const DrillId& id : examDrills([&x] { return (x = std::fmod(x * 9301 + 49297, 233280)) / 233280; })) seen.insert(id);
  for (const char* id : {"backCorner", "parallel", "turn", "brake"}) CHECK(seen.count(id), "%s", id);
  CHECK(seen.count("bayFwd") || seen.count("bayBack"), "bay");
}

}  // namespace

int main() {
  layouts();
  ramp();
  rects();
  parallel();
  bays();
  reversing();
  turning();
  hillScripted();
  brakeScripted();
  hillWithCar();
  brakeWithCar();
  lessons();
  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("drills: ok\n");
  return 0;
}

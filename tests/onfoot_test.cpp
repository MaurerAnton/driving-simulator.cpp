// SPDX-License-Identifier: AGPL-3.0-only
// LINK: game sim render i18n audio input engine world core platform geo
// Origin: tests/game/onfoot.test.ts, tests/game/walkmodes.test.ts @ v1.7.7
/**
 * On foot beside the vehicle: walking round it (never through it), the
 * check before the drive, the driver's door – and the ways of walking
 * (game/walkmodes): which way is in use, the way on foot over and along a
 * road after a tap, and walking it, plus the sideways step.
 */
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/geom.h"
#include "game/onfoot.h"
#include "game/walkmodes.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using fs::harness::ok;
using fs::harness::test;

namespace {

const double TDT = 1.0 / 30;
const WalkLook STILL{};
double flat(double, double, double) { return 0; }
bool open(double, double, double, double) { return false; }
const double PI_T = 3.14159265358979323846;

/** The car standing at the origin, facing north (−z). */
WalkVehicle car() {
  WalkVehicle v;
  v.units = {{0, 0, 0, 2.195, 2.085, 0.895, {1.315, -1.315}, 0.77}};
  v.y = 0;
  v.door = 0.1;
  return v;
}

/** A truck with its trailer (tandem axles) behind it. */
WalkVehicle truck() {
  WalkVehicle v;
  v.units = {{0, 0, 0, 3.75, 4.45, 1.25, {2.35, -2.35}, 1.1}, {0, 9.95, 0, 3.9, 2.5, 1.25, {0.65, -0.65}, 1.1}};
  v.y = 0;
  v.door = 2.45;
  v.hitch = WalkVehicle::Point{0, 0.9, 4.05};
  return v;
}

using Blocked = std::function<bool(double, double, double, double)>;

void walk(OnFoot& w, double seconds, const WalkMove& move, const Blocked& blocked = open) {
  for (double t = 0; t < seconds; t += TDT) w.update(TDT, move, STILL, flat, blocked);
}

/** Turn the walker to look at a point. */
void aim(OnFoot& w, double x, double y, double z) {
  const double dx = x - w.x;
  const double dz = z - w.z;
  w.yaw = std::atan2(-dx, -dz);
  w.pitch = std::atan2(y - (w.y + EYE), std::hypot(dx, dz));
}

WalkMove mv(double fwd, double turn, bool run) {
  WalkMove m;
  m.fwd = fwd;
  m.turn = turn;
  m.run = run;
  return m;
}

int count(const std::vector<CheckSpot>& s, const std::string& id) {
  int n = 0;
  for (const CheckSpot& c : s) n += c.id == id ? 1 : 0;
  return n;
}

void spotsCase() {
  test("what there is to check: every tyre, the lights at the front and at the back – with a trailer the coupling");
  const std::vector<CheckSpot> c = checkSpots(car());
  std::string ids;
  for (const CheckSpot& s : c) ids += s.id + ",";
  ok(ids == "tyres,tyres,tyres,tyres,front,rear,", ids);
  const std::vector<CheckSpot> t = checkSpots(truck());
  ok(count(t, "tyres") == 6, "the tandem axle of the trailer is looked at as one on each side");
  ok(count(t, "front") == 1, "one front");
  ok(count(t, "rear") == 1, "one rear");
  for (const CheckSpot& s : t)
    if (s.id == "rear") ok(s.z > 12, "the lights at the back are those of the trailer");
  ok(count(t, "coupling") == 1, "the coupling");
  // each ring lies on the ground beside the vehicle, outside it
  for (const auto* list : {&c, &t})
    for (const CheckSpot& s : *list) ok(std::hypot(s.rx - s.x, s.rz - s.z) > 0.5, "ring outside");
}

void doorCase() {
  test("one starts beside the driver's door – and can get in from there");
  OnFoot w(car());
  ok(w.x < -0.895, "on the left of the car");
  ok(w.atDoor(), "at the door");
  walk(w, 2, mv(1, 0, false));
  ok(!w.atDoor(), "a few steps on, the door is behind");
}

void wallsCase() {
  test("nobody walks through the vehicle, a wall – or away from the vehicle for good");
  OnFoot w(car());
  // towards the car (east)
  w.yaw = -PI_T / 2;
  walk(w, 4, mv(1, 0, false));
  ok(w.x < -0.895 - 0.27, "stopped at the side of the car (x = " + toFixed(w.x, 2) + ")");
  // away from it (west) towards a wall at x = −6
  w.yaw = PI_T / 2;
  const Blocked wall = [](double x, double, double, double) { return x < -6; };
  walk(w, 6, mv(1, 0, true), wall);
  ok(w.x > -6 && w.x < -5, "stopped at the wall (x = " + toFixed(w.x, 2) + ")");
  // walking along a wall slides along it
  w.yaw = PI_T / 2 + 0.6;
  const double z0 = w.z;
  walk(w, 2, mv(1, 0, false), wall);
  ok(w.z - z0 > 1, "along the wall");
  // away from the vehicle as far as one likes: beyond TETHER the hint says how far it is
  OnFoot far(car());
  far.yaw = PI_T / 2;
  walk(far, 60, mv(1, 0, true));
  ok(std::hypot(far.x, far.z) > TETHER + 20, "walked on (" + toFixed(std::hypot(far.x, far.z), 0) + " m)");
  ok(far.tooFar, "the hint");
}

void turningCase() {
  test("turning and walking: the keys turn the walker, walking goes the way it looks");
  OnFoot w(car());
  const double yaw0 = w.yaw;
  walk(w, 1, mv(0, 1, false));
  ok(std::abs(w.yaw - yaw0 - 1.9) < 0.1, "about 110 degrees a second");
  w.yaw = PI_T / 2;  // west
  const double x0 = w.x;
  walk(w, 2, mv(1, 0, false));
  ok(std::abs(x0 - w.x - 2 * 1.45) < 0.1, "walking pace (" + toFixed(x0 - w.x, 2) + " m in 2 s)");
  const double x1 = w.x;
  walk(w, 2, mv(1, 0, true));
  ok(x1 - w.x > 5, "running");
}

void checkedCase() {
  test("looked at for a moment from near enough, each thing counts as checked");
  OnFoot w(car());
  CheckSpot& tyre = w.spots[0];
  // from far away: no
  w.x = tyre.x - 6;
  w.z = tyre.z;
  aim(w, tyre.x, tyre.y, tyre.z);
  walk(w, 1, mv(0, 0, false));
  ok(!tyre.done, "too far away");
  // near, but looking elsewhere: no
  w.x = tyre.rx;
  w.z = tyre.rz;
  aim(w, tyre.x, tyre.y + 5, tyre.z + 8);
  walk(w, 1, mv(0, 0, false));
  ok(!tyre.done, "not looked at");
  // near and looking at it: yes, after a moment
  aim(w, tyre.x, tyre.y, tyre.z);
  w.update(TDT, mv(0, 0, false), STILL, flat, open);
  ok(!tyre.done, "not at a glance");
  walk(w, 0.6, mv(0, 0, false));
  ok(tyre.done, "checked");
  const OnFoot::Progress p0 = w.progress()[0];
  ok(p0.id == "tyres" && p0.done == 1 && p0.of == 4, "progress of the tyres");
  // all of them
  for (size_t i = 0; i < w.spots.size(); i++) {
    const CheckSpot s = w.spots[i];
    w.x = s.rx;
    w.z = s.rz;
    aim(w, s.x, s.y, s.z);
    walk(w, 0.6, mv(0, 0, false));
  }
  ok(w.allChecked(), "all checked");
  ok(w.missing().empty(), "nothing missing");
}

void missingCase() {
  test("what was not checked is named");
  OnFoot w(truck());
  for (CheckSpot& s : w.spots)
    if (s.id != "coupling" && s.id != "rear") s.done = true;
  const std::vector<CheckId> m = w.missing();
  ok(m.size() == 2 && m[0] == "rear" && m[1] == "coupling", "rear, coupling");
}

// ---- tests/game/walkmodes.test.ts

/** a straight road from west to east along z = 0, 200 m long, 7 m wide, pavements of 2.5 m, a zebra crossing at 120 m */
const Polyline3& roadLine() {
  static const Polyline3 pl = makePolyline({0, 0, 0, 200, 0, 0});
  return pl;
}
FootRoad road() { return {&roadLine(), 3.5, 2.5, 2.5, {120}}; }

std::string dump(const std::vector<FootPoint>& path) {
  std::string s;
  for (const FootPoint& p : path) s += "(" + toFixed(p.x, 2) + "," + toFixed(p.z, 2) + ")";
  return s;
}

void modesCase() {
  test("auto: the classic way on a touch screen, as before with keys");
  ok(walkModeOf("auto", true) == "classic", "auto, touch");
  ok(walkModeOf("auto", false) == "tank", "auto, keys");
  ok(walkModeOf("tap", false) == "tap", "tap");
  ok(walkModeOf("tank", true) == "tank", "tank");
}

void overCase() {
  test("over the road: along the pavement to the crossing near the way, over it, to the place");
  // from the north pavement (z −5) at x 100 to a place south of the road at x 130
  const std::vector<FootPoint> path = planFootPath(100, -5, 130, 6, {road()});
  const FootPoint last = path.back();
  ok(last.x == 130 && last.z == 6, "ends at the place");
  // somewhere it goes over the road at the crossing (x 120): a point on each kerb's side there
  int at = 0;
  bool north = false;
  bool south = false;
  for (const FootPoint& p : path) {
    if (std::abs(p.x - 120) >= 0.01) continue;
    at++;
    north = north || p.z < -3.5;
    south = south || p.z > 3.5;
  }
  ok(at == 2, dump(path));
  ok(north && south, "a point on each side");
  // and nowhere else on the carriageway
  for (size_t i = 0; i + 1 < path.size(); i++)
    if (std::abs(path[i].x - 120) > 0.01) ok(std::abs(path[i].z) > 3.5, dump(path));
}

void straightOverCase() {
  test("over the road with no crossing near: straight over where the place lies");
  FootRoad r = road();
  r.xings.clear();
  const std::vector<FootPoint> path = planFootPath(30, -5, 60, 6, {r});
  // over at x 60, kerb to kerb – before that along the north pavement
  std::vector<size_t> over;
  for (size_t i = 0; i < path.size(); i++)
    if (std::abs(path[i].x - 60) < 0.01 && path[i].z != 6) over.push_back(i);
  ok(over.size() == 2 && path[over[0]].z < 0 && path[over[1]].z > 0, dump(path));
  for (size_t i = 0; i < over[1]; i++) ok(path[i].z < -3.5, dump(path));
}

void alongCase() {
  test("along the road on the same side: on the pavement; with no road: straight there");
  const std::vector<FootPoint> path = planFootPath(10, -5, 90, -6, {road()});
  ok(path.size() > 3, dump(path));
  for (const FootPoint& p : path) ok(p.z < -3.5, dump(path));
  const std::vector<FootPoint> a = planFootPath(10, -50, 90, -60, {road()});
  ok(a.size() == 1 && a[0].x == 90 && a[0].z == -60, dump(a));
  const std::vector<FootPoint> b = planFootPath(0, 0, 5, 5, {});
  ok(b.size() == 1 && b[0].x == 5 && b[0].z == 5, dump(b));
}

void walkWayCase() {
  test("walking a way: there, turning towards it, and the keys take over");
  OnFoot walker(std::nullopt, OnFoot::Start{0, 0, 0, 0});
  WalkControl ctl;
  ctl.goTo({{10, 0}});
  const WalkIntent none{};
  for (int i = 0; i < 600 && ctl.walking(); i++) {
    const WalkStep m = ctl.step(1.0 / 30, none, {walker.x, walker.z, walker.yaw}, false);
    walker.update(1.0 / 30, WalkMove{m.fwd, m.turn, m.run, m.side}, WalkLook{}, flat, open);
  }
  ok(!ctl.walking(), "arrived");
  ok(std::hypot(walker.x - 10, walker.z) < 0.5, toFixed(walker.x, 2) + " " + toFixed(walker.z, 2));
  // (it turned to face east: yaw −π/2)
  ok(std::abs(walker.yaw + PI_T / 2) < 0.3, toFixed(walker.yaw, 3));
  ctl.goTo({{50, 0}});
  const WalkStep m = ctl.step(1.0 / 30, {1, 0, 0, false}, {walker.x, walker.z, walker.yaw}, false);
  ok(!ctl.walking(), "the keys take over");
  ok(m.fwd == 1, "forwards");
}

void wallWayCase() {
  test("walking a way into a wall: gives up and says so");
  OnFoot walker(std::nullopt, OnFoot::Start{0, 0, 0, PI_T / 2});
  WalkControl ctl;
  ctl.goTo({{10, 0}});
  const WalkIntent none{};
  for (int i = 0; i < 300 && ctl.walking(); i++) {
    const WalkStep m = ctl.step(1.0 / 30, none, {walker.x, walker.z, walker.yaw}, false);
    walker.update(1.0 / 30, WalkMove{m.fwd, m.turn, m.run, m.side}, WalkLook{}, flat, [](double x, double, double, double) { return x > 3; });
  }
  ok(!ctl.walking(), "stopped");
  ok(ctl.blocked, "blocked");
  ok(walker.x < 3.2, "before the wall");
}

void easedCase() {
  test("turning by keys is eased in");
  WalkControl ctl;
  const WalkControl::At at{0, 0, 0};
  const WalkStep a = ctl.step(1.0 / 60, {0, 0, 1, false}, at, false);
  ok(a.turn > 0 && a.turn < 0.3, toFixed(a.turn, 3));
  double t = a.turn;
  for (int i = 0; i < 60; i++) t = ctl.step(1.0 / 60, {0, 0, 1, false}, at, false).turn;
  ok(t > 0.95, toFixed(t, 3));
}

void sideCase() {
  test("on foot: a step to the side, and no quicker on a slant");
  OnFoot w(std::nullopt, OnFoot::Start{0, 0, 0, 0});
  // facing north (−z): to the right is east (+x)
  w.update(1, WalkMove{0, 0, false, 1.0}, WalkLook{}, flat, open);
  ok(w.x > 1.3 && std::abs(w.z) < 1e-9, toFixed(w.x, 3) + " " + toFixed(w.z, 3));
  OnFoot v(std::nullopt, OnFoot::Start{0, 0, 0, 0});
  v.update(1, WalkMove{1, 0, false, 1.0}, WalkLook{}, flat, open);
  ok(std::abs(std::hypot(v.x, v.z) - w.x) < 1e-6, "no quicker on a slant");
}

}  // namespace

int main() {
  std::printf("onfoot_test (tests/game/onfoot.test.ts, tests/game/walkmodes.test.ts)\n");
  spotsCase();
  doorCase();
  wallsCase();
  turningCase();
  checkedCase();
  missingCase();
  modesCase();
  overCase();
  straightOverCase();
  alongCase();
  walkWayCase();
  wallWayCase();
  easedCase();
  sideCase();
  std::printf("ok\n");
  return 0;
}

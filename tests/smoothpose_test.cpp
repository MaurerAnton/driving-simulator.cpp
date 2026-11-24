// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim world core platform geo
// Origin: tests/sim/smoothpose.test.ts @ v1.7.7
/**
 * The cars of the traffic turn and move smoothly (src/sim/smoothpose): through
 * a corner of their way drawn as a few straight pieces they turn a little on
 * each picture – not 15° on one and 30° on the next –, they keep to their
 * lane, and their front wheels are steered into the turn.  (What the
 * simulation reckons with stays where their way puts them.)
 */
#include <cmath>
#include <vector>

#include "core/geom.h"
#include "sim/signals.h"
#include "sim/smoothpose.h"
#include "sim/traffic.h"
#include "tests/sim_roads.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using namespace fs::harness;

namespace {

const double DEGREES = 180 / PI;

/** the module's own wrapAngle (not core/geom's) */
double wrap(double a) { return fs::smoothpose::wrapAngle(a); }

/** a quarter circle of radius r turning right from heading north, drawn as `n` straight pieces – as a path through a junction is */
Polyline3 coarseArc(double r, int n) {
  std::vector<double> pts;
  // (going north = −z, turning right towards east = +x round the centre (r, 0))
  for (int i = 0; i <= n; i++) {
    const double a = (PI / 2) * (static_cast<double>(i) / n);
    pts.push_back(r - r * jsCos(a));
    pts.push_back(0);
    pts.push_back(-r * jsSin(a));
  }
  // straight on east for a while
  pts.push_back(r + 20);
  pts.push_back(0);
  pts.push_back(-r);
  return makePolyline(pts);
}

void coarseCorner() {
  test("through a coarse corner the heading turns a little each picture, the wheels steered right");
  const Polyline3 pl = coarseArc(8, 5);
  const double dt = 1.0 / 60;
  const double v = 8;
  PolySample s0 = newPolySample();
  samplePolyline(pl, 0, s0);
  EasedPose p{.x = s0.x, .z = s0.z, .yaw = jsAtan2(-s0.tx, -s0.tz)};
  double rawMax = 0;
  double maxStep = 0;
  double maxOff = 0;
  double minSteer = 0;
  double rawPrev = p.yaw;
  for (double s = v * dt; s < pl.len - 2; s += v * dt) {
    s0 = samplePolyline(pl, s, s0);
    const double raw = jsAtan2(-s0.tx, -s0.tz);
    rawMax = jsMax(rawMax, std::abs(wrap(raw - rawPrev)));
    rawPrev = raw;
    const double before = p.yaw;
    easePose(p, s0.x, s0.z, chordYawOn(pl, s, 1.2), 0, v * dt, dt, 2.6);
    maxStep = jsMax(maxStep, std::abs(wrap(p.yaw - before)));
    maxOff = jsMax(maxOff, jsHypot(p.x - s0.x, p.z - s0.z));
    minSteer = jsMin(minSteer, -p.steer.value_or(0));
  }
  // the line itself turns by 18° at each corner of it: drawn, by a few degrees a picture at most
  ok(rawMax * DEGREES > 15, "the line turns " + jsStr(rawMax * DEGREES) + "° at a corner");
  ok(maxStep * DEGREES < 3.5, "turned by " + jsStr(maxStep * DEGREES) + "° in one picture");
  // it stays on its way (within a few centimetres), turned towards the end
  ok(maxOff < 0.25, jsStr(maxOff) + " m off its way");
  ok(std::abs(wrap(p.yaw + PI / 2)) < 0.05, "facing " + jsStr(p.yaw * DEGREES) + "° at the end");
  // the front wheels were turned to the right (+ right): about atan(2.6 / 8) ≈ 18°
  ok(-minSteer * DEGREES > 10 && -minSteer * DEGREES < 30, "steered " + jsStr(-minSteer * DEGREES) + "°");
}

void sidewaysJump() {
  test("a sideways jump of its way becomes a drift; a jump of many metres is taken at once");
  EasedPose p{.x = 0, .z = 0, .yaw = 0};
  const double dt = 1.0 / 60;
  // (driving north at 10 m/s, the way suddenly 0.5 m to the right)
  double z = 0;
  double maxMove = 0;
  for (int i = 0; i < 60; i++) {
    z -= 10 * dt;
    const double x0 = p.x;
    easePose(p, 0.5, z, 0, 0, 10 * dt, dt, 2.6);
    maxMove = jsMax(maxMove, std::abs(p.x - x0));
  }
  ok(maxMove < 0.12, "moved sideways by " + jsStr(maxMove) + " m in one picture");
  ok(std::abs(p.x - 0.5) < 0.01, "not there after a second: " + jsStr(p.x));
  easePose(p, 30, z, 0, 0, 0, dt, 2.6);
  ok(p.x == 30, "a jump of many metres is taken at once");
}

void standingStill() {
  test("standing still it neither turns nor moves its wheels");
  EasedPose p{.x = 0, .z = 0, .yaw = 0.3, .steer = 0.2};
  for (int i = 0; i < 30; i++) easePose(p, 0, 0, 0.3, 0, 0, 1.0 / 60, 2.6);
  ok(p.steer == 0.2, "the wheels stay");
  ok(std::abs(p.yaw - 0.3) < 1e-9, "the heading stays");
}

// ------------------------------------------------------------- a car of the traffic round a corner

void roundTheSquare() {
  test("a car of the traffic drives round the corners of a square smoothly, in its lane");
  // (straight roads between points – as tests/sim/queues.test.ts makes them, here in town and without a rule at the junctions)
  roads::Net net = roads::makeNet({{0, 0}, {300, 0}, {300, 200}, {0, 200}}, {roads::Road{.a = 0, .b = 1, .urban = true}, roads::Road{.a = 1, .b = 2, .urban = true}, roads::Road{.a = 2, .b = 3, .urban = true}, roads::Road{.a = 3, .b = 0, .urban = true}},
                                false);
  const Sparse<SignalControllerJson> none;
  SignalSystem signals(none);
  Traffic traffic(*net.world, *net.lanes, signals);
  traffic.targetCount = 0;
  Lane* lane = net.lanes->edgeLanes[0]->fwd[0];
  TrafficCar* car = traffic.addCar(*lane, 200);
  const PlayerState away{.x = 150, .y = 160, .z = 100, .yaw = 0, .speed = 0, .length = 4.3, .width = 1.8};
  const double dt = 1.0 / 60;
  PolySample raw = newPolySample();
  double maxStep = 0;
  double maxOff = 0;
  double turned = 0;
  double steered = 0;
  for (int i = 0; i < 60 * 90; i++) {
    const double before = car->drawn ? car->drawn->yaw : car->yaw;
    traffic.update(dt, away);
    if (traffic.cars.empty() || traffic.cars[0] != car) break;
    ok(car->drawn.has_value(), "it is drawn eased");
    const EasedPose& p = *car->drawn;
    const double d = wrap(p.yaw - before);
    maxStep = jsMax(maxStep, std::abs(d));
    turned += std::abs(d);
    steered = jsMax(steered, std::abs(p.steer.value_or(0)));
    // where its way puts it: the lane or the path through the junction
    if (car->conn) raw = samplePolyline(car->conn->pl, car->s, raw);
    else if (car->lane) raw = lanePoint(*car->lane, car->s, raw);
    maxOff = jsMax(maxOff, jsHypot(p.x - raw.x, p.z - raw.z));
    // (the simulation itself reckons with where its way puts it)
    ok(jsHypot(car->x - raw.x, car->z - raw.z) < 1e-6 || car->slide != 0 || car->side != 0, "the simulation reckons with where its way puts it");
  }
  ok(turned * DEGREES > 170, "it went round at least two corners (turned " + jsStr(turned * DEGREES) + "°)");
  ok(maxStep * DEGREES < 4, "turned by " + jsStr(maxStep * DEGREES) + "° in one picture");
  ok(maxOff < 0.4, jsStr(maxOff) + " m off its way");
  ok(steered > 0.1, "its wheels were steered (" + jsStr(steered) + " rad)");
}

}  // namespace

int main() {
  std::printf("smoothpose_test (tests/sim/smoothpose.test.ts)\n");
  coarseCorner();
  sidewaysJump();
  standingStill();
  roundTheSquare();
  std::printf("smoothpose_test: all 4 cases passed\n");
  return 0;
}

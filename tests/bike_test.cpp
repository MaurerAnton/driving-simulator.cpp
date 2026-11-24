// LINK: render engine sim game core platform
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * tests/game/bike.test.ts: the rider of the player's motorcycle: how he moves (RiderMotion – from speed,
 * steering, lean, throttle, brake and indicator) and that the model follows him in every class: hands on
 * the grips, feet on the pegs or on the road, no limb stretched, the camera of his eyes steady while he
 * looks round.
 */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "core/js.h"
#include "render/bike.h"

namespace fs {

/** what the tests look at inside the model */
struct BikeInside {
  struct Limb {
    Object3D* upper;
    Object3D* fore;
    Vector3 wrist;
  };
  struct LegIn {
    int side;
    Object3D* thigh;
    Object3D* shin;
    Object3D* foot;
  };
  static std::vector<Limb> arms(PlayerBike& b) {
    std::vector<Limb> out;
    for (const auto& a : b.arms_) out.push_back({a.upper.get(), a.fore.get(), a.wrist});
    return out;
  }
  static std::vector<LegIn> legs(PlayerBike& b) {
    std::vector<LegIn> out;
    for (const auto& g : b.legs_) out.push_back({g.side, g.thigh.get(), g.shin.get(), g.foot.get()});
    return out;
  }
  static Object3D& front(PlayerBike& b) { return *b.front_; }
  static Object3D& head(PlayerBike& b) { return *b.head_; }
};

}  // namespace fs

using namespace fs;

namespace {

const double DT = 1.0 / 30;
const double DEG = PI / 180;
const double WHEELBASE = 1.45;
const double RAKE = 25 * DEG;

double sgn(double v) { return v > 0 ? 1 : v < 0 ? -1 : v; }

struct Over {
  std::optional<double> throttle, brake, indicator, lean;
  std::optional<bool> reversing;
};

/** Riding at `kmh` round a bend of radius `r` (m, + = to the right; 0 = straight on): the input of a steady curve. */
RiderInput curve(double kmh, double r = 0, const Over& over = {}) {
  const double v = kmh / 3.6;
  const double steer = r == 0 ? 0 : sgn(r) * std::atan(WHEELBASE / std::fabs(r));
  const double lean = r == 0 ? 0 : sgn(r) * std::atan((v * v) / std::fabs(r) / 9.81);
  RiderInput i{.speedKmh = kmh, .reversing = false, .steer = steer, .wheelbase = WHEELBASE, .bar = steer / std::cos(RAKE), .lean = lean, .throttle = 0.2, .brake = 0, .indicator = 0};
  if (over.throttle) i.throttle = *over.throttle;
  if (over.brake) i.brake = *over.brake;
  if (over.indicator) i.indicator = *over.indicator;
  if (over.lean) i.lean = *over.lean;
  if (over.reversing) i.reversing = *over.reversing;
  return i;
}

/** A rider, stepped frame by frame. */
struct Ride {
  RiderMotion m;
  /** `seconds` of the same input; the pose at the end. */
  RiderPose hold(const RiderInput& input, double seconds) {
    const int n = static_cast<int>(jsRound(seconds / DT));
    for (int i = 0; i < n; i++) m.step(input, DT);
    return m.pose;
  }
  /** where he looks, head and shoulders together (rad, + = right of straight ahead) */
  double look() const { return m.pose.torsoYaw + m.pose.headYaw; }
};

#define CHECK(cond, ...)                \
  do {                                  \
    if (!(cond)) {                      \
      std::printf(__VA_ARGS__);         \
      std::printf("\n");                \
      assert(cond);                     \
    }                                   \
  } while (0)

void looksThroughTheBend() {
  Ride right;
  right.hold(curve(15, 8), 2);
  CHECK(right.look() > 25 * DEG && right.look() < 60 * DEG, "turning right at a junction: %g", right.look() / DEG);
  Ride left;
  left.hold(curve(15, -8), 2);
  CHECK(std::fabs(left.look() + right.look()) < 1e-9, "turning left: the same to the other side");
  Ride sweep;
  sweep.hold(curve(70, 150), 4);
  CHECK(sweep.look() > 0 && sweep.look() < right.look() - 0.2, "a sweeping bend: %g", sweep.look() / DEG);
  // (the wheel turned, the machine not yet leaning: the head turns first)
  Ride first;
  first.hold(curve(15), 1);
  first.hold(curve(15, 8, {.lean = 0}), 0.4);
  CHECK(first.look() > 15 * DEG, "0.4 s into the turn: %g", first.look() / DEG);
  // (the shoulders take part of it, the head the rest)
  CHECK(right.m.pose.torsoYaw > 0 && right.m.pose.headYaw > right.m.pose.torsoYaw, "shoulders and head");
  // a turn at full lock at walking pace: far round, but within what a neck does
  Ride uturn;
  const RiderPose p = uturn.hold(curve(8, 3), 2);
  CHECK(uturn.look() > 40 * DEG && std::fabs(p.headYaw) < 80 * DEG, "a U-turn: %g, the head %g", uturn.look() / DEG, p.headYaw / DEG);
}

void leansIn() {
  Ride fast;
  const RiderInput input = curve(100, -120);
  const RiderPose p = fast.hold(input, 3);
  CHECK(input.lean < -30 * DEG, "a strong lean to the left");
  CHECK(p.torsoRoll < -0.05 && p.torsoRoll > -0.25, "the upper body %g further in", p.torsoRoll / DEG);
  CHECK(p.hipShift < -0.03 && p.hipShift > -0.08, "the hips %g cm to the inside", p.hipShift * 100);
  CHECK(p.kneeL > 0.8 && p.kneeR == 0, "the inside knee opens, the outside one stays at the tank");
  // the head keeps the eyes nearer the horizon than the shoulders: leaning in, but less
  const double shoulders = input.lean + p.tilt + p.torsoRoll;
  const double head = shoulders + p.headRoll;
  CHECK(head < 0 && head > shoulders * 0.9 && head < shoulders * 0.4, "head %g, shoulders %g", head / DEG, shoulders / DEG);
  // a gentle lean: in with the machine, nothing hanging off
  const RiderPose gentle = Ride().hold(curve(50, 150), 3);
  CHECK(gentle.torsoRoll > 0 && gentle.torsoRoll < 0.05, "gentle torso");
  CHECK(gentle.hipShift == 0, "gentle hips");
  CHECK(gentle.kneeL + gentle.kneeR == 0, "gentle knees");
}

void shoulderCheck() {
  Ride r;
  r.hold(curve(30), 5);
  const RiderPose eye = r.m.pose;
  const RiderInput on = curve(30, 0, {.indicator = 1});
  r.hold(on, 0.4);
  CHECK(r.look() > 60 * DEG, "0.4 s after: %g to the right", r.look() / DEG);
  r.hold(on, 0.3);
  CHECK(r.look() > 60 * DEG, "still looking 0.7 s after");
  CHECK(r.m.pose.headYaw > 55 * DEG && r.m.pose.torsoYaw > 0.1, "the head turns, the shoulders a little with it");
  CHECK(std::fabs(r.m.pose.eyeYaw - eye.eyeYaw) < 1e-6 && std::fabs(r.m.pose.eyePitch - eye.eyePitch) < 1e-6 && std::fabs(r.m.pose.eyeRoll - eye.eyeRoll) < 1e-6, "the eye's frame stays");
  r.hold(on, 1);
  CHECK(std::fabs(r.look()) < 10 * DEG, "back ahead after 1.7 s");
  r.hold(on, 3);
  CHECK(std::fabs(r.look()) < 10 * DEG, "one check, however long the indicator blinks");
  r.hold(curve(30, 0, {.indicator = -1}), 0.5);
  CHECK(r.look() < -60 * DEG, "changed to the left: a check over the left shoulder");
  // the hazard lights are no indicator: the game hands them over as 0
  Ride h;
  h.hold(curve(30), 1);
  h.hold(curve(30, 0, {.indicator = 0}), 0.5);
  CHECK(std::fabs(h.look()) < 10 * DEG, "hazard");
}

void footDown() {
  Ride r;
  RiderPose p = r.hold(curve(20), 2);
  CHECK(p.footL < 0.01 && p.footR < 0.01 && std::fabs(p.tilt) < 1e-3, "riding: feet up");
  p = r.hold(curve(0), 1.5);
  CHECK(p.footL > 0.95 && p.footR < 0.01, "the left foot on the road, the right on the brake pedal");
  CHECK(p.tilt < -1.5 * DEG && p.tilt > -4 * DEG, "the machine %g on to it", p.tilt / DEG);
  CHECK(std::fabs(p.torsoRoll + p.tilt) < 0.03, "he stays upright");
  // (rolling at a walk he keeps it down; up only once he rides – and down only once he stands)
  p = r.hold(curve(4.5), 1);
  CHECK(p.footL > 0.95, "walk: down");
  p = r.hold(curve(20), 1.5);
  CHECK(p.footL < 0.05 && std::fabs(p.tilt) < 0.2 * DEG, "riding again: on the peg");
  p = r.hold(curve(4.5), 1);
  CHECK(p.footL < 0.05, "walk: up");
  // pushed backwards: both feet on the road, looking over a shoulder
  p = r.hold(curve(2, 0, {.reversing = true}), 1.5);
  CHECK(p.footL > 0.95 && p.footR > 0.95 && std::fabs(p.tilt) < 0.2 * DEG, "pushed back");
  CHECK(std::fabs(r.look()) > 50 * DEG, "over the shoulder");
}

void tuckAndBrace() {
  Ride r;
  const double cruise = r.hold(curve(50), 2).torsoPitch;
  RiderPose p = r.hold(curve(55, 0, {.throttle = 1}), 1.5);
  CHECK(p.torsoPitch > cruise + 4 * DEG && p.elbows > 0.5, "tucked in by %g", (p.torsoPitch - cruise) / DEG);
  CHECK(std::fabs(p.headPitch + p.torsoPitch) < 0.03, "still looking ahead");
  p = r.hold(curve(40, 0, {.throttle = 0, .brake = 0.9}), 1);
  CHECK(p.torsoPitch < cruise - 3 * DEG && p.hipSlide > 0.015 && p.elbows < -0.3, "braced: upright, slid forwards, elbows in");
  p = r.hold(curve(0, 0, {.throttle = 0, .brake = 0.9}), 2);
  CHECK(std::fabs(p.elbows) < 0.05 && p.hipSlide < 0.003, "standing on the brake: at ease");
}

bool samePose(const RiderPose& a, const RiderPose& b) {
  const double* x = &a.hipShift;
  const double* y = &b.hipShift;
  for (size_t i = 0; i < sizeof(RiderPose) / sizeof(double); i++)
    if (x[i] != y[i]) return false;
  return true;
}

void whileHeWaits() {
  Ride a;
  Ride b;
  a.hold(curve(0), 2);
  b.hold(curve(0), 2);
  const RiderPose eye = a.m.pose;
  int busy = 0;
  double most = 0;
  for (int i = 0; i < 60 / DT; i++) {
    a.hold(curve(0), DT);
    b.hold(curve(0), DT);
    CHECK(samePose(a.m.pose, b.m.pose), "the same each time");
    CHECK(std::fabs(a.m.pose.eyeYaw - eye.eyeYaw) < 1e-9 && std::fabs(a.m.pose.eyePitch - eye.eyePitch) < 1e-9 && std::fabs(a.m.pose.eyeRoll - eye.eyeRoll) < 1e-9, "the eye stays");
    if (std::fabs(a.look()) > 0.15) busy++;
    most = std::max(most, std::fabs(a.look()));
  }
  const double share = (busy * DT) / 60;
  CHECK(share > 0.05 && share < 0.5, "looking round %g %% of a minute", share * 100);
  CHECK(most < 50 * DEG, "most");
  // no time, no movement
  const RiderPose before = a.m.pose;
  a.m.step(curve(0, 0, {.indicator = 1}), 0);
  CHECK(samePose(a.m.pose, before), "no time, no movement");
}

void oddNumbers() {
  RiderMotion r;
  const double inf = std::numeric_limits<double>::infinity();
  for (const double x : {std::nan(""), inf, -inf, 1e9, -1e9}) {
    for (const double dt : {x, -1.0, 0.0, DT, 1.0, 30.0}) {
      const RiderPose& p = r.step({.speedKmh = x, .reversing = false, .steer = x, .wheelbase = x, .bar = x, .lean = x, .throttle = x, .brake = x, .indicator = x}, dt);
      const double* v = &p.hipShift;
      for (size_t i = 0; i < sizeof(RiderPose) / sizeof(double); i++) {
        CHECK(std::isfinite(v[i]), "field %zu with %g and dt %g", i, x, dt);
        CHECK(std::fabs(v[i]) < 2, "field %zu: %g", i, v[i]);
      }
    }
  }
}

// ------------------------------------------------------------------------------ the model

struct SOver {
  std::optional<double> speedKmh, throttle, brake, gear;
  std::optional<int> indicator;
  std::optional<bool> braking, blinkOn, reversing;
  std::optional<double> steer;
};

/** The standing state of render/bike.ts, changed by `over`. */
CarVisualState state(const SOver& over = {}) {
  const double r = 0.31;
  CarVisualState s;
  s.speedKmh = 0;
  s.rpm = 1300;
  s.gear = 1;
  s.indicator = 0;
  s.hazard = false;
  s.blinkOn = false;
  s.lowBeam = true;
  s.highBeam = false;
  s.handbrake = false;
  s.ignition = true;
  s.engineRunning = true;
  s.abs = false;
  s.seatbelt = true;
  s.fuel = 0.7;
  s.automatic = false;
  s.showGear = true;
  s.odometerKm = 0;
  s.steerWheel = 0;
  s.wheelSteer = {0, 0, 0, 0};
  s.wheelSpin = {0, 0, 0, 0};
  s.wheelY = {r, r, r, r};
  s.clutch = 0;
  s.brake = 0;
  s.throttle = 0;
  s.handbrakeLever = 0;
  s.braking = false;
  s.reversing = false;
  if (over.speedKmh) s.speedKmh = *over.speedKmh;
  if (over.throttle) s.throttle = *over.throttle;
  if (over.brake) s.brake = *over.brake;
  if (over.gear) s.gear = static_cast<int>(*over.gear);
  if (over.indicator) s.indicator = *over.indicator;
  if (over.braking) s.braking = *over.braking;
  if (over.blinkOn) s.blinkOn = *over.blinkOn;
  if (over.reversing) s.reversing = *over.reversing;
  if (over.steer) s.wheelSteer = {*over.steer, *over.steer, 0, 0};
  return s;
}

/** The far end of a part posed along +y (an arm, a leg) with the length it has unstretched. */
Vector3 end(const Object3D& o, double length) { return Vector3(0, length * o.scale.y, 0).applyQuaternion(o.quaternion).add(o.position); }

void theModelFollowsHim() {
  for (const char* variant : {"moped", "light", "mid", "big"}) {
    PlayerBike bike(variant);
    const Vector3 rest = bike.eye.clone();
    double clock = 0;
    const auto check = [&](const std::string& what) {
      for (const BikeInside::Limb& a : BikeInside::arms(bike)) {
        CHECK(a.upper->scale.y < 1.001 && a.fore->scale.y < 1.001, "%s, %s: an arm stretched", variant, what.c_str());
        const Vector3 grip = a.wrist.clone().applyMatrix4(BikeInside::front(bike).matrix);
        CHECK(end(*a.fore, 0.26).distanceTo(grip) < 1e-3, "%s, %s: a hand off its grip", variant, what.c_str());
        CHECK(end(*a.upper, 0.29).distanceTo(a.fore->position) < 1e-3, "%s, %s: an elbow apart", variant, what.c_str());
      }
      for (const BikeInside::LegIn& g : BikeInside::legs(bike)) {
        CHECK(g.thigh->scale.y < 1.001 && g.shin->scale.y < 1.001, "%s, %s: a leg stretched", variant, what.c_str());
        CHECK(end(*g.thigh, 0.45).distanceTo(g.shin->position) < 1e-3 && end(*g.shin, 0.44).distanceTo(g.foot->position) < 1e-3, "%s, %s: a leg apart", variant, what.c_str());
      }
      CHECK(bike.eye.distanceTo(rest) < 0.2, "%s, %s: the eye moved %g m", variant, what.c_str(), bike.eye.distanceTo(rest));
    };
    /** `seconds` of the same state, every frame checked (on the way between poses too) */
    const auto ride = [&](const std::string& what, const CarVisualState& s, double lean, double seconds = 2) {
      bike.setLean(lean);
      for (int i = 0; i < seconds / DT; i++) {
        bike.update(s, DT, (clock += DT));
        check(what);
      }
      bike.root->updateMatrixWorld(true);
    };
    /** the lowest point of each boot above the road (the root's frame), left and right */
    const auto soles = [&]() {
      std::vector<double> out;
      for (const BikeInside::LegIn& g : BikeInside::legs(bike)) out.push_back(Box3().setFromObject(*g.foot).min.y);
      return out;
    };

    // standing: the left boot on the road, the right one on its peg, the machine a little on to the left
    ride("standing", state(), 0);
    std::vector<double> lr = soles();
    CHECK(std::fabs(lr[0]) < 0.015, "%s: the left sole %g m above the road", variant, lr[0]);
    CHECK(lr[1] > 0.25, "%s: the right boot on its peg", variant);
    CHECK(bike.leaning() < -1.5 * DEG && bike.leaning() > -4 * DEG, "%s: leaning %g", variant, bike.leaning() / DEG);
    bike.setRider(false);
    CHECK(bike.leaning() == 0, "nobody on it: upright");
    bike.setRider(true);
    // riding: both boots on the pegs, the lean as the game asks for it
    ride("riding", state({.speedKmh = 50, .throttle = 0.3}), 0);
    lr = soles();
    CHECK(std::min(lr[0], lr[1]) > 0.25, "%s: riding, the boots on the pegs", variant);
    for (const double lean : {-39.0, 39.0}) {
      const double steer = sgn(lean) * 0.03;
      const auto bend = [&](SOver over) {
        if (!over.speedKmh) over.speedKmh = 60;
        if (!over.throttle) over.throttle = 0.3;
        over.steer = steer;
        return state(over);
      };
      ride("leaning", bend({}), lean * DEG);
      CHECK(std::fabs(bike.leaning() - lean * DEG) < 0.5 * DEG, "%s: the lean", variant);
      ride("leaning, tucked in", bend({.throttle = 1}), lean * DEG, 1);
      ride("leaning, braking", bend({.throttle = 0, .brake = 1, .braking = true}), lean * DEG, 1);
      // (the outside arm at its furthest: hanging off the inside, braced, looking back over the outside shoulder)
      ride("leaning, braking, a shoulder check outwards", bend({.throttle = 0, .brake = 1, .indicator = static_cast<int>(-sgn(lean)), .braking = true, .blinkOn = true}), lean * DEG, 1.3);
      ride("leaning, a shoulder check inwards", bend({.indicator = static_cast<int>(sgn(lean)), .blinkOn = true}), lean * DEG, 1.3);
    }
    for (const double steer : {-0.6, 0.6}) {
      ride("full lock", state({.speedKmh = 8, .steer = steer}), sgn(steer) * 5 * DEG);
      ride("full lock, braking", state({.speedKmh = 8, .brake = 1, .braking = true, .steer = steer}), sgn(steer) * 5 * DEG, 1);
      ride("full lock, a shoulder check", state({.speedKmh = 8, .indicator = static_cast<int>(sgn(steer)), .blinkOn = true, .steer = steer}), sgn(steer) * 5 * DEG, 1.3);
    }
    ride("pushed back", state({.speedKmh = 2, .gear = -1, .reversing = true}), 0);
    lr = soles();
    CHECK(std::max(std::fabs(lr[0]), std::fabs(lr[1])) < 0.015, "%s: both soles on the road (%g, %g)", variant, lr[0], lr[1]);
    // a shoulder check: the head turns far round, the camera of the eyes stays where it was
    ride("riding on", state({.speedKmh = 30}), 0);
    const Vector3 eye = bike.eye.clone();
    double turned = 0;
    for (int i = 0; i < 1.2 / DT; i++) {
      bike.update(state({.speedKmh = 30, .indicator = -1, .blinkOn = i % 20 < 10}), DT, (clock += DT));
      check("a shoulder check");
      turned = std::max(turned, static_cast<double>(BikeInside::head(bike).rotation.y));
      CHECK(bike.eye.distanceTo(eye) < 1e-3, "%s: the eye moved with the shoulder check", variant);
    }
    CHECK(turned > 60 * DEG, "%s: the head turned %g to the left", variant, turned / DEG);
  }
}

}  // namespace

int main() {
  looksThroughTheBend();
  leansIn();
  shoulderCheck();
  footDown();
  tuckAndBrace();
  whileHeWaits();
  oddNumbers();
  theModelFollowsHim();
  std::printf("bike_test: ok\n");
  return 0;
}

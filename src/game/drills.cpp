// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/drills.ts @ v1.7.7
#include "game/drills.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "core/random.h"

namespace fs {

namespace {

constexpr double TAU = 2 * 3.141592653589793;

double wrap(double a) { return a - TAU * jsRound(a / TAU); }

/** A parked car (4.3 × 1.8 m) standing at u, w. */
Box parkedCar(double u, double w, double yaw) {
  Box b;
  b.x = u;
  b.z = w;
  b.yaw = yaw;
  b.hl = 2.15;
  b.hw = 0.9;
  return b;
}

const Hill HILL = {.u0 = -10, .u1 = 2.5, .u2 = 10, .u3 = 22.5, .w0 = -3.2, .w1 = 3.2, .h = 1.25, .side = 2.5};

/** a tyre this close to a kerb (m) touches it */
constexpr double KERB_NEAR = 0.1;

/** 'minor' | 'major' */
std::string severityPoints(const DrillSeverity& s) { return s == "minor" ? "minor" : "major"; }

/** `x.toFixed(1)` */
std::string fix1(double x) { return toFixed(x, 1); }

}  // namespace

double hillRise(const Hill& h, double u, double w) {
  double a;
  if (u <= h.u0 || u >= h.u3) return 0;
  if (u < h.u1) a = (u - h.u0) / (h.u1 - h.u0);
  else if (u <= h.u2) a = 1;
  else a = (h.u3 - u) / (h.u3 - h.u2);
  const double out = w < h.w0 ? h.w0 - w : w > h.w1 ? w - h.w1 : 0;
  const double b = std::max(0.0, 1 - out / h.side);
  return h.h * a * b;
}

// ---- the layouts ------------------------------------------------------------------

DrillLayout drillLayout(const DrillId& id, const CarShape& shape) {
  DrillLayout L;
  L.id = id;
  L.goalMarked = false;
  if (id == "parallel") {
    const ParallelPlan& P = PARALLEL;
    const double K = P.kerb;
    const double G = std::max(P.gap, (shape.front + shape.rear) * 1.45);
    const double mid = K - 0.25 - 0.9;
    L.boundary = "kerb";
    // beside the car parked in front of the gap, the driver's side a little more than half a metre from it
    L.start = {P.from + 18, mid - 0.9 - 0.7 - shape.hw, EAST};
    L.allowed = {{P.from, P.to, 0, K}};
    L.kerbs = {{P.from, K, P.to, K}, {P.from, 0, P.to, 0}};
    // the gap's ends, painted from the kerb out
    L.lines = {{0, K - 2.1, 0, K}, {G, K - 2.1, G, K}};
    L.cones = {{-4.6, K - 2.2}, {G + 4.6, K - 2.2}};
    L.parked = {parkedCar(-2.15, mid, EAST), parkedCar(G + 2.15, mid, EAST), parkedCar(-9.5, mid, EAST)};
    L.goal = {0, G, K - 2.8, K};
    L.steps = {"pAlign", "pTurnIn", "pBack", "pCounter", "pFinish"};
    L.moves = 2;
    L.par = 90;
    L.zoom = 11;
    return L;
  }
  if (id == "bayFwd" || id == "bayBack") {
    const BayPlan& B = BAYS;
    const double half = B.width / 2;
    const double back = B.aisle + B.depth;
    // the lines between the five bays (the one to park in is the middle one)
    for (int k = -2; k <= 3; k++) L.lines.push_back({half + (k - 1) * B.width, B.aisle, half + (k - 1) * B.width, back});
    const Rect row = {-half - 2 * B.width, half + 2 * B.width, B.aisle, back};
    const double midW = B.aisle + B.depth / 2;
    L.boundary = "kerb";
    L.start = id == "bayFwd" ? Pose{-18, 2.6, EAST} : Pose{-18, 3.5, EAST};
    L.allowed = {{-30, 22, 0, B.aisle}, row};
    L.kerbs = {
        {-30, 0, 22, 0},
        {row.u0, back, row.u1, back},
        {-30, B.aisle, row.u0, B.aisle},
        {row.u1, B.aisle, 22, B.aisle},
        {row.u0, B.aisle, row.u0, back},
        {row.u1, B.aisle, row.u1, back},
    };
    // (the cars in the neighbouring bays: one parked forwards, one backwards)
    L.parked = {parkedCar(-B.width, midW, SOUTH), parkedCar(B.width, midW, NORTH), parkedCar(-2 * B.width, midW, SOUTH)};
    L.goal = {-half, half, B.aisle, back};
    L.goalMarked = true;
    L.steps = id == "bayFwd" ? std::vector<std::string>{"bfApproach", "bfTurn", "bfIn", "bfStop"} : std::vector<std::string>{"bbPast", "bbTurn", "bbIn", "bbStop"};
    L.moves = id == "bayFwd" ? 1 : 2;
    L.par = 60;
    L.zoom = 11;
    return L;
  }
  if (id == "backStraight") {
    const Rect lane = {-32, 8, -1.5, 1.5};
    L.boundary = "line";
    L.start = {8 - shape.front - 0.6, 0, EAST};
    L.allowed = {lane};
    L.lines = {{lane.u0, lane.w0, lane.u1, lane.w0}, {lane.u0, lane.w1, lane.u1, lane.w1}, {lane.u0, lane.w0, lane.u0, lane.w1}};
    L.cones = {{lane.u1, lane.w0 - 0.4}, {lane.u1, lane.w1 + 0.4}, {lane.u0, lane.w0 - 0.4}, {lane.u0, lane.w1 + 0.4}};
    L.goal = {-31.5, -23, lane.w0, lane.w1};
    L.goalMarked = true;
    L.steps = {"bsGear", "bsBack", "bsStop"};
    L.moves = 1;
    L.par = 60;
    L.zoom = 9;
    return L;
  }
  if (id == "backCorner") {
    const CornerPlan& C = CORNER;
    const Rect main = {-40, 30, -6, 0};
    const Rect side = {C.u0, C.u1, 0, C.len};
    const double r = C.r;
    L.boundary = "kerb";
    L.start = {-24, -1.6, EAST};
    L.allowed = {main, side};
    L.chamfers = {{C.u0, 0, -r, r}, {C.u1, 0, r, r}};
    L.kerbs = {
        {main.u0, main.w0, main.u1, main.w0},
        {main.u0, 0, C.u0 - r, 0},
        {C.u0 - r, 0, C.u0, r},
        {C.u0, r, C.u0, C.len},
        {C.u1 + r, 0, main.u1, 0},
        {C.u1 + r, 0, C.u1, r},
        {C.u1, r, C.u1, C.len},
    };
    // the middle of the main road, and the end of the side road
    L.lines = {{main.u0, -3, C.u0 - r - 2, -3}, {C.u1 + r + 2, -3, main.u1, -3}, {C.u0, C.len, C.u1, C.len}};
    L.cones = {{C.u0, C.len + 0.3}, {C.u1, C.len + 0.3}};
    L.goal = {C.u0, C.u1, 1, C.len};
    L.steps = {"bcPast", "bcBack", "bcTurn", "bcIn"};
    L.moves = 2;
    L.par = 75;
    L.zoom = 11;
    return L;
  }
  if (id == "turn") {
    const Rect road = {-40, 40, 0, 7};
    L.boundary = "kerb";
    L.start = {-16, 5.0, EAST};
    L.allowed = {road};
    L.kerbs = {{road.u0, road.w0, road.u1, road.w0}, {road.u0, road.w1, road.u1, road.w1}};
    L.lines = {{road.u0, 3.5, -12, 3.5}, {12, 3.5, road.u1, 3.5}};
    L.cones = {{-10, 0.35}, {-10, 6.65}, {10, 0.35}, {10, 6.65}};
    L.goal = {-40, 40, 0, 3.5};
    L.steps = {"tFwd", "tBack", "tAway"};
    L.moves = 3;
    L.par = 60;
    L.zoom = 11;
    return L;
  }
  if (id == "hill") {
    // (the drawn asphalt is a grid of 2.5 m from u = −40: the ramp's bends lie on it)
    const Rect lane = {-38.8, 38.8, -2, 2};
    const std::array<double, 6> at = {lane.u0, HILL.u0, HILL.u1, HILL.u2, HILL.u3, lane.u1};
    for (size_t i = 0; i + 1 < at.size(); i++)
      for (double w : {lane.w0, lane.w1}) L.lines.push_back({at[i], w, at[i + 1], w});
    L.lines.push_back({HILL_LINE, lane.w0, HILL_LINE, lane.w1});
    L.boundary = "line";
    L.start = {-32, 0, EAST};
    L.allowed = {lane};
    L.cones = {{HILL.u0 - 0.5, lane.w0 - 0.6}, {HILL.u0 - 0.5, lane.w1 + 0.6}};
    L.hill = HILL;
    L.goal = {HILL_LINE, HILL.u2, lane.w0, lane.w1};
    L.steps = {"hUp", "hSecure", "hGo"};
    L.moves = -1;
    L.par = 60;
    L.zoom = 0;
    return L;
  }
  // the emergency stop: a long lane
  const Rect lane = {-72, 72, -2, 2};
  L.id = "brake";
  L.boundary = "line";
  L.start = {-68, 0, EAST};
  L.allowed = {lane};
  L.lines = {{lane.u0, lane.w0, lane.u1, lane.w0}, {lane.u0, lane.w1, lane.u1, lane.w1}};
  L.cones = {{lane.u1, lane.w0}, {lane.u1, lane.w1}, {lane.u1, 0}};
  L.goal = lane;
  L.steps = {"brSpeed", "brStop"};
  L.moves = -1;
  L.par = 45;
  L.zoom = 0;
  return L;
}

// ---- judging ----------------------------------------------------------------------

bool rectsOverlap(const std::array<double, 8>& a, const std::array<double, 8>& b) {
  for (const std::array<double, 8>* r : {&a, &b}) {
    for (int i = 0; i < 4; i++) {
      const double ax = (*r)[size_t(((i + 1) % 4) * 2)] - (*r)[size_t(i * 2)];
      const double az = (*r)[size_t(((i + 1) % 4) * 2 + 1)] - (*r)[size_t(i * 2 + 1)];
      // the axis: the edge's normal
      const double nx = -az;
      const double nz = ax;
      double a0 = JS_INF;
      double a1 = -JS_INF;
      double b0 = JS_INF;
      double b1 = -JS_INF;
      for (size_t k = 0; k < 8; k += 2) {
        const double pa = a[k] * nx + a[k + 1] * nz;
        const double pb = b[k] * nx + b[k + 1] * nz;
        a0 = std::min(a0, pa);
        a1 = std::max(a1, pa);
        b0 = std::min(b0, pb);
        b1 = std::max(b1, pb);
      }
      if (a1 < b0 || b1 < a0) return false;
    }
  }
  return true;
}

double kerbDistance(const DrillLayout& lay, double u, double w) {
  double d = JS_INF;
  for (const auto& [u0, w0, u1, w1] : lay.kerbs) {
    const double du = u1 - u0;
    const double dw = w1 - w0;
    const double t = std::max(0.0, std::min(1.0, ((u - u0) * du + (w - w0) * dw) / (du * du + dw * dw)));
    d = std::min(d, std::hypot(u - u0 - du * t, w - w0 - dw * t));
  }
  return d;
}

DrillJudge::DrillJudge(const DrillId& id_, const CarShape& shape_, std::function<double()> rand)
    : id(id_), shape(shape_), layout(drillLayout(id_, shape_)), rand_(rand ? std::move(rand) : std::function<double()>([] { return rnd(); })) {
  for (const Box& b : layout.parked) parkedCorners_.push_back(corners(b, b.hl, b.hl, b.hw));
  delay_ = 1 + 2 * rand_();
}

void DrillJudge::fault(const std::string& fid, const DrillSeverity& severity, const ViolationData& data) {
  status.faults.push_back({fid, severity, data});
  if (severityPoints(severity) == "minor") minor++;
  else major++;
  if (severity == "fail") status.state = "failed";
}

void DrillJudge::say(const std::string& key, const TrParams& data) {
  status.remark = key;
  status.remarkData = data;
}

std::array<double, 8> DrillJudge::carCorners(const Pose& p) const { return corners(p, shape.front, shape.rear, shape.hw); }

const DrillStatus& DrillJudge::update(const DrillInput& s, double dt) {
  DrillStatus& st = status;
  st.faults.clear();
  st.remark = "";
  st.remarkData = {};
  st.signal = false;
  st.stop = false;
  if (st.state != "run") return st;
  time += dt;
  const DrillLayout& lay = layout;
  const Pose& p = s.pose;
  const std::array<double, 8> tc = carCorners(p);

  // ---- the kerbs or the lines: what the wheels do (a body may swing over a kerb)
  std::vector<double> wheels;
  wheelPoints(p, Axles{shape.axles, shape.track + 0.1}, wheels);
  double off = 0;
  double touch = -JS_INF;
  for (size_t i = 0; i < wheels.size(); i += 2) {
    off = std::max(off, offArea(lay, wheels[i], wheels[i + 1]));
    if (lay.boundary == "kerb") touch = std::max(touch, KERB_NEAR - kerbDistance(lay, wheels[i], wheels[i + 1]));
  }
  if (lay.boundary == "kerb") {
    if (off > 0) touch = std::max(touch, KERB_NEAR);
    if (off > 0.3) {
      fault("drillKerbOver", "fail");
      say("drill.r.kerbOver");
      return st;
    }
    if (touch > 0 && !onKerb_) {
      onKerb_ = true;
      fault("drillKerbTouch", "minor");
      say("drill.r.kerb");
    } else if (touch <= -0.05)
      onKerb_ = false;
  } else {
    if (off > 2) {
      fault("drillLine", "fail");
      return st;
    }
    if (off > 0.15 && !outside_) {
      outside_ = true;
      fault("drillLine", "major");
      say("drill.r.line");
    } else if (off < 0.05)
      outside_ = false;
  }

  // ---- the parked cars must not be touched
  for (const std::array<double, 8>& b : parkedCorners_) {
    if (rectsOverlap(tc, b)) {
      st.stop = true;
      fault("drillTouch", "fail");
      say("drill.r.touch");
      return st;
    }
  }

  // ---- the moves, and the corrections beyond those the task takes
  const double step = s.speed * dt;
  if (std::abs(s.speed) > 0.03) {
    const double d = jsSign(step);
    if (d == dir_) against_ = 0;
    else {
      against_ += std::abs(step);
      if (against_ > 0.4) {
        dir_ = d;
        against_ = 0;
        moves_++;
        if (lay.moves >= 0 && moves_ > lay.moves) {
          st.corrections = moves_ - lay.moves;
          if (st.corrections > MAX_CORRECTIONS) {
            fault("drillCorrections", "fail");
            say("drill.r.tooMany");
            return st;
          }
          say("drill.r.correction", {{"n", st.corrections}, {"max", MAX_CORRECTIONS}});
        }
      }
    }
  }

  if (id == "hill") hill(s, dt);
  else if (id == "brake") brake(s, dt);
  else manoeuvre(s, dt, tc);
  return st;
}

// ---- the parking and reversing tasks, and turning round

void DrillJudge::manoeuvre(const DrillInput& s, double dt, const std::array<double, 8>& tc) {
  DrillStatus& st = status;
  const Pose& p = s.pose;
  const CarShape& sh = shape;
  const bool stopped = std::abs(s.speed) < 0.08;
  const auto dev = [&p](double yaw) { return std::abs(wrap(p.yaw - yaw)); };
  const std::array<double, 4> us = {tc[0], tc[2], tc[4], tc[6]};
  const std::array<double, 4> ws = {tc[1], tc[3], tc[5], tc[7]};
  // (the rear corners: 4, 5 and 6, 7)
  const auto advance = [&st](int to) {
    if (st.step < to) st.step = to;
  };
  if (id == "parallel") {
    const double K = PARALLEL.kerb;
    const double G = layout.goal.u1;
    st.kerb = std::max(0.0, K - *std::max_element(ws.begin(), ws.end()));
    if (st.step == 0 && ((stopped && p.x > G + sh.rear - 1.2 && p.x < G + sh.rear + 2 && dev(EAST) < 0.2) || moves_ >= 2)) advance(1);
    else if (st.step == 1 && dev(EAST) > 0.5) advance(2);
    else if (st.step == 2 && std::max(tc[5], tc[7]) > K - 0.9) advance(3);
    else if (st.step == 3 && dev(EAST) < 0.1 && std::min(tc[1], tc[3]) > K - 2.4) advance(4);
  } else if (id == "bayFwd") {
    if (st.step == 0 && (p.x > -BAYS.width / 2 - 5 || dev(EAST) > 0.3)) advance(1);
    else if (st.step == 1 && dev(SOUTH) < 0.35) advance(2);
    else if (st.step == 2 && std::min(tc[1], tc[3]) > BAYS.aisle + 2) advance(3);
  } else if (id == "bayBack") {
    if (st.step == 0 && ((stopped && p.x > BAYS.width / 2 + sh.rear + 0.3) || moves_ >= 2)) advance(1);
    else if (st.step == 1 && dev(NORTH) < 0.35) advance(2);
    else if (st.step == 2 && std::min(tc[5], tc[7]) > BAYS.aisle + 2) advance(3);
  } else if (id == "backStraight") {
    if (st.step == 0 && moves_ >= 1 && s.speed < 0) advance(1);
    else if (st.step == 1 && std::max(tc[4], tc[6]) < layout.goal.u1) advance(2);
  } else if (id == "backCorner") {
    const CornerPlan& C = CORNER;
    st.kerb = std::max(0.0, C.u1 - *std::max_element(us.begin(), us.end()));
    if (st.step == 0 && ((stopped && p.x - sh.rear > C.u1 + 2 && dev(EAST) < 0.3) || moves_ >= 2)) advance(1);
    else if (st.step == 1 && p.x + std::sin(p.yaw) * -sh.axles[1] < C.u1 + C.r + 0.5) advance(2);
    else if (st.step == 2 && dev(NORTH) < 0.35) advance(3);
  } else if (id == "turn") {
    st.step = std::max(0, std::min(2, moves_ - 1));
    // turned round, in the lane on the right for the other way, not reversing: done
    if (dev(WEST) < 0.15 && p.z < 3.6 && s.speed > -0.05 && moves_ >= 3) {
      held_ += dt;
      if (held_ > 0.3) finish();
    } else
      held_ = 0;
    return;
  }

  // ---- standing in the end, with the handbrake on
  if (stopped && s.handbrake) held_ += dt;
  else {
    held_ = 0;
    told_ = false;
  }
  if (held_ < 0.8) return;
  const std::optional<Why> why = whyNot(p, tc);
  if (!why) {
    if (id == "parallel" && st.kerb > 0.3) fault("drillKerbFar", "minor", {{"d", jsRound(st.kerb * 100)}});
    if (id == "backCorner" && st.kerb > 0.6) fault("drillKerbFar", "minor", {{"d", jsRound(st.kerb * 100)}});
    finish();
  } else if (!told_ && held_ > 1.5) {
    told_ = true;
    say(why->key, why->data);
  }
}

std::optional<DrillJudge::Why> DrillJudge::whyNot(const Pose& p, const std::array<double, 8>& tc) const {
  const Rect& g = layout.goal;
  const auto dev = [&p](double yaw) { return std::abs(wrap(p.yaw - yaw)); };
  const auto inGoal = [&](double pad, double w0pad) {
    for (size_t i = 0; i < 8; i += 2)
      if (tc[i] < g.u0 - pad || tc[i] > g.u1 + pad || tc[i + 1] < g.w0 - w0pad || tc[i + 1] > g.w1 + pad) return false;
    return true;
  };
  const auto no = [](const std::string& key, const TrParams& data = {}) { return std::optional<Why>(Why{key, data}); };
  const double kerb = status.kerb;
  if (id == "parallel") {
    if (!inGoal(0.02, 0.02)) return no("drill.r.notInGap");
    if (dev(EAST) > 0.09) return no("drill.r.crooked");
    if (kerb > 0.5) return no("drill.r.kerbFar", {{"d", jsRound(kerb * 100)}});
    return std::nullopt;
  }
  if (id == "bayFwd" || id == "bayBack") {
    if (dev(id == "bayFwd" ? SOUTH : NORTH) > 1.2) return no(id == "bayFwd" ? "drill.r.noseIn" : "drill.r.backIn");
    if (!inGoal(0.05, 0.25)) return no("drill.r.notInBay");
    if (dev(id == "bayFwd" ? SOUTH : NORTH) > 0.1) return no("drill.r.crooked");
    return std::nullopt;
  }
  if (id == "backStraight") {
    if (!inGoal(0.05, 0.05)) return no("drill.r.notInBox");
    if (dev(EAST) > 0.1) return no("drill.r.crooked");
    return std::nullopt;
  }
  if (id == "backCorner") {
    if (!inGoal(0.05, 0.05)) return no("drill.r.notInSide");
    if (dev(NORTH) > 0.12) return no("drill.r.crooked");
    if (kerb > 1.2) return no("drill.r.kerbFar", {{"d", jsRound(kerb * 100)}});
    return std::nullopt;
  }
  return std::nullopt;
}

// ---- the hill start

void DrillJudge::hill(const DrillInput& s, double dt) {
  DrillStatus& st = status;
  const Pose& p = s.pose;
  const double front = p.x + shape.front;
  const bool stopped = std::abs(s.speed) < 0.05;
  if (engineWas_ == true && !s.engine) fault("drillStall", "minor");
  engineWas_ = s.engine;
  if (st.step == 0) {
    if (stopped && front > HILL_LINE - 2 && front < HILL_LINE + 0.3) {
      st.step = 1;
      ref_ = p.x;
      say("drill.r.atLine");
    } else if (!pastTold_ && front > HILL_LINE + 1.2 && s.speed > 0) {
      pastTold_ = true;
      say("drill.r.pastLine");
    }
    return;
  }
  // rolling back, from where it stopped at the line
  st.rollback = std::max(st.rollback, ref_ - p.x);
  if (st.rollback > 1) {
    fault("drillRollback", "fail", {{"d", jsRound(st.rollback * 100)}});
    say("drill.r.rolledFar");
    return;
  }
  if (st.rollback > 0.2 && !rollTold_) {
    rollTold_ = true;
    fault("drillRollback", "major", {{"d", jsRound(st.rollback * 100)}});
    say("drill.r.rolled", {{"d", jsRound(st.rollback * 100)}});
  }
  if (st.step == 1) {
    secured_ = stopped && s.handbrake ? secured_ + dt : 0;
    if (secured_ > 0.5) {
      st.step = 2;
      say("drill.r.secured");
    } else if (p.x - ref_ > 1) {
      // driven on without having secured the car with the handbrake
      fault("drillHillBrake", "major");
      say("drill.r.noHandbrake");
      st.step = 2;
    }
    return;
  }
  if (s.handbrake && s.speed > 1 && !hbTold_) {
    hbTold_ = true;
    fault("handbrakeOn", "minor");
  }
  if (p.x - ref_ > 4) finish();
}

// ---- the emergency stop

void DrillJudge::brake(const DrillInput& s, double dt) {
  DrillStatus& st = status;
  const Pose& p = s.pose;
  if (st.step == 0) {
    atSpeed_ = s.speed > BRAKE_SPEED - 0.8 ? atSpeed_ + dt : 0;
    if ((atSpeed_ > delay_ && p.x > -45) || (p.x > 25 && s.speed > 2)) {
      st.step = 1;
      st.signal = true;
      signalT_ = time;
      v0_ = s.speed;
      const double kmh = jsRound(s.speed * 3.6);
      if (kmh < 25 || kmh > 40) fault("drillBrakeSpeed", "minor", {{"v", kmh}});
    }
    return;
  }
  run_ += std::max(0.0, s.speed) * dt;
  if (reaction_ < 0 && s.brake >= 0.3) reaction_ = time - signalT_;
  maxBrake_ = std::max(maxBrake_, s.brake);
  if (std::abs(s.speed) > 0.1) return;
  // stopped: how it went
  if (reaction_ < 0) reaction_ = time - signalT_;
  stopDist_ = run_;
  const double v = v0_;
  // a second for seeing and reacting at most, then the full brakes (about 7.5 m/s² with ABS on dry asphalt)
  ideal_ = v * 0.8 + (v * v) / (2 * 7.5);
  if (reaction_ > 1) fault("drillBrakeLate", "major", {{"t", fix1(reaction_)}});
  if (maxBrake_ < 0.8) fault("drillBrakeSoft", "major");
  if (stopDist_ > ideal_ * 1.3) fault("drillBrakeLong", "minor", {{"d", fix1(stopDist_)}});
  say("drill.r.braked", {{"d", fix1(stopDist_)}, {"t", fix1(reaction_)}});
  finish();
}

void DrillJudge::finish() {
  if (status.state == "run") status.state = "done";
}

// ---- the result

DrillResult DrillJudge::result() const {
  const DrillStatus& st = status;
  const bool failed = st.state == "failed";
  const double slow = std::min(25.0, std::max(0.0, (time - layout.par) / 4));
  double quality = 6.0 * st.corrections;
  std::string measure = "drillResult.moves";
  TrParams data = {{"n", st.corrections}, {"max", MAX_CORRECTIONS}};
  const double kerb = std::isfinite(st.kerb) ? st.kerb : 0;
  if (id == "parallel" || id == "backCorner") {
    quality += std::max(0.0, kerb - (id == "parallel" ? 0.1 : 0.3)) * 40;
    measure = "drillResult.kerb";
    data = {{"n", st.corrections}, {"max", MAX_CORRECTIONS}, {"d", jsRound(kerb * 100)}};
  } else if (id == "turn") {
    measure = "drillResult.turn";
    data = {{"n", moves_}};
  } else if (id == "hill") {
    quality = st.rollback * 50;
    measure = "drillResult.hill";
    data = {{"d", jsRound(st.rollback * 100)}};
  } else if (id == "brake") {
    quality = std::isfinite(stopDist_) ? std::max(0.0, stopDist_ / ideal_ - 1) * 60 + std::max(0.0, reaction_ - 0.7) * 20 : 0;
    measure = "drillResult.brake";
    data = {{"v", jsRound(v0_ * 3.6)},
            {"t", reaction_ >= 0 ? fix1(reaction_) : "–"},
            {"d", std::isfinite(stopDist_) ? fix1(stopDist_) : "–"},
            {"ideal", std::isfinite(ideal_) ? fix1(ideal_) : "–"}};
  }
  const double score = failed ? 0 : std::max(0.0, jsRound(100 - 12 * minor - 30 * major - slow - quality));
  const int stars = failed ? 0 : score >= 85 ? 3 : score >= 65 ? 2 : score >= 40 ? 1 : 0;
  DrillResult r;
  r.id = id;
  r.text = "drill." + id + ".name";
  r.time = time;
  r.score = score;
  r.stars = stars;
  r.minor = minor;
  r.major = major;
  r.failed = failed;
  r.measure = measure;
  r.measureData = data;
  return r;
}

std::vector<DrillId> examDrills(const std::function<double()>& rand_) {
  const std::function<double()> rand = rand_ ? rand_ : std::function<double()>([] { return rnd(); });
  const auto one = [&rand](const std::vector<std::string>& list) { return list[size_t(std::min(double(list.size() - 1), std::floor(rand() * double(list.size()))))]; };
  const DrillId a = one({"backCorner", "parallel"});
  const std::vector<std::string> groupB = {"turn", "bay", "brake"};
  const std::string first = one(groupB);
  std::vector<std::string> rest;
  for (const std::string& g : groupB)
    if (g != first) rest.push_back(g);
  const std::string second = one(rest);
  std::vector<std::string> b = {first, second};
  // (sorted: the emergency stop last – a stable sort of two)
  std::stable_sort(b.begin(), b.end(), [](const std::string& x, const std::string& y) { return (x == "brake" ? 1 : 0) < (y == "brake" ? 1 : 0); });
  std::vector<DrillId> out = {a};
  for (const std::string& g : b) out.push_back(g == "bay" ? one({"bayFwd", "bayBack"}) : g);
  return out;
}

}  // namespace fs

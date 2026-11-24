// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/ce.ts @ v1.7.7
#include "game/ce.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/js.h"
#include "sim/jsmath.h"

namespace fs {

namespace {

double wrap(double a) { return a - PI * 2 * jsRound(a / (PI * 2)); }

/** facing along +u ("east" of the yard) */
const double EAST = -PI / 2;
/** facing along +w ("south" of the yard) */
const double SOUTH = PI;

std::vector<std::array<double, 4>> outline(const Rect& r) {
  return {
      {r.u0, r.w0, r.u1, r.w0},
      {r.u1, r.w0, r.u1, r.w1},
      {r.u1, r.w1, r.u0, r.w1},
      {r.u0, r.w1, r.u0, r.w0},
  };
}

/** A road of 7 m along the yard and a side road of 7 m leaving it to the north. */
const Rect MAIN = {.u0 = -70, .u1 = 70, .w0 = 6, .w1 = 13};
const Rect SIDE = {.u0 = -24, .u1 = -17, .w0 = -30, .w1 = 6};
/** how much of each corner of the junction is cut off – about what a kerb radius of 7 m leaves free */
const double CORNER = 4;
/** The ramp at the west end, its bay 3.6 m wide, and the apron in front of it. */
const RampRect RAMP = [] {
  RampRect r;
  r.u0 = -72;
  r.u1 = -68;
  r.w0 = -22;
  r.w1 = -2;
  r.h = 1.1;
  return r;
}();
struct Bay {
  double w;
  double half;
};
const Bay BAY = {-12, 1.8};
const Rect APRON = {.u0 = -68, .u1 = 40, .w0 = -24, .w1 = 0};

bool inRect(double u, double w, const Rect& r, double pad) { return u >= r.u0 - pad && u <= r.u1 + pad && w >= r.w0 - pad && w <= r.w1 + pad; }

const CouplingStep& stepOf(const CouplingStepId& id) {
  for (const CouplingStep& s : COUPLING_STEPS)
    if (s.id == id) return s;
  return COUPLING_STEPS.front();
}

}  // namespace

// ---- the yard's frame ----

YardFrame::YardFrame(const YardJson& yard_) : yard(yard_) {
  // rot 1: the long side runs north–south (u points north)
  const double phi = (-PI / 2) * yard.rot;
  cos_ = jsCos(phi);
  sin_ = jsSin(phi);
  turn_ = -phi;
}

std::array<double, 2> YardFrame::toWorld(double u, double w) const { return {yard.x + u * cos_ - w * sin_, yard.z + u * sin_ + w * cos_}; }

std::array<double, 2> YardFrame::toLocal(double x, double z) const {
  const double dx = x - yard.x;
  const double dz = z - yard.z;
  return {dx * cos_ + dz * sin_, -dx * sin_ + dz * cos_};
}

Pose YardFrame::poseToWorld(const Pose& p) const {
  const auto [x, z] = toWorld(p.x, p.z);
  return {x, z, yawToWorld(p.yaw)};
}

Pose YardFrame::poseToLocal(const Pose& p) const {
  const auto [x, z] = toLocal(p.x, p.z);
  return {x, z, yawToLocal(p.yaw)};
}

// ---- the vehicles' outlines ----

std::vector<double>& wheelPoints(const Pose& p, const Axles& axles, std::vector<double>& out) {
  const double fx = -jsSin(p.yaw);
  const double fz = -jsCos(p.yaw);
  const double rx = jsCos(p.yaw);
  const double rz = -jsSin(p.yaw);
  for (const double a : axles.at) {
    out.insert(out.end(), {p.x + fx * a - rx * axles.hw, p.z + fz * a - rz * axles.hw, p.x + fx * a + rx * axles.hw, p.z + fz * a + rz * axles.hw});
  }
  return out;
}

std::array<double, 8> corners(const Pose& p, double front, double rear, double hw) {
  const double fx = -jsSin(p.yaw);
  const double fz = -jsCos(p.yaw);
  // right of the heading
  const double rx = jsCos(p.yaw);
  const double rz = -jsSin(p.yaw);
  return {
      p.x + fx * front - rx * hw, p.z + fz * front - rz * hw, p.x + fx * front + rx * hw, p.z + fz * front + rz * hw,
      p.x - fx * rear + rx * hw,  p.z - fz * rear + rz * hw,  p.x - fx * rear - rx * hw,  p.z - fz * rear - rz * hw,
  };
}

Pose trailerBehind(const Pose& truck) {
  const double d = TRUCK_OUTLINE.hitch + TRAILER_OUTLINE.hitch;
  return {truck.x + jsSin(truck.yaw) * d, truck.z + jsCos(truck.yaw) * d, truck.yaw};
}

// ---- layouts ----

TaskLayout taskLayout(const TaskId& id) {
  if (id == "corner") {
    TaskLayout l;
    // both ends of the cut-off corners, along the side road, and its end
    const double ends[2][2] = {{SIDE.u0, -1}, {SIDE.u1, 1}};
    for (const auto& [u, out] : ends)
      l.cones.insert(l.cones.end(), {{u + out * CORNER, MAIN.w0}, {u, MAIN.w0 - CORNER}, {u, MAIN.w0 - 12}, {u, MAIN.w0 - 21}, {u, SIDE.w0}});
    l.start = {24, (MAIN.w0 + MAIN.w1) / 2, EAST};
    l.allowed = {MAIN, SIDE};
    l.chamfers = {
        {SIDE.u0, MAIN.w0, -CORNER, -CORNER},
        {SIDE.u1, MAIN.w0, CORNER, -CORNER},
    };
    l.lines = {
        {MAIN.u0, MAIN.w1, MAIN.u1, MAIN.w1},
        {MAIN.u0, MAIN.w0, SIDE.u0 - CORNER, MAIN.w0},
        {SIDE.u1 + CORNER, MAIN.w0, MAIN.u1, MAIN.w0},
        {SIDE.u0 - CORNER, MAIN.w0, SIDE.u0, MAIN.w0 - CORNER},
        {SIDE.u1 + CORNER, MAIN.w0, SIDE.u1, MAIN.w0 - CORNER},
        {SIDE.u0, MAIN.w0 - CORNER, SIDE.u0, SIDE.w0},
        {SIDE.u1, MAIN.w0 - CORNER, SIDE.u1, SIDE.w0},
        {SIDE.u0, SIDE.w0, SIDE.u1, SIDE.w0},
    };
    // the whole combination in the side road, clear of the main road
    l.goal = {.u0 = SIDE.u0, .u1 = SIDE.u1, .w0 = SIDE.w0, .w1 = MAIN.w0 - 0.5};
    l.goalMarked = true;
    return l;
  }
  if (id == "ramp") {
    TaskLayout l;
    for (double u = -60; u <= 40; u += 20) l.cones.insert(l.cones.end(), {{u, APRON.w0}, {u, APRON.w1}});
    // 3.6 m to the left of the bay: the offset to make good while reversing
    l.start = {-10, BAY.w - 3.6, EAST};
    l.allowed = {APRON};
    l.lines = outline(APRON);
    l.lines.push_back({RAMP.u1, BAY.w - BAY.half, RAMP.u1 + 18, BAY.w - BAY.half});
    l.lines.push_back({RAMP.u1, BAY.w + BAY.half, RAMP.u1 + 18, BAY.w + BAY.half});
    l.ramp = RAMP;
    // the rear end at most a metre from the ramp, in the bay
    l.goal = {.u0 = RAMP.u1, .u1 = RAMP.u1 + 1, .w0 = BAY.w - BAY.half, .w1 = BAY.w + BAY.half};
    l.goalMarked = true;
    return l;
  }
  const Rect pad = {.u0 = -60, .u1 = 60, .w0 = 14, .w1 = 28};
  TaskLayout l;
  l.start = {-18, 21, EAST};
  l.allowed = {pad};
  l.lines = outline(pad);
  l.cones = {
      {pad.u0, pad.w0},
      {pad.u1, pad.w0},
      {pad.u0, pad.w1},
      {pad.u1, pad.w1},
  };
  l.goal = pad;
  l.goalMarked = false;
  return l;
}

// ---- judging the two manoeuvres ----

double offArea(const TaskLayout& lay, double u, double w, double pad) {
  double d = std::numeric_limits<double>::infinity();
  for (const Rect& r : lay.allowed) {
    const double du = std::max({r.u0 - pad - u, 0.0, u - r.u1 - pad});
    const double dw = std::max({r.w0 - pad - w, 0.0, w - r.w1 - pad});
    d = std::min(d, jsHypot(du, dw));
    if (d == 0) return 0;
  }
  for (const Chamfer& c : lay.chamfers) {
    // in the frame of the triangle: a, b ≥ 0 along the legs, inside where a/|du| + b/|dw| ≤ 1
    const double a = (u - c.u) * jsSign(c.du);
    const double b = (w - c.w) * jsSign(c.dw);
    const double la = std::abs(c.du);
    const double lb = std::abs(c.dw);
    // distance to the cut (the legs border the roads, which are measured above)
    const double over = (a / la + b / lb - 1) / jsHypot(1 / la, 1 / lb) - pad;
    if (a >= -pad && b >= -pad) {
      if (over <= 0) return 0;
      d = std::min(d, over);
    }
  }
  return d;
}

ManoeuvreJudge::ManoeuvreJudge(const TaskId& id_, const RigShape& rig_) : id(id_), layout(taskLayout(id_)), rig(rig_) {}

const ManoeuvreStatus& ManoeuvreJudge::update(const RigState& rs, double dt) {
  ManoeuvreStatus& st = status;
  st.faults.clear();
  if (st.state != "run") return st;
  const TaskLayout& lay = layout;
  const RigShape& R = rig;
  const std::array<double, 8> tc = corners(rs.truck, R.truck.front, R.truck.rear, R.truck.hw);
  const std::array<double, 8> rc = corners(rs.trailer, R.trailer.front, R.trailer.rear, R.trailer.hw);
  // ---- staying on the paved area: the lines are kerbs, and a kerb is driven over with a wheel
  // (a hand's width over the line is not yet off it; the body may swing over it)
  std::vector<double> wheels;
  wheelPoints(rs.trailer, R.trailerAxles, wheelPoints(rs.truck, R.truckAxles, wheels));
  double off = 0;
  for (size_t i = 0; i < wheels.size(); i += 2) off = std::max(off, offArea(lay, wheels[i], wheels[i + 1]));
  if (off > 0.15 && !outside_) {
    outside_ = true;
    st.excursions++;
    st.faults.push_back("ceLine");
  } else if (off < 0.05) outside_ = false;
  // far off the area, or again and again: the task is over
  if (off > 2.5 || st.excursions > 2) st.state = "failed";

  // ---- the ramp must not be touched
  if (lay.ramp) {
    const double rearU = std::min(rc[4], rc[6]);
    st.gap = rearU - lay.ramp->u1;
    if (st.gap < 0.02 && std::min(rc[5], rc[7]) < lay.ramp->w1 && std::max(rc[5], rc[7]) > lay.ramp->w0) {
      st.faults.push_back("ceRamp");
      st.state = "failed";
    }
  }

  // ---- corrections: pulling forward again after having reversed
  const double step = rs.speed * dt;
  if (step < 0) {
    back_ -= step;
    if (back_ > 1) {
      reversed_ = true;
      fwd_ = 0;
    }
  } else if (step > 0 && reversed_) {
    fwd_ += step;
    if (fwd_ > 0.6) {
      st.corrections++;
      reversed_ = false;
      back_ = 0;
      if (st.corrections > MAX_CORRECTIONS) {
        st.faults.push_back("ceCorrections");
        st.state = "failed";
      }
    }
  }
  if (st.state != "run") return st;

  // ---- arrived: standing in the goal, straight, secured
  const bool standing = std::abs(rs.speed) < 0.08 && rs.handbrake;
  held_ = standing && arrived(rs, tc, rc) ? held_ + dt : 0;
  if (held_ > 0.8) st.state = "done";
  return st;
}

bool ManoeuvreJudge::arrived(const RigState& rs) const {
  return arrived(rs, corners(rs.truck, rig.truck.front, rig.truck.rear, rig.truck.hw), corners(rs.trailer, rig.trailer.front, rig.trailer.rear, rig.trailer.hw));
}

bool ManoeuvreJudge::arrived(const RigState& rs, const std::array<double, 8>& tc, const std::array<double, 8>& rc) const {
  const Rect& g = layout.goal;
  if (id == "corner") {
    // everything in the side road, both vehicles along it (front towards the main road)
    for (const std::array<double, 8>* c : {&tc, &rc})
      for (size_t i = 0; i < 8; i += 2)
        if (!inRect((*c)[i], (*c)[i + 1], g, 0.05)) return false;
    return std::abs(wrap(rs.truck.yaw - SOUTH)) < 0.14 && std::abs(wrap(rs.trailer.yaw - SOUTH)) < 0.14;
  }
  // the ramp: the trailer's rear end close to it, the trailer in the bay and square to the ramp
  const double rearU = std::min(rc[4], rc[6]);
  if (rearU < g.u0 + 0.1 || rearU > g.u1) return false;
  for (const size_t i : {4, 6})
    if (rc[i + 1] < g.w0 || rc[i + 1] > g.w1) return false;
  if (std::abs(wrap(rs.trailer.yaw - EAST)) > 0.07) return false;
  return std::abs(wrap(rs.truck.yaw - rs.trailer.yaw)) < 0.2;
}

// ---- coupling and uncoupling ----

const std::vector<CouplingStep> COUPLING_STEPS = {
    // ---- uncoupling
    {"uPark", "do", {}, ""},
    {"uBrake", "act", {"uPark"}, ""},
    {"uChocks", "act", {"uPark"}, ""},
    {"uLeg", "act", {"uPark"}, ""},
    {"uRed", "act", {"uBrake", "uChocks", "uLeg"}, "ceSecure"},
    {"uYellow", "act", {"uRed"}, "ceAir"},
    {"uCable", "act", {"uBrake", "uChocks", "uLeg"}, "ceSecure"},
    {"uOpen", "act", {"uRed", "uYellow", "uCable"}, "ceOpen"},
    {"uPull", "do", {"uOpen"}, ""},
    // ---- coupling
    {"cCheck", "act", {"uPull"}, ""},
    {"cApproach", "do", {"cCheck"}, ""},
    {"cHeight", "act", {"cApproach"}, ""},
    {"cLatch", "do", {"cHeight"}, ""},
    {"cLocked", "act", {"cLatch"}, ""},
    {"cYellow", "act", {"cLocked"}, "ceLocked"},
    {"cRed", "act", {"cYellow"}, "ceAir"},
    {"cCable", "act", {"cLocked"}, "ceLocked"},
    {"cLeg", "act", {"cLocked"}, "ceLocked"},
    {"cChocks", "act", {"cRed", "cYellow"}, "ceRelease"},
    {"cBrake", "act", {"cRed", "cYellow"}, "ceRelease"},
    {"cLights", "act", {"cCable"}, "ceLights"},
    {"cTest", "do", {"cLeg", "cChocks", "cBrake", "cLights"}, ""},
};

CouplingProcedure::CouplingProcedure(const std::array<double, 2>& approach_) : approach(approach_) {}

bool CouplingProcedure::ready(const CouplingStepId& id) const {
  const CouplingStep& step = stepOf(id);
  return std::all_of(step.needs.begin(), step.needs.end(), [this](const CouplingStepId& n) { return done.count(n) > 0; });
}

bool CouplingProcedure::onFoot(const CouplingState& s) const { return std::abs(s.speed) < 0.08 && s.handbrake; }

bool CouplingProcedure::reached(const CouplingStepId& id) const {
  const CouplingStep& step = stepOf(id);
  for (const CouplingStepId& n : step.needs) {
    const CouplingStep& need = stepOf(n);
    if (need.kind == "do" ? !done.count(n) : !reached(n)) return false;
  }
  return true;
}

std::vector<CouplingStepId> CouplingProcedure::choices() const {
  std::vector<CouplingStepId> out;
  for (const CouplingStep& s : COUPLING_STEPS)
    if (s.kind == "act" && !done.count(s.id) && reached(s.id)) out.push_back(s.id);
  return out;
}

std::optional<CouplingFault> CouplingProcedure::act(const CouplingStepId& id, const CouplingState& s) {
  const CouplingStep* step = nullptr;
  for (const CouplingStep& x : COUPLING_STEPS)
    if (x.id == id) step = &x;
  if (!step || step->kind != "act" || done.count(id) || !onFoot(s)) return std::nullopt;
  if (!ready(id)) return step->why.empty() ? "ceSecure" : step->why;
  done.insert(id);
  return std::nullopt;
}

const CouplingStatus& CouplingProcedure::update(const CouplingState& s, double dt) {
  CouplingStatus& st = status;
  st.faults.clear();
  st.uncouple = false;
  st.couple = false;
  if (st.state == "done") return st;
  const bool standing = std::abs(s.speed) < 0.08;
  const auto has = [this](const char* id) { return done.count(id) > 0; };
  const auto finish = [this](const char* id) {
    done.insert(id);
    run_ = 0;
  };

  if (!has("uPark")) {
    if (standing && s.handbrake) finish("uPark");
  } else if (!has("uPull")) {
    if (has("uOpen")) {
      // the coupling is open: the truck drives out from under the eye
      if (s.coupled) st.uncouple = true;
      if (s.dist > 2) finish("uPull");
    }
  } else if (!has("cLatch")) {
    // first back up to about a metre before the eye, in line, and secure the truck …
    if (!has("cApproach") && has("cCheck") && standing && s.handbrake && s.dist > approach[0] && s.dist < approach[1] && std::abs(s.angle) < 0.17) finish("cApproach");
    // … then, the heights compared, on until the coupling takes the eye
    if (s.dist < 0.2 && std::abs(s.angle) < 0.26 && s.speed < 0.05) {
      if (!has("cHeight")) {
        // straight on to the trailer without stopping and looking: it couples, but that is a fault
        st.faults.push_back("ceHeight");
        for (const char* id : {"cCheck", "cApproach", "cHeight"}) done.insert(id);
      }
      if (s.speed < -0.9) st.faults.push_back("ceFast");
      st.couple = true;
      finish("cLatch");
    }
  } else if (!has("cTest")) {
    if (ready("cTest")) {
      // the brake test: pull away, then brake to a stop
      run_ += std::max(0.0, s.speed) * dt;
      // (a few metres at walking pace are enough: it is about whether the trailer brakes with the truck)
      if (run_ > 1.5 && s.speed > 1) braked_ = true;
      if (braked_ && standing) finish("cTest");
    }
  }
  if (has("cTest")) st.state = "done";

  // what the driver is to do with the truck now
  st.todo = std::nullopt;
  for (const CouplingStep& step : COUPLING_STEPS) {
    if (step.kind != "do" || done.count(step.id)) continue;
    if (ready(step.id)) st.todo = step.id;
    break;
  }
  return st;
}

}  // namespace fs

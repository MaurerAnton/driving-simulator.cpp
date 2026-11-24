// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Class CE: the yard's layouts, the judging of the basic manoeuvres and the
 * coupling procedure (origin: src/game/ce.ts @ ff37c5d, v0.7.0).
 */
#include "game/ce.h"

#include <algorithm>

namespace fs {

namespace {
/** facing along +u ("east" of the yard) */
constexpr double EAST = -M_PI / 2;
/** facing along +w ("south" of the yard) */
constexpr double SOUTH = M_PI;

double wrap(double a) { return a - M_PI * 2 * std::round(a / (M_PI * 2)); }

bool inRect(double u, double w, const Rect& r, double pad) {
  return u >= r.u0 - pad && u <= r.u1 + pad && w >= r.w0 - pad && w <= r.w1 + pad;
}

std::vector<std::array<double, 4>> outline(const Rect& r) {
  return {{r.u0, r.w0, r.u1, r.w0}, {r.u1, r.w0, r.u1, r.w1},
          {r.u1, r.w1, r.u0, r.w1}, {r.u0, r.w1, r.u0, r.w0}};
}
}  // namespace

// ---- frame -------------------------------------------------------------------

YardFrame::YardFrame(const YardJson& yardIn) : yard(yardIn) {
  // rot 1: the long side runs north–south (u points north)
  const double phi = (-M_PI / 2) * yard.rot;
  cos_ = std::cos(phi);
  sin_ = std::sin(phi);
  turn_ = -phi;
}

// ---- outlines ----------------------------------------------------------------

const Outline TRUCK_OUTLINE = {3.75, 4.45, 1.25, 4.05};
const Outline TRAILER_OUTLINE = {3.9, 2.5, 1.25, 5.4};

const Axles TRUCK_AXLES = {{2.35, -2.35}, 1.25};
const Axles TRAILER_AXLES = {{0.65, -0.65}, 1.25};

std::vector<double> wheelPoints(const Pose& p, const Axles& axles, std::vector<double> out) {
  const double fx = -std::sin(p.yaw);
  const double fz = -std::cos(p.yaw);
  const double rx = std::cos(p.yaw);
  const double rz = -std::sin(p.yaw);
  for (double a : axles.at) {
    out.push_back(p.x + fx * a - rx * axles.hw);
    out.push_back(p.z + fz * a - rz * axles.hw);
    out.push_back(p.x + fx * a + rx * axles.hw);
    out.push_back(p.z + fz * a + rz * axles.hw);
  }
  return out;
}

std::vector<double> corners(const Pose& p, double front, double rear, double hw) {
  const double fx = -std::sin(p.yaw);
  const double fz = -std::cos(p.yaw);
  // right of the heading
  const double rx = std::cos(p.yaw);
  const double rz = -std::sin(p.yaw);
  return {p.x + fx * front - rx * hw, p.z + fz * front - rz * hw,
          p.x + fx * front + rx * hw, p.z + fz * front + rz * hw,
          p.x - fx * rear + rx * hw,  p.z - fz * rear + rz * hw,
          p.x - fx * rear - rx * hw,  p.z - fz * rear - rz * hw};
}

Pose trailerBehind(const Pose& truck) {
  const double d = TRUCK_OUTLINE.hitch + TRAILER_OUTLINE.hitch;
  return {truck.x + std::sin(truck.yaw) * d, truck.z + std::cos(truck.yaw) * d, truck.yaw};
}

// ---- layouts ------------------------------------------------------------------

namespace {
/** A road of 7 m along the yard and a side road of 7 m leaving it to the north. */
const Rect MAIN = {-70, 70, 6, 13};
const Rect SIDE = {-24, -17, -30, 6};
/** how much of each corner of the junction is cut off – about what a kerb radius of 7 m leaves free */
constexpr double CORNER = 4;
/** The ramp at the west end, its bay 3.6 m wide, and the apron in front of it. */
const Rect RAMP = {-72, -68, -22, -2};
constexpr double RAMP_H = 1.1;
constexpr double BAY_W = -12;
constexpr double BAY_HALF = 1.8;
const Rect APRON = {-68, 40, -24, 0};
}  // namespace

TaskLayout taskLayout(const std::string& id) {
  if (id == "corner") {
    TaskLayout lay;
    // both ends of the cut-off corners, along the side road, and its end
    for (const auto& uOut : {std::array<double, 2>{SIDE.u0, -1.0}, std::array<double, 2>{SIDE.u1, 1.0}}) {
      const double u = uOut[0], out = uOut[1];
      lay.cones.push_back({u + out * CORNER, MAIN.w0});
      lay.cones.push_back({u, MAIN.w0 - CORNER});
      lay.cones.push_back({u, MAIN.w0 - 12});
      lay.cones.push_back({u, MAIN.w0 - 21});
      lay.cones.push_back({u, SIDE.w0});
    }
    lay.start = {24, (MAIN.w0 + MAIN.w1) / 2, EAST};
    lay.allowed = {MAIN, SIDE};
    lay.chamfers = {{SIDE.u0, MAIN.w0, -CORNER, -CORNER}, {SIDE.u1, MAIN.w0, CORNER, -CORNER}};
    lay.lines = {{MAIN.u0, MAIN.w1, MAIN.u1, MAIN.w1},
                 {MAIN.u0, MAIN.w0, SIDE.u0 - CORNER, MAIN.w0},
                 {SIDE.u1 + CORNER, MAIN.w0, MAIN.u1, MAIN.w0},
                 {SIDE.u0 - CORNER, MAIN.w0, SIDE.u0, MAIN.w0 - CORNER},
                 {SIDE.u1 + CORNER, MAIN.w0, SIDE.u1, MAIN.w0 - CORNER},
                 {SIDE.u0, MAIN.w0 - CORNER, SIDE.u0, SIDE.w0},
                 {SIDE.u1, MAIN.w0 - CORNER, SIDE.u1, SIDE.w0},
                 {SIDE.u0, SIDE.w0, SIDE.u1, SIDE.w0}};
    // the whole combination in the side road, clear of the main road
    lay.goal = {SIDE.u0, SIDE.u1, SIDE.w0, MAIN.w0 - 0.5};
    lay.goalMarked = true;
    return lay;
  }
  if (id == "ramp") {
    TaskLayout lay;
    for (double u = -60; u <= 40; u += 20) {
      lay.cones.push_back({u, APRON.w0});
      lay.cones.push_back({u, APRON.w1});
    }
    // 3.6 m to the left of the bay: the offset to make good while reversing
    lay.start = {-10, BAY_W - 3.6, EAST};
    lay.allowed = {APRON};
    lay.lines = outline(APRON);
    lay.lines.push_back({RAMP.u1, BAY_W - BAY_HALF, RAMP.u1 + 18, BAY_W - BAY_HALF});
    lay.lines.push_back({RAMP.u1, BAY_W + BAY_HALF, RAMP.u1 + 18, BAY_W + BAY_HALF});
    lay.hasRamp = true;
    lay.ramp = RAMP;
    lay.rampH = RAMP_H;
    // the rear end at most a metre from the ramp, in the bay
    lay.goal = {RAMP.u1, RAMP.u1 + 1, BAY_W - BAY_HALF, BAY_W + BAY_HALF};
    lay.goalMarked = true;
    return lay;
  }
  // 'couple': the pad the coupling and uncoupling are done on, anywhere
  TaskLayout lay;
  const Rect pad = {-60, 60, 14, 28};
  lay.start = {-18, 21, EAST};
  lay.allowed = {pad};
  lay.lines = outline(pad);
  lay.cones = {{pad.u0, pad.w0}, {pad.u1, pad.w0}, {pad.u0, pad.w1}, {pad.u1, pad.w1}};
  lay.goal = pad;
  lay.goalMarked = false;
  return lay;
}

// ---- judging ------------------------------------------------------------------

double offArea(const TaskLayout& lay, double u, double w, double pad) {
  double d = INFINITY;
  for (const Rect& r : lay.allowed) {
    const double du = std::max(std::max(r.u0 - pad - u, 0.0), u - r.u1 - pad);
    const double dw = std::max(std::max(r.w0 - pad - w, 0.0), w - r.w1 - pad);
    d = std::min(d, std::hypot(du, dw));
    if (d == 0) return 0;
  }
  for (const Chamfer& c : lay.chamfers) {
    // in the frame of the triangle: a, b >= 0 along the legs, inside where a/|du| + b/|dw| <= 1
    const double a = (u - c.u) * (c.du < 0 ? -1 : 1);
    const double b = (w - c.w) * (c.dw < 0 ? -1 : 1);
    const double la = std::fabs(c.du);
    const double lb = std::fabs(c.dw);
    // distance to the cut (the legs border the roads, which are measured above)
    const double over =
        (a / la + b / lb - 1) / std::hypot(1 / la, 1 / lb) - pad;
    if (a >= -pad && b >= -pad) {
      if (over <= 0) return 0;
      d = std::min(d, over);
    }
  }
  return d;
}

ManoeuvreJudge::ManoeuvreJudge(const std::string& idIn) : id(idIn), layout(taskLayout(idIn)) {}

ManoeuvreStatus ManoeuvreJudge::update(const RigState& rig, double dt) {
  ManoeuvreStatus& st = status;
  st.faults.clear();
  if (st.state != "run") return st;
  const TaskLayout& lay = layout;
  const auto tc = corners(rig.truck, TRUCK_OUTLINE.front, TRUCK_OUTLINE.rear, TRUCK_OUTLINE.hw);
  const auto rc = corners(rig.trailer, TRAILER_OUTLINE.front, TRAILER_OUTLINE.rear, TRAILER_OUTLINE.hw);
  // ---- staying on the paved area: the lines are kerbs, and a kerb is driven over with a wheel
  // (a hand's width over the line is not yet off it; the body may swing over it)
  auto wheels = wheelPoints(rig.trailer, TRAILER_AXLES, wheelPoints(rig.truck, TRUCK_AXLES));
  double off = 0;
  for (size_t i = 0; i < wheels.size(); i += 2)
    off = std::max(off, offArea(lay, wheels[i], wheels[i + 1]));
  if (off > 0.15 && !outside_) {
    outside_ = true;
    st.excursions++;
    st.faults.push_back("ceLine");
  } else if (off < 0.05) {
    outside_ = false;
  }
  // far off the area, or again and again: the task is over
  if (off > 2.5 || st.excursions > 2) st.state = "failed";

  // ---- the ramp must not be touched
  if (lay.hasRamp) {
    const double rearU = std::min(rc[4], rc[6]);
    st.gap = rearU - lay.ramp.u1;
    if (st.gap < 0.02 && std::min(rc[5], rc[7]) < lay.ramp.w1 &&
        std::max(rc[5], rc[7]) > lay.ramp.w0) {
      st.faults.push_back("ceRamp");
      st.state = "failed";
    }
  }

  // ---- corrections: pulling forward again after having reversed
  const double step = rig.speed * dt;
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
  const bool standing = std::fabs(rig.speed) < 0.08 && rig.handbrake;
  held_ = (standing && arrived(rig)) ? held_ + dt : 0;
  if (held_ > 0.8) st.state = "done";
  return st;
}

bool ManoeuvreJudge::arrived(const RigState& rig) const {
  const auto tc = corners(rig.truck, TRUCK_OUTLINE.front, TRUCK_OUTLINE.rear, TRUCK_OUTLINE.hw);
  const auto rc = corners(rig.trailer, TRAILER_OUTLINE.front, TRAILER_OUTLINE.rear, TRAILER_OUTLINE.hw);
  const Rect& g = layout.goal;
  if (id == "corner") {
    // everything in the side road, both vehicles along it (front towards the main road)
    for (const auto* c : {&tc, &rc})
      for (size_t i = 0; i < 8; i += 2)
        if (!inRect((*c)[i], (*c)[i + 1], g, 0.05)) return false;
    return std::fabs(wrap(rig.truck.yaw - SOUTH)) < 0.14 &&
           std::fabs(wrap(rig.trailer.yaw - SOUTH)) < 0.14;
  }
  // the ramp: the trailer's rear end close to it, the trailer in the bay and square to the ramp
  const double rearU = std::min(rc[4], rc[6]);
  if (rearU < g.u0 + 0.1 || rearU > g.u1) return false;
  for (size_t i : {size_t(4), size_t(6)})
    if (rc[i + 1] < g.w0 || rc[i + 1] > g.w1) return false;
  if (std::fabs(wrap(rig.trailer.yaw - EAST)) > 0.07) return false;
  return std::fabs(wrap(rig.truck.yaw - rig.trailer.yaw)) < 0.2;
}

// ---- coupling -----------------------------------------------------------------

const std::vector<CouplingStep> COUPLING_STEPS = {
    // ---- uncoupling
    {"uPark", "do", {}, nullptr},
    {"uBrake", "act", {"uPark"}, nullptr},
    {"uChocks", "act", {"uPark"}, nullptr},
    {"uLeg", "act", {"uPark"}, nullptr},
    {"uRed", "act", {"uBrake", "uChocks", "uLeg"}, "ceSecure"},
    {"uYellow", "act", {"uRed"}, "ceAir"},
    {"uCable", "act", {"uBrake", "uChocks", "uLeg"}, "ceSecure"},
    {"uOpen", "act", {"uRed", "uYellow", "uCable"}, "ceOpen"},
    {"uPull", "do", {"uOpen"}, nullptr},
    // ---- coupling
    {"cCheck", "act", {"uPull"}, nullptr},
    {"cApproach", "do", {"cCheck"}, nullptr},
    {"cHeight", "act", {"cApproach"}, nullptr},
    {"cLatch", "do", {"cHeight"}, nullptr},
    {"cLocked", "act", {"cLatch"}, nullptr},
    {"cYellow", "act", {"cLocked"}, "ceLocked"},
    {"cRed", "act", {"cYellow"}, "ceAir"},
    {"cCable", "act", {"cLocked"}, "ceLocked"},
    {"cLeg", "act", {"cLocked"}, "ceLocked"},
    {"cChocks", "act", {"cRed", "cYellow"}, "ceRelease"},
    {"cBrake", "act", {"cRed", "cYellow"}, "ceRelease"},
    {"cLights", "act", {"cCable"}, "ceLights"},
    {"cTest", "do", {"cLeg", "cChocks", "cBrake", "cLights"}, nullptr},
};

CouplingProcedure::CouplingProcedure() { status.todo = "uPark"; }

const CouplingStep* CouplingProcedure::stepOf(const std::string& id) const {
  for (const CouplingStep& s : COUPLING_STEPS)
    if (id == s.id) return &s;
  return nullptr;
}

bool CouplingProcedure::ready(const std::string& id) const {
  const CouplingStep* step = stepOf(id);
  if (!step) return false;
  for (const std::string& n : step->needs)
    if (std::find(done.begin(), done.end(), n) == done.end()) return false;
  return true;
}

bool CouplingProcedure::reached(const std::string& id) const {
  const CouplingStep* step = stepOf(id);
  if (!step) return false;
  for (const std::string& n : step->needs) {
    const CouplingStep* need = stepOf(n);
    if (!need) return false;
    const bool has = std::find(done.begin(), done.end(), n) != done.end();
    if (std::string(need->kind) == "do" ? !has : !reached(n)) return false;
  }
  return true;
}

void CouplingProcedure::finish(const std::string& id) {
  if (std::find(done.begin(), done.end(), id) == done.end()) done.push_back(id);
  run_ = 0;
}

bool CouplingProcedure::onFoot(const CouplingState& s) const {
  return std::fabs(s.speed) < 0.08 && s.handbrake;
}

std::vector<std::string> CouplingProcedure::choices() const {
  std::vector<std::string> out;
  for (const CouplingStep& s : COUPLING_STEPS) {
    if (std::string(s.kind) != "act") continue;
    if (std::find(done.begin(), done.end(), s.id) != done.end()) continue;
    if (reached(s.id)) out.push_back(s.id);
  }
  return out;
}

std::string CouplingProcedure::act(const std::string& id, const CouplingState& s) {
  const CouplingStep* step = stepOf(id);
  if (!step || std::string(step->kind) != "act") return "";
  if (std::find(done.begin(), done.end(), id) != done.end()) return "";
  if (!onFoot(s)) return "";
  if (!ready(id)) return step->why ? step->why : "ceSecure";
  done.push_back(id);
  return "";
}

CouplingStatus CouplingProcedure::update(const CouplingState& s, double dt) {
  CouplingStatus& st = status;
  st.faults.clear();
  st.uncouple = false;
  st.couple = false;
  if (st.state == "done") return st;
  const bool standing = std::fabs(s.speed) < 0.08;
  auto has = [this](const char* id) {
    return std::find(done.begin(), done.end(), id) != done.end();
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
    if (!has("cApproach") && has("cCheck") && standing && s.handbrake && s.dist > 0.25 &&
        s.dist < 1.6 && std::fabs(s.angle) < 0.17)
      finish("cApproach");
    // … then, the heights compared, on until the coupling takes the eye
    if (s.dist < 0.2 && std::fabs(s.angle) < 0.26 && s.speed < 0.05) {
      if (!has("cHeight")) {
        // straight on to the trailer without stopping and looking: it couples, but that is a fault
        st.faults.push_back("ceHeight");
        for (const char* id : {"cCheck", "cApproach", "cHeight"}) {
          if (std::find(done.begin(), done.end(), id) == done.end()) done.push_back(id);
        }
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
  st.todo = "";
  for (const CouplingStep& step : COUPLING_STEPS) {
    if (std::string(step.kind) != "do") continue;
    if (has(step.id)) continue;
    if (ready(step.id)) st.todo = step.id;
    break;
  }
  return st;
}

}  // namespace fs

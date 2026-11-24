// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim world core platform geo
// Origin: tests/vehicle/autopilot.test.ts @ v1.7.7
/**
 * The autopilot and the assist of the light aeroplane (src/sim/autopilot),
 * flown headless on the flight model (src/sim/flight) at fixed steps, the
 * way tests/flight_test.cpp flies it.
 *
 * The expectations are those of a flying school: the take-off on the centre
 * line, rotation at vr, flaps up after the climb has begun; heading and
 * height held within ±5° and ±50 ft; the circuit with its legs; a stable
 * approach, a touchdown on the main wheels at well under 2 m/s and a stop on
 * the runway – or a go-around when the approach is not stable at 100 ft.
 * For the assist: what a beginner does with the keys (the stick pulled or
 * rolled all the way and let go) must not stall the aeroplane, overstress it
 * or leave it in a steepening turn.
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "sim/airfield.h"
#include "sim/autopilot.h"
#include "sim/flight.h"
#include "sim/flightspec.h"
#include "tests/vehicle_harness.h"
#include "world/types.h"

using namespace fs;
using namespace fs::harness;

namespace {

constexpr double DT_ = 1.0 / 60;
constexpr double DEG = M_PI / 180;
constexpr double FT = 0.3048;
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
constexpr double Inf = std::numeric_limits<double>::infinity();

double wrap180(double d) { return std::fmod(std::fmod(d + 180, 360) + 360, 360) - 180; }

void diag(const std::string& s) { std::printf("  # %s\n", s.c_str()); }

std::string fx(double v, int d) { return toFixed(v, d); }

/** Level ground at a height: mown grass. */
class Level : public Ground {
 public:
  explicit Level(double y) : y(y) {}
  void sample(double, double, double, GroundSample& out) override {
    out.y = y;
    out.nx = 0;
    out.ny = 1;
    out.nz = 0;
    out.grip = 0.6;
    out.rolling = 3;
  }

 private:
  double y;
};

/** An airfield with one runway "09/27" from (0, 0) eastwards: x is the way along it, z the way across (+ south, to the right of 09). */
AirfieldJson airfield(double len = 1000, const std::string& surface = "asphalt") {
  AirfieldJson f;
  f.name = "Testfeld";
  f.icao = "EDXX";
  f.kind = "airport";
  f.x = len / 2;
  f.z = 0;
  RunwayJson r;
  r.ref = "09/27";
  r.x1 = 0;
  r.z1 = 0;
  r.x2 = len;
  r.z2 = 0;
  r.w = 30;
  r.s = surface;
  f.runways = {r};
  return f;
}

struct Touchdown {
  double x, z, sink;
};

struct Log {
  /** seconds flown */
  double time = 0;
  std::optional<CrashKind> crashed;
  bool stalled = false;
  /** seconds the stall warning sounded */
  double warning = 0;
  /** greatest bank off the ground (rad) */
  double maxBank = 0;
  bool tailStrike = false;
  /** the phases in the order they came */
  std::vector<std::string> phases;
  /** the first touch of a main wheel: where, how fast it sank (m/s) */
  std::optional<Touchdown> touchdown;
};

bool includes(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

std::string join(const std::vector<std::string>& v) {
  std::string s;
  for (size_t i = 0; i < v.size(); i++) s += (i ? " " : "") + v[i];
  return s;
}

/**
 * Fly with the autopilot until it is done (standing still after a landing),
 * the aeroplane crashes, `seconds` have passed or `each` returns true.
 */
Log fly(Aircraft& a, FlightAutopilot& ap, Ground& g, double seconds, const std::function<bool(double)>& each = nullptr, double dt = DT_) {
  Log log;
  const long n = std::lround(seconds / dt);
  for (long i = 0; i < n; i++) {
    const double t = i * dt;
    log.time = t;
    if (each && each(t)) break;
    const double vy = a.verticalSpeed();
    const bool main = a.wheels[1].contact || a.wheels[2].contact;
    a.step(dt, ap.update(a, dt, g), g);
    if (!main && (a.wheels[1].contact || a.wheels[2].contact) && !log.touchdown && includes(log.phases, "final")) log.touchdown = Touchdown{a.pos.x, a.pos.z, -vy};
    const std::string& p = ap.state.phase;
    if (log.phases.empty() || log.phases.back() != p) log.phases.push_back(p);
    if (!a.onGround()) log.maxBank = std::max(log.maxBank, std::abs(a.bank()));
    if (a.stalled()) log.stalled = true;
    if (a.stallWarning()) log.warning += dt;
    if (a.crashed || ap.state.done) break;
  }
  log.crashed = a.crashed;
  log.tailStrike = a.tailStrike;
  return log;
}

/** The phases in `want` come in this order in `got` (others may lie between). */
bool inOrder(const std::vector<std::string>& got, const std::vector<std::string>& want) {
  size_t i = 0;
  for (const std::string& p : got)
    if (i < want.size() && p == want[i]) i++;
  return i == want.size();
}

/** Standing on the runway of `airfield(len)`: between its ends, within its width. */
void onRunway(const Aircraft& a, double len, const std::string& what) {
  between(a.pos.x, 0, len, what + ": stopped along the runway (m)");
  between(a.pos.z, -15, 15, what + ": stopped across the runway (m)");
  ok(a.groundSpeed() < 0.5 && a.onGround(), what + ": standing");
}

Aircraft inAir(double x, double height, double z, double heading, double speed, Ground& g) {
  Aircraft a;
  a.placeInAir(x, height, z, -heading * DEG, speed, &g);
  return a;
}

std::string crashName(const std::optional<CrashKind>& c) { return c ? crashKindName(*c) : "null"; }

void noCrash(const std::optional<CrashKind>& c, const std::string& what) { ok(!c, what + ": crashed (" + crashName(c) + ")"); }

// ---- 1. take-off ------------------------------------------------------------------------------------

void takeoff() {
  test("take-off: on the centre line, rotation at vr, flaps up, 1000 ft above the field without a stall – then it holds the departure height");
  const AirfieldJson f = airfield();
  Level base(0);
  AirfieldGround g(base, {f});
  const RunwayJson& r = f.runways[0];
  const RunwayStart sp = runwayStart(r, false);
  Aircraft a;
  a.placeOnGround(sp.x, sp.z, sp.yaw, g, true);
  FlightAutopilot ap;
  ap.takeoff(r, false);
  double rotate = NaN, liftoff = NaN, offRoll = 0, at1000 = NaN, flapsUp = NaN, maxBankLow = 0, holding = NaN, holdLo = Inf, holdHi = -Inf;
  const Log log = fly(a, ap, g, 400, [&](double time) {
    if (ap.state.phase == "rotate" && std::isnan(rotate)) rotate = a.ias();
    if (a.onGround()) offRoll = std::max(offRoll, std::abs(a.pos.z));
    else if (std::isnan(liftoff)) liftoff = a.pos.x;
    if (!a.onGround() && a.agl() < 1000 * FT) maxBankLow = std::max(maxBankLow, std::abs(a.bank()));
    if (std::isnan(flapsUp) && ap.state.flapNotch == 0 && !a.onGround()) flapsUp = a.agl();
    if (std::isnan(at1000) && a.agl() >= 1000 * FT) at1000 = time;
    if (std::isnan(holding) && ap.state.mode == "hold") holding = time;
    if (!std::isnan(holding)) {
      holdLo = std::min(holdLo, a.agl());
      holdHi = std::max(holdHi, a.agl());
    }
    return time > holding + 60;
  });
  diag("take-off: rotation at " + fx(rotate, 1) + " m/s, lift-off " + fx(liftoff - sp.x, 0) + " m from the start, " + fx(offRoll, 1) + " m off the centre line, flaps up at " + fx(flapsUp, 0) + " m, 1000 ft after " + fx(at1000, 0) + " s, then " + fx(holdLo, 0) + "…" + fx(holdHi, 0) + " m for a minute");
  noCrash(log.crashed, "take-off");
  ok(!log.stalled, "never stalled");
  ok(log.warning == 0, "the stall warning never sounded");
  ok(!log.tailStrike, "no tail strike");
  ok(inOrder(log.phases, {"lineup", "takeoffRoll", "rotate", "initialClimb", "climb", "cruise"}), join(log.phases));
  between(rotate, TRAINER.vr - 0.5, TRAINER.vr + 1.5, "rotation speed (m/s)");
  between(liftoff, sp.x + 250, r.x2 - 300, "lift-off point, well within the runway (m)");
  ok(offRoll < 3, "on the centre line (" + fx(offRoll, 1) + " m off)");
  between(flapsUp, AUTOPILOT.flapsUp - 5, 200, "flaps up (m above the ground)");
  ok(maxBankLow < 5 * DEG, "wings level in the climb");
  ok(at1000 < 150, "1000 ft above the field after " + fx(at1000, 0) + " s");
  // then: holding the height it climbs to (1500 ft above the field), on the runway's heading
  const double target = AUTOPILOT.departHeight;
  between(holdLo, target - 50 * FT, target + 50 * FT, "height held after the climb, lowest (m)");
  between(holdHi, target - 50 * FT, target + 50 * FT, "height held after the climb, highest (m)");
  ok(std::abs(wrap180(a.heading() - 90)) < 5, "heading " + fx(a.heading(), 1));
}

// ---- 2. heading and height hold ------------------------------------------------------------------------

void headingHold() {
  test("heading hold: turns the shorter way to a new heading, holds it within ±5° and the height within ±50 ft");
  Level g(0);
  Aircraft a = inAir(0, 600, 0, 0, 50, g);
  FlightAutopilot ap;
  for (const auto& [heading, side] : std::vector<std::pair<double, double>>{{120, 1}, {330, -1}, {140, 1}}) {
    ap.hold(heading, 600);
    double first = 0, lo = Inf, hi = -Inf, off = 0;
    const Log log = fly(a, ap, g, 90, [&](double time) {
      if (time > 2 && first == 0 && std::abs(a.turnRate()) > 1 * DEG) first = a.turnRate() > 0 ? 1 : -1;
      lo = std::min(lo, a.altitude());
      hi = std::max(hi, a.altitude());
      if (time > 45) off = std::max(off, std::abs(wrap180(a.heading() - heading)));
      return false;
    });
    diag("to " + jsStr(heading) + "°: turned " + (first > 0 ? "right" : "left") + ", height " + fx(lo, 1) + "…" + fx(hi, 1) + " m, heading within " + fx(off, 1) + "° after 45 s, bank up to " + fx(log.maxBank / DEG, 1) + "°");
    noCrash(log.crashed, "heading hold");
    ok(!log.stalled, "no stall");
    ok(first == side, "to " + jsStr(heading) + "°: the shorter way");
    ok(off < 5, "to " + jsStr(heading) + "°: heading held (" + fx(off, 1) + "° off)");
    between(lo, 600 - 50 * FT, 600 + 50 * FT, "lowest height");
    between(hi, 600 - 50 * FT, 600 + 50 * FT, "highest height");
    ok(log.maxBank < AUTOPILOT.bankMax + 0.5 * DEG, "bank " + fx(log.maxBank / DEG, 1) + "°");
    ok(ap.state.phase == "cruise", "phase cruise");
    ok(ap.state.targetHeading == heading, "target heading");
  }
}

void heightChanges() {
  test("height changes: a climb at full power and climb speed, a descent at a few m/s, each ended without overshooting by more than 50 ft");
  Level g(0);
  Aircraft a = inAir(0, 600, 0, 0, 50, g);
  FlightAutopilot ap;
  for (const double target : {1200.0, 400.0}) {
    ap.hold(0, target);
    std::string vertical;
    double over = 0, h20 = 0, h60 = 0;
    const bool climb = target > a.altitude();
    const Log log = fly(a, ap, g, 400, [&](double time) {
      if (std::abs(time - 5) < DT_ / 2) vertical = ap.state.vertical;
      if (std::abs(time - 20) < DT_ / 2) h20 = a.altitude();
      if (std::abs(time - 60) < DT_ / 2) h60 = a.altitude();
      over = std::max(over, climb ? a.altitude() - target : target - a.altitude());
      return time > 60 && std::abs(a.altitude() - target) < 3 && std::abs(a.verticalSpeed()) < 0.3;
    });
    // (the rate once settled: in the first seconds a climb also trades the speed for height)
    const double rate = (h60 - h20) / 40;
    diag("to " + jsStr(target) + " m: " + vertical + " at " + fx(rate, 2) + " m/s, there after " + fx(log.time, 0) + " s, overshoot " + fx(over, 1) + " m");
    noCrash(log.crashed, "height change");
    ok(!log.stalled && log.warning == 0, "no stall, no warning");
    ok(vertical == (climb ? "climb" : "descend"), "vertical mode " + vertical);
    ok(over < 50 * FT, "overshoot " + fx(over, 1) + " m");
    if (climb) between(rate, 2.5, 5, "climb rate (m/s)");
    else between(rate, -AUTOPILOT.vsDown - 0.3, -2.5, "descent rate (m/s)");
    ok(log.time < 399, "arrived");
  }
}

// ---- 3. to a place, and round it --------------------------------------------------------------------------

void gotoPlace() {
  test("goto: flies to a place 5 km away and circles it at its radius and height");
  Level g(0);
  Aircraft a = inAir(0, 600, 0, 0, 50, g);
  FlightAutopilot ap;
  ap.goto_(3000, -4000, 800);
  double arrived = NaN, rMin = Inf, rMax = 0, hOff = 0;
  const Log log = fly(a, ap, g, 400, [&](double time) {
    if (std::isnan(arrived) && ap.state.phase == "orbit") arrived = time;
    if (!std::isnan(arrived) && time > arrived + 60) {
      const double r = std::hypot(a.pos.x - 3000, a.pos.z + 4000);
      rMin = std::min(rMin, r);
      rMax = std::max(rMax, r);
      hOff = std::max(hOff, std::abs(a.altitude() - 800));
    }
    return false;
  });
  diag("goto: circling after " + fx(arrived, 0) + " s, radius " + fx(rMin, 0) + "…" + fx(rMax, 0) + " m, height within " + fx(hOff, 1) + " m");
  noCrash(log.crashed, "goto");
  ok(!log.stalled, "no stall");
  ok(arrived < 150, "there after " + fx(arrived, 0) + " s (5 km at some 50 m/s and a climb)");
  between(rMin, AUTOPILOT.orbit - 100, AUTOPILOT.orbit + 100, "smallest distance from the place while circling (m)");
  between(rMax, AUTOPILOT.orbit - 100, AUTOPILOT.orbit + 100, "greatest distance from the place while circling (m)");
  ok(hOff < 50 * FT, "height held while circling");
  ok(ap.state.waypoint && *ap.state.waypoint == (AutopilotPoint{3000, -4000}), "the waypoint is the place");
  ok(log.maxBank < AUTOPILOT.bankMax + 0.5 * DEG, "bank");
}

// ---- 4. landing ------------------------------------------------------------------------------------------------

void landingFromEverySide() {
  test("landing on a 1000 m runway: from every side into the circuit (or straight in), touchdown on the main wheels, stopped on the runway");
  const AirfieldJson f = airfield();
  Level base(0);
  AirfieldGround g(base, {f});
  const RunwayJson& r = f.runways[0];
  const std::vector<std::tuple<double, double, double, double, std::string>> cases = {
      {-5000, 3000, 305, 90, "from the south-west: base"},
      {500, -6000, 305, 180, "from the north: downwind"},
      {8000, 500, 305, 270, "from beyond the far end: downwind"},
      {-8000, 200, 400, 90, "from far out on the centre line: straight in"},
      {500, 0, 600, 0, "overhead, high"},
      {-3000, -3000, 1500, 90, "far too high: down beside the field first"},
  };
  for (const auto& [x, z, h, heading, how] : cases) {
    Aircraft a = inAir(x, h, z, heading, 45, g);
    FlightAutopilot ap;
    ap.land(r, false);
    const Log log = fly(a, ap, g, 1500);
    const std::optional<Touchdown>& td = log.touchdown;
    diag(how + ": " + join(log.phases) + "; touchdown " + (td ? fx(td->x, 0) + " m past the threshold, " + fx(td->sink, 2) + " m/s" : "–") + ", stopped " + fx(a.pos.x, 0) + " m, " + fx(log.time, 0) + " s, attempt " + std::to_string(ap.state.attempt));
    noCrash(log.crashed, how);
    ok(!log.stalled && !log.tailStrike, how);
    ok(ap.state.done && ap.state.phase == "stopped", how + ": landed");
    ok(ap.state.goArounds == 0, how + ": at the first attempt");
    ok(td.has_value(), how);
    between(td->sink, 0, 1.5, how + ": sink rate at touchdown (m/s)");
    between(td->x, 60, 450, how + ": touchdown (m past the threshold)");
    onRunway(a, 1000, how);
    ok(inOrder(log.phases, {"final", "flare", "rollout", "stopped"}), how + ": " + join(log.phases));
    ok(log.maxBank < AUTOPILOT.bankMax + 0.5 * DEG, how + ": bank " + fx(log.maxBank / DEG, 1) + "°");
  }
}

void landingInWind() {
  test("landing in wind: 6 m/s across the runway, 5 m/s from behind, 8 m/s on the nose");
  const AirfieldJson f = airfield();
  Level base(0);
  AirfieldGround g(base, {f});
  const RunwayJson& r = f.runways[0];
  const std::vector<std::tuple<double, double, std::string>> cases = {{0, 6, "crosswind from the north"}, {5, 0, "tailwind"}, {-8, 0, "headwind"}};
  for (const auto& [wx, wz, what] : cases) {
    Aircraft a = inAir(-5000, 305, 3000, 90, 45, g);
    a.wind.x = wx;
    a.wind.z = wz;
    FlightAutopilot ap;
    ap.land(r, false);
    const Log log = fly(a, ap, g, 1500);
    diag(what + ": touchdown " + (log.touchdown ? fx(log.touchdown->x, 0) : "–") + " m, " + (log.touchdown ? fx(log.touchdown->z, 1) : "–") + " m off the centre line, stopped " + fx(a.pos.x, 0) + " m, attempts " + std::to_string(ap.state.attempt));
    noCrash(log.crashed, what);
    ok(ap.state.done, what + ": landed");
    onRunway(a, 1000, what);
    ok(log.touchdown && log.touchdown->sink < 1.5, what);
  }
}

void goAround() {
  test("go-around: an approach that is not lined up at 100 ft goes round, flies the circuit again and lands at the second attempt");
  const AirfieldJson f = airfield();
  Level base(0);
  AirfieldGround g(base, {f});
  Aircraft a = inAir(-5000, 305, 3000, 90, 45, g);
  FlightAutopilot ap;
  ap.land(f.runways[0], false);
  bool pushed = false;
  double gaAt = NaN, lowest = Inf;
  const Log log = fly(a, ap, g, 1500, [&](double time) {
    // a gust puts it 60 m beside the centre line just above the gate
    if (!pushed && ap.state.phase == "final" && a.agl() < AUTOPILOT.gate + 4) {
      a.pos.z += 60;
      pushed = true;
    }
    if (ap.state.phase == "goAround") {
      if (std::isnan(gaAt)) gaAt = time;
      lowest = std::min(lowest, a.agl());
    }
    return false;
  });
  diag("go-around at " + fx(gaAt, 0) + " s, lowest " + fx(lowest, 1) + " m; " + join(log.phases) + "; landed after " + fx(log.time, 0) + " s");
  noCrash(log.crashed, "go-around");
  ok(!log.stalled, "no stall");
  ok(pushed && !std::isnan(gaAt), "went round");
  ok(lowest > 15, "it climbed away (lowest " + fx(lowest, 1) + " m)");
  ok(ap.state.goArounds == 1, "one go-around");
  ok(ap.state.attempt == 2, "second attempt");
  ok(inOrder(log.phases, {"final", "goAround", "upwind", "crosswind", "downwind", "base", "final", "flare", "rollout", "stopped"}), join(log.phases));
  onRunway(a, 1000, "second attempt");
}

void rejectedTakeoff() {
  test("rejected take-off: a runway too short to get airborne on is stopped on, not run off the end at speed");
  const AirfieldJson f = airfield(300);
  Level base(0);
  AirfieldGround g(base, {f});
  const RunwayJson& r = f.runways[0];
  const RunwayStart sp = runwayStart(r, false);
  Aircraft a;
  a.placeOnGround(sp.x, sp.z, sp.yaw, g, true);
  FlightAutopilot ap;
  ap.takeoff(r, false);
  double vmax = 0;
  const Log log = fly(a, ap, g, 120, [&](double) {
    vmax = std::max(vmax, a.ias());
    return false;
  });
  diag("rejected at up to " + fx(vmax, 1) + " m/s, stopped " + fx(a.pos.x, 0) + " m along the 300 m runway");
  noCrash(log.crashed, "rejected take-off");
  ok(ap.state.rejected && ap.state.done, "stopped");
  ok(includes(log.phases, "rejected"), "phase rejected");
  ok(vmax < TRAINER.vr, "never flew");
  between(a.pos.x, 0, 300, "stopped on the runway (m)");
}

// ---- 5. the demonstration flight ----------------------------------------------------------------------------------

void demonstration() {
  test("demonstration flight: take-off, circuit, landing on the same runway – the legs in order, the flaps by the book");
  const AirfieldJson f = airfield();
  Level base(0);
  AirfieldGround g(base, {f});
  const RunwayJson& r = f.runways[0];
  const RunwayStart sp = runwayStart(r, false);
  Aircraft a;
  a.placeOnGround(sp.x, sp.z, sp.yaw, g, true);
  FlightAutopilot ap;
  ap.demo(r, false);
  std::vector<int> flaps;
  double downwindLo = Inf, downwindHi = -Inf;
  const Log log = fly(a, ap, g, 900, [&](double) {
    if (flaps.empty() || flaps.back() != ap.state.flapNotch) flaps.push_back(ap.state.flapNotch);
    if (ap.state.phase == "downwind" && ap.state.vertical == "hold" && a.pos.x > 0) {
      downwindLo = std::min(downwindLo, a.agl());
      downwindHi = std::max(downwindHi, a.agl());
    }
    return false;
  });
  if (flaps.empty() || flaps.back() != ap.state.flapNotch) flaps.push_back(ap.state.flapNotch);
  std::string fl;
  for (size_t i = 0; i < flaps.size(); i++) fl += (i ? "→" : "") + std::to_string(flaps[i]);
  diag("demonstration: " + join(log.phases) + " in " + fx(log.time, 0) + " s; flaps " + fl + "; downwind " + fx(downwindLo, 0) + "…" + fx(downwindHi, 0) + " m; touchdown " + (log.touchdown ? fx(log.touchdown->x, 0) + " m at " + fx(log.touchdown->sink, 2) : "–") + " m/s, stopped " + fx(a.pos.x, 0) + " m; bank up to " + fx(log.maxBank / DEG, 1) + "°");
  noCrash(log.crashed, "demonstration");
  ok(!log.stalled && log.warning == 0 && !log.tailStrike, "no stall, warning or tail strike");
  ok(log.phases == std::vector<std::string>{"lineup", "takeoffRoll", "rotate", "initialClimb", "upwind", "crosswind", "downwind", "base", "final", "flare", "rollout", "stopped"}, join(log.phases));
  ok(flaps == std::vector<int>{1, 0, 1, 2, 3, 0}, "flaps " + fl);
  between(downwindLo, AUTOPILOT.circuitHeight - 50 * FT, AUTOPILOT.circuitHeight + 50 * FT, "downwind height, lowest (m)");
  between(downwindHi, AUTOPILOT.circuitHeight - 50 * FT, AUTOPILOT.circuitHeight + 50 * FT, "downwind height, highest (m)");
  ok(log.maxBank < AUTOPILOT.bankMax + 0.5 * DEG, "bank");
  ok(log.time < 480, fx(log.time, 0) + " s");
  ok(ap.state.attempt == 1, "first attempt");
  ok(log.touchdown && log.touchdown->sink < 1.5, "soft touchdown");
  onRunway(a, 1000, "demonstration");
}

void demonstrationRoute() {
  test("demonstration with a sightseeing route, from a field 280 m above the sea and in the other direction");
  const AirfieldJson f = airfield(1200);
  Level base(280);
  AirfieldGround g(base, {f});
  const RunwayJson& r = f.runways[0];
  const RunwayStart sp = runwayStart(r, true);
  Aircraft a;
  a.placeOnGround(sp.x, sp.z, sp.yaw, g, true);
  FlightAutopilot ap;
  const std::vector<AutopilotWaypoint> route = {{-4000, -3000, 280 + 450}, {-2000, 4000, std::nullopt}, {4000, 4000, 280 + 600}};
  ap.demo(r, true, route);
  std::vector<double> closest(route.size(), Inf);
  const Log log = fly(a, ap, g, 1500, [&](double) {
    for (size_t i = 0; i < route.size(); i++) closest[i] = std::min(closest[i], std::hypot(a.pos.x - route[i].x, a.pos.z - route[i].z));
    return false;
  });
  std::string cl;
  for (size_t i = 0; i < closest.size(); i++) cl += (i ? ", " : "") + fx(closest[i], 0);
  diag("route: " + join(log.phases) + " in " + fx(log.time, 0) + " s; came within " + cl + " m of the waypoints; stopped " + fx(a.pos.x, 0) + " m");
  noCrash(log.crashed, "route");
  ok(!log.stalled, "no stall");
  ok(ap.state.runway == "27", "runway 27");
  ok(inOrder(log.phases, {"takeoffRoll", "initialClimb", "route", "final", "flare", "stopped"}), join(log.phases));
  for (const double d : closest) ok(d < 250, "a waypoint flown over at " + fx(d, 0) + " m");
  ok(ap.state.done && ap.state.routeIndex == static_cast<int>(route.size()), "done, all waypoints");
  // landing towards the west: from the second end
  between(a.pos.x, 0, 1200, "stopped on the runway (m)");
  ok(std::abs(a.pos.z) < 15 && a.groundSpeed() < 0.5, "standing on the centre line");
  ok(std::abs(wrap180(a.heading() - 270)) < 10, "facing west");
}

void demonstrationEngines() {
  test("demonstration with every engine of the game and at long frame times");
  const AirfieldJson f = airfield();
  Level base(0);
  AirfieldGround g(base, {f});
  const RunwayJson& r = f.runways[0];
  const RunwayStart sp = runwayStart(r, false);
  const std::vector<std::tuple<double, double, std::string>> cases = {
      {100, 1.0 / 30, "100 PS"},
      {180, 1.0 / 30, "180 PS"},
      {160, 1.0 / 20, "1/20 s steps"},
      {160, 0.2, "fly() with 1/5 s frames"},
  };
  for (const auto& [power, dt, how] : cases) {
    Aircraft a(TRAINER, power);
    a.placeOnGround(sp.x, sp.z, sp.yaw, g, true);
    FlightAutopilot ap;
    ap.demo(r, false);
    Log log;
    if (how.rfind("fly()", 0) == 0) {
      // the game's frame, cut into pieces of at most 1/30 s by the autopilot itself
      double time = 0;
      while (time < 1500 && !ap.state.done && !a.crashed) {
        ap.fly(a, dt, g);
        time += dt;
      }
      log.time = time;
      log.crashed = a.crashed;
      log.tailStrike = a.tailStrike;
      log.phases = {ap.state.phase};
    } else log = fly(a, ap, g, 1500, nullptr, dt);
    diag(how + ": " + fx(log.time, 0) + " s, stopped " + fx(a.pos.x, 0) + " m, attempts " + std::to_string(ap.state.attempt));
    noCrash(log.crashed, how);
    ok(!log.stalled, how);
    ok(ap.state.done, how + ": landed");
    onRunway(a, 1000, how);
  }
}

// ---- 6. placed in the air anywhere (the map's teleport) ---------------------------------------------------------------

void placedInAir() {
  test("placed in the air at any height and speed: the instruments are right at once and the autopilot holds that height and heading");
  Level g(200);
  const std::vector<std::tuple<double, double, double, std::string>> cases = {
      {300, 50, 0, "300 m, cruise"},
      {1500, 30, 0, "1500 m, slow"},
      {3000, 70, 0, "3000 m, fast"},
      {4500, 45, 0, "4500 m"},
      {800, 32, 20, "800 m, slow with 20° of flap"},
  };
  for (const auto& [height, speed, flaps, what] : cases) {
    Aircraft a;
    if (flaps > 0) {
      // (the flaps are where the lever left them before the aeroplane was placed)
      a.placeOnGround(0, 0, 0, g, true);
      FlightControls c;
      c.throttle = 0;
      c.flaps = flaps / TRAINER.flapsMax;
      c.trim = 0;
      c.brake = 1;
      c.parkingBrake = true;
      c.engineOn = true;
      for (int i = 0; i < 6 * 60; i++) a.step(DT_, c, g);
    }
    const double y = 200 + height;
    a.placeInAir(1000, y, 2000, -60 * DEG, speed, &g);
    between(a.agl(), height - TRAINER.gear[1].radius + TRAINER.gear[1].y - 0.5, height, what + ": agl right after the placement");
    FlightAutopilot ap;
    ap.hold(a.heading(), a.altitude());
    double off = 0, lowest = Inf;
    const Log log = fly(a, ap, g, 120, [&](double time) {
      lowest = std::min(lowest, a.altitude());
      if (time > 60) off = std::max(off, std::abs(a.altitude() - y));
      return false;
    });
    diag(what + ": lowest " + fx(lowest - y, 1) + " m, after a minute within " + fx(off, 1) + " m, " + fx(a.ias(), 1) + " m/s indicated, flaps " + std::to_string(ap.state.flapNotch));
    noCrash(log.crashed, what);
    ok(!log.stalled, what + ": no stall");
    ok(off < 50 * FT, what + ": height held (" + fx(off, 1) + " m off)");
    ok(std::abs(wrap180(a.heading() - 60)) < 5, what + ": heading " + fx(a.heading(), 1));
    ok(ap.state.flapNotch == 0, what + ": flaps up");
  }
  // too low over rising ground: it keeps 150 m above what lies ahead
  PlaneGround hill(0.05);
  Aircraft a;
  a.placeInAir(0, 60, 0, 0, 50, &hill);
  FlightAutopilot ap;
  ap.hold(0, a.altitude());
  double low = Inf;
  const Log log = fly(a, ap, hill, 150, [&](double time) {
    if (time > 40) low = std::min(low, a.agl());
    return false;
  });
  diag("placed 60 m above a 5 % slope: " + fx(low, 0) + " m above the ground after 40 s");
  noCrash(log.crashed, "rising ground");
  ok(low > 120, "clear of the rising ground (" + fx(low, 0) + " m)");
}

// ---- 7. the rest of the interface ------------------------------------------------------------------------------------------

void sameFlight() {
  test("the same flight gives the same controls; the HUD state tells what it does; off hands back");
  const AirfieldJson f = airfield();
  auto run = [&]() {
    Level base(0);
    AirfieldGround g(base, {f});
    const RunwayStart sp = runwayStart(f.runways[0], false);
    Aircraft a;
    a.placeOnGround(sp.x, sp.z, sp.yaw, g, true);
    FlightAutopilot ap;
    ap.demo(f.runways[0], false);
    fly(a, ap, g, 150);
    return std::vector<double>{a.pos.x, a.pos.y, a.pos.z, a.q[0], a.q[1], a.q[2], a.q[3], ap.controls.elevator, ap.controls.throttle};
  };
  ok(run() == run(), "the same flight twice");
  Level g(0);
  Aircraft a = inAir(0, 600, 0, 0, 50, g);
  FlightAutopilot ap;
  GotoOptions o;
  o.radius = 500;
  o.right = true;
  ap.goto_(2000, 0, 700, o);
  fly(a, ap, g, 5);
  const AutopilotState& s = ap.state;
  ok(s.mode == "goto", "mode goto");
  ok(s.phase == "enroute", "phase enroute");
  ok(s.targetAltitude == 700, "target altitude");
  ok(s.targetSpeed > 30 && s.targetSpeed < 60, "target speed");
  ok(s.distance > 1500 && s.distance < 2100, "distance");
  ok(ap.engaged(), "engaged");
  ap.off();
  ok(ap.state.mode == "off", "mode off");
  ok(!ap.engaged(), "not engaged");
  const FlightControls before = ap.controls;
  const FlightControls& after = ap.update(a, DT_, g);
  ok(after.throttle == before.throttle && after.elevator == before.elevator && after.aileron == before.aileron && after.rudder == before.rudder && after.flaps == before.flaps && after.trim == before.trim && after.brake == before.brake && after.brakeLeft == before.brakeLeft &&
         after.brakeRight == before.brakeRight && after.parkingBrake == before.parkingBrake && after.engineOn == before.engineOn,
     "switched off it no longer steers");
}

void whichRunway() {
  test("runwayAt finds the runway and direction the aeroplane stands on; landingRunway picks a runway and its approach");
  const AirfieldJson f = airfield();
  AirfieldJson second = airfield(600, "grass");
  second.name = "Weit";
  second.x = 20000;
  second.z = 0;
  RunwayJson r2;
  r2.ref = "18/36";
  r2.x1 = 20000;
  r2.z1 = -300;
  r2.x2 = 20000;
  r2.z2 = 300;
  r2.w = 30;
  r2.s = "grass";
  second.runways = {r2};
  const std::vector<AirfieldJson> both = {second, f};
  const RunwayStart start = runwayStart(f.runways[0], true);
  const auto at = runwayAt(both, start.x, start.z, start.yaw);
  ok(at && at->runway == &both[1].runways[0] && at->fromSecond == true, "on the runway, from its second end");
  ok(!runwayAt({f}, 500, 200, 0), "beside the runway: none");
  const std::vector<AirfieldJson> fs2 = {f, second};
  const auto west = landingRunway(fs2, -6000, 300);
  ok(west && west->field == &fs2[0] && west->fromSecond == false, "from the west: towards the east");
  const auto east = landingRunway(fs2, 6000, 300);
  ok(east && east->field == &fs2[0] && east->fromSecond == true, "from the east: towards the west");
  ok(!landingRunway({second}, 0, 0, 700), "none long enough");
}

// ---- 8. the assist ----------------------------------------------------------------------------------------------------------------

/** The game's own mapping of the stick (game.ts stepFlight): what flying without the assist is. */
void gameMapping(const Aircraft& a, const StickInput& s, FlightControls& c) {
  c.elevator = s.pitch;
  c.aileron = s.roll;
  c.rudder = std::max(-1.0, std::min(1.0, s.yaw + s.roll * (a.onGround() ? 0.9 : 0.25)));
  if (!a.onGround()) c.trim = std::max(-1.0, std::min(1.0, c.trim + s.pitch * DT_ * 0.06));
}

struct HandLog {
  bool stalled = false;
  double maxG = 0;
  double minG = 0;
  double maxBank = 0;
  double maxIas = 0;
  std::optional<CrashKind> crashed;
};

/** Fly by hand for `seconds`: `hands(t)` gives the stick; with an assist or the game's plain mapping. */
HandLog byHand(Aircraft& a, Ground& g, double seconds, const std::function<StickInput(double)>& hands, FlightAssist* assist, FlightControls& c, const std::function<void(double)>& each = nullptr) {
  HandLog log;
  for (int i = 0; i < seconds / DT_; i++) {
    const double t = i * DT_;
    const StickInput s = hands(t);
    if (assist) assist->apply(a, s, DT_, c);
    else gameMapping(a, s, c);
    a.step(DT_, c, g);
    if (a.stalled()) log.stalled = true;
    log.maxG = std::max(log.maxG, a.loadFactor());
    log.minG = std::min(log.minG, a.loadFactor());
    log.maxBank = std::max(log.maxBank, std::abs(a.bank()));
    log.maxIas = std::max(log.maxIas, a.ias());
    if (each) each(t);
    if (a.crashed) break;
  }
  log.crashed = a.crashed;
  return log;
}

struct Trimmed {
  Aircraft a;
  FlightControls c;
};

Trimmed trimmed(double speed, double height, Ground& g) {
  Trimmed r;
  r.a.placeInAir(0, height, 0, 0, speed, &g);
  const LevelFlight lf = r.a.levelFlight(speed, 0);
  r.c.throttle = lf.throttle;
  r.c.flaps = 0;
  r.c.trim = lf.trim;
  r.c.brake = 0;
  r.c.parkingBrake = false;
  r.c.engineOn = true;
  return r;
}

StickInput stick(double pitch = 0, double roll = 0, double yaw = 0) { return StickInput{pitch, roll, yaw}; }

void assistPull() {
  test("assist: the stick pulled right back with the keys for 8 s – no stall and no overstress with it, a stall without");
  Level g(0);
  std::vector<std::string> results;
  for (const auto& [name, opts] : std::vector<std::pair<std::string, const AssistOptions*>>{{"none", nullptr}, {"light", &ASSIST_LIGHT}, {"beginner", &ASSIST_BEGINNER}}) {
    Trimmed tr = trimmed(45, 1500, g);
    std::optional<FlightAssist> assist;
    if (opts) assist.emplace(*opts);
    if (assist) assist->reset(tr.c.trim);
    const HandLog log = byHand(tr.a, g, 12, [](double time) { return stick(time < 8 ? 1 : 0); }, assist ? &*assist : nullptr, tr.c);
    results.push_back(name + ": " + (log.stalled ? "stalled" : "no stall") + ", up to " + fx(log.maxG, 2) + " g");
    noCrash(log.crashed, name);
    if (opts) {
      ok(!log.stalled, name + ": no stall");
      ok(log.maxG < ASSIST.gMax + 0.3, name + ": " + fx(log.maxG, 2) + " g");
    } else ok(log.stalled, "without the assist the pulled stick stalls it (that is what the assist is for)");
  }
  diag(join(results));
}

void assistRoll() {
  test("assist (beginner): rolled hard and let go, the wings come level by themselves and the height stays");
  Level g(0);
  std::vector<std::string> out;
  for (const auto& [name, opts] : std::vector<std::pair<std::string, const AssistOptions*>>{{"none", nullptr}, {"beginner", &ASSIST_BEGINNER}}) {
    Trimmed tr = trimmed(50, 1500, g);
    Aircraft& a = tr.a;
    std::optional<FlightAssist> assist;
    if (opts) assist.emplace(*opts);
    if (assist) assist->reset(tr.c.trim);
    double levelAt = NaN, lo = Inf, hi = -Inf;
    const HandLog log = byHand(a, g, 30, [](double time) { return stick(0, time < 1.2 ? 1 : 0); }, assist ? &*assist : nullptr, tr.c, [&](double time) {
      lo = std::min(lo, a.altitude());
      hi = std::max(hi, a.altitude());
      if (time > 1.2 && std::isnan(levelAt) && std::abs(a.bank()) < 3 * DEG) levelAt = time - 1.2;
    });
    out.push_back(name + ": bank up to " + fx(log.maxBank / DEG, 0) + "°, " + (std::isnan(levelAt) ? "still " + fx(a.bank() / DEG, 0) + "° half a minute later" : "level " + fx(levelAt, 1) + " s after letting go") + ", height " + fx(lo - 1500, 0) + "…" + fx(hi - 1500, 0) + " m");
    if (opts) {
      ok(log.maxBank < ASSIST.bank + 1 * DEG, "bank limited (" + fx(log.maxBank / DEG, 1) + "°)");
      ok(levelAt < 8, "level " + fx(levelAt, 1) + " s after letting go");
      ok(std::abs(a.bank()) < 2 * DEG, "and stays level");
      ok(hi - lo < 30, "height kept (" + fx(hi - lo, 0) + " m)");
    } else ok(std::isnan(levelAt), "without it the aeroplane stays in its bank");
  }
  diag(join(out));
}

/** Roll the aeroplane about its longitudinal axis by `bank` (rad, + = right wing down) – a gust. */
void rollBy(Aircraft& a, double bank) {
  const double x = a.q[0], y = a.q[1], z = a.q[2], w = a.q[3];
  const double s = std::sin(-bank / 2);
  const double k = std::cos(-bank / 2);
  a.q[0] = x * k + y * s;
  a.q[1] = y * k - x * s;
  a.q[2] = z * k + w * s;
  a.q[3] = w * k - z * s;
}

void assistGust() {
  test("assist (beginner): hands off after a gust has rolled and dropped it, it flies straight and level again");
  Level g(0);
  Trimmed tr = trimmed(50, 1500, g);
  Aircraft& a = tr.a;
  FlightAssist assist(ASSIST_BEGINNER);
  assist.reset(tr.c.trim);
  byHand(a, g, 5, [](double) { return stick(); }, &assist, tr.c);
  rollBy(a, -30 * DEG);
  a.vel.y -= 4;
  const double h0 = a.altitude();
  double maxBank = 0;
  const HandLog log = byHand(a, g, 60, [](double) { return stick(); }, &assist, tr.c, [&](double) { maxBank = std::max(maxBank, std::abs(a.bank())); });
  diag("after the gust: bank " + fx(a.bank() / DEG, 1) + "°, " + fx(a.verticalSpeed(), 2) + " m/s, " + fx(a.altitude() - h0, 0) + " m from where it was hit");
  noCrash(log.crashed, "gust");
  ok(!log.stalled, "no stall");
  ok(maxBank < 31 * DEG, "the bank only got smaller");
  ok(std::abs(a.bank()) < 2 * DEG, "wings level");
  ok(std::abs(a.turnRate()) < 0.5 * DEG, "not turning");
  ok(std::abs(a.verticalSpeed()) < 1, "steady (" + fx(a.verticalSpeed(), 2) + " m/s)");
}

void assistCoordinate() {
  test("assist (light): the rudder keeps the ball in the middle in a turn flown with the stick alone");
  Level g(0);
  std::vector<double> out;
  for (const AssistOptions* opts : {static_cast<const AssistOptions*>(nullptr), &ASSIST_LIGHT}) {
    Trimmed tr = trimmed(45, 1500, g);
    Aircraft& a = tr.a;
    std::optional<FlightAssist> assist;
    if (opts) assist.emplace(*opts);
    if (assist) assist->reset(tr.c.trim);
    double slip = 0;
    int n = 0;
    // roll in with half the stick for a second and a half, then let it be
    byHand(a, g, 6, [](double time) { return stick(0, time < 1.5 ? 0.5 : 0); }, assist ? &*assist : nullptr, tr.c, [&](double time) {
      if (time > 0.3 && time < 4) {
        slip += std::abs(a.sideslip());
        n++;
      }
    });
    out.push_back(slip / n);
  }
  diag("mean sideslip rolling in: " + fx(out[0] / DEG, 2) + "° with the game's rudder, " + fx(out[1] / DEG, 2) + "° coordinated");
  ok(out[1] < out[0] * 0.5, "the coordinated rudder halves the sideslip at least");
}

void assistPush() {
  test("assist: the stick pushed forward at full power – the airspeed stays below vne with it, without it the airframe fails");
  Level g(0);
  std::vector<std::string> out;
  for (const auto& [name, opts] : std::vector<std::pair<std::string, const AssistOptions*>>{{"none", nullptr}, {"light", &ASSIST_LIGHT}, {"beginner", &ASSIST_BEGINNER}}) {
    Trimmed tr = trimmed(55, 3000, g);
    tr.c.throttle = 1;
    std::optional<FlightAssist> assist;
    if (opts) assist.emplace(*opts);
    if (assist) assist->reset(tr.c.trim);
    const HandLog log = byHand(tr.a, g, 40, [](double) { return stick(-1); }, assist ? &*assist : nullptr, tr.c);
    out.push_back(name + ": up to " + fx(log.maxIas, 1) + " m/s, " + fx(log.minG, 2) + "…" + fx(log.maxG, 2) + " g" + (log.crashed ? ", " + crashName(log.crashed) : ""));
    if (opts) {
      noCrash(log.crashed, name);
      ok(log.maxIas < TRAINER.vne, name + ": " + fx(log.maxIas, 1) + " m/s");
      between(log.minG, ASSIST.gMin - 0.5, 1, name + ": least load factor (g)");
      between(log.maxG, 1, ASSIST.gMax + 0.5, name + ": greatest load factor (g)");
    } else ok(log.maxIas > TRAINER.vne || log.crashed.has_value(), "without the assist it goes beyond vne or breaks");
  }
  diag(join(out));
}

void assistRunway() {
  test("assist on the runway: the stick steers the nose wheel as the game does, and a take-off by hand works with it");
  const AirfieldJson f = airfield();
  Level base(0);
  AirfieldGround g(base, {f});
  const RunwayStart sp = runwayStart(f.runways[0], false);
  Aircraft a;
  a.placeOnGround(sp.x, sp.z, sp.yaw, g, true);
  FlightAssist assist(ASSIST_BEGINNER);
  FlightControls c;
  c.throttle = 1;
  c.flaps = 1.0 / 3;
  c.trim = 0.15;
  c.brake = 0;
  c.parkingBrake = false;
  c.engineOn = true;
  assist.reset(c.trim);
  for (int i = 0; i < 10; i++) assist.apply(a, stick(0, 1), 0.1, c);
  ok(c.rudder > 0.85, "the nose wheel follows the stick (" + fx(c.rudder, 2) + ")");
  for (int i = 0; i < 10; i++) assist.apply(a, stick(), 0.1, c);
  // a pilot who keeps straight with the stick, pulls at vr and lets go once climbing
  double airborne = NaN;
  const HandLog log = byHand(
      a, g, 60,
      [&](double time) {
        const double keep = std::max(-1.0, std::min(1.0, -0.05 * a.pos.z - 0.3 * wrap180(a.heading() - 90)));
        if (a.onGround()) return stick(a.ias() > TRAINER.vr ? 0.5 : 0, keep);
        if (std::isnan(airborne)) airborne = time;
        return stick(time - airborne < 3 ? 0.3 : 0, 0);
      },
      &assist, c);
  diag("by hand with the assist: airborne after " + fx(airborne, 1) + " s, " + fx(a.agl(), 0) + " m up a minute after brakes off");
  noCrash(log.crashed, "take-off by hand");
  ok(!log.stalled, "no stall");
  ok(a.agl() > 30 && !a.onGround(), "climbing away");
}

void assistRandom() {
  test("assist: random sticks for two minutes at random frame times – never a stall, a crash or a number that is not one");
  double seed = 4242;
  auto rnd = [&]() {
    seed = std::fmod(seed * 1103515245 + 12345, 2147483648.0);
    return seed / 2147483648.0;
  };
  Level g(0);
  for (const AssistOptions* opts : {&ASSIST_BEGINNER, &ASSIST_LIGHT}) {
    Trimmed tr = trimmed(50, 3000, g);
    Aircraft& a = tr.a;
    FlightControls& c = tr.c;
    FlightAssist assist(*opts);
    assist.reset(c.trim);
    StickInput s = stick();
    for (int i = 0; i < 4000; i++) {
      if (i % 25 == 0) {
        const double p = rnd() * 2 - 1;
        const double r = rnd() * 2 - 1;
        const double y = rnd() < 0.2 ? rnd() * 2 - 1 : 0;
        s = stick(p, r, y);
      }
      if (i % 400 == 0) c.throttle = rnd();
      const double dt = rnd() * 0.05 + 0.005;
      assist.apply(a, s, dt, c);
      a.step(dt, c, g);
      for (const double v : {c.elevator, c.aileron, c.rudder, c.trim, a.pos.y, a.ias()}) ok(std::isfinite(v), "finite after " + std::to_string(i) + " steps");
      ok(!a.stalled(), "no stall after " + std::to_string(i) + " steps");
      noCrash(a.crashed, "random sticks");
    }
  }
}

}  // namespace

int main() {
  std::printf("flight_autopilot_test (tests/vehicle/autopilot.test.ts)\n");
  takeoff();
  headingHold();
  heightChanges();
  gotoPlace();
  landingFromEverySide();
  landingInWind();
  goAround();
  rejectedTakeoff();
  demonstration();
  demonstrationRoute();
  demonstrationEngines();
  placedInAir();
  sameFlight();
  whichRunway();
  assistPull();
  assistRoll();
  assistGust();
  assistCoordinate();
  assistPush();
  assistRunway();
  assistRandom();
  std::printf("flight_autopilot_test: all 21 cases passed\n");
  return 0;
}

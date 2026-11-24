// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim world core platform geo
// Origin: tests/sim/crane.test.ts @ v1.7.7
/**
 * The cranes' physics (src/sim/crane.ts): the tipping check against hand
 * calculation, the load charts worked out from it (every one within three
 * quarters of the tipping load in every slewing direction), the load moment
 * limiter (warning, cut-off before an overload leaves the ground, which
 * movements it stops), the hook swinging as a pendulum, the outriggers and
 * the levelling, the wind, and a crane tipping over beyond its limits.
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <optional>
#include <vector>

#include "sim/crane.h"

using namespace fs;

namespace {

constexpr double DT = 1.0 / 240;
constexpr double PI_T = 3.14159265358979323846;

/** a platform: a box with the height a load stands on (`Box3 & { y }`) */
struct Platform : CraneBox3 {
  double y = 0;
};

struct WorldOpts {
  std::optional<std::array<double, 2>> wind;
  std::vector<CraneBox3> boxes;
  std::vector<Platform> platforms;
  std::function<double(double, double)> ground;
};

/** flat ground at height 0, no wind (or the wind given), solids and platforms as asked */
CraneWorld world(const WorldOpts& opts = {}) {
  const std::function<double(double, double)> ground = opts.ground ? opts.ground : [](double, double) { return 0.0; };
  const CraneWind wind = {opts.wind ? (*opts.wind)[0] : 0, opts.wind ? (*opts.wind)[1] : 0, 1};
  const std::vector<Platform> platforms = opts.platforms;
  const std::vector<CraneBox3> boxes = opts.boxes;
  CraneWorld w;
  w.floor = [ground, platforms](double x, double z, double hx, double hz, double y) {
    double best = std::max({ground(x, z), ground(x - hx, z - hz), ground(x + hx, z - hz), ground(x - hx, z + hz), ground(x + hx, z + hz)});
    for (const Platform& p : platforms)
      if (p.y <= y + 0.05 && p.y > best && !(x + hx <= p.x0 || x - hx >= p.x1 || z + hz <= p.z0 || z - hz >= p.z1)) best = p.y;
    return best;
  };
  w.boxes = [boxes] { return boxes; };
  w.ground = ground;
  w.wind = [wind] { return wind; };
  return w;
}

void run(Crane& c, const CraneWorld& w, double seconds, const std::function<bool()>& each = nullptr) {
  const long n = std::lround(seconds / DT);
  for (long i = 0; i < n; i++) {
    c.step(DT, w);
    if (each && each()) return;
  }
}

HookLoad pallet(double t) { return {"p", t, 0.6, 0.55, 0.5, 1.6, 0, 1.4}; }

/** Lower the hook of a crane until it stands on the ground, then sling a load of `t` tonnes standing there. */
void slingOnGround(Crane& c, const CraneWorld& w, double t) {
  c.cmd.hoist = -1;
  run(c, w, 90, [&] { return c.resting; });
  c.cmd.hoist = 0;
  run(c, w, 1);
  c.attach(pallet(t), c.hook.x, 1.1, c.hook.z);
}

/** A mobile crane on full outriggers, levelled, its boom up at `luff` and out to `tele`. */
void mobileSetUp(Crane& c, const CraneWorld& w, double luffDeg, double tele = 0) {
  c.place(0, 0, 0, 0);
  c.cmd.beams = 1;
  run(c, w, 16);
  c.cmd.beams = 0;
  c.cmd.jacks = 1;
  run(c, w, 20, [&] { return c.onOutriggers(w); });
  run(c, w, 1);
  c.cmd.jacks = 0;
  c.cmd.boom = 1;
  run(c, w, 60, [&] { return c.q.luff >= (luffDeg * PI_T) / 180; });
  c.cmd.boom = 0;
  if (tele > 0) {
    c.cmd.tele = 1;
    run(c, w, 120, [&] { return c.q.tele >= tele; });
    c.cmd.tele = 0;
  }
  run(c, w, 3);
}

Joints J(double slew, double trolley, double luff, double tele, double inner, double outer, double ext, double rope) {
  Joints q;
  q.slew = slew;
  q.trolley = trolley;
  q.luff = luff;
  q.tele = tele;
  q.inner = inner;
  q.outer = outer;
  q.ext = ext;
  q.rope = rope;
  return q;
}

std::vector<Weight> plus(std::vector<Weight> a, const Weight& w) {
  a.push_back(w);
  return a;
}

void tippingCheck() {
  const std::vector<std::array<double, 2>> square = {{-2, -2}, {2, -2}, {2, 2}, {-2, 2}};
  {
    auto pts = square;
    pts.push_back({0, 0});
    pts.push_back({1, 1});
    assert(convexHull(pts).size() == 4);  // points inside are no supports
  }
  const std::vector<Weight> base = {{10, 0, 1, 0}};
  assert(stability(base, {}, square).u == 0);  // a weight over the middle tips nothing
  // 10 t over the middle (2 m inside the line x = 2) and 4 t at x = 7 (5 m beyond it): 20 t·m hold, 20 t·m tip
  const double tip = tippingLoad(base, 7, 0, square);
  assert(std::abs(tip - 4) < 1e-9);
  assert(std::abs(stability(plus(base, {4, 7, 5, 0}), {}, square).u - 1) < 1e-9);
  assert(stability(plus(base, {3, 7, 5, 0}), {}, square).u < 1);
  assert(stability(plus(base, {5, 7, 5, 0}), {}, square).u > 1);
  // a sideways push at a height tips as well: 10 t · 2 m held against 20 kN·9.81 at 1 m … at 2 m
  const Stability side = stability(base, {{10 * G * 2, 0, 1}}, square);
  assert(std::abs(side.u - 1) < 1e-9);
  // fewer than three supports: no standing at all
  assert(stability(base, {}, {{0, 0}, {1, 0}}).u == std::numeric_limits<double>::infinity());
}

void towerCrane() {
  assert(towerCapacity(10) == 6);
  assert(towerCapacity(16) == 6);
  assert(std::abs(towerCapacity(48.5) - 1.6) < 0.01);
  for (double r = 3; r < 48; r += 1) assert(towerCapacity(r + 1) <= towerCapacity(r) + 1e-9);  // never more further out
  const auto feet = supportsFor(TOWER_CRANE, "fixed");
  for (const double r : {2.5, 16.0, 30.0, 48.5}) {
    for (int k = 0; k < 16; k++) {
      const Joints q = J((k / 16.0) * PI_T * 2, r, 0, 0, 0, 0, 0, 5);
      const auto [dx, dz] = boomDir(q.slew);
      const Stability loaded = stability(craneWeights(TOWER_CRANE, q, {{towerCapacity(r) + TOWER_CRANE.hook, dx * r, 31, dz * r}}), {}, feet);
      const Stability empty = stability(craneWeights(TOWER_CRANE, q), {}, feet);
      assert(loaded.u < 0.75);
      assert(empty.u < 0.6);
    }
  }
}

void mobileChart() {
  const CraneType mobile = "mobile";
  const LoadChart& chart = chartOf(mobile);
  const std::vector<Setting> settings = {"full", "half", "zero", "tyres"};
  for (size_t li = 0; li < chart.lengths.size(); li++) {
    for (size_t ri = 1; ri < chart.radii.size(); ri++) {
      for (const Setting& s : settings) {
        const auto& col = chart.t.at(s)[li];
        if (col[ri] > 0 && col[ri - 1] > 0) assert(col[ri] <= col[ri - 1] + 1e-9);
      }
      const auto at = [&](const Setting& s) { return chart.t.at(s)[li][ri]; };
      assert(at("full") >= at("half") && at("half") >= at("zero") && at("zero") >= at("tyres"));
    }
  }
  // the numbers of a 40-t class crane: about 12 t at 10 m on full outriggers, a fraction of that on tyres
  const double full10 = capacity("mobile", "full", 10, 14.5);
  const double tyres10 = capacity("mobile", "tyres", 10, 14.5);
  std::printf("mobile: full at 10 m %.3f t, on tyres %.3f t\n", full10, tyres10);
  assert(full10 > 10 && full10 < 15);
  assert(tyres10 < 0.25 * full10);
  assert(capacity("mobile", "full", 3, 9.6) == 40);  // its 40 t close in
  // at the rated load, in every slewing direction, it stands – and it would take a third more (the load ÷ 0.75) to tip it
  const MobileGeometry& g = MOBILE_GEOMETRY;
  struct Case {
    Setting s;
    double L;
    double R;
  };
  for (const Case& cs : {Case{"full", 14.48, 10}, Case{"full", 24.24, 20}, Case{"half", 19.36, 12}, Case{"zero", 14.48, 8}}) {
    const double cap = capacity("mobile", cs.s, cs.R, cs.L);
    const double luff = std::acos((cs.R - g.footA) / (cs.L + g.head));
    for (int k = 0; k < 36; k++) {
      const Joints q = J((k / 36.0) * PI_T * 2, 0, luff, (cs.L - g.length0) / (g.length1 - g.length0), 0, 0, 0, 5);
      const auto [dx, dz] = boomDir(q.slew);
      const double lx = MOBILE_CRANE.slewX + dx * cs.R;
      const double lz = MOBILE_CRANE.slewZ + dz * cs.R;
      const double u = stability(craneWeights(MOBILE_CRANE, q, {{cap, lx, 10, lz}}), {}, supportsFor(MOBILE_CRANE, cs.s)).u;
      assert(u < 0.9);
      const double tip = tippingLoad(craneWeights(MOBILE_CRANE, q), lx, lz, supportsFor(MOBILE_CRANE, cs.s));
      assert(tip >= cap / MARGIN_OUTRIGGERS - 0.01);
      // (and at the tipping load itself the check says just that)
      if (std::isfinite(tip)) assert(std::abs(stability(craneWeights(MOBILE_CRANE, q, {{tip, lx, 10, lz}}), {}, supportsFor(MOBILE_CRANE, cs.s)).u - 1) < 1e-6);
    }
  }
}

void loaderCrane() {
  for (const double r : {3.5, 5.0, 7.0, 10.0}) {
    const double full = capacity("loader", "full", r);
    assert(full <= LOADER_GEOMETRY.moment / r + 1e-9 && full > 0.9 * std::min(LOADER_GEOMETRY.maxLoad, LOADER_GEOMETRY.moment / r) - 0.06);
    assert(capacity("loader", "tyres", r) < 0.6 * full);
  }
  // folded for the road the hook hangs close behind the cab; the extensions take the head further out
  const Crane c("loader");
  assert(c.stowed());
  Joints q = c.q;
  q.inner = 0.3;
  q.outer = -0.3;
  Joints qn = q;
  qn.ext = 0;
  Joints qf = q;
  qf.ext = 1;
  const HeadAt near = headOf("loader", qn);
  const HeadAt far = headOf("loader", qf);
  assert(std::abs(far.a - near.a - (LOADER_GEOMETRY.outer1 - LOADER_GEOMETRY.outer0)) < 1e-9);
}

void limiterCutsOverload() {
  const CraneWorld w = world();
  struct Case {
    double t;
    bool lifts;
  };
  for (const Case& cs : {Case{5.2, true}, Case{6.3, false}, Case{8, false}}) {
    Crane c("tower");
    c.place(0, 0, 0, 0);
    slingOnGround(c, w, cs.t);
    c.cmd.hoist = 1;
    bool off = false;
    bool cut = false;
    run(c, w, 12, [&] {
      off = off || c.events.liftedOff;
      cut = cut || c.events.cut;
      return false;
    });
    if (cs.lifts) {
      assert(off && c.clearance > 1);  // comes up
      assert(c.lmi.warn && !c.lmi.stop);  // warned, not cut off
    } else {
      assert(cut && c.lmi.stop);  // cut off
      assert(c.resting && c.clearance < 0.02);  // stays on the ground
      // what raises the moment stays cut off, what lowers it works
      assert(c.momentUp("rope", -1) && c.momentUp("trolley", 1));
      assert(!c.momentUp("rope", 1) && !c.momentUp("trolley", -1));
      c.cmd.hoist = 0;
      c.cmd.boom = 1;
      const double was = c.q.trolley;
      run(c, w, 2);
      assert(c.q.trolley == was);  // the trolley does not go out
      c.cmd.boom = -1;
      run(c, w, 2);
      assert(c.q.trolley < was - 0.3);  // it comes in
    }
  }
}

void limiterKnowsMoment() {
  Crane m("mobile");
  m.q.luff = 0.6;
  m.q.tele = 0.3;
  assert(m.momentUp("luff", -1) && !m.momentUp("luff", 1));
  assert(m.momentUp("tele", 1) && !m.momentUp("tele", -1));
  assert(m.momentUp("rope", -1) && !m.momentUp("rope", 1));
  assert(!m.momentUp("slew", 1));
  Crane l("loader");
  l.q.inner = 0.4;
  l.q.outer = -0.4;
  assert(l.momentUp("ext", 1) && !l.momentUp("ext", -1));
}

void pendulum() {
  const CraneWorld w = world();
  Crane c("tower");
  c.place(0, 0, 0, 0);
  run(c, w, 3);
  // run the trolley out and stop: the load swings on
  c.cmd.boom = 1;
  run(c, w, 6);
  c.cmd.boom = 0;
  double max = 0;
  run(c, w, 2, [&] {
    max = std::max(max, c.swing);
    return false;
  });
  assert(max > 0.2);  // it swings after the trolley stops
  // its period: 2π √(L/g)
  const double L = c.head.y - c.hook.y;
  std::vector<double> crossings;
  double prev = c.hook.z - c.head.z;
  double t = 0;
  run(c, w, 40, [&] {
    t += DT;
    const double d = c.hook.z - c.head.z;
    if (prev < 0 && d >= 0) crossings.push_back(t);
    prev = d;
    return false;
  });
  const double period = (crossings.back() - crossings.front()) / static_cast<double>(crossings.size() - 1);
  const double want = 2 * PI_T * std::sqrt(L / G);
  std::printf("pendulum: period %.3f s, wanted %.3f s; swing %.3f of %.3f m\n", period, want, c.swing, max);
  assert(std::abs(period - want) / want < 0.05);
  assert(c.swing < max * 0.6);  // damped
  // at rest the rope holds the hook block's weight: that is what the limiter shows
  run(c, w, 30);
  assert(std::abs(c.lmi.load - TOWER_CRANE.hook) < 0.02);
}

void outriggers() {
  // ground rising 2 % to the east and 1 % to the south
  WorldOpts o;
  o.ground = [](double x, double z) { return 0.02 * x + 0.01 * z; };
  const CraneWorld w = world(o);
  Crane c("mobile");
  c.place(0, 0, 0, 0, 0.02, 0.01);
  assert(c.setting(w) == "tyres");
  c.cmd.beams = 1;
  run(c, w, 16);
  c.cmd.beams = 0;
  assert(c.beamSetting() > 0.99);
  c.cmd.jacks = 1;
  run(c, w, 25, [&] { return c.onOutriggers(w); });
  c.cmd.jacks = 0;
  assert(c.onOutriggers(w));  // the wheels are free
  assert(c.setting(w) == "full");
  assert(c.incline() > 1);  // still on the slope
  // not level: the limiter lets nothing out
  run(c, w, 0.5);
  assert(c.lmi.notLevel);
  c.cmd.boom = 1;
  const double before = c.q.luff;
  run(c, w, 2);
  assert(c.q.luff > before);  // raising the boom (bringing it in) still works
  c.cmd.boom = 0;
  c.cmd.tele = 1;
  run(c, w, 2);
  assert(c.q.tele == 0);  // telescoping out is cut off
  c.cmd.tele = 0;
  // the levelling
  c.cmd.level = true;
  run(c, w, 30, [&] { return c.incline() < 0.05; });
  c.cmd.level = false;
  std::printf("outriggers: levelled to %.3f deg\n", c.incline());
  assert(c.incline() <= 0.3);  // levelled
  assert(c.onOutriggers(w));  // still off its wheels
  // the boom is up: jacks and beams stay where they are
  const std::vector<double> jacks = c.jacks;
  c.cmd.jacks = -1;
  c.cmd.beams = -1;
  run(c, w, 3);
  assert(c.jacks == jacks);  // the jacks stay down under a raised boom
  assert(c.beamSetting() > 0.99);  // the beams stay out
}

void tipsOver() {
  const CraneWorld w = world();
  Crane c("mobile");
  mobileSetUp(c, w, 45, 0.4);
  const double R = c.lmi.radius;
  const double cap = capacity("mobile", "full", R, c.lmi.length);
  assert(cap > 0);
  // hung on in the air (as if the limiter had been bridged): far beyond what tips it
  const double top = c.hook.y - MOBILE_CRANE.hookDrop - 1.6;
  HookLoad l = pallet(cap * 2);
  l.sling = 1.6;
  c.attach(l, c.hook.x, top, c.hook.z);
  run(c, w, 3, [&] { return c.tipped.has_value(); });
  std::printf("tipping: u %.3f\n", c.lmi.tipping);
  assert(c.tipped);
  run(c, w, 2);
  assert((c.tipped ? c.tipped->angle : 0) > 0.3);  // and goes over
}

void withinChart() {
  const CraneWorld w = world();
  Crane c("mobile");
  mobileSetUp(c, w, 45, 0.4);
  const double cap = capacity("mobile", "full", c.lmi.radius, c.lmi.length);
  const double t = cap * 0.8 - MOBILE_CRANE.hook;
  const double top = c.hook.y - MOBILE_CRANE.hookDrop - 1.6;
  c.attach(pallet(t), c.hook.x, top, c.hook.z);
  run(c, w, 8);
  std::printf("within: limiter %.1f %%, tipping %.3f\n", c.lmi.ratio * 100, c.lmi.tipping);
  assert(!c.tipped);
  assert(std::abs(c.lmi.ratio - 0.8) < 0.03);
  assert(c.lmi.tipping < 0.75);
}

void wind() {
  assert(windAtHeight(10, 40) > windAtHeight(10, 10) && windAtHeight(10, 10) == 10);
  for (double t = 0; t < 200; t += 0.7) {
    const double g = gust(t, 1.5);
    assert(g >= 1 - 1e-9 && g <= 1.5 + 1e-9);
  }
  Crane calm("tower");
  Crane windy("tower");
  const CraneWorld w0 = world();
  WorldOpts o1;
  o1.wind = std::array<double, 2>{12, 0};
  const CraneWorld w1 = world(o1);
  for (auto [c, w] : {std::pair<Crane*, const CraneWorld*>{&calm, &w0}, std::pair<Crane*, const CraneWorld*>{&windy, &w1}}) {
    c->place(0, 0, 0, 0);
    c->attach({"c", 2.4, 3, 1.3, 1.2, 3.8, 0, 15.7}, c->hook.x, c->hook.y - TOWER_CRANE.hookDrop - 3.8, c->hook.z);
    run(*c, *w, 10);
  }
  // (over two periods of its swing: where it hangs on average)
  double off = 0;
  double still = 0;
  int n = 0;
  for (int i = 0; i < 14 / DT; i++) {
    windy.step(DT, w1);
    calm.step(DT, w0);
    off += windy.hook.x - windy.head.x;
    still = std::max(still, std::abs(calm.hook.x - calm.head.x));
    n++;
  }
  std::printf("wind: blown east %.3f m, calm %.4f m, at the jib %.2f m/s\n", off / n, still, windy.lmi.wind);
  assert(off / n > 0.3);
  assert(still < 0.05);
  assert(windy.lmi.wind > 12);  // the wind at the jib is stronger than 10 m up
}

void platformAndWall() {
  Platform plat;
  plat.x0 = -5;
  plat.x1 = 5;
  plat.z0 = -16;
  plat.z1 = -12;
  plat.y0 = -2;
  plat.y1 = 6;
  plat.y = 6;
  const CraneBox3 wall = {-5, 0, -10.2, 5, 20, -9.8};
  WorldOpts o;
  o.platforms = {plat};
  o.boxes = {wall};
  const CraneWorld w = world(o);
  Crane c("tower");
  c.place(0, 0, 0, 0);
  c.q.trolley = 14;
  c.place(0, 0, 0, 0);
  c.attach(pallet(1.2), c.hook.x, c.hook.y - TOWER_CRANE.hookDrop - 1.6, c.hook.z);
  double landed = 0;
  c.cmd.hoist = -1;
  run(c, w, 40, [&] {
    landed = std::max(landed, c.events.landed);
    return c.resting;
  });
  c.cmd.hoist = 0;
  run(c, w, 2);
  std::printf("platform: bottom at %.3f, landed at %.3f m/s\n", c.hook.y - c.drop(), landed);
  assert(c.resting);  // it stands
  assert(std::abs(c.hook.y - c.drop() - 6) < 0.02);  // on the platform
  assert(landed > 0 && landed < 0.5);  // set down gently near the platform
  // run the trolley in: the load hits the wall in its way
  c.cmd.hoist = 1;
  run(c, w, 3);
  c.cmd.hoist = 0;
  c.cmd.boom = -1;
  double hit = 0;
  double nearest = -std::numeric_limits<double>::infinity();
  run(c, w, 8, [&] {
    hit = std::max(hit, c.events.hit);
    nearest = std::max(nearest, c.hook.z + c.footprint()[1]);
    return false;
  });
  std::printf("wall: hit at %.3f m/s, near face at %.3f\n", hit, nearest);
  assert(hit > 0.05);  // it ran into the wall
  assert(nearest <= -10.2 + 0.03);  // and does not pass through it
}

void everyCraneBuilt() {
  for (const CraneType type : {"tower", "mobile", "loader"}) {
    Crane c(type);
    c.place(10, 5, -3, 0.4);
    assert(c.hook.y < c.head.y);  // the hook below the head
    assert(std::isfinite(c.radius()));
  }
  assert(TOWER_GEOMETRY.trolleyMax <= TOWER_GEOMETRY.jibLength);
}

}  // namespace

int main() {
  tippingCheck();
  towerCrane();
  mobileChart();
  loaderCrane();
  limiterCutsOverload();
  limiterKnowsMoment();
  pendulum();
  outriggers();
  tipsOver();
  withinChart();
  wind();
  platformAndWall();
  everyCraneBuilt();
  std::puts("crane_test: all passed");
  return 0;
}

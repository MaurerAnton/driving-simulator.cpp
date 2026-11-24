// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/walkmodes.ts @ v1.7.7
#include "game/walkmodes.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace fs {

const std::vector<WalkSetting> WALK_SETTINGS = {"auto", "classic", "tank", "tap"};

WalkMode walkModeOf(const WalkSetting& setting, bool touch) { return setting == "auto" ? (touch ? "classic" : "tank") : setting; }

namespace {

/** a marked crossing is taken when the way round it is at most this much longer (m) */
constexpr double DETOUR = 70;
/** points along a pavement every this many metres */
constexpr double STEP = 12;

/** The place `off` metres to the left (+) of the line at `s` metres along it. */
FootPoint besideLine(const Polyline3& pl, double s, double off) {
  PolySample p;
  samplePolyline(pl, s, p);
  // (left of the direction (tx, tz) is (tz, −tx))
  return {p.x + p.tz * off, p.z - p.tx * off};
}

/** The middle of the pavement on one side (−1 right, 1 left) – or, without one, a metre beyond the kerb. */
double walkOff(const FootRoad& r, double side) {
  const double sw = side > 0 ? r.swL : r.swR;
  return side * (r.half + (sw > 0.5 ? std::min(sw, 4.0) / 2 : 1));
}

/** Points along the pavement on `side` from s0 to s1 (s0 itself left out, s1 included). */
void along(const FootRoad& r, double s0, double s1, double side, std::vector<FootPoint>& out) {
  const int n = std::max(1, static_cast<int>(std::ceil(std::abs(s1 - s0) / STEP)));
  for (int i = 1; i <= n; i++) out.push_back(besideLine(*r.pl, s0 + ((s1 - s0) * i) / n, walkOff(r, side)));
}

double sign1(double v) { return v > 0 ? 1 : v < 0 ? -1 : 1; }

/** the shortest way between two headings (radians, −π … π) */
double angleTo(double from, double to) {
  double d = to - from;
  d -= PI * 2 * jsRound(d / (PI * 2));
  return d;
}

}  // namespace

std::vector<FootPoint> planFootPath(double fx, double fz, double tx, double tz, const std::vector<FootRoad>& roads) {
  PolyProjection pa;
  PolyProjection pb;
  const double direct = std::hypot(tx - fx, tz - fz);
  // the road to be crossed: the one the straight way crosses, nearest to the start
  struct Cross {
    const FootRoad* r;
    double s0, s1, side0, side1;
  };
  std::optional<Cross> cross;
  double crossCost = JS_INF;
  // a road to go along on the same side
  struct Follow {
    const FootRoad* r;
    double s0, s1, side;
  };
  std::optional<Follow> follow;
  double followCost = JS_INF;
  for (const FootRoad& r : roads) {
    if (r.pl->len < 2) continue;
    projectOnPolyline(*r.pl, fx, fz, pa);
    projectOnPolyline(*r.pl, tx, tz, pb);
    // (beyond either end of the road: not this road's business)
    auto inside = [&](const PolyProjection& p) { return p.s > 0.5 && p.s < r.pl->len - 0.5; };
    if (!inside(pa) && !inside(pb)) continue;
    const double reach = r.half + 25;
    if (std::abs(pa.off) > reach || std::abs(pb.off) > reach) continue;
    const double side0 = sign1(pa.off);
    const double side1 = sign1(pb.off);
    // (standing on the road itself: just go – off it, the way one wants)
    const bool onRoad = std::abs(pa.off) < r.half * 0.8;
    if (side0 != side1 && !onRoad && std::abs(pb.off) > r.half * 0.8) {
      if (std::abs(pa.off) < crossCost) {
        crossCost = std::abs(pa.off);
        cross = Cross{&r, pa.s, pb.s, side0, side1};
      }
    } else if (side0 == side1 && !onRoad && std::abs(pb.s - pa.s) > STEP && std::abs(pa.off) < r.half + 8 && std::abs(pb.off) < r.half + 15) {
      if (std::abs(pa.off) < followCost) {
        followCost = std::abs(pa.off);
        follow = Follow{&r, pa.s, pb.s, side0};
      }
    }
  }
  std::vector<FootPoint> out;
  if (cross) {
    const FootRoad& r = *cross->r;
    const double s0 = cross->s0;
    const double s1 = cross->s1;
    // a crossing near the way (the detour over it not much longer), else straight over where the place lies
    double sx = std::max(1.0, std::min(r.pl->len - 1, s1));
    double best = JS_INF;
    for (const double c : r.xings) {
      const double extra = std::abs(c - s0) + std::abs(s1 - c) - std::abs(s1 - s0);
      if (extra <= DETOUR && extra < best) {
        best = extra;
        sx = c;
      }
    }
    if (std::abs(sx - s0) > 2) along(r, s0, sx, cross->side0, out);
    else out.push_back(besideLine(*r.pl, sx, walkOff(r, cross->side0)));
    out.push_back(besideLine(*r.pl, sx, walkOff(r, cross->side1)));
    if (std::abs(s1 - sx) > STEP) along(r, sx, s1, cross->side1, out);
  } else if (follow) {
    // (the last point beside the place: then to it)
    along(*follow->r, follow->s0, follow->s1, follow->side, out);
  }
  // (no point that leads further away than the place itself, and none right on top of the next)
  std::vector<FootPoint> pts;
  for (const FootPoint& p : out)
    if (std::hypot(p.x - fx, p.z - fz) < direct + DETOUR + 30) pts.push_back(p);
  pts.push_back({tx, tz});
  std::vector<FootPoint> clean;
  for (size_t i = 0; i < pts.size(); i++) {
    const FootPoint& p = pts[i];
    const FootPoint last = clean.empty() ? FootPoint{fx, fz} : clean.back();
    if (std::hypot(p.x - last.x, p.z - last.z) > 0.8 || i == pts.size() - 1) clean.push_back(p);
  }
  return clean;
}

void WalkControl::goTo(const std::vector<FootPoint>& p) {
  path = p;
  lastGap = JS_INF;
  stuckT = 0;
  blocked = false;
  sinceLook = 9;
}

WalkStep WalkControl::step(double dt, const WalkIntent& want, const At& at, bool looked) {
  // (running: Shift held – or switched on by Caps Lock or the runner's button: Input.runToggle)
  const bool run = want.run;
  // turning by keys or the bar: eased in and out
  turnNow += (want.turn - turnNow) * (1 - std::exp(-dt * (std::abs(want.turn) > std::abs(turnNow) ? 7 : 10)));
  if (std::abs(turnNow) < 1e-3) turnNow = 0;
  const bool manual = std::abs(want.fwd) > 0.05 || std::abs(want.side) > 0.05;
  if (manual) cancel();
  if (looked) sinceLook = 0;
  else sinceLook += dt;
  if (path.empty()) return {want.fwd, want.side, turnNow, run};
  // by oneself, along the way: to the next point
  FootPoint p = path[0];
  double gap = std::hypot(p.x - at.x, p.z - at.z);
  while (gap < (path.size() > 1 ? 1.2 : 0.35)) {
    path.erase(path.begin());
    lastGap = JS_INF;
    stuckT = 0;
    if (path.empty()) return {0, 0, turnNow, run};
    p = path[0];
    gap = std::hypot(p.x - at.x, p.z - at.z);
  }
  // not coming any nearer (a wall, a fence): give up after a while
  if (gap < lastGap - 0.05) {
    lastGap = gap;
    stuckT = 0;
  } else {
    stuckT += dt;
    if (stuckT > 1.6) {
      cancel();
      blocked = true;
      return {0, 0, turnNow, run};
    }
  }
  // the way to go, in the walker's own frame: forwards (−sin, −cos), to the right (cos, −sin)
  const double dx = (p.x - at.x) / gap;
  const double dz = (p.z - at.z) / gap;
  const double s = std::sin(at.yaw);
  const double c = std::cos(at.yaw);
  double fwd = -dx * s - dz * c;
  double side = dx * c - dz * s;
  // slowing down for the last steps
  const double k = path.size() == 1 ? std::min(1.0, gap / 0.8 + 0.25) : 1;
  fwd *= k;
  side *= k;
  // the walker turns towards the way (unless the view was just turned by hand) – no faster than a person turns
  double turn = turnNow;
  if (sinceLook > 1.2) {
    const double head = std::atan2(-dx, -dz);
    const double d = angleTo(at.yaw, head);
    // (turn is in units of 1.9 rad/s: at most about 3 rad/s)
    turn = std::max(-1.6, std::min(1.6, d * 3));
  }
  return {fwd, side, turn, run};
}

}  // namespace fs

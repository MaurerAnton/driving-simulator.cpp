// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/wildlife.ts @ v1.7.7
#include "sim/wildlife.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/geom.h"
#include "core/random.h"
#include "world/cover.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

namespace {

/** how often to look for a place ahead (s), how far ahead (s of driving, m at least and at most) */
constexpr double CHECK = 2.5;
constexpr double AHEAD_S = 6;
constexpr double AHEAD_MIN = 70;
constexpr double AHEAD_MAX = 140;
/** the chance at each look where a forest meets the road at night – twice that at dusk */
constexpr double CHANCE = 0.05;
/** above this sun (degrees) it is day: no animals */
constexpr double DAY_SUN = 4;
/** at dusk: the sun between these */
constexpr double DUSK[2] = {-7, 4};
/** after a group has gone: a while without (s) */
constexpr double COOLDOWN = 100;
/** gone when this far from the player (m) */
constexpr double GONE = 320;

/** The measures of an animal for the traffic: length, width. */
std::pair<double, double> sizeOf(const WildKind& kind) {
  if (kind == "deer") return {1.5, 0.5};
  if (kind == "boar") return {1.3, 0.6};
  return {0.7, 0.35};
}

}  // namespace

Wildlife::Wildlife(WildHost host_, std::function<double()> rnd_) : chance(CHANCE), host(std::move(host_)), rnd(rnd_ ? std::move(rnd_) : std::function<double()>(fs::rnd)), timer(CHECK) {}

void Wildlife::clear() {
  animals.clear();
  boxes.clear();
}

void Wildlife::update(double dt, const PlayerState& player) {
  if (!enabled) {
    clear();
    return;
  }
  cooldown = std::max(0.0, cooldown - dt);
  timer -= dt;
  if (timer <= 0) {
    timer = CHECK;
    if (animals.empty() && cooldown <= 0) look(player);
  }
  step(dt, player);
  makeBoxes();
}

/** Is there a forest at the road ahead – and do animals come out of it now? */
void Wildlife::look(const PlayerState& player) {
  const double sun = host.sun();
  if (sun > DAY_SUN || player.speed < 7) return;
  const double d = clamp(player.speed * AHEAD_S, AHEAD_MIN, AHEAD_MAX);
  const double px = player.x - std::sin(player.yaw) * d;
  const double pz = player.z - std::cos(player.yaw) * d;
  WildRoad& r = road;
  if (!host.road(px, pz, 15, r) || !r.rural) return;
  // (the road ahead runs about the way the player goes – not a road crossing it)
  const double fx = -std::sin(player.yaw);
  const double fz = -std::cos(player.yaw);
  if (std::abs(r.dx * fx + r.dz * fz) < 0.6) return;
  // the forest: on one side, near the road and further in
  const double nx = -r.dz;
  const double nz = r.dx;
  std::vector<double> sides;
  for (const double side : {1.0, -1.0}) {
    bool forest = true;
    for (const double off : {6.0, 14.0})
      if (host.coverAt(r.x + nx * side * (r.half + off), r.z + nz * side * (r.half + off)) != COVER_FOREST) forest = false;
    if (forest) sides.push_back(side);
  }
  if (sides.empty()) return;
  const bool dusk = sun >= DUSK[0] && sun <= DUSK[1];
  if (rnd() >= chance * (dusk ? 2 : 1)) return;
  const double side = sides[static_cast<size_t>(std::floor(rnd() * static_cast<double>(sides.size())))];
  spawn(r, side);
}

/** A group comes out of the forest on `side` of the road (+1 its left). */
void Wildlife::spawn(const WildRoad& r, double side) {
  const bool deer = rnd() < 0.7;
  std::vector<WildKind> kinds{deer ? "deer" : "boar"};
  if (deer) {
    if (rnd() < 0.55) kinds.emplace_back("deer");
    if (rnd() < 0.25) kinds.emplace_back("deer");
  } else {
    const int young = static_cast<int>(std::floor(rnd() * 4));
    for (int i = 0; i < young; i++) kinds.emplace_back("piglet");
  }
  const double nx = -r.dz * side;
  const double nz = r.dx * side;
  for (size_t k = 0; k < kinds.size(); k++) {
    const WildKind& kind = kinds[k];
    const double i = static_cast<double>(k);
    // (one behind the other along the road, a little apart; the young close to the sow)
    const double along = (deer ? 2.2 : 1.1) * i * (k % 2 ? 1 : -1) * 0.6;
    const double ox = r.x + r.dx * along;
    const double oz = r.z + r.dz * along;
    WildAnimal a;
    a.kind = kind;
    a.x = ox + nx * (r.half + 12);
    a.y = JS_NAN;
    a.z = oz + nz * (r.half + 12);
    a.yaw = 0;
    a.speed = 0;
    a.dist = 0;
    a.phase = "out";
    a.delay = deer ? i * (1.3 + rnd() * 0.8) : i * 0.25;
    a.pause = deer ? 0.5 + rnd() * 1.4 : 0.2 + rnd() * 0.4;
    a.ax = ox + nx * (r.half + 12);
    a.az = oz + nz * (r.half + 12);
    a.ex = ox + nx * (r.half + 1.5);
    a.ez = oz + nz * (r.half + 1.5);
    a.bx = ox - nx * (r.half + 24);
    a.bz = oz - nz * (r.half + 24);
    a.run = kind == "deer" ? 7.5 + rnd() * 2 : kind == "boar" ? 4.6 : 4.2;
    a.yaw = std::atan2(-(a.ex - a.ax), -(a.ez - a.az));
    a.y = host.heightAt(a.x, a.z);
    animals.push_back(std::move(a));
  }
}

void Wildlife::step(double dt, const PlayerState& player) {
  bool done = !animals.empty();
  bool far = false;
  for (WildAnimal& a : animals) {
    if (std::hypot(a.x - player.x, a.z - player.z) > GONE) far = true;
    if (a.delay > 0) {
      a.delay -= dt;
      done = false;
      continue;
    }
    double tx;
    double tz;
    double v;
    if (a.phase == "out") {
      tx = a.ex;
      tz = a.ez;
      v = a.kind == "deer" ? 2.2 : 1.6;
    } else if (a.phase == "wait") {
      a.pause -= dt;
      a.speed = 0;
      if (a.pause <= 0) a.phase = "cross";
      done = false;
      continue;
    } else {
      tx = a.bx;
      tz = a.bz;
      v = a.phase == "away" ? a.run * 0.6 : a.run;
    }
    // (speeding up and slowing down like an animal: quickly)
    a.speed += clamp(v - a.speed, -8 * dt, 6 * dt);
    const double dx = tx - a.x;
    const double dz = tz - a.z;
    const double d = std::hypot(dx, dz);
    const double go = std::min(d, a.speed * dt);
    if (d > 1e-6) {
      a.x += (dx / d) * go;
      a.z += (dz / d) * go;
      a.yaw = std::atan2(-dx, -dz);
    }
    a.dist += go;
    const double y = host.heightAt(a.x, a.z);
    if (!std::isnan(y)) a.y = y;
    if (d - go < 0.3) {
      if (a.phase == "out") {
        a.phase = "wait";
        a.speed = 0;
      } else if (a.phase == "cross") {
        // over: on, slower, a good way into the far side – then it is gone
        a.phase = "away";
        const double ux = a.bx - a.ex;
        const double uz = a.bz - a.ez;
        const double l0 = std::hypot(ux, uz);
        const double l = l0 != 0 ? l0 : 1;
        a.bx += (ux / l) * 30;
        a.bz += (uz / l) * 30;
      } else continue;
    }
    done = false;
  }
  if (done || far) {
    if (!animals.empty()) cooldown = COOLDOWN;
    animals.clear();
  }
}

/** The animals on the road or at its edge, as obstacles. */
void Wildlife::makeBoxes() {
  boxes.clear();
  size_t n = 0;
  for (const WildAnimal& a : animals) {
    if (a.delay > 0 || a.phase == "out" || a.phase == "away") continue;
    const auto [len, wide] = sizeOf(a.kind);
    if (n >= pool.size()) pool.push_back(PlayerState{.x = 0, .y = 0, .z = 0, .yaw = 0, .speed = 0, .length = len, .width = wide});
    PlayerState& b = pool[n++];
    b.x = a.x;
    b.y = std::isnan(a.y) ? 0 : a.y;
    b.z = a.z;
    b.yaw = a.yaw;
    b.speed = a.speed;
    b.length = len;
    b.width = wide;
    boxes.push_back(&b);
  }
}

std::function<bool(double x, double z, double reach, WildRoad& out)> worldRoads(World& world) {
  auto hit = std::make_shared<SurfaceHit>(newSurfaceHit());
  auto ps = std::make_shared<PolySample>(newPolySample());
  return [&world, hit, ps](double x, double z, double reach, WildRoad& out) {
    if (!world.nearestRoad(x, z, reach, 0, *hit) || !hit->edge) return false;
    const Edge& e = *hit->edge;
    samplePolyline(e.pl, hit->s, *ps);
    out.x = ps->x;
    out.z = ps->z;
    out.dx = ps->tx;
    out.dz = ps->tz;
    out.half = e.width / 2;
    const bool motorway = e.cls == MOTORWAY || e.cls == MOTORWAY_LINK || (e.flags & EF_DUAL) != 0;
    out.rural = !motorway && (e.flags & (EF_URBAN | EF_TUNNEL | EF_BRIDGE)) == 0;
    return true;
  };
}

}  // namespace fs

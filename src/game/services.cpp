// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/services.ts @ v1.7.7
#include "game/services.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "core/js.h"

namespace fs {

// ------------------------------------------------------------------ filling up

double Refuelling::nozzle() const {
  if (phase == "out") return std::min(1.0, t / NOZZLE_TIME);
  if (phase == "run") return 1;
  if (phase == "back") return std::max(0.0, 1 - t / NOZZLE_TIME);
  return 0;
}

void Refuelling::stop(const std::string& why) {
  if (phase == "out" || phase == "run") {
    reason = why;
    // (moved or started: the nozzle is out at once – in the game, it goes back as always)
    t = phase == "out" ? NOZZLE_TIME - t : 0;
    phase = "back";
  }
}

void Refuelling::step(double dt, FuelTank& tank, const StandState& s) {
  if (phase == "done") return;
  if (phase == "out" || phase == "run") {
    if (s.engineOn) stop("engine");
    else if (!s.standing) stop("moved");
  }
  t += dt;
  if (phase == "out" && t >= NOZZLE_TIME) {
    phase = "run";
    t = 0;
  } else if (phase == "run") {
    fill.step(dt, tank);
    if (fill.done) stop("full");
  } else if (phase == "back" && t >= NOZZLE_TIME) {
    phase = "done";
  }
}

// ------------------------------------------------------------------ washing

Washing::Washing(double dirt) : dirt0(std::max(0.0, std::min(1.0, dirt))) {}

std::string Washing::stepId() const { return step < static_cast<int>(WASH_STEPS.size()) ? WASH_STEPS[static_cast<size_t>(step)].id : ""; }

double Washing::at() const {
  if (step >= static_cast<int>(WASH_STEPS.size())) return 1;
  const WashStep& s = WASH_STEPS[static_cast<size_t>(step)];
  const double u = std::min(1.0, t / s.time);
  const double e = u * u * (3 - 2 * u);
  return s.from + (s.to - s.from) * e;
}

double Washing::share() const {
  double all = 0;
  double done = 0;
  for (int i = 0; i < static_cast<int>(WASH_STEPS.size()); i++) {
    const WashStep& s = WASH_STEPS[static_cast<size_t>(i)];
    all += s.time;
    if (i < step) done += s.time;
    else if (i == step) done += std::min(t, s.time);
  }
  return done / all;
}

double Washing::dirt() const {
  double left = 1;
  for (int i = 0; i < static_cast<int>(WASH_STEPS.size()); i++) {
    const WashStep& s = WASH_STEPS[static_cast<size_t>(i)];
    const double u = i < step ? 1 : i == step ? std::min(1.0, t / s.time) : 0;
    left -= s.clean * u;
  }
  return dirt0 * std::max(0.0, left);
}

void Washing::update(double dt, const StandState& s) {
  if (done()) return;
  if (!s.standing) {
    aborted = true;
    return;
  }
  t += dt;
  const WashStep& cur = WASH_STEPS[static_cast<size_t>(step)];
  if (t >= cur.time) {
    t -= cur.time;
    step++;
    if (step >= static_cast<int>(WASH_STEPS.size())) t = 0;
  }
}

// ------------------------------------------------------------------ dirt

namespace {
double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
}  // namespace

double dirtGrowth(double metres, double wet, double offRoad, double snow) {
  const double km = std::max(0.0, metres) / 1000;
  return km * (0.012 + 0.09 * clamp01(wet) + 0.06 * clamp01(snow) + 1.6 * clamp01(offRoad));
}

// ------------------------------------------------------------------ where something is, seen from the driver's seat

int bearingSector(double x, double z, double yaw, double tx, double tz) {
  const double fx = -std::sin(yaw);
  const double fz = -std::cos(yaw);
  const double dx = tx - x;
  const double dz = tz - z;
  const double ahead = dx * fx + dz * fz;
  // (right of the heading: the heading turned a quarter clockwise seen from above)
  const double right = dx * -fz + dz * fx;
  const double a = std::atan2(right, ahead);
  return static_cast<int>(jsRound(a / (std::numbers::pi / 4)) + 8) % 8;
}

// ------------------------------------------------------------------ the load of a truck

const std::vector<std::string>& LOADED_KINDS() {
  static const auto* k = new std::vector<std::string>{"lightTruck", "truckSolo"};
  return *k;
}

BoxInside boxInside(double wheelbase, double width) {
  const double axle = wheelbase / 2;
  const double hw = width / 2;
  const double front = -(axle + 1.4) + 2.0;
  const double rear = axle + 2.1;
  // (render/boxbody.h: walls 5 cm, the frame round the door 8 cm, the floor 3 cm over the box's bottom)
  return {-hw + 0.05, hw - 0.05, 1.15, front + 0.05, rear - 0.08};
}

LoadPlan planLoad(double wheelbase, double width, double maxLoad, const std::function<double()>& rand) {
  const int n = 4 + static_cast<int>(std::min(2.0, std::floor(rand() * 3)));
  std::vector<PalletPlan> pallets;
  double mass = 0;
  for (int i = 0; i < n; i++) {
    const double m = std::min(std::floor(maxLoad / n), jsRound(120 + rand() * 280));
    const int kind = static_cast<int>(std::floor(rand() * 4));
    pallets.push_back({0.8 + rand() * 0.55, kind, m});
    mass += m;
  }
  const BoxInside inside = boxInside(wheelbase, width);
  const double rows = std::ceil(n / 2.0);
  const double len = rows * 1.25;
  double sum = 0;
  for (const PalletPlan& p : pallets) sum += p.height;
  const double height = sum / n + 0.144;
  const Payload payload{.mass = mass, .z = inside.front + 0.1 + len / 2, .y = inside.floor + height / 2, .len = len, .wid = 1.7, .hgt = height};
  return {std::move(pallets), payload};
}

// ------------------------------------------------------------------ the depot

std::optional<DepotSpot> findDepot(double x, double z, double yaw, double front, double rear, const DepotProbe& probe) {
  const double fx = -std::sin(yaw);
  const double fz = -std::cos(yaw);
  const double rx = -fz;
  const double rz = fx;
  for (double s : {0, 15, -15, 30, -30, 45, -45, 60}) {
    for (double side : {1, -1}) {
      const double bx = x + fx * s;
      const double bz = z + fz * s;
      // the road's edge on that side: the first of three metres off the road
      int edge = -1;
      for (int t = 2; t <= 22; t++) {
        bool off = true;
        for (int k = 0; k < 3 && off; k++)
          if (probe.road(bx + rx * side * (t + k), bz + rz * side * (t + k))) off = false;
        if (off) {
          edge = t;
          break;
        }
      }
      if (edge < 0) continue;
      // the truck, facing the road: its front 2 m beyond the edge
      const double nose = edge + 2;
      const double ox = bx + rx * side * (nose + front);
      const double oz = bz + rz * side * (nose + front);
      const double tyaw = std::atan2(rx * side, rz * side);
      // (along the truck, away from the road; across it)
      const double ax = rx * side;
      const double az = rz * side;
      const double wall = nose + front + rear + DEPOT.gap;
      const double y0 = probe.ground(ox, oz);
      if (std::isnan(y0)) continue;
      bool ok = true;
      // off the road, and the ground not too far above or below
      for (double d = nose; d <= wall + DEPOT.dep && ok; d += 2) {
        const double half = d < wall ? 2 : DEPOT.wid / 2;
        for (double w = -half; w <= half && ok; w += 2) {
          const double px = bx + ax * d - az * w;
          const double pz = bz + az * d + ax * w;
          if (probe.road(px, pz)) ok = false;
          const double g = probe.ground(px, pz);
          if (std::isnan(g) || std::abs(g - y0) > 1.1) ok = false;
        }
      }
      if (!ok) continue;
      // not steep under the truck
      const double gf = probe.ground(bx + ax * nose, bz + az * nose);
      const double gr = probe.ground(bx + ax * (wall - DEPOT.gap), bz + az * (wall - DEPOT.gap));
      if (std::abs(gf - gr) > 0.07 * (front + rear)) continue;
      // nothing in the way: the truck itself, the way out to the road, and the warehouse (covered by trucks standing across it)
      if (probe.blocked(ox, oz, tyaw) || probe.blocked(ox + ax * -2, oz + az * -2, tyaw)) continue;
      for (double dd : {1.5, 4.0, 6.5, 9.0}) {
        for (double w : {-3.5, 3.5}) {
          const double d = wall + dd;
          if (probe.blocked(bx + ax * d - az * w, bz + az * d + ax * w, tyaw + std::numbers::pi / 2)) ok = false;
        }
      }
      if (ok) return DepotSpot{ox, oz, tyaw};
    }
  }
  return std::nullopt;
}

}  // namespace fs

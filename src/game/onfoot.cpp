// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/onfoot.ts @ v1.7.7
#include "game/onfoot.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "engine/geometries.h"

namespace fs {

const std::vector<CheckId> CHECK_ORDER = {"tyres", "front", "rear", "coupling"};

namespace {

constexpr double RADIUS = 0.28;
/** how long something has to be looked at (s) */
constexpr double LOOK = 0.45;

struct XZ {
  double x = 0;
  double z = 0;
};

/** A point `ahead` metres ahead of and `right` metres to the right of a unit's origin. */
XZ at(double ux, double uz, double uyaw, double ahead, double right) {
  const double s = std::sin(uyaw);
  const double c = std::cos(uyaw);
  return {ux - s * ahead + c * right, uz - c * ahead - s * right};
}

/** Inside a unit's outline (grown by `margin`)? */
bool inside(const WalkUnit& u, double x, double z, double margin) {
  const double s = std::sin(u.yaw);
  const double c = std::cos(u.yaw);
  const double dx = x - u.x;
  const double dz = z - u.z;
  const double ahead = -dx * s - dz * c;
  const double right = dx * c - dz * s;
  return ahead < u.front + margin && ahead > -u.rear - margin && std::abs(right) < u.hw + margin;
}

}  // namespace

std::vector<CheckSpot> checkSpots(const WalkVehicle& v) {
  std::vector<CheckSpot> out;
  auto add = [&](const CheckId& id, const WalkUnit& u, double ahead, double right, double y, double reach, double ringAhead, double ringRight) {
    const XZ p = at(u.x, u.z, u.yaw, ahead, right);
    const XZ r = at(u.x, u.z, u.yaw, ringAhead, ringRight);
    out.push_back({id, p.x, v.y + y, p.z, r.x, r.z, reach, 0, false});
  };
  for (size_t i = 0; i < v.units.size(); i++) {
    const WalkUnit& u = v.units[i];
    // (a tandem axle is looked at as one: the tyres stand close together)
    std::vector<double> axles;
    for (const double a : u.axles)
      if (std::none_of(axles.begin(), axles.end(), [a](double b) { return std::abs(b - a) < 1.6; })) axles.push_back(a);
    for (const double a : axles) {
      std::vector<double> group;
      for (const double b : u.axles)
        if (std::abs(b - a) < 1.6) group.push_back(b);
      double mid = 0;
      for (const double b : group) mid = mid + b / static_cast<double>(group.size());
      for (const double side : {-1.0, 1.0}) add("tyres", u, mid, side * u.track, 0.35, 2.3, mid, side * (u.hw + 0.75));
    }
    if (i == 0) add("front", u, u.front, 0, 0.7, 2.8, u.front + 0.9, 0);
    if (i == v.units.size() - 1) add("rear", u, -u.rear, 0, 0.85, 2.8, -u.rear - 0.9, 0);
  }
  if (v.hitch && v.units.size() > 1) {
    // the coupling is between the two: seen from beside it
    const WalkUnit& u = v.units[0];
    const XZ ring = at(v.hitch->x, v.hitch->z, u.yaw, 0, -(u.hw + 0.7));
    out.push_back({"coupling", v.hitch->x, v.hitch->y, v.hitch->z, ring.x, ring.z, 3.4, 0, false});
  }
  return out;
}

OnFoot::OnFoot(std::optional<WalkVehicle> vehicle_, std::optional<Start> start) : vehicle(std::move(vehicle_)) {
  if (vehicle) spots = checkSpots(*vehicle);
  if (vehicle) {
    const Spot p = doorSpot();
    x = p.x;
    z = p.z;
    y = vehicle->y;
    yaw = vehicle->units[0].yaw + 0.35;
  } else {
    x = start ? start->x : 0;
    y = start ? start->y : 0;
    z = start ? start->z : 0;
    yaw = start ? start->yaw : 0;
  }
}

OnFoot::Spot OnFoot::doorSpot() const {
  if (!vehicle) return {x, z};
  const WalkUnit& u = vehicle->units[0];
  const XZ p = at(u.x, u.z, u.yaw, vehicle->door, vehicle->doorSide.value_or(-1) * (u.hw + 0.6));
  return {p.x, p.z};
}

bool OnFoot::atDoor() const {
  if (!vehicle) return false;
  const Spot p = doorSpot();
  return std::hypot(x - p.x, z - p.z) < 1.3;
}

std::vector<OnFoot::Progress> OnFoot::progress() const {
  std::vector<Progress> out;
  for (const CheckId& id : CHECK_ORDER) {
    int of = 0;
    int done = 0;
    for (const CheckSpot& s : spots) {
      if (s.id != id) continue;
      of++;
      if (s.done) done++;
    }
    if (of > 0) out.push_back({id, done, of});
  }
  return out;
}

bool OnFoot::allChecked() const {
  return std::all_of(spots.begin(), spots.end(), [](const CheckSpot& s) { return s.done; });
}

std::vector<CheckId> OnFoot::missing() const {
  std::vector<CheckId> out;
  for (const Progress& p : progress())
    if (p.done < p.of) out.push_back(p.id);
  return out;
}

OnFoot::Result OnFoot::update(double dt, const WalkMove& move, const WalkLook& look, const std::function<double(double, double, double)>& ground,
                              const std::function<bool(double, double, double, double)>& blocked) {
  yaw += look.yaw + move.turn * 1.9 * dt;
  pitch = std::max(-1.3, std::min(1.1, pitch + look.pitch));
  const double sp = move.run ? runSpeed : WALK;
  const double x0 = x;
  const double z0 = z;
  const double s = std::sin(yaw);
  const double c = std::cos(yaw);
  // forwards = (−sin, −cos), to the right = (cos, −sin)
  // (forwards and sideways at once: no quicker than straight on)
  const double side = move.side.value_or(0);
  const double norm = std::max(1.0, std::hypot(move.fwd, side));
  const double ahead = (move.fwd / norm) * sp * dt + look.fwd * 4;
  const double right = (side / norm) * sp * dt + look.right * 4;
  const double dx = -s * ahead + c * right;
  const double dz = -c * ahead - s * right;
  bool step = false;
  if (dx != 0 || dz != 0) {
    // whole step, else along whatever is in the way (one axis at a time)
    const std::array<std::array<double, 2>, 3> tries = {{{dx, dz}, {dx, 0}, {0, dz}}};
    for (const auto& [mx, mz] : tries) {
      if (mx == 0 && mz == 0) continue;
      const double nx = x + mx;
      const double nz = z + mz;
      if (vehicle && std::any_of(vehicle->units.begin(), vehicle->units.end(), [&](const WalkUnit& u) { return inside(u, nx, nz, RADIUS); })) continue;
      const double ny = ground(nx, nz, y + 0.6);
      // (a step up of a kerb, not up a wall)
      if (!std::isfinite(ny) || ny > y + 0.45 || blocked(nx, ny, nz, RADIUS)) continue;
      // (as far from the vehicle as one likes – the hint only says how far it is now)
      const WalkUnit* u = vehicle && !vehicle->units.empty() ? &vehicle->units[0] : nullptr;
      tooFar = u && std::hypot(nx - u->x, nz - u->z) > TETHER;
      walked += std::hypot(mx, mz);
      x = nx;
      z = nz;
      y = ny;
      break;
    }
    if (walked > (move.run ? 1.1 : 0.72)) {
      walked = 0;
      step = true;
    }
  }
  const double moved = std::hypot(x - x0, z - z0);
  distance += moved;
  speed = dt > 0 ? moved / dt : 0;
  running = move.run && speed > WALK * 1.2;
  // --- what is looked at ---
  const CheckSpot* checked = nullptr;
  const double cp = std::cos(pitch);
  const double fx = -s * cp;
  const double fy = std::sin(pitch);
  const double fz = -c * cp;
  const double ey = y + EYE;
  for (CheckSpot& sp2 : spots) {
    if (sp2.done) continue;
    const double hx = sp2.x - x;
    const double hz = sp2.z - z;
    const double hd = std::hypot(hx, hz);
    const double vy = sp2.y - ey;
    const double d = std::hypot(hd, vy);
    const double facing = (hx * fx + vy * fy + hz * fz) / std::max(d, 1e-6);
    if (hd < sp2.reach && facing > 0.82) {
      sp2.seen += dt;
      if (sp2.seen >= LOOK) {
        sp2.done = true;
        checked = &sp2;
      }
    } else sp2.seen = std::max(0.0, sp2.seen - dt);
  }
  return {step, checked};
}

CheckMarkers::CheckMarkers() {
  open = basic({.color = Color(0xffb000), .transparent = true, .opacity = 0.9, .depthWrite = false});
  done = basic({.color = Color(0x2fd06a), .transparent = true, .opacity = 0.9, .depthWrite = false});
  ring = ringGeometry(0.2, 0.3, 28);
  ring->rotateX(-PI / 2);
  group->name = "check markers";
  group->visible = false;
}

void CheckMarkers::show(const std::vector<CheckSpot>& spots, const std::function<double(double, double)>& ground) {
  hide();
  for (const CheckSpot& s : spots) {
    auto m = make<Mesh>(ring, s.done ? done : open);
    const double g = ground(s.rx, s.rz);
    m->position.set(s.rx, (std::isfinite(g) ? g : s.y) + 0.035, s.rz);
    m->renderOrder = 2;
    meshes.push_back(m);
    group->add(m);
  }
  group->visible = true;
}

void CheckMarkers::update(const std::vector<CheckSpot>& spots) {
  for (size_t i = 0; i < spots.size(); i++) {
    if (i < meshes.size()) meshes[i]->material = spots[i].done ? done : open;
  }
}

void CheckMarkers::hide() {
  for (const auto& m : meshes) group->remove(m);
  meshes.clear();
  group->visible = false;
}

}  // namespace fs

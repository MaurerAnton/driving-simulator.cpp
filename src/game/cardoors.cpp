// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/cardoors.ts @ v1.7.7
#include "game/cardoors.h"

#include <cmath>
#include <tuple>

#include "core/js.h"

namespace fs {

namespace {

/** seconds a part takes from shut to wide open */
double swing(const OpeningId& id) {
  if (id == "fl" || id == "fr" || id == "rl" || id == "rr") return 0.75;
  if (id == "bonnet" || id == "boot") return 1.3;
  if (id == "body") return 2.4;
  return 1.8;   // trailer
}

double* find(std::vector<std::pair<OpeningId, double>>& m, const OpeningId& id) {
  for (auto& [k, v] : m)
    if (k == id) return &v;
  return nullptr;
}

const double* find(const std::vector<std::pair<OpeningId, double>>& m, const OpeningId& id) {
  for (const auto& [k, v] : m)
    if (k == id) return &v;
  return nullptr;
}

void put(std::vector<std::pair<OpeningId, double>>& m, const OpeningId& id, double v) {
  if (double* p = find(m, id)) *p = v;
  else m.emplace_back(id, v);
}

/** The point `ahead` metres ahead of and `right` metres to the right of a unit's origin. */
std::pair<double, double> at(const SpotUnit& u, double ahead, double right) {
  const double s = std::sin(u.yaw);
  const double c = std::cos(u.yaw);
  return {u.x - s * ahead + c * right, u.z - c * ahead - s * right};
}

}  // namespace

bool isDoor(const OpeningId& id) { return id == "fl" || id == "fr" || id == "rl" || id == "rr"; }

SeatId seatOfDoor(const DoorId& door) { return door == "fl" ? "driver" : door == "fr" ? "front" : door == "rl" ? "rearLeft" : "rearRight"; }

DoorId doorOfSeat(const SeatId& seat) { return seat == "driver" ? "fl" : seat == "front" ? "fr" : seat == "rearLeft" ? "rl" : "rr"; }

std::array<double, 3> seatEye(const SeatId& seat) {
  if (seat == "front") return {0.36, 1.2, 0.2};
  if (seat == "rearLeft") return {-0.36, 1.16, 1.2};
  if (seat == "rearRight") return {0.36, 1.16, 1.2};
  return {-0.36, 1.2, 0.2};
}

double Openings::get(const OpeningId& id) const {
  const double* v = find(value_, id);
  return v ? *v : 0;
}

bool Openings::wanted(const OpeningId& id) const {
  const double* t = find(target_, id);
  return (t ? *t : 0) > 0.5;
}

void Openings::set(const OpeningId& id, bool open) { put(target_, id, open ? 1 : 0); }

bool Openings::toggle(const OpeningId& id) {
  const bool open = !wanted(id);
  set(id, open);
  return open;
}

bool Openings::anyOpen() const {
  for (const auto& [k, v] : value_)
    if (v > 0) return true;
  for (const auto& [k, t] : target_)
    if (t > 0) return true;
  return false;
}

std::vector<OpeningEvent>& Openings::step(double dt, std::vector<OpeningEvent>& events) {
  for (const auto& [id, t] : target_) {
    const double* pv = find(value_, id);
    const double v = pv ? *pv : 0;
    if (v == t) continue;
    const double k = dt / swing(id);
    const double n = t > v ? jsMin(t, v + k) : jsMax(t, v - k);
    if (v == 0 && n > 0) events.push_back({id, "opening"});
    if (n == 0) events.push_back({id, "shut"});
    put(value_, id, n);
  }
  return events;
}

void Openings::reset() {
  value_.clear();
  target_.clear();
}

std::vector<OpeningSpot> carSpots(const SpotUnit& u, const CarSpotEdges& edges) {
  const double side = u.hw + 0.6;
  const std::tuple<OpeningId, double, double> list[] = {
      {"fl", edges.frontAhead, -side},
      {"fr", edges.frontAhead, side},
      {"rl", -(edges.rearDoorRear - 0.2), -side},
      {"rr", -(edges.rearDoorRear - 0.2), side},
      {"bonnet", -edges.nose + 0.75, 0},
      {"boot", -edges.tail - 0.75, 0},
  };
  std::vector<OpeningSpot> out;
  for (const auto& [id, ahead, right] : list) {
    const auto [x, z] = at(u, ahead, right);
    out.push_back({id, x, z});
  }
  return out;
}

OpeningSpot rearSpot(const OpeningId& id, const SpotUnit& u) {
  const auto [x, z] = at(u, -u.rear - 0.8, 0);
  return {id, x, z};
}

std::optional<OpeningSpot> nearestSpot(const std::vector<OpeningSpot>& spots, double x, double z, double reach) {
  std::optional<OpeningSpot> best;
  double bestD = reach;
  for (const OpeningSpot& s : spots) {
    const double d = std::hypot(s.x - x, s.z - z);
    if (d < bestD) {
      bestD = d;
      best = s;
    }
  }
  return best;
}

std::vector<FootAct> footActs(const OpeningId& id, const Openings& openings, bool canEnter) {
  std::vector<FootAct> out = {openings.wanted(id) ? "close" : "open"};
  if (isDoor(id) && canEnter) out.push_back("enter");
  return out;
}

Boarding::Boarding(const std::string& way_, const DoorId& door_) : way(way_), door(door_), seat(seatOfDoor(door_)) {}

double Boarding::blend() const {
  if (phase == "open") return 0;
  if (phase != "move") return 1;
  const double k = jsMin(1, t_ / MOVE_TIME);
  return k * k * (3 - 2 * k);
}

BoardStep Boarding::step(double dt, Openings& o) {
  BoardStep r;
  if (phase == "open") {
    o.set(door, true);
    // (wide enough to pass a little before it is fully open)
    if (o.get(door) >= 0.85) {
      phase = "move";
      t_ = 0;
      r.start = true;
    }
  } else if (phase == "move") {
    t_ += dt;
    if (t_ >= MOVE_TIME) {
      phase = "close";
      r.arrive = true;
      o.set(door, false);
    }
  } else if (phase == "close") {
    o.set(door, false);
    if (o.get(door) <= 0) {
      phase = "done";
      r.done = true;
    }
  }
  return r;
}

}  // namespace fs

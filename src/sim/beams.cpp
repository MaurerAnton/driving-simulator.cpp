// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/beams.ts @ v1.7.7
#include "sim/beams.h"

#include <algorithm>
#include <cmath>

namespace fs {

namespace {

constexpr double INF = std::numeric_limits<double>::infinity();

/** … a cyclist or somebody walking towards one? */
bool riderInBeam(const Relation& r, double range) { return r.facing < -0.3 && r.along > 2 && r.along < range && std::abs(r.side) < 3 + r.along * 0.15; }

/** the flasher's rhythm: two flashes of 0.3 s, a quarter of a second apart */
bool flashLit(double t) { return (t >= 0 && t < 0.3) || (t >= 0.55 && t < 0.85); }

}  // namespace

Relation& relate(double x, double z, double yaw, const BeamUser& o, Relation& out) {
  const double fx = -std::sin(yaw);
  const double fz = -std::cos(yaw);
  const double dx = o.x - x;
  const double dz = o.z - z;
  out.along = dx * fx + dz * fz;
  // (to the left: the forward turned a quarter to the left, (−cos yaw, sin yaw))
  out.side = dx * -std::cos(yaw) + dz * std::sin(yaw);
  out.facing = std::cos(o.yaw - yaw);
  return out;
}

bool inOncomingBeam(const Relation& r, double range) { return r.facing < -0.5 && r.along > 3 && r.along < range && std::abs(r.side) < 4 + r.along * 0.2; }

bool inBeamAhead(const Relation& r, double range) { return r.facing > 0.6 && r.along > 3 && r.along < range && std::abs(r.side) < 2.5 + r.along * 0.06; }

double darkness(double sunElevation) {
  const double t = std::min(1.0, std::max(0.0, (sunElevation - -4) / 10));
  return 1 - t * t * (3 - 2 * t);
}

// ---------------------------------------------------------------------------- the player's lights

void BeamWatch::reset() {
  tip = "";
  dazzling = -1;
  assistHigh = false;
  oncoming = INF;
  tOncoming_ = tAhead_ = tRider_ = tLit_ = tDark_ = 0;
}

std::vector<BeamFault> BeamWatch::update(double dt, const BeamScene& s) {
  std::vector<BeamFault> out;
  tip = "";
  dazzling = -1;
  const bool moving = std::abs(s.speed) > 2;

  // --- lights at all: in the dark, and in thick fog, by day too (§ 17 Abs. 1, Abs. 3) ---
  const bool need = s.dark || s.poorSight;
  if (need && !s.low && moving) {
    tDark_ += dt;
    tip = "beam.tipLightsOn";
    if (tDark_ > BEAM.GRACE_DARK) out.push_back({"noLights", "major", 60});
  } else
    tDark_ = 0;

  // --- who is in the main beam's reach ---
  double near = INF;
  int64_t oncomingId = -1;
  double ahead = INF;
  double rider = INF;
  if (s.dark) {
    Relation& r = r_;
    for (const BeamUser& c : s.cars) {
      relate(s.x, s.z, s.yaw, c, r);
      if (inOncomingBeam(r, BEAM.ONCOMING_TIP)) {
        if (r.along < near && (!s.sees || s.sees(s.x, s.z, c.x, c.z))) {
          near = r.along;
          oncomingId = c.id;
        }
      } else if (inBeamAhead(r, BEAM.AHEAD_TIP) && r.along < ahead)
        ahead = r.along;
    }
    for (const BeamUser& p : s.riders) {
      relate(s.x, s.z, s.yaw, p, r);
      if (riderInBeam(r, BEAM.RIDER_TIP) && r.along < rider) rider = r.along;
    }
  }
  oncoming = near;
  assistHigh = s.dark && s.low && !s.lit && s.speed > BEAM.ASSIST_SPEED && near == INF && ahead == INF && rider == INF;

  if (!s.high || !s.dark) {
    tOncoming_ = tAhead_ = tRider_ = tLit_ = 0;
    return out;
  }
  // --- the main beam on: dip it in time (§ 17 Abs. 2) ---
  if (near < BEAM.ONCOMING_FLASH) dazzling = oncomingId;
  tOncoming_ = near < BEAM.ONCOMING_FAULT ? tOncoming_ + dt : 0;
  tAhead_ = ahead < BEAM.AHEAD_FAULT ? tAhead_ + dt : 0;
  tRider_ = rider < BEAM.RIDER_FAULT ? tRider_ + dt : 0;
  tLit_ = s.lit && moving ? tLit_ + dt : 0;
  if (tOncoming_ > BEAM.GRACE_ONCOMING) out.push_back({"dazzleOncoming", "minor", 20});
  if (tAhead_ > BEAM.GRACE_AHEAD) out.push_back({"dazzleAhead", "minor", 20});
  if (tRider_ > BEAM.GRACE_RIDER) out.push_back({"dazzleRider", "minor", 20});
  if (tLit_ > BEAM.GRACE_LIT) out.push_back({"beamLit", "hint", 60});
  // (the tip: the most urgent first)
  if (near < BEAM.ONCOMING_TIP) tip = "beam.tipOncoming";
  else if (rider < BEAM.RIDER_TIP) tip = "beam.tipRider";
  else if (ahead < BEAM.AHEAD_TIP) tip = "beam.tipAhead";
  else if (s.lit) tip = "beam.tipLit";
  return out;
}

// ---------------------------------------------------------------------------- the traffic's main beam

void TrafficBeams::reset() {
  m_.clear();
  glare = 0;
}

bool TrafficBeams::high(int64_t id) const {
  const auto it = m_.find(id);
  return it != m_.end() && (it->second.high || flashLit(time_ - it->second.flash));
}

bool TrafficBeams::flashing(int64_t id) const {
  const auto it = m_.find(id);
  return it != m_.end() && time_ - it->second.flash < 0.85;
}

double TrafficBeams::dipDistance(int64_t id) { return id % 8 == 3 ? 170 : 340 + (id % 5) * 20; }

void TrafficBeams::update(double dt, const std::vector<TrafficBeamCar>& cars, const BeamPlayer* player, int64_t dazzling, bool dark, const BeamSees& sees) {
  time_ += dt;
  if (!dark) {
    if (!m_.empty()) m_.clear();
    glare = 0;
    return;
  }
  const double t = time_;
  // the player dazzles somebody: after a second that one flashes back (once in a while)
  for (auto& [id, b] : m_) {
    if (id != dazzling) b.dazzled = 0;
    else {
      b.dazzled += dt;
      if (b.dazzled > 1 && t - b.flash > 12) b.flash = t;
    }
  }
  if (t >= nextTick_) {
    nextTick_ = t + 0.2;
    const int st = ++stamp_;
    Relation& r = r_;
    for (const TrafficBeamCar& c : cars) {
      auto it = m_.find(c.id);
      if (it == m_.end()) {
        CarBeam nb;
        nb.clearSince = t;
        nb.seen = st;
        if (c.id == dazzling) nb.dazzled = dt;
        it = m_.emplace(c.id, nb).first;
      }
      CarBeam& b = it->second;
      b.seen = st;
      if (!c.rural || c.v < 8) {
        b.high = false;
        b.clearSince = t;
        continue;
      }
      const double dip = dipDistance(c.id);
      bool blocked = false;
      for (const TrafficBeamCar& o : cars) {
        if (&o == &c) continue;
        relate(c.x, c.z, c.yaw, o, r);
        if (inOncomingBeam(r, dip) || inBeamAhead(r, 150)) {
          blocked = true;
          break;
        }
      }
      if (!blocked && player) {
        relate(c.x, c.z, c.yaw, *player, r);
        blocked = (inOncomingBeam(r, dip) && (!sees || sees(c.x, c.z, player->x, player->z))) || inBeamAhead(r, 150);
      }
      if (blocked) {
        b.high = false;
        b.clearSince = t;
      } else if (t - b.clearSince > 2)
        b.high = true;
    }
    for (auto it = m_.begin(); it != m_.end();) {
      if (it->second.seen != st) it = m_.erase(it);
      else ++it;
    }
  }
  // the player dazzled by a beam coming towards him
  double g = 0;
  if (player) {
    Relation& r = r_;
    for (const TrafficBeamCar& c : cars) {
      if (!high(c.id)) continue;
      relate(player->x, player->z, player->yaw, c, r);
      if (r.facing > -0.7 || r.along < 5 || r.along > 300 || std::abs(r.side) > 4 + r.along * 0.15) continue;
      const double k = (1 - r.along / 300) * (flashing(c.id) ? 1 : 0.8);
      if (k > g && (!sees || sees(player->x, player->z, c.x, c.z))) g = k;
    }
  }
  glare = g;
}

}  // namespace fs

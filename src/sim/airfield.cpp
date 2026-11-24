// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/airfield.ts @ v1.7.7
#include "sim/airfield.h"

#include <algorithm>
#include <cmath>

#include "core/geom.h"
#include "core/js.h"

namespace fs {

bool paved(const std::string& surface) {
  for (const char* w : {"grass", "ground", "dirt", "earth", "gravel", "sand", "unpaved", "compacted"})
    if (surface.find(w) != std::string::npos) return false;
  return true;
}

AirfieldGround::AirfieldGround(Ground& base, const std::vector<AirfieldJson>& fields) : base(base) {
  for (const auto& f : fields) {
    for (const auto& r : f.runways) addStrip(r.x1, r.z1, r.x2, r.z2, r.w, paved(r.s));
    for (const auto& t : f.taxiways)
      for (size_t i = 0; i + 3 < t.p.size(); i += 2) addStrip(t.p[i], t.p[i + 1], t.p[i + 2], t.p[i + 3], t.w, paved(t.s));
    for (const auto& a : f.aprons)
      if (paved(a.s) && a.p.size() >= 6) aprons.push_back(a.p);
  }
}

void AirfieldGround::addStrip(double x1, double z1, double x2, double z2, double w, bool isPaved) {
  const double len = std::hypot(x2 - x1, z2 - z1);
  if (len < 1) return;
  strips.push_back({x1, z1, (x2 - x1) / len, (z2 - z1) / len, len, w / 2, isPaved});
}

int AirfieldGround::surfaceAt(double x, double z) const {
  bool grass = false;
  for (const auto& s : strips) {
    const double dx = x - s.x1;
    const double dz = z - s.z1;
    const double along = dx * s.ux + dz * s.uz;
    if (along < -s.hw || along > s.len + s.hw) continue;
    if (std::abs(-dx * s.uz + dz * s.ux) > s.hw) continue;
    if (s.paved) return 1;
    grass = true;
  }
  for (const auto& a : aprons)
    if (pointInRing(x, z, a)) return 1;
  return grass ? 0 : -1;
}

void AirfieldGround::sample(double x, double z, double yRef, GroundSample& out) {
  base.sample(x, z, yRef, out);
  const int s = surfaceAt(x, z);
  // (the runways, taxiways and aprons are drawn a few centimetres above the ground: the wheels roll on them)
  if (s >= 0) out.y += SURFACE_LIFT;
  if (s == 1) {
    out.grip = std::max(out.grip, 1.0);
    out.rolling = 1;
  } else if (s == 0) {
    out.grip = std::min(out.grip, 0.8);
    out.rolling = std::min(out.rolling, 2.2);
  }
}

RunwayStart runwayStart(const RunwayJson& r, bool fromSecond) {
  const double ax = fromSecond ? r.x2 : r.x1, az = fromSecond ? r.z2 : r.z1;
  const double bx = fromSecond ? r.x1 : r.x2, bz = fromSecond ? r.z1 : r.z2;
  double len = std::hypot(bx - ax, bz - az);
  if (len == 0 || std::isnan(len)) len = 1;
  const double ux = (bx - ax) / len;
  const double uz = (bz - az) / len;
  const double k = std::min(60.0, len * 0.1);
  return {ax + ux * k, az + uz * k, std::atan2(-ux, -uz)};
}

std::string runwayEnd(const RunwayJson& r, bool fromSecond) {
  std::vector<std::string> parts = split(r.ref, "/");
  for (auto& p : parts) p = trim(p);
  if (parts.size() == 2 && !parts[0].empty() && !parts[1].empty()) return fromSecond ? parts[1] : parts[0];
  const double ax = fromSecond ? r.x2 : r.x1, az = fromSecond ? r.z2 : r.z1;
  const double bx = fromSecond ? r.x1 : r.x2, bz = fromSecond ? r.z1 : r.z2;
  const double heading = jsMod((std::atan2(bx - ax, -(bz - az)) * 180) / M_PI + 360, 360);
  int n = static_cast<int>(jsRound(heading / 10));
  if (n == 0) n = 36;
  return padStart(std::to_string(n), 2, '0');
}

}  // namespace fs

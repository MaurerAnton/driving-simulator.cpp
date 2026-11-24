// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/spawnplace.ts @ v1.7.7
#include "game/spawnplace.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_set>

#include "core/js.h"

namespace fs {

namespace {
const double MATH_PI = std::numbers::pi;
}  // namespace

void LoadedArea::set(const std::string& key, const TileBox& box, bool loaded) {
  auto it = std::find_if(tiles_.begin(), tiles_.end(), [&](const auto& t) { return t.first == key; });
  if (loaded) {
    if (it != tiles_.end()) it->second = TileBox{box.cx, box.cz, box.size};
    else tiles_.emplace_back(key, TileBox{box.cx, box.cz, box.size});
  } else if (it != tiles_.end()) tiles_.erase(it);
  outlineCache_.reset();
}

void LoadedArea::clear() {
  tiles_.clear();
  outlineCache_.reset();
}

bool LoadedArea::has(double x, double z, double margin) const {
  if (margin <= 0) return on(x, z);
  return on(x, z) && on(x - margin, z) && on(x + margin, z) && on(x, z - margin) && on(x, z + margin);
}

bool LoadedArea::on(double x, double z) const {
  for (const auto& [key, t] : tiles_) {
    const double h = t.size / 2;
    if (x >= t.cx - h && x <= t.cx + h && z >= t.cz - h && z <= t.cz + h) return true;
  }
  return false;
}

const std::vector<float>& LoadedArea::outline() {
  if (outlineCache_) return *outlineCache_;
  std::vector<TileBox> boxes;
  boxes.reserve(tiles_.size());
  for (const auto& [key, t] : tiles_) boxes.push_back(t);
  outlineCache_ = tileOutline(boxes);
  return *outlineCache_;
}

std::vector<float> tileOutline(const std::vector<TileBox>& tiles) {
  const auto key = [](double x, double z) { return jsStr(jsRound(x * 10)) + "|" + jsStr(jsRound(z * 10)); };
  std::unordered_set<std::string> at;
  for (const TileBox& t : tiles) at.insert(key(t.cx, t.cz));
  std::vector<float> out;
  const auto push = [&out](double a, double b, double c, double d) {
    out.push_back(static_cast<float>(a));
    out.push_back(static_cast<float>(b));
    out.push_back(static_cast<float>(c));
    out.push_back(static_cast<float>(d));
  };
  for (const TileBox& t : tiles) {
    const double h = t.size / 2;
    const double x0 = t.cx - h;
    const double x1 = t.cx + h;
    const double z0 = t.cz - h;
    const double z1 = t.cz + h;
    if (!at.count(key(t.cx, t.cz - t.size))) push(x0, z0, x1, z0);
    if (!at.count(key(t.cx + t.size, t.cz))) push(x1, z0, x1, z1);
    if (!at.count(key(t.cx, t.cz + t.size))) push(x1, z1, x0, z1);
    if (!at.count(key(t.cx - t.size, t.cz))) push(x0, z1, x0, z0);
  }
  return out;
}

void Footprints::setRings(const std::string& key, std::vector<double> pts, std::vector<uint32_t> counts) {
  const double INF = std::numeric_limits<double>::infinity();
  TileRings t;
  t.start.resize(counts.size());
  t.box.resize(counts.size() * 4);
  t.all[0] = INF;
  t.all[1] = INF;
  t.all[2] = -INF;
  t.all[3] = -INF;
  size_t o = 0;
  for (size_t r = 0; r < counts.size(); r++) {
    t.start[r] = static_cast<uint32_t>(o);
    double x0 = INF;
    double z0 = INF;
    double x1 = -INF;
    double z1 = -INF;
    for (size_t i = 0; i < counts[r]; i++) {
      // (a ring that runs past the end of the points: those corners are nothing, as in the original – NaN)
      const double x = o + i * 2 < pts.size() ? pts[o + i * 2] : JS_NAN;
      const double z = o + i * 2 + 1 < pts.size() ? pts[o + i * 2 + 1] : JS_NAN;
      x0 = jsMin(x0, x);
      x1 = jsMax(x1, x);
      z0 = jsMin(z0, z);
      z1 = jsMax(z1, z);
    }
    t.box[r * 4] = static_cast<float>(x0);
    t.box[r * 4 + 1] = static_cast<float>(z0);
    t.box[r * 4 + 2] = static_cast<float>(x1);
    t.box[r * 4 + 3] = static_cast<float>(z1);
    t.all[0] = jsMin(t.all[0], x0);
    t.all[1] = jsMin(t.all[1], z0);
    t.all[2] = jsMax(t.all[2], x1);
    t.all[3] = jsMax(t.all[3], z1);
    o += static_cast<size_t>(counts[r]) * 2;
  }
  // (rings past the end of the points are never inside anything: they are cut off here, where the original reads nothing)
  if (o > pts.size()) pts.resize(o, JS_NAN);
  t.pts = std::move(pts);
  t.counts = std::move(counts);
  tiles_[key] = std::move(t);
}

bool Footprints::inside(double x, double z) const {
  for (const auto& [key, t] : tiles_) {
    if (x < t.all[0] || x > t.all[2] || z < t.all[1] || z > t.all[3]) continue;
    for (size_t r = 0; r < t.counts.size(); r++) {
      const size_t b = r * 4;
      if (x < t.box[b] || x > t.box[b + 2] || z < t.box[b + 1] || z > t.box[b + 3]) continue;
      if (inRing(x, z, t.pts, t.start[r], t.counts[r])) return true;
    }
  }
  return false;
}

std::vector<double> outlinePoints(double x, double z, double yaw, const Outline& o) {
  const double fx = -std::sin(yaw);
  const double fz = -std::cos(yaw);
  // (right of the way it faces)
  const double rx = std::cos(yaw);
  const double rz = -std::sin(yaw);
  std::vector<double> pts = {x, z, x + fx * o.front, z + fz * o.front, x - fx * o.rear, z - fz * o.rear};
  for (double a : {o.front, -o.rear})
    for (double s : {o.hw, -o.hw}) {
      pts.push_back(x + fx * a + rx * s);
      pts.push_back(z + fz * a + rz * s);
    }
  return pts;
}

bool inBuilding(double x, double z, double yaw, const Outline& o, const std::function<bool(double x, double z)>& inside) {
  const std::vector<double> p = outlinePoints(x, z, yaw, o);
  for (size_t i = 0; i < p.size(); i += 2)
    if (inside(p[i], p[i + 1])) return true;
  return false;
}

std::optional<FreePlace> freeNear(double x, double z, double yaw, const std::function<bool(double x, double z, double yaw)>& free, double maxR, double step) {
  if (free(x, z, yaw)) return FreePlace{x, z, yaw};
  for (double r = step; r <= maxR + 1e-9; r += step) {
    const double n = std::max(6.0, jsRound((2 * MATH_PI * r) / step));
    for (double y : {yaw, yaw + MATH_PI}) {
      for (double i = 0; i < n; i++) {
        const double a = (i / n) * MATH_PI * 2;
        const double px = x + std::cos(a) * r;
        const double pz = z + std::sin(a) * r;
        if (free(px, pz, y)) return FreePlace{px, pz, y};
      }
    }
  }
  return std::nullopt;
}

std::optional<FreePlace> besidePlace(double x, double z, double yaw, const std::function<bool(double x, double z, double yaw)>& free, double maxR) {
  const double fx = -std::sin(yaw);
  const double fz = -std::cos(yaw);
  const double rx = std::cos(yaw);
  const double rz = -std::sin(yaw);
  static const double SIDES[6][2] = {
      {0, 3.5},
      {0, -3.5},
      {0, 5},
      {0, -5},
      {6, 0},
      {-6, 0},
  };
  for (const auto& side : SIDES) {
    const double a = side[0];
    const double s = side[1];
    const double px = x + fx * a + rx * s;
    const double pz = z + fz * a + rz * s;
    if (free(px, pz, yaw)) return FreePlace{px, pz, yaw};
  }
  return freeNear(
      x, z, yaw, [&](double px, double pz, double py) { return std::hypot(px - x, pz - z) >= 2.5 && free(px, pz, py); }, maxR);
}

std::optional<GroundPoint> rayGround(double ox, double oy, double oz, double dx, double dy, double dz, const std::function<double(double x, double z)>& heightAt,
                                     double max) {
  const double len = std::hypot(dx, dy, dz);
  if (len < 1e-9) return std::nullopt;
  dx /= len;
  dy /= len;
  dz /= len;
  const auto above = [&](double t) {
    const double h = heightAt(ox + dx * t, oz + dz * t);
    return std::isnan(h) ? JS_NAN : oy + dy * t - h;
  };
  double t0 = 0;
  double a0 = above(0);
  for (double t = 0; t < max;) {
    const double t1 = t + std::max(0.5, t * 0.02);
    const double a1 = above(t1);
    if (!std::isnan(a0) && !std::isnan(a1) && a0 >= 0 && a1 < 0) {
      // between the two: halve until it is near enough
      double lo = t0;
      double hi = t1;
      for (int i = 0; i < 16; i++) {
        const double m = (lo + hi) / 2;
        if (above(m) >= 0) lo = m;
        else hi = m;
      }
      const double t2 = (lo + hi) / 2;
      const double x = ox + dx * t2;
      const double z = oz + dz * t2;
      return GroundPoint{x, heightAt(x, z), z};
    }
    t0 = t1;
    a0 = a1;
    t = t1;
  }
  return std::nullopt;
}

double rayBox(double ox, double oy, double oz, double dx, double dy, double dz, const StandingBox& b) {
  const double len = std::hypot(dx, dy, dz);
  if (len < 1e-9) return -1;
  // into the vehicle's frame: along its front, to its right, up
  const double fx = -std::sin(b.yaw);
  const double fz = -std::cos(b.yaw);
  const double rx = std::cos(b.yaw);
  const double rz = -std::sin(b.yaw);
  const double px = ox - b.x;
  const double pz = oz - b.z;
  const double o[3] = {px * fx + pz * fz, px * rx + pz * rz, oy - b.y};
  const double d[3] = {(dx * fx + dz * fz) / len, (dx * rx + dz * rz) / len, dy / len};
  const double lo[3] = {-b.rear, -b.hw, 0};
  const double hi[3] = {b.front, b.hw, b.height};
  double tn = 0;
  double tf = std::numeric_limits<double>::infinity();
  for (int i = 0; i < 3; i++) {
    if (std::fabs(d[i]) < 1e-12) {
      if (o[i] < lo[i] || o[i] > hi[i]) return -1;
      continue;
    }
    double a = (lo[i] - o[i]) / d[i];
    double c = (hi[i] - o[i]) / d[i];
    if (a > c) std::swap(a, c);
    tn = std::max(tn, a);
    tf = std::min(tf, c);
    if (tn > tf) return -1;
  }
  return tn;
}

}  // namespace fs

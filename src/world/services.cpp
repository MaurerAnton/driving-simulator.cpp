// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/services.ts @ v1.7.7
#include "world/services.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <set>
#include <stdexcept>

#include "core/js.h"
#include "world/fuelprices.h"

namespace fs::svc {

namespace {

constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

std::optional<double> optNum(const Json& v) {
  if (v.isNumber()) return v.number;
  return std::nullopt;
}

std::vector<double> numbers(const Json& v) {
  std::vector<double> out;
  for (const Json& e : v.array) out.push_back(e.isNumber() ? e.number : NaN);
  return out;
}

}  // namespace

ServicesJson servicesFromJson(const Json& j) {
  ServicesJson out;
  out.v = j.at("v").isNumber() ? static_cast<int>(j.at("v").number) : -1;
  out.map = j.at("map").asStr();
  for (const Json& a : j.at("attribution").array) out.attribution.push_back(a.asStr());
  if (!j.at("sites").isArray()) {
    out.v = -1;
    return out;
  }
  for (const Json& s : j.at("sites").array) {
    ServiceSiteJson site;
    site.k = s.at("k").asStr();
    site.osm = s.at("osm").asStr();
    site.name = s.at("name").asStr();
    site.brand = s.at("brand").asStr();
    site.x = s.at("x").isNumber() ? s.at("x").number : NaN;
    site.z = s.at("z").isNumber() ? s.at("z").number : NaN;
    if (s.at("area").isArray()) site.area = numbers(s.at("area"));
    for (const Json& f : s.at("fuels").array) site.fuels.push_back(f.asStr());
    for (const Json& r : s.at("roofs").array) {
      RoofJson roof;
      roof.ring = numbers(r.at("ring"));
      roof.y0 = optNum(r.at("y0"));
      roof.y1 = optNum(r.at("y1"));
      roof.src = r.at("src").asStr();
      roof.h = optNum(r.at("h"));
      roof.open = r.at("open").isNumber() && r.at("open").number != 0;
      site.roofs.push_back(std::move(roof));
    }
    if (s.at("sign").isArray() && s.at("sign").array.size() >= 3) {
      const auto& a = s.at("sign").array;
      site.sign = std::array<double, 3>{a[0].asNum(), a[1].asNum(), a[2].asNum()};
    }
    site.auto_ = s.at("auto").isNumber() && s.at("auto").number != 0;
    site.self = s.at("self").isNumber() && s.at("self").number != 0;
    site.hgv = s.at("hgv").isNumber() && s.at("hgv").number != 0;
    out.sites.push_back(std::move(site));
  }
  return out;
}

// ------------------------------------------------------------------ fuels

const std::map<std::string, FuelInfo>* const FUELS = new std::map<std::string, FuelInfo>{
    {"e10", {"Super E10", false, 1.739}}, {"e5", {"Super", false, 1.799}},       {"plus", {"Super Plus", false, 1.929}},
    {"diesel", {"Diesel", true, 1.659}},  {"hgv", {"Lkw-Diesel", true, 1.629}},  {"lpg", {"Autogas", false, 0.999}},
    {"cng", {"CNG", false, 1.459}},       {"adblue", {"AdBlue", true, 0.899}},
};

const std::vector<std::string>& FUEL_ORDER() {
  static const auto* o = new std::vector<std::string>{"e10", "e5", "plus", "diesel", "hgv", "lpg", "cng", "adblue"};
  return *o;
}

const std::vector<std::string>& DEFAULT_FUELS() {
  static const auto* d = new std::vector<std::string>{"e10", "e5", "diesel"};
  return *d;
}

std::vector<std::string> fuelsOf(const std::map<std::string, std::string>& tags) {
  const auto yes = [&tags](const char* k) {
    auto it = tags.find(k);
    if (it == tags.end()) return false;
    const std::string& v = it->second;
    return v == "yes" || v == "true" || v == "1";
  };
  std::set<std::string> out;
  if (yes("fuel:e10")) out.insert("e10");
  if (yes("fuel:octane_95") || yes("fuel:e5")) out.insert("e5");
  if (yes("fuel:octane_98") || yes("fuel:octane_100") || yes("fuel:octane_102")) out.insert("plus");
  if (yes("fuel:diesel") || yes("fuel:GTL_diesel") || yes("fuel:biodiesel")) out.insert("diesel");
  if (yes("fuel:HGV_diesel")) out.insert("hgv");
  if (yes("fuel:lpg")) out.insert("lpg");
  if (yes("fuel:cng")) out.insert("cng");
  if (yes("fuel:adblue")) out.insert("adblue");
  std::vector<std::string> list;
  for (const std::string& k : FUEL_ORDER())
    if (out.count(k)) list.push_back(k);
  return list;
}

double priceAt(const std::string& osm, std::optional<double> x, std::optional<double> z, const std::string& fuel) {
  // (today's real price, where the player has a key of Tankerkönig: world/fuelprices.h)
  if (x && z) {
    const std::optional<double> live = fuelPrices().priceAt(*x, *z, fuel);
    if (live) return *live;
  }
  auto it = FUELS->find(fuel);
  const double base = it != FUELS->end() ? it->second.price : 1.8;
  uint32_t h = 2166136261u;
  // (the ids are ASCII: a char is a UTF-16 unit)
  for (unsigned char c : osm) h = (h ^ c) * 16777619u;
  const double k = static_cast<double>(h % 1000) / 1000;
  return jsRound((base - 0.04 + k * 0.08) * 1000) / 1000;
}

// ------------------------------------------------------------------ geometry

std::array<double, 2> toRect(const Rect& r, double x, double z) {
  const double dx = x - r.cx;
  const double dz = z - r.cz;
  return {dx * r.ux + dz * r.uz, -dx * r.uz + dz * r.ux};
}

std::array<double, 2> fromRect(const Rect& r, double u, double v) { return {r.cx + u * r.ux - v * r.uz, r.cz + u * r.uz + v * r.ux}; }

std::vector<double> convexHull(const std::vector<double>& pts) {
  using P = std::array<double, 2>;
  std::vector<P> p;
  for (size_t i = 0; i + 1 < pts.size(); i += 2) p.push_back({pts[i], pts[i + 1]});
  std::stable_sort(p.begin(), p.end(), [](const P& a, const P& b) { return a[0] - b[0] < 0 || (a[0] - b[0] == 0 && a[1] - b[1] < 0); });
  std::vector<double> flat;
  if (p.size() < 3) {
    for (const P& q : p) flat.insert(flat.end(), q.begin(), q.end());
    return flat;
  }
  const auto cross = [](const P& o, const P& a, const P& b) { return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]); };
  std::vector<P> lower;
  for (const P& q : p) {
    while (lower.size() >= 2 && cross(lower[lower.size() - 2], lower[lower.size() - 1], q) <= 0) lower.pop_back();
    lower.push_back(q);
  }
  std::vector<P> upper;
  for (size_t i = p.size(); i-- > 0;) {
    const P& q = p[i];
    while (upper.size() >= 2 && cross(upper[upper.size() - 2], upper[upper.size() - 1], q) <= 0) upper.pop_back();
    upper.push_back(q);
  }
  upper.pop_back();
  lower.pop_back();
  for (const P& q : lower) flat.insert(flat.end(), q.begin(), q.end());
  for (const P& q : upper) flat.insert(flat.end(), q.begin(), q.end());
  return flat;
}

Rect minRect(const std::vector<double>& ring) {
  const std::vector<double> h = convexHull(ring);
  const size_t n = h.size() / 2;
  if (n == 0) return {0, 0, 1, 0, 0, 0};
  if (n < 3) {
    const double dx = h[h.size() - 2] - h[0];
    const double dz = h[h.size() - 1] - h[1];
    const double l = std::hypot(dx, dz);
    return {(h[0] + h[h.size() - 2]) / 2, (h[1] + h[h.size() - 1]) / 2, l > 0 ? dx / l : 1, l > 0 ? dz / l : 0, l, 0};
  }
  std::optional<Rect> best;
  double bestArea = JS_INF;
  for (size_t i = 0; i < n; i++) {
    const size_t j = (i + 1) % n;
    const double dx = h[j * 2] - h[i * 2];
    const double dz = h[j * 2 + 1] - h[i * 2 + 1];
    const double l = std::hypot(dx, dz);
    if (l < 1e-6) continue;
    const Rect r{0, 0, dx / l, dz / l, 0, 0};
    double u0 = JS_INF;
    double u1 = -JS_INF;
    double v0 = JS_INF;
    double v1 = -JS_INF;
    for (size_t k = 0; k < n; k++) {
      const auto [u, v] = toRect(r, h[k * 2], h[k * 2 + 1]);
      u0 = std::min(u0, u);
      u1 = std::max(u1, u);
      v0 = std::min(v0, v);
      v1 = std::max(v1, v);
    }
    const double area = (u1 - u0) * (v1 - v0);
    if (area < bestArea - 1e-9) {
      bestArea = area;
      const auto [cx, cz] = fromRect(r, (u0 + u1) / 2, (v0 + v1) / 2);
      best = Rect{cx, cz, r.ux, r.uz, u1 - u0, v1 - v0};
    }
  }
  const Rect b = best.value();
  // (the long side along u)
  if (b.wid > b.len) return {b.cx, b.cz, -b.uz, b.ux, b.wid, b.len};
  return b;
}

std::vector<double> rectRing(const Rect& r, double pad) {
  std::vector<double> out;
  for (const auto& [su, sv] : {std::pair{-1, -1}, std::pair{1, -1}, std::pair{1, 1}, std::pair{-1, 1}}) {
    const auto p = fromRect(r, su * (r.len / 2 + pad), sv * (r.wid / 2 + pad));
    out.push_back(p[0]);
    out.push_back(p[1]);
  }
  return out;
}

bool inRing(double x, double z, const std::vector<double>& ring) {
  bool inside = false;
  const size_t n = ring.size();
  if (n < 2) return false;
  for (size_t i = 0, j = n - 2; i < n; j = i, i += 2) {
    const double xi = ring[i];
    const double zi = ring[i + 1];
    const double xj = ring[j];
    const double zj = ring[j + 1];
    if ((zi > z) != (zj > z) && x < ((xj - xi) * (z - zi)) / (zj - zi) + xi) inside = !inside;
  }
  return inside;
}

double ringDistance(double x, double z, const std::vector<double>& ring) {
  double best = JS_INF;
  const size_t n = ring.size();
  if (n < 2) return best;
  for (size_t i = 0, j = n - 2; i < n; j = i, i += 2) {
    const double ax = ring[j];
    const double az = ring[j + 1];
    const double dx = ring[i] - ax;
    const double dz = ring[i + 1] - az;
    const double l2 = dx * dx + dz * dz;
    const double t = l2 > 1e-12 ? std::max(0.0, std::min(1.0, ((x - ax) * dx + (z - az) * dz) / l2)) : 0;
    best = std::min(best, std::hypot(ax + dx * t - x, az + dz * t - z));
  }
  return best;
}

double ringAreaAbs(const std::vector<double>& ring) {
  double a = 0;
  const size_t n = ring.size();
  for (size_t i = 0; i < n; i += 2) {
    const size_t j = (i + 2) % n;
    a += ring[i] * ring[j + 1] - ring[j] * ring[i + 1];
  }
  return std::abs(a / 2);
}

// ------------------------------------------------------------------ the layout of a site

namespace {
constexpr double ROW_PITCH = 7;
constexpr double COLUMN_PITCH = 10;
}  // namespace

CanopyPlan planCanopy(const std::vector<double>& ring, double y0, double y1, double h) {
  const Rect rect = minRect(ring);
  const int rows = static_cast<int>(std::max(1.0, std::floor((rect.wid + 1.5) / ROW_PITCH)));
  const double pitchV = std::min(7.4, rect.wid / rows);
  const int cols = static_cast<int>(std::max(1.0, std::floor((rect.len + 2) / COLUMN_PITCH)));
  const double pitchU = rect.len / cols;
  const double len = std::max(3.0, std::min(5.5, pitchU - 4));
  CanopyPlan plan;
  plan.ring = ring;
  plan.rect = rect;
  plan.y0 = y0;
  plan.y1 = y1;
  plan.h = h;
  for (int i = 0; i < rows; i++) {
    const double v = (i - (rows - 1) / 2.0) * pitchV;
    for (int j = 0; j < cols; j++) {
      const double u = (j - (cols - 1) / 2.0) * pitchU;
      Rect isl{0, 0, rect.ux, rect.uz, len, ISLAND_WIDTH};
      const auto c = fromRect(rect, u, v);
      isl.cx = c[0];
      isl.cz = c[1];
      // (every corner under the roof)
      const std::vector<double> corners = rectRing(isl, 0.3);
      bool under = true;
      for (size_t k = 0; k < corners.size(); k += 2)
        if (!inRing(corners[k], corners[k + 1], ring)) under = false;
      if (!under && !(rows == 1 && cols == 1)) continue;
      plan.islands.push_back({isl.cx, isl.cz, rect.ux, rect.uz, len, ISLAND_WIDTH});
      plan.pumps.push_back(isl.cx);
      plan.pumps.push_back(isl.cz);
      // a column at the outer end of the island (at both ends of a lone island), standing on it
      const std::vector<double> ends = cols == 1 ? std::vector<double>{-1, 1} : std::vector<double>{u < 0 ? -1.0 : 1.0};
      for (double e : ends) {
        const auto p = fromRect(rect, u + e * (len / 2 - 0.35), v);
        plan.columns.push_back(p[0]);
        plan.columns.push_back(p[1]);
      }
    }
  }
  return plan;
}

std::optional<WashPlan> planWash(const ServiceSiteJson& site) {
  if (site.roofs.empty()) return std::nullopt;
  const RoofJson& roof = site.roofs[0];
  const Rect rect = minRect(roof.ring);
  if (rect.wid < WASH_MIN.wid || rect.len < WASH_MIN.len) return std::nullopt;
  return WashPlan{roof.ring, rect, roof.y0.value_or(NaN), roof.y1.value_or(NaN), roof.src != "gen"};
}

// ------------------------------------------------------------------ a vehicle at a pump, in a bay

std::optional<AtPump> pumpBeside(const CanopyPlan& plan, const Pose& p) {
  const double fx = -std::sin(p.yaw);
  const double fz = -std::cos(p.yaw);
  std::optional<AtPump> best;
  for (size_t i = 0; i < plan.islands.size(); i++) {
    const Island& isl = plan.islands[i];
    // parallel to the island, one way or the other
    if (std::abs(fx * isl.ux + fz * isl.uz) < std::cos((35 * std::numbers::pi) / 180)) continue;
    const double dx = plan.pumps[i * 2] - p.x;
    const double dz = plan.pumps[i * 2 + 1] - p.z;
    // along the vehicle (+ towards its rear) and across it (+ to its right)
    const double along = -(dx * fx + dz * fz);
    const double across = dx * -fz + dz * fx;
    const double gap = std::abs(across) - p.width / 2 - ISLAND_WIDTH / 2;
    if (gap < -0.2 || gap > HOSE_REACH) continue;
    if (std::abs(along) > p.length / 2 + 0.8) continue;
    if (!best || gap < best->gap) best = AtPump{static_cast<int>(i), across >= 0 ? 1 : -1, along, gap};
  }
  return best;
}

InBay inBay(const WashPlan& plan, const Pose& p) {
  const auto [u, v] = toRect(plan.rect, p.x, p.z);
  const double fx = -std::sin(p.yaw);
  const double fz = -std::cos(p.yaw);
  const double c = fx * plan.rect.ux + fz * plan.rect.uz;
  const int dir = c >= 0 ? 1 : -1;
  return {u * dir, v, std::abs(c) > std::cos((14 * std::numbers::pi) / 180), std::abs(u) < plan.rect.len / 2 + 0.5 && std::abs(v) < plan.rect.wid / 2, dir};
}

// ------------------------------------------------------------------ the sites, to be found by place

std::vector<ServiceSite> parseServices(const ServicesJson& json) {
  if (json.v != SERVICES_VERSION) throw std::runtime_error("not a services file of version 1");
  std::vector<ServiceSite> out;
  for (const ServiceSiteJson& s : json.sites) {
    if (!std::isfinite(s.x) || !std::isfinite(s.z) || (s.k != "fuel" && s.k != "wash")) continue;
    ServiceSite site;
    static_cast<ServiceSiteJson&>(site) = s;
    site.index = static_cast<int>(out.size());
    if (s.k == "fuel") {
      if (!s.fuels.empty()) {
        for (const std::string& f : s.fuels)
          if (FUELS->count(f)) site.sells.push_back(f);
      } else site.sells = DEFAULT_FUELS();
      for (const RoofJson& r : s.roofs) {
        if (r.ring.size() < 6) continue;
        CanopyPlan plan = planCanopy(r.ring, r.y0.value_or(NaN), r.y1.value_or(NaN), r.h.value_or(NaN));
        if (!plan.islands.empty()) site.canopies.push_back(std::move(plan));
      }
    } else site.wash = planWash(s);
    out.push_back(std::move(site));
  }
  return out;
}

ServiceIndex::ServiceIndex(std::vector<ServiceSite> sitesIn) : sites(std::move(sitesIn)) {
  for (const ServiceSite& s : sites) {
    const auto [x0, z0, x1, z1] = siteBox(s, 20);
    for (double i = std::floor(x0 / CELL); i <= std::floor(x1 / CELL); i++) {
      for (double j = std::floor(z0 / CELL); j <= std::floor(z1 / CELL); j++) grid_[key(i, j)].push_back(&s);
    }
  }
}

std::vector<const ServiceSite*> ServiceIndex::near(double x, double z, double r) const {
  std::vector<const ServiceSite*> out;
  std::set<const ServiceSite*> seen;
  for (double i = std::floor((x - r) / CELL); i <= std::floor((x + r) / CELL); i++) {
    for (double j = std::floor((z - r) / CELL); j <= std::floor((z + r) / CELL); j++) {
      auto it = grid_.find(key(i, j));
      if (it == grid_.end()) continue;
      for (const ServiceSite* s : it->second)
        if (seen.insert(s).second) out.push_back(s);
    }
  }
  return out;
}

std::optional<ServiceIndex::Nearest> ServiceIndex::nearest(double x, double z, const std::string& kind, const std::function<bool(const ServiceSite&)>& accept) const {
  std::optional<Nearest> best;
  for (const ServiceSite& s : sites) {
    if (s.k != kind || (kind == "fuel" ? s.canopies.empty() : !s.wash) || (accept && !accept(s))) continue;
    const double d = std::hypot(s.x - x, s.z - z);
    if (!best || d < best->dist) best = Nearest{&s, d};
  }
  return best;
}

std::array<double, 4> siteBox(const ServiceSiteJson& s, double pad) {
  double x0 = s.x;
  double z0 = s.z;
  double x1 = s.x;
  double z1 = s.z;
  const auto take = [&](const double* r, size_t n) {
    for (size_t i = 0; i + 1 < n; i += 2) {
      x0 = std::min(x0, r[i]);
      x1 = std::max(x1, r[i]);
      z0 = std::min(z0, r[i + 1]);
      z1 = std::max(z1, r[i + 1]);
    }
  };
  if (s.area) take(s.area->data(), s.area->size());
  for (const RoofJson& r : s.roofs) take(r.ring.data(), r.ring.size());
  if (s.sign) take(s.sign->data(), 3);
  return {x0 - pad, z0 - pad, x1 + pad, z1 + pad};
}

bool onSite(const ServiceSiteJson& s, double x, double z, double margin) {
  if (s.area && (inRing(x, z, *s.area) || ringDistance(x, z, *s.area) < margin)) return true;
  for (const RoofJson& r : s.roofs)
    if (inRing(x, z, r.ring) || ringDistance(x, z, r.ring) < margin) return true;
  return !s.area && s.roofs.empty() && std::hypot(x - s.x, z - s.z) < margin + 15;
}

}  // namespace fs::svc

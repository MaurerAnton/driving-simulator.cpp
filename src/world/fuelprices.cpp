// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/fuelprices.ts @ v1.7.7
#include "world/fuelprices.h"

#include <cmath>
#include <limits>

#include "core/js.h"
#include "geo/utm.h"

namespace fs {

std::map<std::string, double> pricesOf(const Json& s) {
  std::map<std::string, double> out;
  const auto ok = [](const Json& v) { return v.isNumber() && v.number > 0.3 && v.number < 5; };
  if (ok(s.at("e5"))) out["e5"] = s.at("e5").number;
  if (ok(s.at("e10"))) out["e10"] = s.at("e10").number;
  if (ok(s.at("diesel"))) {
    out["diesel"] = s.at("diesel").number;
    out["hgv"] = s.at("diesel").number;
  }
  return out;
}

FuelPrices::FuelPrices(FetchFn fetchFn)
    : asked_{std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), -JS_INF},
      fetchFn_(fetchFn ? std::move(fetchFn) : FetchFn([](plat::Request r) { return plat::fetch(std::move(r)); })) {}

void FuelPrices::refresh(double x, double z, std::optional<double> nowIn) {
  const double now = nowIn ? *nowIn : plat::dateMs();
  if (key.empty() || pending_) return;
  const bool moved = !std::isfinite(asked_.x) || std::hypot(x - asked_.x, z - asked_.z) > 10000;
  if (!moved && now - asked_.at < 10 * 60000) return;
  if (now - asked_.at < 60000) return;
  asked_ = {x, z, now};
  const auto [lat, lon] = worldToLatLon(x, z);
  const std::string url = std::string(FUEL_PRICE_SOURCE.url) + "/json/list.php?lat=" + toFixed(lat, 3) + "&lng=" + toFixed(lon, 3) +
                          "&rad=25&sort=dist&type=all&apikey=" + encodeURIComponent(key);
  pending_ = true;
  ask(url, now).detach();
}

Task<void> FuelPrices::ask(std::string url, double now) {
  std::weak_ptr<bool> life = life_;
  plat::Request req;
  req.url = std::move(url);
  // (no stored copy; no cookies and no referrer: the host's fetch)
  req.cache = "no-store";
  plat::Response r = co_await fetchFn_(std::move(req));
  if (life.expired()) co_return;
  pending_ = false;
  if (!r.ok()) co_return;
  Json j;
  if (!Json::parse(r.text(), j)) co_return;
  if (!(j.at("ok").isBool() && j.at("ok").boolean) || !j.at("stations").isArray()) co_return;
  std::vector<Live> list;
  for (const Json& s : j.at("stations").array) {
    const double lat = s.at("lat").asNum();
    const double lng = s.at("lng").asNum();
    if (!s.at("lat").isNumber() || !s.at("lng").isNumber() || !std::isfinite(lat) || !std::isfinite(lng)) continue;
    const auto [sx, sz] = latLonToWorld(lat, lng);
    list.push_back({sx, sz, !(s.at("isOpen").isBool() && !s.at("isOpen").boolean), pricesOf(s)});
  }
  live_ = std::move(list);
  updated = now;
}

std::optional<double> FuelPrices::priceAt(double x, double z, const std::string& fuel) const {
  const Live* best = nullptr;
  double bd = 150;
  for (const Live& s : live_) {
    const double d = std::hypot(s.x - x, s.z - z);
    if (d < bd) {
      bd = d;
      best = &s;
    }
  }
  if (!best) return std::nullopt;
  auto it = best->prices.find(fuel);
  if (it == best->prices.end()) return std::nullopt;
  return it->second;
}

void FuelPrices::setLive(const std::vector<LiveIn>& list, std::optional<double> now) {
  live_.clear();
  for (const LiveIn& s : list) live_.push_back({s.x, s.z, true, s.prices});
  updated = now ? *now : plat::dateMs();
}

FuelPrices& fuelPrices() {
  static auto* p = new FuelPrices();
  return *p;
}

}  // namespace fs

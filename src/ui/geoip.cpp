// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/geoip.ts @ v1.7.7
#include "ui/geoip.h"

#include <cmath>

#include "core/js.h"
#include "geo/utm.h"
#include "platform/json.h"

namespace fs {

namespace {
/** `typeof v === 'number' ? v : Number(v)` of a member of the answer (null: it has no such member) */
double numberOf(const Json* v) {
  if (!v) return JS_NAN;
  switch (v->kind) {
    case Json::Kind::Number: return v->number;
    case Json::Kind::Null: return 0;
    case Json::Kind::Bool: return v->boolean ? 1 : 0;
    case Json::Kind::String: return jsNumber(v->str);
    default: return JS_NAN;
  }
}
}  // namespace

Task<IpPlace> ipPlace(FetchFn fetchFn) {
  plat::Request req;
  req.url = "https://ipwho.is/";
  req.cache = "no-store";
  // (a fetch of the port never throws: no answer is status 0)
  const plat::Response res = co_await (fetchFn ? fetchFn(std::move(req)) : plat::fetch(std::move(req)));
  if (!res.ok()) co_return std::nullopt;
  Json j;
  if (!Json::parse(res.text(), j) || !j.isObject()) co_return std::nullopt;
  if (j.at("success").isBool() && !j.at("success").boolean) co_return std::nullopt;
  const double lat = numberOf(j.object.find("latitude"));
  const double lon = numberOf(j.object.find("longitude"));
  if (!std::isfinite(lat) || !std::isfinite(lon)) co_return std::nullopt;
  co_return LatLon{lat, lon};
}

const Region* ipRegion(const RegionIndex* index, const IpPlace& place) {
  if (!index || !place) return nullptr;
  const auto [x, z] = latLonToWorld(place->lat, place->lon);
  return regionAt(index, x, z);
}

}  // namespace fs

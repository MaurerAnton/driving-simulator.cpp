// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/geolocate.ts @ v1.7.7
#include "ui/geolocate.h"

#include "geo/utm.h"

namespace fs {

bool onMap(double x, double z, const MapBounds& b, double margin) { return x >= b.minX - margin && x <= b.maxX + margin && z >= b.minZ - margin && z <= b.maxZ + margin; }

Task<LocateResult> locate(MapBounds bounds, const plat::Geolocation* geo) {
  Resolver<LocateResult> resolve;
  if (!geo) {
    resolve.resolve(LocateResult{.ok = false, .why = "unsupported"});
    return resolve.task();
  }
  geo->getCurrentPosition(
      [resolve, bounds](const plat::GeoPosition& p) {
        const auto [x, z] = latLonToWorld(p.coords.latitude, p.coords.longitude);
        resolve.resolve(onMap(x, z, bounds) ? LocateResult{.ok = true, .x = x, .z = z, .accuracy = p.coords.accuracy} : LocateResult{.ok = false, .why = "outside"});
      },
      [resolve](const plat::GeoError& e) { resolve.resolve(LocateResult{.ok = false, .why = e.code == 1 ? "denied" : "unavailable"}); },
      plat::GeoOptions{.enableHighAccuracy = true, .timeout = 15000, .maximumAge = 60000});
  return resolve.task();
}

}  // namespace fs

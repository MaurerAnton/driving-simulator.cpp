// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Where the device is, asked of the browser: `navigator.geolocation` (the Geolocation
 * API).  The browser asks the player before it answers.
 *
 *     navigator.geolocation.getCurrentPosition(ok, fail, { enableHighAccuracy, timeout, maximumAge })
 *
 * Two hosts implement geolocation(): the browser (platform/web: the real API; null where
 * `navigator.geolocation` is missing) and this machine (platform/native/maps_native.cpp:
 * null until a test sets a position or an error – maps_native.h).
 */
#pragma once

#include <functional>

namespace fs::plat {

/** GeolocationCoordinates: what the game reads of it */
struct GeoCoords {
  double latitude = 0;
  double longitude = 0;
  /** metres */
  double accuracy = 0;
};

/** GeolocationPosition */
struct GeoPosition {
  GeoCoords coords;
};

/** GeolocationPositionError: code 1 PERMISSION_DENIED, 2 POSITION_UNAVAILABLE, 3 TIMEOUT */
struct GeoError {
  int code = 2;
};

/** PositionOptions (timeout and maximumAge in milliseconds) */
struct GeoOptions {
  bool enableHighAccuracy = false;
  double timeout = 1e300;
  double maximumAge = 0;
};

/** `Geolocation`: the one method the game calls.  Exactly one of the two callbacks is called, once, later. */
struct Geolocation {
  std::function<void(std::function<void(const GeoPosition&)> ok, std::function<void(const GeoError&)> fail, const GeoOptions& options)> getCurrentPosition;
};

/** `navigator.geolocation` – null: the browser has none (`undefined`) */
const Geolocation* geolocation();

}  // namespace fs::plat

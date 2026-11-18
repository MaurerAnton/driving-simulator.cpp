// SPDX-License-Identifier: AGPL-3.0-only
/**
 * ETRS89 / UTM zone 32N (EPSG:25832) <-> geographic coordinates, plus the
 * simulator's local world frame.
 *
 * Transverse Mercator via the Krüger n-series (sub-millimetre inside a zone).
 *
 * World frame (metres):  x = east, y = up (height above sea level, DHHN2016),
 * z = south.  North is therefore -Z, which matches three.js' default camera.
 *
 * Origin: src/geo/utm.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#pragma once

#include <utility>

namespace fs {

/** Geographic (degrees) -> UTM32 easting/northing (metres). */
std::pair<double, double> latLonToUtm32(double latDeg, double lonDeg);

/** UTM32 easting/northing (metres) -> geographic (lat, lon) in degrees. */
std::pair<double, double> utm32ToLatLon(double easting, double northing);

/** UTM32 origin of the Baunatal world frame (close to the town centre). */
constexpr double WORLD_ORIGIN_E = 528000;
constexpr double WORLD_ORIGIN_N = 5678000;

/**
 * Extent of the simulated world in UTM32 kilometre squares.  It is the
 * bounding rectangle of the official 1 km DGM1 tiles published for Baunatal.
 */
constexpr double WORLD_MIN_E = 522000;
constexpr double WORLD_MAX_E = 534000;
constexpr double WORLD_MIN_N = 5674000;
constexpr double WORLD_MAX_N = 5682000;

/** UTM32 -> world frame (east, south). */
std::pair<double, double> utmToWorld(double easting, double northing);

/** World frame -> UTM32 (east, north). */
std::pair<double, double> worldToUtm(double x, double z);

/** Geographic (degrees) -> world frame. */
std::pair<double, double> latLonToWorld(double latDeg, double lonDeg);

/** World frame -> geographic (lat, lon) in degrees. */
std::pair<double, double> worldToLatLon(double x, double z);

}  // namespace fs

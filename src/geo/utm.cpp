// SPDX-License-Identifier: AGPL-3.0-only
/**
 * ETRS89 / UTM zone 32N (EPSG:25832) <-> geographic coordinates, plus the
 * simulator's local world frame.
 *
 * Origin: src/geo/utm.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * The Krüger n-series coefficients are carried over exactly.
 */
#include "geo/utm.h"

#include <cmath>

namespace fs {

namespace {
constexpr double A_GRS80 = 6378137.0;
constexpr double F_GRS80 = 1.0 / 298.257222101;
constexpr double K0 = 0.9996;
constexpr double FALSE_EASTING = 500000;
constexpr double LON0_ZONE32 = (9.0 * 3.14159265358979323846) / 180.0;

constexpr double N1 = F_GRS80 / (2 - F_GRS80);
constexpr double N2 = N1 * N1;
constexpr double N3 = N2 * N1;
constexpr double N4 = N2 * N2;
constexpr double A_RECT = (A_GRS80 / (1 + N1)) * (1 + N2 / 4 + N4 / 64);

constexpr double ALPHA[3] = {N1 / 2 - (2 * N2) / 3 + (5 * N3) / 16,
                             (13 * N2) / 48 - (3 * N3) / 5,
                             (61 * N3) / 240};
constexpr double BETA[3] = {N1 / 2 - (2 * N2) / 3 + (37 * N3) / 96,
                            N2 / 48 + N3 / 15,
                            (17 * N3) / 480};
constexpr double DELTA[3] = {2 * N1 - (2 * N2) / 3 - 2 * N3,
                             (7 * N2) / 3 - (8 * N3) / 5,
                             (56 * N3) / 15};
constexpr double E_CONF = (2 * std::sqrt(N1)) / (1 + N1);
constexpr double PI_U = 3.14159265358979323846;

// the Krüger series need atanh() for |x| < 1 only (the inputs are sines)
double atanhD(double x) { return 0.5 * std::log((1 + x) / (1 - x)); }
}  // namespace

std::pair<double, double> latLonToUtm32(double latDeg, double lonDeg) {
  double phi = (latDeg * PI_U) / 180;
  double dLam = (lonDeg * PI_U) / 180 - LON0_ZONE32;
  double sinPhi = std::sin(phi);
  double t = std::sinh(atanhD(sinPhi) - E_CONF * atanhD(E_CONF * sinPhi));
  double xiP = std::atan2(t, std::cos(dLam));
  double etaP = atanhD(std::sin(dLam) / std::sqrt(1 + t * t));
  double xi = xiP;
  double eta = etaP;
  for (int j = 0; j < 3; j++) {
    int k = 2 * (j + 1);
    xi += ALPHA[j] * std::sin(k * xiP) * std::cosh(k * etaP);
    eta += ALPHA[j] * std::cos(k * xiP) * std::sinh(k * etaP);
  }
  return {FALSE_EASTING + K0 * A_RECT * eta, K0 * A_RECT * xi};
}

std::pair<double, double> utm32ToLatLon(double easting, double northing) {
  double xi = northing / (K0 * A_RECT);
  double eta = (easting - FALSE_EASTING) / (K0 * A_RECT);
  double xiP = xi;
  double etaP = eta;
  for (int j = 0; j < 3; j++) {
    int k = 2 * (j + 1);
    xiP -= BETA[j] * std::sin(k * xi) * std::cosh(k * eta);
    etaP -= BETA[j] * std::cos(k * xi) * std::sinh(k * eta);
  }
  double chi = std::asin(std::sin(xiP) / std::cosh(etaP));
  double phi = chi;
  for (int j = 0; j < 3; j++) phi += DELTA[j] * std::sin(2 * (j + 1) * chi);
  double lam = LON0_ZONE32 + std::atan2(std::sinh(etaP), std::cos(xiP));
  return {(phi * 180) / PI_U, (lam * 180) / PI_U};
}

std::pair<double, double> utmToWorld(double easting, double northing) {
  return {easting - WORLD_ORIGIN_E, -(northing - WORLD_ORIGIN_N)};
}

std::pair<double, double> worldToUtm(double x, double z) {
  return {x + WORLD_ORIGIN_E, WORLD_ORIGIN_N - z};
}

std::pair<double, double> latLonToWorld(double latDeg, double lonDeg) {
  auto en = latLonToUtm32(latDeg, lonDeg);
  return utmToWorld(en.first, en.second);
}

std::pair<double, double> worldToLatLon(double x, double z) {
  auto en = worldToUtm(x, z);
  return utm32ToLatLon(en.first, en.second);
}

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
/** UTM32 ↔ world frame: Baunatal reference points and round trips. */
#include "../src/geo/utm.h"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace fs;

int main() {
  // the world origin maps to the configured UTM origin
  auto en = worldToUtm(0, 0);
  assert(en.first == WORLD_ORIGIN_E && en.second == WORLD_ORIGIN_N);
  auto xz = utmToWorld(WORLD_ORIGIN_E, WORLD_ORIGIN_N);
  assert(xz.first == 0 && xz.second == 0);

  // the world frame is east + south: moving north decreases z
  auto n = worldToUtm(0, -100);
  assert(n.first == WORLD_ORIGIN_E && n.second == WORLD_ORIGIN_N + 100);

  // Baunatal town centre (51.2551 N, 9.4119 E) must land inside the world box
  auto c = latLonToWorld(51.2550775, 9.4119007);
  assert(c.first > WORLD_MIN_E - WORLD_ORIGIN_E - 1);
  assert(c.first < WORLD_MAX_E - WORLD_ORIGIN_E + 1);
  assert(c.second > WORLD_MIN_N - WORLD_ORIGIN_N - 1);  // z = -(N - N0), so inside means negative
  assert(c.second < WORLD_MAX_N - WORLD_ORIGIN_N + 1);

  // round trip: geographic -> world -> geographic (sub-millimetre inside a zone)
  double lat = 51.25272, lon = 9.41077;  // Stadtmitte
  auto w = latLonToWorld(lat, lon);
  auto back = worldToLatLon(w.first, w.second);
  assert(std::fabs(back.first - lat) < 1e-7);
  assert(std::fabs(back.second - lon) < 1e-7);

  // round trip: UTM -> lat/lon -> UTM
  auto en2 = latLonToUtm32(lat, lon);
  assert(std::fabs(en2.first - 528666.6) < 0.5);   // matches the value resolved earlier
  assert(std::fabs(en2.second - 5678009.1) < 0.5);
  auto backEn = utm32ToLatLon(en2.first, en2.second);
  assert(std::fabs(backEn.first - lat) < 1e-7);
  assert(std::fabs(backEn.second - lon) < 1e-7);

  // the central meridian of zone 32 has exactly the false easting
  auto cm = latLonToUtm32(51.0, 9.0);
  assert(std::fabs(cm.first - 500000.0) < 1e-6);  // false easting of zone 32

  std::printf("utm: all checks passed (world origin, Baunatal, round trips)\n");
  return 0;
}

// SPDX-License-Identifier: AGPL-3.0-only
// LINK: ui i18n core platform
// Origin: tests/ui/fuelgauge.test.ts @ v1.7.7
/** The fuel amount as a percentage (src/ui/fuelgauge.ts): three decimals, clipped to 0 … 100. */
#include <cassert>
#include <cmath>
#include <cstdio>

#include "ui/fuelgauge.h"

using namespace fs;

int main() {
  // the tank's share is a percentage to three decimals
  assert(fuelPercent(0.09536) == "9.536");
  assert(fuelPercent(1) == "100.000");
  assert(fuelPercent(0.5) == "50.000");
  assert(fuelPercent(0) == "0.000");
  assert(fuelPercent(0.3333333) == "33.333");

  // a share outside 0 … 1 is clipped, never a negative or over 100 %
  assert(fuelPercent(-1) == "0.000");
  assert(fuelPercent(2) == "100.000");
  assert(fuelPercent(NAN) == "0.000");

  std::puts("fuelgauge_test: ok");
  return 0;
}

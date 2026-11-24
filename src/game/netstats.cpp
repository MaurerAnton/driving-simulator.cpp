// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/netstats.ts @ v1.7.7
#include "game/netstats.h"

#include "platform/intl.h"

namespace fs {

std::string formatBytes(double n, const std::string& locale) {
  const double mb = n / 1e6;
  const auto f = [&locale](double v, int d) { return plat::formatNumber(v, locale, {.minimumFractionDigits = d, .maximumFractionDigits = d}); };
  return mb >= 1 ? f(mb, mb >= 100 ? 0 : 1) + " MB" : f(n / 1e3, 0) + " kB";
}

}  // namespace fs

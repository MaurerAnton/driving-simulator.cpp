// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/releases.ts @ v1.7.7
#include "game/releases.h"

#include "platform/intl.h"

namespace fs {

std::vector<std::pair<std::string, std::string>> sizeWords(const ReleaseSize& s, const std::string& locale) {
  // (Western digits, like all the texts)
  const plat::NumberFormat full = {.numberingSystem = "latn"};
  const plat::NumberFormat shortForm = {.notation = "compact", .maximumSignificantDigits = 3, .numberingSystem = "latn"};
  const plat::NumberFormat mb = {.minimumFractionDigits = 1, .maximumFractionDigits = 1, .numberingSystem = "latn"};
  const plat::NumberFormat g = {.minimumFractionDigits = 1, .maximumFractionDigits = 1, .numberingSystem = "latn"};
  const double maps = s.maps.value_or(0);
  return {
      {"lines", plat::formatNumber(s.lines, locale, full)},
      {"code", plat::formatNumber(s.code, locale, shortForm)},
      {"tests", plat::formatNumber(s.tests, locale, shortForm)},
      {"texts", plat::formatNumber(s.i18n, locale, shortForm)},
      {"bytes", plat::formatNumber(s.bytes / 1e6, locale, mb) + " MB"},
      {"maps", maps > 0 ? plat::formatNumber(maps / 1e9, locale, g) + " GB" : ""},
  };
}

namespace {
const char* const KEY = "fahrsim-baunatal.news";
}  // namespace

bool newsDue(bool played, KeyValueStore* store) {
  if (!store) return false;
  const std::optional<std::string> seen = store->getItem(KEY);
  // (a storage that keeps nothing: never)
  if (!store->setItem(KEY, VERSION)) return false;
  return played && seen != VERSION;
}

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
/**
 * i18n checks: the C++ catalogue must carry the same keys and strings as
 * src/ui/i18n.ts @ 5b6ddc7 (v0.1.0), and {param} substitution must behave
 * like JS String.replace (first occurrence only).
 */
#include "../src/game/i18n.h"
#include <cassert>
#include <cstdio>

using namespace fs;

int main() {
  // 149 UI keys + 35 violation ids at v0.5.0
  assert(UI_ENTRIES.size() == 149);
  assert(VIOLATION_ENTRIES.size() == 35);

  // basic lookups (verbatim from the TypeScript table)
  assert(tr("title", Lang::De) == "Fahrsim Baunatal");
  assert(tr("loading", Lang::En) == "Loading map …");
  assert(tr("webglMissing", Lang::De) == "Dieser Browser unterstützt kein WebGL 2.");
  assert(tr("navRoundabout", Lang::En) == "take the {n}. exit at the roundabout");

  // 0.5.0: the touch variants of the tips and the address search
  assert(tr("startEngineTouch", Lang::De) == "Motor starten: hier gedrückt halten");
  assert(tr("teleport", Lang::En) == "Jump to an address");
  assert(tr("lessonNoRoute", Lang::De).find("freie Fahrt") != std::string::npos);

  // unknown key returns the key itself
  assert(tr("nopeNotAKey", Lang::En) == "nopeNotAKey");

  // {param} substitution, first occurrence only (JS String.replace semantics)
  assert(tr("navIn", Lang::En, {{"d", "300"}, {"what", "turn left"}}) == "In 300 m turn left");
  assert(tr("navNow", Lang::De, {{"what", "rechts abbiegen"}}) == "Jetzt rechts abbiegen");

  // violation catalogue
  assert(violationTitle("speed", Lang::De, {{"speed", "80"}, {"limit", "50"}}) ==
         "Zu schnell: 80 bei erlaubten 50 km/h");
  assert(violationTitle("speed", Lang::En, {{"speed", "80"}, {"limit", "50"}}) ==
         "Too fast: 80 where 50 km/h is allowed");
  assert(violationWhy("redLight", Lang::De) ==
         "Rot bedeutet: Halt vor der Kreuzung, an der Haltlinie warten.");
  assert(violationWhy("redLight", Lang::En) ==
         "Red means: stop before the junction and wait at the stop line.");
  assert(violationTitle("unknownViolation", Lang::En, {}) == "unknownViolation");
  assert(violationWhy("unknownViolation", Lang::En) == "");

  // every violation carries a legal basis
  for (const auto& e : VIOLATION_ENTRIES) assert(!e.second.ref.empty());

  std::printf("i18n: all checks passed (%zu UI strings, %zu violations)\n",
              UI_ENTRIES.size(), VIOLATION_ENTRIES.size());
  return 0;
}

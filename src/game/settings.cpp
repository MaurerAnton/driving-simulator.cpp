// SPDX-License-Identifier: AGPL-3.0-only
/**
 * User settings, persisted in localStorage.
 *
 * Origin: src/game/settings.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * The browser build stores the JSON under the same key as the original; the
 * native build keeps the last saved value in memory.
 */
#include "game/settings.h"

#include "i18n/index.h"

#include <cstdlib>

namespace fs {

namespace {
const char* kKey = "fahrsim-baunatal.settings.v1";
Settings g_saved;      // last value written (native build has no localStorage)
bool g_hasSaved = false;
}  // namespace

Settings defaultSettings() {
  Settings s;
  // JS: typeof navigator !== 'undefined' ? pickLang(navigator.languages …) : 'en'
  // Without a browser navigator no language is reported: English, as the
  // original falls back to it (the native build starts in it too).
  s.lang = pickLang({});
  return s;
}

Settings loadSettings() {
  if (!g_hasSaved) return defaultSettings();
  Settings s = g_saved;
  // a language the simulator speaks, or the default (as the original checks)
  if (s.lang.empty()) s.lang = defaultSettings().lang;
  bool known = false;
  for (const LangInfo& l : LANGS)
    if (l.code == s.lang) known = true;
  if (!known) s.lang = defaultSettings().lang;
  return s;
}

void saveSettings(const Settings& s) {
  g_saved = s;
  g_hasSaved = true;
}

}  // namespace fs

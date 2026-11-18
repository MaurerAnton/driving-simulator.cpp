// SPDX-License-Identifier: AGPL-3.0-only
/**
 * User settings, persisted in localStorage.
 *
 * Origin: src/game/settings.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * The browser build stores the JSON under the same key as the original; the
 * native build keeps the last saved value in memory.
 */
#include "game/settings.h"

#include <cstdlib>

namespace fs {

namespace {
const char* kKey = "fahrsim-baunatal.settings.v1";
Settings g_saved;      // last value written (native build has no localStorage)
bool g_hasSaved = false;
}  // namespace

Settings defaultSettings() {
  Settings s;
  // JS: typeof navigator !== 'undefined' && !navigator.language.startsWith('de') ? 'en' : 'de'
  // Without a browser navigator the language stays German, as the original does.
  s.lang = LangSetting::De;
  return s;
}

Settings loadSettings() {
  if (!g_hasSaved) return defaultSettings();
  return g_saved;
}

void saveSettings(const Settings& s) {
  g_saved = s;
  g_hasSaved = true;
}

}  // namespace fs

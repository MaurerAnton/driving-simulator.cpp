// SPDX-License-Identifier: AGPL-3.0-only
/**
 * User settings, persisted in localStorage.
 *
 * Origin: src/game/settings.ts (MaurerAnton/driving-simulator @ ff37c5d, v0.7.0).
 * The browser build stores the JSON under the same key as the original; the
 * native build keeps the last saved value in memory.
 */
#include "game/settings.h"

#include "i18n/index.h"

#include <cmath>
#include <cstdlib>

namespace fs {

namespace {
const char* kKey = "fahrsim-baunatal.settings.v2";
/** settings of the versions before the playtest (other defaults) */
const char* kOldKey = "fahrsim-baunatal.settings.v1";
Settings g_saved;      // last value written (native build has no localStorage)
bool g_hasSaved = false;

/** Defaults, overlaid with what was stored – whatever is missing or implausible keeps its default. */
Settings merge(const Settings& parsed) {
  const Settings d = defaultSettings();
  Settings s = parsed;
  bool known = false;
  for (const LangInfo& l : LANGS)
    if (l.code == s.lang) known = true;
  if (!known) s.lang = d.lang;
  bool powerOk = false;
  for (int p : ENGINE_POWERS)
    if (p == s.power) powerOk = true;
  if (!powerOk) s.power = d.power;
  if (s.view != View::Chase && s.view != View::Cockpit) s.view = d.view;
  if (s.vehicle != VehicleKind::Car && s.vehicle != VehicleKind::Truck) s.vehicle = d.vehicle;
  if (s.transmission != Transmission::Assist && s.transmission != Transmission::Manual &&
      s.transmission != Transmission::Automatic)
    s.transmission = d.transmission;
  if (s.quality != Quality::Low && s.quality != Quality::Medium && s.quality != Quality::High)
    s.quality = d.quality;
  if (!(s.cabinOpacity >= 0.2 && s.cabinOpacity <= 1)) s.cabinOpacity = d.cabinOpacity;
  if (s.lastStart.spawn < 0) s.lastStart.spawn = 0;
  if (s.lastStart.hasAddress &&
      (s.lastStart.label.empty() || !std::isfinite(s.lastStart.x) || !std::isfinite(s.lastStart.z)))
    s.lastStart = d.lastStart;
  return s;
}
}  // namespace

const int ENGINE_POWERS[6] = {60, 75, 95, 115, 150, 200};

Settings defaultSettings() {
  Settings s;
  // JS: typeof navigator !== 'undefined' ? pickLang(navigator.languages …) : 'en'
  // Without a browser navigator no language is reported: English, as the
  // original falls back to it (the native build starts in it too).
  s.lang = pickLang({});
  // what the playtesters asked for: an automatic, seen from behind, with an
  // engine that is easy to dose
  s.vehicle = VehicleKind::Car;
  s.transmission = Transmission::Automatic;
  s.power = 75;
  s.limiter = true;
  s.view = View::Chase;
  s.cabinOpacity = 1;
  s.lastStart = Settings::StartPlace();
  return s;
}

Settings loadSettings() {
  if (!g_hasSaved) return defaultSettings();
  // defaults overlaid with what was stored; implausible values keep the default
  return merge(g_saved);
}

void saveSettings(const Settings& s) {
  // false: this browser keeps nothing (private window, storage switched off)
  g_saved = s;
  g_hasSaved = true;
}

}  // namespace fs

// LINK: game sim render i18n audio input engine world core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The cockpit: the map on the vehicle's screen became the default (stored
 * settings of before are moved over once), the finer vehicles belong to the
 * "very high" graphics, and the mirror key goes round the mirrors a vehicle
 * has (game/cockpit.h).
 *
 * Origin: tests/game/cockpit.test.ts @ v1.7.7 (its last case – what the glass of a mirror
 * shows – is render/mirrors.ts's).
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "game/cockpit.h"
#include "game/settings.h"
#include "platform/json.h"
#include "platform/plat.h"

using namespace fs;

static const char* const KEY = "fahrsim-baunatal.settings.v2";
static const char* const OLD_KEY = "fahrsim-baunatal.settings.v1";

/** A browser's storage, as far as the settings use it: with nothing in it, or with one entry. */
static void storage() {
  plat::localStorage().removeItem(KEY);
  plat::localStorage().removeItem(OLD_KEY);
}
static void storage(const char* key, const std::string& text) {
  storage();
  plat::localStorage().setItem(key, text);
}

using Shown = std::optional<std::string>;
static const Shown NONE;

int main() {
  // the map is on the vehicle's screen by default, and settings are written with their version
  {
    storage();
    const Settings s = loadSettings();
    assert(s.mapOnScreen == true);
    assert(s.version == SETTINGS_VERSION);
    assert(DEFAULT_SETTINGS().mapOnScreen == true);
    storage();
    saveSettings(s);
    Json stored;
    assert(Json::parse(*plat::localStorage().getItem(KEY), stored));
    assert(stored.at("version").number == SETTINGS_VERSION);
  }

  // settings stored before 1.4.0: the old default (map in the corner) is moved over to the new one, once
  {
    // as 1.3.0 stored them: every setting, the map's then default among them, no version
    storage(KEY, R"({"lang":"de","mapOnScreen":false,"quality":"high","volume":0.4})");
    Settings s = loadSettings();
    assert(s.mapOnScreen == true);
    assert(s.version == SETTINGS_VERSION);
    // (what else was chosen stays)
    assert(s.volume == 0.4);
    assert(s.lang == "de");
    // … the player switches it off again: that choice is kept from now on
    s.mapOnScreen = false;
    storage();
    saveSettings(s);
    assert(loadSettings().mapOnScreen == false);
    // and the settings of the versions before the playtest move over the same way
    storage(OLD_KEY, R"({"mapOnScreen":false})");
    assert(loadSettings().mapOnScreen == true);
  }

  // a stored version of the future or nonsense does not break the migration
  {
    storage(KEY, R"({"version":3,"mapOnScreen":false})");
    assert(loadSettings().mapOnScreen == false);
    storage(KEY, R"({"version":"x","mapOnScreen":false})");
    assert(loadSettings().mapOnScreen == true);
    storage(KEY, R"({"version":2,"mapOnScreen":"yes"})");
    assert(loadSettings().mapOnScreen == true);
  }

  // the finer vehicles belong to the "very high" preset only, also for settings stored before they existed
  {
    for (const char* q : {"low", "medium", "high"}) assert(GRAPHICS_PRESETS.find(q)->vehicleDetail == false);
    assert(GRAPHICS_PRESETS.ultra.vehicleDetail == true);
    const auto g = [](const std::string& quality, const std::string& graphics = "") {
      storage(KEY, "{\"version\":2,\"quality\":\"" + quality + "\"" + (graphics.empty() ? "" : ",\"graphics\":" + graphics) + "}");
      return loadSettings().graphics.vehicleDetail;
    };
    // stored graphics of before: as the preset of then would have them
    assert(g("ultra", R"({"shadows":"far","viewDistance":3000,"propDensity":1,"facades":true,"propShadows":true,"farView":20000})") == true);
    assert(g("high", R"({"shadows":"far","viewDistance":2000,"propDensity":1,"facades":true,"propShadows":true,"farView":12000})") == false);
    assert(g("custom", R"({"shadows":"far","viewDistance":2000,"propDensity":1,"facades":true,"propShadows":true,"farView":12000})") == false);
    assert(g("ultra") == true);
    // chosen in the advanced settings: kept, whatever the preset was
    assert(g("custom", R"({"shadows":"near","viewDistance":1250,"propDensity":0.8,"facades":true,"propShadows":false,"farView":8000,"vehicleDetail":true,"water":false,"weatherFx":false})") == true);
    assert(g("custom", R"({"shadows":"far","viewDistance":3000,"propDensity":1,"facades":true,"propShadows":true,"farView":20000,"vehicleDetail":false,"water":true,"weatherFx":false})") == false);
  }
  storage();

  // the mirror key goes round the mirrors a vehicle has: left, right, inside, off
  {
    assert((MIRROR_ORDER == std::vector<std::string>{"left", "right", "inside"}));
    const std::vector<std::string> car = {"inside", "left", "right"};
    MirrorLook look;
    assert(look.shown == NONE);
    assert(look.next(car) == "left");
    assert(look.next(car) == "right");
    assert(look.next(car) == "inside");
    assert(look.next(car) == NONE);
    assert(look.next(car) == "left");
    // a truck, a bus, a tractor: no inside mirror
    const std::vector<std::string> truck = {"left", "right"};
    look.close();
    assert(look.next(truck) == "left");
    assert(look.next(truck) == "right");
    assert(look.next(truck) == NONE);
    // the tram's cab, an aeroplane: nothing to look into
    assert(look.next({}) == NONE);
    assert(look.next({}) == NONE);
  }

  // a tap on a mirror shows it, a second tap puts it away; a mirror the vehicle lacks is shown no more
  {
    const std::vector<std::string> car = {"inside", "left", "right"};
    MirrorLook look;
    assert(look.pick("right", car) == "right");
    assert(look.pick("left", car) == "left");
    assert(look.pick("left", car) == NONE);
    assert(look.pick("inside", {"left", "right"}) == NONE);
    // from the inside mirror the key goes on to "off"
    look.pick("inside", car);
    assert(look.next(car) == NONE);
    // the car's inside mirror shown, then the truck is driven: none
    look.pick("inside", car);
    assert(look.keep({"left", "right"}) == NONE);
    look.pick("left", car);
    assert(look.keep({"left", "right"}) == "left");
  }

  // which mirror a tap means: its glass, or close enough to it on a small screen
  {
    const std::vector<MirrorOnScreen> boxes = {
        {.name = "left", .x0 = 40, .y0 = 300, .x1 = 120, .y1 = 350, .distance = 0.9},
        {.name = "inside", .x0 = 600, .y0 = 40, .x1 = 760, .y1 = 80, .distance = 0.5},
        {.name = "right", .x0 = 1500, .y0 = 320, .x1 = 1540, .y1 = 345, .distance = 1.6},
    };
    assert(mirrorAt(80, 320, boxes) == "left");
    assert(mirrorAt(680, 60, boxes) == "inside");
    // a finger beside the small glass of the right mirror
    assert(mirrorAt(1555, 333, boxes) == "right");
    assert(mirrorAt(1600, 333, boxes) == NONE);
    assert(mirrorAt(900, 500, boxes) == NONE);
    // two near each other: the one the tap is inside, then the nearer one
    const std::vector<MirrorOnScreen> two = {
        {.name = "a", .x0 = 0, .y0 = 0, .x1 = 100, .y1 = 100, .distance = 2},
        {.name = "b", .x0 = 90, .y0 = 0, .x1 = 200, .y1 = 100, .distance = 1},
    };
    assert(mirrorAt(50, 50, two) == "a");
    assert(mirrorAt(95, 50, two) == "b");
    assert(mirrorAt(105, 50, two) == "b");
  }

  // the panel a mirror is shown in fits the upper half of the screen, centred
  {
    // a car's door mirror (17 × 10.5 cm), its inside mirror (15 × 4.2), a truck's tall one (22 × 40)
    assert(std::fabs(zoomAspect(0.17, 0.105) - 0.17 / 0.105) < 1e-9);
    assert(zoomAspect(0.15, 0.042) == 2.6);
    assert(zoomAspect(0.22, 0.4) == 1.4);
    const double screens[4][3] = {{1920, 1080, 10}, {844, 390, 52}, {390, 844, 52}, {1280, 720, 10}};
    for (const auto& screen : screens) {
      const double w = screen[0];
      const double h = screen[1];
      const double top = screen[2];
      for (double aspect : {1.4, 1.62, 2.6}) {
        const PanelRect r = zoomPanel(w, h, aspect, top);
        assert(r.y == top);
        assert(r.y + r.h <= h * 0.5 + top + 1);
        assert(r.w <= w * 0.94 + 1);
        assert(std::fabs(r.x + r.w / 2 - w / 2) <= 1);   // centred
        assert(std::fabs(r.w / r.h - aspect) < 0.02);     // its shape
      }
    }
    // as large as the screen allows: on a desktop down to the middle of it
    assert(zoomPanel(1920, 1080, 1.62, 10).h == 538);
  }

  std::printf("game_cockpit_test: ok\n");
  return 0;
}

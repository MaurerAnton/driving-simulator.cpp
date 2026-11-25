// LINK: game sim render i18n audio input engine world core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * 1.7.1: the weather on the vehicle and the roads (windscreen, wet roads, mud) and the real seasons belong to the
 * new "No mercy" preset only, which never comes by itself; and the simpler physics (even grip) are the default.
 *
 * Origin: tests/game/nomercy.test.ts @ v1.7.7
 */
#include <cassert>
#include <cstdio>
#include <string>

#include "core/js.h"
#include "game/outdoors.h"
#include "game/settings.h"
#include "platform/plat.h"

using namespace fs;

static const char* const KEY = "fahrsim-baunatal.settings.v2";

static void storage() {
  plat::localStorage().removeItem(KEY);
  plat::localStorage().removeItem("fahrsim-baunatal.settings.v1");
}
static void storage(const std::string& text) {
  storage();
  plat::localStorage().setItem(KEY, text);
}

/** the graphics of a preset as 1.7.0 and before stored them: without the weather effects */
static std::string graphics170(const Graphics& g) {
  const auto flag = [](bool b) { return std::string(b ? "true" : "false"); };
  return "{\"shadows\":\"" + g.shadows + "\",\"viewDistance\":" + jsStr(g.viewDistance) + ",\"propDensity\":" + jsStr(g.propDensity) + ",\"facades\":" + flag(g.facades) +
         ",\"propShadows\":" + flag(g.propShadows) + ",\"farView\":" + jsStr(g.farView) + ",\"vehicleDetail\":" + flag(g.vehicleDetail) + ",\"water\":" + flag(g.water) + "}";
}

int main() {
  // only "No mercy" has the weather on the vehicle and the roads; it is never the default
  {
    for (const char* q : {"low", "medium", "high", "ultra"}) assert(GRAPHICS_PRESETS.find(q)->weatherFx == false);
    assert(GRAPHICS_PRESETS.nomercy.weatherFx == true);
    Graphics dry = GRAPHICS_PRESETS.nomercy;
    dry.weatherFx = false;
    assert(dry == GRAPHICS_PRESETS.ultra);
    assert(DEFAULT_SETTINGS().quality != "nomercy");
    storage();
    assert(loadSettings().graphics.weatherFx == false);
  }

  // settings of 1.7.0 and before: "very high" loses the weather effects, a custom choice with the fine vehicles keeps them
  {
    const auto load = [](const std::string& quality, const std::string& graphics) {
      storage("{\"version\":2,\"quality\":\"" + quality + "\",\"graphics\":" + graphics + "}");
      return loadSettings().graphics.weatherFx;
    };
    const std::string ultra170 = graphics170(GRAPHICS_PRESETS.ultra);
    assert(load("ultra", ultra170) == false);
    assert(load("custom", ultra170) == true);
    Graphics plain = GRAPHICS_PRESETS.ultra;
    plain.vehicleDetail = false;
    assert(load("custom", graphics170(plain)) == false);
  }

  // simpler physics by default, also for settings stored before 1.7.1 – a choice made since stays
  {
    assert(DEFAULT_SETTINGS().evenRoads == true);
    storage(R"({"version":2,"evenRoads":false})");
    assert(loadSettings().evenRoads == true);
    storage("{\"version\":" + std::to_string(SETTINGS_VERSION) + ",\"evenRoads\":false}");
    assert(loadSettings().evenRoads == false);
  }

  // always summer unless "No mercy" (a season chosen is shown all the same)
  {
    assert(seasonShown({.weather = "real", .season = "real", .weatherFx = false}) == "summer");
    assert(seasonShown({.weather = "real", .season = "real", .weatherFx = true}) == "real");
    assert(seasonShown({.weather = "real", .season = "winter", .weatherFx = false}) == "winter");
  }

  storage();
  std::printf("game_nomercy_test: ok\n");
  return 0;
}

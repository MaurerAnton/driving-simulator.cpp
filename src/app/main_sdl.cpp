// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The entry point of the game in a window of its own (SDL3, experimental – docs/SDL.md).
 *
 * The page's address is made of the command line: every `--name=value` becomes
 * `?name=value` (`--start=free --vehicle=car` → `?start=free&vehicle=car`), so the game
 * starts as the browser does with that address.  Without --start it starts a free drive
 * (`--start=` with nothing: the menu – which is not painted yet, see docs/SDL.md).
 *
 * The program's own options (not handed to the game):
 *   --region=ID      the region to open (settings: region – its map comes from the network)
 *   --map=DIR        the site's own map (map.json and its tiles); otherwise MAP_DIR, or the
 *                    original's public/map beside the repository
 *   --site=DIR       the site's other files (signs/, parts/, lang/, data/: the repository's web/)
 *   --offline        nothing from the network (only what lies on disk)
 *   --size=WxH       the window's size (1280x720)
 *   --frames=N       stop N pictures after the game is ready
 *   --shot=PATH      … and write the last one as a PNG first (N: 120 when not given)
 *   --mute, --novsync
 */
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "core/js.h"
#include "core/task.h"
#include "game/game.h"
#include "ui/sdlhud.h"
#include "platform/overlay.h"
#include "game/settings.h"
#include "platform/dom.h"
#include "platform/native/native.h"
#include "platform/plat.h"
#include "platform/sdl/sdl_host.h"

using namespace fs;

namespace {

bool isMapDir(const std::string& dir) {
  return !dir.empty() && (std::filesystem::exists(dir + "/map.json") || std::filesystem::exists(dir + "/world.json"));
}

std::string findMap(const std::string& asked) {
  std::vector<std::string> dirs;
  if (!asked.empty()) dirs.push_back(asked);
  if (const char* d = std::getenv("MAP_DIR")) dirs.push_back(d);
  dirs.push_back("public/map");
  dirs.push_back(std::string(FS_SOURCE_DIR) + "/../driving-simulator/public/map");
  if (const char* home = std::getenv("HOME")) dirs.push_back(std::string(home) + "/driving-simulator/public/map");
  for (std::string d : dirs) {
    while (d.size() > 1 && d.back() == '/') d.pop_back();
    if (isMapDir(d)) return d + "/";
  }
  return "";
}

/** the device's languages, as navigator.languages has them ("de-DE", "en") */
std::vector<std::string> deviceLanguages() {
  std::vector<std::string> out;
  int n = 0;
  if (SDL_Locale** locales = SDL_GetPreferredLocales(&n)) {
    for (int i = 0; i < n; i++) {
      if (!locales[i] || !locales[i]->language) continue;
      std::string l = locales[i]->language;
      if (locales[i]->country) out.push_back(l + "-" + locales[i]->country);
      out.push_back(l);
    }
    SDL_free(locales);
  }
  if (out.empty()) out = {"de-DE", "de", "en"};
  return out;
}

/** game.boot, and what went wrong (main_web.cpp's bootGame) */
Task<void> boot(Game* game, std::string search, std::string* failed) {
  try {
    co_await game->boot(URLSearchParams(search));
  } catch (const std::exception& e) {
    *failed = e.what();
  }
}

int usage() {
  std::printf(
      "fahrsim-sdl [--start=free] [--vehicle=car] [--region=ID] [--lang=de] [--NAME=VALUE …]\n"
      "            [--map=DIR] [--site=DIR] [--offline] [--size=WxH] [--frames=N] [--shot=PATH] [--mute] [--novsync]\n"
      "Every --NAME=VALUE the program does not take itself is handed to the game as ?NAME=VALUE.\n");
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::string search, mapAsked, site = std::string(FS_SOURCE_DIR) + "/web", region, shot;
  bool offline = false, startGiven = false;
  int frames = -1;
  sdl::Options options;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help") return usage();
    if (a.rfind("--", 0) != 0) {
      std::fprintf(stderr, "fahrsim-sdl: what is %s? (--help)\n", a.c_str());
      return 2;
    }
    a = a.substr(2);
    const size_t eq = a.find('=');
    const std::string name = a.substr(0, eq), value = eq == std::string::npos ? "" : a.substr(eq + 1);
    if (name == "map") mapAsked = value;
    else if (name == "site") site = value;
    else if (name == "region") region = value;
    else if (name == "offline") offline = true;
    else if (name == "mute") options.audio = false;
    else if (name == "novsync") options.vsync = false;
    else if (name == "frames") frames = std::atoi(value.c_str());
    else if (name == "shot") shot = value;
    else if (name == "size") std::sscanf(value.c_str(), "%dx%d", &options.width, &options.height);
    else {
      if (name == "start") startGiven = true;
      search += (search.empty() ? "?" : "&") + encodeURIComponent(name) + (eq == std::string::npos ? "" : "=" + encodeURIComponent(value));
    }
  }
  if (!startGiven) search += std::string(search.empty() ? "?" : "&") + "start=free";
  if (!shot.empty() && frames < 0) frames = 120;

  std::string why;
  if (!sdl::open(options, &why)) {
    std::fprintf(stderr, "fahrsim-sdl: no window (%s)\n", why.c_str());
    return 1;
  }
  std::printf("fahrsim-sdl: %s, drawn by %s; the address: %s\n", sdl::videoDriver().c_str(), sdl::rendererName().c_str(), search.c_str());

  // the site: its files from disk, the map from its folder, the rest (regions) from the network
  plat::native::setSearch(search);
  plat::native::mount(plat::locationBase(), site);
  const std::string mapDir = findMap(mapAsked);
  if (!mapDir.empty()) plat::native::mount(plat::locationBase() + "map/", mapDir);
  else std::fprintf(stderr, "fahrsim-sdl: no map on disk (--map=DIR or MAP_DIR): only a region (--region) can be opened\n");
  plat::native::useNetwork(!offline);
  plat::native::setLanguages(deviceLanguages());
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
  plat::native::setCoarsePointer(true);
#endif
  {
    // (no guess of the region from the IP address: the region is asked for on the command line, or the site's map opens)
    Settings s = loadSettings();
    s.regionAsked = true;
    if (!region.empty()) s.region = region;
    saveSettings(s);
  }

  dom::El canvas = dom::el("canvas"), ui = dom::el("div");
  canvas.id("view");
  ui.id("ui");
  dom::body().append(canvas, ui);
  sdl::attach(canvas);

  int status = 0;
  {
    std::unique_ptr<Game> game;
    const void* hudShown = nullptr;
    try {
      game = std::make_unique<Game>(canvas, ui);
    } catch (const std::exception& e) {
      std::fprintf(stderr, "fahrsim-sdl: the game could not be made: %s\n", e.what());
      sdl::close();
      return 1;
    }
    std::string failed;
    boot(game.get(), search, &failed).detach();
    long readyAt = -1;
    while (sdl::frame()) {
      if (!failed.empty()) {
        std::fprintf(stderr, "fahrsim-sdl: the game did not start: %s\n", failed.c_str());
        status = 1;
        break;
      }
      // the small display drawn over the picture (ui/sdlhud: the street, the speed, the tips, the corner map):
      // as soon as boot has made the Hud – boot itself goes on a while after the game is ready
      if (game->hud && game->hud.get() != (const void*)hudShown) {
        installSdlHud(*game->hud);
        hudShown = game->hud.get();
      }
      if (game->ready && readyAt < 0) {
        readyAt = sdl::picturesShown();
        std::printf("fahrsim-sdl: ready after %ld pictures (%s)\n", readyAt, game->state.c_str());
      }
      if (frames >= 0 && readyAt >= 0 && sdl::picturesShown() - readyAt >= frames) {
        if (!shot.empty()) std::printf("fahrsim-sdl: %s %s\n", sdl::screenshot(shot) ? "wrote" : "could not write", shot.c_str());
        break;
      }
    }
    // (the game's GL objects go while the context is still there)
    sdlOverlayHook = nullptr;   // (it reads the Hud that goes with the game)
    game.reset();
  }
  sdl::close();
  return status;
}

// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The game in an SDL3 window (platform/sdl, cmake -DFS_SDL3=ON): a window opens, the game
 * boots with ?start=free on the real map, and 120 pictures are drawn – without an OpenGL
 * error, and with the game's frames running.  Skipped where no window can be had (no
 * display: SDL_Init(SDL_INIT_VIDEO) fails – SDL_VIDEODRIVER=offscreen draws without one)
 * and where the map is not on disk.
 */
#include <GLES3/gl3.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "game/game.h"
#include "game/settings.h"
#include "platform/dom.h"
#include "platform/native/native.h"
#include "platform/plat.h"
#include "platform/sdl/sdl_host.h"

using namespace fs;

int main() {
  std::string mapDir = "/home/user/driving-simulator/public/map/";
  if (const char* d = std::getenv("MAP_DIR")) mapDir = std::string(d) + "/";
  if (!std::filesystem::exists(mapDir + "map.json")) {
    std::printf("SKIP: no map at %s\n", mapDir.c_str());
    return 0;
  }
  sdl::Options options;
  options.width = 640;
  options.height = 360;
  options.audio = false;
  options.vsync = false;
  std::string why;
  if (!sdl::open(options, &why)) {
    std::printf("SKIP: no window here (%s)\n", why.c_str());
    return 0;
  }
  std::printf("  window: %s, %s\n", sdl::videoDriver().c_str(), sdl::rendererName().c_str());
  plat::native::setSearch("?start=free");
  plat::native::mount(plat::locationBase(), std::string(FS_SOURCE_DIR) + "/web");
  plat::native::mount(plat::locationBase() + "map/", mapDir);
  plat::native::setLanguages({"de"});
  {
    Settings s = loadSettings();
    s.regionAsked = true;
    saveSettings(s);
  }
  dom::El canvas = dom::el("canvas"), ui = dom::el("div");
  dom::body().append(canvas, ui);
  sdl::attach(canvas);

  int failures = 0;
  {
    Game game(canvas, ui);
    game.boot(URLSearchParams("?start=free")).detach();
    // the map is opened and the loop begins (in real time: give it a minute at most)
    const double until = plat::nowMs() + 60000;
    while (!game.looping && plat::nowMs() < until && sdl::frame()) {
    }
    if (!game.looping) {
      std::printf("FAIL: the game's loop did not begin (state %s)\n", game.state.c_str());
      failures++;
    }
    while (glGetError() != GL_NO_ERROR) {
    }
    const long before = sdl::callbacksRun();
    for (int i = 0; i < 120; i++) sdl::frame();
    const GLenum err = glGetError();
    const long ran = sdl::callbacksRun() - before;
    std::printf("  120 pictures: %ld frames of the game ran, state %s\n", ran, game.state.c_str());
    if (err != GL_NO_ERROR) {
      std::printf("FAIL: OpenGL error 0x%x\n", err);
      failures++;
    }
    if (ran < 100) {
      std::printf("FAIL: only %ld of 120 frames ran\n", ran);
      failures++;
    }
  }
  sdl::close();
  std::printf(failures ? "sdl_smoke_test: %d failures\n" : "sdl_smoke_test: ok\n", failures);
  return failures ? 1 : 0;
}

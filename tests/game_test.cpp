// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The hub (src/game/game.ts @ v1.7.7) on the native host.  The original has no test of its own
 * that drives the Game (its tests take the layers one by one; tests/sim/limitmatch.test.ts only
 * names the game's speed limiter), so this is a smoke test beyond them:
 *
 *   boot on the real map (public/map of the original, mounted as the page's map/), with ?start=free;
 *   the car is placed on a road of the start place (state "placing" → "drive");
 *   some seconds with the throttle down (the key W, as the keyboard gives it);
 *   the car has moved ahead the way it faced, it is still on a lane, and the game says so in debugInfo().
 *
 * It needs an OpenGL ES context off screen (tools/dev/view.cpp brings one up the same way).
 */
#include <filesystem>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

#include "game/game.h"
#include "input/input.h"
#include "platform/dom.h"
#include "platform/glhost.h"
#include "platform/native/native.h"
#include "platform/plat.h"
#include "sim/vehicle/index.h"
#include "world/lanes.h"

using namespace fs;

static int failures = 0;
#define CHECK(cond, ...)                         \
  do {                                           \
    if (!(cond)) {                               \
      std::printf("FAIL %s: ", #cond);           \
      std::printf(__VA_ARGS__);                  \
      std::printf("\n");                         \
      failures++;                                \
    }                                            \
  } while (0)

int main() {
  const std::string mapDir = "/home/user/driving-simulator/public/map/";
  // (the real map is only on the machine the port is made on: elsewhere – CI – nothing to test)
  if (!std::filesystem::exists(mapDir + "map.json")) {
    std::printf("SKIP: no map at %s\n", mapDir.c_str());
    return 0;
  }
  plat::native::mount(plat::locationBase() + "map/", mapDir);
  plat::native::setViewport(640, 360, 1);
  plat::native::setLanguages({"de"});
  if (!plat::gl::createContext("game_test", 640, 360)) {
    std::printf("SKIP: no OpenGL ES here\n");
    return 0;
  }

  Game game(dom::el("canvas"), dom::el("div"));
  plat::native::wait(game.boot(URLSearchParams("?start=free")));
  // the test steps the pictures itself
  game.looping = false;
  double now = plat::nowMs();
  const auto picture = [&]() {
    plat::native::pump();
    now += 1000.0 / 60;
    game.frame(now);
  };

  // placing: the roads and the tiles round the start place are fetched, then the car is put on a road
  for (int i = 0; i < 20000 && game.state != "drive"; i++) picture();
  CHECK(game.state == "drive", "state %s after placing", game.state.c_str());
  if (game.state != "drive") return 1;
  CHECK(game.vehicle != nullptr, "no vehicle");
  Lane* startLane = game.currentLane();
  // (on a street the car stands in a lane; the first start place is a car park, where it stands in none – as in 1.7.7)
  if (game.spawn && game.spawn->kind == "street") CHECK(startLane != nullptr, "the car was not put on a lane");
  // (settled: a second standing)
  for (int i = 0; i < 60; i++) picture();
  const double x0 = game.mpos[0], z0 = game.mpos[2];
  const double yaw0 = game.vehicle->yaw;
  std::printf("placed at %.1f, %.1f facing %.0f°, lane %d\n", x0, z0, yaw0 * 180 / M_PI, startLane ? startLane->id : -1);

  // the throttle down for five seconds (the free drive holds the car until the driver gives gas)
  game.input->press("KeyW", true);
  double top = 0;
  for (int i = 0; i < 300; i++) {
    picture();
    top = std::max(top, game.vehicle->speed);
  }
  game.input->press("KeyW", false);
  const double dx = game.mpos[0] - x0, dz = game.mpos[2] - z0;
  const double moved = std::hypot(dx, dz);
  // the car looks along -z turned by its yaw (the chase camera stands at +sin, +cos behind it)
  const double ahead = -dx * std::sin(yaw0) - dz * std::cos(yaw0);
  std::printf("moved %.1f m (%.1f ahead), top %.1f km/h\n", moved, ahead, top * 3.6);
  CHECK(top > 3, "top speed %.2f m/s", top);
  CHECK(moved > 8, "moved %.1f m", moved);
  CHECK(ahead > 0.8 * moved, "%.1f m of %.1f m ahead", ahead, moved);
  Lane* endLane = game.currentLane();
  CHECK(endLane != nullptr, "the car left the lanes");

  const std::string info = game.debugInfo();
  std::printf("%s\n", info.c_str());
  CHECK(info.find("\"state\":\"drive\"") != std::string::npos, "debugInfo: %s", info.c_str());

  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("game: ok\n");
  return 0;
}

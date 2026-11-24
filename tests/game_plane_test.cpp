// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The aeroplane in the real game (src/game/game.ts @ v1.7.7: begin, tryPlace, startPlane, stepFlight,
 * toggleFlightAuto, planeVisual): the original has no test that flies in the Game, so this is a smoke
 * test beyond tests/flight_test.cpp and tests/flight_autopilot_test.cpp:
 *
 *   boot on the real map with ?vehicle=plane&start=free – the flight waits for the airfields, then
 *   stands at the end of the map's first runway (state "placing" → "drive"), on its wheels;
 *   the autopilot's key (as the demonstration button): a demonstration flight from that runway –
 *   it rolls, lifts off and climbs; the model and its instruments follow the flight.
 *
 * It needs an OpenGL ES context off screen (as tests/game_test.cpp).
 */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "game/game.h"
#include "input/input.h"
#include "platform/dom.h"
#include "platform/glhost.h"
#include "platform/native/native.h"
#include "platform/plat.h"
#include "render/plane.h"
#include "sim/autopilot.h"
#include "sim/flight.h"
#include "world/types.h"

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
  if (!std::filesystem::exists(mapDir + "map.json") || !std::filesystem::exists(mapDir + "air.json.gz")) {
    std::printf("SKIP: no map with airfields at %s\n", mapDir.c_str());
    return 0;
  }
  plat::native::mount(plat::locationBase() + "map/", mapDir);
  plat::native::setViewport(640, 360, 1);
  plat::native::setLanguages({"de"});
  if (!plat::gl::createContext("game_plane_test", 640, 360)) {
    std::printf("SKIP: no OpenGL ES here\n");
    return 0;
  }

  Game game(dom::el("canvas"), dom::el("div"));
  plat::native::wait(game.boot(URLSearchParams("?vehicle=plane&start=free")));
  game.looping = false;
  double now = plat::nowMs();
  const auto picture = [&]() {
    plat::native::pump();
    now += 1000.0 / 60;
    game.frame(now);
  };

  // placing: the airfields are read, the tiles round the runway's end fetched, the aeroplane put there
  for (int i = 0; i < 20000 && game.state != "drive"; i++) picture();
  CHECK(game.state == "drive", "state %s after placing", game.state.c_str());
  if (game.state != "drive") return 1;
  CHECK(game.air != nullptr && !game.air->airfields.empty(), "the airfields were not read");
  CHECK(game.aircraft != nullptr, "no aeroplane");
  CHECK(game.planeModel != nullptr && game.planeModel->root->visible, "the aeroplane is not drawn");
  if (!game.aircraft) return 1;
  for (int i = 0; i < 60; i++) picture();
  Aircraft& a = *game.aircraft;
  const double x0 = a.pos.x, z0 = a.pos.z, alt0 = a.altitude();
  std::printf("on the runway %s at %.1f, %.1f, %.0f m, heading %.0f°\n", game.spawn ? game.spawn->name.c_str() : "?", x0, z0, alt0, a.heading());
  CHECK(a.onGround(), "not on its wheels");
  CHECK(!a.crashed, "crashed standing");
  // the game's vehicle stands below it: the map and the tiles keep to it
  CHECK(std::hypot(game.mpos[0] - x0, game.mpos[2] - z0) < 0.5, "the proxy is not below the aeroplane");

  // the autopilot's key: a demonstration flight from this runway
  game.onAction("reset");
  CHECK(game.flightAuto->engaged(), "the autopilot did not engage on the runway");
  double top = 0, high = alt0;
  bool airborne = false;
  for (int i = 0; i < 60 * 70 && !a.crashed; i++) {
    picture();
    top = std::max(top, a.ias());
    high = std::max(high, a.altitude());
    if (!a.onGround()) airborne = true;
    if (high - alt0 > 120) break;
  }
  std::printf("autopilot %s: top %.1f m/s, %.0f m above the runway, %.0f m from the start, %s\n", game.flightAuto->state.phase.c_str(), top, high - alt0,
              std::hypot(a.pos.x - x0, a.pos.z - z0), a.crashed ? crashKindName(*a.crashed) : "whole");
  CHECK(!a.crashed, "crashed");
  CHECK(airborne, "never lifted off");
  CHECK(top > TRAINER.vr, "never reached the rotation speed: %.1f", top);
  CHECK(high - alt0 > 50, "climbed only %.0f m", high - alt0);
  CHECK(std::hypot(game.mpos[0] - a.pos.x, game.mpos[2] - a.pos.z) < 0.5, "the proxy did not follow");
  // the model where the flight has it
  CHECK(game.planeModel->root->position.distanceTo(Vector3(a.pos.x, a.pos.y, a.pos.z)) < 1, "the model is not where the aeroplane is");

  // the autopilot off by its key: flown by hand from here
  game.onAction("reset");
  CHECK(!game.flightAuto->engaged(), "the autopilot did not let go");
  for (int i = 0; i < 120; i++) picture();
  CHECK(!a.crashed, "crashed after the autopilot let go");

  const std::string info = game.debugInfo();
  CHECK(info.find("\"state\":\"drive\"") != std::string::npos, "debugInfo: %s", info.c_str());

  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("game_plane: ok\n");
  return 0;
}

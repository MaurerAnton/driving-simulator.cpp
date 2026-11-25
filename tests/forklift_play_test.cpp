// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The forklift in the game (src/game/forkliftplay.h, the hub's forklift parts – game.ts makeForkliftWork,
 * stepForklift …) on the native host.  The original has no test that drives the forklift in the game
 * (tests/sim/forklift.test.ts and tests/game/forkliftmodes.test.ts take the parts one by one), so this is a
 * smoke test beyond them:
 *
 *   boot on the real map with the forklift chosen, "Forklift work": the drive begins at the forklift's start on
 *   the practice yard, the place is laid out once its ground is there, the guided start is the task; Page Up
 *   lifts the forks, the gear in D and W drive it forwards, its model is the forklift's; the camera key goes on
 *   to the operator's seat; G gets the operator off beside the seat, G again on.
 *
 * It needs an OpenGL ES context off screen (tools/dev/view.cpp brings one up the same way).
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "game/forkliftplay.h"
#include "game/game.h"
#include "game/onfoot.h"
#include "input/input.h"
#include "platform/dom.h"
#include "platform/glhost.h"
#include "platform/native/dom_native.h"
#include "platform/native/native.h"
#include "platform/plat.h"
#include "render/forklift.h"
#include "sim/vehicle/vehicle.h"

using namespace fs;
namespace nd = fs::dom::native;

static int failures = 0;
#define CHECK(cond, ...)               \
  do {                                 \
    if (!(cond)) {                     \
      std::printf("FAIL %s: ", #cond); \
      std::printf(__VA_ARGS__);        \
      std::printf("\n");               \
      failures++;                      \
    }                                  \
  } while (0)

int main() {
  const std::string mapDir = "/home/user/driving-simulator/public/map/";
  if (!std::filesystem::exists(mapDir + "map.json")) {
    std::printf("SKIP: no map at %s\n", mapDir.c_str());
    return 0;
  }
  plat::native::mount(plat::locationBase() + "map/", mapDir);
  plat::native::setViewport(640, 360, 1);
  plat::native::setLanguages({"de"});
  if (!plat::gl::createContext("forklift_play_test", 640, 360)) {
    std::printf("SKIP: no OpenGL ES here\n");
    return 0;
  }

  Game game(dom::el("canvas"), dom::body());
  game.settings.vehicle = "forklift";
  plat::native::wait(game.boot(URLSearchParams("?start=free")));
  game.looping = false;
  double now = plat::nowMs();
  const auto picture = [&]() {
    plat::native::pump();
    now += 1000.0 / 60;
    game.frame(now);
  };
  // "Forklift work" (FORK_WORK_MODE) from the menu
  game.toMenu();
  game.begin("forklift", 0, true, nullptr);
  for (int i = 0; i < 20000 && game.state != "drive"; i++) picture();
  CHECK(game.state == "drive", "state %s after placing", game.state.c_str());
  if (game.state != "drive") return 1;
  CHECK(game.def->kind == "forklift", "driven: %s", game.def->kind.c_str());
  CHECK(game.forklift != nullptr, "no forklift work");
  if (!game.forklift) return 1;
  ForkliftWork& fw = *game.forklift;
  CHECK(fw.siteOn, "the work on the yard");
  CHECK(dynamic_cast<ForkliftModel*>(&game.model()) != nullptr, "the model is the forklift's");
  CHECK(game.cameraMode == "chase" && std::abs(game.input->orbitRest - FORK_SIDE_YAW) < 1e-9, "the view from the side: %s", game.cameraMode.c_str());
  const std::optional<ForkPlace> at = forkliftStart(game.yardFrame ? &*game.yardFrame : nullptr);
  CHECK(at.has_value(), "the map has its practice yard");
  if (at) CHECK(std::hypot(game.mpos[0] - at->x, game.mpos[2] - at->z) < 3, "placed %.1f, %.1f – the start is at %.1f, %.1f", game.mpos[0], game.mpos[2], at->x, at->z);
  for (int i = 0; i < 6000 && !fw.tasks; i++) picture();
  CHECK(fw.tasks != nullptr, "the place was not laid out");
  if (fw.tasks) CHECK(fw.tasks->stage == "guide", "the guided start first: %s", fw.tasks->stage.c_str());
  for (int i = 0; i < 30; i++) picture();
  // Page Up: the forks up
  const double lift0 = fw.f.lift;
  nd::key("PageUp");
  for (int i = 0; i < 60; i++) picture();
  nd::key("PageUp", "", false);
  for (int i = 0; i < 10; i++) picture();
  std::printf("lift %.3f → %.3f\n", lift0, fw.f.lift);
  CHECK(fw.f.lift > lift0 + 0.2, "the forks did not rise: %.3f → %.3f", lift0, fw.f.lift);
  // down again, then forwards: the brake off, D, the pedal
  nd::key("PageDown");
  for (int i = 0; i < 90; i++) picture();
  nd::key("PageDown", "", false);
  game.input->c.handbrake = 0;
  game.input->c.gear = 1;
  const double x0 = fw.f.x, z0 = fw.f.z;
  nd::key("KeyW");
  for (int i = 0; i < 90; i++) picture();
  nd::key("KeyW", "", false);
  for (int i = 0; i < 60; i++) picture();
  const double moved = std::hypot(fw.f.x - x0, fw.f.z - z0);
  std::printf("moved %.2f m, speed %.2f\n", moved, fw.f.v);
  CHECK(moved > 0.5, "it did not drive: %.2f m", moved);
  CHECK(std::hypot(game.mpos[0] - fw.f.x, game.mpos[2] - fw.f.z) < 0.01, "the vehicle follows the forklift");
  // the camera key: the operator's seat next
  game.onAction("view");
  CHECK(fw.view == "cockpit" && game.cameraMode == "cockpit", "view %s, camera %s", fw.view.c_str(), game.cameraMode.c_str());
  // stopped: off beside the seat, and on again
  game.input->c.handbrake = 1;
  for (int i = 0; i < 120; i++) picture();
  game.onAction("door");
  CHECK(game.walker != nullptr, "off the forklift");
  for (int i = 0; i < 10; i++) picture();
  game.onAction("door");
  CHECK(game.walker == nullptr, "on again");
  const std::string info = game.debugInfo();
  CHECK(info.find("\"state\":\"drive\"") != std::string::npos, "debugInfo: %s", info.c_str());

  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("forklift_play: ok\n");
  return 0;
}

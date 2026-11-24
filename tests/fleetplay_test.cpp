// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The hub with the vehicles of group "vehicles" (src/game/game.ts @ v1.7.7: makeModel, nextTask …
 * updateTask) on the native host – a smoke test beyond the mirrored ones (ce_test, semitrailer_test,
 * fleet_test, truck_test), as tests/game_test.cpp is for the car:
 *
 *   boot on the real map with ?start=ceCorner&vehicle=truck: the truck of class CE is placed on the
 *   practice yard at the corner's start with its trailer in line behind it, the task runs, the yard
 *   is laid out and the display says what to do;
 *   then the lesson is left for a free drive with the bus: the bus is drawn with its own model (not
 *   the car), the truck and its trailer are hidden, and the bus moves ahead with the throttle down.
 *
 * It needs an OpenGL ES context off screen (as game_test).
 */
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "game/game.h"
#include "game/yardtask.h"
#include "input/input.h"
#include "platform/dom.h"
#include "platform/glhost.h"
#include "platform/native/native.h"
#include "platform/plat.h"
#include "render/fleet.h"
#include "render/truck.h"
#include "sim/trailer.h"
#include "sim/vehicle/index.h"

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
  if (!std::filesystem::exists(mapDir + "map.json")) {
    std::printf("SKIP: no map at %s\n", mapDir.c_str());
    return 0;
  }
  plat::native::mount(plat::locationBase() + "map/", mapDir);
  plat::native::setViewport(640, 360, 1);
  plat::native::setLanguages({"de"});
  if (!plat::gl::createContext("fleetplay_test", 640, 360)) {
    std::printf("SKIP: no OpenGL ES here\n");
    return 0;
  }

  Game game(dom::el("canvas"), dom::el("div"));
  plat::native::wait(game.boot(URLSearchParams("?start=ceCorner&vehicle=truck&traffic=0")));
  game.looping = false;
  double now = plat::nowMs();
  const auto picture = [&]() {
    plat::native::pump();
    now += 1000.0 / 60;
    game.frame(now);
  };

  // --- class CE: the corner on the practice yard ---
  for (int i = 0; i < 20000 && game.state != "drive"; i++) picture();
  CHECK(game.state == "drive", "state %s after placing", game.state.c_str());
  if (game.state != "drive") return 1;
  CHECK(game.def->kind == "truck", "vehicle %s", game.def->kind.c_str());
  CHECK(game.yardFrame.has_value(), "the map has no yard");
  CHECK(game.task != nullptr, "no task on the yard");
  CHECK(game.trailer != nullptr, "no trailer");
  CHECK(game.truck != nullptr && game.truck->root->visible && game.truck->trailer->visible, "the truck and its trailer are not shown");
  CHECK(!game.car.root->visible, "the car is shown");
  for (int i = 0; i < 120; i++) picture();
  if (game.task && game.trailer && game.vehicle) {
    CHECK(game.task->run->id == "corner", "task %s", game.task->run->id.c_str());
    CHECK(game.task->laid, "the yard is not laid out");
    CHECK(!game.lessonLine.empty(), "nothing to do is said");
    // the trailer hangs behind the truck: its axles some 9 m back, on the line the truck faces
    const Vehicle& v = *game.vehicle;
    const Trailer& t = *game.trailer;
    const double d = std::hypot(t.x - game.mpos[0], t.z - game.mpos[2]);
    std::printf("truck at %.1f, %.1f; trailer %.1f m behind; task \"%s\" – %s\n", game.mpos[0], game.mpos[2], d, game.lessonLine.c_str(), game.taskSub.c_str());
    CHECK(t.coupled, "the trailer is not coupled");
    CHECK(d > 6 && d < 14, "the trailer is %.1f m away", d);
    const double behind = (t.x - game.mpos[0]) * std::sin(v.yaw) + (t.z - game.mpos[2]) * std::cos(v.yaw);
    CHECK(behind > 0.9 * d, "the trailer is not behind the truck (%.1f of %.1f m)", behind, d);
  }

  // --- the city bus, in a free drive ---
  game.toMenu();
  CHECK(game.task == nullptr, "the task is kept in the menu");
  game.settings.vehicle = "bus";
  game.begin("free", 0, true);
  for (int i = 0; i < 20000 && game.state != "drive"; i++) picture();
  CHECK(game.state == "drive", "state %s after placing the bus", game.state.c_str());
  if (game.state != "drive") return 1;
  CHECK(game.def->kind == "bus", "vehicle %s", game.def->kind.c_str());
  CHECK(dynamic_cast<PlayerBus*>(&game.model()) != nullptr, "the bus is not drawn as the bus");
  CHECK(game.model().root->visible && !game.car.root->visible, "the bus's model is not the one shown");
  CHECK(!game.truck || (!game.truck->root->visible && !game.truck->trailer->visible), "the truck is still shown");
  CHECK(game.trailer == nullptr, "the bus has a trailer");
  for (int i = 0; i < 60; i++) picture();
  const double x0 = game.mpos[0], z0 = game.mpos[2];
  game.input->press("KeyW", true);
  double top = 0;
  for (int i = 0; i < 360; i++) {
    picture();
    top = std::max(top, game.vehicle->speed);
  }
  game.input->press("KeyW", false);
  const double moved = std::hypot(game.mpos[0] - x0, game.mpos[2] - z0);
  std::printf("the bus moved %.1f m, top %.1f km/h\n", moved, top * 3.6);
  CHECK(moved > 5, "the bus moved %.1f m", moved);

  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("fleetplay: ok\n");
  return 0;
}

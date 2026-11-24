// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The motorcycle in the game (2.0, beyond the original's tests – as game_test is): boot on the real map
 * with ?vehicle=motorcycle (class A) and ?quality=ultra (the finer car of the very high graphics is made
 * with the settings), the machine is placed on a road and drawn by render/bike's PlayerBike of its class;
 * ridden for some seconds it moves ahead, the rider's camera sits on it, and through a bend the drawn
 * lean goes into the bend.  Then the car's parts that open are worked through the game's OpenPlay:
 * the bonnet opened, stepped as the game steps it – the car's model shows it open.
 *
 * It needs an OpenGL ES context off screen and the original's map (else it skips, as game_test does).
 */
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
#include "render/bike.h"
#include "render/caropen.h"
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
  if (!plat::gl::createContext("game_bike_test", 640, 360)) {
    std::printf("SKIP: no OpenGL ES here\n");
    return 0;
  }

  Game game(dom::el("canvas"), dom::el("div"));
  plat::native::wait(game.boot(URLSearchParams("?start=free&vehicle=motorcycle&quality=ultra")));
  game.looping = false;
  double now = plat::nowMs();
  const auto picture = [&]() {
    plat::native::pump();
    now += 1000.0 / 60;
    game.frame(now);
  };

  for (int i = 0; i < 20000 && game.state != "drive"; i++) picture();
  CHECK(game.state == "drive", "state %s after placing", game.state.c_str());
  if (game.state != "drive") return 1;
  CHECK(game.def && game.def->kind == "motorcycle", "the vehicle is %s", game.def ? game.def->kind.c_str() : "none");
  PlayerBike* bike = dynamic_cast<PlayerBike*>(&game.model());
  CHECK(bike != nullptr, "the motorcycle is not drawn by PlayerBike");
  if (!bike) return 1;
  CHECK(bike->variant == "big", "class A rides the big one, not %s", bike->variant.c_str());
  CHECK(bike->root->parent != nullptr, "the motorcycle's model is not in the scene");
  CHECK(game.settings.graphics.vehicleDetail, "?quality=ultra: the finer vehicles");

  for (int i = 0; i < 60; i++) picture();
  const double x0 = game.mpos[0], z0 = game.mpos[2];
  const double yaw0 = game.vehicle->yaw;
  std::printf("placed at %.1f, %.1f facing %.0f°\n", x0, z0, yaw0 * 180 / M_PI);

  // the throttle open for five seconds
  game.input->press("KeyW", true);
  double top = 0;
  for (int i = 0; i < 300; i++) {
    picture();
    top = std::max(top, game.vehicle->speed);
  }
  const double dx = game.mpos[0] - x0, dz = game.mpos[2] - z0;
  const double moved = std::hypot(dx, dz);
  const double ahead = -dx * std::sin(yaw0) - dz * std::cos(yaw0);
  std::printf("moved %.1f m (%.1f ahead), top %.1f km/h\n", moved, ahead, top * 3.6);
  CHECK(top > 3, "top speed %.2f m/s", top);
  CHECK(moved > 8, "moved %.1f m", moved);
  CHECK(ahead > 0.8 * moved, "%.1f m of %.1f m ahead", ahead, moved);
  // the rider's camera: on the machine, about the height of his eyes above the road
  if (game.cameraMode == "cockpit") {
    const Vector3& cp = game.app.camera->position;
    const double off = std::hypot(cp.x - game.mpos[0], cp.z - game.mpos[2]);
    std::printf("the rider's eye %.2f m from the machine's origin, %.2f m above it\n", off, cp.y - game.mpos[1]);
    CHECK(off < 1.5 && cp.y - game.mpos[1] > 0.5 && cp.y - game.mpos[1] < 2, "the eye is off the machine");
  }

  // a bend to the right: the drawn lean goes into it (to the right: negative rotation about z)
  game.input->press("KeyD", true);
  double lean = 0;
  for (int i = 0; i < 90; i++) {
    picture();
    lean = std::max(lean, bike->leaning());
  }
  game.input->press("KeyD", false);
  game.input->press("KeyW", false);
  std::printf("leaning %.1f° in the bend at %.1f km/h\n", lean * 180 / M_PI, game.vehicle->speed * 3.6);
  CHECK(std::isfinite(bike->leaning()), "the lean is not a number");
  CHECK(lean > 0.02, "no lean into the right-hand bend: %.3f", lean);

  // the car's bonnet, opened through the game's parts that open and drawn by the car's model
  game.opens.openings.toggle("bonnet");
  for (int i = 0; i < 120; i++) game.opens.step(1.0 / 60, [](const std::string&) {});
  CHECK(game.opens.openings.get("bonnet") > 0.99, "the bonnet stands open: %.2f", game.opens.openings.get("bonnet"));
  // (the car's body is cut into its parts the first time one opens: more objects under its root)
  const auto count = [&]() {
    int n = 0;
    game.car.root->traverse([&n](Object3D&) { n++; });
    return n;
  };
  const int before = count();
  game.opens.pose(&game.car);
  const int after = count();
  std::printf("the car's objects: %d shut, %d with the bonnet open\n", before, after);
  CHECK(after > before, "the car's parts that open were not made (%d → %d)", before, after);

  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("game_bike: ok\n");
  return 0;
}

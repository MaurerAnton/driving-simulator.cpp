// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The cranes in the game (src/game/craneplay.h, the hub's crane parts – game.ts makeCraneWork, stepCrane …)
 * on the native host.  The original has no test that works a crane in the game (tests/sim/crane.test.ts and
 * tests/game/cranetasks.test.ts take the parts one by one), so this is a smoke test beyond them:
 *
 *   boot on the real map with the tower crane chosen, ?start=free: the drive begins on the construction
 *   site of the practice yard, the crane is worked at once (its own keys), the site is laid out once its
 *   ground is there; A slews the jib, W hoists; the street-name block says what to do;
 *   then the mobile crane: it begins at the depot, is driven, and cannot be worked before it stands.
 *
 * It needs an OpenGL ES context off screen (tools/dev/view.cpp brings one up the same way).
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "game/craneplay.h"
#include "game/game.h"
#include "input/input.h"
#include "platform/dom.h"
#include "platform/glhost.h"
#include "platform/native/dom_native.h"
#include "platform/native/native.h"
#include "platform/plat.h"
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
  // (the real map is only on the machine the port is made on: elsewhere – CI – nothing to test)
  if (!std::filesystem::exists(mapDir + "map.json")) {
    std::printf("SKIP: no map at %s\n", mapDir.c_str());
    return 0;
  }
  plat::native::mount(plat::locationBase() + "map/", mapDir);
  plat::native::setViewport(640, 360, 1);
  plat::native::setLanguages({"de"});
  if (!plat::gl::createContext("crane_play_test", 640, 360)) {
    std::printf("SKIP: no OpenGL ES here\n");
    return 0;
  }

  Game game(dom::el("canvas"), dom::body());
  game.settings.vehicle = "towerCrane";
  plat::native::wait(game.boot(URLSearchParams("?start=free")));
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
  CHECK(game.def->kind == "towerCrane", "driven: %s", game.def->kind.c_str());
  CHECK(game.crane != nullptr, "no crane work");
  if (!game.crane) return 1;
  CraneWork& cw = *game.crane;
  CHECK(cw.operating, "the tower crane is worked at once");
  CHECK(game.input->crane, "the keys are the crane's");
  CHECK(game.yardFrame.has_value(), "the map has its practice yard");
  // the drive begins at the tower crane's base on the yard
  const std::optional<CranePlace> at = craneStart("tower", game.yardFrame ? &*game.yardFrame : nullptr);
  if (at) CHECK(std::hypot(game.mpos[0] - at->x, game.mpos[2] - at->z) < 3, "placed %.1f, %.1f – the base is at %.1f, %.1f", game.mpos[0], game.mpos[2], at->x, at->z);
  // the site is laid out once its ground is loaded
  for (int i = 0; i < 6000 && !cw.tasks; i++) picture();
  CHECK(cw.tasks != nullptr, "the site was not laid out");
  for (int i = 0; i < 30; i++) picture();
  const double slew0 = cw.crane.q.slew;
  const double rope0 = cw.crane.q.rope;
  // A slews the jib to the left, W hoists
  nd::key("KeyA");
  nd::key("KeyW");
  for (int i = 0; i < 180; i++) picture();
  nd::key("KeyA", "", false);
  nd::key("KeyW", "", false);
  for (int i = 0; i < 30; i++) picture();
  std::printf("slew %.3f → %.3f, rope %.2f → %.2f, hook at %.1f %.1f %.1f\n", slew0, cw.crane.q.slew, rope0, cw.crane.q.rope, cw.crane.hook.x, cw.crane.hook.y, cw.crane.hook.z);
  CHECK(cw.crane.q.slew > slew0 + 0.05, "the jib did not slew: %.3f → %.3f", slew0, cw.crane.q.slew);
  CHECK(cw.crane.q.rope < rope0 - 0.1, "the hook was not hoisted: %.2f → %.2f", rope0, cw.crane.q.rope);
  CHECK(!cw.crane.tipped, "the tower crane tipped");

  // the mobile crane: from the depot, driven like a truck; not worked before it stands with its brake on
  game.toMenu();
  game.settings.vehicle = "mobileCrane";
  game.begin("free", 0, true, nullptr);
  for (int i = 0; i < 20000 && game.state != "drive"; i++) picture();
  CHECK(game.state == "drive", "state %s after placing the mobile crane", game.state.c_str());
  CHECK(game.crane != nullptr && !game.crane->operating, "the mobile crane is driven first");
  CHECK(!game.input->crane, "the keys are the driver's");
  if (game.crane) {
    game.input->c.handbrake = 0;
    game.vehicle->speed = 2;
    const std::optional<std::string> no = game.crane->toggle(*game.vehicle, false);
    CHECK(no && *no == "craneTip.stopFirst", "moving: %s", no ? no->c_str() : "(worked)");
    game.vehicle->speed = 0;
    const std::optional<std::string> no2 = game.crane->toggle(*game.vehicle, false);
    CHECK(no2 && *no2 == "craneTip.handbrake", "no handbrake: %s", no2 ? no2->c_str() : "(worked)");
    const std::optional<std::string> yes = game.crane->toggle(*game.vehicle, true);
    CHECK(!yes && game.crane->operating && game.input->crane, "standing with the brake on: worked");
    for (int i = 0; i < 30; i++) picture();
    CHECK(game.crane && game.crane->operating, "still worked");
    // folded, outriggers in: back to driving
    const std::optional<std::string> back = game.crane ? game.crane->toggle(*game.vehicle, true) : std::nullopt;
    CHECK(!back && game.crane && !game.crane->operating, "back to driving: %s", back ? back->c_str() : "");
  }
  const std::string info = game.debugInfo();
  CHECK(info.find("\"state\":\"drive\"") != std::string::npos, "debugInfo: %s", info.c_str());

  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("crane_play: ok\n");
  return 0;
}

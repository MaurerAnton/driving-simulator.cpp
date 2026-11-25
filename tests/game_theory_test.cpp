// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The theory lessons in the game (2.1, beyond the original's tests – as game_test is), on the real map:
 * ?start=theory&theory=rbl opens the mode straight into the first lesson; its place is laid out on the
 * practice yard (the mode counts ready), the scene plays and stops at its first question, the bar at
 * the foot of the screen offers its answers; the right one tapped, the bar says so; Escape goes back to
 * the list, and again to the main menu.
 *
 * Needs an OpenGL ES context off screen and the original's map (else it skips, as game_test does).
 */
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>

#include "game/game.h"
#include "game/theorymode.h"
#include "input/input.h"
#include "platform/dom.h"
#include "platform/glhost.h"
#include "platform/native/native.h"
#include "platform/plat.h"

using namespace fs;

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
  plat::native::setLanguages({"en"});
  plat::native::setSearch("?start=theory&theory=rbl");
  if (!plat::gl::createContext("game_theory_test", 640, 360)) {
    std::printf("SKIP: no OpenGL ES here\n");
    return 0;
  }
  const dom::El ui = dom::el("div");
  Game game(dom::el("canvas"), ui);
  plat::native::wait(game.boot(URLSearchParams("?start=theory&theory=rbl")));
  game.looping = false;
  double now = plat::nowMs();
  const auto picture = [&]() {
    plat::native::pump();
    now += 1000.0 / 60;
    game.frame(now);
  };
  for (int i = 0; i < 20000 && !(game.theory && game.theory->ready()); i++) picture();
  CHECK(game.state == "theory", "state %s", game.state.c_str());
  CHECK(game.theory && game.theory->ready(), "the lesson's place is not laid out");
  if (failures) return 1;
  CHECK(ui.querySelector(".th-bar"), "no bar at the foot of the screen");

  // the first question of "right before left" comes at 5 s of the script
  dom::El answers;
  for (int i = 0; i < 60 * 8 && !(answers = ui.querySelector(".th-answers")); i++) picture();
  CHECK(answers, "the scene did not stop for its question");
  if (failures) return 1;
  const std::vector<dom::El> buttons = answers.querySelectorAll("button");
  CHECK(buttons.size() == 3, "%zu answers", buttons.size());
  std::printf("asked: %s\n", ui.querySelector(".th-ask").textContent().c_str());
  // (the answers are blue, you, white: the blue car comes from the right)
  if (buttons.size() == 3) buttons[0].click();
  picture();
  const dom::El verdict = ui.querySelector(".th-verdict");
  CHECK(verdict && verdict.className().find("ok") != std::string::npos, "the right answer is not taken as right");

  // Escape: to the list of the lessons, and from there to the main menu
  game.input->press("Escape", true);
  picture();
  game.input->press("Escape", false);
  picture();
  CHECK(ui.querySelector(".th-list"), "Escape does not lead to the list");
  game.input->press("Escape", true);
  picture();
  game.input->press("Escape", false);
  picture();
  CHECK(game.state == "menu", "state %s after the list", game.state.c_str());

  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("game_theory: ok\n");
  return 0;
}

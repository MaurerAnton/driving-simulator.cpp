// SPDX-License-Identifier: AGPL-3.0-only
// Origin: tests/game/forkliftmodes.test.ts @ v1.7.7
/**
 * The forklift's drive besides the forklift itself (src/game/forkliftmodes.h):
 * its cameras go round – the view from the side it starts with among them –,
 * where the operator stands after getting off, and from how far one gets on
 * again.  (The camera circling a vehicle resting beside it – Input.orbitRest –
 * is tested in input_test.cpp.)
 */
// LINK: game core
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "game/forkliftmodes.h"

using namespace fs;

namespace {

std::string readFile(const std::string& path) {
  std::ifstream in(path);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void views() {
  // the camera key goes through every view and back to the side view the drive starts with
  ForkView v = "side";
  std::vector<ForkView> seen;
  for (size_t i = 0; i < FORK_VIEWS.size(); i++) {
    seen.push_back(v);
    v = nextForkView(v);
  }
  assert(v == "side");
  assert((seen == std::vector<ForkView>{"side", "cockpit", "chase", "free"}));
  // (twice round: still the same order)
  for (size_t i = 0; i < FORK_VIEWS.size() * 2; i++) v = nextForkView(v);
  assert(v == "side");
}

void cameras() {
  // the side view is the camera circling the forklift, resting on its left a little ahead; the others are the game's own
  const ForkCamera s = forkCamera("side");
  assert(s.mode == "chase");
  assert(s.rest == FORK_SIDE_YAW);
  // (the chase camera stands at (sin r, cos r) turned by the heading: 0 behind, so −π/2 … −π is left and ahead)
  assert(std::sin(s.rest) < -0.8);
  assert(std::cos(s.rest) < 0);
  assert((forkCamera("cockpit") == ForkCamera{"cockpit", 0}));
  assert((forkCamera("chase") == ForkCamera{"chase", 0}));
  assert((forkCamera("free") == ForkCamera{"free", 0}));
}

void gettingOff() {
  // getting off: beside the seat on the left, clear of the forklift, facing its way; on again only from near by
  for (double yaw : {0.0, 0.7, M_PI / 2, -2.2, M_PI}) {
    const double x = 120;
    const double z = -40;
    const ForkExit p = forkliftExit(x, z, yaw);
    // forwards (−sin, −cos), to the right (cos, −sin)
    const double dx = p.x - x;
    const double dz = p.z - z;
    const double right = dx * std::cos(yaw) - dz * std::sin(yaw);
    const double ahead = -dx * std::sin(yaw) - dz * std::cos(yaw);
    assert(right < -0.9 && right > -2);
    assert(std::abs(ahead) < 1e-9);
    assert(p.yaw == yaw);
    // (its half width is 0.58 m: the person stands clear of it – and can get on again from there)
    assert(std::hypot(dx, dz) > 0.58 + 0.3);
    assert(nearForklift(x, z, p.x, p.z));
  }
  assert(nearForklift(0, 0, 3, 0) == true);
  assert(nearForklift(0, 0, 4, 0) == false);
  assert(nearForklift(0, 0, 0, -12) == false);
}

void workMode() {
  // "Forklift work" has a mode of its own (the free drive is "free")
  assert(std::string(FORK_WORK_MODE) == "forklift");
  assert(std::string(FORK_WORK_MODE) != "free");
}

void css() {
  // the forklift's layer over the whole view lets the mouse and the fingers through (#ui's children take them otherwise)
  const std::string css = readFile("web/css/forklift.css");
  const std::string main = readFile("web/css/style.css");
  // (the rule that makes it necessary – and the one that answers it, as strong)
  assert(std::regex_search(main, std::regex(R"(#ui > \* \{ pointer-events: auto; \})")));
  assert(std::regex_search(css, std::regex(R"(#ui > \.fork-ui \{ pointer-events: none; \})")));
  assert(std::regex_search(css, std::regex(R"(\.fork-btns \{[^}]*pointer-events: auto)")));
  // (and the page loads it)
  assert(readFile("web/index.html").find("css/forklift.css") != std::string::npos);
}

}  // namespace

int main() {
  views();
  cameras();
  gettingOff();
  workMode();
  css();
  std::printf("forkliftmodes: ok\n");
  return 0;
}

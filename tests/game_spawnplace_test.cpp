// LINK: game core platform
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Where a vehicle may be put down (src/game/spawnplace.h): only on the
 * loaded tiles, the line round them, never inside a building, the nearest
 * free place round one that is not free, a place beside somebody on foot –
 * and where a tap on the picture meets the ground or a vehicle standing.
 *
 * Origin: tests/game/spawnplace.test.ts @ v1.7.7
 */
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

#include "core/js.h"
#include "game/spawnplace.h"

using namespace fs;

static const Outline CAR = {.front = 1.9, .rear = 2.2, .hw = 0.9};
static const double PI_ = std::numbers::pi;

int main() {
  // the loaded area: on a tile, a margin inside its edge, gone with the tile
  {
    LoadedArea a;
    a.set("0_0", {.cx = 0, .cz = 0, .size = 100}, true);
    a.set("1_0", {.cx = 100, .cz = 0, .size = 100}, true);
    assert(a.has(0, 0));
    assert(a.has(120, 10));
    assert(!a.has(0, 80));
    // (across the shared side the margin is no matter; at the outer edge it is)
    assert(a.has(50, 0, 10));
    assert(!a.has(-45, 0, 10));
    a.set("1_0", {.cx = 100, .cz = 0, .size = 100}, false);
    assert(!a.has(120, 10));
    assert(a.size() == 1);
  }

  // the line round the loaded tiles: only the sides no other tile shares
  {
    // two tiles side by side: 6 outer sides (the shared one left out)
    const std::vector<float> two = tileOutline({{.cx = 0, .cz = 0, .size = 10}, {.cx = 10, .cz = 0, .size = 10}});
    assert(two.size() == 6 * 4);
    for (size_t i = 0; i < two.size(); i += 4) assert(!(two[i] == 5 && two[i + 2] == 5));   // the shared side x = 5 is no part of the line
    // (the sides of each tile in turn: north, east, south, west)
    assert((two == std::vector<float>{-5, -5, 5, -5, 5, 5, -5, 5, -5, 5, -5, -5, 5, -5, 15, -5, 15, -5, 15, 5, 15, 5, 5, 5}));
    // a block of 3 × 3: 12 outer sides
    std::vector<TileBox> nine;
    for (int x = 0; x < 3; x++)
      for (int z = 0; z < 3; z++) nine.push_back({.cx = x * 10.0, .cz = z * 10.0, .size = 10});
    assert(tileOutline(nine).size() == 12 * 4);
    // the area's own outline is the same, and follows a tile going
    LoadedArea a;
    for (size_t i = 0; i < nine.size(); i++) a.set(std::to_string(i), nine[i], true);
    assert(a.outline().size() == 12 * 4);
    assert(a.outline() == tileOutline(nine));
    a.set("4", nine[4], false);
    // (the middle gone: a hole – 4 more sides)
    assert(a.outline().size() == 16 * 4);
    a.clear();
    assert(a.size() == 0 && a.outline().empty() && !a.has(0, 0));
  }

  // a point inside a ring, and the footprints of buildings
  {
    const std::vector<double> square = {0, 0, 10, 0, 10, 10, 0, 10};
    assert(inRing(5, 5, square, 0, 4));
    assert(!inRing(15, 5, square, 0, 4));
    // an L-shaped building: its notch is outside
    const std::vector<double> l = {0, 0, 20, 0, 20, 10, 10, 10, 10, 20, 0, 20};
    assert(inRing(5, 15, l, 0, 6));
    assert(!inRing(15, 15, l, 0, 6));
    Footprints f;
    std::vector<float> pts(square.begin(), square.end());
    for (size_t i = 0; i < l.size(); i++) pts.push_back(static_cast<float>(l[i] + (i % 2 == 0 ? 100 : 0)));
    const std::vector<uint16_t> counts = {4, 6};
    f.set("t", &pts, &counts);
    assert(f.inside(5, 5));
    assert(f.inside(105, 15));
    assert(!f.inside(115, 15));
    assert(!f.inside(50, 5));
    f.remove("t");
    assert(!f.inside(5, 5));
    // (a tile without buildings has no footprints)
    f.set("t", &pts, &counts);
    const std::vector<uint16_t> none;
    f.set("t", &pts, &none);
    assert(!f.inside(5, 5));
    f.set("t", &pts, &counts);
    f.set<std::vector<float>, std::vector<uint16_t>>("t", nullptr, nullptr);
    assert(!f.inside(5, 5));
  }

  // a car reaching into a building is in it; beside it, it is not
  {
    Footprints f;
    const std::vector<float> pts = {0, 0, 10, 0, 10, 10, 0, 10};
    const std::vector<uint16_t> counts = {4};
    f.set("t", &pts, &counts);
    const auto inside = [&f](double x, double z) { return f.inside(x, z); };
    // facing north (yaw 0), its middle 1.5 m south of the wall: the bonnet reaches in
    assert(inBuilding(5, 11.5, 0, CAR, inside));
    assert(!inBuilding(5, 15, 0, CAR, inside));
    // facing east (yaw −π/2), beside the wall
    assert(!inBuilding(-2, 5, -PI_ / 2, CAR, inside));
    // the points: middle, front, rear, four corners; the front lies ahead (yaw 0: north = −z)
    const std::vector<double> p = outlinePoints(0, 0, 0, CAR);
    assert(p.size() == 14);
    assert(std::fabs(p[3] - -1.9) < 1e-9);
  }

  // the nearest free place: the place itself if free, else the nearest round it
  {
    const auto free = [](double x, double, double) { return x >= 10; };
    const auto here = freeNear(20, 0, 0, free);
    assert(here && here->x == 20 && here->z == 0 && here->yaw == 0);
    const auto near = freeNear(5, 0, 0, free, 40, 1.5);
    assert(near);
    assert(near->x >= 10 && std::hypot(near->x - 5, near->z) < 7);
    assert(!freeNear(0, 0, 0, [](double, double, double) { return false; }, 10));
    // only the other way round fits: then facing that way
    const auto turned = freeNear(0, 0, 0, [](double, double, double yaw) { return std::cos(yaw) < 0; }, 5);
    assert(turned && std::cos(turned->yaw) < 0);
  }

  // a place beside somebody on foot: to the right first, never on them
  {
    const auto all = besidePlace(0, 0, 0, [](double, double, double) { return true; });
    assert(all);
    // facing north, the right is east (+x)
    assert(all->x > 3 && std::fabs(all->z) < 1e-9);
    const auto leftOnly = besidePlace(0, 0, 0, [](double x, double, double) { return x < -1; });
    assert(leftOnly && leftOnly->x < -3);
    const auto far = besidePlace(0, 0, 0, [](double x, double z, double) { return std::hypot(x, z) > 8; });
    assert(far && std::hypot(far->x, far->z) > 8);
    assert(!besidePlace(0, 0, 0, [](double, double, double) { return false; }, 5));
  }

  // a tap on the picture: where the ray meets the ground (also on a slope; never where the ground is not known)
  {
    const auto flat = [](double, double) { return 0.0; };
    const auto g = rayGround(0, 10, 0, 1, -1, 0, flat);
    assert(g && std::fabs(g->x - 10) < 0.05 && std::fabs(g->z) < 1e-9);
    // ground rising eastwards (y = x / 10): meets it sooner
    const auto slope = [](double x, double) { return x / 10; };
    const auto s = rayGround(0, 10, 0, 1, -1, 0, slope);
    assert(s && std::fabs(s->x - 100.0 / 11) < 0.05);
    // up into the sky, or over ground not loaded
    assert(!rayGround(0, 10, 0, 1, 0.2, 0, flat));
    assert(!rayGround(0, 10, 0, 1, -1, 0, [](double, double) { return JS_NAN; }));
  }

  // a tap on a vehicle standing: the ray meets its box, or passes it
  {
    StandingBox box;
    box.front = CAR.front;
    box.rear = CAR.rear;
    box.hw = CAR.hw;
    box.height = 1.6;
    // from the south-east, down onto the car
    const double t = rayBox(10, 5, 0, -10, -4, 0, box);
    assert(t > 0);
    // over it, and beside it
    assert(rayBox(10, 5, 0, -10, 0, 0, box) == -1);
    assert(rayBox(10, 1, 5, -10, 0, 0, box) == -1);
    // facing north it is long along z: a ray along x at z = 1.5 meets it – turned by 90°, it is narrow there, and passes
    assert(rayBox(10, 1, 1.5, -1, 0, 0, box) > 0);
    StandingBox turned = box;
    turned.yaw = PI_ / 2;
    assert(rayBox(10, 1, 1.5, -1, 0, 0, turned) == -1);
  }

  std::printf("game_spawnplace_test: ok\n");
  return 0;
}

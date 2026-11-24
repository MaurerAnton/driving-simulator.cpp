// SPDX-License-Identifier: AGPL-3.0-only
// LINK: ui reality game i18n world core platform geo
// Origin: tests/game/lessonwatch.test.ts @ v1.7.7 (the case of ui/lessonstart.ts; game/lessonwatch.ts is not in 2.0.0)
/** The map of the districts of the lesson start (ui/lessonstart.ts): every district on the picture, a tap picks the one under it. */
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "game/lessonplaces.h"
#include "ui/lessonstart.h"
#include "world/types.h"

using namespace fs;

static SpawnJson spawn(const char* name, double x, double z) {
  SpawnJson s;
  s.name = name;
  s.x = x;
  s.z = z;
  s.yaw = 0;
  s.kind = "street";
  return s;
}

int main() {
  const std::vector<SpawnJson> spawns = {
      spawn("Altenbauna – Parkplatz", 0, 0),
      spawn("Großenritte – Parkplatz", -3000, 1500),
      spawn("Kassel Mitte – Parkplatz", 2000, -9000),
      spawn("Rengershausen – Parkplatz", 1200, -2500),
  };
  const std::vector<LessonArea> areas = areaChoices(spawns);
  assert(areas.size() == 4);
  const double W = 640;
  const double H = 400;
  const DistrictFrame f = districtFrame(areas, W, H);
  for (const LessonArea& a : areas) {
    const auto [x, y] = f.toPx(a.x, a.z);
    assert(x >= 0 && x <= W && y >= 0 && y <= H);  // on the picture
  }
  // north is up, east is right
  const auto [kx, ky] = f.toPx(2000, -9000);
  const auto [gx, gy] = f.toPx(-3000, 1500);
  assert(ky < gy && kx > gx);
  for (size_t i = 0; i < areas.size(); i++) {
    const auto [x, y] = f.toPx(areas[i].x, areas[i].z);
    assert(districtAt(areas, f, x + 5, y - 5) == static_cast<int>(i));
  }
  assert(districtAt(areas, f, -500, -500) == -1);  // nothing there
  // one district only, or none: still a frame
  const DistrictFrame one = districtFrame(std::vector<LessonArea>(areas.begin(), areas.begin() + 1), W, H);
  assert((one.toPx(areas[0].x, areas[0].z) == std::array<double, 2>{W / 2, H / 2}));
  assert(std::isfinite(districtFrame({}, W, H).toPx(0, 0)[0]));

  std::puts("lessonstart_test: ok");
  return 0;
}

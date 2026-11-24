// LINK: game sim render i18n audio input engine world core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Lessons in real places (game/lessonplaces.h): which lessons look for a
 * place and which stay as they are, where a lesson begins by the setting –
 * and the places found in a town: right before a junction of the lesson's
 * kind, in a lane leading to it, with the way through it to the destination.
 *
 * Origin: tests/game/lessonplaces.test.ts @ v1.7.7 (its last case looks for the places on the
 * real map, which needs the map's loader; here they are looked for in the small town of tests/town.h).
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "game/lessonplaces.h"
#include "game/nav.h"
#include "tests/town.h"
#include "world/types.h"

using namespace fs;

static SpawnJson spawn(const std::string& name, double x, double z) {
  SpawnJson s;
  s.name = name;
  s.x = x;
  s.z = z;
  s.yaw = 0;
  s.kind = "parking";
  return s;
}

static const LessonDef& lesson(const std::string& id) {
  for (const LessonDef& l : LESSONS)
    if (l.id == id) return l;
  assert(false);
  return LESSONS[0];
}

int main() {
  const std::vector<SpawnJson> SPAWNS = {spawn("Baunatal – Parkplatz", 700, -300), spawn("Altenbauna – Parkplatz", 1100, -800), spawn("Altenbauna – Markt", 1150, -850),
                                         spawn("Kassel – Parkplatz", 6000, -7000), spawn("Bad Hersfeld – Am Markt", 21588, 42581)};

  // which lessons look for a real place – the yard, the tram and the bus do not
  {
    std::map<std::string, std::optional<PlaceKind>> kinds;
    for (const LessonDef& l : LESSONS) kinds[l.id] = placeKindOf(l);
    assert(kinds["basics"] == "quiet");
    assert(kinds["rbl"] == "rbl");
    assert(kinds["priority"] == "priority");
    assert(kinds["signals"] == "signals");
    assert(kinds["roundabout"] == "roundabout");
    assert(kinds["rural"] == "rural");
    assert(kinds["motorway"] == "motorway");
    assert(kinds["exam"] == "area");
    assert(kinds["ceExam"] == "area");
    for (const char* id : {"ceCouple", "ceCorner", "ceRamp", "tramLine", "busLine"}) assert(!kinds[id]);
    assert(LESSONS.size() == 21 && LESSON_PLACE_MODES.size() == 3);
    assert(searchRadius("motorway") == 3000 && searchRadius("rbl") == SEARCH_RADIUS);
  }

  // the districts to choose from, and where a lesson begins by the setting
  {
    const std::vector<LessonArea> areas = areaChoices(SPAWNS);
    // each district once, nearest first, none far away
    assert(areas.size() == 3 && areas[0].name == "Baunatal" && areas[1].name == "Altenbauna" && areas[2].name == "Kassel");
    const LessonDef& rbl = lesson("rbl");
    const SpawnJson here = spawn("Somewhere", 1, 2);
    assert(!lessonStart(rbl, "start", nullptr, SPAWNS, here));                     // as before
    assert(!lessonStart(lesson("ceCouple"), "town", nullptr, SPAWNS, here));       // the yard stays the yard
    const LessonArea kassel = {.name = "Kassel", .x = 6010, .z = -7010};
    const std::optional<LessonStart> inArea = lessonStart(rbl, "area", &kassel, SPAWNS, here);
    assert(inArea && inArea->spawn.name == "Kassel – Parkplatz");
    assert(inArea->kind == "rbl");
    assert(inArea->fallback.name == "Somewhere" && inArea->fallback.x == 1 && inArea->fallback.z == 2);   // nothing found: where the player chose
    // at random: a district of the home region – never one far away
    for (int i = 0; i < 20; i++) {
      const std::optional<LessonStart> s = lessonStart(rbl, "town", nullptr, SPAWNS, here, [i] { return i / 20.0; });
      assert(s && std::hypot(s->spawn.x - 700, s->spawn.z + 300) < 12000);
    }
    // the test drive: only the district
    const std::optional<LessonStart> exam = lessonStart(lesson("exam"), "area", nullptr, SPAWNS, here);
    assert(exam && exam->kind == "area");
    // (a district without a car park near its middle: a road near the middle, once loaded)
    const LessonArea nowhere = {.name = "Nowhere", .x = 90000, .z = 90000};
    const std::optional<LessonStart> rough = lessonStart(rbl, "area", &nowhere, SPAWNS, here);
    assert(rough && rough->spawn.name == "Nowhere" && rough->spawn.rough && rough->spawn.kind == "street" && rough->spawn.x == 90000);
  }

  // in a town: a place before a junction where right before left counts, and a quiet street
  {
    TownOptions opt;
    opt.cols = 5;
    opt.rows = 5;
    opt.block = 200;
    const Town town = makeTown(opt);
    double seed = 11;
    const auto rnd = [&seed] {
      seed = std::fmod(seed * 16807, 2147483647);
      return seed / 2147483647;
    };
    PlaceOptions options;
    options.rnd = rnd;
    const std::optional<LessonSpot> at = findLessonPlace(*town.world, *town.lanes, "rbl", 400, 400, options);
    assert(at && at->node);   // a junction
    assert(junctionOfType(*at->node, "rbl"));   // the junction is of the lesson's kind
    // about a hundred metres before it (less where the road is short), facing it
    const double d = std::hypot(at->node->x - at->x, at->node->z - at->z);
    assert(d > 15 && d < 250);
    const double fx = -std::sin(at->yaw);
    const double fz = -std::cos(at->yaw);
    assert(((at->node->x - at->x) * fx + (at->node->z - at->z) * fz) / d > 0.3);   // facing the junction
    assert(at->goal);   // a destination: a road leading away from the junction
    assert(at->goal->a == at->node || at->goal->b == at->node);
    assert(!at->name.empty());
    // a quiet, straight street of a built-up area: no junction, no destination of its own
    const std::optional<LessonSpot> quiet = findLessonPlace(*town.world, *town.lanes, "quiet", 400, 400, options);
    assert(quiet && quiet->goal == nullptr && quiet->node == nullptr);
    // what the town has not: lights, a roundabout, a country road, a motorway – and the test drive needs no place
    for (const char* kind : {"signals", "roundabout", "rural", "motorway", "area"}) assert(!findLessonPlace(*town.world, *town.lanes, kind, 400, 400, options));
    // (too far from every street: nothing)
    PlaceOptions near = options;
    near.radius = 50;
    assert(!findLessonPlace(*town.world, *town.lanes, "rbl", 5000, 5000, near));
    // lanes one cannot drive around in are no place to begin
    PlaceOptions none = options;
    none.usable = [](const Lane&) { return false; };
    assert(!findLessonPlace(*town.world, *town.lanes, "rbl", 400, 400, none));
  }

  // … and before a junction with lights
  {
    TownOptions opt;
    opt.cols = 5;
    opt.rows = 5;
    opt.block = 200;
    opt.lights = {{2, 2}};
    const Town town = makeTown(opt);
    const std::optional<LessonSpot> at = findLessonPlace(*town.world, *town.lanes, "signals", 400, 400);
    assert(at && at->node && at->node->ctrl == CTRL_SIGNALS && at->goal);
    assert(at->node == town.world->nodes[town.node(2, 2)]);
  }

  std::printf("game_lessonplaces_test: ok\n");
  return 0;
}

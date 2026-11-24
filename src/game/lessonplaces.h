// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/lessonplaces.ts @ v1.7.7
/**
 * Lessons in real places.  The driving lessons begin, as they always did,
 * where the player chose to start – or, if the player wishes so (a setting,
 * asked the first time a lesson starts), right before a place of the real
 * town that fits the lesson: a junction of the kind it is about, a quiet
 * straight street to learn pulling away and braking in, the edge of town
 * where a country road begins, a road with a slip road onto the motorway.
 * That place is looked for at random in Baunatal and Kassel, or in or near
 * a district the player chose.
 *
 *   'start'  as before: from the start place chosen in the menu
 *   'town'   at a random district of the home region (Baunatal, Kassel and round)
 *   'area'   in or near the district chosen in the settings
 *
 * The search runs on the roads that are loaded (world.h, lanes.h) once
 * the start district's roads are there, so it sees what the drive will see:
 * the kind of junction, its signs and lights, its lanes.  The vehicle is put
 * into the rightmost lane of the road leading to the place, about a hundred
 * metres before it, and the lesson's first destination is the road leading
 * away from it (game/nav.h) – the rest of the lesson goes on as before.
 * Where nothing fits, the lesson begins at the start place chosen.
 *
 * Not affected: the tasks on the practice yard (class CE, the cranes, the
 * forklift) and the theory lessons, which are scenes of their own on the
 * practice yard (game/theorystage.ts).
 *
 * (This header needs nothing of the road network but its names, so that the
 * settings – which keep where the lessons take place – can include it.)
 */
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "world/types.h"

namespace fs {

class World;
class LaneGraph;
struct Lane;
struct Edge;
struct Node;
struct LessonDef;

/** Where the lessons take place (the setting): 'start' | 'town' | 'area' */
using LessonPlaceMode = std::string;
inline const std::vector<LessonPlaceMode> LESSON_PLACE_MODES = {"start", "town", "area"};

/** A district to hold the lessons in: its name and middle. */
struct LessonArea {
  std::string name;
  double x = 0;
  double z = 0;
};

/**
 * What place a lesson looks for – 'area': no place of its own, only the district (the test drive):
 * 'quiet' | 'rbl' | 'priority' | 'signals' | 'roundabout' | 'rural' | 'motorway' | 'area'
 */
using PlaceKind = std::string;

/** The place a lesson looks for, or null: the lesson has none (the yard, the tram, the bus). */
std::optional<PlaceKind> placeKindOf(const LessonDef& l);

/** how far round the district's middle a place is looked for (m) – inside what is loaded once the start is ready (1.5 km) */
inline constexpr double SEARCH_RADIUS = 1300;

/** How far round the start place the roads must be loaded before the place is looked for (m). */
double searchRadius(const PlaceKind& kind);

/**
 * The districts the player may choose: those of the map's start places
 * (car parks, one in each district) near the home town, nearest first, each
 * name once.
 */
std::vector<LessonArea> areaChoices(const std::vector<SpawnJson>& spawns, size_t max = 60, double within = 25000);

/** Where a lesson is to begin: the start place to load, what to look for there, and where to go if nothing is found. */
struct LessonStart {
  SpawnJson spawn;
  PlaceKind kind;
  SpawnJson fallback;
};

/**
 * The start of a lesson in a real place – null where the lesson begins where
 * it always did (the setting says so, or the lesson has no real place).
 * `area`: the district chosen (null: none); `rnd`: Math.random unless given.
 */
std::optional<LessonStart> lessonStart(const LessonDef& lesson, const LessonPlaceMode& mode, const LessonArea* area, const std::vector<SpawnJson>& spawns,
                                       const SpawnJson& current, const std::function<double()>& rnd = nullptr);

/** A place found for a lesson: where the vehicle stands, and the lesson's first destination (null: the lesson finds its own). */
struct LessonSpot {
  double x = 0;
  double z = 0;
  double yaw = 0;
  /** the street it stands in */
  std::string name;
  Edge* goal = nullptr;
  /** the junction the place is about (null: a street) */
  Node* node = nullptr;
};

struct PlaceOptions {
  std::optional<double> radius;
  /** (Math.random unless given) */
  std::function<double()> rnd;
  /** lanes of the network one can drive around in (the start must be one) */
  std::function<bool(const Lane& lane)> usable;
};

/**
 * Look for a place for a lesson round (x, z) in the roads that are loaded:
 * null if there is none.  `kind` 'area' needs no place (null).
 */
std::optional<LessonSpot> findLessonPlace(World& world, LaneGraph& graph, const PlaceKind& kind, double x, double z, const PlaceOptions& opt = {});

}  // namespace fs

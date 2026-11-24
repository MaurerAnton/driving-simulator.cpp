// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/lessonroutes.ts @ v1.7.7
/**
 * What the player sees of two things of 1.6.0, kept out of the big files:
 *
 *  - the set routes (game/routepresets.ts): a page of them, opened from the
 *    main menu, and a section of the big map's way planner – each route a
 *    big button with its name and what it leads through;
 *  - where the lessons take place (game/lessonplaces.ts): the question asked
 *    the first time a lesson starts, and the two rows of the settings page.
 *
 * Built from the classes the other pages use (overlay, panel, btn, row),
 * with tap targets as big as theirs.
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "game/settings.h"
#include "i18n/index.h"
#include "platform/dom.h"
#include "world/types.h"

namespace fs {

/** `(key, vars?) => string`: a text in the player's language (the original's `T`) */
using TrFn = std::function<std::string(const std::string& key, const TrParams& vars)>;

/** The page of set routes (from the main menu): a tap starts one – the drive begins at its first place. */
dom::El routePresetPage(const TrFn& t, std::function<void(const std::string& id)> start, std::function<void()> back);

/** The section of the big map's planner: the set routes, each driven from where the vehicle stands. */
dom::El routePresetSection(const TrFn& t, std::function<void(const std::string& id)> start);

/** The rows of the settings page: where the lessons take place, and the district for "in an area". */
std::vector<dom::El> lessonPlaceRows(const TrFn& t, Settings& s, const std::vector<SpawnJson>& spawns, std::function<void()> changed);

/**
 * The question asked the first time a lesson starts: where shall the lessons
 * take place?  `done` once answered (the settings are set; the caller saves
 * them and starts the lesson).
 */
dom::El askLessonPlace(const TrFn& t, Settings& s, const std::vector<SpawnJson>& spawns, std::function<void()> done);

}  // namespace fs

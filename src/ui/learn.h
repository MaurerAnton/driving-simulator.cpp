// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/learn.ts @ v1.7.7
/**
 * The learning modes, opened from the "Learn" group of each main menu
 * (stage.ts, carousel.ts, the simple list of hud.ts): plain pages over the
 * picture, made for a phone held in the hand – big buttons, one thing at a
 * time, nothing that needs a keyboard.
 *
 *  - the theory exam (game/exam/exam.ts): 30 questions in 45 minutes, as in
 *    the test for class B; then the result, the mistakes explained, the
 *    exams taken before;
 *  - the sign trainer (game/exam/signs.ts): a sign, four meanings – or four
 *    numbers – to choose from; spaced repetition, a page of statistics;
 *  - "spot the danger" (game/exam/danger.ts): a theory scene frozen and seen
 *    from above (learnplan.ts) – why be ready to brake?
 *  - the lesson log (game/exam/learnlog.ts): where am I?
 *
 * What is learned is kept by game/exam/learnstore.ts (and synced with the
 * server: net/sync.ts, kind "learn").  The words of the questions come from
 * src/i18n/exam/, the rest from the interface's texts.  hud.ts loads this
 * file only when a learning mode is opened.
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/task.h"
#include "i18n/index.h"
#include "platform/dom.h"

namespace fs {

/** 'hub' | 'exam' | 'signs' | 'danger' | 'log' */
using LearnScreen = std::string;

struct LearnContext {
  std::function<std::string(const std::string& key, const TrParams& vars)> t;
  std::string lang;
  /** the page to begin with */
  LearnScreen screen;
  /** put a page on the screen (in place of the one before) */
  std::function<void(const dom::El& page)> show;
  /** back to the menu */
  std::function<void()> back;
  /** where the signs' files are (public/signs/) */
  std::string signBase;
};

/**
 * What the learning modes read of parts that are not in this version yet: the theory
 * scenes (game/theory.ts THEORY – their ids, in their order) and the basic tasks of
 * class B (game/drills.ts DRILL_IDS).  Empty: the lesson log has no rows for them, and
 * "spot the danger" names no scene.  Set where those parts are brought.
 */
struct LearnHooks {
  std::function<std::vector<std::string>()> THEORY = nullptr;
  std::function<std::vector<std::string>()> DRILL_IDS = nullptr;
};
inline LearnHooks learnHooks;

/** A sign as the game draws it: its SVG file, or drawn by render/signart.ts. */
dom::El signPicture(const std::string& code, const std::string& base, double size);

/** Open the learning modes on a page. */
Task<void> openLearn(LearnContext c);

}  // namespace fs

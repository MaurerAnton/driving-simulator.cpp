// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/exam/signs.ts @ v1.7.7
/**
 * The sign trainer: every sign the game can draw – the official graphics of
 * public/signs/ and those render/signart.ts draws itself – shown one at a
 * time; the player picks its meaning (or its number in the catalogue) from
 * four.  Which sign comes next is decided by spaced repetition (Leitner
 * boxes): a sign answered right moves up a box and comes back later and
 * later – in five minutes, a day, three days, a week, three weeks – a sign
 * answered wrong goes back to the first box and comes again soon.  Signs due
 * come first, then new ones; when none is due, the one due soonest.
 *
 * Pure functions (tests/game/learn.test.ts); the meanings are in
 * src/i18n/exam/<lang>.ts under `signs`, the statistics kept by learnstore.ts.
 */
#pragma once

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "game/exam/exam.h"
#include "i18n/types.h"

namespace fs {

/** The signs that render/signart.ts draws (all others are SVG files in public/signs/svg/). */
inline const std::unordered_set<std::string> ART_SIGNS = {"133-20", "136-20", "138-20", "140-10", "142-20", "144-10", "238", "257-53", "257-54", "257-58", "258", "261", "263", "266", "268", "269", "270.1", "270.2", "273", "275-30", "277.1", "279-30", "354", "358", "363", "1010-62", "1022-12", "1024-14", "1024-17"};

namespace detail {
const std::vector<std::string>& trainerSigns();
}

/** The signs of the trainer, in the order they are first shown: as the catalogue numbers them. */
inline const std::vector<std::string>& TRAINER_SIGNS = detail::trainerSigns();

/** The family of a sign, from which the wrong answers are taken: danger signs, speed signs, other rules, directions and information, added panels ('danger' | 'speed' | 'rule' | 'info' | 'panel'). */
std::string signFamily(const std::string& code);

/** The number of a sign as the catalogue writes it ("Zeichen 274-50"). */
inline std::string signNumber(const std::string& code) { return code; }

/**
 * The four choices for a sign: its own and three others – of its family
 * where there are enough, never two with the same text.  `label` gives the
 * text of a choice (the meaning, or the number).
 */
std::vector<std::string> signChoices(const std::string& code, const Rand& rand, const std::function<std::string(const std::string&)>& label, const std::vector<std::string>& codes = TRAINER_SIGNS, size_t n = 4);

/** What the trainer knows of a sign. */
struct SignStat {
  /** the Leitner box: 0 (just wrong) … 5 */
  double box = 0;
  /** when it is due again (ms) */
  double due = 0;
  double right = 0;
  double wrong = 0;
  /** when it was last answered (ms) */
  double last = 0;

  bool operator==(const SignStat&) const = default;
};

/** how long a sign rests in each box before it comes again */
inline const std::vector<double> BOX_WAIT = {0, 5 * 60'000.0, 24 * 60 * 60'000.0, 3 * 24 * 60 * 60'000.0, 7 * 24 * 60 * 60'000.0, 21 * 24 * 60 * 60'000.0};
inline const int TOP_BOX = 5;
/** from this box on a sign counts as learned */
inline constexpr int LEARNED_BOX = 3;

/** A sign answered: right – up a box, wrong – back to the first.  (`s` nullptr: never answered) */
SignStat review(const SignStat* s, bool right, double now);

/**
 * The next sign to ask: one that is due (the lowest box first, the longest
 * due first), else a new one (in the catalogue's order), else the one due
 * soonest.  Never `avoid` (the one just asked) while another can be asked.
 */
std::string nextSign(const Record<SignStat>& stats, double now, const std::string& avoid = "", const std::vector<std::string>& codes = TRAINER_SIGNS);

struct SignProgress {
  int learned = 0;
  int seen = 0;
  int due = 0;
  int total = 0;
  bool operator==(const SignProgress&) const = default;
};

/** How far the trainer has come: signs learned (box 3 and up), seen, due now, of how many. */
SignProgress signProgress(const Record<SignStat>& stats, double now, const std::vector<std::string>& codes = TRAINER_SIGNS);

}  // namespace fs

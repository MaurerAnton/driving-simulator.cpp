// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/exam/learnstore.ts @ v1.7.7
/**
 * What the player learned, kept in the browser (and on the server, as the
 * progress kind "learn": net/sync.ts): the theory exams taken, the sign
 * trainer's boxes, the "spot the danger" answers, and how each practical
 * lesson and each basic task ended – tried, passed, with stars, when.
 *
 * Two copies (two devices) are merged item by item: the exams are a union;
 * of a sign, a danger item, a lesson the copy answered more often wins (on a
 * tie the later one); a lesson keeps the most tries and the best stars.
 */
#pragma once

#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "game/exam/signs.h"
#include "i18n/types.h"
#include "platform/json.h"
#include "platform/plat.h"

namespace fs {

/** A theory exam taken. */
struct ExamRecord {
  /** when it ended (ms) */
  double t = 0;
  double points = 0;
  double max = 0;
  double errors = 0;
  bool passed = false;
  /** the ids of the questions answered wrong */
  std::vector<std::string> wrong;
  /** how long it took (s) */
  double secs = 0;

  bool operator==(const ExamRecord&) const = default;
};

/** A "spot the danger" item answered. */
struct DangerStat {
  double right = 0;
  double wrong = 0;
  /** the last answer was right */
  bool ok = false;
  double last = 0;

  bool operator==(const DangerStat&) const = default;
};

/** How a practical lesson (or a basic task) went. */
struct LessonRecord {
  double tries = 0;
  double passes = 0;
  /** the best stars (0–3) */
  double stars = 0;
  /** the last try, the last pass (ms; 0: never) */
  double last = 0;
  double passedAt = 0;

  bool operator==(const LessonRecord&) const = default;
};

struct LearnData {
  std::vector<ExamRecord> exams;
  Record<SignStat> signs;
  Record<DangerStat> danger;
  Record<LessonRecord> lessons;
  Record<LessonRecord> drills;

  bool operator==(const LearnData&) const = default;
};

/** What is needed of localStorage (tests bring their own). */
using LearnStore = plat::Storage;

inline const char* const LEARN_KEY = "fahrsim.learn.v1";
/** the exams kept (the newest) */
inline constexpr size_t EXAMS_KEPT = 50;

inline LearnData emptyLearn() { return LearnData{}; }

/** Whatever was stored (or sent by the server), made well-formed. */
LearnData cleanLearn(const Json& v);

/** The data as JSON.stringify writes it (and as a document, for cleanLearn and mergeLearn). */
std::string learnText(const LearnData& d);
Json learnJson(const LearnData& d);

/** the browser's localStorage */
inline LearnStore* defaultLearnStore() { return &plat::localStorage(); }

LearnData loadLearn(LearnStore* store = defaultLearnStore());

void saveLearn(const LearnData& d, LearnStore* store = defaultLearnStore());

/** Change what is stored: read it, change it, write it back (and give it back). */
LearnData updateLearn(const std::function<void(LearnData& d)>& change, LearnStore* store = defaultLearnStore());

/** An exam taken: kept with the newest first. */
void addExam(LearnData& d, const ExamRecord& e);

/** A try of a lesson or a task: passed or not, with stars (0–3). */
void noteTry(Record<LessonRecord>& list, const std::string& id, bool passed, double stars, double now);

/** The stars of a lesson driven on the road: passed without a fault 3, with up to 4 fault points 2, else 1. */
int lessonStars(bool passed, double faultPoints);

/** a basic task finished in a lesson (game/drills.ts DrillResult) */
struct DrillTry {
  std::string id;
  double stars = 0;
  bool failed = false;
};

/**
 * The end of a practical lesson (game.ts finish): its try, and the try of
 * each basic task finished in it (game/drills.ts) with the task's stars.
 * (`now` NaN: Date.now())
 */
void noteLessonEnd(const std::string& id, bool passed, double faultPoints, const std::vector<DrillTry>& drills = {}, double now = std::numeric_limits<double>::quiet_NaN(), LearnStore* store = defaultLearnStore());

/** Two copies of what was learned, merged (the base – the copy both last agreed on – is not needed: nothing is ever taken out). */
LearnData mergeLearn(const Json& local, const Json& remote);

}  // namespace fs

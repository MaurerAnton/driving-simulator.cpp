// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/exam/exam.ts @ v1.7.7
/**
 * The theory exam, as the German test for class B is made: 30 questions in
 * 45 minutes – 20 of the basic material and 10 of the class – each worth 2
 * to 5 points, 110 points in all.  Failed with more than 10 error points, or
 * with two wrong 5-point questions (which are 10 error points already).  A
 * question with answers to tick counts only when exactly the right ones are
 * ticked; a question with a number counts when the number is right.  When the
 * time is up, what is not answered is wrong.
 *
 * Pure functions (tests/game/exam.test.ts); ui/learn.ts shows it, the
 * results are kept by game/exam/learnstore.ts.
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "core/random.h"
#include "game/exam/questions.h"

namespace fs {

inline constexpr int EXAM_BASIC = 20;
inline constexpr int EXAM_CLASS = 10;
inline constexpr double EXAM_MINUTES = 45;
inline constexpr int EXAM_MAX = 110;
/** more error points than these: failed */
inline constexpr int EXAM_MAX_ERRORS = 10;

using Rand = std::function<double()>;

namespace exam {
/**
 * A small seeded random generator (mulberry32): the same seed, the same exam.
 * (In a namespace of its own: audio/waves.h has a `seeded` of its own, another generator.)
 */
Rand seeded(uint32_t seed);
}  // namespace exam

template <class T>
std::vector<T> shuffle(const std::vector<T>& list, const Rand& rand) {
  std::vector<T> out = list;
  for (int i = static_cast<int>(out.size()) - 1; i > 0; i--) {
    const int j = static_cast<int>(std::floor(rand() * (i + 1)));
    std::swap(out[i], out[j]);
  }
  return out;
}

/**
 * The questions of one exam: 20 of the basic material and 10 of the class,
 * spread over the topics, together worth 110 points – questions are swapped
 * for others of the same part (of the same topic where one fits) until the
 * points add up.
 */
std::vector<const ExamQuestion*> pickExam(const std::vector<ExamQuestion>& bank = QUESTIONS, const Rand& rand = rnd, int target = EXAM_MAX);

/** An answer: the answers ticked (indices), or the number typed (as typed). */
using ExamAnswer = std::variant<std::vector<int>, std::string>;
/** the answers given, by question (a question not in it: undefined) */
using ExamAnswers = std::unordered_map<std::string, ExamAnswer>;

/** A number as typed: "1,6", "1.6", " 50 km/h" → 1.6, 50 (NaN: none). */
double typedNumber(const std::string& s);

/** Is this answer right? (ticked: exactly the right ones; typed: the right number) – `a` nullptr: undefined */
bool isRight(const ExamQuestion& q, const ExamAnswer* a);

struct ExamScore {
  /** points of the right answers, of how many */
  int points = 0;
  int max = 0;
  /** points of the wrong (and the unanswered) */
  int errors = 0;
  /** wrong 5-point questions */
  int wrongFive = 0;
  bool passed = false;
  /** the ids of the wrong questions, in the order of the exam */
  std::vector<std::string> wrong;
};

/** The result of an exam: failed with more than 10 error points or two wrong 5-point questions. */
ExamScore scoreExam(const std::vector<const ExamQuestion*>& qs, const ExamAnswers& answers);

/** How much time is left (ms, never below 0) of an exam begun at `start` (ms). */
double timeLeft(double start, double now, double minutes = EXAM_MINUTES);

/** A time left as the clock shows it: "44:59" (rounded up to the second, as a countdown does). */
std::string clockText(double ms);

/** Has this answer been given at all (something ticked or typed)? */
bool answered(const ExamAnswer* a);

/** `answers[id]` (nullptr: undefined) */
inline const ExamAnswer* answerOf(const ExamAnswers& answers, const std::string& id) {
  auto it = answers.find(id);
  return it == answers.end() ? nullptr : &it->second;
}

}  // namespace fs

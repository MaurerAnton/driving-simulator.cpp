// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/exam/questions.ts @ v1.7.7
/**
 * The question bank of the theory exam (game/exam/exam.ts) – a starter bank
 * written for this game from the rules themselves (StVO, StVG, FeV,
 * StVZO), never copied from the official catalogue of exam questions or from
 * any app: that catalogue is copyrighted.
 *
 * This file holds what is the same in every language: each question's id,
 * its topic, its points (2 to 5, as in the real test), how many answers it
 * has and which of them are right (one or more) – or the number to type –,
 * the paragraph it rests on, and its picture (a sign drawn by the game, or a
 * frozen moment of a theory scene seen from above).  The words – question,
 * answers, explanation – are in src/i18n/exam/<lang>.ts under the same id,
 * the answers in the same order.
 *
 * Topics: danger lessons, behaviour, priority, signs, overtaking, parking,
 * lights, alcohol and fitness, environment – the basic material – and speed
 * and distance, vehicle technique, class B: the material of the class.
 * tests/game/exam.test.ts checks that every id is unique, every question has
 * a right answer, texts and a reference.
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace fs {

/** 'danger' | 'behaviour' | 'priority' | 'signs' | 'overtaking' | 'parking' | 'lights' | 'alcohol' | 'environment' | 'speed' | 'technique' | 'classB' */
using ExamTopic = std::string;

/** All topics, in the order of the lists. */
inline const std::vector<ExamTopic> EXAM_TOPICS = {"danger", "behaviour", "priority", "signs", "overtaking", "parking", "lights", "alcohol", "environment", "speed", "technique", "classB"};

/** The topics of the class-specific part (class B); all others are the basic material. */
inline const std::vector<ExamTopic> CLASS_TOPICS = {"speed", "technique", "classB"};

/** A picture beside the question: a sign (its catalogue number), or a moment of a theory scene (the time of one of its questions). */
struct ExamPicture {
  /** `{ sign }` ('sign' in p) */
  std::optional<std::string> sign;
  /** `{ scene, step, off? }` */
  std::string scene;
  int step = 0;
  std::optional<double> off;
};

struct ExamNumber {
  double value = 0;
  std::string unit;
};

struct ExamQuestion {
  std::string id;
  ExamTopic topic;
  /** 2 | 3 | 4 | 5 */
  int points = 0;
  /** how many answers to choose from (0: a number is typed) */
  int answers = 0;
  /** the right answers (indices) – all of them must be ticked, and none else */
  std::vector<int> correct;
  /** a number to type instead: its value and unit */
  std::optional<ExamNumber> number;
  /** the rule (the same in every language) */
  std::string ref;
  std::optional<ExamPicture> picture;
};

namespace detail {
const std::vector<ExamQuestion>& examQuestions();
}

inline const std::vector<ExamQuestion>& QUESTIONS = detail::examQuestions();

}  // namespace fs

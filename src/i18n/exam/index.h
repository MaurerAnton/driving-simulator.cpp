// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/i18n/exam/index.ts @ v1.7.7
/**
 * The words of the learning modes (ui/learn.ts) in the languages: the
 * questions of the theory exam, the "spot the danger" items and the meanings
 * of the signs of the sign trainer.  They are many and long, so they live
 * apart from the texts of the interface and are fetched only when a learning
 * mode is opened.
 *
 * All 28 languages of the game.  Whatever a language lacks – a question, an
 * answer, a sign – is taken from English, so a translation can be done bit
 * by bit.  The rules (the references) stay in German in every language.
 *
 * The words are data: tools/i18n-export.mjs writes them from the original's
 * src/i18n/exam/<lang>.ts as web/lang/exam/<lang>.json (fetched beside the page), and
 * compiles the English ones into the program (EXAM_EN).  The right answers are counted by
 * their places (game/exam/questions.ts), and the signs are read in the order of the file.
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/task.h"
#include "i18n/types.h"

namespace fs {

/** A question (or a danger item): its text, its answers in order, why the right ones are right. */
struct ExamText {
  std::string q;
  std::vector<std::string> a;
  std::string why;

  friend bool operator==(const ExamText&, const ExamText&) = default;
};

struct ExamTexts {
  Record<ExamText> questions;
  Record<ExamText> danger;
  /** what a sign means, by its catalogue number */
  Record<std::string> signs;
};

namespace detail {
const std::vector<std::string>& examLangs();
const ExamTexts& examEn();
}  // namespace detail

/** The languages that have the words of their own (all others: English). */
inline const std::vector<std::string>& EXAM_LANGS = detail::examLangs();

/**
 * One language's words with English beneath: what it lacks comes from English.
 * (`own`: what the language's file has – parts of it may be missing or empty; nullptr: nothing.)
 */
ExamTexts withFallback(const ExamTexts* own);

/** The words in a language (English for a language that has none of its own yet). */
Task<ExamTexts> loadExamTexts(std::string lang);

/** The English words, at once (tests, and while another language is fetched). */
inline const ExamTexts& EXAM_EN = detail::examEn();

namespace i18n {
/** Read the words from the JSON of a language's file (false: it is not of that shape). */
bool parseExamTexts(std::string_view json, ExamTexts& out);
}  // namespace i18n

}  // namespace fs

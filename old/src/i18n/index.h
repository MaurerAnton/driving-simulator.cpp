// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Texts in many languages.
 *
 * German and English are always there; every other language is a dictionary of
 * its own (src/i18n/<code>.cpp) that is registered when it is chosen.  A text
 * missing in a language falls back to English.  To add a language: copy en.cpp,
 * translate the values, add a line to LANGS – the tests check that nothing is
 * missing.
 *
 * Origin: src/i18n/{index,keys,violations,types}.ts (MaurerAnton/driving-simulator
 * @ ff37c5d, v0.7.0).  The TypeScript fetches a language file when it is
 * chosen; here every dictionary is linked into the binary and registered on
 * first use, so `loadLang` answers the same way without the network.
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace fs {

/** All texts of one language: key → text. */
using Dict = std::vector<std::pair<std::string, std::string>>;
using DictEntry = std::pair<std::string, std::string>;

struct LangInfo {
  std::string code;
  /** the name of the language in the language itself */
  std::string name;
  /** for dates and the spoken instructions */
  std::string locale;
  /** written from right to left */
  bool rtl = false;
};

/** In the order of the language menu. */
extern const std::vector<LangInfo> LANGS;

/** The dictionary of a language, or null when it has not been loaded. */
const Dict* dictOf(const std::string& code);
/** The dictionary of a language as shipped (never null for a known code). */
const Dict& rawDictOf(const std::string& code);

LangInfo langInfo(const std::string& code);
bool isRtl(const std::string& code);
std::string localeOf(const std::string& code);

/** Register the texts of a language (a no-op if they are there). False for an unknown code. */
bool loadLang(const std::string& code);

/**
 * The first of the reader's languages (navigator.languages) that exists here –
 * or a closely related one; English otherwise.
 */
std::string pickLang(const std::vector<std::string>& preferred);

/** Name keys as keys of the keyboard or as the buttons on the screen that stand in for them. */
enum class KeyStyle { Keys, Touch };
void setKeyStyle(KeyStyle style);
KeyStyle keyStyle();

/**
 * A text: {name} placeholders are filled from `params`, [[KeyX]] becomes the
 * name of that key.
 */
std::string tr(const std::string& key, const std::string& lang);
std::string tr(const std::string& key, const std::string& lang,
               const std::vector<std::pair<std::string, std::string>>& params);

/** The label of a key of the keyboard for the texts and tables (always the key, never the touch button). */
std::string keyName(const std::string& code, const std::string& lang);

/**
 * A quantity with its unit the way the language writes it ("5,1 km",
 * "12 мин", "5.1公里") – with Western digits, like all the texts.
 * The browser's Intl would do it; here the forms the languages actually use
 * on the road and in the highway code, with the Western digits the texts keep.
 */
std::string formatUnit(double value, const std::string& unit, const std::string& lang,
                       int digits = 0);

/** Month and year of a point in time ("May 2023"), Western digits, Gregorian calendar. */
std::string formatMonth(double timeMs, const std::string& lang);

std::string violationTitle(const std::string& id, const std::string& lang,
                           const std::vector<std::pair<std::string, std::string>>& data);
std::string violationWhy(const std::string& id, const std::string& lang);

/** The rule a fault refers to (the German citation, or a word where there is none). */
std::string violationRef(const std::string& id, const std::string& lang);

std::string lessonTitle(const std::string& id, const std::string& lang);
std::string lessonIntro(const std::string& id, const std::string& lang);

}  // namespace fs

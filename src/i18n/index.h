// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/i18n/index.ts @ v1.7.7
/**
 * Texts in many languages.
 *
 * German and English are always there; every other language is a file of its
 * own that is fetched when it is chosen.  A text missing in a language falls
 * back to English.
 *
 * The texts are data, not code: tools/i18n-export.mjs writes them from the original's
 * dictionaries (src/i18n/<code>.ts there) as web/lang/<code>.json – that is what loadLang()
 * fetches, beside the page – and compiles German and English into the program (builtin.h).
 * To add a language: add it to the original (its docs/TRANSLATING.md) and run the script.
 *
 * Where the original takes `params?: Record<string, string | number>`, the port takes a
 * list of names and values (TrParams): tr("navIn", lang, {{"d", 120}, {"what", what}}).
 */
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/js.h"
#include "core/task.h"
#include "i18n/types.h"

namespace fs {

using Lang = std::string;

struct LangInfo {
  std::string code;
  /** the name of the language in the language itself */
  std::string name;
  /** for dates and the spoken instructions */
  std::string locale;
  /** written from right to left */
  bool rtl = false;
  /** fetched when it is chosen (false: part of the program – German and English) */
  bool load = false;
};

namespace detail {
const std::vector<LangInfo>& langs();
}

/** In the order of the language menu.  (Ready whenever it is asked for, also while the program starts.) */
inline const std::vector<LangInfo>& LANGS = detail::langs();

const LangInfo& langInfo(std::string_view code);
bool isRtl(std::string_view code);
const std::string& localeOf(std::string_view code);

/**
 * Fetch the texts of a language (no-op if they are there). Resolves to false if that fails.
 * The file is lang/<code>.json beside the page (i18n::langUrl); what has arrived stays, and a
 * language asked for again while it is on its way is fetched once.
 */
Task<bool> loadLang(std::string code);

/** The first of the reader's languages (navigator.languages) that exists here – or a closely related one; English otherwise. */
std::string pickLang(const std::vector<std::string>& preferred);

/** `'keys' | 'touch'` */
enum class KeyStyle { Keys, Touch };
/** Name keys as keys of the keyboard or as the buttons on the screen that stand in for them. */
void setKeyStyle(KeyStyle style);

/** The label of a key of the keyboard for the texts and tables (always the key, never the touch button). */
std::string keyName(std::string_view code, std::string_view lang);

/** One value for a text – `d: 120` is {"d", 120}.  A number is written as JavaScript writes it (String(v)). */
struct TrParam {
  std::string name;
  std::string value;

  TrParam(std::string_view name, std::string value) : name(name), value(std::move(value)) {}
  TrParam(std::string_view name, std::string_view value) : name(name), value(value) {}
  TrParam(std::string_view name, const char* value) : name(name), value(value) {}
  template <class N>
    requires(std::is_arithmetic_v<N> && !std::is_same_v<N, bool> && !std::is_same_v<N, char> && !std::is_same_v<N, wchar_t> && !std::is_same_v<N, char8_t> && !std::is_same_v<N, char16_t> &&
             !std::is_same_v<N, char32_t>)
  TrParam(std::string_view name, N number) : name(name), value(jsStr(static_cast<double>(number))) {}
};

/** `Record<string, string | number>`: the values a text is filled with, in the order they are written. */
class TrParams {
 public:
  TrParams() = default;
  TrParams(std::initializer_list<TrParam> list) : list_(list) {}
  /** (the values of a fault as the rules keep them: sim/rules.h Violation::data) */
  TrParams(const std::vector<std::pair<std::string, std::string>>& pairs) {
    list_.reserve(pairs.size());
    for (const auto& [name, value] : pairs) list_.emplace_back(name, value);
  }

  /** `params[name] = value` for a name that is not there yet */
  TrParams& add(TrParam p) {
    list_.push_back(std::move(p));
    return *this;
  }
  bool empty() const { return list_.empty(); }
  size_t size() const { return list_.size(); }
  std::vector<TrParam>::const_iterator begin() const { return list_.begin(); }
  std::vector<TrParam>::const_iterator end() const { return list_.end(); }

 private:
  std::vector<TrParam> list_;
};

/** A text: {name} placeholders are filled from `params`, [[KeyX]] becomes the name of that key. */
std::string tr(std::string_view key, std::string_view lang, const TrParams& params = {});

/** `'kilometer' | 'meter' | 'minute'` */
enum class Unit { Kilometer, Meter, Minute };

/**
 * A quantity with its unit the way the language writes it ("5,1 km",
 * "12 мин", "5.1公里") – with Western digits, like all the texts.
 */
std::string formatUnit(double value, Unit unit, std::string_view lang, int digits = 0);

/** Month and year of a point in time ("May 2023"). */
std::string formatMonth(double time, std::string_view lang);

std::string violationTitle(std::string_view id, std::string_view lang, const TrParams& data);
std::string violationWhy(std::string_view id, std::string_view lang);

/** The rule a fault refers to (the German citation, or a word where there is none). */
std::string violationRef(std::string_view id, std::string_view lang);

std::string lessonTitle(std::string_view id, std::string_view lang);
std::string lessonIntro(std::string_view id, std::string_view lang);

/** For the tests and tools: the texts of a language that has been loaded (nullptr: it has not). */
const Dict* dictOf(std::string_view code);

namespace i18n {
/** Read the texts of a language from the JSON of its file (false: that is no object of texts). */
bool parseDict(std::string_view json, Dict& out);
/**
 * The address of a file of texts beside the page: "fr.json" → <the page's folder>/lang/fr.json?v=<stamp>
 * (plat::locationBase() may end in the page's own name: the file lies beside it; the stamp
 * is that of the texts the program was built with – builtin.h, web/lang/index.json).
 */
std::string langUrl(std::string_view file);
}  // namespace i18n

}  // namespace fs

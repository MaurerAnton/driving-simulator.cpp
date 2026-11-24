// SPDX-License-Identifier: AGPL-3.0-only
/**
 * What the host knows about languages (beside plat.h): numbers, quantities, dates and
 * lists written the way a language writes them – the browser's Intl – and what is printed
 * on the keys of the reader's keyboard.
 *
 * The original asks the browser: `new Intl.NumberFormat(locale, options).format(v)`,
 * `v.toLocaleString(locale, options)`, `new Intl.DateTimeFormat(…).format(date)`,
 * `date.toLocaleDateString(…)`, `date.toLocaleTimeString(…)`, `new Intl.ListFormat(…)`.
 * The port asks the host through the three format…() hooks below; each says which call it is.
 *
 * Two hosts implement them:
 *   - the browser (platform/web): the real Intl, called with exactly the locale and options
 *     given.  optionsJson() writes an options struct as the JSON of Intl's options object, so
 *     the browser side of formatNumber can be
 *         new Intl.NumberFormat(locale, JSON.parse(json)).format(value)
 *     (with the formatter kept under `locale + json`: the display asks for the same few
 *     again and again);
 *   - this machine (platform/native/intl_native.cpp): plain rules, the same on every machine.
 *     Numbers, quantities (the units the game uses) and lists come out as CLDR writes them
 *     for every language of the game; dates in German and English (other languages get the
 *     English form), in the Gregorian calendar.  What it does not know it refuses with an
 *     IntlError rather than guessing – each hook says what the native host knows.
 *
 * The option structs carry Intl's own option names and values ("unit", "long", "2-digit" …);
 * a member left as it is ("" or -1) is not passed on, so Intl's default holds.  Written with
 * designated initialisers they read like the original's object:
 *
 *     plat::formatNumber(v, locale, {.minimumFractionDigits = d, .maximumFractionDigits = d, .numberingSystem = "latn"})
 *
 * (C++ wants the members in the order of the struct, which is the order the original mostly
 * writes them in.)
 *
 * Where Intl throws – a RangeError for a locale tag that is not well-formed or for an option
 * value it does not know (a unit, a time zone), a TypeError for options that do not go
 * together – the hooks throw IntlError: the original's `try { … } catch { … }` around a call
 * is a try/catch here.
 */
#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/task.h"

namespace fs::plat {

/** What Intl throws (RangeError, TypeError). */
struct IntlError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// --- numbers and quantities -----------------------------------------------------

/** The options of Intl.NumberFormat the game uses. */
struct NumberFormat {
  /** "decimal" (Intl's default) or "unit" */
  std::string style = "";
  /** with style "unit": "meter", "kilometer", "minute", "kilometer-per-hour" */
  std::string unit = "";
  /** with style "unit": "short" ("5 km", Intl's default) or "long" ("5 kilometres") */
  std::string unitDisplay = "";
  /** "standard" (Intl's default) or "compact" ("118K") */
  std::string notation = "";
  /** digits behind the decimal sign: at least and at most (-1: Intl's default – 0 and 3, or the other bound) */
  int minimumFractionDigits = -1;
  int maximumFractionDigits = -1;
  /** at most that many digits that count, from the first ("120K" of 120400 with 3; -1: not asked for) */
  int maximumSignificantDigits = -1;
  /** "latn": Western digits whatever the language's own are ("": the digits of the locale – Bengali for bn-BD, Persian for fa-IR) */
  std::string numberingSystem = "";
};

/**
 * `new Intl.NumberFormat(locale, options).format(value)` – which is also
 * `value.toLocaleString(locale, options)`.
 *
 * The original's calls:
 *   {min/maximumFractionDigits: d, numberingSystem: 'latn'}                          "1.234,5" (a number of the display)
 *   {minimumFractionDigits: d, maximumFractionDigits: d}                             the same in the locale's own digits
 *   {minimumFractionDigits: 1}                                                       "3,5" … "3,125" (at most 3 digits)
 *   {numberingSystem: 'latn'}                                                        "203.374"
 *   {notation: 'compact', maximumSignificantDigits: 3, numberingSystem: 'latn'}      "118K", "1,2 Mio."
 *   {style: 'unit', unit, unitDisplay: 'short', min/maximumFractionDigits: d, numberingSystem: 'latn'}   "5,1 km" (i18n formatUnit)
 *   {style: 'unit', unit, unitDisplay: 'long', maximumFractionDigits: 2, numberingSystem: 'latn'}        "200 metrów" (i18n speechtext)
 *
 * Throws IntlError where Intl throws: an ill-formed locale tag, a style, unit, unitDisplay or
 * notation it does not know, digits out of range (0 … 100, the least above the most).
 *
 * The native host: every language of the game (and "en", American English; any other language
 * is written as British English), rounded as ICU rounds (the shortest decimal form of the
 * number, halves away from zero).  Units: the four above, "short" and "long".  "compact" has
 * the words of German and English only (other languages: the English letters).  A numbering
 * system other than "latn" is taken as the locale's own.
 */
std::string formatNumber(double value, const std::string& locale, const NumberFormat& options = {});

// --- dates and times --------------------------------------------------------------

/** The options of Intl.DateTimeFormat the game uses. */
struct DateTimeFormat {
  /** "medium" ("20.11.2026", "20 Nov 2026") – not together with the single parts below */
  std::string dateStyle = "";
  /** "short" ("17:30") – not together with the single parts below */
  std::string timeStyle = "";
  /** "short" ("Fr.") or "long" ("Freitag") */
  std::string weekday = "";
  /** "numeric" or "2-digit" */
  std::string day = "";
  /** "numeric", "2-digit", "short" ("Nov") or "long" ("November") */
  std::string month = "";
  /** "numeric" or "2-digit" */
  std::string year = "";
  /** "numeric" or "2-digit" */
  std::string hour = "";
  /** "numeric" or "2-digit" */
  std::string minute = "";
  /** "UTC", "Europe/Berlin" … ("": the reader's own time zone) */
  std::string timeZone = "";
  /** "latn": Western digits whatever the language's own are */
  std::string numberingSystem = "";
  /** "gregory": the Gregorian calendar whatever the language's own is (fa-IR: the Persian one) */
  std::string calendar = "";
};

/**
 * `new Intl.DateTimeFormat(locale, options).format(new Date(ms))`.  With no part and no
 * style asked for, Intl writes the day, the month and the year in numbers ("20.11.2026").
 *
 * The same answers what the original asks of a Date itself:
 *   date.toLocaleDateString(locale)                                    = formatDate(ms, locale)
 *   date.toLocaleDateString(locale, {year, month, …})                  = formatDate(ms, locale, {…})
 *   date.toLocaleTimeString(locale, {hour: '2-digit', minute: '2-digit'}) = formatDate(ms, locale, {.hour = "2-digit", .minute = "2-digit"})
 *   date.toLocaleString(locale, {weekday, day, month, hour, minute})   = formatDate(ms, locale, {…})
 * (they differ from Intl.DateTimeFormat only in what is written when no part is named, and
 * the original names the parts wherever it asks for a time).
 *
 * The original's calls:
 *   {}                                                                 "20.11.2026" (lists of drives and exams)
 *   {month: 'long', year: 'numeric', timeZone: 'UTC', numberingSystem: 'latn', calendar: 'gregory'}   "Mai 2023" (i18n formatMonth)
 *   {day: 'numeric', month: 'long', year: 'numeric'}                   "20. November 2026" (the news)
 *   {day: 'numeric', month: 'long', year: 'numeric', hour: 'numeric', minute: '2-digit'}   "20. November 2026 um 17:30"
 *   {dateStyle: 'medium'}, {timeStyle: 'short'}, both (also with numberingSystem: 'latn', calendar: 'gregory')
 *   {weekday: 'short', day: 'numeric', month: 'numeric', hour: '2-digit', minute: '2-digit'}   "Fr., 20.11., 17:30" (the clock)
 *   {hour: '2-digit', minute: '2-digit'}                               "17:30"
 *
 * Throws IntlError where Intl throws: `ms` that is no time (NaN – where toLocaleDateString
 * would say "Invalid Date"), an ill-formed locale tag, an unknown value of an option or time
 * zone, a style together with single parts.
 *
 * The native host: German and English (en-GB, en-US, en-CA; every other language is written
 * as British English), always in the Gregorian calendar and with Western digits.  The parts
 * it knows: the day with the month, and with them the year and the weekday; month and year;
 * one part alone; the hour with the minute; dateStyle "medium", timeStyle "short".  Time
 * zones: the reader's own (plat::timezoneOffsetMin), "UTC" and "Europe/Berlin".
 */
std::string formatDate(double ms, const std::string& locale, const DateTimeFormat& options = {});

// --- lists ------------------------------------------------------------------------

/** The options of Intl.ListFormat the game uses. */
struct ListFormat {
  /** "long" (Intl's default) */
  std::string style = "";
  /** "conjunction" (Intl's default: "a, b and c") */
  std::string type = "";
};

/**
 * `new Intl.ListFormat(locale, options).format(items)` – the original's one call is
 * `{style: 'long', type: 'conjunction'}`: "Licht, Reifen und Spiegel".
 *
 * Throws IntlError where Intl throws (an ill-formed locale tag, an unknown style or type).
 * The native host: every language of the game, "long" and "conjunction" only (the words
 * between the items as CLDR has them; it does not know the rules by which a few languages
 * change the word before certain sounds – Spanish "e" for "y").
 */
std::string formatList(const std::vector<std::string>& items, const std::string& locale, const ListFormat& options = {});

// --- the keyboard -------------------------------------------------------------------

/** KeyboardLayoutMap: the code of a key ("KeyZ") → what it writes ("y" on a German keyboard) */
using KeyboardLayout = std::unordered_map<std::string, std::string>;

/**
 * `await navigator.keyboard.getLayoutMap()` with all its entries – nullopt where the browser
 * has no such thing (all but Chromium) or refuses (the original's `catch`).  Never throws.
 * The native host: nullopt, unless a test has set a layout (native::setKeyboardLayout).
 */
Task<std::optional<KeyboardLayout>> keyboardLayoutMap();

// --- the options as Intl takes them ---------------------------------------------------

namespace detail {
inline void jsonMember(std::string& json, const char* name, const std::string& value) {
  if (value.empty()) return;
  json += json.size() > 1 ? ",\"" : "\"";
  json += name;
  json += "\":\"";
  for (char c : value) {
    if (c == '"' || c == '\\') json += '\\';
    json += c;
  }
  json += '"';
}
inline void jsonMember(std::string& json, const char* name, int value) {
  if (value < 0) return;
  json += json.size() > 1 ? ",\"" : "\"";
  json += name;
  json += "\":";
  json += std::to_string(value);
}
}  // namespace detail

/** The options as the JSON of Intl's options object: only what is set, e.g. {"style":"unit","unit":"meter","unitDisplay":"long"}. */
inline std::string optionsJson(const NumberFormat& o) {
  std::string json = "{";
  detail::jsonMember(json, "style", o.style);
  detail::jsonMember(json, "unit", o.unit);
  detail::jsonMember(json, "unitDisplay", o.unitDisplay);
  detail::jsonMember(json, "notation", o.notation);
  detail::jsonMember(json, "minimumFractionDigits", o.minimumFractionDigits);
  detail::jsonMember(json, "maximumFractionDigits", o.maximumFractionDigits);
  detail::jsonMember(json, "maximumSignificantDigits", o.maximumSignificantDigits);
  detail::jsonMember(json, "numberingSystem", o.numberingSystem);
  return json + "}";
}
inline std::string optionsJson(const DateTimeFormat& o) {
  std::string json = "{";
  detail::jsonMember(json, "dateStyle", o.dateStyle);
  detail::jsonMember(json, "timeStyle", o.timeStyle);
  detail::jsonMember(json, "weekday", o.weekday);
  detail::jsonMember(json, "day", o.day);
  detail::jsonMember(json, "month", o.month);
  detail::jsonMember(json, "year", o.year);
  detail::jsonMember(json, "hour", o.hour);
  detail::jsonMember(json, "minute", o.minute);
  detail::jsonMember(json, "timeZone", o.timeZone);
  detail::jsonMember(json, "numberingSystem", o.numberingSystem);
  detail::jsonMember(json, "calendar", o.calendar);
  return json + "}";
}
inline std::string optionsJson(const ListFormat& o) {
  std::string json = "{";
  detail::jsonMember(json, "style", o.style);
  detail::jsonMember(json, "type", o.type);
  return json + "}";
}

// --- the native host only (tests) -------------------------------------------------------

namespace native {
/** what keyboardLayoutMap() answers on this machine (nullopt, as at the start: the host does not tell) */
void setKeyboardLayout(std::optional<KeyboardLayout> layout);
}  // namespace native

}  // namespace fs::plat

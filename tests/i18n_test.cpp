// SPDX-License-Identifier: AGPL-3.0-only
// LINK: i18n core platform
/**
 * The texts: every language is complete and well-formed, and the machinery
 * around them (fallback, placeholders, key names) works; the texts made ready
 * to be read out (units in words, the ordinals of the roundabout exits, symbols
 * and key codes gone, long texts in pieces).
 *
 * Mirrors the original's tests/i18n/i18n.test.ts and tests/i18n/speechtext.test.ts – the
 * same cases, the same texts – and its tools/i18n-check.ts, the checker of a translation,
 * which those tests run over every language (checkDict below: the same rules, the same
 * messages).  Here they run on the language files of web/lang, fetched through the native
 * host as the program fetches them, with the native host's Intl (platform/intl.h).
 *
 * Besides: what only the port has – the files against what was compiled in, the reader of
 * the files, the words of the learning modes, the citations, the native host's Intl.
 *
 *   tools/t.sh i18n_test
 */
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/js.h"
#include "i18n/builtin.h"
#include "i18n/exam/index.h"
#include "i18n/index.h"
#include "i18n/json.h"
#include "i18n/keys.h"
#include "i18n/speechtext.h"
#include "i18n/unicode.h"
#include "i18n/violations.h"
#include "platform/intl.h"
#include "platform/json.h"
#include "platform/native/native.h"
#include "platform/plat.h"

using namespace fs;
using i18n::decodeUtf8;
using i18n::encodeUtf8;

namespace {

// --- what a test says when it fails ------------------------------------------------------

[[noreturn]] void failed(const char* file, int line, const std::string& what) {
  std::fprintf(stderr, "%s:%d: %s\n", file, line, what.c_str());
  std::abort();
}
std::string note() { return ""; }
std::string note(const std::string& m) { return "\n  (" + m + ")"; }

#define OK(cond, ...) \
  do { \
    if (!(cond)) failed(__FILE__, __LINE__, std::string("not true: " #cond) + note(__VA_ARGS__)); \
  } while (0)
#define EQ(is, wanted, ...) \
  do { \
    const std::string is_ = (is); \
    const std::string wanted_ = (wanted); \
    if (is_ != wanted_) failed(__FILE__, __LINE__, std::string(#is) + "\n  is:     \"" + is_ + "\"\n  wanted: \"" + wanted_ + "\"" + note(__VA_ARGS__)); \
  } while (0)

// --- characters that cannot be seen, by their names -----------------------------------------

const std::string LRI = "\xe2\x81\xa6";    // U+2066 left-to-right isolate
const std::string PDI = "\xe2\x81\xa9";    // U+2069 pop directional isolate
const std::string NBSP = "\xc2\xa0";       // U+00A0 no-break space
const std::string RLM = "\xe2\x80\x8f";    // U+200F right-to-left mark
const std::string SECTION = "\xc2\xa7";    // U+00A7 §

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
bool anyOf(const std::string& s, const std::vector<std::pair<char32_t, char32_t>>& ranges) {
  for (char32_t c : decodeUtf8(s))
    for (const auto& [from, to] : ranges)
      if (c >= from && c <= to) return true;
  return false;
}
/** `s.slice(from, to)` (UTF-16 units; a negative `from` counts from the end) */
std::string slice16(const std::string& s, long from, long to = 1L << 30) {
  const std::u16string u = i18n::toUtf16(s);
  const long n = static_cast<long>(u.size());
  if (from < 0) from = std::max(0L, n + from);
  from = std::min(from, n);
  to = std::min(to, n);
  return to <= from ? "" : i18n::fromUtf16(std::u16string_view(u).substr(static_cast<size_t>(from), static_cast<size_t>(to - from)));
}

std::string readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  OK(static_cast<bool>(f), path + " cannot be read (run the test in the folder of the repository)");
  std::ostringstream text;
  text << f.rdbuf();
  return text.str();
}

/** `await` */
template <class T>
T await(Task<T> task) {
  return plat::native::wait(std::move(task));
}

const Dict& en() { return *dictOf("en"); }
const std::string& enText(const std::string& key) {
  const std::string* text = en().find(key);
  OK(text != nullptr, key + " is not in English");
  return *text;
}

// ============================================================================================
// tools/i18n-check.ts: checks a translation against the English reference.
//
// A language is complete when it has exactly the keys of en.ts, every text
// keeps its {placeholders} and [[key]] tokens, the names and numbers that
// must not change are still there, and the short labels are short.
// See docs/TRANSLATING.md of the original.
// ============================================================================================

struct Report {
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
};

/** Names that are the same in every language. */
const std::vector<std::string> KEEP = {
    "StVO", "§", "Baunatal", "Fahrsim", "Panoramax", "KartaView", "Mapillary", "OpenStreetMap", "ODbL", "AGPL-3.0-only", "three.js", "MIT", "Wikimedia Commons", "CC BY-SA 4.0",
    "EXIF", "XMP", "GeoJSON", "JSON", "CSV", "JPEG", "PNG", "WebP", "WebGL 2", "▲", "▼", "⋯", "DGM1", "LoD2", "UrhG", "Marktplatz 5", "A 49", "Logitech", "Thrustmaster", "Fanatec",
    "RT", "LT", "RB/LB", "X/B", "Anlieger frei", "P·R·N·D", "360°", "IP", "Kassel", "CE", "BE", "C1E", "D1E", "C1", "D1", "A1", "A2", "AM", "DE",
};

/**
 * Longest text (in characters) that still fits where these keys are shown.  A key is one of
 * `keys` ("*" stands for \w+), or – `prefix` – begins with it; the first line that fits counts.
 */
struct Limit {
  std::vector<const char*> keys;
  bool prefix;
  size_t max;
};
const std::vector<Limit> MAX = {
    {{"tbGas", "tbBrake"}, false, 9},
    {{"limitNone", "walk"}, false, 14},
    {{"key."}, true, 12},
    {{"qLow", "qMedium", "qHigh", "mOff"}, false, 16},
    {{"kr."}, true, 26},
    {{"tb"}, true, 34},
    {{"start", "free", "exam", "settings", "controls", "about", "resume", "menu", "paused", "back", "result", "passed", "failed", "faults", "points", "distance", "duration", "hint",
      "minor", "major", "fail", "reality", "realityClose"},
     false, 34},
    {{"realityCopy", "realityCopied"}, false, 16},
    {{"mapTitle"}, false, 22},
    {{"viewChase", "viewCockpit", "enginePower", "startView", "mapPath", "mapClearPath", "logClear", "logTitle", "realityPlace", "mapTeleport", "endDrive", "mapCentre", "mapPlace"}, false, 36},
    {{"realityAerial"}, false, 44},
    {{"navLeft", "navRight", "navUturn", "navFollow", "navMotorway", "laneLeft", "laneRight", "routeDone"}, false, 60},
    {{"lesson.*.title"}, false, 60},
    {{"demo", "demoStart", "demoClear", "demoArrived", "demoStop", "demoTaken"}, false, 34},
    {{"demoAdd"}, false, 40},
    {{"demoRunning"}, false, 120},
    {{"demo.*"}, false, 96},
    {{"vehicle"}, false, 30},
    {{"vehCar", "vehTruck"}, false, 46},
    {{"ce.corrections", "ce.gap"}, false, 46},
    {{"ce.onFoot"}, false, 64},
    {{"ce.step."}, true, 84},
    {{"ce.do.*", "ce.again"}, false, 200},
    {{"v.*.title"}, false, 80},
};

bool wordChar(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }

/** Is the key the pattern ("*": one or more characters of a word)? */
bool keyIs(const std::string& key, const std::string& pattern) {
  const size_t star = pattern.find('*');
  if (star == std::string::npos) return key == pattern;
  const std::string head = pattern.substr(0, star), tail = pattern.substr(star + 1);
  if (key.size() < head.size() + tail.size() + 1 || key.compare(0, head.size(), head) != 0 || key.compare(key.size() - tail.size(), tail.size(), tail) != 0) return false;
  for (size_t i = head.size(); i < key.size() - tail.size(); i++)
    if (!wordChar(key[i])) return false;
  return true;
}
bool fits(const Limit& l, const std::string& key) {
  for (const char* k : l.keys)
    if (l.prefix ? startsWith(key, k) : keyIs(key, k)) return true;
  return false;
}

/** Scripts: most texts of these languages must contain letters of the script (catches untranslated texts). */
std::vector<std::pair<char32_t, char32_t>> scriptOf(const std::string& code) {
  if (code == "ru" || code == "uk" || code == "bg") return {{0x0400, 0x04FF}};
  if (code == "el") return {{0x0370, 0x03FF}};
  if (code == "ar" || code == "fa" || code == "ur") return {{0x0600, 0x06FF}};
  if (code == "bn") return {{0x0980, 0x09FF}};
  if (code == "hi") return {{0x0900, 0x097F}};
  if (code == "zh") return {{0x4E00, 0x9FFF}};
  if (code == "ja") return {{0x3040, 0x30FF}, {0x4E00, 0x9FFF}};
  if (code == "ko") return {{0xAC00, 0xD7AF}};
  return {};
}

/** Languages whose letters are as wide as they are high: fewer of them fit. */
bool wide(const std::string& code) { return code == "zh" || code == "ja" || code == "ko"; }

/** Digits other than 0–9, and characters that change the writing direction. */
const std::vector<std::pair<char32_t, char32_t>> FOREIGN_DIGITS = {{0x0660, 0x0669}, {0x06F0, 0x06F9}, {0x0966, 0x096F}, {0x09E6, 0x09EF}, {0xFF10, 0xFF19}};
const std::vector<std::pair<char32_t, char32_t>> BIDI_CONTROLS = {{0x200E, 0x200F}, {0x202A, 0x202E}, {0x2066, 0x2069}};

/** `[...s.matchAll(re)].map((m) => m[0]).sort()` for /\{\w+\}/g (open "{", close "}") and /\[\[\w+\]\]/g */
std::vector<std::string> tokens(const std::string& s, const std::string& open, const std::string& close, std::string* without = nullptr) {
  std::vector<std::string> found;
  std::string rest;
  for (size_t i = 0; i < s.size();) {
    if (s.compare(i, open.size(), open) == 0) {
      size_t j = i + open.size();
      while (j < s.size() && wordChar(s[j])) j++;
      if (j > i + open.size() && s.compare(j, close.size(), close) == 0) {
        found.push_back(s.substr(i, j + close.size() - i));
        i = j + close.size();
        continue;
      }
    }
    rest += s[i++];
  }
  std::sort(found.begin(), found.end());
  if (without) *without = rest;
  return found;
}
/** `s.replace(/\[\[\w+\]\]/g, '').replace(/\{\w+\}/g, '')`: without its key tokens, and then without its placeholders */
std::string bare(const std::string& s) {
  std::string noKeys, noBraces;
  tokens(s, "[[", "]]", &noKeys);
  tokens(noKeys, "{", "}", &noBraces);
  return noBraces;
}
/** `s.replace(/\[\[\w+\]\]|\{\w+\}/g, '')`: without both, in one go */
std::string bareAtOnce(const std::string& s) {
  std::string rest;
  for (size_t i = 0; i < s.size();) {
    const std::string open = s.compare(i, 2, "[[") == 0 ? "[[" : s[i] == '{' ? "{" : "";
    const std::string close = open == "[[" ? "]]" : "}";
    size_t j = i + open.size();
    while (!open.empty() && j < s.size() && wordChar(s[j])) j++;
    if (!open.empty() && j > i + open.size() && s.compare(j, close.size(), close) == 0) i = j + close.size();
    else rest += s[i++];
  }
  return rest;
}
std::string listOf(const std::vector<std::string>& t) { return t.empty() ? "none" : join(t, " "); }

/** How a sentence may end (in any of the scripts): /[.!?…。！？।؟۔;]["”»“’')）」』]*$/ */
bool sentenceEnds(const std::u32string& v) {
  size_t n = v.size();
  auto closer = [](char32_t c) { return c == U'"' || c == 0x201D || c == 0xBB || c == 0x201C || c == 0x2019 || c == U'\'' || c == U')' || c == 0xFF09 || c == 0x300D || c == 0x300F; };
  while (n > 0 && closer(v[n - 1])) n--;
  if (n == 0) return false;
  const char32_t c = v[n - 1];
  // (the Greek question mark is ";" – U+037E, which most keyboards write as the semicolon)
  return c == U'.' || c == U'!' || c == U'?' || c == 0x2026 || c == 0x3002 || c == 0xFF01 || c == 0xFF1F || c == 0x0964 || c == 0x061F || c == 0x06D4 || c == U';';
}

/** a run of at least `least` letters of the kind */
template <class Kind>
bool run(const std::u32string& s, size_t least, Kind kind) {
  size_t n = 0;
  for (char32_t c : s) {
    n = kind(c) ? n + 1 : 0;
    if (n >= least) return true;
  }
  return false;
}
bool ascii(char32_t c) { return (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z'); }

Report checkDict(const std::string& code, const Dict& d, const Dict& ref) {
  Report r;
  for (const auto& [k, source] : ref)
    if (!d.has(k)) r.errors.push_back(k + ": missing");
  for (const auto& [k, text] : d)
    if (!ref.has(k)) r.errors.push_back(k + ": not a key of en.ts");
  int scriptOk = 0;
  int scriptAll = 0;
  int identical = 0;
  const std::vector<std::pair<char32_t, char32_t>> script = scriptOf(code);
  for (const auto& [k, source] : ref) {
    const std::string* found = d.find(k);
    if (!found) continue;
    const std::string& v = *found;
    const std::u32string v32 = decodeUtf8(v), source32 = decodeUtf8(source);
    const size_t length = v32.size();   // [...v].length
    if (i18n::trimJs(v).empty()) r.errors.push_back(k + ": empty");
    if (v != i18n::trimJs(v)) r.errors.push_back(k + ": starts or ends with white space");
    if (v.find_first_of("<>") != std::string::npos) r.errors.push_back(k + ": contains < or > (no HTML in texts)");
    if (contains(v, "\n")) r.errors.push_back(k + ": contains a line break");
    if (tokens(v, "{", "}") != tokens(source, "{", "}")) r.errors.push_back(k + ": placeholders differ from English (" + listOf(tokens(source, "{", "}")) + ")");
    if (tokens(v, "[[", "]]") != tokens(source, "[[", "]]")) r.errors.push_back(k + ": key tokens differ from English (" + listOf(tokens(source, "[[", "]]")) + ")");
    {
      const std::string rest = bare(v);
      if (contains(rest, "[[") || contains(rest, "]]") || contains(rest, "{") || contains(rest, "}")) r.errors.push_back(k + ": stray brackets or braces");
    }
    for (const std::string& name : KEEP)
      if (contains(source, name) && !contains(v, name)) r.errors.push_back(k + ": \"" + name + "\" must stay as it is");
    // numbers of two and more digits (sign numbers, speeds) must all be there
    // (written the language's way: 7,5 where English writes 7.5)
    {
      // source.replace(/\[\[\w+\]\]|\{\w+\}/g, '').match(/\d[\d.]*\d/g)
      const std::string s = bareAtOnce(source);
      auto digit = [&](size_t i) { return i < s.size() && s[i] >= '0' && s[i] <= '9'; };
      for (size_t i = 0; i < s.size();) {
        if (!digit(i)) {
          i++;
          continue;
        }
        size_t j = i + 1;
        while (digit(j) || (j < s.size() && s[j] == '.')) j++;
        while (j > i + 1 && !digit(j - 1)) j--;
        if (j - i < 2) {
          i++;
          continue;
        }
        const std::string n = s.substr(i, j - i);
        const std::string ways[] = {n, replaceAll(n, ".", ","), replaceAll(n, ".", "\xd9\xab"), replaceAll(n, ".", "")};   // (U+066B: the Arabic decimal sign)
        bool there = false;
        for (const std::string& w : ways) there = there || contains(v, w);
        if (!there) r.errors.push_back(k + ": the number " + n + " is missing");
        i = j;
      }
    }
    for (const Limit& l : MAX) {
      if (!fits(l, k)) continue;
      if (length > l.max) r.errors.push_back(k + ": " + std::to_string(length) + " characters, at most " + std::to_string(l.max) + " fit (\"" + v + "\")");
      break;
    }
    // the roundel of the speed limit: the text may wrap at spaces, but a word must fit its width
    if (k == "limitNone" || k == "walk") {
      size_t longest = 0, word = 0;
      for (char32_t c : v32) {
        word = i18n::isSpace(c) ? 0 : word + 1;
        longest = std::max(longest, word);
      }
      const size_t room = wide(code) ? 4 : 9;
      if (longest > room) r.errors.push_back(k + ": the word of " + std::to_string(longest) + " characters does not fit the roundel (at most " + std::to_string(room) + ") (\"" + v + "\")");
    }
    if (anyOf(v, FOREIGN_DIGITS)) r.errors.push_back(k + ": digits must be the Western ones (0–9)");
    if (anyOf(v, BIDI_CONTROLS)) r.errors.push_back(k + ": contains an invisible direction mark");
    if (contains(v, "  ")) r.errors.push_back(k + ": two spaces in a row");
    // a sentence stays a sentence, a label stays a label
    if (code != "en") {
      const char32_t last = source32.empty() ? 0 : source32.back();
      const bool sentence = last == U'.' || last == U'!' || last == U'?' || last == 0x2026;
      if (sentence && !sentenceEnds(v32)) r.warnings.push_back(k + ": the English text ends a sentence, this one does not (\"…" + slice16(v, -24) + "\")");
      // (not the pedal labels, which may be abbreviated, and not "2." – an ordinal)
      const char32_t vLast = v32.empty() ? 0 : v32.back();
      const bool stop = vLast == U'.' || vLast == 0x3002 || vLast == 0x0964 || vLast == 0x06D4;
      const bool sourceStop = endsWith(source, ".") || endsWith(source, ".)");
      const bool ordinal = v.size() >= 2 && v.back() == '.' && v[v.size() - 2] >= '0' && v[v.size() - 2] <= '9';
      if (!sentence && stop && !sourceStop && !ordinal && k != "tbGas" && k != "tbBrake")
        r.warnings.push_back(k + ": the English text is a label without a full stop, this one ends with one (\"…" + slice16(v, -24) + "\")");
      // parts of a sentence ("turn left") are put behind other words: no capital letter
      if (!source.empty() && source[0] >= 'a' && source[0] <= 'z' && v32.size() >= 2 && i18n::isUpperLetter(v32[0]) && i18n::isLowerLetter(v32[1]) && code != "de")
        r.warnings.push_back(k + ": starts with a capital letter, but it is put into the middle of a sentence (\"" + slice16(v, 0, 24) + "…\")");
    }
    if (code != "en" && v == source && run(decodeUtf8(bareAtOnce(source)), 4, ascii) && std::find(KEEP.begin(), KEEP.end(), source) == KEEP.end()) identical++;
    if (!script.empty() && run(source32, 3, [](char32_t c) { return ascii(c) || (c >= 0xC0 && c <= 0x24F); })) {
      scriptAll++;
      if (anyOf(v, script)) scriptOk++;
      else if (length > 14) r.warnings.push_back(k + ": no letters of the language's script (\"" + slice16(v, 0, 50) + "\")");
    }
  }
  // the list of controls names the gearbox setting as the settings do
  const std::string* manual = d.find("trManual");
  const std::string* clutch = d.find("ctl.clutch");
  if (manual && clutch && !contains(*clutch, *manual)) r.errors.push_back("ctl.clutch: must quote the text of trManual (\"" + *manual + "\")");
  if (identical > 12) r.warnings.push_back(std::to_string(identical) + " texts are identical to the English ones – untranslated?");
  if (scriptAll > 0 && scriptOk < scriptAll * 0.93) r.errors.push_back("only " + std::to_string(scriptOk) + " of " + std::to_string(scriptAll) + " texts are written in the script of the language");
  return r;
}
Report checkDict(const std::string& code, const Dict& d) { return checkDict(code, d, en()); }

/** Codes of all language files next to en.json. */
std::vector<std::string> languageFiles(const std::string& dir) {
  std::vector<std::string> codes;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    const std::string name = entry.path().filename().string();
    if (!entry.is_regular_file() || !endsWith(name, ".json")) continue;
    const std::string code = name.substr(0, name.size() - 5);
    bool letters = code.size() >= 2 && code.size() <= 3;
    for (char c : code) letters = letters && c >= 'a' && c <= 'z';
    if (letters) codes.push_back(code);
  }
  std::sort(codes.begin(), codes.end());
  return codes;
}

// ============================================================================================
// tests/i18n/i18n.test.ts
// ============================================================================================

/** The lessons (src/game/nav.ts LESSONS – the game is not linked into this test: their ids as of v1.7.7). */
const std::vector<std::string> LESSON_IDS = {"basics", "rbl", "priority", "signals", "roundabout", "rural", "motorway", "exam", "ceCouple", "ceCorner", "ceRamp",
                                             "bParallel", "bBay", "bReverse", "bTurn", "bHill", "bBrake", "bDrills", "tramLine", "busLine", "ceExam"};

/** (the port's own) a file that is not there is no language – and is asked for again the next time */
void loadsOnlyWhatIsThere() {
  // German and English need no file
  OK(await(loadLang("de")) && await(loadLang("en")));
  OK(dictOf("de") && dictOf("en") && !dictOf("fr"));
  EQ(tr("paused", "de"), "Pause");
  // nothing is mounted yet: the request fails, the language is not there
  OK(await(loadLang("fr")) == false, "without its file a language cannot be loaded");
  OK(!dictOf("fr"));
  EQ(tr("paused", "fr"), "Paused", "… and its texts are the English ones");
  // the files of the program: lang/ beside the page (plat::locationBase() is http://localhost/ on this host)
  plat::native::mount("http://localhost/lang/", "web/lang/");
  EQ(i18n::langUrl("fr.json"), std::string("http://localhost/lang/fr.json?v=") + i18n::BUILTIN_STAMP);
  // asked for twice at once: one answer for both
  Task<bool> first = loadLang("fr");
  Task<bool> second = loadLang("fr");
  OK(!first.done() && !second.done(), "the file arrives later");
  OK(await(first) && await(second));
  OK(dictOf("fr") != nullptr);
  OK(loadLang("fr").done(), "what has arrived is there at once");
}

void everyLanguageFileIsRegistered() {
  // 'every language file is registered, and every registered language loads'
  const std::vector<std::string> files = languageFiles("web/lang");
  std::vector<std::string> codes;
  for (const LangInfo& l : LANGS) codes.push_back(l.code);
  std::sort(codes.begin(), codes.end());
  OK(codes == files, "LANGS (src/i18n/builtin.cpp) and the files in web/lang must match: run tools/i18n-export.mjs");
  OK(std::set<std::string>(codes.begin(), codes.end()).size() == LANGS.size(), "no language twice");
  for (const LangInfo& l : LANGS) {
    OK(await(loadLang(l.code)), l.code + " cannot be loaded");
    OK(i18n::utf16Length(l.name) > 1 && startsWith(l.locale, l.code), l.code + ": name and locale");
    // the locale must be one the browser's date and speech functions understand
    try {
      plat::formatDate(0, l.locale, {.month = "long"});
    } catch (const plat::IntlError& e) {
      failed(__FILE__, __LINE__, l.code + ": locale " + l.locale + ": " + e.what());
    }
  }
  OK(await(loadLang("xx")) == false, "an unknown language is refused");
  OK(langInfo("xx").code == "en" && &langInfo("de") == &LANGS[0]);
}

/** (the port's own) web/lang/index.json and the files say what the program was built with */
void theFilesAreTheProgramsOwn() {
  Json index;
  OK(Json::parse(readFile("web/lang/index.json"), index) && index.isObject());
  EQ(index.at("stamp").asStr(), i18n::BUILTIN_STAMP, "the texts in web/lang are not those the program was built with: run tools/i18n-export.mjs and build again");
  EQ(index.at("source").asStr(), std::string("driving-simulator ") + i18n::BUILTIN_SOURCE);
  OK(index.at("keys").asNum() == static_cast<double>(en().size()));
  const std::vector<Json>& langs = index.at("langs").array;
  OK(langs.size() == LANGS.size());
  for (size_t i = 0; i < langs.size(); i++) {
    const LangInfo& l = LANGS[i];
    EQ(langs[i].at("code").asStr(), l.code);
    EQ(langs[i].at("name").asStr(), l.name);
    EQ(langs[i].at("locale").asStr(), l.locale);
    OK(langs[i].at("rtl").boolean == l.rtl && langs[i].at("builtin").boolean == !l.load, l.code);
  }
  const std::vector<Json>& exam = index.at("exam").array;
  OK(exam.size() == EXAM_LANGS.size());
  for (size_t i = 0; i < exam.size(); i++) EQ(exam[i].asStr(), EXAM_LANGS[i]);
  // what is compiled in is the file: the same texts in the same order
  for (const LangInfo& l : LANGS) {
    Dict file;
    OK(i18n::parseDict(readFile("web/lang/" + l.code + ".json"), file), l.code);
    const Dict* here = dictOf(l.code);
    OK(here && here->entries() == file.entries(), l.code + ": what is loaded is the file, entry by entry");
  }
  ExamTexts file;
  OK(i18n::parseExamTexts(readFile("web/lang/exam/en.json"), file));
  OK(file.questions.entries() == EXAM_EN.questions.entries() && file.danger.entries() == EXAM_EN.danger.entries() && file.signs.entries() == EXAM_EN.signs.entries());
}

void everyLanguageIsComplete() {
  // 'every language is complete: same keys, placeholders, key tokens, names, numbers, lengths'
  for (const LangInfo& l : LANGS) {
    OK(await(loadLang(l.code)));
    const Report report = checkDict(l.code, *dictOf(l.code));
    if (!report.errors.empty()) {
      for (const std::string& e : report.errors) std::fprintf(stderr, "%s: ERROR – %s\n", l.code.c_str(), e.c_str());
      failed(__FILE__, __LINE__, l.name + " (" + l.code + "): " + std::to_string(report.errors.size()) + " error(s)");
    }
  }
}

void theCatalogueOfFaultsAndTheLessons() {
  // 'the catalogue of faults and the lessons have their texts in the reference'
  for (const auto& [id, ref] : VIOLATION_REFS) {
    OK(!enText("v." + id + ".title").empty(), "v." + id + ".title");
    OK(!enText("v." + id + ".why").empty(), "v." + id + ".why");
    OK(i18n::utf16Length(violationRef(id, "en")) > 3, "rule of " + id);
  }
  for (const auto& [key, text] : en()) {
    // /^v\.(\w+)\.title$/
    if (!keyIs(key, "v.*.title")) continue;
    OK(VIOLATION_REFS.has(key.substr(2, key.size() - 8)), key + " has no rule");
  }
  for (const std::string& id : LESSON_IDS) {
    OK(!enText("lesson." + id + ".title").empty() && !enText("lesson." + id + ".intro").empty(), "lesson " + id);
    EQ(lessonTitle(id, "en"), enText("lesson." + id + ".title"));
    OK(!contains(lessonIntro(id, "de"), "[["), "key tokens are resolved in the introduction");
  }
  // (the lessons of the texts are those of the list above)
  for (const auto& [key, text] : en())
    if (keyIs(key, "lesson.*.title")) OK(std::find(LESSON_IDS.begin(), LESSON_IDS.end(), key.substr(7, key.size() - 13)) != LESSON_IDS.end(), key + ": a lesson this test does not know");
}

void textsPlaceholdersFallback() {
  // 'texts: placeholders, fallback to English, rule references'
  EQ(tr("navIn", "en", {{"d", 120}, {"what", "turn left"}}), "In 120 m turn left");
  EQ(tr("navIn", "de", {{"d", 120}, {"what", "links abbiegen"}}), "In 120 m links abbiegen");
  EQ(violationTitle("speed", "en", {{"speed", 62}, {"limit", 50}}), "Too fast: 62 where 50 km/h is allowed");
  OK(startsWith(violationWhy("redLight", "de"), "Rot"));
  EQ(violationRef("redLight", "de"), "§ 37 Abs. 2 StVO", "the citation is the same in every language");
  EQ(violationRef("stall", "de"), "Fahrzeugbedienung");
  EQ(violationRef("stall", "en"), "Vehicle handling");
  EQ(tr("no.such.key", "de"), "no.such.key", "an unknown key shows itself");
  EQ(tr("paused", "xx"), "Paused", "an unknown language falls back to English");
  // (the port's own: numbers are written as JavaScript writes them; the values of a fault come as the rules keep them)
  EQ(tr("navIn", "en", {{"d", 0.1 + 0.2}, {"what", std::string("x")}}), "In 0.30000000000000004 m x");
  EQ(tr("navIn", "en", {{"d", 1e21}, {"what", std::string_view("y")}}), "In 1e+21 m y");
  const std::vector<std::pair<std::string, std::string>> data = {{"speed", "62"}, {"limit", "50"}};
  EQ(violationTitle("speed", "en", data), "Too fast: 62 where 50 km/h is allowed");
  EQ(tr("navIn", "en"), "In {d} m {what}", "what is not given stays");
}

void keysAreNamed() {
  // 'keys are named as printed on the keyboard usual for the language – or as the button on a touch screen'
  setKeyStyle(KeyStyle::Keys);
  EQ(tr("fastenBelt", "en"), "Fasten your seat belt: B");
  EQ(tr("releaseHandbrake", "de"), "Handbremse lösen: Leertaste");
  EQ(tr("releaseHandbrake", "en"), "Release the handbrake: Space");
  // the key left of X: Z on a US keyboard, Y on a German one
  EQ(keyName("KeyZ", "en") + keyName("KeyZ", "de") + keyName("KeyY", "de"), "ZYZ");
  EQ(keyName("Digit1", "en") + "|" + keyName("ArrowUp", "en") + "|" + keyName("F1", "en") + "|" + keyName("ShiftLeft", "de") + "|" + keyName("Escape", "en"), "1|↑|F1|Umschalt|Esc");
  OK(await(loadLang("fr")));
  // French keyboards: the driving keys W A S D carry Z Q S D
  EQ(keyName("KeyW", "fr") + keyName("KeyA", "fr") + keyName("KeyS", "fr") + keyName("KeyD", "fr") + keyName("KeyQ", "fr"), "ZQSDA");
  setKeyStyle(KeyStyle::Touch);
  EQ(tr("releaseHandbrake", "en"), "Release the handbrake: P");
  EQ(tr("basics.belt", "en"), "Fasten your seat belt: press ⋯ Seat belt");
  EQ(tr("startEngine", "en"), "Start the engine: hold ⋯ Starter", "the button is named without its hint in brackets");
  EQ(tr("basics.pullAway", "de"), "Sanft anfahren: etwas Gas (GAS), bis 15 km/h");
  EQ(keyName("KeyB", "en"), "B", "tables of keys always show the key");
  setKeyStyle(KeyStyle::Keys);

  // (the port's own) where the browser tells what the keys write (Chromium), that is what the texts say
  OK(await(plat::keyboardLayoutMap()) == std::nullopt, "this host does not tell by itself");
  plat::native::setKeyboardLayout(plat::KeyboardLayout{{"KeyZ", "y"}, {"KeyW", "ц"}, {"KeyS", "ß"}, {"KeyA", "ab"}, {"KeyQ", ""}, {"Period", ":"}});
  await(initKeyLabels());
  EQ(keyName("KeyZ", "en"), "Y", "the letter of the reader's own keyboard, as a capital");
  EQ(keyName("KeyW", "en"), "Ц");
  EQ(keyName("KeyS", "en"), "SS", "toUpperCase() of the one letter");
  EQ(keyName("KeyA", "fr") + keyName("KeyQ", "fr"), "QA", "what is not one letter is not taken: the layout usual for the language");
  EQ(keyName("Period", "en") + keyName("KeyM", "fr") + keyName("Space", "en"), ":,Space");
  EQ(tr("fastenBelt", "en"), "Fasten your seat belt: B");
  // (a browser that does not tell leaves what was known; an empty answer is "no key known")
  plat::native::setKeyboardLayout(std::nullopt);
  await(initKeyLabels());
  EQ(keyName("KeyZ", "en"), "Y");
  plat::native::setKeyboardLayout(plat::KeyboardLayout{});
  await(initKeyLabels());
  EQ(keyName("KeyZ", "en") + keyName("KeyZ", "de") + keyName("Period", "en"), "ZY.");
  plat::native::setKeyboardLayout(std::nullopt);
}

void theLanguageIsChosen() {
  // 'the language is chosen from what the browser prefers'
  EQ(pickLang({"de-AT", "en"}), "de");
  EQ(pickLang({"en-US"}), "en");
  EQ(pickLang({"xx", "yy"}), "en", "nothing known: English");
  EQ(pickLang({"sr-RS", "en"}), "hr", "a closely related language before a foreign one");
  EQ(pickLang({"gsw-CH"}), "de");
  EQ(pickLang({}), "en");
  for (const LangInfo& l : LANGS) EQ(pickLang({l.code + "-XX", "en"}), l.code);
  OK(isRtl("de") == false);
  EQ(localeOf("de"), "de-DE");
  OK(isRtl("ar") == true);
  // (the port's own: the case of a tag does not count)
  EQ(pickLang({"PT-br"}), "pt");
}

void rightToLeft() {
  // 'right-to-left languages: citations stay in one left-to-right piece'
  bool arabic = false;
  for (const LangInfo& l : LANGS) {
    if (!l.rtl) continue;
    arabic = arabic || l.code == "ar";
    OK(await(loadLang(l.code)));
    EQ(violationRef("redLight", l.code), LRI + "§ 37 Abs. 2 StVO" + PDI, l.code);
    OK(contains(tr("lesson.rbl.intro", l.code), LRI + SECTION + NBSP + "8" + NBSP + "StVO" + PDI), l.code + ": the paragraph in the lesson text");
    OK(contains(tr("about.signs", l.code), LRI + SECTION + NBSP + "5" + NBSP + "(1)" + NBSP + "UrhG" + PDI), l.code + ": the paragraph in the credits");
    OK(!contains(violationRef("stall", l.code), LRI) && !contains(violationRef("stall", l.code), PDI), "a word needs no mark");
  }
  OK(arabic, "Arabic is written from right to left");
  EQ(violationRef("redLight", "en"), "§ 37 Abs. 2 StVO", "nothing is added in left-to-right languages");
  OK(!contains(tr("lesson.rbl.intro", "de"), LRI) && !contains(tr("lesson.rbl.intro", "de"), PDI));
}

void theCheckerFindsWhatCanGoWrong() {
  // 'the checker finds what can go wrong in a translation'
  struct Change {
    std::string key;
    /** nullptr: the key is taken away */
    const char* text;
  };
  auto broken = [](const std::vector<Change>& changes) {
    Dict d = en();
    for (const Change& c : changes) {
      if (c.text == nullptr) d.erase(c.key);
      else d.set(c.key, c.text);
    }
    return checkDict("xx", d).errors;
  };
  auto first = [&](const Change& c) {
    const std::vector<std::string> errors = broken({c});
    OK(!errors.empty(), c.key + ": the checker has nothing to say");
    return errors[0];
  };
  OK(broken({}).empty(), "the reference itself is clean");
  OK(contains(first({"paused", nullptr}), "paused: missing"));
  OK(contains(first({"extra", "x"}), "not a key"));
  OK(contains(first({"navIn", "In {dist} m {what}"}), "placeholders differ"));
  OK(contains(first({"fastenBelt", "Fasten your seat belt: B"}), "key tokens differ"));
  OK(contains(first({"fastenBelt", "Fasten your seat belt: [[KeyB]] [[KeyB]]"}), "key tokens differ"));
  OK(contains(first({"tbGas", "ACCELERATOR"}), "at most 9 fit"));
  OK(contains(first({"walk", "walkingspeed"}), "does not fit the roundel"));
  OK(contains(first({"lesson.motorway.title", "7 · Motorway"}), "\"A 49\" must stay"));
  {
    std::string why = enText("v.stopSign.why");
    OK(contains(why, "206"));
    why.replace(why.find("206"), 3, "207");
    OK(contains(first({"v.stopSign.why", why.c_str()}), "number 206 is missing"));
  }
  OK(contains(first({"paused", "Paused <b>"}), "no HTML"));
  OK(contains(first({"paused", " Paused"}), "white space"));
  {
    std::string why = enText("v.speed.why");
    OK(contains(why, "50"));
    why.replace(why.find("50"), 2, "\xd9\xa5\xd9\xa0");   // "٥٠": 50 in Arabic-Indic digits
    OK(contains(join(broken({{"v.speed.why", why.c_str()}}), "|"), "Western"));
  }
  OK(contains(first({"paused", ("Paused" + RLM).c_str()}), "direction mark"));
  OK(contains(first({"trManual", "Manual gearbox"}), "ctl.clutch: must quote"));
  // a language with its own script must be written in it
  OK(contains(join(checkDict("ru", en()).errors, "|"), "written in the script"));
  // hints, not errors: sentence ends and capital letters
  Dict d = en();
  d.set("paused", "Paused.");
  d.set("navLeft", "Turn left");
  {
    std::string desc = enText("freeDesc");
    OK(endsWith(desc, "."));
    desc.pop_back();
    d.set("freeDesc", desc);
  }
  const std::string hints = join(checkDict("xx", d).warnings, "|");
  auto hint = [&](const std::string& key, const std::string& word) {
    const size_t at = hints.find(key + ": ");
    return at != std::string::npos && hints.find(word, at) != std::string::npos;
  };
  OK(hint("paused", "label"));
  OK(hint("navLeft", "capital"));
  OK(hint("freeDesc", "sentence"));
}

/** `s.replace(/\s/g, ' ')` */
std::string plain(const std::string& s) {
  std::u32string u = decodeUtf8(s);
  for (char32_t& c : u)
    if (i18n::isSpace(c)) c = U' ';
  return encodeUtf8(u);
}

void quantitiesAndDates() {
  // 'quantities and dates are written the way the language writes them, with Western digits'
  EQ(plain(formatUnit(5.12, Unit::Kilometer, "de", 1)), "5,1 km");
  EQ(plain(formatUnit(5.12, Unit::Kilometer, "en", 1)), "5.1 km");
  {
    // /^12\s?min/
    const std::string t = plain(formatUnit(12, Unit::Minute, "en"));
    OK(startsWith(t, "12 min") || startsWith(t, "12min"), t);
  }
  EQ(plain(formatUnit(40, Unit::Meter, "de")), "40 m");
  {
    // /^5,1\s?км$/
    const std::string t = plain(formatUnit(5.12, Unit::Kilometer, "ru", 1));
    OK(t == "5,1 км" || t == "5,1км", t);
  }
  const double may = 1684281600000.0;   // Date.UTC(2023, 4, 17)
  EQ(formatMonth(may, "de"), "Mai 2023");
  EQ(formatMonth(may, "en"), "May 2023");
  for (const LangInfo& l : LANGS) {
    const std::string texts[] = {formatUnit(1234.5, Unit::Kilometer, l.code, 1), formatUnit(7, Unit::Minute, l.code), formatUnit(35, Unit::Meter, l.code), formatMonth(may, l.code)};
    for (const std::string& t : texts) {
      OK(t.find_first_of("0123456789") != std::string::npos, l.code + ": \"" + t + "\" has Western digits");
      OK(!anyOf(t, {{0x0660, 0x0669}, {0x06F0, 0x06F9}, {0x0966, 0x096F}}), l.code + ": \"" + t + "\"");
    }
    OK(contains(texts[3], "2023"), l.code + ": the year");
  }
  // a value is put in as it is, whatever it contains
  EQ(tr("navIn", "en", {{"d", 50}, {"what", "cost $& more {d}"}}), "In 50 m cost $& more {d}");
}

/**
 * this->t("key"), tr("key", …), and "@key" (a text named inside a table): the keys a source asks for.
 * (A key that is put together – tr("lesson." + id + ".title") – is none: the original writes those
 * with template literals, which its search does not take either.)
 */
void asksFor(const std::string& code, std::set<std::string>& asked) {
  const size_t n = code.size();
  // /(?:\bt|\btr)\(\s*"([\w.]+)"/g, the text being the whole argument
  for (size_t i = 0; i < n; i++) {
    if (code[i] != 't' || (i > 0 && wordChar(code[i - 1]))) continue;
    size_t j = i + 1;
    if (j < n && code[j] == 'r') j++;
    if (j >= n || code[j] != '(') continue;
    j++;
    while (j < n && (code[j] == ' ' || code[j] == '\t' || code[j] == '\n' || code[j] == '\r')) j++;
    if (j >= n || code[j] != '"') continue;
    size_t k = j + 1;
    while (k < n && (wordChar(code[k]) || code[k] == '.')) k++;
    if (k == j + 1 || k >= n || code[k] != '"') continue;
    size_t behind = k + 1;
    while (behind < n && (code[behind] == ' ' || code[behind] == '\t' || code[behind] == '\n' || code[behind] == '\r')) behind++;
    if (behind < n && (code[behind] == ',' || code[behind] == ')')) asked.insert(code.substr(j + 1, k - j - 1));
  }
  // /"[^"\n]*@(\w[\w.]*)"/g
  for (size_t i = 0; i < n;) {
    if (code[i] != '"') {
      i++;
      continue;
    }
    size_t end = i + 1;
    while (end < n && code[end] != '"' && code[end] != '\n') end++;
    const size_t at = end < n && code[end] == '"' ? code.rfind('@', end) : std::string::npos;
    bool key = at != std::string::npos && at > i && at + 1 < end && wordChar(code[at + 1]);
    for (size_t k = at + 1; key && k < end; k++) key = wordChar(code[k]) || code[k] == '.';
    if (!key) {
      i++;
      continue;
    }
    asked.insert(code.substr(at + 1, end - at - 1));
    i = end + 1;
  }
}

void everyTextTheProgramAsksFor() {
  // 'every text the program asks for exists'
  std::set<std::string> asked;
  size_t files = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator("src")) {
    const std::string path = entry.path().string();
    if (!entry.is_regular_file() || !(endsWith(path, ".cpp") || endsWith(path, ".h"))) continue;
    files++;
    asksFor(readFile(path), asked);
  }
  // (the original looks for more than a hundred texts in its code; the port grows towards that – what must be
  // found today are the texts its own tables name: the buttons of keys.cpp, the words of violations.cpp)
  OK(files > 10 && asked.count("tbStarter") && asked.count("tbGas") && asked.count("ref.vehicle") && asked.count("ref.crane"), "the search does not find the texts the code asks for");
  {
    std::set<std::string> sample;
    asksFor("x = this->t(\"paused\"); y = tr( \"lesson.rbl.title\", lang); z = \"⋯ @tbMenu\"; q = split(\"a.b\"); w = \"@ not a key\"; str(\"menu\"); tr(\"v.\" + id)", sample);
    OK(sample == std::set<std::string>({"paused", "lesson.rbl.title", "tbMenu"}));
  }
  std::string missing;
  for (const std::string& k : asked)
    if (!en().has(k)) missing += (missing.empty() ? "" : ", ") + k;
  EQ(missing, "", "asked for in the code, missing in the English texts");
}

// ============================================================================================
// tests/i18n/speechtext.test.ts
// ============================================================================================

std::string say(const std::string& key, const std::string& lang, const TrParams& p = {}) { return speakable(tr(key, lang, p), lang); }

void unitsAreReadAsWords() {
  // 'units are read as words, in the grammar of the language'
  EQ(speakable("In 200 m turn right", "en"), "In 200 metres turn right");
  EQ(speakable("Innerorts mindestens 1,5 m, außerorts mindestens 2 m.", "de"), "Innerorts mindestens 1,5 Meter, außerorts mindestens 2 Meter.");
  EQ(speakable("Za 200 m skręć w prawo", "pl"), "Za 200 metrów skręć w prawo");
  EQ(speakable("Через 200 м поверните", "ru"), "Через 200 метров поверните");
  EQ(speakable("Τουλάχιστον 1,5 m μέσα στην πόλη", "el"), "Τουλάχιστον 1,5 μέτρα μέσα στην πόλη");
  EQ(speakable("200 m先、右折", "ja"), "200 メートル先、右折");
  EQ(speakable("Mit 30 km/h, vorsichtig", "de"), "Mit 30 Kilometer pro Stunde, vorsichtig");
  EQ(speakable("30 km/h", "tr"), "saatte 30 kilometre");
  OK(!contains(speakable("30 km/h", "vi"), "/"));
  // (not a unit: a word that begins with the letter)
  EQ(speakable("5 min", "en"), "5 min");
  EQ(speakable("1 мин", "ru"), "1 мин");
}

void theExitOfARoundabout() {
  // 'the exit of a roundabout is an ordinal word in the right case'
  EQ(speakable("im Kreisverkehr die 2. Ausfahrt nehmen", "de"), "im Kreisverkehr die zweite Ausfahrt nehmen");
  EQ(speakable("en la rotonda, toma la 3.ª salida", "es"), "en la rotonda, toma la tercera salida");
  EQ(speakable("na kruhovém objezdu vyjeď 1. výjezdem", "cs"), "na kruhovém objezdu vyjeď prvním výjezdem");
  EQ(speakable("на круговом движении выберите 2-й съезд", "ru"), "на круговом движении выберите второй съезд");
  EQ(speakable("di bundaran ambil jalan keluar ke-1", "id"), "di bundaran ambil jalan keluar pertama");
  // (only before the noun: a full stop after a number stays one)
  EQ(speakable("Es sind 2. Dann weiter.", "de"), "Es sind 2. Dann weiter.");
}

void symbolsDashesQuotes() {
  // 'symbols, dashes, quotes and key codes are not read as such'
  EQ(speakable("Wer darf als Nächstes – du oder das weiße Auto?", "de"), "Wer darf als Nächstes, du oder das weiße Auto?");
  EQ(speakable("Am Schild „Vorfahrt gewähren“.", "de"), "Am Schild Vorfahrt gewähren.");
  EQ(speakable("in deinen toten Winkel (orange).", "de"), "in deinen toten Winkel, orange.");
  EQ(speakable("Press [[KeyG]] ★★☆ · then ↑", "en"), "Press G, then");
  EQ(speakable(LRI + "§ 37 StVO" + PDI, "de"), "§ 37 Straßenverkehrsordnung");
  // (the Persian zero-width non-joiner belongs to the words)
  EQ(speakable("می\342\200\214رسند", "fa"), "می\342\200\214رسند");
  EQ(speakable("Trả lời đúng 3/5", "vi"), "Trả lời đúng 3 trên 5");
}

void everyLanguageSpokenTextsClean() {
  // 'every language: the spoken texts come out clean'
  for (const LangInfo& l : LANGS) {
    OK(await(loadLang(l.code)));
    const std::string texts[] = {
        say("navIn", l.code, {{"d", 200}, {"what", tr("navRight", l.code)}}),
        say("navNow", l.code, {{"what", tr("navRoundabout", l.code, {{"n", 2}})}}),
        say("theory.overtake.2.why", l.code),
        say("theory.score", l.code, {{"right", 3}, {"count", 5}}),
        say("theory.roundabout.intro", l.code),
    };
    for (const std::string& s : texts) {
      OK(i18n::utf16Length(s) > 3, l.code + ": " + s);
      // /[{}[\]„“”«»–—]|\d\s?(m|м|م)(?![\p{L}])/u
      const std::u32string u = decodeUtf8(s);
      for (size_t i = 0; i < u.size(); i++) {
        const char32_t c = u[i];
        const bool sign = c == U'{' || c == U'}' || c == U'[' || c == U']' || c == 0x201E || c == 0x201C || c == 0x201D || c == 0xAB || c == 0xBB || c == 0x2013 || c == 0x2014;
        bool unit = false;
        if (i18n::isDigit(c)) {
          for (size_t at : {i + 1, i + 2}) {
            if (at == i + 2 && !(i + 1 < u.size() && i18n::isSpace(u[i + 1]))) continue;
            unit = unit || (at < u.size() && (u[at] == U'm' || u[at] == 0x43C || u[at] == 0x645) && !(at + 1 < u.size() && i18n::isLetter(u[at + 1])));
          }
        }
        OK(!sign && !unit, l.code + ": " + s);
      }
    }
  }
}

void longTextsAreCut() {
  // 'long texts are cut into pieces a voice reads in one go'
  std::string s;
  for (int i = 0; i < 30; i++) s += "Eins zwei drei. ";
  s = trim(s);
  const std::vector<std::string> parts = splitForSpeech(s, 60);
  OK(parts.size() > 1);
  for (const std::string& p : parts) OK(i18n::utf16Length(p) <= 60, p);
  EQ(join(parts, " "), s);
  // (a single long sentence: at its commas, else at a space)
  std::vector<std::string> list;
  for (int i = 0; i < 20; i++) list.push_back("part " + std::to_string(i));
  for (const std::string& p : splitForSpeech(join(list, ", "), 50)) OK(i18n::utf16Length(p) <= 50, p);
  std::string words;
  for (int i = 0; i < 60; i++) words += "word ";
  for (const std::string& p : splitForSpeech(trim(words), 50)) OK(i18n::utf16Length(p) <= 50, p);
  // (scripts without spaces)
  std::string zh;
  for (int i = 0; i < 10; i++) zh += "城区内至少1.5米。城区外至少2米。";
  const std::vector<std::string> zp = splitForSpeech(zh, 40);
  OK(zp.size() > 1);
  for (const std::string& p : zp) OK(i18n::utf16Length(p) <= 40, p);
  EQ(join(zp, ""), zh);
  // (short texts stay whole; the decimal point is no sentence end)
  OK(splitForSpeech("At least 1.5 metres. Go.") == std::vector<std::string>({"At least 1.5 metres. Go."}));
  OK(splitForSpeech("   ").empty());

  // (the port's own: lengths are counted as JavaScript counts them, and a character beyond U+FFFF stays whole)
  std::string faces;
  for (int i = 0; i < 12; i++) faces += "\xf0\x9f\x9a\x97";   // 12 × U+1F697, 24 units of UTF-16
  const std::vector<std::string> cars = splitForSpeech(faces, 9);
  OK(cars.size() == 3);
  for (const std::string& p : cars) OK(i18n::utf16Length(p) == 8, "four whole characters in each piece");
  EQ(join(cars, ""), faces);
}

// ============================================================================================
// The port's own
// ============================================================================================

void theReaderOfTheFiles() {
  Dict d;
  OK(i18n::parseDict("{}", d) && d.empty());
  OK(i18n::parseDict(" {\n\"b\" : \"1\",\n\"a\":\"2\" , \"10\":\"x\", \"2\":\"y\"}\n", d));
  // (the order of JavaScript: the keys that are numbers first, in their order, then the others as they were written)
  OK(d.keys() == std::vector<std::string>({"2", "10", "b", "a"}));
  // escapes: the short ones, a character by its number, one beyond U+FFFF by its two halves
  OK(i18n::parseDict("{\"k\":\"a\\\"b\\\\c\\/d\\n\\t\\u00e4\\u20AC\\ud83d\\ude97\"}", d));
  EQ(*d.find("k"), "a\"b\\c/d\n\tä€\xf0\x9f\x9a\x97");
  OK(i18n::parseDict("{\"a\":\"1\",\"a\":\"2\"}", d) && d.size() == 1 && *d.find("a") == "2", "the later of two wins, as in JavaScript");
  // what is not an object of texts is refused, and what was there stays
  for (const char* bad : {"", "[]", "{\"a\":1}", "{\"a\":\"1\",}", "{\"a\":\"1\"", "{\"a\":\"1\"} x", "{a:\"1\"}", "{\"a\":\"line\nbreak\"}", "{\"a\":\"\\x\"}", "{\"a\":\"\\u12\"}", "{\"a\" \"1\"}", "null"})
    OK(!i18n::parseDict(bad, d), std::string("taken for a file of texts: ") + bad);
  OK(d.size() == 1);
  // a value of any kind is stepped over where it is not asked for
  i18n::JsonCursor c("{\"a\":[1,-2.5e3,true,null,{\"b\":\"]\"}],\"c\":\"d\"}");
  std::string seen;
  OK(c.object([&](const std::string& name) {
    seen += name;
    return c.skip();
  }) && c.atEnd());
  EQ(seen, "ac");
  for (const char* bad : {"[1,]", "[01]", "[1.]", "[tru]", "{\"a\":}", "[-]"}) {
    i18n::JsonCursor broken(bad);
    OK(!(broken.skip() && broken.atEnd()), std::string("taken for JSON: ") + bad);
  }

  // Record: looked up, changed, taken away – the others keep their order
  Record<std::string> r = {{"x", "1"}, {"y", "2"}, {"z", "3"}};
  r.set("y", "two");
  OK(r.erase("x") && !r.erase("x") && !r.has("x") && r.find("x") == nullptr);
  r.set("7", "seven");
  r.set("x", "again");
  r.set("3", "three");
  OK(r.keys() == std::vector<std::string>({"3", "7", "y", "z", "x"}) && r.at("y") == "two" && r.size() == 5);
  OK(r.erase("3") && r.keys() == std::vector<std::string>({"7", "y", "z", "x"}) && *r.find("x") == "again");
  r.set("007", "no number");
  r.set("4294967295", "too great for an index");
  OK(r.keys() == std::vector<std::string>({"7", "y", "z", "x", "007", "4294967295"}));
  bool thrown = false;
  try {
    r.at("nothing");
  } catch (const std::out_of_range&) {
    thrown = true;
  }
  OK(thrown);
  OK(Record<std::string>({{"a", "1"}, {"b", "2"}}) == Record<std::string>({{"b", "2"}, {"a", "1"}}));
}

void theWordsOfTheLearningModes() {
  OK(EXAM_EN.questions.size() > 100 && EXAM_EN.danger.size() > 20 && EXAM_EN.signs.size() > 150);
  OK(EXAM_LANGS.size() == LANGS.size() && EXAM_LANGS[0] == "en");
  // the signs are read in the order of JavaScript: the numbers first ("101" … "720"), then "103-10" …
  OK(EXAM_EN.signs.keys()[0] == "101" && EXAM_EN.signs.has("1020-30") && EXAM_EN.signs.has("325.1"));
  {
    const std::vector<std::string> keys = EXAM_EN.signs.keys();
    size_t numbers = 0;
    while (numbers < keys.size() && keys[numbers].find_first_not_of("0123456789") == std::string::npos) numbers++;
    OK(numbers > 0 && numbers < keys.size());
    for (size_t i = 1; i < numbers; i++) OK(std::stoi(keys[i - 1]) < std::stoi(keys[i]));
    for (size_t i = numbers; i < keys.size(); i++) OK(keys[i].find_first_not_of("0123456789") != std::string::npos, keys[i]);
  }
  // every language has the words of every question, sign and danger, answer by answer
  for (const std::string& lang : EXAM_LANGS) {
    const ExamTexts t = await(loadExamTexts(lang));
    OK(t.questions.keys() == EXAM_EN.questions.keys() && t.danger.keys() == EXAM_EN.danger.keys() && t.signs.keys() == EXAM_EN.signs.keys(), lang);
    for (const Record<ExamText>* part : {&t.questions, &t.danger}) {
      const Record<ExamText>& ref = part == &t.questions ? EXAM_EN.questions : EXAM_EN.danger;
      for (const auto& [id, w] : *part) OK(!w.q.empty() && !w.why.empty() && w.a.size() == ref.at(id).a.size(), lang + ": " + id);
    }
    if (lang != "en") OK(t.questions.at("dan01").q != EXAM_EN.questions.at("dan01").q, lang + ": its own words");
    // (its own file, not English beneath it)
    ExamTexts own;
    OK(i18n::parseExamTexts(readFile("web/lang/exam/" + lang + ".json"), own) && own.questions.size() == EXAM_EN.questions.size(), lang);
  }
  OK(await(loadExamTexts("xx")).questions == EXAM_EN.questions, "a language without words of its own: English");
  {
    // (asked for again: from what has arrived)
    Task<ExamTexts> again = loadExamTexts("de");
    OK(again.done() && again.get().signs.at("205") != EXAM_EN.signs.at("205"));
  }

  // tests/game/exam.test.ts: 'a translation lacking words falls back to English, answer by answer'
  const std::string id = EXAM_EN.questions.keys()[0];
  const ExamText& english = EXAM_EN.questions.at(id);
  ExamTexts own;
  own.questions.set(id, ExamText{"Frage?", {"Eins"}, ""});
  own.signs.set("205", "Vorfahrt gewähren");
  const ExamTexts t = withFallback(&own);
  EQ(t.questions.at(id).q, "Frage?");
  EQ(t.questions.at(id).a[0], "Eins");
  EQ(t.questions.at(id).a[1], english.a[1]);
  EQ(t.questions.at(id).why, english.why);
  EQ(t.signs.at("205"), "Vorfahrt gewähren");
  EQ(t.signs.at("206"), EXAM_EN.signs.at("206"));
  OK(t.danger.size() == EXAM_EN.danger.size());
  OK(t.questions.keys() == EXAM_EN.questions.keys() && t.signs.keys() == EXAM_EN.signs.keys(), "the order is English's");
  OK(withFallback(nullptr).questions == EXAM_EN.questions);
  // (a question English does not have is taken as it is; a new sign goes where JavaScript puts it)
  own.questions.set("new1", ExamText{"Neu?", {"a", "b"}, "weil"});
  own.signs.set("100", "vor allen");
  own.signs.set("x-1", "hinter allen");
  const ExamTexts more = withFallback(&own);
  OK(more.questions.keys().back() == "new1" && more.questions.at("new1").a.size() == 2);
  OK(more.signs.keys()[0] == "100" && more.signs.keys().back() == "x-1");

  // a file that is not of the shape is no words (and English is shown)
  ExamTexts keep;
  for (const char* bad : {"", "[]", "{\"questions\":[]}", "{\"signs\":{\"205\":5}}", "{\"questions\":{\"a\":{\"q\":1}}}", "{\"danger\":{\"a\":{\"a\":\"x\"}}}"}) OK(!i18n::parseExamTexts(bad, keep), bad);
  OK(i18n::parseExamTexts("{\"more\":[1,2],\"questions\":{\"a\":{\"why\":\"w\",\"extra\":null,\"a\":[\"1\",\"2\"]}}}", keep) && keep.questions.at("a").a.size() == 2 && keep.questions.at("a").q.empty());
}

void theCitations() {
  // tests/game/playtest.test.ts: 'every citation of the StVO leads to the text of its paragraph'
  auto same = [](const CitationPart& p, const std::string& text, const std::optional<std::string>& url) { return p.text == text && p.url == url; };
  {
    const std::vector<CitationPart> p = citationParts("§ 37 Abs. 2 StVO");
    OK(p.size() == 2 && same(p[0], "§ 37 Abs. 2", "https://www.gesetze-im-internet.de/stvo_2013/__37.html") && same(p[1], " StVO", std::nullopt));
  }
  {
    std::string linked;
    for (const CitationPart& p : citationParts("§ 1 Abs. 2, § 3 Abs. 2a StVO"))
      if (p.url) linked += p.text + "→" + p.url->substr(p.url->rfind('/') + 1) + ";";
    EQ(linked, "§ 1 Abs. 2→__1.html;§ 3 Abs. 2a→__3.html;");
  }
  EQ(citationParts("§ 21a Abs. 1 StVO")[0].url.value(), "https://www.gesetze-im-internet.de/stvo_2013/__21a.html");
  OK(same(citationParts("§ 9 Abs. 1, Abs. 3 StVO")[0], "§ 9 Abs. 1, Abs. 3", "https://www.gesetze-im-internet.de/stvo_2013/__9.html"));
  OK(same(citationParts("Anlage 2 StVO, Zeichen 206")[0], "Anlage 2", "https://www.gesetze-im-internet.de/stvo_2013/anlage_2.html"));
  for (const auto& [id, ref] : VIOLATION_REFS) {
    const std::vector<CitationPart> parts = citationParts(ref);
    std::string whole;
    bool link = false;
    for (const CitationPart& p : parts) {
      whole += p.text;
      link = link || p.url.has_value();
      if (!p.url) continue;
      // (the StVO, or – for the tasks of the truck test – the annex of the driving-licence regulation)
      // /^https:\/\/www\.gesetze-im-internet\.de\/(stvo_2013\/(__\d+[a-z]?|anlage_\d)|fev_2010\/anlage_\d|strabbo_1987\/__\d+)\.html$/
      const std::string& u = *p.url;
      const std::string site = "https://www.gesetze-im-internet.de/";
      OK(startsWith(u, site) && endsWith(u, ".html"), u);
      const std::string page = u.substr(site.size(), u.size() - site.size() - 5);
      auto digits = [](const std::string& s, size_t from, bool letter) {
        size_t i = from;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++;
        if (i == from) return false;
        if (letter && i < s.size() && s[i] >= 'a' && s[i] <= 'z') i++;
        return i == s.size();
      };
      const bool known = (startsWith(page, "stvo_2013/__") && digits(page, 12, true)) || (startsWith(page, "stvo_2013/anlage_") && page.size() == 18 && digits(page, 17, false)) ||
                         (startsWith(page, "fev_2010/anlage_") && page.size() == 17 && digits(page, 16, false)) || (startsWith(page, "strabbo_1987/__") && digits(page, 15, false));
      OK(known, id + ": " + u);
    }
    EQ(whole, ref, id + ": nothing of the citation is lost");
    if (contains(ref, "§") || contains(ref, "Anlage")) OK(link, id + ": \"" + ref + "\" has no link");
  }
  // (the tram's rules link to the tram regulation)
  EQ(citationParts("§ 54 Abs. 2 BOStrab")[0].url.value(), "https://www.gesetze-im-internet.de/strabbo_1987/__54.html");

  // tests/game/ce.test.ts: 'the faults of the truck lessons cite the test regulations and the StVO, with links'
  const std::vector<CitationPart> fev = citationParts(VIOLATION_REFS.at("ceAir"));
  EQ(fev[0].text, "Anlage 7");
  EQ(fev[0].url.value(), "https://www.gesetze-im-internet.de/fev_2010/anlage_7.html");
  // an annex of the StVO still leads to the StVO
  EQ(citationParts("Anlage 3 StVO, Zeichen 325.1")[0].url.value(), "https://www.gesetze-im-internet.de/stvo_2013/anlage_3.html");
  {
    std::string urls;
    for (const CitationPart& p : citationParts(VIOLATION_REFS.at("truckSpeed")))
      if (p.url) urls += *p.url + " ";
    EQ(urls, "https://www.gesetze-im-internet.de/stvo_2013/__3.html https://www.gesetze-im-internet.de/stvo_2013/__18.html ");
  }
  EQ(citationParts(VIOLATION_REFS.at("truckRightTurn"))[0].url.value(), "https://www.gesetze-im-internet.de/stvo_2013/__9.html");

  // tests/game/theoryscenes.test.ts: the criminal code for first aid
  {
    std::vector<std::string> urls;
    for (const CitationPart& p : citationParts("§ 34 Abs. 1 StVO, § 323c StGB"))
      if (p.url) urls.push_back(*p.url);
    OK(urls.size() == 2 && urls[0] == "https://www.gesetze-im-internet.de/stvo_2013/__34.html" && urls[1] == "https://www.gesetze-im-internet.de/stgb/__323c.html");
  }
  OK(citationParts("").empty() && citationParts("Zusatzzeichen 1020-30").size() == 1 && !citationParts("Zusatzzeichen 1020-30")[0].url);
  // (the table itself: in the order of the original's, each id once)
  OK(VIOLATION_REFS.size() > 120 && VIOLATION_REFS.keys()[0] == "speed" && VIOLATION_REFS.at("stall") == "@ref.vehicle");
}

/** what a call that Intl refuses says */
template <class F>
std::string refused(F call) {
  try {
    call();
  } catch (const plat::IntlError& e) {
    return e.what();
  }
  return "";
}

void theNativeHostsIntl() {
  using plat::formatDate;
  using plat::formatList;
  using plat::formatNumber;
  // numbers: the signs of the language, rounded as a browser rounds (1.005 is "1.01", 1.45 is "1.5")
  EQ(formatNumber(1234.5678, "de-DE"), "1.234,568");
  EQ(formatNumber(1234.5678, "en-GB"), "1,234.568");
  EQ(formatNumber(1.45, "de-DE", {.minimumFractionDigits = 1, .maximumFractionDigits = 1, .numberingSystem = "latn"}), "1,5");
  EQ(formatNumber(1.005, "en", {.maximumFractionDigits = 2}), "1.01");
  EQ(formatNumber(3, "de-DE", {.minimumFractionDigits = 1}), "3,0");
  EQ(formatNumber(-0.04, "en-GB", {.minimumFractionDigits = 1, .maximumFractionDigits = 1}), "-0.0");
  EQ(formatNumber(1234.5, "es-ES") + " " + formatNumber(12345.5, "es-ES"), "1234,5 12.345,5", "Spanish groups from five digits on");
  EQ(formatNumber(1234567.5, "fr-FR"), "1\xe2\x80\xaf" "234\xe2\x80\xaf" "567,5");
  EQ(formatNumber(1234567, "hi-IN"), "12,34,567");
  EQ(formatNumber(12.5, "bn-BD"), "\xe0\xa7\xa7\xe0\xa7\xa8.\xe0\xa7\xab", "the language's own digits …");
  EQ(formatNumber(12.5, "bn-BD", {.numberingSystem = "latn"}), "12.5", "… unless Western ones are asked for");
  // tests/game/releases.test.ts: the words of a version's size
  EQ(formatNumber(172000, "en-GB", {.numberingSystem = "latn"}), "172,000");
  EQ(formatNumber(120400, "en-GB", {.notation = "compact", .maximumSignificantDigits = 3, .numberingSystem = "latn"}), "120k");
  EQ(formatNumber(17000, "en-GB", {.notation = "compact", .maximumSignificantDigits = 3, .numberingSystem = "latn"}), "17k");
  EQ(formatNumber(11.6, "en-GB", {.minimumFractionDigits = 1, .maximumFractionDigits = 1, .numberingSystem = "latn"}), "11.6");
  EQ(formatNumber(1.45, "de-DE", {.minimumFractionDigits = 1, .maximumFractionDigits = 1, .numberingSystem = "latn"}) + " MB", "1,5 MB");
  EQ(formatNumber(1234567, "de-DE", {.notation = "compact", .maximumSignificantDigits = 3}), "1,23" + NBSP + "Mio.");
  EQ(formatNumber(999999, "en", {.notation = "compact"}), "1M");
  // quantities: the plural form of the language
  EQ(formatNumber(1, "en-GB", {.style = "unit", .unit = "meter", .unitDisplay = "long"}), "1 metre");
  EQ(formatNumber(21, "ru-RU", {.style = "unit", .unit = "kilometer", .unitDisplay = "long"}), "21 километр");
  EQ(formatNumber(22, "ru-RU", {.style = "unit", .unit = "kilometer", .unitDisplay = "long"}), "22 километра");
  EQ(formatNumber(25, "ru-RU", {.style = "unit", .unit = "kilometer", .unitDisplay = "long"}), "25 километров");
  EQ(formatNumber(5.1, "zh-CN", {.style = "unit", .unit = "kilometer"}), "5.1公里");
  OK(refused([] { formatNumber(1, "de", {.style = "unit", .unit = "parsec"}); }) != "");
  OK(refused([] { formatNumber(1, "de", {.style = "unit"}); }) != "");
  OK(refused([] { formatNumber(1, "de_DE"); }) == "Invalid language tag: de_DE");
  OK(refused([] { formatNumber(1, "de", {.minimumFractionDigits = 3, .maximumFractionDigits = 1}); }) != "");

  // dates (the clock of this host is Germany's: plat::timezoneOffsetMin)
  const double evening = 1795192200000.0;   // 2026-11-20 17:30 in Germany (16:30 UTC), a Friday
  EQ(formatDate(evening, "de-DE"), "20.11.2026");
  EQ(formatDate(evening, "en-GB"), "20/11/2026");
  EQ(formatDate(evening, "de-DE", {.day = "numeric", .month = "long", .year = "numeric"}), "20. November 2026");
  EQ(formatDate(evening, "de-DE", {.day = "numeric", .month = "long", .year = "numeric", .hour = "numeric", .minute = "2-digit"}), "20. November 2026 um 17:30");
  EQ(formatDate(evening, "en-GB", {.day = "numeric", .month = "long", .year = "numeric", .hour = "numeric", .minute = "2-digit"}), "20 November 2026 at 17:30");
  EQ(formatDate(evening, "de-DE", {.dateStyle = "medium", .timeStyle = "short"}), "20.11.2026, 17:30");
  EQ(formatDate(evening, "en-GB", {.dateStyle = "medium", .timeStyle = "short", .numberingSystem = "latn", .calendar = "gregory"}), "20 Nov 2026, 17:30");
  EQ(formatDate(evening, "en-US", {.timeStyle = "short"}), "5:30 PM");
  // tests/ui/arcade.test.ts: the clock in the HUD names the day and the hour
  EQ(formatDate(evening, "de-DE", {.weekday = "short", .day = "numeric", .month = "numeric", .hour = "2-digit", .minute = "2-digit"}), "Fr., 20.11., 17:30");
  EQ(formatDate(evening, "en-GB", {.hour = "2-digit", .minute = "2-digit"}), "17:30");
  EQ(formatDate(evening, "de-DE", {.hour = "2-digit", .minute = "2-digit", .timeZone = "UTC"}), "16:30");
  // tests/game/versions.test.ts: the day as it was in Germany
  EQ(formatDate(1795215600000.0, "en-CA", {.day = "2-digit", .month = "2-digit", .year = "numeric", .timeZone = "Europe/Berlin"}), "2026-11-21", "23:00 UTC is the next day in Germany");
  EQ(formatDate(evening, "fr-FR", {.month = "long", .year = "numeric"}), "November 2026", "a language this host has no dates of: English");
  OK(refused([] { formatDate(JS_NAN, "de"); }) == "Invalid time value");
  OK(refused([] { formatDate(0, "de", {.dateStyle = "medium", .year = "numeric"}); }) != "");
  OK(refused([] { formatDate(0, "de", {.timeZone = "Mars/Phobos"}); }) != "");
  OK(contains(refused([] { formatDate(0, "de", {.hour = "numeric"}); }), "not known to the native host"), "what this host does not know it says so");

  // lists
  EQ(formatList({"Licht", "Reifen", "Spiegel"}, "de-DE", {.style = "long", .type = "conjunction"}), "Licht, Reifen und Spiegel");
  EQ(formatList({"lights", "tyres", "mirrors"}, "en-GB") + "|" + formatList({"lights", "tyres", "mirrors"}, "en"), "lights, tyres and mirrors|lights, tyres, and mirrors");
  EQ(formatList({}, "de") + "|" + formatList({"a"}, "de") + "|" + formatList({"a", "b"}, "de"), "|a|a und b");
  OK(refused([] { formatList({"a"}, "de", {.type = "weird"}); }) != "");

  // the options as the browser side takes them
  EQ(plat::optionsJson(plat::NumberFormat{}), "{}");
  EQ(plat::optionsJson(plat::NumberFormat{.style = "unit", .unit = "meter", .unitDisplay = "long", .maximumFractionDigits = 2, .numberingSystem = "latn"}),
     "{\"style\":\"unit\",\"unit\":\"meter\",\"unitDisplay\":\"long\",\"maximumFractionDigits\":2,\"numberingSystem\":\"latn\"}");
  EQ(plat::optionsJson(plat::DateTimeFormat{.month = "long", .year = "numeric", .timeZone = "UTC"}), "{\"month\":\"long\",\"year\":\"numeric\",\"timeZone\":\"UTC\"}");
  EQ(plat::optionsJson(plat::ListFormat{.style = "long", .type = "conjunction"}), "{\"style\":\"long\",\"type\":\"conjunction\"}");
}

}  // namespace

int main() {
  // the port's own: the files, and what is compiled in
  loadsOnlyWhatIsThere();
  theReaderOfTheFiles();
  // tests/i18n/i18n.test.ts
  everyLanguageFileIsRegistered();
  theFilesAreTheProgramsOwn();
  everyLanguageIsComplete();
  theCatalogueOfFaultsAndTheLessons();
  textsPlaceholdersFallback();
  keysAreNamed();
  theLanguageIsChosen();
  rightToLeft();
  theCheckerFindsWhatCanGoWrong();
  quantitiesAndDates();
  everyTextTheProgramAsksFor();
  // tests/i18n/speechtext.test.ts
  unitsAreReadAsWords();
  theExitOfARoundabout();
  symbolsDashesQuotes();
  everyLanguageSpokenTextsClean();
  longTextsAreCut();
  // the port's own: the words of the learning modes, the citations, this host's Intl
  theWordsOfTheLearningModes();
  theCitations();
  theNativeHostsIntl();
  std::printf("i18n_test: %zu languages of %zu texts, %zu with the words of the learning modes – all as the original has them\n", LANGS.size(), en().size(), EXAM_LANGS.size());
  return 0;
}

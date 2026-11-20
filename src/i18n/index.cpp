// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Texts in many languages: the registry, the lookup with its fallbacks and
 * the formatting of quantities and dates.
 *
 * Origin: src/i18n/index.ts (MaurerAnton/driving-simulator @ 408014b, v0.6.0).
 * German and English are built in; the other dictionaries are registered the
 * first time their language is asked for (the TypeScript fetches the file).
 */
#include "i18n/index.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>

#include "i18n/keys.h"
#include "i18n/violations.h"

namespace fs {

const std::vector<DictEntry>& dictOfDE();
const std::vector<DictEntry>& dictOfEN();
const std::vector<DictEntry>& dictOfID();
const std::vector<DictEntry>& dictOfCS();
const std::vector<DictEntry>& dictOfES();
const std::vector<DictEntry>& dictOfFR();
const std::vector<DictEntry>& dictOfHR();
const std::vector<DictEntry>& dictOfIT();
const std::vector<DictEntry>& dictOfHU();
const std::vector<DictEntry>& dictOfNL();
const std::vector<DictEntry>& dictOfPL();
const std::vector<DictEntry>& dictOfPT();
const std::vector<DictEntry>& dictOfRO();
const std::vector<DictEntry>& dictOfSQ();
const std::vector<DictEntry>& dictOfVI();
const std::vector<DictEntry>& dictOfTR();
const std::vector<DictEntry>& dictOfEL();
const std::vector<DictEntry>& dictOfBG();
const std::vector<DictEntry>& dictOfRU();
const std::vector<DictEntry>& dictOfUK();
const std::vector<DictEntry>& dictOfAR();
const std::vector<DictEntry>& dictOfFA();
const std::vector<DictEntry>& dictOfUR();
const std::vector<DictEntry>& dictOfHI();
const std::vector<DictEntry>& dictOfBN();
const std::vector<DictEntry>& dictOfZH();
const std::vector<DictEntry>& dictOfJA();
const std::vector<DictEntry>& dictOfKO();

namespace {

/** registered languages (code → texts), filled by loadLang */
std::map<std::string, Dict> g_loaded;

/** which style the [[KeyX]] tokens are written in */
KeyStyle g_keyStyle = KeyStyle::Keys;

bool hasLang(const std::string& code) {
  for (const LangInfo& l : LANGS)
    if (l.code == code) return true;
  return false;
}

std::string lookup(const std::string& key, const std::string& lang) {
  // a language that is here (built in, or registered by loadLang) answers
  auto it = g_loaded.find(lang);
  if (it != g_loaded.end())
    for (const auto& e : it->second)
      if (e.first == key) return e.second;
  if (lang == "de")
    for (const auto& e : dictOfDE())
      if (e.first == key) return e.second;
  if (lang == "en")
    for (const auto& e : dictOfEN())
      if (e.first == key) return e.second;
  // (a text missing in a language falls back to English, then to the key)
  if (lang != "en")
    for (const auto& e : dictOfEN())
      if (e.first == key) return e.second;
  return key;
}

/** Inside right-to-left text a citation such as "§ 37 Abs. 2 StVO" would be torn apart. */
std::string ltrPiece(const std::string& s) {
  return "\xe2\x81\xa7" + s + "\xe2\x81\xa9";  // ‪ …   (LRI … PDI)
}

bool citation(const std::string& s, size_t pos, size_t& len) {
  // §\s*\d[\w\s.,()§]*?(?:StVO|UrhG)
  if (pos >= s.size() || s[pos] != '\xc2' || s[pos + 1] != '\xa7') return false;  // §
  size_t i = pos + 2;
  // \s*\d
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
  if (i >= s.size() || s[i] < '0' || s[i] > '9') {
    return false;
  }
  i++;
  // [\w\s.,()§]*? then StVO or UrhG
  size_t j = i;
  while (j < s.size()) {
    unsigned char c = static_cast<unsigned char>(s[j]);
    bool word = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                c == '_' || c == ' ' || c == '\t' || c == '.' || c == ',' || c == '(' ||
                c == ')' || (c == 0xc2 && j + 1 < s.size() && s[j + 1] == 0xa7);
    if (!word) break;
    if (s.compare(j, 4, "StVO") == 0 || s.compare(j, 4, "UrhG") == 0) {
      len = j + 4 - pos;
      return true;
    }
    j++;
  }
  return false;
}

}  // namespace

const std::vector<LangInfo> LANGS = {
    {"de", "Deutsch", "de-DE"},
    {"en", "English", "en-GB"},
    {"id", "Bahasa Indonesia", "id-ID"},
    {"cs", "Čeština", "cs-CZ"},
    {"es", "Español", "es-ES"},
    {"fr", "Français", "fr-FR"},
    {"hr", "Hrvatski", "hr-HR"},
    {"it", "Italiano", "it-IT"},
    {"hu", "Magyar", "hu-HU"},
    {"nl", "Nederlands", "nl-NL"},
    {"pl", "Polski", "pl-PL"},
    {"pt", "Português", "pt-PT"},
    {"ro", "Română", "ro-RO"},
    {"sq", "Shqip", "sq-AL"},
    {"vi", "Tiếng Việt", "vi-VN"},
    {"tr", "Türkçe", "tr-TR"},
    {"el", "Ελληνικά", "el-GR"},
    {"bg", "Български", "bg-BG"},
    {"ru", "Русский", "ru-RU"},
    {"uk", "Українська", "uk-UA"},
    {"ar", "العربية", "ar", true},
    {"fa", "فارسی", "fa-IR", true},
    {"ur", "اردو", "ur-PK", true},
    {"hi", "हिन्दी", "hi-IN"},
    {"bn", "বাংলা", "bn-BD"},
    {"zh", "简体中文", "zh-CN"},
    {"ja", "日本語", "ja-JP"},
    {"ko", "한국어", "ko-KR"},
};

const Dict* dictOf(const std::string& code) {
  if (code == "en") {
    static const Dict en = dictOfEN();
    return &en;
  }
  if (code == "de") {
    static const Dict de = dictOfDE();
    return &de;
  }
  auto it = g_loaded.find(code);
  return it == g_loaded.end() ? nullptr : &it->second;
}

const Dict& rawDictOf(const std::string& code) {
    if (code == "de") return dictOfDE();
  if (code == "en") return dictOfEN();
  if (code == "id") return dictOfID();
  if (code == "cs") return dictOfCS();
  if (code == "es") return dictOfES();
  if (code == "fr") return dictOfFR();
  if (code == "hr") return dictOfHR();
  if (code == "it") return dictOfIT();
  if (code == "hu") return dictOfHU();
  if (code == "nl") return dictOfNL();
  if (code == "pl") return dictOfPL();
  if (code == "pt") return dictOfPT();
  if (code == "ro") return dictOfRO();
  if (code == "sq") return dictOfSQ();
  if (code == "vi") return dictOfVI();
  if (code == "tr") return dictOfTR();
  if (code == "el") return dictOfEL();
  if (code == "bg") return dictOfBG();
  if (code == "ru") return dictOfRU();
  if (code == "uk") return dictOfUK();
  if (code == "ar") return dictOfAR();
  if (code == "fa") return dictOfFA();
  if (code == "ur") return dictOfUR();
  if (code == "hi") return dictOfHI();
  if (code == "bn") return dictOfBN();
  if (code == "zh") return dictOfZH();
  if (code == "ja") return dictOfJA();
  if (code == "ko") return dictOfKO();
  static const Dict empty;
  return empty;
}

LangInfo langInfo(const std::string& code) {
  for (const LangInfo& l : LANGS)
    if (l.code == code) return l;
  return LANGS[1];  // English otherwise (as the original's fallback)
}

bool isRtl(const std::string& code) { return langInfo(code).rtl; }

std::string localeOf(const std::string& code) { return langInfo(code).locale; }

bool loadLang(const std::string& code) {
  if (!hasLang(code)) return false;
  if (code == "en" || code == "de") return true;   // built in: always here
  if (g_loaded.count(code)) return true;
  g_loaded[code] = rawDictOf(code);
  return true;
}

std::string pickLang(const std::vector<std::string>& preferred) {
  // Languages that are not here, but whose readers understand one that is.
  static const std::map<std::string, std::string> RELATED = {
      {"bs", "hr"}, {"sr", "hr"}, {"sh", "hr"}, {"sk", "cs"}, {"ms", "id"},
      {"be", "ru"}, {"ca", "es"}, {"gl", "es"}, {"mo", "ro"}, {"lb", "de"},
      {"gsw", "de"}, {"nds", "de"},
  };
  auto has = [](const std::string& code) { return !code.empty() && hasLang(code); };
  for (const std::string& p : preferred) {
    std::string primary = p;
    std::transform(primary.begin(), primary.end(), primary.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    size_t dash = primary.find('-');
    if (dash != std::string::npos) primary = primary.substr(0, dash);
    if (has(primary)) return primary;
    auto it = RELATED.find(primary);
    if (it != RELATED.end() && has(it->second)) return it->second;
  }
  return "en";
}

void setKeyStyle(KeyStyle style) { g_keyStyle = style; }

KeyStyle keyStyle() { return g_keyStyle; }

std::string tr(const std::string& key, const std::string& lang) {
  return tr(key, lang, {});
}

std::string keyName(const std::string& code, const std::string& lang) {
  return keyLabel(code, lang, [&](const std::string& k) { return lookup(k, lang); });
}

std::string tr(const std::string& key, const std::string& lang,
               const std::vector<std::pair<std::string, std::string>>& params) {
  std::string s = lookup(key, lang);
  if (s.find("[[") != std::string::npos) {
    // [[KeyX]] becomes the name of that key: as the key, or as the button on
    // the screen that stands in for it
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
      size_t open = s.find("[[", i);
      if (open == std::string::npos) {
        out += s.substr(i);
        break;
      }
      size_t close = s.find("]]", open + 2);
      if (close == std::string::npos) {
        out += s.substr(i);
        break;
      }
      std::string code = s.substr(open + 2, close - open - 2);
      out += s.substr(i, open - i);
      out += g_keyStyle == KeyStyle::Touch
                 ? touchLabel(code, lang, [&](const std::string& k) { return lookup(k, lang); })
                 : keyLabel(code, lang, [&](const std::string& k) { return lookup(k, lang); });
      i = close + 2;
    }
    s = out;
  }
  // (a value may contain "$&" – a file name, a street name – so replace directly)
  for (const auto& kv : params) {
    std::string needle = "{" + kv.first + "}";
    size_t pos = 0;
    while ((pos = s.find(needle, pos)) != std::string::npos) {
      s.replace(pos, needle.size(), kv.second);
      pos += kv.second.size();
    }
  }
  // (inside a sentence also kept on one line)
  if (isRtl(lang) && s.find("\xc2\xa7") != std::string::npos) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
      size_t len = 0;
      if (citation(s, i, len)) {
        std::string piece = s.substr(i, len);
        for (size_t p = 0; p < piece.size(); p++)
          if (piece[p] == ' ') {
            piece.replace(p, 1, "\xc2\xa0");
            p++;
          }  // nbsp
        out += ltrPiece(piece);
        i += len;
      } else {
        out.push_back(s[i]);
        i++;
      }
    }
    s = out;
  }
  return s;
}

std::string formatUnit(double value, const std::string& unit, const std::string& lang,
                       int digits) {
  // The form the language writes on the road and in the highway code – with
  // Western digits, whatever the digits of the language are.
  char num[64];
  if (digits > 0) std::snprintf(num, sizeof num, "%.*f", digits, value);
  else {
    double rounded = value < 0 ? value - 0.5 : value + 0.5;
    std::snprintf(num, sizeof num, "%lld", static_cast<long long>(rounded));
  }
  // the decimal comma where the language writes it
  std::string n = num;
  if (digits > 0 && lang != "en" && lang != "vi" && lang != "id" && lang != "ja" &&
      lang != "ko" && lang != "zh" && lang != "th")
    std::replace(n.begin(), n.end(), '.', ',');
  struct Unit { const char* code; const char* de; const char* en; const char* ru; };
  static const Unit UNITS[] = {
      {"kilometer", "km", "km", "\xd0\xba\xd0\xbc"},
      {"meter", "m", "m", "\xd0\xbc"},
      {"minute", "min", "min", "\xd0\xbc\xd0\xb8\xd0\xbd"},
  };
  for (const Unit& u : UNITS) {
    if (unit != u.code) continue;
    const char* label = u.en;
    if (lang == "de") label = u.de;
    else if (lang == "ru" || lang == "uk" || lang == "bg" || lang == "sr" || lang == "be")
      label = u.ru;
    else if (lang == "fr" || lang == "es" || lang == "it" || lang == "pt" || lang == "ro" ||
             lang == "nl" || lang == "sq" || lang == "tr" || lang == "el")
      label = unit == "kilometer" ? "km" : (unit == "meter" ? "m" : "min");
    // Arabic, Persian, Urdu, Hindi, Bengali write the Latin unit letters
    return n + " " + label;
  }
  return n + " " + unit;
}

std::string formatMonth(double timeMs, const std::string& lang) {
  // (the calendar of the photographs and of the country the simulator is
  // about, whatever the language's own is: Western digits, Gregorian)
  time_t secs = static_cast<time_t>(timeMs / 1000.0);
  struct tm tmv;
  gmtime_r(&secs, &tmv);
  static const char* const MONTHS[12] = {
      "January", "February", "March", "April", "May", "June",
      "July", "August", "September", "October", "November", "December"};
  static const char* const MONTHS_DE[12] = {
      "Januar", "Februar", "März", "April", "Mai", "Juni",
      "Juli", "August", "September", "Oktober", "November", "Dezember"};
  const char* const* months = (lang == "de") ? MONTHS_DE : MONTHS;
  // (only German and English are built in without the browser; the other
  // languages' month names are the English ones here – the browser's
  // toLocaleDateString writes them the way the language does)
  if (lang != "de" && lang != "en") {
    for (const LangInfo& l : LANGS)
      if (l.code == lang) return std::string(months[tmv.tm_mon]) + " " +
                              std::to_string(1900 + tmv.tm_year);
  }
  char out[64];
  std::snprintf(out, sizeof out, "%s %d", months[tmv.tm_mon], 1900 + tmv.tm_year);
  return out;
}

std::string violationTitle(const std::string& id, const std::string& lang,
                           const std::vector<std::pair<std::string, std::string>>& data) {
  return tr("v." + id + ".title", lang, data);
}

std::string violationWhy(const std::string& id, const std::string& lang) {
  return tr("v." + id + ".why", lang);
}

std::string violationRef(const std::string& id, const std::string& lang) {
  std::string r;
  for (const auto& e : VIOLATION_REFS)
    if (e.first == id) {
      r = e.second;
      break;
    }
  if (r.rfind("@", 0) == 0) return tr(r.substr(1), lang);
  return isRtl(lang) ? ltrPiece(r) : r;
}

std::string lessonTitle(const std::string& id, const std::string& lang) {
  return tr("lesson." + id + ".title", lang);
}

std::string lessonIntro(const std::string& id, const std::string& lang) {
  return tr("lesson." + id + ".intro", lang);
}

}  // namespace fs

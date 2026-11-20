// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The translation checker, rule for rule as tools/i18n-check.ts.
 *
 * Origin: tools/i18n-check.ts (MaurerAnton/driving-simulator @ 408014b,
 * v0.6.0).
 */
#include "i18n/check.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <map>
#include <regex>

namespace fs {

namespace {

// ---------------------------------------------------------------- utf-8 helpers

/** The code points of a UTF-8 string. */
std::vector<unsigned> codePoints(const std::string& s) {
  std::vector<unsigned> out;
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    unsigned cp = 0;
    size_t n = 1;
    if (c < 0x80) {
      cp = c;
    } else if ((c & 0xe0) == 0xc0 && i + 1 < s.size()) {
      cp = ((c & 0x1f) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3f);
      n = 2;
    } else if ((c & 0xf0) == 0xe0 && i + 2 < s.size()) {
      cp = ((c & 0x0f) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3f) << 6) |
           (static_cast<unsigned char>(s[i + 2]) & 0x3f);
      n = 3;
    } else if ((c & 0xf8) == 0xf0 && i + 3 < s.size()) {
      cp = ((c & 0x07) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3f) << 12) |
           ((static_cast<unsigned char>(s[i + 2]) & 0x3f) << 6) |
           (static_cast<unsigned char>(s[i + 3]) & 0x3f);
      n = 4;
    } else {
      cp = c;
    }
    out.push_back(cp);
    i += n;
  }
  return out;
}

bool hasRange(const std::string& s, unsigned lo, unsigned hi) {
  for (unsigned cp : codePoints(s))
    if (cp >= lo && cp <= hi) return true;
  return false;
}

/** Does the string end like a sentence (any of the scripts)? */
bool endsSentence(const std::string& s) {
  static const char* const ENDS[] = {".", "!", "?", "…", "。", "！", "？", "।", "؟", "۔"};
  // trailing closing quotes/brackets after the mark
  size_t end = s.size();
  while (end > 0) {
    unsigned char c = static_cast<unsigned char>(s[end - 1]);
    if (c == '"' || c == '\'' || c == ')' || c == ']' || c == 0xe2) {
      // " ” » “ ’ ) ） 」 』  – the last is a lead byte; take it as one mark
      end--;
      if (c == 0xe2 && end > 0) end--;  // the continuation bytes follow
      continue;
    }
    break;
  }
  if (end == 0) return false;
  std::string last = s.substr(0, end);
  for (const char* e : ENDS)
    if (last.size() >= std::strlen(e) &&
        last.compare(last.size() - std::strlen(e), std::strlen(e), e) == 0)
      return true;
  // )）」』 after a mark: check one more mark before the closers
  return false;
}

/** "…" plus the last 24 characters, as the original shows it. */
std::string tail(const std::string& s, size_t n) {
  std::vector<unsigned> cps = codePoints(s);
  std::string out = "…";
  for (size_t i = cps.size() > n ? cps.size() - n : 0; i < cps.size(); i++) {
    unsigned cp = cps[i];
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
      out += static_cast<char>(0xc0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xe0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
      out += static_cast<char>(0x80 | (cp & 0x3f));
    } else {
      out += static_cast<char>(0xf0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
      out += static_cast<char>(0x80 | (cp & 0x3f));
    }
  }
  return out;
}

std::string head(const std::string& s, size_t n) {
  std::vector<unsigned> cps = codePoints(s);
  if (cps.size() <= n) return s;
  std::string out;
  for (size_t i = 0; i < n; i++) {
    unsigned cp = cps[i];
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
      out += static_cast<char>(0xc0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xe0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
      out += static_cast<char>(0x80 | (cp & 0x3f));
    } else {
      out += static_cast<char>(0xf0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
      out += static_cast<char>(0x80 | (cp & 0x3f));
    }
  }
  return out;
}

size_t utf8Len(const std::string& s) { return codePoints(s).size(); }

/** Does the text start with a capital letter (any script's capitals)? */
bool startsCapital(const std::string& s) {
  if (s.empty()) return false;
  std::vector<unsigned> cps = codePoints(s);
  if (cps.empty()) return false;
  unsigned c = cps[0];
  // A–Z, À–Þ … and the Cyrillic/Greek capitals
  if (c >= 'A' && c <= 'Z') return true;
  if (c >= 0xc0 && c <= 0xde && c != 0xd7) return true;
  if (c >= 0x410 && c <= 0x42f) return true;
  if (c >= 0x391 && c <= 0x3a9 && c != 0x3a2) return true;
  return false;
}

/** Does the source have a word of four letters or more (for "untranslated?"). */
bool hasLongWord(const std::string& s) {
  size_t run = 0;
  for (char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
      run++;
    else {
      if (run >= 4) return true;
      run = 0;
    }
  }
  return run >= 4;
}

/** Latin letters in the source (the script check applies to those texts). */
bool hasLatinWord(const std::string& s) {
  size_t run = 0;
  for (char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
      run++;
    else {
      if (run >= 3) return true;
      run = 0;
    }
  }
  return run >= 3;
}

/** "2." – an ordinal, not a sentence. */
bool isOrdinal(const std::string& s) {
  size_t n = s.size();
  if (n < 2 || s[n - 1] != '.') return false;
  return n >= 2 && std::isdigit(static_cast<unsigned char>(s[n - 2]));
}

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
  return s.substr(a, b - a);
}

/** The text with {…} and [[…]] tokens taken out. */
std::string stripTokens(const std::string& s) {
  std::string out;
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '{') {
      size_t close = s.find('}', i);
      if (close != std::string::npos) {
        i = close + 1;
        continue;
      }
    }
    if (s[i] == '[' && i + 1 < s.size() && s[i + 1] == '[') {
      size_t close = s.find("]]", i);
      if (close != std::string::npos) {
        i = close + 2;
        continue;
      }
    }
    out.push_back(s[i]);
    i++;
  }
  return out;
}

/** Numbers of two and more digits ("206", "30.5"). */
std::vector<std::string> numbers(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    if (std::isdigit(static_cast<unsigned char>(s[i]))) {
      size_t start = i;
      while (i < s.size() &&
             (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.'))
        i++;
      std::string n = s.substr(start, i - start);
      // at least two digits (with optional dots between)
      size_t digits = 0;
      for (char c : n)
        if (std::isdigit(static_cast<unsigned char>(c))) digits++;
      if (digits >= 2) out.push_back(n);
    } else {
      i++;
    }
  }
  return out;
}

std::vector<std::string> splitSpaces(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && s[i] == ' ') i++;
    size_t start = i;
    while (i < s.size() && s[i] != ' ') i++;
    if (i > start) out.push_back(s.substr(start, i - start));
  }
  return out;
}

/** Names that are the same in every language. */
const char* const KEEP[] = {
    "StVO", "§", "Baunatal", "Fahrsim", "Panoramax", "KartaView", "Mapillary",
    "OpenStreetMap", "ODbL", "AGPL-3.0-only", "three.js", "MIT", "Wikimedia Commons",
    "CC BY-SA 4.0", "EXIF", "XMP", "GeoJSON", "JSON", "CSV", "JPEG", "PNG", "WebP",
    "WebGL 2", "▲", "▼", "⋯", "DGM1", "LoD2", "UrhG", "Marktplatz 5", "A 49", "Logitech",
    "Thrustmaster", "Fanatec", "RT", "LT", "RB/LB", "X/B", "Anlieger frei", "P·R·N·D",
    "360°", "IP",
};

// ---------------------------------------------------------------- rules

/** Longest text (in characters) that still fits where these keys are shown. */
struct MaxRule {
  std::function<bool(const std::string&)> match;
  size_t max;
};

const std::vector<MaxRule>& maxRules() {
  static const std::vector<MaxRule> rules = {
      {[](const std::string& k) { return k == "tbGas" || k == "tbBrake"; }, 9},
      {[](const std::string& k) { return k == "limitNone" || k == "walk"; }, 14},
      {[](const std::string& k) { return k.rfind("key.", 0) == 0; }, 12},
      {[](const std::string& k) {
         return k == "qLow" || k == "qMedium" || k == "qHigh" || k == "mOff";
       }, 16},
      {[](const std::string& k) { return k.rfind("kr.", 0) == 0; }, 26},
      {[](const std::string& k) { return k.rfind("tb", 0) == 0; }, 34},
      {[](const std::string& k) {
         static const char* const EXACT[] = {
             "start", "free", "exam", "settings", "controls", "about", "resume", "menu",
             "paused", "back", "result", "passed", "failed", "faults", "points", "distance",
             "duration", "hint", "minor", "major", "fail", "reality", "realityClose"};
         for (const char* e : EXACT)
           if (k == e) return true;
         return false;
       }, 34},
      {[](const std::string& k) {
         return k == "navLeft" || k == "navRight" || k == "navUturn" || k == "navFollow" ||
                k == "navMotorway" || k == "laneLeft" || k == "laneRight" ||
                k == "routeDone";
       }, 60},
      {[](const std::string& k) {
         return k.rfind("lesson.", 0) == 0 && k.size() > 6 &&
                k.compare(k.size() - 6, 6, ".title") == 0;
       }, 60},
      {[](const std::string& k) {
         return k.rfind("v.", 0) == 0 && k.size() > 6 &&
                k.compare(k.size() - 6, 6, ".title") == 0;
       }, 80},
  };
  return rules;
}

/** Scripts: most texts of these languages must contain letters of the script. */
const std::map<std::string, std::function<bool(const std::string&)>>& scripts() {
  static const std::map<std::string, std::function<bool(const std::string&)>> m = {
      {"ru", [](const std::string& s) { return hasRange(s, 0x0400, 0x04ff); }},
      {"uk", [](const std::string& s) { return hasRange(s, 0x0400, 0x04ff); }},
      {"bg", [](const std::string& s) { return hasRange(s, 0x0400, 0x04ff); }},
      {"el", [](const std::string& s) { return hasRange(s, 0x0370, 0x03ff); }},
      {"ar", [](const std::string& s) { return hasRange(s, 0x0600, 0x06ff); }},
      {"fa", [](const std::string& s) { return hasRange(s, 0x0600, 0x06ff); }},
      {"ur", [](const std::string& s) { return hasRange(s, 0x0600, 0x06ff); }},
      {"bn", [](const std::string& s) { return hasRange(s, 0x0980, 0x09ff); }},
      {"hi", [](const std::string& s) { return hasRange(s, 0x0900, 0x097f); }},
      {"zh", [](const std::string& s) { return hasRange(s, 0x4e00, 0x9fff); }},
      {"ja", [](const std::string& s) {
         return hasRange(s, 0x3040, 0x30ff) || hasRange(s, 0x4e00, 0x9fff);
       }},
      {"ko", [](const std::string& s) { return hasRange(s, 0xac00, 0xd7af); }},
  };
  return m;
}

bool hasKey(const Dict& d, const std::string& k) {
  for (const auto& e : d)
    if (e.first == k) return true;
  return false;
}

std::string join(const std::vector<std::string>& v, const std::string& sep) {
  std::string out;
  for (size_t i = 0; i < v.size(); i++) {
    if (i) out += sep;
    out += v[i];
  }
  return out;
}

/** The {…} tokens of a text, sorted (as the original's tokens()). */
std::vector<std::string> placeholderTokens(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while ((i = s.find('{', i)) != std::string::npos) {
    size_t close = s.find('}', i);
    if (close == std::string::npos) break;
    out.push_back(s.substr(i, close - i + 1));
    i = close + 1;
  }
  std::sort(out.begin(), out.end());
  return out;
}

/** The [[…]] key tokens of a text, sorted. */
std::vector<std::string> keyTokens(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while ((i = s.find("[[", i)) != std::string::npos) {
    size_t close = s.find("]]", i);
    if (close == std::string::npos) break;
    out.push_back(s.substr(i, close - i + 2));
    i = close + 2;
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool sameTokens(const std::vector<std::string>& a, const std::vector<std::string>& b) {
  return a == b;
}

/** The text with its tokens taken out (for stray brackets and numbers). */
std::string withoutTokens(const std::string& s) {
  std::string out;
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '{') {
      size_t close = s.find('}', i);
      if (close != std::string::npos) {
        i = close + 1;
        continue;
      }
    }
    if (s[i] == '[' && i + 1 < s.size() && s[i + 1] == '[') {
      size_t close = s.find("]]", i);
      if (close != std::string::npos) {
        i = close + 2;
        continue;
      }
    }
    out.push_back(s[i]);
    i++;
  }
  return out;
}

/** Numbers of two and more digits in a text. */
std::vector<std::string> bigNumbers(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    if (std::isdigit(static_cast<unsigned char>(s[i]))) {
      size_t start = i;
      while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.'))
        i++;
      std::string n = s.substr(start, i - start);
      size_t digits = 0;
      for (char c : n)
        if (std::isdigit(static_cast<unsigned char>(c))) digits++;
      if (digits >= 2) out.push_back(n);
    } else {
      i++;
    }
  }
  return out;
}

/** Digits other than 0–9 (Eastern Arabic, Devanagari, Bengali, fullwidth). */
bool foreignDigits(const std::string& s) {
  for (unsigned cp : codePoints(s)) {
    if (cp >= 0x0660 && cp <= 0x0669) return true;   // ٠-٩
    if (cp >= 0x06f0 && cp <= 0x06f9) return true;   // ۰-۹
    if (cp >= 0x0966 && cp <= 0x096f) return true;   // ०-९
    if (cp >= 0x09e6 && cp <= 0x09ef) return true;   // ০-৯
    if (cp >= 0xff10 && cp <= 0xff19) return true;   // ０-９
  }
  return false;
}

/** Characters that change the writing direction (invisible marks). */
bool bidiMarks(const std::string& s) {
  for (unsigned cp : codePoints(s)) {
    if (cp == 0x200e || cp == 0x200f) return true;         // LRM / RLM
    if (cp >= 0x202a && cp <= 0x202e) return true;         // LRE … RLO
    if (cp >= 0x2066 && cp <= 0x2069) return true;         // LRI … PDI
  }
  return false;
}

}  // namespace

Report checkDict(const std::string& code, const Dict& dict) {
  return checkDict(code, dict, rawDictOf("en"));
}

Report checkDict(const std::string& code, const Dict& dict, const Dict& ref) {
  Report r;
  if (dict.empty()) {
    r.errors.push_back("the file does not export an object of texts");
    return r;
  }
  for (const auto& e : ref)
    if (!hasKey(dict, e.first)) r.errors.push_back(e.first + ": missing");
  for (const auto& e : dict)
    if (!hasKey(ref, e.first)) r.errors.push_back(e.first + ": not a key of en.ts");

  size_t scriptOk = 0, scriptAll = 0, identical = 0;
  auto scriptIt = scripts().find(code);
  const bool wide = code == "zh" || code == "ja" || code == "ko";

  for (const auto& src : ref) {
    const std::string& k = src.first;
    const std::string& source = src.second;
    const std::string* vp = nullptr;
    for (const auto& e : dict)
      if (e.first == k) {
        vp = &e.second;
        break;
      }
    if (!vp) continue;
    const std::string& v = *vp;

    if (v.empty()) r.errors.push_back(k + ": empty");
    if (v != trim(v)) r.errors.push_back(k + ": starts or ends with white space");
    if (v.find('<') != std::string::npos || v.find('>') != std::string::npos)
      r.errors.push_back(k + ": contains < or > (no HTML in texts)");
    if (v.find('\n') != std::string::npos) r.errors.push_back(k + ": contains a line break");
    if (!sameTokens(placeholderTokens(v), placeholderTokens(source))) {
      std::string want = join(placeholderTokens(source), " ");
      r.errors.push_back(k + ": placeholders differ from English (" +
                         (want.empty() ? std::string("none") : want) + ")");
    }
    if (!sameTokens(keyTokens(v), keyTokens(source))) {
      std::string want = join(keyTokens(source), " ");
      r.errors.push_back(k + ": key tokens differ from English (" +
                         (want.empty() ? std::string("none") : want) + ")");
    }
    {
      std::string rest = withoutTokens(v);
      if (rest.find('{') != std::string::npos || rest.find('}') != std::string::npos ||
          rest.find("[[") != std::string::npos || rest.find("]]") != std::string::npos)
        r.errors.push_back(k + ": stray brackets or braces");
    }
    for (const char* name : KEEP)
      if (std::string(source).find(name) != std::string::npos &&
          v.find(name) == std::string::npos)
        r.errors.push_back(k + ": \"" + std::string(name) + "\" must stay as it is");
    for (const std::string& n : bigNumbers(withoutTokens(source)))
      if (v.find(n) == std::string::npos)
        r.errors.push_back(k + ": the number " + n + " is missing");
    for (const MaxRule& rule : maxRules()) {
      if (rule.match(k)) {
        if (utf8Len(v) > rule.max)
          r.errors.push_back(k + ": " + std::to_string(utf8Len(v)) +
                             " characters, at most " + std::to_string(rule.max) +
                             " fit (\"" + v + "\")");
        break;
      }
    }
    // the roundel of the speed limit: the text may wrap at spaces, but a word
    // must fit its width (wide scripts have room for fewer letters)
    if (k == "limitNone" || k == "walk") {
      size_t longest = 0;
      for (const std::string& w : splitSpaces(v)) longest = std::max(longest, utf8Len(w));
      const size_t fits = wide ? 4 : 9;
      if (longest > fits)
        r.errors.push_back(k + ": the word of " + std::to_string(longest) +
                           " characters does not fit the roundel (at most " +
                           std::to_string(fits) + ") (\"" + v + "\")");
    }
    if (foreignDigits(v)) r.errors.push_back(k + ": digits must be the Western ones (0–9)");
    if (bidiMarks(v)) r.errors.push_back(k + ": contains an invisible direction mark");
    if (v.find("  ") != std::string::npos) r.errors.push_back(k + ": two spaces in a row");
    // a sentence stays a sentence, a label stays a label
    if (code != "en") {
      const bool sentence = endsSentence(source);
      if (sentence && !endsSentence(v))
        r.warnings.push_back(k + ": the English text ends a sentence, this one does not (…" +
                             tail(v, 24) + ")");
      if (!sentence && endsSentence(v) && !endsSentence(source) && !isOrdinal(v) &&
          k != "tbGas" && k != "tbBrake")
        r.warnings.push_back(
            k + ": the English text is a label without a full stop, this one ends with one (…" +
            tail(v, 24) + ")");
      // parts of a sentence ("turn left") are put behind other words: no capital
      if (!source.empty() && std::islower(static_cast<unsigned char>(source[0])) &&
          startsCapital(v) && code != "de")
        r.warnings.push_back(k + ": starts with a capital letter, but it is put into the "
                                 "middle of a sentence (\"" +
                             head(v, 24) + "…\")");
    }
    if (code != "en" && v == source && hasLongWord(source) && source != "StVO" &&
        source != "§")
      identical++;
    if (scriptIt != scripts().end() && hasLatinWord(source)) {
      scriptAll++;
      if (scriptIt->second(v)) scriptOk++;
      else if (utf8Len(v) > 14)
        r.warnings.push_back(k + ": no letters of the language's script (\"" +
                             head(v, 50) + "\")");
    }
  }
  // the list of controls names the gearbox setting as the settings do
  {
    const std::string* trManual = nullptr;
    const std::string* clutch = nullptr;
    for (const auto& e : dict) {
      if (e.first == "trManual") trManual = &e.second;
      if (e.first == "ctl.clutch") clutch = &e.second;
    }
    if (trManual && clutch && clutch->find(*trManual) == std::string::npos)
      r.errors.push_back("ctl.clutch: must quote the text of trManual (\"" + *trManual + "\")");
  }
  if (identical > 12)
    r.warnings.push_back(std::to_string(identical) +
                         " texts are identical to the English ones – untranslated?");
  if (scriptAll > 0 && scriptOk * 100 < scriptAll * 93)
    r.errors.push_back("only " + std::to_string(scriptOk) + " of " +
                       std::to_string(scriptAll) +
                       " texts are written in the script of the language");
  return r;
}

}  // namespace fs

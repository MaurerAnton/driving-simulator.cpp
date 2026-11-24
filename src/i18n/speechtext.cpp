// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/i18n/speechtext.ts @ v1.7.7
#include "i18n/speechtext.h"

#include <cstdlib>
#include <functional>
#include <optional>

#include "core/js.h"
#include "i18n/index.h"
#include "i18n/unicode.h"
#include "platform/intl.h"

namespace fs {

namespace {

using i18n::isDigit;
using i18n::isSpace;
using i18n::isWord;
using i18n::startsAt;
using Text = std::u32string;

/**
 * `s.replace(re, …)` with the flag g, for an expression that matches at least one
 * character: at(s, i) says how long the match is that begins at i (0: none there), put()
 * writes what stands in its place.  Like the original's engine it tries every place from
 * the left and goes on behind a match.
 */
template <class At, class Put>
Text replaceEach(const Text& s, At at, Put put) {
  Text out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    const size_t len = at(s, i);
    if (len == 0) {
      out += s[i++];
      continue;
    }
    put(out, i, len);
    i += len;
  }
  return out;
}

Text text(std::string_view utf8) { return i18n::decodeUtf8(utf8); }

/** \b before a word character at i: nothing of a word stands before it */
bool wordStart(const Text& s, size_t i) { return i == 0 || !isWord(s[i - 1]); }
/** \b behind a word character that ends at i */
bool wordEnd(const Text& s, size_t i) { return i >= s.size() || !isWord(s[i]); }

// ------------------------------------------------------------------ units

struct UnitToken {
  /** as the texts write it (code points, ending in 0) */
  char32_t token[8];
  /** as Intl names the unit */
  const char* unit;
};

/** The ways the texts write the units, in every script they use. */
const UnitToken UNIT_TOKENS[] = {
    {{U'k', U'm', U'/', U'h'}, "kilometer-per-hour"},
    {{0x43A, 0x43C, U'/', 0x447}, "kilometer-per-hour"},                  // км/ч
    {{0x43A, 0x43C, U'/', 0x433, 0x43E, 0x434}, "kilometer-per-hour"},    // км/год
    {{0x643, 0x645, U'/', 0x633}, "kilometer-per-hour"},                  // كم/س
    {{U'k', U'm'}, "kilometer"},
    {{0x43A, 0x43C}, "kilometer"},                                        // км
    {{0x643, 0x645}, "kilometer"},                                        // كم
    {{U'm'}, "meter"},
    {{0x43C}, "meter"},                                                   // м
    {{0x645}, "meter"},                                                   // م
};

struct UnitMatch {
  size_t length = 0;
  /** the figures: the first capture */
  size_t number = 0;
  const UnitToken* token = nullptr;
};

/**
 * UNIT_RE at position i:
 *   (\d+(?:[.,]\d{1,2})?)(?!\d)\s?(km/h|км/ч|…|m|м|م)(?![\p{Script=Latin}\p{Script=Cyrillic}\p{Script=Arabic}\p{N}])
 * A number (decimal comma or point, one or two places) and a unit that stands
 * alone – not the start of a word in the same script ("1 мин", "5 min" are
 * not metres).  Japanese "200 m先" has a CJK sign after the unit: that is fine.
 * (What the expression may take in more than one way is tried in its order: all the
 * digits before fewer, two decimals before one before none, the space before none.)
 */
bool unitAt(const Text& s, size_t i, UnitMatch& m) {
  const size_t n = s.size();
  size_t digits = 0;
  while (i + digits < n && isDigit(s[i + digits])) digits++;
  for (size_t whole = digits; whole >= 1; whole--) {
    const size_t point = i + whole;
    size_t ends[3];
    size_t count = 0;
    if (point < n && (s[point] == U'.' || s[point] == U',')) {
      if (point + 2 < n && isDigit(s[point + 1]) && isDigit(s[point + 2])) ends[count++] = point + 3;
      if (point + 1 < n && isDigit(s[point + 1])) ends[count++] = point + 2;
    }
    ends[count++] = point;
    for (size_t e = 0; e < count; e++) {
      const size_t end = ends[e];
      if (end < n && isDigit(s[end])) continue;
      for (int space = end < n && isSpace(s[end]) ? 1 : 0; space >= 0; space--) {
        const size_t at = end + static_cast<size_t>(space);
        for (const UnitToken& t : UNIT_TOKENS) {
          const std::u32string_view token = t.token;
          if (!startsAt(s, at, token)) continue;
          const size_t behind = at + token.size();
          if (behind < n && (i18n::isLatin(s[behind]) || i18n::isCyrillic(s[behind]) || i18n::isArabic(s[behind]) || i18n::isNumber(s[behind]))) continue;
          m.length = behind - i;
          m.number = end - i;
          m.token = &t;
          return true;
        }
      }
    }
  }
  return false;
}

/** "1,5" → 1.5; "200" → 200 */
double parseNum(const Text& s) {
  std::string ascii;
  for (char32_t c : s) ascii += c == U',' ? '.' : static_cast<char>(c);
  return std::strtod(ascii.c_str(), nullptr);
}

std::optional<std::string> unitWords(double value, const char* unit, std::string_view lang) {
  std::string out;
  try {
    out = plat::formatNumber(value, localeOf(lang), {.style = "unit", .unit = unit, .unitDisplay = "long", .maximumFractionDigits = 2, .numberingSystem = "latn"});
  } catch (const plat::IntlError&) {
    return std::nullopt;
  }
  // (CLDR writes a few speeds with a slash: "30 kilometre/saat", "30 kilômét/giờ")
  if (out.find('/') != std::string::npos) {
    if (lang == "tr") {
      // out.replace(/^(.*?)\s*kilometre\/saat$/, 'saatte $1 kilometre')
      static const std::string_view SPEED = "kilometre/saat";
      if (endsWith(out, std::string(SPEED))) {
        Text head = text(std::string_view(out).substr(0, out.size() - SPEED.size()));
        while (!head.empty() && isSpace(head.back())) head.pop_back();
        bool line = true;
        for (char32_t c : head) line = line && c != U'\n' && c != U'\r' && c != 0x2028 && c != 0x2029;
        if (line) out = "saatte " + i18n::encodeUtf8(head) + " kilometre";
      }
    } else if (lang == "vi") {
      out.replace(out.find('/'), 1, " m\xe1\xbb\x99t ");   // " một "
    }
    out = replaceAll(out, "/", " ");
  }
  return out;
}

// ------------------------------------------------------------------ ordinals

/**
 * The exit of a roundabout is written with a figure ("die {n}. Ausfahrt");
 * read out, the figure needs the ordinal word in the gender and case of the
 * noun after it.  Each rule only fires right before that noun.
 *
 * The expressions are all of one shape, (?<!\d)(\d{1,2})<sign>(?=\s+<noun>): a figure of one
 * or two digits, the sign that makes it an ordinal (with `stop`: a full stop may stand
 * before the sign – "3.ª", "3ª"), and behind it, after white space, the noun (in either
 * of two spellings).  Only Indonesian writes the figure behind a word: "ke-2".
 */
struct OrdinalRule {
  const char* lang;
  bool stop;
  const char* sign;
  const char* nouns[2];
  const char* words[10];
};

const OrdinalRule ORDINALS[] = {
    // de: (?<!\d)(\d{1,2})\.(?=\s+Ausfahrt)
    {"de", false, ".", {"Ausfahrt", ""}, {"erste", "zweite", "dritte", "vierte", "fünfte", "sechste", "siebte", "achte", "neunte", "zehnte"}},
    // cs: (?<!\d)(\d{1,2})\.(?=\s+výjezdem)
    {"cs", false, ".", {"výjezdem", ""}, {"prvním", "druhým", "třetím", "čtvrtým", "pátým", "šestým", "sedmým", "osmým", "devátým", "desátým"}},
    // pl: (?<!\d)(\d{1,2})\.(?=\s+zjazdem)
    {"pl", false, ".", {"zjazdem", ""}, {"pierwszym", "drugim", "trzecim", "czwartym", "piątym", "szóstym", "siódmym", "ósmym", "dziewiątym", "dziesiątym"}},
    // hr: (?<!\d)(\d{1,2})\.(?=\s+izlazu)
    {"hr", false, ".", {"izlazu", ""}, {"prvom", "drugom", "trećem", "četvrtom", "petom", "šestom", "sedmom", "osmom", "devetom", "desetom"}},
    // tr: (?<!\d)(\d{1,2})\.(?=\s+çıkış)
    {"tr", false, ".", {"çıkış", ""}, {"birinci", "ikinci", "üçüncü", "dördüncü", "beşinci", "altıncı", "yedinci", "sekizinci", "dokuzuncu", "onuncu"}},
    // hu: (?<!\d)(\d{1,2})\.(?=\s+kijárat)
    {"hu", false, ".", {"kijárat", ""}, {"első", "második", "harmadik", "negyedik", "ötödik", "hatodik", "hetedik", "nyolcadik", "kilencedik", "tizedik"}},
    // nl: (?<!\d)(\d{1,2})e(?=\s+afslag)
    {"nl", false, "e", {"afslag", ""}, {"eerste", "tweede", "derde", "vierde", "vijfde", "zesde", "zevende", "achtste", "negende", "tiende"}},
    // es: (?<!\d)(\d{1,2})\.?ª(?=\s+salida)
    {"es", true, "ª", {"salida", ""}, {"primera", "segunda", "tercera", "cuarta", "quinta", "sexta", "séptima", "octava", "novena", "décima"}},
    // pt: (?<!\d)(\d{1,2})\.?ª(?=\s+saída)
    {"pt", true, "ª", {"saída", ""}, {"primeira", "segunda", "terceira", "quarta", "quinta", "sexta", "sétima", "oitava", "nona", "décima"}},
    // el: (?<!\d)(\d{1,2})η(?=\s+έξοδο)
    {"el", false, "η", {"έξοδο", ""}, {"πρώτη", "δεύτερη", "τρίτη", "τέταρτη", "πέμπτη", "έκτη", "έβδομη", "όγδοη", "ένατη", "δέκατη"}},
    // ru: (?<!\d)(\d{1,2})-й(?=\s+съезд)
    {"ru", false, "-й", {"съезд", ""}, {"первый", "второй", "третий", "четвёртый", "пятый", "шестой", "седьмой", "восьмой", "девятый", "десятый"}},
    // uk: (?<!\d)(\d{1,2})-й(?=\s+з['’]їзд)
    {"uk", false, "-й", {"з'їзд", "з’їзд"}, {"перший", "другий", "третій", "четвертий", "п’ятий", "шостий", "сьомий", "восьмий", "дев’ятий", "десятий"}},
    // id: \bke-(\d{1,2})(?!\d) – in OrdinalRule's terms the figure stands behind "ke-", see ordinalAt()
    {"id", false, "", {"", ""}, {"pertama", "kedua", "ketiga", "keempat", "kelima", "keenam", "ketujuh", "kedelapan", "kesembilan", "kesepuluh"}},
};

const OrdinalRule* ordinalsOf(std::string_view lang) {
  for (const OrdinalRule& r : ORDINALS)
    if (lang == r.lang) return &r;
  return nullptr;
}

/** The ordinal at position i: how long it is (0: none) and its figure. */
size_t ordinalAt(const OrdinalRule& r, const Text& s, size_t i, int& figure) {
  const size_t n = s.size();
  if (std::string_view(r.lang) == "id") {
    // \bke-(\d{1,2})(?!\d)
    if (!startsAt(s, i, U"ke-") || !wordStart(s, i)) return 0;
    size_t d = 0;
    while (i + 3 + d < n && isDigit(s[i + 3 + d])) d++;
    if (d < 1 || d > 2) return 0;
    figure = std::atoi(i18n::encodeUtf8(std::u32string_view(s).substr(i + 3, d)).c_str());
    return 3 + d;
  }
  // (?<!\d)(\d{1,2})
  if (!isDigit(s[i]) || (i > 0 && isDigit(s[i - 1]))) return 0;
  const size_t d = i + 1 < n && isDigit(s[i + 1]) ? 2 : 1;
  size_t j = i + d;
  if (r.stop && j < n && s[j] == U'.') j++;
  const Text sign = text(r.sign);
  if (!startsAt(s, j, sign)) return 0;
  j += sign.size();
  // (?=\s+noun)
  size_t k = j;
  while (k < n && isSpace(s[k])) k++;
  if (k == j) return 0;
  bool noun = false;
  for (const char* spelling : r.nouns) noun = noun || (*spelling && startsAt(s, k, text(spelling)));
  if (!noun) return 0;
  figure = std::atoi(i18n::encodeUtf8(std::u32string_view(s).substr(i, d)).c_str());
  return j - i;
}

// ------------------------------------------------------------------ words

/** Abbreviations a voice spells or stumbles over, per language. */
Text spokenWords(Text s, std::string_view lang) {
  auto put = [](std::string_view words) {
    return [words](Text& out, size_t, size_t) { out += text(words); };
  };
  /** \b and the letters as they stand, with \b behind them where `whole` */
  auto word = [](std::u32string_view letters, bool whole = false) {
    return [letters, whole](const Text& t, size_t i) -> size_t { return wordStart(t, i) && startsAt(t, i, letters) && (!whole || wordEnd(t, i + letters.size())) ? letters.size() : 0; };
  };
  if (lang == "de") {
    // /\bz\.\s?B\./g
    s = replaceEach(
        s,
        [](const Text& t, size_t i) -> size_t {
          if (!wordStart(t, i) || !startsAt(t, i, U"z.")) return 0;
          size_t j = i + 2;
          if (j < t.size() && isSpace(t[j])) j++;
          return startsAt(t, j, U"B.") ? j + 2 - i : 0;
        },
        put("zum Beispiel"));
    s = replaceEach(s, word(U"bzw."), put("beziehungsweise"));          // /\bbzw\./g
    s = replaceEach(s, word(U"ggf."), put("gegebenenfalls"));           // /\bggf\./g
    s = replaceEach(s, word(U"StVO", true), put("Straßenverkehrsordnung"));   // /\bStVO\b/g
  } else if (lang == "en") {
    s = replaceEach(s, word(U"e.g."), put("for example"));              // /\be\.g\./g
    s = replaceEach(s, word(U"i.e."), put("that is"));                  // /\bi\.e\./g
  } else if (lang == "sq") {
    // ("daljen nr. 2": the number, not "en-er")   /\bnr\.\s?(?=\d)/g
    s = replaceEach(
        s,
        [](const Text& t, size_t i) -> size_t {
          if (!wordStart(t, i) || !startsAt(t, i, U"nr.")) return 0;
          size_t j = i + 3;
          if (j < t.size() && isSpace(t[j])) j++;
          return j < t.size() && isDigit(t[j]) ? j - i : 0;
        },
        put("numër "));
  } else if (lang == "vi") {
    // ("3/5" in the score: three of five, not a fraction or a date)   /(\d+)\s?\/\s?(\d+)/g → '$1 trên $2'
    size_t first = 0, second = 0, secondAt = 0;
    s = replaceEach(
        s,
        [&](const Text& t, size_t i) -> size_t {
          size_t j = i;
          while (j < t.size() && isDigit(t[j])) j++;
          if (j == i) return 0;
          first = j - i;
          if (j < t.size() && isSpace(t[j])) j++;
          if (j >= t.size() || t[j] != U'/') return 0;
          j++;
          if (j < t.size() && isSpace(t[j])) j++;
          secondAt = j;
          while (j < t.size() && isDigit(t[j])) j++;
          if (j == secondAt) return 0;
          second = j - secondAt;
          return j - i;
        },
        [&](Text& out, size_t i, size_t) {
          // (s is not changed while it is read: the figures are still at their places)
          out.append(s, i, first);
          out += text(" trên ");
          out.append(s, secondAt, second);
        });
  }
  return s;
}

// ------------------------------------------------------------------ the whole

/**
 * Glyphs that are labels on the screen (arrows of the keys, the buttons of
 * the touch controls, stars) or decoration: read out they are noise.
 * GLYPHS = /[←→↑↓▲▼◀▶⋯★☆✓✔✗✘•]|\p{Extended_Pictographic}/gu
 */
bool glyph(char32_t c) {
  switch (c) {
    case 0x2190: case 0x2192: case 0x2191: case 0x2193:   // ← → ↑ ↓
    case 0x25B2: case 0x25BC: case 0x25C0: case 0x25B6:   // ▲ ▼ ◀ ▶
    case 0x22EF:                                          // ⋯
    case 0x2605: case 0x2606:                             // ★ ☆
    case 0x2713: case 0x2714: case 0x2717: case 0x2718:   // ✓ ✔ ✗ ✘
    case 0x2022:                                          // •
      return true;
    default:
      return i18n::isPictographic(c);
  }
}

/** \s*<one of the signs>\s* at i, with at least `least` white space on either side: how long (0: none) */
template <class Sign>
size_t padded(const Text& s, size_t i, size_t least, Sign sign) {
  size_t j = i;
  while (j < s.size() && isSpace(s[j])) j++;
  if (j - i < least || j >= s.size() || !sign(s[j])) return 0;
  size_t k = j + 1;
  while (k < s.size() && isSpace(s[k])) k++;
  return k - (j + 1) < least ? 0 : k - i;
}

const auto PAUSE = [](Text& out, size_t, size_t) { out += U", "; };
const auto NOTHING = [](Text&, size_t, size_t) {};

/** s.trim() */
std::u16string trim16(std::u16string_view s) {
  size_t a = 0, b = s.size();
  while (a < b && isSpace(s[a])) a++;
  while (b > a && isSpace(s[b - 1])) b--;
  return std::u16string(s.substr(a, b - a));
}

/**
 * `s.split(re)` for an expression that looks behind and takes white space: matchEnd(s, q)
 * says where the match that begins at q ends (npos: none there).  (The steps of
 * RegExp.prototype[Symbol.split]: a match that is empty and lies at the end of the piece
 * before it does not cut.)
 */
template <class MatchEnd>
std::vector<std::u16string> split16(const std::u16string& s, MatchEnd matchEnd) {
  std::vector<std::u16string> out;
  const size_t size = s.size();
  if (size == 0) {
    // (an empty text: one empty piece, unless the expression matches it)
    if (matchEnd(s, 0) == std::u16string::npos) out.push_back(s);
    return out;
  }
  size_t p = 0, q = 0;
  auto advance = [&](size_t at) { return at + (s[at] >= 0xD800 && s[at] <= 0xDBFF && at + 1 < size && s[at + 1] >= 0xDC00 && s[at + 1] <= 0xDFFF ? 2 : 1); };
  while (q < size) {
    const size_t e = matchEnd(s, q);
    if (e == std::u16string::npos || e == p) {
      q = advance(q);
      continue;
    }
    out.push_back(s.substr(p, q - p));
    p = e > size ? size : e;
    q = p;
  }
  out.push_back(s.substr(p));
  return out;
}

size_t spaceEnd(const std::u16string& s, size_t q) {
  while (q < s.size() && isSpace(s[q])) q++;
  return q;
}

/**
 * Where a sentence ends: a stop with a space after it, or a full-width / Indic / Arabic one.
 * SENTENCE: behind one of . ! ? … at least one white space, or behind one of 。 ！ ？ । ؟ ۔ any white space (also none)
 */
size_t sentenceEnd(const std::u16string& s, size_t q) {
  if (q == 0) return std::u16string::npos;
  const char16_t before = s[q - 1];
  if (before == u'.' || before == u'!' || before == u'?' || before == 0x2026) {
    const size_t e = spaceEnd(s, q);
    return e > q ? e : std::u16string::npos;
  }
  if (before == 0x3002 || before == 0xFF01 || before == 0xFF1F || before == 0x0964 || before == 0x061F || before == 0x06D4) return spaceEnd(s, q);
  return std::u16string::npos;
}

/**
 * Where a long sentence may be cut: after a comma, colon or semicolon of any script.
 * CLAUSE: behind one of , ; : ، 、 ， ； ： any white space (also none)
 */
size_t clauseEnd(const std::u16string& s, size_t q) {
  if (q == 0) return std::u16string::npos;
  const char16_t before = s[q - 1];
  if (before == u',' || before == u';' || before == u':' || before == 0x060C || before == 0x3001 || before == 0xFF0C || before == 0xFF1B || before == 0xFF1A) return spaceEnd(s, q);
  return std::u16string::npos;
}

}  // namespace

std::string speakable(std::string_view utf8, std::string_view lang) {
  Text s = text(utf8);
  // marks of direction, soft hyphens, zero-width spaces (not the joiners: Persian writes with them)
  // (U+2066 … U+2069, U+200E, U+200F, U+200B, U+00AD)
  s = replaceEach(
      s, [](const Text& t, size_t i) -> size_t { return (t[i] >= 0x2066 && t[i] <= 0x2069) || t[i] == 0x200E || t[i] == 0x200F || t[i] == 0x200B || t[i] == 0xAD ? 1 : 0; }, NOTHING);
  // a key left as code: its name   /\[\[(\w+)\]\]/g
  s = replaceEach(
      s,
      [](const Text& t, size_t i) -> size_t {
        if (!startsAt(t, i, U"[[")) return 0;
        size_t j = i + 2;
        while (j < t.size() && isWord(t[j])) j++;
        return j > i + 2 && startsAt(t, j, U"]]") ? j + 2 - i : 0;
      },
      [&](Text& out, size_t i, size_t len) { out += text(keyName(i18n::encodeUtf8(std::u32string_view(s).substr(i + 2, len - 4)), lang)); });
  if (const OrdinalRule* ord = ordinalsOf(lang)) {
    int figure = 0;
    s = replaceEach(
        s, [&](const Text& t, size_t i) { return ordinalAt(*ord, t, i, figure); },
        [&](Text& out, size_t i, size_t len) {
          // ord.words[Number(n) - 1] ?? m
          if (figure >= 1 && figure <= 10) out += text(ord->words[figure - 1]);
          else out.append(s, i, len);
        });
  }
  s = spokenWords(std::move(s), lang);
  {
    UnitMatch m;
    s = replaceEach(
        s,
        [&](const Text& t, size_t i) -> size_t { return isDigit(t[i]) && unitAt(t, i, m) ? m.length : 0; },
        [&](Text& out, size_t i, size_t len) {
          const std::optional<std::string> words = unitWords(parseNum(s.substr(i, m.number)), m.token->unit, lang);
          if (words) out += text(*words);
          else out.append(s, i, len);
        });
  }
  // a separator or a dash between words: a pause   /\s*[·|]\s*/g, /\s+[–—]\s+/g
  s = replaceEach(s, [](const Text& t, size_t i) { return padded(t, i, 0, [](char32_t c) { return c == 0xB7 || c == U'|'; }); }, PAUSE);
  s = replaceEach(s, [](const Text& t, size_t i) { return padded(t, i, 1, [](char32_t c) { return c == 0x2013 || c == 0x2014; }); }, PAUSE);
  s = replaceEach(s, [](const Text& t, size_t i) -> size_t { return glyph(t[i]) ? 1 : 0; }, [](Text& out, size_t, size_t) { out += U' '; });
  // quotation marks are not read; brackets are a pause   /["„“”«»]/g, /\s*[()]\s*/g
  s = replaceEach(
      s, [](const Text& t, size_t i) -> size_t { return t[i] == U'"' || t[i] == 0x201E || t[i] == 0x201C || t[i] == 0x201D || t[i] == 0xAB || t[i] == 0xBB ? 1 : 0; }, NOTHING);
  s = replaceEach(s, [](const Text& t, size_t i) { return padded(t, i, 0, [](char32_t c) { return c == U'(' || c == U')'; }); }, PAUSE);
  // /\s+/g → ' '
  s = replaceEach(
      s,
      [](const Text& t, size_t i) -> size_t {
        size_t j = i;
        while (j < t.size() && isSpace(t[j])) j++;
        return j - i;
      },
      [](Text& out, size_t, size_t) { out += U' '; });
  // /\s+([,.!?;:])/g → '$1'
  s = replaceEach(
      s,
      [](const Text& t, size_t i) -> size_t {
        size_t j = i;
        while (j < t.size() && isSpace(t[j])) j++;
        if (j == i || j >= t.size()) return 0;
        const char32_t c = t[j];
        return c == U',' || c == U'.' || c == U'!' || c == U'?' || c == U';' || c == U':' ? j + 1 - i : 0;
      },
      [&](Text& out, size_t i, size_t len) { out += s[i + len - 1]; });
  // /,\s*,/g → ','   and   /,\s*([.!?])/g → '$1'
  s = replaceEach(
      s,
      [](const Text& t, size_t i) -> size_t {
        if (t[i] != U',') return 0;
        size_t j = i + 1;
        while (j < t.size() && isSpace(t[j])) j++;
        return j < t.size() && t[j] == U',' ? j + 1 - i : 0;
      },
      [](Text& out, size_t, size_t) { out += U','; });
  s = replaceEach(
      s,
      [](const Text& t, size_t i) -> size_t {
        if (t[i] != U',') return 0;
        size_t j = i + 1;
        while (j < t.size() && isSpace(t[j])) j++;
        return j < t.size() && (t[j] == U'.' || t[j] == U'!' || t[j] == U'?') ? j + 1 - i : 0;
      },
      [&](Text& out, size_t i, size_t len) { out += s[i + len - 1]; });
  // /^[\s,]+|[\s,]+$/g → ''
  size_t a = 0, b = s.size();
  while (a < b && (isSpace(s[a]) || s[a] == U',')) a++;
  while (b > a && (isSpace(s[b - 1]) || s[b - 1] == U',')) b--;
  return i18n::encodeUtf8(std::u32string_view(s).substr(a, b - a));
}

std::vector<std::string> splitForSpeech(std::string_view utf8, int max) {
  const std::u16string all = i18n::toUtf16(utf8);
  std::vector<std::string> parts;
  const size_t limit = max > 0 ? static_cast<size_t>(max) : 0;
  auto push = [&](const std::u16string& piece) { parts.push_back(i18n::fromUtf16(piece)); };
  std::function<void(const std::vector<std::u16string>&, const std::u16string&)> pack;
  auto cut = [&](const std::u16string& piece) {
    if (piece.size() <= limit) {
      push(piece);
      return;
    }
    std::vector<std::u16string> clauses;
    for (std::u16string& c : split16(piece, clauseEnd))
      if (!trim16(c).empty()) clauses.push_back(std::move(c));
    if (clauses.size() > 1) {
      pack(clauses, u" ");
      return;
    }
    std::u16string rest = piece;
    while (rest.size() > limit) {
      // rest.lastIndexOf(' ', max)
      const size_t found = rest.rfind(u' ', limit);
      size_t at = limit;
      if (found != std::u16string::npos && static_cast<double>(found) >= max / 3.0) at = found;
      // (a cut never falls between the two halves of a character beyond U+FFFF, where the original
      // would leave half a character on either side: the whole character goes to the next piece –
      // or, where a piece would be left without any, stays in this one)
      auto halves = [&](size_t i) { return i > 0 && i < rest.size() && rest[i - 1] >= 0xD800 && rest[i - 1] <= 0xDBFF && rest[i] >= 0xDC00 && rest[i] <= 0xDFFF; };
      if (halves(at)) at = at > 1 ? at - 1 : at + 1;
      // (no room at all – `max` below 1, with which the original never comes to an end: one character at a time)
      if (at == 0) at = halves(1) ? 2 : 1;
      push(trim16(std::u16string_view(rest).substr(0, at)));
      rest = trim16(std::u16string_view(rest).substr(at));
    }
    if (!rest.empty()) push(rest);
  };
  pack = [&](const std::vector<std::u16string>& pieces, const std::u16string& join) {
    std::u16string cur;
    for (const std::u16string& raw : pieces) {
      const std::u16string p = trim16(raw);
      if (p.empty()) continue;
      if (cur.empty()) cur = p;
      else if (cur.size() + join.size() + p.size() <= limit) cur += join + p;
      else {
        cut(cur);
        cur = p;
      }
    }
    if (!cur.empty()) cut(cur);
  };
  // (the scripts without spaces between sentences are joined without one: any of U+3000 … U+9FFF, U+AC00 … U+D7AF in the text)
  bool unspaced = false;
  for (char16_t c : all) unspaced = unspaced || (c >= 0x3000 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF);
  const std::vector<std::u16string> sentences = split16(trim16(all), sentenceEnd);
  pack(sentences, unspaced ? u"" : u" ");
  return parts;
}

}  // namespace fs

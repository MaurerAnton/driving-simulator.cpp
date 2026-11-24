// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/i18n/index.ts @ v1.7.7
#include "i18n/index.h"

#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#include "i18n/builtin.h"
#include "i18n/json.h"
#include "i18n/keys.h"
#include "i18n/unicode.h"
#include "i18n/violations.h"
#include "platform/intl.h"
#include "platform/plat.h"

namespace fs {

namespace i18n {

bool parseDict(std::string_view json, Dict& out) {
  Dict dict;
  JsonCursor c(json);
  std::string text;
  const bool ok = c.object([&](const std::string& key) {
    if (!c.string(text)) return false;
    dict.set(key, text);
    return true;
  });
  if (!ok || !c.atEnd()) return false;
  out = std::move(dict);
  return true;
}

std::string langUrl(std::string_view file) {
  std::string base = plat::locationBase();
  // (the page's own name, if the address ends in one, is not part of the folder)
  const size_t scheme = base.find("://");
  const size_t slash = base.rfind('/');
  if (slash == std::string::npos || (scheme != std::string::npos && slash < scheme + 3)) base += '/';
  else base.resize(slash + 1);
  return base + "lang/" + std::string(file) + "?v=" + BUILTIN_STAMP;
}

}  // namespace i18n

namespace detail {

const std::vector<LangInfo>& langs() {
  static const std::vector<LangInfo> list = [] {
    std::vector<LangInfo> out;
    out.reserve(i18n::BUILTIN_LANG_COUNT);
    for (size_t i = 0; i < i18n::BUILTIN_LANG_COUNT; i++) {
      const i18n::BuiltinLang& l = i18n::BUILTIN_LANGS[i];
      out.push_back(LangInfo{l.code, l.name, l.locale, l.rtl, !l.builtin});
    }
    return out;
  }();
  return list;
}

}  // namespace detail

namespace {

struct StringHash {
  using is_transparent = void;
  size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};
struct StringEqual {
  using is_transparent = void;
  bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
};

struct State {
  /** `dicts`: the languages that are here – German and English from the start */
  std::unordered_map<std::string, Dict, StringHash, StringEqual> dicts;
  const Dict* en = nullptr;
  KeyStyle keyStyle = KeyStyle::Keys;
  /** the languages on their way */
  std::unordered_map<std::string, Task<bool>> loading;
};

State& state() {
  static State st = [] {
    State s;
    for (size_t i = 0; i < i18n::BUILTIN_DICT_COUNT; i++) {
      const i18n::BuiltinText& b = i18n::BUILTIN_DICTS[i];
      Dict d;
      if (!i18n::parseDict(std::string_view(b.json, b.size), d)) {
        // (the program's own texts: only a broken build gets here)
        std::fprintf(stderr, "i18n: the built-in texts of \"%s\" cannot be read\n", b.code);
        std::abort();
      }
      s.dicts.emplace(b.code, std::move(d));
    }
    return s;
  }();
  if (!st.en) {
    auto it = st.dicts.find(std::string_view("en"));
    if (it != st.dicts.end()) st.en = &it->second;
  }
  return st;
}

const LangInfo* findLang(std::string_view code) {
  for (const LangInfo& l : detail::langs())
    if (l.code == code) return &l;
  return nullptr;
}

/**
 * Inside right-to-left text a citation such as "§ 37 Abs. 2 StVO" would be
 * torn apart and re-ordered; marked as one left-to-right piece it stays whole.
 */
const char LRI[] = "\xe2\x81\xa6";    // U+2066 left-to-right isolate
const char PDI[] = "\xe2\x81\xa9";    // U+2069 pop directional isolate
const char NBSP[] = "\xc2\xa0";       // U+00A0
const char SECTION[] = "\xc2\xa7";    // U+00A7 §
std::string ltrPiece(const std::string& s) { return LRI + s + PDI; }

/** CITATION = /§\s*\d[\w\s.,()§]*?(?:StVO|UrhG)/ at position i: how long it is there (0: it is not) */
size_t citationAt(const std::u32string& s, size_t i) {
  const size_t n = s.size();
  if (s[i] != 0xA7) return 0;
  size_t j = i + 1;
  while (j < n && i18n::isSpace(s[j])) j++;
  if (j >= n || !i18n::isDigit(s[j])) return 0;
  j++;
  // (as few characters as possible, up to the law)
  for (;; j++) {
    if (i18n::startsAt(s, j, U"StVO") || i18n::startsAt(s, j, U"UrhG")) return j + 4 - i;
    if (j >= n) return 0;
    const char32_t c = s[j];
    if (!(i18n::isWord(c) || i18n::isSpace(c) || c == U'.' || c == U',' || c == U'(' || c == U')' || c == 0xA7)) return 0;
  }
}

/** `dicts.get(lang)?.[key] ?? en[key] ?? key` */
std::string lookup(std::string_view key, std::string_view lang) {
  State& st = state();
  auto it = st.dicts.find(lang);
  if (it != st.dicts.end()) {
    if (const std::string* text = it->second.find(key)) return *text;
  }
  if (st.en) {
    if (const std::string* text = st.en->find(key)) return *text;
  }
  return std::string(key);
}

/** The file has arrived (or not): keep its texts. */
Task<bool> fetchLang(std::string code) {
  plat::Response r = co_await plat::fetch(i18n::langUrl(code + ".json"));
  State& st = state();
  bool ok = false;
  if (r.ok()) {
    Dict d;
    if (i18n::parseDict(r.text(), d)) {
      st.dicts.insert_or_assign(code, std::move(d));
      ok = true;
    }
  }
  st.loading.erase(code);
  co_return ok;
}

}  // namespace

const LangInfo& langInfo(std::string_view code) {
  const LangInfo* l = findLang(code);
  return l ? *l : detail::langs()[1];
}
bool isRtl(std::string_view code) { return langInfo(code).rtl == true; }
const std::string& localeOf(std::string_view code) { return langInfo(code).locale; }

Task<bool> loadLang(std::string code) {
  State& st = state();
  if (st.dicts.find(code) != st.dicts.end()) return resolved(true);
  const LangInfo* info = findLang(code);
  if (!info || !info->load) return resolved(false);
  // (asked for again while it is on its way: the same request, as a module is imported once)
  auto on = st.loading.find(code);
  if (on != st.loading.end()) return on->second;
  Task<bool> task = fetchLang(code);
  if (!task.done()) st.loading.emplace(code, task);
  return task;
}

std::string pickLang(const std::vector<std::string>& preferred) {
  /** Languages that are not here, but whose readers understand one that is. */
  static const std::pair<const char*, const char*> RELATED[] = {
      {"bs", "hr"}, {"sr", "hr"}, {"sh", "hr"}, {"sk", "cs"}, {"ms", "id"}, {"be", "ru"}, {"ca", "es"}, {"gl", "es"}, {"mo", "ro"}, {"lb", "de"}, {"gsw", "de"}, {"nds", "de"},
  };
  auto has = [](std::string_view code) { return !code.empty() && findLang(code) != nullptr; };
  for (const std::string& p : preferred) {
    const std::string lower = toLowerAscii(p);
    const std::string primary = lower.substr(0, lower.find('-'));
    if (has(primary)) return primary;
    for (const auto& [from, to] : RELATED)
      if (primary == from && has(to)) return to;
  }
  return "en";
}

void setKeyStyle(KeyStyle style) { state().keyStyle = style; }

std::string keyName(std::string_view code, std::string_view lang) {
  return keyLabel(code, lang, [&](std::string_view k) { return lookup(k, lang); });
}

std::string tr(std::string_view key, std::string_view lang, const TrParams& params) {
  std::string s = lookup(key, lang);
  if (s.find("[[") != std::string::npos) {
    const KeyNameFn name = [&](std::string_view k) { return lookup(k, lang); };
    const bool touch = state().keyStyle == KeyStyle::Touch;
    // s.replace(/\[\[(\w+)\]\]/g, …)
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
      if (s.compare(i, 2, "[[") == 0) {
        size_t end = i + 2;
        while (end < s.size() && i18n::isWord(static_cast<unsigned char>(s[end]))) end++;
        if (end > i + 2 && s.compare(end, 2, "]]") == 0) {
          const std::string_view code = std::string_view(s).substr(i + 2, end - i - 2);
          out += touch ? touchLabel(code, lang, name) : keyLabel(code, lang, name);
          i = end + 2;
          continue;
        }
      }
      out += s[i++];
    }
    s = std::move(out);
  }
  // (a value is put in as it is: it may contain "$&" – a file name, a street name)
  for (const TrParam& p : params) s = replaceAll(std::move(s), "{" + p.name + "}", p.value);
  // (inside a sentence also kept on one line)
  if (isRtl(lang) && s.find(SECTION) != std::string::npos) {
    const std::u32string u = i18n::decodeUtf8(s);
    std::string out;
    for (size_t i = 0; i < u.size();) {
      const size_t len = citationAt(u, i);
      if (len == 0) {
        i18n::appendUtf8(out, u[i++]);
        continue;
      }
      std::string piece;
      for (size_t k = i; k < i + len; k++) {
        if (u[k] == U' ') piece += NBSP;
        else i18n::appendUtf8(piece, u[k]);
      }
      out += ltrPiece(piece);
      i += len;
    }
    s = std::move(out);
  }
  return s;
}

namespace {
struct UnitName {
  const char* unit;
  const char* fallback;
};
/** the unit as Intl names it, and UNIT_FALLBACK */
UnitName unitName(Unit unit) {
  switch (unit) {
    case Unit::Kilometer: return {"kilometer", "km"};
    case Unit::Meter: return {"meter", "m"};
    case Unit::Minute: return {"minute", "min"};
  }
  return {"meter", "m"};
}
}  // namespace

std::string formatUnit(double value, Unit unit, std::string_view lang, int digits) {
  const UnitName u = unitName(unit);
  try {
    return plat::formatNumber(value, localeOf(lang),
                              {.style = "unit", .unit = u.unit, .unitDisplay = "short", .minimumFractionDigits = digits, .maximumFractionDigits = digits, .numberingSystem = "latn"});
  } catch (const plat::IntlError&) {
    return toFixed(value, digits) + " " + u.fallback;
  }
}

std::string formatMonth(double time, std::string_view lang) {
  // (the calendar of the photographs and of the country the simulator is about, whatever the language's own is)
  return plat::formatDate(time, localeOf(lang), {.month = "long", .year = "numeric", .timeZone = "UTC", .numberingSystem = "latn", .calendar = "gregory"});
}

std::string violationTitle(std::string_view id, std::string_view lang, const TrParams& data) { return tr("v." + std::string(id) + ".title", lang, data); }

std::string violationWhy(std::string_view id, std::string_view lang) { return tr("v." + std::string(id) + ".why", lang); }

std::string violationRef(std::string_view id, std::string_view lang) {
  const std::string* found = VIOLATION_REFS.find(id);
  const std::string r = found ? *found : "";
  if (!r.empty() && r[0] == '@') return tr(std::string_view(r).substr(1), lang);
  return isRtl(lang) ? ltrPiece(r) : r;
}

std::string lessonTitle(std::string_view id, std::string_view lang) { return tr("lesson." + std::string(id) + ".title", lang); }
std::string lessonIntro(std::string_view id, std::string_view lang) { return tr("lesson." + std::string(id) + ".intro", lang); }

const Dict* dictOf(std::string_view code) {
  State& st = state();
  auto it = st.dicts.find(code);
  return it == st.dicts.end() ? nullptr : &it->second;
}

}  // namespace fs

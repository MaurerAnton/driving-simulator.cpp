// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The texts: every language is complete and well-formed, and the machinery
 * around them (fallback, placeholders, key names) works.
 *
 * Origin: tests/i18n/i18n.test.ts (MaurerAnton/driving-simulator @ 408014b,
 * v0.6.0).  The original loads each language from its file; here every
 * dictionary is registered with loadLang, and the checker (the original's
 * tools/i18n-check.ts) lives in src/i18n/check.h so the test can run it
 * against any dict.
 */
#include "../src/i18n/check.h"
#include "../src/i18n/index.h"
#include "../src/i18n/violations.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace fs;

namespace {
const Dict& en() { return rawDictOf("en"); }

bool inDict(const Dict& d, const std::string& key) {
  for (const auto& e : d)
    if (e.first == key) return true;
  return false;
}

std::string get(const Dict& d, const std::string& key) {
  for (const auto& e : d)
    if (e.first == key) return e.second;
  return "";
}

bool contains(const std::vector<std::string>& list, const std::string& needle) {
  for (const std::string& e : list)
    if (e.find(needle) != std::string::npos) return true;
  return false;
}

bool containsWarning(const std::vector<std::string>& list, const std::string& key) {
  for (const std::string& e : list)
    if (e.rfind(key + ":", 0) == 0) return true;
  return false;
}
}  // namespace

int main() {
  // ---- every language is registered, and every registered language loads ----
  {
    assert(loadLang("en"));
    assert(loadLang("de"));
    std::vector<std::string> codes;
    for (const LangInfo& l : LANGS) {
      codes.push_back(l.code);
      assert(loadLang(l.code));
      assert(l.name.size() > 1);
      assert(l.locale.rfind(l.code, 0) == 0);
      assert(dictOf(l.code) != nullptr);
    }
    // no language twice
    for (size_t i = 0; i < codes.size(); i++)
      for (size_t j = i + 1; j < codes.size(); j++) assert(codes[i] != codes[j]);
    assert(codes.size() == 28);
    assert(!loadLang("xx"));   // an unknown language is refused
  }

  // ---- every language is complete: same keys, placeholders, key tokens ----
  for (const LangInfo& l : LANGS) {
    loadLang(l.code);
    Report r = checkDict(l.code, *dictOf(l.code));
    assert(r.errors.empty());
  }

  // ---- the catalogue of faults and the lessons have their texts ----
  {
    const Dict& e = en();
    for (const auto& ref : VIOLATION_REFS) {
      assert(inDict(e, "v." + ref.first + ".title"));
      assert(inDict(e, "v." + ref.first + ".why"));
      assert(violationRef(ref.first, "en").size() > 3);
    }
    for (const auto& entry : e) {
      const std::string& k = entry.first;
      if (k.rfind("v.", 0) == 0 && k.size() > 7 &&
          k.compare(k.size() - 6, 6, ".title") == 0) {
        std::string id = k.substr(2, k.size() - 8);
        bool found = false;
        for (const auto& ref : VIOLATION_REFS)
          if (ref.first == id) found = true;
        assert(found);
      }
    }
    for (const std::string& id : {"basics", "rbl", "priority", "signals", "roundabout",
                                  "rural", "motorway", "exam"}) {
      assert(inDict(e, std::string("lesson.") + id + ".title"));
      assert(inDict(e, std::string("lesson.") + id + ".intro"));
      assert(lessonTitle(id, "en") == get(e, std::string("lesson.") + id + ".title"));
    }
    assert(lessonIntro("rbl", "de").find("[[") == std::string::npos);
  }

  // ---- texts: placeholders, fallback to English, rule references ----
  {
    assert(tr("navIn", "en", {{"d", "120"}, {"what", "turn left"}}) ==
           "In 120 m turn left");
    assert(tr("navIn", "de", {{"d", "120"}, {"what", "links abbiegen"}}) ==
           "In 120 m links abbiegen");
    assert(violationTitle("speed", "en", {{"speed", "62"}, {"limit", "50"}}) ==
           "Too fast: 62 where 50 km/h is allowed");
    assert(violationWhy("redLight", "de").rfind("Rot", 0) == 0);
    assert(violationRef("redLight", "de") == "§ 37 Abs. 2 StVO");
    assert(violationRef("stall", "de") == "Fahrzeugbedienung");
    assert(violationRef("stall", "en") == "Vehicle handling");
    assert(tr("no.such.key", "de") == "no.such.key");
    assert(tr("paused", "xx") == "Paused");   // an unknown language falls back to English
  }

  // ---- keys are named as printed on the keyboard usual for the language ----
  {
    setKeyStyle(KeyStyle::Keys);
    assert(tr("fastenBelt", "en") == "Fasten your seat belt: B");
    assert(tr("releaseHandbrake", "de") == "Handbremse lösen: Leertaste");
    assert(tr("releaseHandbrake", "en") == "Release the handbrake: Space");
    // the key left of X: Z on a US keyboard, Y on a German one
    assert(keyName("KeyZ", "en") == "Z");
    assert(keyName("KeyZ", "de") == "Y");
    assert(keyName("KeyY", "de") == "Z");
    assert(keyName("Digit1", "en") == "1");
    assert(keyName("ArrowUp", "en") == "\xe2\x86\x91");   // ↑
    assert(keyName("F1", "en") == "F1");
    assert(keyName("ShiftLeft", "de") == "Umschalt");
    assert(keyName("Escape", "en") == "Esc");
    loadLang("fr");
    // French keyboards: the driving keys W A S D carry Z Q S D
    assert(keyName("KeyW", "fr") == "Z");
    assert(keyName("KeyA", "fr") == "Q");
    assert(keyName("KeyS", "fr") == "S");
    assert(keyName("KeyD", "fr") == "D");
    assert(keyName("KeyQ", "fr") == "A");
    setKeyStyle(KeyStyle::Touch);
    assert(tr("releaseHandbrake", "en") == "Release the handbrake: P");
    assert(tr("basics.belt", "en") == "Fasten your seat belt: press \xe2\x8b\x8f Seat belt");
    assert(tr("startEngine", "en") == "Start the engine: hold \xe2\x8b\x8f Starter");
    assert(tr("basics.pullAway", "de") == "Sanft anfahren: etwas Gas (GAS), bis 15 km/h");
    assert(keyName("KeyB", "en") == "B");   // tables of keys always show the key
    setKeyStyle(KeyStyle::Keys);
  }

  // ---- the language is chosen from what the reader prefers ----
  {
    assert(pickLang({"de-AT", "en"}) == "de");
    assert(pickLang({"en-US"}) == "en");
    assert(pickLang({"xx", "yy"}) == "en");
    assert(pickLang({"sr-RS", "en"}) == "hr");
    assert(pickLang({"gsw-CH"}) == "de");
    assert(pickLang({}) == "en");
    for (const LangInfo& l : LANGS) assert(pickLang({l.code + "-XX", "en"}) == l.code);
    assert(!isRtl("de"));
    assert(localeOf("de") == "de-DE");
    assert(isRtl("ar"));
  }

  // ---- right-to-left: citations stay in one left-to-right piece ----
  {
    const std::string LRI = "\xe2\x81\xa7";   // ‪
    const std::string PDI = "\xe2\x81\xa9";   //  
    const std::string NBSP = "\xc2\xa0";
    bool sawArabic = false;
    for (const LangInfo& l : LANGS) {
      if (!l.rtl) continue;
      loadLang(l.code);
      if (l.code == "ar") sawArabic = true;
      assert(violationRef("redLight", l.code) == LRI + "§ 37 Abs. 2 StVO" + PDI);
      assert(lessonIntro("rbl", l.code).find(LRI + "§" + NBSP + "8" + NBSP + "StVO" + PDI) !=
             std::string::npos);
      assert(tr("about.signs", l.code).find(
                 LRI + "§" + NBSP + "5" + NBSP + "(1)" + NBSP + "UrhG" + PDI) !=
             std::string::npos);
      assert(violationRef("stall", l.code).find(LRI) == std::string::npos);
    }
    assert(sawArabic);
    assert(violationRef("redLight", "en") == "§ 37 Abs. 2 StVO");
    assert(lessonIntro("rbl", "de").find(LRI) == std::string::npos);
  }

  // ---- the checker finds what can go wrong in a translation ----
  {
    auto broken = [](const std::vector<std::pair<std::string, std::string>>& change) {
      Dict d = en();
      for (const auto& kv : change) {
        if (kv.second.empty()) {
          for (size_t i = 0; i < d.size(); i++)
            if (d[i].first == kv.first) {
              d.erase(d.begin() + static_cast<long>(i));
              break;
            }
        } else {
          bool found = false;
          for (auto& e : d)
            if (e.first == kv.first) {
              e.second = kv.second;
              found = true;
              break;
            }
          if (!found) d.push_back(kv);
        }
      }
      return checkDict("xx", d);
    };
    assert(broken({}).errors.empty());
    assert(contains(broken({{"paused", ""}}).errors, "paused: missing"));
    assert(contains(broken({{"extra", "x"}}).errors, "extra: not a key of en.ts"));
    assert(contains(broken({{"navIn", "In {dist} m {what}"}}).errors, "placeholders differ"));
    assert(contains(broken({{"fastenBelt", "Fasten your seat belt: B"}}).errors,
                   "key tokens differ"));
    assert(contains(broken({{"fastenBelt", "Fasten your seat belt: [[KeyB]] [[KeyB]]"}}).errors,
                   "key tokens differ"));
    assert(contains(broken({{"tbGas", "ACCELERATOR"}}).errors, "at most 9 fit"));
    assert(contains(broken({{"walk", "walkingspeed"}}).errors, "does not fit the roundel"));
    assert(contains(broken({{"lesson.motorway.title", "7 · Motorway"}}).errors,
                   "\"A 49\" must stay"));
    {
      std::string why = get(en(), "v.stopSign.why");
      size_t at = why.find("206");
      std::string changed = why;
      if (at != std::string::npos) changed.replace(at, 3, "207");
      assert(contains(broken({{"v.stopSign.why", changed}}).errors, "number 206 is missing"));
    }
    assert(contains(broken({{"paused", "Paused <b>"}}).errors, "no HTML"));
    assert(contains(broken({{"paused", " Paused"}}).errors, "white space"));
    {
      std::string why = get(en(), "v.speed.why");
      size_t at = why.find("50");
      std::string changed = why;
      if (at != std::string::npos) changed.replace(at, 2, "\xd9\xa5\xd9\xa0");  // ٥٠
      assert(contains(broken({{"v.speed.why", changed}}).errors, "Western"));
    }
    assert(contains(broken({{"paused", "Paused\xe2\x80\x8f"}}).errors, "direction mark"));
    assert(contains(broken({{"trManual", "Manual gearbox"}}).errors, "ctl.clutch: must quote"));
    // a language with its own script must be written in it
    {
      Report ru = checkDict("ru", en());
      bool scriptErr = false;
      for (const auto& e : ru.errors)
        if (e.find("script") != std::string::npos) scriptErr = true;
      assert(scriptErr);
    }
    // hints, not errors: sentence ends and capital letters
    {
      Dict d = en();
      for (auto& e : d) {
        if (e.first == "paused") e.second = "Paused.";
        if (e.first == "navLeft") e.second = "Turn left";
        if (e.first == "freeDesc" && !e.second.empty() && e.second.back() == '.')
          e.second.pop_back();
      }
      Report h = checkDict("xx", d);
      assert(containsWarning(h.warnings, "paused"));
      assert(containsWarning(h.warnings, "navLeft"));
      assert(containsWarning(h.warnings, "freeDesc"));
    }
  }

  // ---- quantities and dates, with Western digits ----
  {
    auto plain = [](std::string s) {
      for (char& c : s)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
      return s;
    };
    assert(plain(formatUnit(5.12, "kilometer", "de", 1)) == "5,1 km");
    assert(plain(formatUnit(5.12, "kilometer", "en", 1)) == "5.1 km");
    {
      std::string m = formatUnit(12, "minute", "en");
      assert(m.rfind("12", 0) == 0);
      assert(m.find("min") != std::string::npos);
    }
    assert(plain(formatUnit(40, "meter", "de")) == "40 m");
    {
      std::string ru = formatUnit(5.12, "kilometer", "ru", 1);
      assert(ru.rfind("5,1", 0) == 0);
      assert(ru.find("\xd0\xba\xd0\xbc") != std::string::npos);   // км
    }
    assert(formatMonth(1684300800000.0, "de") == "Mai 2023");   // 2023-05-17 UTC
    assert(formatMonth(1684300800000.0, "en") == "May 2023");
    for (const LangInfo& l : LANGS) {
      std::vector<std::string> texts = {
          formatUnit(1234.5, "kilometer", l.code, 1),
          formatUnit(7, "minute", l.code),
          formatUnit(35, "meter", l.code),
          formatMonth(1684300800000.0, l.code),
      };
      for (const std::string& t : texts) {
        bool western = false;
        bool eastern = false;
        for (unsigned char c : t) {
          if (c >= '0' && c <= '9') western = true;
          if (c >= 0xd9 && c <= 0xdb) eastern = true;   // ٠-٩ ۰-۹
          if (c == 0xe0 || c == 0xe6) eastern = true;   // ०-९ ০-৯
        }
        assert(western);
        assert(!eastern);
      }
      assert(texts[3].find("2023") != std::string::npos);
    }
    // a value is put in as it is, whatever it contains
    assert(tr("navIn", "en", {{"d", "50"}, {"what", "cost $& more {d}"}}) ==
           "In 50 m cost $& more {d}");
  }

  std::printf("i18n: all checks passed (%zu languages, %zu texts each)\n", LANGS.size(),
              en().size());
  return 0;
}

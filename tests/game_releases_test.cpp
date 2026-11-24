// LINK: game sim render i18n audio input engine world core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The versions and their notes ("What's new"): the newest is the version of
 * the game, every note is there in every language, it is shown once
 * after an update – to whoever played before.
 *
 * Origin: tests/game/releases.test.ts @ v1.7.7
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/js.h"
#include "game/releases.h"
#include "i18n/index.h"
#include "platform/native/native.h"
#include "platform/plat.h"

using namespace fs;

/** "1.7.7" as one number: 1 007 007 */
static double num(const std::string& v) {
  double a = 0;
  for (const std::string& p : split(v, ".")) a = a * 1000 + jsNumber(p);
  return a;
}

/** `/^\d+\.\d+\.\d+$/` */
static bool threeNumbers(const std::string& v) {
  const std::vector<std::string> parts = split(v, ".");
  if (parts.size() != 3) return false;
  for (const std::string& p : parts) {
    if (p.empty()) return false;
    for (char c : p)
      if (c < '0' || c > '9') return false;
  }
  return true;
}

/** `/^[0-9a-f]{40}$/` */
static bool commitName(const std::string& c) {
  if (c.size() != 40) return false;
  for (char ch : c)
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
  return true;
}

/** the word of a name in what sizeWords gives */
static std::string word(const std::vector<std::pair<std::string, std::string>>& words, const std::string& name) {
  for (const auto& [k, v] : words)
    if (k == name) return v;
  assert(false);
  return "";
}

/** a storage that keeps what it is given */
struct MemoryStore : KeyValueStore {
  std::map<std::string, std::string> data;
  std::optional<std::string> getItem(const std::string& key) override {
    auto it = data.find(key);
    if (it == data.end()) return std::nullopt;
    return it->second;
  }
  bool setItem(const std::string& key, const std::string& value) override {
    data[key] = value;
    return true;
  }
  void removeItem(const std::string& key) override { data.erase(key); }
};

int main() {
  // the newest release is the version of the game, and the versions go down the list
  {
    assert(VERSION == RELEASES[0].version);
    for (size_t i = 1; i < RELEASES.size(); i++) assert(num(RELEASES[i - 1].version) > num(RELEASES[i].version));   // newest first
    bool has177 = false;
    for (const Release& r : RELEASES) {
      assert(threeNumbers(r.version));
      assert(r.date.size() == 10 && r.date[4] == '-' && r.date[7] == '-');
      assert(r.items.size() > 0);
      has177 = has177 || r.version == "1.7.7";
    }
    // (the version of the original the port was made from)
    assert(has177);
  }

  // every note of every release is there in every language
  {
    plat::native::mount("http://localhost/lang/", "web/lang/");
    for (const LangInfo& lang : LANGS) {
      // (the language's own file: the game would fall back to English for what is missing)
      assert(plat::native::wait(loadLang(lang.code)));
      const Dict* dict = dictOf(lang.code);
      assert(dict);
      for (const Release& r : RELEASES) {
        std::vector<std::string> keys = r.items;
        if (r.intro) keys.push_back(*r.intro);
        keys.insert(keys.end(), {"news", "newsVersion", "news.size"});
        if (r.dev) keys.insert(keys.end(), {"news.dev", "news.devIntro"});
        for (const std::string& key : keys) {
          const std::string* text = dict->find(key);
          if (!text || text->empty()) std::fprintf(stderr, "%s: %s\n", lang.code.c_str(), key.c_str());
          assert(text && !text->empty());
        }
      }
    }
  }

  // the development versions come below the releases, oldest last, each with its commit
  {
    size_t firstDev = RELEASES.size();
    for (size_t i = 0; i < RELEASES.size(); i++) {
      if (RELEASES[i].dev) {
        firstDev = i;
        break;
      }
    }
    assert(firstDev > 0 && firstDev < RELEASES.size());   // there are development versions, and they are not the newest
    for (size_t i = 0; i < RELEASES.size(); i++) {
      const Release& r = RELEASES[i];
      assert(r.dev == (i >= firstDev));   // all development versions together, below the releases
      if (r.dev) {
        assert(startsWith(r.version, "0."));   // before 1.0.0
        assert(r.commit && commitName(*r.commit));
        assert(r.at != "");   // the moment of its commit
      } else assert(!r.commit);   // a release is found by its tag
    }
    assert(RELEASES.back().version == "0.1.0");
  }

  // every version says how big the game was – and it grew
  {
    for (size_t i = 0; i < RELEASES.size(); i++) {
      const Release& r = RELEASES[i];
      // (the newest: counted right before its release)
      if (!r.size) {
        assert(i == 0 && r.at == "");
        continue;
      }
      const ReleaseSize& s = *r.size;
      for (double v : {s.lines, s.code, s.tests, s.i18n, s.bytes}) assert(v == std::floor(v) && v >= 0);
      assert(s.code + s.tests + s.i18n <= s.lines);   // the parts are lines among all
      assert(s.code > 10000 && s.lines < s.bytes);    // plausible
    }
    std::vector<const Release*> counted;
    for (const Release& r : RELEASES)
      if (r.size) counted.push_back(&r);
    for (size_t i = 1; i < counted.size(); i++) {
      const ReleaseSize& newer = *counted[i - 1]->size;
      const ReleaseSize& older = *counted[i]->size;
      // (a version may tidy up a little, not lose a tenth)
      assert(newer.lines >= older.lines * 0.9);
      assert(newer.code >= older.code * 0.9);
      assert(newer.bytes >= older.bytes * 0.9);
      assert(newer.lines <= older.lines * 3);   // not more than three times as many lines at once
    }
    const auto w = sizeWords({.lines = 172000, .code = 120400, .tests = 17000, .i18n = 36000, .bytes = 11600000}, "en-GB");
    // no additional maps: no word for them
    assert(w.size() == 6 && word(w, "lines") == "172,000" && word(w, "code") == "120k" && word(w, "tests") == "17k" && word(w, "texts") == "36k" && word(w, "bytes") == "11.6 MB" && word(w, "maps") == "");
    assert(word(sizeWords({.lines = 1, .code = 1, .tests = 1, .i18n = 1, .bytes = 1450000}, "de-DE"), "bytes") == "1,5 MB");
    // (the additional maps are counted apart from the game's own files)
    assert(word(sizeWords({.lines = 1, .code = 1, .tests = 1, .i18n = 1, .bytes = 20000000, .maps = 2532710569.0}, "en-GB"), "maps") == "2.5 GB");
    assert(word(sizeWords({.lines = 1, .code = 1, .tests = 1, .i18n = 1, .bytes = 20000000, .maps = 2532710569.0}, "en-GB"), "bytes") == "20.0 MB");   // the game is not inflated by the maps
  }

  // "What's new" comes once after an update – not for whoever comes for the first time
  {
    MemoryStore store;
    assert(newsDue(false, &store) == false);   // a first visit
    assert(newsDue(true, &store) == false);    // and after it: the notes of this version were not news to them
    store.data.clear();
    store.data["fahrsim-baunatal.news"] = "0.9.0";
    assert(newsDue(true, &store) == true);    // played an earlier version: shown
    assert(newsDue(true, &store) == false);   // once
    assert(newsDue(true, nullptr) == false);  // without storage: never
  }

  std::printf("game_releases_test: ok\n");
  return 0;
}

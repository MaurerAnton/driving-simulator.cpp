// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/i18n/exam/index.ts @ v1.7.7
#include "i18n/exam/index.h"

#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#include "i18n/builtin.h"
#include "i18n/index.h"
#include "i18n/json.h"
#include "platform/plat.h"

namespace fs {

namespace i18n {

namespace {

/** `{ "q": …, "a": […], "why": … }` */
bool examText(JsonCursor& c, ExamText& out) {
  return c.object([&](const std::string& name) {
    if (name == "q") return c.string(out.q);
    if (name == "why") return c.string(out.why);
    if (name == "a") {
      out.a.clear();
      return c.array([&] {
        std::string answer;
        if (!c.string(answer)) return false;
        out.a.push_back(std::move(answer));
        return true;
      });
    }
    return c.skip();
  });
}

bool examTextsOf(JsonCursor& c, Record<ExamText>& out) {
  return c.object([&](const std::string& id) {
    ExamText t;
    if (!examText(c, t)) return false;
    out.set(id, std::move(t));
    return true;
  });
}

}  // namespace

bool parseExamTexts(std::string_view json, ExamTexts& out) {
  ExamTexts texts;
  JsonCursor c(json);
  const bool ok = c.object([&](const std::string& name) {
    if (name == "questions") return examTextsOf(c, texts.questions);
    if (name == "danger") return examTextsOf(c, texts.danger);
    if (name == "signs") {
      return c.object([&](const std::string& number) {
        std::string meaning;
        if (!c.string(meaning)) return false;
        texts.signs.set(number, std::move(meaning));
        return true;
      });
    }
    return c.skip();
  });
  if (!ok || !c.atEnd()) return false;
  out = std::move(texts);
  return true;
}

}  // namespace i18n

namespace detail {

const std::vector<std::string>& examLangs() {
  static const std::vector<std::string> list(i18n::BUILTIN_EXAM_LANGS, i18n::BUILTIN_EXAM_LANGS + i18n::BUILTIN_EXAM_LANG_COUNT);
  return list;
}

const ExamTexts& examEn() {
  static const ExamTexts en = [] {
    ExamTexts t;
    if (!i18n::parseExamTexts(std::string_view(i18n::BUILTIN_EXAM_EN.json, i18n::BUILTIN_EXAM_EN.size), t)) {
      // (the program's own words: only a broken build gets here)
      std::fprintf(stderr, "i18n: the built-in words of the learning modes cannot be read\n");
      std::abort();
    }
    return t;
  }();
  return en;
}

}  // namespace detail

namespace {

/** the words that have arrived, as their files have them (what `import()` keeps) */
std::unordered_map<std::string, ExamTexts>& loaded() {
  static std::unordered_map<std::string, ExamTexts> map;
  return map;
}
/** … and those on their way (asked for again meanwhile: the same request, as a module is imported once) */
std::unordered_map<std::string, Task<bool>>& arriving() {
  static std::unordered_map<std::string, Task<bool>> map;
  return map;
}

/** `await load()`: the file of a language; false – it has not come, or is not of the shape */
Task<bool> fetchExamTexts(std::string lang) {
  plat::Response r = co_await plat::fetch(i18n::langUrl("exam/" + lang + ".json"));
  ExamTexts own;
  const bool ok = r.ok() && i18n::parseExamTexts(r.text(), own);
  if (ok) loaded().insert_or_assign(lang, std::move(own));
  arriving().erase(lang);
  co_return ok;
}

}  // namespace

ExamTexts withFallback(const ExamTexts* own) {
  const ExamTexts& en = detail::examEn();
  if (!own) return en;
  auto merge = [](const Record<ExamText>& a, const Record<ExamText>& b) {
    Record<ExamText> out = b;
    for (const auto& [k, v] : a) {
      const ExamText* base = b.find(k);
      if (!base) {
        out.set(k, v);
        continue;
      }
      // (a question whose answers are not all translated keeps English for the missing ones)
      ExamText t;
      t.q = !v.q.empty() ? v.q : base->q;
      t.a.reserve(base->a.size());
      for (size_t i = 0; i < base->a.size(); i++) t.a.push_back(i < v.a.size() && !v.a[i].empty() ? v.a[i] : base->a[i]);
      t.why = !v.why.empty() ? v.why : base->why;
      out.set(k, std::move(t));
    }
    return out;
  };
  ExamTexts out;
  out.questions = merge(own->questions, en.questions);
  out.danger = merge(own->danger, en.danger);
  out.signs = en.signs;
  for (const auto& [k, v] : own->signs) out.signs.set(k, v);
  return out;
}

Task<ExamTexts> loadExamTexts(std::string lang) {
  bool known = false;
  for (const std::string& l : detail::examLangs()) known = known || l == lang;
  if (!known || lang == "en") co_return detail::examEn();
  if (loaded().find(lang) == loaded().end()) {
    Task<bool> task;
    auto on = arriving().find(lang);
    if (on != arriving().end()) task = on->second;
    else {
      task = fetchExamTexts(lang);
      if (!task.done()) arriving().emplace(lang, task);
    }
    const bool ok = co_await task;
    if (!ok) co_return detail::examEn();
  }
  co_return withFallback(&loaded().at(lang));
}

}  // namespace fs

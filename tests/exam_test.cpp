// LINK: game sim world i18n core platform
// SPDX-License-Identifier: AGPL-3.0-only
// Origin: tests/game/exam.test.ts @ v1.7.7
/**
 * The theory exam (game/exam/): the question bank is whole – unique ids,
 * every question with its words, a right answer, an explanation and a rule
 * – an exam is 20 + 10 questions worth 110 points, it is scored as the real
 * one (more than 10 error points, or two wrong 5-point questions: failed),
 * typed numbers are read as people type them, and the clock runs down.
 */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include "core/js.h"
#include "game/exam/danger.h"
#include "game/exam/exam.h"
#include "game/exam/questions.h"
#include "game/theory.h"
#include "i18n/exam/index.h"

using namespace fs;

static const std::regex REF(R"(§ \d+[a-z]? |Anlage \d+ |Zeichen \d|Verordnung|StGB|StVG|FeV|StVZO)");

static bool has(const std::vector<std::string>& list, const std::string& x) { return std::find(list.begin(), list.end(), x) != list.end(); }
static bool isClassQ(const ExamQuestion* q) { return has(CLASS_TOPICS, q->topic); }

static void questionBank() {
  assert(QUESTIONS.size() >= 150);
  std::set<std::string> ids;
  for (const ExamQuestion& q : QUESTIONS) {
    assert(!ids.count(q.id));
    ids.insert(q.id);
    assert(has(EXAM_TOPICS, q.topic));
    assert(q.points >= 2 && q.points <= 5);
    const ExamText* w = EXAM_EN.questions.find(q.id);
    assert(w);
    assert(trim(w->q).size() > 10);
    assert(trim(w->why).size() > 10);
    assert(std::regex_search(q.ref, REF));
    if (q.number) {
      assert(q.answers == 0);
      assert(std::isfinite(q.number->value));
      assert(w->a.empty());
    } else {
      assert(q.answers >= 2 && q.answers <= 4);
      assert(static_cast<int>(w->a.size()) == q.answers);
      assert(!q.correct.empty());
      for (int i : q.correct) assert(i >= 0 && i < q.answers);
      assert(std::set<std::string>(w->a.begin(), w->a.end()).size() == w->a.size());
      for (const std::string& a : w->a) assert(!trim(a).empty());
    }
  }
  for (const std::string& id : EXAM_EN.questions.keys()) assert(ids.count(id));
  // every topic has questions, the class part enough for two exams
  for (const ExamTopic& t : EXAM_TOPICS) assert(std::count_if(QUESTIONS.begin(), QUESTIONS.end(), [&t](const ExamQuestion& q) { return q.topic == t; }) >= 8);
  assert(std::count_if(QUESTIONS.begin(), QUESTIONS.end(), [](const ExamQuestion& q) { return has(CLASS_TOPICS, q.topic); }) >= 2 * EXAM_CLASS);
  // pictures: signs the trainer knows, scenes and questions that exist
  for (const ExamQuestion& q : QUESTIONS) {
    if (!q.picture) continue;
    const ExamPicture& p = *q.picture;
    if (p.sign) assert(EXAM_EN.signs.find(*p.sign));
    else {
      const theory::LessonDef* l = theory::theoryLesson(p.scene);
      assert(l && p.step >= 0 && p.step < static_cast<int>(l->steps.size()));
    }
  }
}

static void ownWords() {
  // (the official catalogue numbers its questions like "1.1.02-001"; none of that here)
  std::ifstream f("web/lang/exam/en.json");
  assert(f);
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string text = ss.str();
  assert(text.size() > 1000);
  assert(!std::regex_search(text, std::regex(R"(\d\.\d\.\d{2}-\d{3})")));
}

static void exams() {
  for (uint32_t seed = 1; seed <= 300; seed++) {
    const std::vector<const ExamQuestion*> qs = pickExam(QUESTIONS, exam::seeded(seed));
    assert(qs.size() == static_cast<size_t>(EXAM_BASIC + EXAM_CLASS));
    std::set<std::string> ids;
    int points = 0;
    for (const ExamQuestion* q : qs) {
      ids.insert(q->id);
      points += q->points;
    }
    assert(ids.size() == qs.size());
    if (points != EXAM_MAX) std::printf("seed %u: %d points\n", seed, points);
    assert(points == EXAM_MAX);
    assert(std::count_if(qs.begin(), qs.end(), isClassQ) == EXAM_CLASS);
    // the basic part first
    for (int i = 0; i < EXAM_BASIC; i++) assert(!isClassQ(qs[static_cast<size_t>(i)]));
    // every basic topic at least once
    for (const ExamTopic& t : EXAM_TOPICS)
      if (!has(CLASS_TOPICS, t)) assert(std::any_of(qs.begin(), qs.end(), [&t](const ExamQuestion* q) { return q->topic == t; }));
  }
  // the same seed, the same exam
  assert(pickExam(QUESTIONS, exam::seeded(7)) == pickExam(QUESTIONS, exam::seeded(7)));
}

static ExamQuestion Q(const std::string& id, int points, std::vector<int> correct = {0}) {
  return ExamQuestion{.id = id, .topic = "danger", .points = points, .answers = 3, .correct = std::move(correct), .number = std::nullopt, .ref = "§ 1 StVO", .picture = std::nullopt};
}

static void scoring() {
  const ExamQuestion multi = Q("m", 4, {0, 2});
  const auto right = [&multi](ExamAnswer a) { return isRight(multi, &a); };
  assert(right(std::vector<int>{2, 0}));
  assert(!right(std::vector<int>{0}));        // one right answer missing
  assert(!right(std::vector<int>{0, 1, 2}));  // one wrong answer too many
  assert(!right(std::vector<int>{}));
  assert(!isRight(multi, nullptr));
  assert(!right(std::string("0")));

  const std::vector<ExamQuestion> bank = {Q("a", 5), Q("b", 5), Q("c", 4), Q("d", 3), Q("e", 3), Q("f", 2)};
  std::vector<const ExamQuestion*> qs;
  for (const ExamQuestion& q : bank) qs.push_back(&q);
  ExamAnswers all;
  for (const ExamQuestion& q : bank) all[q.id] = std::vector<int>{0};
  const auto with = [&all](std::initializer_list<std::pair<const char*, std::optional<std::vector<int>>>> changes) {
    ExamAnswers a = all;
    for (const auto& [id, v] : changes) {
      if (v) a[id] = *v;
      else a.erase(id);
    }
    return a;
  };
  ExamScore s = scoreExam(qs, all);
  assert(s.points == 22 && s.max == 22 && s.errors == 0 && s.passed);
  // 10 error points without two 5-pointers: passed
  s = scoreExam(qs, with({{"a", std::vector<int>{1}}, {"d", std::vector<int>{1}}, {"f", std::vector<int>{1}}}));
  assert(s.errors == 10 && s.wrongFive == 1 && s.passed);
  assert((s.wrong == std::vector<std::string>{"a", "d", "f"}));
  // 11: failed
  s = scoreExam(qs, with({{"c", std::vector<int>{1}}, {"d", std::vector<int>{1}}, {"e", std::vector<int>{1}}, {"f", std::nullopt}}));
  assert(s.errors == 12 && !s.passed);
  // two wrong 5-point questions: 10 error points – and failed
  s = scoreExam(qs, with({{"a", std::vector<int>{1}}, {"b", std::vector<int>{}}}));
  assert(s.errors == 10 && s.wrongFive == 2 && !s.passed);
  // nothing answered (the time ran out)
  s = scoreExam(qs, {});
  assert(s.points == 0 && s.errors == 22 && !s.passed);
}

static void numbers() {
  assert(typedNumber("1,6") == 1.6);
  assert(typedNumber(" 1.6 mm") == 1.6);
  assert(typedNumber("50 km/h") == 50);
  assert(typedNumber("3.500") == 3500);
  assert(typedNumber("3500") == 3500);
  assert(typedNumber("0,5 ‰") == 0.5);
  assert(std::isnan(typedNumber("fifty")));
  const ExamQuestion q{.id = "n", .topic = "speed", .points = 3, .answers = 0, .correct = {}, .number = ExamNumber{1.6, "mm"}, .ref = "§ 36 StVZO", .picture = std::nullopt};
  const auto right = [&q](ExamAnswer a) { return isRight(q, &a); };
  assert(right(std::string("1,6")));
  assert(right(std::string("1.60")));
  assert(!right(std::string("16")));
  assert(!right(std::vector<int>{0}));
  // every number question of the bank is right with its own value typed, German style
  for (const ExamQuestion& x : QUESTIONS) {
    if (!x.number) continue;
    ExamAnswer a = replaceAll(jsStr(x.number->value), ".", ",");
    assert(isRight(x, &a));
  }
}

static void countdown() {
  const double t0 = 1'000'000;
  assert(timeLeft(t0, t0) == 45 * 60'000);
  assert(examClockText(timeLeft(t0, t0)) == "45:00");
  assert(examClockText(timeLeft(t0, t0 + 1)) == "45:00");
  assert(examClockText(timeLeft(t0, t0 + 1000)) == "44:59");
  assert(examClockText(timeLeft(t0, t0 + 44 * 60'000 + 50'500)) == "0:10");
  assert(timeLeft(t0, t0 + 46 * 60'000) == 0);
  assert(examClockText(0) == "0:00");
}

static void danger() {
  std::set<std::string> ids;
  for (const DangerItem& d : DANGER_ITEMS) ids.insert(d.id);
  assert(ids.size() == DANGER_ITEMS.size());
  for (const DangerItem& d : DANGER_ITEMS) {
    const theory::LessonDef* l = theory::theoryLesson(d.scene);
    assert(l);
    assert(d.step >= 0 && d.step < static_cast<int>(l->steps.size()));
    const double t = l->steps[static_cast<size_t>(d.step)].at + d.off;
    assert(t >= 0 && t <= l->end);
    assert(d.answers >= 2 && d.answers <= 4);
    assert(!d.correct.empty());
    for (int i : d.correct) assert(i >= 0 && i < d.answers);
    assert(std::regex_search(d.ref, REF));
    const ExamText* w = EXAM_EN.danger.find(d.id);
    assert(w && !w->q.empty() && !w->why.empty() && static_cast<int>(w->a.size()) == d.answers);
    assert(std::set<std::string>(w->a.begin(), w->a.end()).size() == w->a.size());
  }
  // (a scene added later needs an item: listed, so that it is not forgotten)
  std::vector<std::string> missing;
  for (const theory::LessonDef& l : theory::THEORY())
    if (std::none_of(DANGER_ITEMS.begin(), DANGER_ITEMS.end(), [&l](const DangerItem& d) { return d.scene == l.id; })) missing.push_back(l.id);
  for (const std::string& id : missing) std::printf("theory scene without a \"spot the danger\" item – add one to game/exam/danger.h: %s\n", id.c_str());
  assert(missing.empty());
}

static void fallback() {
  const std::string id = QUESTIONS[0].id;
  const ExamText& en = EXAM_EN.questions.at(id);
  ExamTexts own;
  own.questions.set(id, ExamText{"Frage?", {"Eins"}, ""});
  own.signs.set("205", "Vorfahrt gewähren");
  const ExamTexts t = withFallback(&own);
  assert(t.questions.at(id).q == "Frage?");
  assert(t.questions.at(id).a[0] == "Eins");
  assert(t.questions.at(id).a[1] == en.a[1]);
  assert(t.questions.at(id).why == en.why);
  assert(t.signs.at("205") == "Vorfahrt gewähren");
  assert(t.signs.at("206") == EXAM_EN.signs.at("206"));
  assert(t.danger.size() == EXAM_EN.danger.size());
}

int main() {
  questionBank();
  ownWords();
  exams();
  scoring();
  numbers();
  countdown();
  danger();
  fallback();
  std::printf("exam_test: ok\n");
  return 0;
}

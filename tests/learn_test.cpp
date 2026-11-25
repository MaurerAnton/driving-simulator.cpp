// LINK: game ui render engine world sim i18n core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
// Origin: tests/game/learn.test.ts @ v1.7.7
/**
 * The learning modes besides the exam (game/exam/): the sign trainer – every
 * sign drawable and named, four different choices, the boxes of spaced
 * repetition –, what is kept (merged from two devices) and the lesson log
 * gathered from the stores.
 *
 * (The original also checks that net/sync.ts syncs it as the progress kind "learn": the
 * accounts and their syncing are not in this version.)
 */
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include "game/exam/exam.h"
#include "game/exam/learnlog.h"
#include "game/exam/learnstore.h"
#include "game/exam/signs.h"
#include "i18n/exam/index.h"
#include "platform/json.h"
#include "render/signart.h"

using namespace fs;

/** a store of its own (tests/game/learn.test.ts memory()) */
class Memory : public plat::Storage {
 public:
  std::map<std::string, std::string> data;
  std::optional<std::string> getItem(const std::string& k) override {
    auto it = data.find(k);
    return it == data.end() ? std::nullopt : std::optional<std::string>(it->second);
  }
  bool setItem(const std::string& k, const std::string& v) override {
    data[k] = v;
    return true;
  }
  void removeItem(const std::string& k) override { data.erase(k); }
};

static bool has(const std::vector<std::string>& list, const std::string& x) { return std::find(list.begin(), list.end(), x) != list.end(); }

static void trainerSigns() {
  std::ifstream f("web/signs/manifest.json");
  assert(f);
  std::stringstream ss;
  ss << f.rdbuf();
  Json manifest;
  assert(Json::parse(ss.str(), manifest) && manifest.isObject());
  assert(TRAINER_SIGNS.size() >= 150);
  assert(std::set<std::string>(TRAINER_SIGNS.begin(), TRAINER_SIGNS.end()).size() == TRAINER_SIGNS.size());
  for (const std::string& c : TRAINER_SIGNS) {
    if (ART_SIGNS.count(c)) {
      assert(!manifest.has(c));            // has a graphic – not to be drawn by signart
      assert(artAspect(c).has_value());   // render/signart.ts can draw it
    } else assert(manifest.has(c));        // a graphic in public/signs/
    const std::string* m = EXAM_EN.signs.find(c);
    assert(m && m->size() > 2);
  }
  for (const std::string& c : ART_SIGNS) assert(has(TRAINER_SIGNS, c));
  // every graphic the game has is trained
  for (const auto& [c, v] : manifest.object) assert(has(TRAINER_SIGNS, c));
  std::set<std::string> meanings;
  for (const std::string& c : TRAINER_SIGNS) meanings.insert(EXAM_EN.signs.at(c));
  assert(meanings.size() == TRAINER_SIGNS.size());
}

static void choices() {
  const Rand rand = exam::seeded(3);
  const std::function<std::string(const std::string&)> meaning = [](const std::string& c) { return EXAM_EN.signs.at(c); };
  const std::function<std::string(const std::string&)> number = [](const std::string& c) { return c; };
  for (const std::string& code : TRAINER_SIGNS) {
    for (const auto* label : {&meaning, &number}) {
      const std::vector<std::string> ch = signChoices(code, rand, *label);
      assert(ch.size() == 4);
      assert(has(ch, code));
      std::set<std::string> labels;
      for (const std::string& c : ch) labels.insert((*label)(c));
      assert(labels.size() == 4);
    }
    const std::string fam = signFamily(code);
    if (std::count_if(TRAINER_SIGNS.begin(), TRAINER_SIGNS.end(), [&fam](const std::string& c) { return signFamily(c) == fam; }) >= 4)
      for (const std::string& c : signChoices(code, rand, meaning)) assert(signFamily(c) == fam);
  }
  assert(signFamily("274-50") == "speed");
  assert(signFamily("101") == "danger");
  assert(signFamily("1020-30") == "panel");
  assert(signFamily("306") == "info");
  assert(signFamily("205") == "rule");
}

static void spacedRepetition() {
  const double t0 = 1'000'000'000;
  SignStat s = review(nullptr, true, t0);
  assert(s.box == 1 && s.due == t0 + BOX_WAIT[1] && s.right == 1 && s.wrong == 0);
  s = review(&s, true, t0 + 1);
  assert(s.box == 2);
  for (int i = 0; i < 10; i++) s = review(&s, true, t0 + 2);
  assert(s.box == 5);  // the top box at most
  s = review(&s, false, t0 + 3);
  assert(s.box == 0 && s.due == t0 + 3 && s.wrong == 1);

  const std::vector<std::string> codes = {"a", "b", "c", "d"};
  Record<SignStat> stats;
  // nothing seen: the first new one
  assert(nextSign(stats, t0, "", codes) == "a");
  stats.set("a", review(nullptr, true, t0));
  // a learned, not due: the next new one
  assert(nextSign(stats, t0 + 1, "", codes) == "b");
  stats.set("b", review(nullptr, false, t0));
  // b wrong, due at once – before any new one; but not twice in a row
  assert(nextSign(stats, t0 + 1, "", codes) == "b");
  assert(nextSign(stats, t0 + 1, "b", codes) == "c");
  // all seen, none due: the one due soonest
  const SignStat c1 = review(nullptr, true, t0);
  stats.set("c", review(&c1, true, t0));
  stats.set("d", review(nullptr, true, t0 + 10));
  stats.set("b", review(stats.find("b"), true, t0 + 5));
  assert(nextSign(stats, t0 + 6, "", codes) == "a");
  // due: the lower box first
  assert(nextSign(stats, t0 + 10 * 60'000, "", codes) == "a");
  assert((signProgress(stats, t0 + 6, codes) == SignProgress{0, 4, 0, 4}));
}

static ExamRecord examRec(double t, bool passed = true) { return ExamRecord{t, 104, 110, 6, passed, {"dan01"}, 900}; }

static void store() {
  Memory m;
  noteLessonEnd("bParallel", true, 0, {{"parallel", 2, false}}, 1000, &m);
  noteLessonEnd("bParallel", false, 12, {{"parallel", 0, true}}, 2000, &m);
  noteLessonEnd("rbl", true, 3, {}, 3000, &m);
  LearnData d = loadLearn(&m);
  assert((d.lessons.at("bParallel") == LessonRecord{2, 1, 2, 2000, 1000}));
  assert((d.drills.at("parallel") == LessonRecord{2, 1, 2, 2000, 1000}));
  assert(d.lessons.at("rbl").stars == lessonStars(true, 3));
  assert(lessonStars(true, 0) == 3 && lessonStars(true, 4) == 2 && lessonStars(true, 9) == 1 && lessonStars(false, 0) == 0);
  addExam(d, examRec(5));
  addExam(d, examRec(9, false));
  addExam(d, examRec(5));
  assert(d.exams.size() == 2 && d.exams[0].t == 9 && d.exams[1].t == 5);
  m.setItem(LEARN_KEY, R"({"exams":[{"t":"x"},null,7],"signs":{"205":{"box":99,"due":"no"}},"lessons":[1,2]})");
  const LearnData g = loadLearn(&m);
  assert(g.signs.at("205").box == 5);
  assert(g.signs.at("205").due == 0);
  assert(g.lessons.empty());
  assert(g.exams.size() == 1);
  m.setItem(LEARN_KEY, "not json");
  assert(loadLearn(&m) == emptyLearn());
  // kept as the original keeps it (JSON.stringify, the same order of the members)
  LearnData one;
  addExam(one, examRec(5));
  noteTry(one.lessons, "rbl", true, 3, 10);
  saveLearn(one, &m);
  assert(m.data[LEARN_KEY] ==
         R"({"exams":[{"t":5,"points":104,"max":110,"errors":6,"passed":true,"wrong":["dan01"],"secs":900}],"signs":{},"danger":{},"lessons":{"rbl":{"tries":1,"passes":1,"stars":3,"last":10,"passedAt":10}},"drills":{}})");
}

static void twoDevices() {
  LearnData a = emptyLearn();
  LearnData b = emptyLearn();
  addExam(a, ExamRecord{1, 100, 110, 10, true, {}, 1});
  addExam(b, ExamRecord{2, 90, 110, 20, false, {}, 1});
  addExam(b, ExamRecord{1, 100, 110, 10, true, {}, 1});
  a.signs.set("205", SignStat{1, 5, 1, 0, 5});
  b.signs.set("205", SignStat{3, 9, 3, 1, 4});
  b.signs.set("206", SignStat{0, 1, 0, 1, 1});
  noteTry(a.lessons, "rbl", true, 3, 10);
  noteTry(b.lessons, "rbl", false, 0, 20);
  noteTry(b.lessons, "rbl", false, 0, 21);
  const LearnData m = mergeLearn(learnJson(a), learnJson(b));
  assert(m.exams.size() == 2 && m.exams[0].t == 2 && m.exams[1].t == 1);
  assert(m.signs.at("205").box == 3);
  assert(m.signs.at("206").wrong == 1);
  assert((m.lessons.at("rbl") == LessonRecord{2, 1, 3, 21, 10}));
  // the same either way round, and nothing lost merging with nothing
  assert(mergeLearn(learnJson(b), learnJson(a)).signs == m.signs);
  assert(mergeLearn(learnJson(a), Json()) == cleanLearn(learnJson(a)));
}

static void lessonLog() {
  LearnData d = emptyLearn();
  noteTry(d.lessons, "rbl", true, 2, 5000);
  noteTry(d.lessons, "priority", false, 0, 6000);
  noteTry(d.drills, "parallel", true, 3, 7000);
  d.danger.set("zebra", DangerStat{1, 1, true, 8000});
  d.danger.set("rbl", DangerStat{0, 1, false, 8100});
  addExam(d, ExamRecord{9000, 106, 110, 4, true, {}, 600});
  addExam(d, ExamRecord{9500, 80, 110, 30, false, {}, 600});
  d.signs.set("205", SignStat{3, 1e12, 3, 0, 1});
  d.signs.set("206", SignStat{0, 0, 0, 1, 1});
  LogInput in;
  in.learn = d;
  in.lessons = {"basics", "rbl", "priority", "signals"};
  in.drills = {"parallel", "turn"};
  in.theory = {"rbl", "zebra"};
  in.theoryDone = {"zebra", "gone"};
  in.danger = {"rbl", "zebra", "tram"};
  in.faults = {{"signals", 4000}, {"free", 4500}};
  in.tours = {{"2026-11-01T10:00:00Z", true}, {"bad", false}};
  in.now = 10'000;
  const LearnLog log = learnLog(in);
  const auto sec = [&log](const std::string& id) -> const LogSection& { return *std::find_if(log.sections.begin(), log.sections.end(), [&id](const LogSection& s) { return s.id == id; }); };
  const auto row = [&sec](const std::string& s, const std::string& id) -> const LogRow& {
    const std::vector<LogRow>& rows = sec(s).rows;
    return *std::find_if(rows.begin(), rows.end(), [&id](const LogRow& r) { return r.id == id; });
  };
  const auto states = [&sec](const std::string& s) {
    std::vector<std::string> out;
    for (const LogRow& r : sec(s).rows) out.push_back(r.state);
    return out;
  };
  assert((states("lessons") == std::vector<std::string>{"none", "passed", "tried", "tried"}));
  assert(row("lessons", "rbl").stars == 2.0);
  assert(row("lessons", "rbl").date == 5000);
  assert(row("lessons", "signals").date == 4000);  // tried: a fault was logged in it
  assert(row("drills", "parallel").stars == 3.0);
  assert(row("drills", "turn").state == "none");
  assert((states("theory") == std::vector<std::string>{"none", "passed"}));
  assert((states("danger") == std::vector<std::string>{"tried", "passed", "none"}));
  assert(sec("lessons").passed == 1 && sec("lessons").total == 4);
  assert((log.exams == LearnLog::Exams{2, 1, 106, 110, 9500}));
  assert(log.signs.learned == 1);
  assert(log.signs.seen == 2);
  assert(log.signs.due == 1);
  assert((log.tours == LearnLog::Tours{2, 1, 1793527200000.0}));
}

int main() {
  trainerSigns();
  choices();
  spacedRepetition();
  store();
  twoDevices();
  lessonLog();
  std::printf("learn_test: ok\n");
  return 0;
}

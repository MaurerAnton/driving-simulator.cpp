// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The learning modes on the page (ui/learn.ts), driven as a player would: a whole theory exam
 * answered right and handed in, one whose time runs out, one left in the middle; a sign of the
 * trainer and a "spot the danger" item answered; the lesson log and the overview – and what each
 * leaves in the learning store (game/exam/learnstore.ts).
 * (The original has no test of its own for ui/learn.ts.)
 */
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>

#include "core/js.h"
#include "game/exam/danger.h"
#include "game/exam/learnstore.h"
#include "game/exam/questions.h"
#include "game/nav.h"
#include "i18n/exam/index.h"
#include "i18n/index.h"
#include "platform/dom.h"
#include "platform/native/dom_native.h"
#include "platform/native/native.h"
#include "ui/learn.h"

using namespace fs;
using dom::El;
namespace dn = fs::dom::native;

static int backs = 0;

static void open(const std::string& screen) {
  LearnContext c;
  c.t = [](const std::string& k, const TrParams& v) { return tr(k, "en", v); };
  c.lang = "en";
  c.screen = screen;
  c.show = [](const El& page) { dom::body().replaceChildren(page); };
  c.back = [] { backs++; };
  c.signBase = "signs/";
  plat::native::wait(openLearn(c));
}

static El q(const std::string& selector) { return dom::querySelector(selector); }
static void click(const El& e) {
  assert(e);
  dn::dispatch(e, "click");
}

/** the question on the page, found by its words */
static const ExamQuestion& shown() {
  const std::string text = dn::text(q(".lr-q .lr-text"));
  for (const ExamQuestion& x : QUESTIONS)
    if (EXAM_EN.questions.at(x.id).q == text) return x;
  assert(false);
  return QUESTIONS[0];
}

static void wholeExam() {
  open("exam");
  assert(dn::text(q(".lr-head h1")) == tr("learn.exam", "en"));
  assert(q(".lr-list") && dn::text(q(".lr-panel")).find(tr("learn.examNone", "en")) != std::string::npos);
  click(q("[data-k=exam-start]"));
  for (int n = 1; n <= 30; n++) {
    assert(dn::text(q(".lr-count")) == tr("learn.qOf", "en", {{"n", n}, {"count", 30}}));
    assert(dn::text(q(".lr-clock")).find("45:00") != std::string::npos);
    const ExamQuestion& x = shown();
    const ExamText& w = EXAM_EN.questions.at(x.id);
    if (x.number) dn::type(q(".lr-number input"), replaceAll(jsStr(x.number->value), ".", ","));
    else
      for (int i : x.correct) {
        El b = dn::find(q(".lr-answers"), w.a[static_cast<size_t>(i)], "button");
        click(b);
        assert(b.getAttribute("aria-pressed") == "true");
      }
    // the overview shows it answered
    assert(dom::document().querySelectorAll(".lr-grid .lr-cell.done").size() == static_cast<size_t>(n));
    click(q(".lr-nav").lastElementChild());
  }
  // handed in: all right, passed, kept
  assert(q(".lr-verdict.ok"));
  assert(dn::text(q(".lr-panel")).find(tr("learn.allRight", "en")) != std::string::npos);
  const LearnData d = loadLearn();
  assert(d.exams.size() == 1 && d.exams[0].passed && d.exams[0].points == 110 && d.exams[0].errors == 0 && d.exams[0].wrong.empty());
  assert(plat::native::confirmed.empty());
}

static void timeUp() {
  open("exam");
  click(q("[data-k=exam-start]"));
  // one answer ticked (maybe wrong), then the time runs out
  click(q(".lr-answers button"));
  plat::native::advance(30 * 60'000);
  plat::native::drain();
  assert(dn::text(q(".lr-clock")).find("15:00") != std::string::npos);
  plat::native::advance(15 * 60'000);
  plat::native::drain();
  assert(q(".lr-verdict.bad"));
  assert(dn::text(q(".lr-panel")).find(tr("learn.timeUp", "en")) != std::string::npos);
  // the mistakes explained: one box each, with the rule linked
  const size_t wrong = dom::document().querySelectorAll(".lr-review").size();
  assert(wrong >= 29);
  assert(q(".lr-review .lr-why .lr-ref"));
  const LearnData d = loadLearn();
  assert(d.exams.size() == 2 && !d.exams[0].passed && d.exams[0].wrong.size() == wrong && d.exams[0].secs == 45 * 60);
  // the clock stopped with it
  assert(plat::native::pending() == 0);
  // the list of exams on the first page of the exam
  click(dn::find(q(".lr-panel .buttons"), tr("learn.examHistory", "en"), "button"));
  assert(dom::document().querySelectorAll(".lr-list .lr-row").size() == 2);
}

static void leaveExam() {
  open("exam");
  click(q("[data-k=exam-start]"));
  plat::native::confirmAnswer = false;
  click(q(".lr-back"));
  assert(plat::native::confirmed.size() == 1);
  assert(q(".lr-count"));  // still there
  plat::native::confirmAnswer = true;
  click(q(".lr-back"));
  assert(q(".lr-tiles"));  // the overview: not counted
  assert(loadLearn().exams.size() == 2);
  // (the clock's last call comes and finds it stopped)
  plat::native::advance(1000);
  plat::native::drain();
  assert(plat::native::pending() == 0);
  // from the overview back to the menu
  click(q(".lr-back"));
  assert(backs == 1);
}

static void signTrainer() {
  open("signs");
  assert(dom::document().querySelectorAll(".lr-answers button").size() == 4);
  click(q(".lr-answers button"));
  assert(q(".lr-answers button.right"));
  for (const El& b : dom::document().querySelectorAll(".lr-answers button")) assert(b.disabled());
  const LearnData d = loadLearn();
  assert(d.signs.size() == 1);
  click(q(".lr-after .btn"));
  assert(!q(".lr-answers button.right"));
  // the page of statistics: every sign of the trainer
  click(dn::find(q(".lr-chips"), tr("learn.stats", "en"), "button"));
  assert(dom::document().querySelectorAll(".lr-signrow").size() > 150);
}

static void spotTheDanger() {
  open("danger");
  click(q(".lr-answers button"));
  click(q(".btn.lr-big"));
  assert(q(".lr-after .lr-why"));
  const LearnData d = loadLearn();
  assert(d.danger.size() == 1 && d.danger.has(DANGER_ITEMS[0].id));
  click(q(".lr-after .btn"));
  // the next one is another
  assert(dn::text(q(".lr-text")) != EXAM_EN.danger.at(DANGER_ITEMS[0].id).q);
}

static void lessonLog() {
  noteLessonEnd(LESSONS[1].id, true, 0);
  open("log");
  assert(dom::document().querySelectorAll("details.lr-sec").size() == 5);
  assert(dom::document().querySelectorAll("details.lr-sec[open]").size() == 2);
  assert(dom::document().querySelectorAll("details.lr-sec")[0].querySelectorAll(".lr-row").size() == LESSONS.size());
  assert(dn::text(q("details.lr-sec")).find("★★★") != std::string::npos);
  assert(dom::document().querySelectorAll(".lr-fact").size() == 3);
  click(q("button.lr-fact"));
  assert(q("[data-k=exam-start]"));
  open("hub");
  assert(dom::document().querySelectorAll(".lr-tiles .lr-tile").size() == 4);
  click(q("[data-k=learn-signs]"));
  assert(q(".lr-chips"));
}

int main() {
  plat::native::manualClock(true);
  plat::localStorage().removeItem(LEARN_KEY);
  dn::reset();
  wholeExam();
  timeUp();
  leaveExam();
  signTrainer();
  spotTheDanger();
  lessonLog();
  std::printf("learn_ui_test: ok\n");
  return 0;
}

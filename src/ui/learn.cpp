// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/learn.ts @ v1.7.7
#include "ui/learn.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <numeric>
#include <set>

#include "core/js.h"
#include "core/random.h"
#include "game/exam/danger.h"
#include "game/exam/exam.h"
#include "game/exam/learnlog.h"
#include "game/exam/learnstore.h"
#include "game/exam/questions.h"
#include "game/exam/signs.h"
#include "game/faultlog.h"
#include "game/nav.h"
#include "game/theory.h"
#include "game/tourlog.h"
#include "i18n/exam/index.h"
#include "i18n/violations.h"
#include "platform/canvas.h"
#include "platform/intl.h"
#include "platform/json.h"
#include "platform/plat.h"
#include "render/signart.h"
#include "ui/learnplan.h"

namespace fs {

namespace plat {
/** window.confirm(text): a question the player answers with OK (true) or Cancel (the hosts: platform/web, platform/native) */
bool confirm(const std::string& text);
}  // namespace plat

using dom::El;

namespace {

El button(const std::string& cls, const std::string& text, std::function<void()> act) {
  El b = dom::el("button", cls, text);
  b.type("button");
  b.onclick(std::move(act));
  return b;
}

/** The rule as text with links to the paragraphs. */
El refNode(const std::string& ref) {
  El box = dom::el("span", "lr-ref");
  for (const CitationPart& p : citationParts(ref)) {
    if (p.url) {
      El a = dom::el("a", "", p.text);
      a.href(*p.url);
      a.set("target", "_blank");
      a.set("rel", "noopener");
      box.append(a);
    } else box.append(p.text);
  }
  return box;
}

/** `'★'.repeat(n)` */
std::string repeat(const std::string& s, int n) {
  std::string out;
  for (int i = 0; i < n; i++) out += s;
  return out;
}

/** The theory exam running: what is answered, where, since when. */
struct ExamRun {
  std::vector<const ExamQuestion*> qs;
  ExamAnswers answers;
  /** the answers of each question in the order shown */
  std::map<std::string, std::vector<int>> order;
  double start = 0;
  size_t at = 0;
  bool over = false;
};

/** Its page (the elements are handed to the handlers by value: dom::collect counts what a handler holds). */
struct ExamView {
  El root, panel, count, pts, clock, body, nav, grid;
};

class LearnUi : public std::enable_shared_from_this<LearnUi> {
 public:
  LearnUi(LearnContext c, ExamTexts texts) : c_(std::move(c)), tx_(std::move(texts)) {}

  void open(const LearnScreen& s) {
    tick_ = 0;
    if (s == "exam") examIntro();
    else if (s == "signs") signs("meaning");
    else if (s == "danger") danger();
    else if (s == "log") log();
    else hub();
  }

 private:
  LearnContext c_;
  ExamTexts tx_;
  /** the clock of the exam running (0: none – a clock whose number is not this one stops) */
  int tick_ = 0;
  int ticks_ = 0;

  std::string t(const std::string& key, const TrParams& vars = {}) const { return c_.t(key, vars); }

  std::string date(double ms) const { return ms > 0 ? plat::formatDate(ms, localeOf(c_.lang)) : ""; }

  struct Page {
    El panel;
    El root;
    El back;
  };

  /** A page: its title, a back button (to the hub, or from the hub to the menu), what is in it. */
  Page page(const std::string& title, bool toHub = true) {
    auto self = shared_from_this();
    El root = dom::el("div", "overlay solid lr");
    El panel = dom::el("div", "panel lr-panel");
    El head = dom::el("div", "lr-head");
    El back = button("lr-back", "‹", [self, toHub] {
      if (toHub) self->open("hub");
      else self->c_.back();
    });
    back.title(t("back"));
    back.setAttribute("aria-label", t("back"));
    head.append(back, dom::el("h1", "", title));
    panel.append(head);
    root.append(panel);
    return {panel, root, back};
  }

  // ------------------------------------------------------------------ the hub

  void hub() {
    auto self = shared_from_this();
    Page pg = page(t("learn.title"), false);
    pg.panel.append(dom::el("p", "", t("learn.hint")));
    const LearnData d = loadLearn();
    const SignProgress sp = signProgress(d.signs, plat::dateMs());
    int right = 0;
    for (const DangerItem& x : DANGER_ITEMS)
      if (const DangerStat* s = d.danger.find(x.id); s && s->ok) right++;
    const int examsPassed = static_cast<int>(std::count_if(d.exams.begin(), d.exams.end(), [](const ExamRecord& e) { return e.passed; }));
    struct Tile {
      LearnScreen id;
      std::string title, desc, state;
    };
    const std::vector<Tile> tiles = {
        {"exam", t("learn.exam"), t("learn.examDesc"), t("learn.examProgress", {{"passed", examsPassed}, {"taken", d.exams.size()}})},
        {"signs", t("learn.signs"), t("learn.signsDesc"), t("learn.signsProgress", {{"learned", sp.learned}, {"total", sp.total}, {"due", sp.due}})},
        {"danger", t("learn.danger"), t("learn.dangerDesc"), t("learn.dangerProgress", {{"right", right}, {"total", DANGER_ITEMS.size()}})},
        {"log", t("learn.log"), t("learn.logDesc"), ""},
    };
    El grid = dom::el("div", "lr-tiles");
    for (const Tile& tile : tiles) {
      El b = dom::el("button", "card lr-tile");
      b.type("button");
      b.dataset("k", "learn-" + tile.id);
      b.append(dom::el("b", "", tile.title), dom::el("small", "", tile.desc));
      if (!tile.state.empty()) b.append(dom::el("span", "lr-state", tile.state));
      b.onclick([self, id = tile.id] { self->open(id); });
      grid.append(b);
    }
    pg.panel.append(grid);
    c_.show(pg.root);
  }

  // ------------------------------------------------------------------ the theory exam

  void examIntro() {
    auto self = shared_from_this();
    Page pg = page(t("learn.exam"));
    pg.panel.append(dom::el("p", "", t("learn.examRules")), dom::el("p", "note", t("learn.examOwn")));
    El go = button("btn lr-big", t("learn.examStart"), [self] { self->examRun(); });
    go.dataset("k", "exam-start");
    pg.panel.append(go);
    const LearnData d = loadLearn();
    pg.panel.append(dom::el("h2", "", t("learn.examHistory")));
    if (d.exams.empty()) pg.panel.append(dom::el("p", "", t("learn.examNone")));
    El list = dom::el("div", "lr-list");
    for (size_t i = 0; i < d.exams.size() && i < 20; i++) list.append(examRow(d.exams[i]));
    pg.panel.append(list);
    c_.show(pg.root);
  }

  El examRow(const ExamRecord& e) {
    El r = dom::el("div", std::string("lr-row ") + (e.passed ? "ok" : "bad"));
    r.append(dom::el("b", "", e.passed ? t("learn.passed") : t("learn.failed")), dom::el("span", "", t("learn.examRow", {{"points", e.points}, {"max", e.max}, {"errors", e.errors}})),
             dom::el("small", "", date(e.t)));
    return r;
  }

  void examRun() {
    auto self = shared_from_this();
    auto run = std::make_shared<ExamRun>();
    const double now = plat::dateMs();
    run->qs = pickExam(QUESTIONS, exam::seeded(static_cast<uint32_t>(jsInt32(now) ^ jsInt32(std::floor(rnd() * 1e9)))));
    const Rand rand = exam::seeded(jsUint32(now));
    for (const ExamQuestion* q : run->qs) {
      std::vector<int> keys(static_cast<size_t>(q->answers));
      std::iota(keys.begin(), keys.end(), 0);
      run->order[q->id] = shuffle(keys, rand);
    }
    run->start = plat::dateMs();
    Page pg = page(t("learn.exam"));
    pg.root.addClass("lr-exam");
    ExamView v;
    v.root = pg.root;
    v.panel = pg.panel;
    // (leaving in the middle: asked first – the exam is not counted)
    pg.back.onclick([self, run] {
      if (!run->over && !plat::confirm(self->t("learn.leaveExam"))) return;
      run->over = true;
      self->open("hub");
    });
    El bar = dom::el("div", "lr-bar");
    v.count = dom::el("span", "lr-count");
    v.pts = dom::el("span", "lr-pts");
    v.clock = dom::el("span", "lr-clock");
    v.clock.setAttribute("role", "timer");
    v.clock.title(t("learn.clock"));
    bar.append(v.count, v.pts, v.clock);
    v.body = dom::el("div", "lr-q");
    v.nav = dom::el("div", "lr-nav");
    v.grid = dom::el("div", "lr-grid");
    v.grid.hidden(true);
    v.panel.append(bar, v.body, v.nav, v.grid);

    examDraw(run, v);
    examShowClock(run, v);
    tick_ = ++ticks_;
    examTick(run, v, tick_);
    c_.show(v.root);
  }

  /** window.setInterval(…, 1000) of the clock */
  void examTick(const std::shared_ptr<ExamRun>& run, const ExamView& v, int id) {
    auto self = shared_from_this();
    plat::setTimeout(
        [self, run, v, id] {
          if (self->tick_ != id) return;
          // (the page left another way – the menu, the language: the clock stops)
          if (!v.root.isConnected()) {
            self->tick_ = 0;
            return;
          }
          self->examShowClock(run, v);
          if (self->tick_ == id) self->examTick(run, v, id);
        },
        1000);
  }

  void examFinish(const std::shared_ptr<ExamRun>& run, bool timeUp) {
    if (run->over) return;
    run->over = true;
    tick_ = 0;
    const ExamScore s = scoreExam(run->qs, run->answers);
    const double now = plat::dateMs();
    const ExamRecord rec{now, static_cast<double>(s.points), static_cast<double>(s.max), static_cast<double>(s.errors), s.passed, s.wrong, jsRound((now - run->start) / 1000)};
    updateLearn([&rec](LearnData& d) { addExam(d, rec); });
    examResult(run, s, timeUp);
  }

  void examShowClock(const std::shared_ptr<ExamRun>& run, const ExamView& v) {
    const double left = timeLeft(run->start, plat::dateMs());
    v.clock.textContent("⏱ " + examClockText(left));
    v.clock.toggleClass("low", left < 5 * 60'000);
    if (left <= 0) examFinish(run, true);
  }

  void examDraw(const std::shared_ptr<ExamRun>& run, const ExamView& v) {
    auto self = shared_from_this();
    const ExamQuestion& q = *run->qs[run->at];
    const size_t n = run->qs.size();
    v.count.textContent(t("learn.qOf", {{"n", run->at + 1}, {"count", n}}));
    v.pts.textContent(t("learn.points", {{"n", q.points}}));
    v.body.replaceChildren(questionNodes(q, run->order[q.id], run, [self, run, v] { self->examDrawGrid(run, v); }, false));
    El prev = button("btn ghost", "‹ " + t("learn.prev"), [self, run, v] { self->examGo(run, v, static_cast<int>(run->at) - 1); });
    El overview = button("btn ghost", t("learn.overview"), [grid = v.grid] { grid.hidden(!grid.hidden()); });
    El last = run->at < n - 1 ? button("btn", t("learn.next") + " ›", [self, run, v] { self->examGo(run, v, static_cast<int>(run->at) + 1); })
                              : button("btn", t("learn.handIn"), [self, run] { self->examHandIn(run); });
    v.nav.replaceChildren(prev, overview, last);
    prev.disabled(run->at == 0);
    examDrawGrid(run, v);
    v.panel.scrollTop(0);
  }

  void examDrawGrid(const std::shared_ptr<ExamRun>& run, const ExamView& v) {
    auto self = shared_from_this();
    std::vector<El> cells;
    const size_t n = run->qs.size();
    for (size_t i = 0; i < n; i++) {
      const ExamQuestion* q = run->qs[i];
      El b = button(std::string("lr-cell") + (answered(answerOf(run->answers, q->id)) ? " done" : "") + (i == run->at ? " on" : ""), jsStr(static_cast<double>(i + 1)), [self, run, v, i] {
        v.grid.hidden(true);
        self->examGo(run, v, static_cast<int>(i));
      });
      b.setAttribute("aria-label", t("learn.qOf", {{"n", i + 1}, {"count", n}}));
      cells.push_back(b);
    }
    cells.push_back(button("btn lr-handin", t("learn.handIn"), [self, run] { self->examHandIn(run); }));
    v.grid.replaceChildren(cells);
  }

  void examGo(const std::shared_ptr<ExamRun>& run, const ExamView& v, int i) {
    run->at = static_cast<size_t>(std::max(0, std::min(static_cast<int>(run->qs.size()) - 1, i)));
    examDraw(run, v);
  }

  void examHandIn(const std::shared_ptr<ExamRun>& run) {
    int open = 0;
    for (const ExamQuestion* q : run->qs)
      if (!answered(answerOf(run->answers, q->id))) open++;
    if (open > 0 && !plat::confirm(t("learn.handInOpen", {{"n", open}}))) return;
    examFinish(run, false);
  }

  /** A question: its picture, its text, its answers to tick (in `order`) or a field for the number.  (`review`: shown after the exam) */
  std::vector<El> questionNodes(const ExamQuestion& q, const std::vector<int>& order, const std::shared_ptr<ExamRun>& run, std::function<void()> changed, bool review) {
    const ExamText* found = tx_.questions.find(q.id);
    const ExamText w = found ? *found : ExamText{};
    std::vector<El> out;
    if (q.picture) out.push_back(picture(*q.picture));
    out.push_back(dom::el("p", "lr-text", w.q));
    const std::string id = q.id;
    if (q.number) {
      El row = dom::el("label", "lr-number");
      row.append(dom::el("span", "", t("learn.typeNumber")));
      El input = dom::el("input");
      input.type("text");
      input.set("inputMode", "decimal");
      input.set("autocomplete", "off");
      const ExamAnswer* a = answerOf(run->answers, id);
      input.value(a && std::holds_alternative<std::string>(*a) ? std::get<std::string>(*a) : "");
      input.disabled(review);
      input.oninput([input, run, id, changed] {
        run->answers[id] = input.value();
        changed();
      });
      row.append(input);
      if (!q.number->unit.empty()) row.append(dom::el("span", "lr-unit", q.number->unit));
      out.push_back(row);
      return out;
    }
    if (q.correct.size() > 1 || !review) out.push_back(dom::el("small", "lr-multi", t("learn.multi")));
    El list = dom::el("div", "lr-answers");
    for (const int i : order) {
      const auto ticked = [run, id, i] {
        const ExamAnswer* a = answerOf(run->answers, id);
        if (!a || !std::holds_alternative<std::vector<int>>(*a)) return false;
        const std::vector<int>& v = std::get<std::vector<int>>(*a);
        return std::find(v.begin(), v.end(), i) != v.end();
      };
      El b = dom::el("button", "lr-answer");
      b.type("button");
      El box = dom::el("span", "lr-box");
      b.append(box, dom::el("span", "", i < static_cast<int>(w.a.size()) ? w.a[static_cast<size_t>(i)] : ""));
      const auto paint = [b, box, ticked] {
        b.setAttribute("aria-pressed", ticked() ? "true" : "false");
        b.toggleClass("on", ticked());
        box.textContent(ticked() ? "✓" : "");
      };
      if (review) {
        b.disabled(true);
        if (std::find(q.correct.begin(), q.correct.end(), i) != q.correct.end()) b.addClass("right");
        else if (ticked()) b.addClass("wrong");
      } else
        b.onclick([run, id, i, paint, changed] {
          const ExamAnswer* a = answerOf(run->answers, id);
          std::vector<int> now = a && std::holds_alternative<std::vector<int>>(*a) ? std::get<std::vector<int>>(*a) : std::vector<int>{};
          auto at = std::find(now.begin(), now.end(), i);
          if (at != now.end()) now.erase(std::remove(now.begin(), now.end(), i), now.end());
          else now.push_back(i);
          run->answers[id] = now;
          paint();
          changed();
        });
      paint();
      list.append(b);
    }
    out.push_back(list);
    return out;
  }

  El picture(const ExamPicture& p) {
    El box = dom::el("div", "lr-pic");
    if (p.sign) box.append(signPicture(*p.sign, c_.signBase, 120));
    else {
      Canvas canvas(640, 400);
      canvas.el().className("lr-plan");
      const std::optional<double> at = sceneTime(p.scene, p.step, p.off.value_or(0));
      if (at) drawPlan(canvas, p.scene, *at, t("learn.you"), c_.signBase);
      box.append(canvas.el());
    }
    return box;
  }

  std::string answerText(const ExamQuestion& q, const ExamAnswer* a) {
    if (!answered(a)) return t("learn.noAnswer");
    if (q.number) return trim((std::holds_alternative<std::string>(*a) ? std::get<std::string>(*a) : std::string()) + " " + q.number->unit);
    const ExamText* w = tx_.questions.find(q.id);
    std::vector<int> list = std::get<std::vector<int>>(*a);
    std::sort(list.begin(), list.end());
    std::vector<std::string> parts;
    for (const int i : list) parts.push_back(w && i >= 0 && i < static_cast<int>(w->a.size()) ? w->a[static_cast<size_t>(i)] : "");
    return join(parts, " · ");
  }

  void examResult(const std::shared_ptr<ExamRun>& run, const ExamScore& s, bool timeUp) {
    auto self = shared_from_this();
    Page pg = page(t("learn.exam"));
    El head = dom::el("div", std::string("lr-verdict ") + (s.passed ? "ok" : "bad"));
    head.append(dom::el("b", "", s.passed ? t("learn.passed") : t("learn.failed")), dom::el("span", "", t("learn.score", {{"points", s.points}, {"max", s.max}, {"errors", s.errors}})));
    if (timeUp) pg.panel.append(dom::el("p", "note warn", t("learn.timeUp")));
    pg.panel.append(head);
    if (s.wrongFive >= 2) pg.panel.append(dom::el("p", "note warn", t("learn.wrongFive")));
    if (s.wrong.empty()) pg.panel.append(dom::el("p", "", t("learn.allRight")));
    else {
      pg.panel.append(dom::el("h2", "", t("learn.wrongList")));
      for (const std::string& id : s.wrong) {
        const ExamQuestion& q = **std::find_if(run->qs.begin(), run->qs.end(), [&id](const ExamQuestion* x) { return x->id == id; });
        const ExamText* w = tx_.questions.find(id);
        El box = dom::el("div", "lr-review");
        box.append(dom::el("small", "lr-topic", t("learn.topic." + q.topic) + " · " + t("learn.points", {{"n", q.points}})));
        box.append(questionNodes(q, run->order[id], run, [] {}, true));
        if (q.number)
          box.append(dom::el("p", "lr-yours", t("learn.yourAnswer", {{"a", answerText(q, answerOf(run->answers, id))}})),
                     dom::el("p", "lr-rightans", t("learn.rightAnswer", {{"a", trim(jsStr(q.number->value) + " " + q.number->unit)}})));
        else if (!answered(answerOf(run->answers, id))) box.append(dom::el("p", "lr-yours", t("learn.yourAnswer", {{"a", t("learn.noAnswer")}})));
        El why = dom::el("p", "lr-why", w ? w->why : "");
        why.append(refNode(q.ref));
        box.append(why);
        pg.panel.append(box);
      }
    }
    El b = dom::el("div", "buttons");
    b.append(button("btn", t("learn.again"), [self] { self->examRun(); }), button("btn ghost", t("learn.examHistory"), [self] { self->examIntro(); }));
    pg.panel.append(b);
    c_.show(pg.root);
  }

  // ------------------------------------------------------------------ the sign trainer

  std::string meaning(const std::string& code) const {
    const std::string* m = tx_.signs.find(code);
    return m ? *m : code;
  }

  void signs(const std::string& mode, const std::string& after = "") {
    auto self = shared_from_this();
    Page pg = page(t("learn.signs"));
    const LearnData d = loadLearn();
    const double now = plat::dateMs();
    const SignProgress sp = signProgress(d.signs, now);
    El top = dom::el("div", "lr-chips");
    for (const std::string m : {"meaning", "number"}) {
      El b = button(std::string("lr-chip") + (m == mode ? " on" : ""), t(m == "meaning" ? "learn.signMeaning" : "learn.signNumber"), [self, m] { self->signs(m); });
      b.setAttribute("aria-pressed", m == mode ? "true" : "false");
      top.append(b);
    }
    top.append(button("lr-chip", t("learn.stats"), [self] { self->signStats(); }));
    pg.panel.append(top, dom::el("p", "lr-state", t("learn.signsProgress", {{"learned", sp.learned}, {"total", sp.total}, {"due", sp.due}})));
    const std::string code = nextSign(d.signs, now, after);
    const std::function<std::string(const std::string&)> label = mode == "meaning" ? std::function<std::string(const std::string&)>([self](const std::string& c) { return self->meaning(c); })
                                                                                    : std::function<std::string(const std::string&)>(signNumber);
    const std::vector<std::string> choices = signChoices(code, rnd, label);
    El card = dom::el("div", "lr-signcard");
    card.append(signPicture(code, c_.signBase, 150));
    pg.panel.append(card, dom::el("p", "lr-text", t(mode == "meaning" ? "learn.askMeaning" : "learn.askNumber")));
    El list = dom::el("div", "lr-answers");
    El after2 = dom::el("div", "lr-after");
    const int rightAt = static_cast<int>(std::find(choices.begin(), choices.end(), code) - choices.begin());
    for (const std::string& ch : choices) {
      El b = dom::el("button", "lr-answer");
      b.type("button");
      b.append(dom::el("span", "", label(ch)));
      b.onclick([self, b, list, after2, ch, code, rightAt, mode] {
        const bool right = ch == code;
        updateLearn([&](LearnData& x) { x.signs.set(code, review(x.signs.find(code), right, plat::dateMs())); });
        for (const El& o : list.querySelectorAll("button")) o.disabled(true);
        b.addClass(right ? "right" : "wrong");
        if (El r = list.child(rightAt)) r.addClass("right");
        after2.replaceChildren(dom::el("b", right ? "ok" : "bad", self->t(right ? "learn.right" : "learn.wrong")),
                               dom::el("p", "", self->t("learn.signIs", {{"code", signNumber(code)}, {"meaning", self->meaning(code)}})),
                               button("btn lr-big", self->t("learn.next") + " ›", [self, mode, code] { self->signs(mode, code); }));
        if (El go = after2.querySelector(".btn")) go.focus();
      });
      list.append(b);
    }
    pg.panel.append(list, after2);
    c_.show(pg.root);
  }

  void signStats() {
    auto self = shared_from_this();
    Page pg = page(t("learn.stats"));
    pg.back.onclick([self] { self->signs("meaning"); });
    const LearnData d = loadLearn();
    El list = dom::el("div", "lr-list");
    for (const std::string& code : TRAINER_SIGNS) {
      const SignStat* s = d.signs.find(code);
      El r = dom::el("div", std::string("lr-row lr-signrow") + (s && s->box >= LEARNED_BOX ? " ok" : ""));
      r.append(signPicture(code, c_.signBase, 40));
      El txt = dom::el("div", "lr-signtxt");
      const std::string state = s ? t("learn.box", {{"n", s->box}, {"max", TOP_BOX}}) + " · " + t("learn.statsRow", {{"right", s->right}, {"wrong", s->wrong}}) : t("learn.statsNew");
      txt.append(dom::el("b", "", meaning(code)), dom::el("small", "", signNumber(code) + " · " + state));
      r.append(txt);
      list.append(r);
    }
    pg.panel.append(list);
    c_.show(pg.root);
  }

  // ------------------------------------------------------------------ spot the danger

  /** The next item: one not answered yet, else one answered wrong last, else the one answered longest ago. */
  const DangerItem& nextDanger(const LearnData& d, const std::string& avoid) {
    std::vector<const DangerItem*> items;
    for (const DangerItem& x : DANGER_ITEMS)
      if (x.id != avoid && tx_.danger.find(x.id)) items.push_back(&x);
    for (const DangerItem* x : items)
      if (!d.danger.find(x->id)) return *x;
    for (const DangerItem* x : items)
      if (const DangerStat* s = d.danger.find(x->id); !s || !s->ok) return *x;
    const auto last = [&d](const DangerItem* x) {
      const DangerStat* s = d.danger.find(x->id);
      return s ? s->last : 0;
    };
    std::stable_sort(items.begin(), items.end(), [&last](const DangerItem* a, const DangerItem* b) { return last(a) < last(b); });
    return items.empty() ? DANGER_ITEMS[0] : *items[0];
  }

  void danger(const std::string& after = "") {
    auto self = shared_from_this();
    Page pg = page(t("learn.danger"));
    const LearnData d = loadLearn();
    const DangerItem& item = nextDanger(d, after);
    const ExamText* found = tx_.danger.find(item.id);
    const ExamText w = found ? *found : ExamText{};
    int right = 0;
    for (const DangerItem& x : DANGER_ITEMS)
      if (const DangerStat* s = d.danger.find(x.id); s && s->ok) right++;
    const bool scene = theory::theoryLesson(item.scene) != nullptr;
    pg.panel.append(dom::el("p", "lr-state", (scene ? t("theory." + item.scene + ".title") : "") + " · " + t("learn.dangerProgress", {{"right", right}, {"total", DANGER_ITEMS.size()}})));
    ExamPicture pic;
    pic.scene = item.scene;
    pic.step = item.step;
    pic.off = item.off;
    pg.panel.append(picture(pic));
    pg.panel.append(dom::el("p", "lr-text", w.q), dom::el("p", "lr-ask", t("learn.dangerAsk")), dom::el("small", "lr-multi", t("learn.multi")));
    std::vector<int> keys(static_cast<size_t>(item.answers));
    std::iota(keys.begin(), keys.end(), 0);
    const std::vector<int> order = shuffle(keys, rnd);
    auto ticked = std::make_shared<std::set<int>>();
    El list = dom::el("div", "lr-answers");
    std::vector<El> buttons;
    El after2 = dom::el("div", "lr-after");
    El check = button("btn lr-big", t("learn.check"), nullptr);
    for (const int i : order) {
      El b = dom::el("button", "lr-answer");
      b.type("button");
      El box = dom::el("span", "lr-box");
      b.append(box, dom::el("span", "", i < static_cast<int>(w.a.size()) ? w.a[static_cast<size_t>(i)] : ""));
      b.setAttribute("aria-pressed", "false");
      b.onclick([b, box, check, ticked, i] {
        if (ticked->count(i)) ticked->erase(i);
        else ticked->insert(i);
        b.toggleClass("on", ticked->count(i) > 0);
        box.textContent(ticked->count(i) ? "✓" : "");
        b.setAttribute("aria-pressed", ticked->count(i) ? "true" : "false");
        check.disabled(ticked->empty());
      });
      b.dataset("i", jsStr(i));
      buttons.push_back(b);
      list.append(b);
    }
    const std::string id = item.id;
    const std::vector<int> correct = item.correct;
    const std::string ref = item.ref;
    const std::string why0 = w.why;
    check.onclick([self, buttons, check, after2, ticked, id, correct, ref, why0] {
      bool ok = ticked->size() == correct.size();
      for (const int i : correct) ok = ok && ticked->count(i) > 0;
      updateLearn([&](LearnData& x) {
        const DangerStat* f = x.danger.find(id);
        const DangerStat s = f ? *f : DangerStat{};
        x.danger.set(id, DangerStat{s.right + (ok ? 1 : 0), s.wrong + (ok ? 0 : 1), ok, plat::dateMs()});
      });
      for (const El& b : buttons) {
        const int i = static_cast<int>(jsNumber(b.dataset("i")));
        b.disabled(true);
        if (std::find(correct.begin(), correct.end(), i) != correct.end()) b.addClass("right");
        else if (ticked->count(i)) b.addClass("wrong");
      }
      check.remove();
      El why = dom::el("p", "lr-why", why0);
      why.append(refNode(ref));
      after2.replaceChildren(dom::el("b", ok ? "ok" : "bad", self->t(ok ? "learn.right" : "learn.wrong")), why, button("btn lr-big", self->t("learn.next") + " ›", [self, id] { self->danger(id); }));
    });
    check.disabled(true);
    pg.panel.append(list, check, after2);
    c_.show(pg.root);
  }

  // ------------------------------------------------------------------ the lesson log

  void log() {
    auto self = shared_from_this();
    Page pg = page(t("learn.log"));
    const LearnData d = loadLearn();
    std::vector<std::string> theoryDone;
    if (const std::optional<std::string> raw = plat::localStorage().getItem("fahrsim-baunatal.theory.v1")) {
      Json v;
      if (Json::parse(*raw, v) && v.isObject() && v.at("done").isArray())
        for (const Json& x : v.at("done").array)
          if (x.isString()) theoryDone.push_back(x.str);
    }
    LogInput in;
    in.learn = d;
    for (const LessonDef& l : LESSONS) in.lessons.push_back(l.id);
    if (learnHooks.DRILL_IDS) in.drills = learnHooks.DRILL_IDS();
    for (const theory::LessonDef& l : theory::THEORY()) in.theory.push_back(l.id);
    in.theoryDone = theoryDone;
    for (const DangerItem& x : DANGER_ITEMS) in.danger.push_back(x.id);
    for (const FaultEntry& f : FaultLog().entries) in.faults.push_back({f.mode, f.t});
    for (const TourRecord& r : loadTours()) in.tours.push_back({r.date, r.done});
    in.now = plat::dateMs();
    const LearnLog lg = learnLog(in);
    // ---- at the top: the exams, the signs, the tours
    El sum = dom::el("div", "lr-sum");
    const auto fact = [&](const std::string& title, const std::string& value, const LearnScreen& go) {
      El b = dom::el(go.empty() ? "div" : "button", "lr-fact");
      if (!go.empty()) {
        b.type("button");
        b.onclick([self, go] { self->open(go); });
      }
      b.append(dom::el("small", "", title), dom::el("b", "", value));
      sum.append(b);
    };
    fact(t("learn.logExams"),
         lg.exams.taken > 0 ? t("learn.examProgress", {{"passed", lg.exams.passed}, {"taken", lg.exams.taken}}) + " · " + jsStr(lg.exams.best) + "/" + jsStr(lg.exams.max) : t("learn.examNone"), "exam");
    fact(t("learn.logSigns"), t("learn.signsProgress", {{"learned", lg.signs.learned}, {"total", lg.signs.total}, {"due", lg.signs.due}}), "signs");
    fact(t("learn.logTourTitle"), t("learn.logTours", {{"driven", lg.tours.driven}, {"done", lg.tours.done}}), "");
    pg.panel.append(sum);
    const std::string lang = c_.lang;
    const auto name = [self, lang](const std::string& section, const std::string& id) -> std::string {
      if (section == "lessons") return lessonTitle(id, lang);
      if (section == "drills") return self->t("drill." + id + ".name");
      if (section == "theory" || section == "danger") return self->t("theory." + id + ".title");
      return "";
    };
    const std::map<std::string, std::string> titles = {
        {"lessons", t("learn.logLessons")}, {"drills", t("learn.logDrills")}, {"theory", t("learn.logTheory")}, {"danger", t("learn.logDanger")}, {"exams", t("learn.logExams")}};
    for (const LogSection& s : lg.sections) {
      if (s.id == "exams") continue;
      El box = dom::el("details", "lr-sec");
      box.set("open", s.id == "lessons" || s.id == "drills");
      El head = dom::el("summary");
      head.append(dom::el("b", "", titles.at(s.id)), dom::el("span", "lr-of", t("learn.passedOf", {{"passed", s.passed}, {"total", s.total}})));
      box.append(head);
      for (const LogRow& r : s.rows) box.append(logRow(name(s.id, r.id), r));
      pg.panel.append(box);
    }
    El ex = dom::el("details", "lr-sec");
    El exHead = dom::el("summary");
    exHead.append(dom::el("b", "", t("learn.logExams")), dom::el("span", "lr-of", t("learn.examProgress", {{"passed", lg.exams.passed}, {"taken", lg.exams.taken}})));
    ex.append(exHead);
    for (size_t i = 0; i < d.exams.size() && i < 20; i++) ex.append(examRow(d.exams[i]));
    pg.panel.append(ex);
    c_.show(pg.root);
  }

  El logRow(const std::string& name, const LogRow& r) {
    El row = dom::el("div", std::string("lr-row ") + (r.state == "passed" ? "ok" : r.state == "tried" ? "tried" : ""));
    row.append(dom::el("span", "lr-name", name));
    const std::string state = r.state == "passed"  ? (r.stars ? repeat("★", static_cast<int>(*r.stars)) + repeat("☆", 3 - static_cast<int>(*r.stars)) : "✓")
                              : r.state == "tried" ? t("learn.stateTried")
                                                   : t("learn.stateNone");
    El st = dom::el("span", "lr-st", state);
    st.title(t(r.state == "passed" ? "learn.statePassed" : r.state == "tried" ? "learn.stateTried" : "learn.stateNone"));
    std::vector<std::string> parts;
    if (r.tries > 1) parts.push_back(t("learn.tries", {{"n", r.tries}}));
    if (const std::string dt = date(r.date); !dt.empty()) parts.push_back(dt);
    row.append(st, dom::el("small", "", join(parts, " · ")));
    return row;
  }
};

}  // namespace

El signPicture(const std::string& code, const std::string& base, double size) {
  if (ART_SIGNS.count(code)) {
    const double a = artAspect(code).value_or(1);
    const double w = a >= 1 ? size : size * a;
    const double h = a >= 1 ? size / a : size;
    const double k = std::min(2.0, plat::devicePixelRatio() > 0 ? plat::devicePixelRatio() : 1.0);
    Canvas c(static_cast<int>(jsRound(w * k)), static_cast<int>(jsRound(h * k)));
    c.el().className("lr-sign");
    c.el().style("width", jsStr(jsRound(w)) + "px");
    c.el().style("height", jsStr(jsRound(h)) + "px");
    if (Ctx2D g = c.getContext2D()) drawSignArt(g, code, 0, 0, c.width(), c.height());
    return c.el();
  }
  El img = dom::el("img", "lr-sign");
  img.src(base + "svg/" + encodeURIComponent(code) + ".svg");
  img.set("alt", "");
  img.set("decoding", "async");
  img.style("maxWidth", jsStr(size) + "px");
  img.style("maxHeight", jsStr(size) + "px");
  return img;
}

Task<void> openLearn(LearnContext c) {
  ExamTexts texts = co_await loadExamTexts(c.lang);
  const LearnScreen screen = c.screen;
  auto ui = std::make_shared<LearnUi>(std::move(c), std::move(texts));
  ui->open(screen);
}

}  // namespace fs

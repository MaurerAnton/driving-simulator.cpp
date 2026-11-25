// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/theory.ts @ v1.7.7
#include "ui/theory.h"

#include "i18n/violations.h"
#include "platform/frame.h"

namespace fs {

using dom::El;
using dom::el;

TheoryPanel::TheoryPanel(const El& rootIn, T tIn) : root(rootIn), t(std::move(tIn)) {}

void TheoryPanel::showList(const std::vector<TheoryItem>& items, std::function<void(const std::string& id)> pick, std::function<void()> close) {
  hide();
  El o = el("div", "overlay th-list");
  El p = el("div", "panel");
  p.append(el("h1", "", t("theory.title", {})), el("p", "", t("theory.listText", {})));
  El cards = el("div", "cards");
  for (size_t i = 0; i < items.size(); i++) {
    const TheoryItem& it = items[i];
    El c = el("button", "card");
    c.append(el("b", "", std::to_string(i + 1) + " · " + it.title), el("small", "", it.intro));
    if (it.done) {
      El d = el("span", "th-done", "✓");
      d.title(t("theory.done", {}));
      d.setAttribute("aria-label", t("theory.done", {}));
      c.append(d);
    }
    c.onclick([pick, id = it.id] { pick(id); });
    cards.append(c);
  }
  p.append(cards);
  El b = el("div", "buttons");
  El back = el("button", "btn ghost", t("menu", {}));
  back.onclick(std::move(close));
  b.append(back);
  p.append(b);
  o.append(p);
  root.append(o);
  list = o;
  if (El first = cards.firstElementChild()) first.focus();
}

void TheoryPanel::show(const TheoryBar& v, const TheoryBarActions& act) {
  if (list) list.remove();
  list = El();
  El b0 = bar ? bar : el("div", "th-bar");
  b0.replaceChildren();
  El head = el("div", "th-head");
  const auto icon = [](const std::string& text, const std::string& label, std::function<void()> run) {
    El b = el("button", "th-icon", text);
    b.title(label);
    b.setAttribute("aria-label", label);
    b.onclick(std::move(run));
    return b;
  };
  head.append(el("span", "th-n", std::to_string(v.n) + "/" + std::to_string(v.count)), el("b", "", v.title));
  // (on a low screen the bar scrolls: going on is always at hand up here)
  if (v.phase == "told") head.append(icon("▶", t("theory.goOn", {}), act.goOn));
  head.append(icon("↺", t("theory.again", {}), act.again), icon("☰", t("theory.list", {}), act.list));
  b0.append(head);
  if (v.phase == "loading") b0.append(el("p", "th-text", t("theory.loading", {})));
  else if (v.phase == "play") b0.append(el("p", "th-text", v.text));
  else if (v.phase == "ask" || v.phase == "told") {
    // (what is going on: with the question – once answered, the reason takes its place)
    if (!v.text.empty() && v.phase == "ask") b0.append(el("p", "th-text", v.text));
    b0.append(el("p", "th-ask", v.ask));
    El answers = el("div", "th-answers");
    for (const TheoryAnswer& a : v.answers) {
      El b = el("button", "", a.label);
      if (v.phase == "told") {
        b.disabled(true);
        if (a.id == v.right) b.addClass("right");
        else if (v.picked && a.id == *v.picked) b.addClass("wrong");
      } else b.onclick([answer = act.answer, id = a.id] { answer(id); });
      answers.append(b);
    }
    b0.append(answers);
    if (v.phase == "told") {
      const bool ok = v.picked && *v.picked == v.right;
      b0.append(el("div", std::string("th-verdict ") + (ok ? "ok" : "bad"), t(ok ? "theory.right" : "theory.wrong", {})));
      El why = el("p", "th-why", v.why);
      why.append(rule(v.ref));
      b0.append(why);
      El row = el("div", "th-actions");
      El go = el("button", "btn", t("theory.goOn", {}) + " ▶");
      go.onclick(act.goOn);
      row.append(go);
      b0.append(row);
      plat::requestAnimationFrame([go](double) { go.focus(); });
    } else b0.append(el("small", "th-help", t("theory.help", {})));
  } else {
    b0.append(el("p", "th-ask", v.score));
    El row = el("div", "th-actions");
    El again = el("button", "btn ghost", t("theory.again", {}));
    again.onclick(act.again);
    El lst = el("button", "btn ghost", t("theory.list", {}));
    lst.onclick(act.list);
    row.append(again, lst);
    if (v.hasNext) {
      El next = el("button", "btn", t("theory.next", {}) + " ▶");
      next.onclick(act.next);
      row.append(next);
      plat::requestAnimationFrame([next](double) { next.focus(); });
    }
    b0.append(row);
  }
  if (!bar) {
    root.append(b0);
    bar = b0;
  }
}

El TheoryPanel::rule(const std::string& ref) const {
  El node = el("span", "rule");
  node.dir("ltr");
  for (const CitationPart& part : citationParts(ref)) {
    if (!part.url) {
      node.append(part.text);
      continue;
    }
    El a = el("a", "", part.text);
    a.href(*part.url);
    a.set("target", "_blank");
    a.set("rel", "noopener noreferrer");
    a.title(t("logRule", {}));
    node.append(a);
  }
  return node;
}

void TheoryPanel::hide() {
  if (bar) bar.remove();
  bar = El();
  if (list) list.remove();
  list = El();
}

}  // namespace fs

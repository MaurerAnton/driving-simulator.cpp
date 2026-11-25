// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/forkliftpanel.ts @ v1.7.7
#include "ui/forkliftpanel.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/js.h"

namespace fs {

using dom::El;
using dom::el;

ForkPanel::ForkPanel(const El& parent, std::function<Lang()> lang_, std::function<void(const std::string&, bool)> press, std::function<void()> click) : lang(std::move(lang_)) {
  const Lang L = lang();
  const auto meter = [](const El& b0, const El* text, const std::string& label) {
    El m = el("div", "fork-meter");
    m.append(el("span", "", label));
    El b = el("div", "fork-bar");
    b.append(b0);
    b.append(el("u"));
    if (text) b.append(*text);
    m.append(b);
    return m;
  };
  box.append(meter(bar, &barText, tr("forkPanel.moment", L)));
  box.append(meter(sideBar, nullptr, tr("forkPanel.side", L)));
  box.append(vals);
  // the mast's buttons: held down, they hold their key down
  El btns = el("div", "fork-btns");
  const auto btn = [&](const std::string& code, const std::string& label, const std::string& title) {
    El b = el("button", "fork-btn", label);
    b.title(tr(title, L));
    b.setAttribute("aria-label", b.get("title"));
    const auto up = [b, press, code](dom::Event& e) {
      e.preventDefault();
      b.removeClass("on");
      press(code, false);
    };
    b.on("pointerdown", [b, press, click, code](dom::Event& e) {
      e.preventDefault();
      // (the pointer may be gone already)
      b.setPointerCapture(e.pointerId);
      b.addClass("on");
      click();
      press(code, true);
    });
    b.on("pointerup", up);
    b.on("pointercancel", up);
    b.on("contextmenu", [](dom::Event& e) { e.preventDefault(); });
    btns.append(b);
  };
  btn(FORK_KEYS.up, "▲", "forkPanel.up");
  btn(FORK_KEYS.back, "↶", "forkPanel.back");
  btn(FORK_KEYS.left, "◀", "forkPanel.left");
  btn(FORK_KEYS.down, "▼", "forkPanel.down");
  btn(FORK_KEYS.forward, "↷", "forkPanel.forward");
  btn(FORK_KEYS.right, "▶", "forkPanel.right");
  root.append(box);
  root.append(btns);
  root.append(keys);
  root.hidden(true);
  parent.append(root);
}

void ForkPanel::setMode(bool touch) {
  root.toggleClass("touch", touch);
  const Lang L = lang();
  if (!touch && keysLang != L) {
    keysLang = L;
    keys.replaceChildren();
    for (const char* k : {"forkKeys.lift", "forkKeys.tilt", "forkKeys.shift"}) keys.append(el("span", "", tr(k, L)));
  }
}

void ForkPanel::update(const ForkInfo& i) {
  const Lang L = lang();
  const double pct = jsRound(i.moment * 100);
  bar.style("width", jsStr(std::min(100.0, pct)) + "%");
  sideBar.style("width", jsStr(std::min(100.0, jsRound(i.side * 100))) + "%");
  box.toggleClass("warn", i.moment >= 0.8 || i.side >= 0.8);
  box.toggleClass("stop", i.moment >= 1 || i.side >= 1);
  barText.textContent(jsStr(pct) + " %");
  const std::vector<std::string> text = {
      i.load > 0 ? tr("forkPanel.load", L, {{"kg", jsRound(i.load)}, {"max", jsRound(i.allowed / 10) * 10}}) : tr("forkPanel.empty", L),
      tr("forkPanel.lift", L, {{"m", toFixed(i.lift, 2)}, {"deg", toFixed(jsRound(i.tilt * 10) / 10, 1)}}),
  };
  const std::string key = text[0] + "|" + text[1] + (i.load > i.allowed ? "!" : "");
  if (key == last) return;
  last = key;
  vals.replaceChildren();
  for (size_t n = 0; n < text.size(); n++) vals.append(el("span", n == 0 && i.load > i.allowed ? "bad" : "", text[n]));
}

void ForkPanel::dispose() { root.remove(); }

}  // namespace fs

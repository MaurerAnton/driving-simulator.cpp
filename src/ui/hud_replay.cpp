// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/hud.ts @ v1.7.7 – the replay's bar (showReplay, updateReplay, hideReplay)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/js.h"
#include "game/replay.h"
#include "i18n/violations.h"
#include "ui/hud.h"
#include "ui/hud_parts.h"

namespace fs {

using dom::El;
using dom::el;

void Hud::showReplay(ReplayBar bar, const std::vector<ReplayNote>& notes, const ReplayState& state) {
  hideReplay();
  replayNotes = &notes;
  El rootEl = el("div", "replay");
  El top = el("div", "replay-top");
  El time = el("span", "replay-time");
  El note = el("span", "replay-note");
  top.append(time, note);
  El line = el("div", "replay-line");
  El range = el("input");
  range.type("range");
  range.set("min", "0");
  range.set("max", "1000");
  range.set("step", "1");
  range.setAttribute("aria-label", t("replay"));
  const double span = std::max(0.001, state.end - state.start);
  const double start = state.start;
  range.oninput([bar, range, start, span] { bar.seek(start + (jsNumber(range.value()) / 1000) * span); });
  El marks = el("div", "replay-marks");
  for (const ReplayNote& n : notes) {
    El m = el("button", "replay-mark " + n.severity);
    m.style("left", jsStr(((n.t - state.start) / span) * 100) + "%");
    const std::string title = violationTitle(n.id, lang(), hudParts::params(n.data));
    m.title(title);
    m.setAttribute("aria-label", title);
    const double at = n.t;
    m.onclick([bar, at] { bar.seek(at - 4); });
    marks.append(m);
  }
  line.append(range, marks);
  El buttons = el("div", "replay-buttons");
  const auto btn = [](const std::string& label, const std::string& aria, std::function<void()> run) {
    El b = el("button", "btn ghost", label);
    b.setAttribute("aria-label", aria);
    b.onclick(std::move(run));
    return b;
  };
  El play = btn("⏸", t("replayPause"), [bar] { bar.toggle(); });
  El speed = btn("1×", t("replaySpeed"), [bar] { bar.speed(); });
  El cam = btn("", t("tbCamera"), [bar] { bar.camera(); });
  El close = btn("✕", t("back"), [bar] { bar.exit(); });
  close.addClass("replay-x");
  buttons.append(btn("⏪ 10 s", t("replayBack"), [bar] { bar.step(-10); }), play, btn("10 s ⏩", t("replayOn"), [bar] { bar.step(10); }), speed, cam, close);
  rootEl.append(top, line, buttons, el("div", "replay-help", t("replayHelp")));
  root.append(rootEl);
  replayBox = ReplayBox{.root = rootEl, .play = play, .speed = speed, .cam = cam, .time = time, .note = note, .range = range, .marks = marks, .shown = ""};
  updateReplay(state);
}

void Hud::updateReplay(const ReplayState& s) {
  if (!replayBox) return;
  ReplayBox& b = *replayBox;
  const auto clock = [](double t) {
    const long sec = static_cast<long>(std::max(0.0, jsRound(t)));
    char buf[32];
    std::snprintf(buf, sizeof buf, "%ld:%02ld", sec / 60, sec % 60);
    return std::string(buf);
  };
  // (the note of the instructor that fell within the last seconds)
  const ReplayNote* near = nullptr;
  if (replayNotes)
    for (const ReplayNote& n : *replayNotes)
      if (n.t <= s.t + 0.2 && n.t > s.t - 5) near = &n;
  const std::string key = jsStr(jsRound(s.t * 4)) + "|" + (s.playing ? "true" : "false") + "|" + jsStr(s.speed) + "|" + s.camera + "|" + (near ? jsStr(near->t) : "");
  if (key == b.shown) return;
  b.shown = key;
  b.time.textContent(clock(s.t - s.start) + " / " + clock(s.end - s.start));
  b.note.textContent(near ? violationTitle(near->id, lang(), hudParts::params(near->data)) : "");
  b.note.className("replay-note " + (near ? near->severity : std::string()));
  b.range.value(jsStr(jsRound(((s.t - s.start) / std::max(0.001, s.end - s.start)) * 1000)));
  b.play.textContent(s.playing ? "⏸" : "▶");
  b.play.setAttribute("aria-label", t(s.playing ? "replayPause" : "replayPlay"));
  b.speed.textContent((s.speed < 1 ? std::string("½") : jsStr(s.speed)) + "×");
  b.cam.textContent(t(s.camera == "behind" ? "replayBehind" : s.camera == "driver" ? "replayDriver" : "replayFree"));
}

void Hud::hideReplay() {
  if (replayBox) replayBox->root.remove();
  replayBox.reset();
}

}  // namespace fs

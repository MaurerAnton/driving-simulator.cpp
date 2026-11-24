// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/signaltimers.ts @ v1.7.7
#include "ui/signaltimers.h"

#include <algorithm>
#include <optional>

#include "core/js.h"

namespace fs {

using dom::El;

/** at most this many heads at a time (the nearest) */
static const int MOST = 10;
/** where the label sits above the foot of a head: over a vehicles' head (3.25 m high), over a pedestrians' head (2.6 m) */
static const double ABOVE = 3.6;
static const double ABOVE_PED = 2.85;
/** the size a label takes, for keeping them apart (CSS pixels) */
static const double LABEL_W = 50;
static const double LABEL_H = 26;

/** DOT: the class of the dot for what the light shows next */
static const char* dotOf(int next) {
  switch (next) {
    case SIG_RED:
      return "r";
    case SIG_RED_AMBER:
      return "ra";
    case SIG_GREEN:
      return "g";
    case SIG_AMBER:
      return "a";
    default:
      return "off";
  }
}

/** "12", or "–" while a light waits for somebody to ask for it */
static std::string seconds(const SignalChange& c) {
  if (!std::isfinite(c.left)) return "–";
  return jsStr(std::max(0.0, std::ceil(c.left - 0.05)));
}

SignalTimers::SignalTimers(const El& parent) {
  layer = dom::el("div");
  layer.className("sigtimes");
  // (first: under the interface that lies over the picture)
  parent.prepend(layer);
}

void SignalTimers::setHint(const std::string& text) {
  if (text == hint) return;
  hint = text;
  for (const auto& l : pool) {
    l->el.title(text);
    l->el.setAttribute("aria-label", text);
  }
}

void SignalTimers::hide() {
  for (int i = 0; i < shown; i++) pool[static_cast<size_t>(i)]->el.style("display", "none");
  shown = 0;
}

SignalTimers::Label& SignalTimers::label(int i) {
  if (i < static_cast<int>(pool.size())) return *pool[static_cast<size_t>(i)];
  El el = dom::el("button");
  el.type("button");
  el.className("sigt");
  el.tabIndex(-1);
  El dot = dom::el("i");
  El num = dom::el("b");
  el.append(dot, num);
  el.title(hint);
  el.setAttribute("aria-label", hint);
  auto l = std::make_shared<Label>(Label{el, dot, num, "", "", "", -1});
  // (at once on the press: the label moves with the picture, a click might miss it on the way up)
  // (the handler names the label's number, not its elements: they are the button's own)
  el.on("pointerdown", [this, me = std::weak_ptr<Label>(l)](dom::Event& e) {
    e.preventDefault();
    e.stopPropagation();
    const std::shared_ptr<Label> label = me.lock();
    if (label && label->controller >= 0) onTap(label->controller);
  });
  el.style("display", "none");
  layer.append(el);
  pool.push_back(l);
  return *l;
}

void SignalTimers::place(const Camera& camera, double width, double height, double zoom, SignalSystem& signals) {
  std::stable_sort(near.begin(), near.end(), [](const Near& a, const Near& b) { return a.d < b.d; });
  placed.clear();
  const double w = width / zoom;
  const double hgt = height / zoom;
  int used = 0;
  auto put = [&](const char* kind, int controller, const std::optional<SignalChange>& c, double x, double y) {
    if (!c) return;
    // (kept apart: one that would cover a nearer one is left out)
    for (size_t k = 0; k < placed.size(); k += 2)
      if (std::abs(placed[k] - x) < LABEL_W && std::abs(placed[k + 1] - y) < LABEL_H) return;
    placed.push_back(x);
    placed.push_back(y);
    Label& l = label(used++);
    l.controller = controller;
    const std::string text = seconds(*c);
    if (text != l.text) {
      l.text = text;
      l.num.textContent(text);
    }
    const std::string dot = c->next < 0 ? "off" : dotOf(c->next);
    if (dot != l.dotClass) {
      l.dotClass = dot;
      l.dot.className(dot);
    }
    if (kind != l.kind) {
      l.kind = kind;
      l.el.className(std::string("sigt ") + kind);
    }
    l.el.style("transform", "translate(" + toFixed(x, 1) + "px, " + toFixed(y, 1) + "px) translate(-50%, -100%)");
    l.el.style("display", "");
  };
  for (size_t i = 0; i < near.size() && i < static_cast<size_t>(MOST); i++) {
    const TimerHead& h = near[i].h;
    const bool ped = near[i].ped;
    v.set(h.x, h.y + (ped ? ABOVE_PED : ABOVE), h.z).project(camera);
    if (v.z > 1 || v.x < -1.05 || v.x > 1.05 || v.y < -1.05 || v.y > 1.05) continue;
    const double x = ((v.x + 1) / 2) * w;
    const double y = ((1 - v.y) / 2) * hgt;
    if (ped) {
      put("ped", h.controller, signals.pedestrianChange(h.controller, h.group), x, y);
      continue;
    }
    const std::optional<SignalChange> arrow = h.arrow ? signals.arrowChange(h.arrow) : std::nullopt;
    // with an arrow beside it: the two side by side, the arrow's on the side it hangs (the driver's right)
    put("main", h.controller, signals.change(h.controller, h.group), arrow ? x - LABEL_W * 0.52 : x, y);
    if (arrow) put("arrow", h.controller, arrow, x + LABEL_W * 0.52, y);
  }
  for (int i = used; i < shown; i++) pool[static_cast<size_t>(i)]->el.style("display", "none");
  shown = used;
}

}  // namespace fs

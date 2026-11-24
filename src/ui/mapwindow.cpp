// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/mapwindow.ts @ v1.7.7
#include "ui/mapwindow.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "core/js.h"
#include "platform/observers.h"
#include "platform/plat.h"

namespace fs {

using dom::El;

dom::El foldButton(const dom::El& panel, const dom::El& head, std::function<std::string(bool folded)> label) {
  El b = dom::el("button");
  b.type("button");
  b.className("bm-x bm-fold");
  const std::function<void()> show = [b, panel, label] {
    const bool folded = panel.hasClass("bm-folded");
    b.textContent(folded ? "▸" : "▾");
    b.title(label(folded));
    b.setAttribute("aria-label", label(folded));
    b.setAttribute("aria-expanded", folded ? "false" : "true");
  };
  b.onclick([panel, show] {
    panel.toggleClass("bm-folded");
    show();
  });
  show();
  // (before the close button, if the row has one)
  El close = head.querySelector(".bm-x:not(.bm-fold)");
  if (close) close.before(b);
  else head.append(b);
  return b;
}

std::function<void()> addGrip(const dom::El& panel_, const std::string& label, GripMin min) {
  const El panel = panel_;
  const El box = panel.parentElement();
  if (!box) return [] {};
  box.addClass("bm-griphost");
  El grip = dom::el("div");
  grip.className("bm-grip");
  grip.title(label);
  grip.setAttribute("role", "separator");
  grip.setAttribute("aria-label", label);
  box.append(grip);
  const std::function<bool()> rtl = [panel] { return panel.computedStyle("direction") == "rtl"; };
  const std::function<void()> place = [panel, grip, rtl] {
    const bool hidden = panel.hidden() || panel.offsetWidth() == 0 || panel.hasClass("bm-folded");
    grip.hidden(hidden);
    if (hidden) return;
    // (at the corner away from the side the window stands at: the lower right one, the lower left one in a language written from the right)
    const bool r = rtl();
    grip.toggleClass("rtl", r);
    grip.style("top", jsStr(panel.num("offsetTop") + panel.offsetHeight() - 30) + "px");
    grip.style("left", jsStr(r ? panel.num("offsetLeft") : panel.num("offsetLeft") + panel.offsetWidth() - 30) + "px");
  };
  const plat::ResizeObserver ro(place);
  ro.observe(panel);
  ro.observe(box);
  const plat::MutationObserver mo(place);
  mo.observe(panel, {"hidden", "class", "style"});
  struct Drag {
    int id = 0;
    double x = 0, y = 0, w = 0, h = 0;
  };
  struct State {
    std::optional<Drag> drag;
    double lastTap = 0;
  };
  auto st = std::make_shared<State>();
  grip.on("pointerdown", [st, panel, grip](dom::Event& e) {
    e.preventDefault();
    e.stopPropagation();
    const double now = plat::nowMs();
    if (now - st->lastTap < 350) {
      // a double tap: as large as what is in it again
      panel.style("width", "");
      panel.style("maxHeight", "");
      st->lastTap = 0;
      return;
    }
    st->lastTap = now;
    const dom::Rect r = panel.getBoundingClientRect();
    st->drag = Drag{e.pointerId, e.clientX, e.clientY, r.width, r.height};
    grip.setPointerCapture(e.pointerId);
  });
  grip.on("pointermove", [st, panel, box, rtl, place, min](dom::Event& e) {
    if (!st->drag || st->drag->id != e.pointerId) return;
    e.stopPropagation();
    const double dx = (e.clientX - st->drag->x) * (rtl() ? -1 : 1);
    const double w = std::max(min.w, std::min(box.clientWidth(), st->drag->w + dx));
    const double h = std::max(min.h, std::min(box.clientHeight(), st->drag->h + e.clientY - st->drag->y));
    panel.style("width", jsStr(jsRound(w)) + "px");
    panel.style("maxHeight", jsStr(jsRound(h)) + "px");
    place();
  });
  const dom::Handler end = [st](dom::Event& e) {
    if (st->drag && st->drag->id == e.pointerId) st->drag.reset();
  };
  grip.on("pointerup", end);
  grip.on("pointercancel", end);
  place();
  return [ro, mo, grip] {
    ro.disconnect();
    mo.disconnect();
    grip.remove();
  };
}

}  // namespace fs

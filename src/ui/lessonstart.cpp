// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/lessonstart.ts @ v1.7.7
#include "ui/lessonstart.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include "core/js.h"
#include "platform/canvas.h"

namespace fs {

using dom::El;
using dom::el;

DistrictFrame districtFrame(const std::vector<LessonArea>& areas, double w, double h, double pad) {
  double x0 = JS_INF;
  double x1 = -JS_INF;
  double z0 = JS_INF;
  double z1 = -JS_INF;
  for (const LessonArea& a : areas) {
    x0 = std::min(x0, a.x);
    x1 = std::max(x1, a.x);
    z0 = std::min(z0, a.z);
    z1 = std::max(z1, a.z);
  }
  if (!std::isfinite(x0)) {
    x0 = z0 = -1;
    x1 = z1 = 1;
  }
  const double span = std::max({x1 - x0, z1 - z0, 1.0});
  const double k = std::min((w - 2 * pad) / std::max(x1 - x0, span * 0.05), (h - 2 * pad) / std::max(z1 - z0, span * 0.05));
  const double cx = (x0 + x1) / 2;
  const double cz = (z0 + z1) / 2;
  return DistrictFrame{w, h, cx, cz, k};
}

int districtAt(const std::vector<LessonArea>& areas, const DistrictFrame& frame, double px, double py, double reach) {
  int best = -1;
  double bd = reach;
  for (size_t i = 0; i < areas.size(); i++) {
    const std::array<double, 2> at = frame.toPx(areas[i].x, areas[i].z);
    const double d = std::hypot(at[0] - px, at[1] - py);
    if (d < bd) {
      bd = d;
      best = static_cast<int>(i);
    }
  }
  return best;
}

static El button(const std::string& cls, const std::string& text, std::function<void()> act) {
  El b = el("button", cls, text);
  b.type("button");
  b.onclick(std::move(act));
  b.setAttribute("style", "display:block;width:100%;min-height:48px;margin:6px 0");
  return b;
}

El lessonStartPanel(const TrFn& t, Settings& s, const std::vector<SpawnJson>& spawns, const LessonStartOptions& o, std::function<void()> done, std::function<void()> back) {
  El overlay = el("div", "overlay");
  El p = el("div", "panel narrow");
  p.append(el("h1", "", o.title));
  Settings* sp = &s;
  auto areas = std::make_shared<const std::vector<LessonArea>>(areaChoices(spawns));
  if (o.place) {
    p.append(el("h2", "", t("lessonPlace.title", {})));
    static const std::vector<std::pair<LessonPlaceMode, std::string>> modes = {
        {"start", "lessonPlace.start"},
        {"town", "lessonPlace.town"},
        {"area", "lessonPlace.area"},
    };
    // (the buttons are made first and told what to do below, once `setMode` is there)
    std::vector<El> modeButtons;
    for (const auto& [m, key] : modes) {
      El b = button("btn ghost", t(key, {}), nullptr);
      b.setAttribute("aria-pressed", "false");
      modeButtons.push_back(b);
    }
    p.append(modeButtons);
    // the district: on a list, or on the map of the districts
    El pick = el("div");
    El sel = el("select");
    sel.setAttribute("style", "width:100%;min-height:48px");
    sel.setAttribute("aria-label", t("lessonPlace.areaLabel", {}));
    for (size_t i = 0; i < areas->size(); i++) {
      El op = el("option", "", (*areas)[i].name);
      op.value(jsStr(static_cast<double>(i)));
      sel.append(op);
    }
    const double W = 640;
    const double H = 400;
    El canvasEl = el("canvas");
    canvasEl.width(W);
    canvasEl.height(H);
    canvasEl.setAttribute("style", "display:block;width:100%;height:auto;margin:8px 0;border-radius:8px;touch-action:manipulation;cursor:pointer");
    canvasEl.setAttribute("role", "img");
    const DistrictFrame frame = districtFrame(*areas, W, H);
    auto chosenIndex = [sp, areas]() -> int {
      if (sp->lessonArea)
        for (size_t i = 0; i < areas->size(); i++)
          if ((*areas)[i].name == sp->lessonArea->name) return static_cast<int>(i);
      return 0;
    };
    const std::string mapLabel = t("lessonStart.map", {});
    auto draw = [canvasEl, p, areas, frame, chosenIndex, mapLabel, W, H] {
      const Ctx2D g = Canvas(canvasEl).getContext2D();
      if (!g) return;
      std::string ink = p.computedStyle("color");
      if (ink.empty()) ink = "#ddd";
      const int at = chosenIndex();
      g.clearRect(0, 0, W, H);
      g.fillStyle("rgba(127,127,127,0.14)");
      g.fillRect(0, 0, W, H);
      g.font("22px system-ui, sans-serif");
      g.textAlign("center");
      for (size_t i = 0; i < areas->size(); i++) {
        if (static_cast<int>(i) == at) continue;
        const std::array<double, 2> xy = frame.toPx((*areas)[i].x, (*areas)[i].z);
        g.fillStyle(ink);
        g.globalAlpha(0.55);
        g.beginPath();
        g.arc(xy[0], xy[1], 7, 0, M_PI * 2);
        g.fill();
      }
      g.globalAlpha(1);
      const LessonArea* a = at >= 0 && static_cast<size_t>(at) < areas->size() ? &(*areas)[static_cast<size_t>(at)] : nullptr;
      if (a) {
        const std::array<double, 2> xy = frame.toPx(a->x, a->z);
        g.fillStyle("#e8a33d");
        g.beginPath();
        g.arc(xy[0], xy[1], 13, 0, M_PI * 2);
        g.fill();
        g.fillStyle(ink);
        g.font("bold 26px system-ui, sans-serif");
        g.fillText(a->name, std::max(90.0, std::min(W - 90, xy[0])), xy[1] > 50 ? xy[1] - 22 : xy[1] + 42);
      }
      canvasEl.setAttribute("aria-label", mapLabel + ": " + (a ? a->name : ""));
    };
    auto choose = [sp, areas, sel, draw](double i) {
      if (!(i >= 0 && i < static_cast<double>(areas->size()) && i == std::floor(i))) return;
      sp->lessonArea = (*areas)[static_cast<size_t>(i)];
      sel.value(jsStr(i));
      draw();
    };
    sel.onchange([sel, choose] { choose(jsNumber(sel.value())); });
    canvasEl.onclick([canvasEl, areas, frame, choose, W, H](dom::Event& e) {
      const dom::Rect r = canvasEl.getBoundingClientRect();
      if (r.width <= 0 || r.height <= 0) return;
      const int i = districtAt(*areas, frame, ((e.clientX - r.left) * W) / r.width, ((e.clientY - r.top) * H) / r.height, 60);
      if (i >= 0) choose(i);
    });
    pick.append(sel, canvasEl, el("p", "note", t("lessonStart.mapHint", {})));
    p.append(pick);
    auto setMode = [sp, modeButtons, pick, areas, sel, chosenIndex, draw](const LessonPlaceMode& m) {
      sp->lessonPlace = m;
      sp->lessonPlaceAsked = true;
      for (size_t i = 0; i < modeButtons.size(); i++) {
        const bool on = modes[i].first == m;
        modeButtons[i].className(on ? "btn" : "btn ghost");
        modeButtons[i].setAttribute("aria-pressed", on ? "true" : "false");
      }
      pick.hidden(m != "area" || areas->empty());
      if (m == "area") {
        if (!sp->lessonArea && !areas->empty()) sp->lessonArea = (*areas)[0];
        sel.value(jsStr(chosenIndex()));
        draw();
      }
    };
    for (size_t i = 0; i < modeButtons.size(); i++) modeButtons[i].onclick([setMode, m = modes[i].first] { setMode(m); });
    setMode(s.lessonPlace);
  } else p.append(el("p", "note", t("lessonStart.here", {})));
  // how: drive it, or watch it
  p.append(el("h2", "", t("lessonStart.how", {})));
  auto go = [sp, done = std::move(done)](bool watch) {
    sp->lessonWatch = watch;
    done();
  };
  El drive = button(s.lessonWatch && o.watch ? "btn ghost" : "btn", t("lessonStart.drive", {}), [go] { go(false); });
  p.append(drive);
  if (o.watch) {
    p.append(button(s.lessonWatch ? "btn" : "btn ghost", t("lessonStart.watch", {}), [go] { go(true); }), el("p", "note", t("lessonStart.watchNote", {})));
  } else p.append(el("p", "note", t("lessonStart.noWatch", {})));
  p.append(el("p", "note", t("lessonPlace.note", {})));
  El b = el("div", "buttons");
  El backBtn = el("button", "btn ghost", t("back", {}));
  backBtn.type("button");
  backBtn.onclick(std::move(back));
  b.append(backBtn);
  p.append(b);
  overlay.append(p);
  return overlay;
}

}  // namespace fs

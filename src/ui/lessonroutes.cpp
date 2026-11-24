// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/lessonroutes.ts @ v1.7.7
#include "ui/lessonroutes.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "core/js.h"
#include "game/lessonplaces.h"
#include "game/routepresets.h"

namespace fs {

using dom::El;
using dom::el;

static El button(const std::string& cls, const std::string& text, std::function<void()> act) {
  El b = el("button", cls, text);
  b.type("button");
  b.onclick(std::move(act));
  return b;
}

/** One button for a set route: its name, and what it leads through below it. */
static El routeButton(const TrFn& t, const std::string& id, const std::function<void(const std::string& id)>& start) {
  El b = button("btn ghost", "", [start, id] { start(id); });
  b.setAttribute("style", "display:block;width:100%;text-align:start;min-height:48px;margin:6px 0");
  El name = el("b", "", t("routes." + id + ".name", {}));
  El desc = el("small", "", t("routes." + id + ".desc", {}));
  desc.setAttribute("style", "display:block;opacity:0.75;font-weight:400;margin-top:2px");
  b.append(name, desc);
  return b;
}

El routePresetPage(const TrFn& t, std::function<void(const std::string& id)> start, std::function<void()> back) {
  El o = el("div", "overlay");
  El p = el("div", "panel narrow");
  p.append(el("h1", "", t("routes.title", {})), el("p", "", t("routes.hint", {})));
  for (const RoutePreset& r : ROUTE_PRESETS) p.append(routeButton(t, r.id, start));
  El b = el("div", "buttons");
  b.append(button("btn", t("back", {}), std::move(back)));
  p.append(b);
  o.append(p);
  return o;
}

El routePresetSection(const TrFn& t, std::function<void(const std::string& id)> start) {
  El box = el("details", "bm-tour");
  box.append(el("summary", "", t("routes.title", {})), el("small", "bm-result", t("routes.mapHint", {})));
  for (const RoutePreset& r : ROUTE_PRESETS) box.append(routeButton(t, r.id, start));
  return box;
}

/**
 * The districts to choose from as a <select>, the chosen one (or the home town) selected.
 * `onchange`: what the select does when it changes (the original calls `area.onchange` itself
 * where a district is to be set without the player choosing one).
 */
static El areaSelect(Settings& s, const std::vector<SpawnJson>& spawns, std::function<void()> changed, std::function<void()>* onchange = nullptr) {
  auto choices = std::make_shared<const std::vector<LessonArea>>(areaChoices(spawns));
  El sel = el("select");
  for (size_t i = 0; i < choices->size(); i++) {
    El op = el("option", "", (*choices)[i].name);
    op.value(jsStr(static_cast<double>(i)));
    sel.append(op);
  }
  int at = 0;
  if (s.lessonArea) {
    at = -1;
    for (size_t i = 0; i < choices->size(); i++)
      if ((*choices)[i].name == s.lessonArea->name) {
        at = static_cast<int>(i);
        break;
      }
  }
  sel.value(jsStr(std::max(0, at)));
  Settings* sp = &s;
  std::function<void()> change = [sp, sel, choices, changed = std::move(changed)] {
    const double i = jsNumber(sel.value());
    if (i >= 0 && i < static_cast<double>(choices->size()) && i == std::floor(i)) sp->lessonArea = (*choices)[static_cast<size_t>(i)];
    else sp->lessonArea = std::nullopt;
    changed();
  };
  sel.onchange(change);
  if (onchange) *onchange = change;
  return sel;
}

static const std::vector<std::pair<LessonPlaceMode, std::string>> MODES = {
    {"start", "lessonPlace.start"},
    {"town", "lessonPlace.town"},
    {"area", "lessonPlace.area"},
};

std::vector<El> lessonPlaceRows(const TrFn& t, Settings& s, const std::vector<SpawnJson>& spawns, std::function<void()> changed) {
  El row = el("div", "row");
  El sel = el("select");
  for (const auto& [v, k] : MODES) {
    El op = el("option", "", t(k, {}));
    op.value(v);
    sel.append(op);
  }
  sel.value(s.lessonPlace);
  El areaRow = el("div", "row");
  std::function<void()> areaChange;
  El area = areaSelect(s, spawns, changed, &areaChange);
  area.disabled(s.lessonPlace != "area");
  Settings* sp = &s;
  sel.onchange([sp, sel, area, areaChange, changed] {
    sp->lessonPlace = sel.value();
    sp->lessonPlaceAsked = true;
    area.disabled(sp->lessonPlace != "area");
    if (sp->lessonPlace == "area" && !sp->lessonArea) areaChange();
    changed();
  });
  row.append(el("label", "", t("lessonPlace.title", {})), sel);
  areaRow.append(el("label", "", t("lessonPlace.areaLabel", {})), area);
  // … and whether they are driven or watched (asked again at each lesson's start: ui/lessonstart.ts)
  El howRow = el("div", "row");
  El how = el("select");
  for (const auto& [v, k] : std::vector<std::pair<std::string, std::string>>{{"drive", "lessonStart.drive"}, {"watch", "lessonStart.watch"}}) {
    El op = el("option", "", t(k, {}));
    op.value(v);
    how.append(op);
  }
  how.value(s.lessonWatch ? "watch" : "drive");
  how.onchange([sp, how, changed] {
    sp->lessonWatch = how.value() == "watch";
    changed();
  });
  howRow.append(el("label", "", t("lessonStart.how", {})), how);
  return {row, areaRow, howRow, el("p", "note", t("lessonPlace.note", {}))};
}

El askLessonPlace(const TrFn& t, Settings& s, const std::vector<SpawnJson>& spawns, std::function<void()> done) {
  El o = el("div", "overlay");
  El p = el("div", "panel narrow");
  p.append(el("h1", "", t("lessonPlace.title", {})), el("p", "", t("lessonPlace.ask", {})));
  Settings* sp = &s;
  auto pick = [sp, done = std::move(done)](const LessonPlaceMode& m) {
    sp->lessonPlace = m;
    sp->lessonPlaceAsked = true;
    done();
  };
  auto big = [](const El& b) {
    b.setAttribute("style", "display:block;width:100%;min-height:48px;margin:8px 0");
    return b;
  };
  p.append(big(button("btn", t("lessonPlace.start", {}), [pick] { pick("start"); })), big(button("btn ghost", t("lessonPlace.town", {}), [pick] { pick("town"); })));
  // "in an area": the district first, then go
  El areaRow = el("div", "row");
  std::function<void()> areaChange;
  El area = areaSelect(s, spawns, [] {}, &areaChange);
  if (!s.lessonArea) areaChange();
  El go = button("btn ghost", t("lessonPlace.area", {}), [pick] { pick("area"); });
  go.style("minHeight", "48px");
  areaRow.append(area, go);
  p.append(areaRow, el("p", "note", t("lessonPlace.note", {})));
  o.append(p);
  return o;
}

}  // namespace fs

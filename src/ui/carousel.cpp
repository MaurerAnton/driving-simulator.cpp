// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/carousel.ts @ v1.7.7
#include "ui/carousel.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

#include "core/js.h"
#include "game/nav.h"
#include "platform/frame.h"
#include "platform/plat.h"
#include "ui/stage.h"

namespace fs {

using dom::El;
using dom::el;

namespace {

const std::map<VehicleKind, std::string>& icons() {
  static const auto* m = new std::map<VehicleKind, std::string>{
      {"car", "🚗"},          {"carTrailer", "🚙"}, {"moped", "🛵"},     {"motorcycle125", "🏍️"}, {"motorcycle35", "🏍️"}, {"motorcycle", "🏍️"},
      {"lightTruck", "🚚"},   {"lightTruckTrailer", "🚚"}, {"truckSolo", "🚛"}, {"truck", "🚛"},   {"semi", "🚛"},          {"minibus", "🚐"},
      {"minibusTrailer", "🚐"}, {"bus", "🚌"},       {"busTrailer", "🚌"}, {"tractorSlow", "🚜"},  {"tractor", "🚜"},       {"tram", "🚊"},
      {"plane", "🛩️"},        {"mobileCrane", "🏗️"}, {"loaderCrane", "🏗️"}, {"towerCrane", "🏗️"}, {"forklift", "🏭"},     {"pedestrian", "🚶"},
  };
  return *m;
}

/** How far each row was swiped (kept when the menu is drawn again after a choice), and whether the middle rows are folded away (at first: on a low screen). */
std::map<std::string, double>& swiped() {
  static auto* m = new std::map<std::string, double>();
  return *m;
}
std::optional<bool> folded;

/** A row: its title, and its cards in a strip to swipe – the chosen one brought into view the first time. */
El row(const std::string& key, const std::string& title, const std::vector<El>& items, const El& extra = El()) {
  El r = el("div", "cm-row cm-" + key);
  El head = el("div", "cm-head");
  head.append(el("span", "", title));
  if (extra) head.append(extra);
  El strip = el("div", "cm-strip");
  strip.append(items);
  strip.on("scroll", [key, strip] { swiped()[key] = strip.num("scrollLeft"); });
  r.append(head, strip);
  // (after it is in the page: where it was, else the chosen card in view)
  plat::requestAnimationFrame([key, strip](double) {
    const auto was = swiped().find(key);
    if (was != swiped().end()) strip.set("scrollLeft", was->second);
    else if (El on = strip.querySelector(".on")) on.scrollIntoView();
  });
  return r;
}

El card(const std::string& cls, bool on, const std::vector<El>& parts, std::function<void()> act, const std::string& title = "") {
  El b = el("button", cls + (on ? " on" : ""));
  b.append(parts);
  if (!title.empty()) b.title(title);
  b.setAttribute("aria-pressed", on ? "true" : "false");
  b.onclick(std::move(act));
  return b;
}

El text(const std::string& s) { return el("span", "", s); }

}  // namespace

El carouselMenu(const CarouselContext& c) {
  Settings& s = *c.settings;
  if (!folded) folded = plat::innerHeight() < 760;
  const VehicleDef& def = VEHICLES.find(s.vehicle) ? *VEHICLES.find(s.vehicle) : VEHICLES.at("car");
  const VehicleKind kind = def.kind;
  Settings::LastStart& last = s.lastStart;
  Settings* sp = &s;
  const auto save = [sp] { saveSettings(*sp); };
  const auto t = [&c](const std::string& k, const TrParams& v = {}) { return c.t(k, v); };
  const std::function<void()> redraw = c.redraw;
  El root = el("div", "cm");

  // ---- at the top: the name, the language, the other pages
  El top = el("div", "cm-top");
  El title = el("div", "cm-title");
  title.innerHTML("Fahrsim <span>Baunatal</span>");
  title.append(el("small", "", c.version), el("small", "cm-loading"));
  El tools = el("div", "cm-tools");
  tools.append(c.language);
  for (const auto& [label, run] : c.tools) {
    El b = el("button", "btn ghost", label);
    b.onclick(run);
    tools.append(b);
  }
  top.append(title, tools);

  El rows = el("div", "cm-rows");
  // ---- the vehicles
  std::vector<El> vehicles;
  for (const VehicleKind& k : VEHICLE_KINDS) {
    const VehicleDef* d = VEHICLES.find(k);
    if (!d) continue;
    const bool on = k == s.vehicle;
    const auto ic = icons().find(k);
    vehicles.push_back(card("cm-card", on, {el("span", "cm-icon", ic != icons().end() ? ic->second : ""), el("span", "cm-name", t(d->name))}, [sp, k, save, redraw] {
      if (k == sp->vehicle) return;
      sp->vehicle = k;
      save();
      redraw();
    }));
  }
  // (the place and the options folded away: what they are set to, in one line beside the button that unfolds them)
  El fold = el("button", "cm-fold");
  El summary = el("span", "cm-summary");
  fold.append(summary, el("span", "", *folded ? " ▴" : " ▾"));
  fold.setAttribute("aria-label", t(*folded ? "menuMore" : "menuLess"));
  fold.title(fold.getAttribute("aria-label"));
  fold.onclick([redraw] {
    folded = !*folded;
    redraw();
  });
  rows.append(row("vehicle", t("vehicle"), vehicles, fold));

  // ---- where: the towns, then the places of the town chosen (stops for the tram, runway ends for the aeroplane)
  std::optional<MenuPreview> preview;
  StartRequest request;
  request.spawn = std::min(last.spawn, std::max(0, static_cast<int>(c.spawns.size()) - 1));
  std::vector<El> middle;
  const auto loading = [&t] { return el("span", "cm-chip off", t("realityLoading")); };
  if (kind == "plane") {
    const std::vector<RunwayChoice>* list = c.runways;
    if (!list || list->empty()) middle.push_back(row("town", t("startPoint"), {list ? el("span", "cm-chip off", "–") : loading()}));
    else {
      int at = -1;
      for (size_t i = 0; i < list->size(); i++)
        if (last.runway && runwayKey((*list)[i]) == *last.runway) {
          at = static_cast<int>(i);
          break;
        }
      if (at < 0) at = 0;
      const RunwayChoice chosen = (*list)[static_cast<size_t>(at)];
      std::vector<std::string> fields;
      for (const RunwayChoice& r : *list)
        if (std::find(fields.begin(), fields.end(), r.field) == fields.end()) fields.push_back(r.field);
      std::vector<El> fieldCards;
      for (const std::string& f : fields) {
        std::string first;
        for (const RunwayChoice& r : *list)
          if (r.field == f) {
            first = runwayKey(r);
            break;
          }
        fieldCards.push_back(card("cm-chip", f == chosen.field, {text(f)}, [sp, first, save, redraw] {
          sp->lastStart.runway = first;
          save();
          redraw();
        }));
      }
      std::vector<El> ends;
      for (const RunwayChoice& r : *list) {
        if (r.field != chosen.field) continue;
        const std::string key = runwayKey(r);
        ends.push_back(card("cm-chip", key == runwayKey(chosen), {text(t("runwayEnd", {{"end", r.end}}))}, [sp, key, save, redraw] {
          sp->lastStart.runway = key;
          save();
          redraw();
        }));
      }
      middle.push_back(row("town", t("startPoint"), fieldCards));
      middle.push_back(row("place", chosen.field, ends));
      request.runway = chosen;
      preview = MenuPreview{.kind = kind, .x = chosen.x, .z = chosen.z, .yaw = chosen.yaw};
    }
  } else if (kind == "tram") {
    const std::vector<TramStopChoice>* list = c.tramStops;
    if (!list || list->empty()) middle.push_back(row("town", t("startPoint"), {list ? el("span", "cm-chip off", "–") : loading()}));
    else {
      int at = -1;
      for (size_t i = 0; i < list->size(); i++)
        if (last.tramStop && tramKey((*list)[i]) == *last.tramStop) {
          at = static_cast<int>(i);
          break;
        }
      if (at < 0) {
        int k = -1;
        for (size_t i = 0; i < list->size(); i++)
          if ((*list)[i].name == "Königsplatz") {
            k = static_cast<int>(i);
            break;
          }
        at = std::max(0, k);
      }
      const TramStopChoice chosen = (*list)[static_cast<size_t>(at)];
      std::vector<std::string> towns;
      for (const TramStopChoice& q : *list)
        if (std::find(towns.begin(), towns.end(), q.town) == towns.end()) towns.push_back(q.town);
      std::vector<El> townCards;
      for (const std::string& town : towns) {
        std::string first;
        for (const TramStopChoice& q : *list)
          if (q.town == town) {
            first = tramKey(q);
            break;
          }
        townCards.push_back(card("cm-chip", town == chosen.town, {text(town)}, [sp, first, save, redraw] {
          sp->lastStart.tramStop = first;
          save();
          redraw();
        }));
      }
      std::vector<El> stopCards;
      for (const TramStopChoice& q : *list) {
        if (q.town != chosen.town) continue;
        const std::string key = tramKey(q);
        stopCards.push_back(card("cm-chip", key == tramKey(chosen), {text(q.name)}, [sp, key, save, redraw] {
          sp->lastStart.tramStop = key;
          save();
          redraw();
        }));
      }
      middle.push_back(row("town", t("startPoint"), townCards));
      middle.push_back(row("place", chosen.town, stopCards));
      request.stop = chosen;
      preview = MenuPreview{.kind = kind, .x = chosen.x, .z = chosen.z, .yaw = 0, .stop = chosen};
    }
  } else if (!c.spawns.empty()) {
    // (the start points of the map by town, in the order the map lists them; or an address)
    const int spawn = request.spawn;
    const std::optional<AddressHit> address = last.address;
    const std::vector<SpawnJson>& spawns = c.spawns;
    const auto town = [&spawns](int i) { return i >= 0 && static_cast<size_t>(i) < spawns.size() ? spawns[static_cast<size_t>(i)].g : std::string(); };
    const std::string chosenTown = town(spawn);
    std::vector<std::string> towns;
    for (const SpawnJson& q : spawns)
      if (std::find(towns.begin(), towns.end(), q.g) == towns.end()) towns.push_back(q.g);
    El pickAddress = el("button", std::string("cm-chip") + (address ? " on" : ""), "📍 " + (address ? address->label : t("startAddress")));
    const auto field = std::make_shared<El>();
    const auto addressField = c.address;
    pickAddress.onclick([pickAddress, field, addressField, sp, save, redraw] {
      if (*field) {
        field->remove();
        *field = El();
        return;
      }
      *field = addressField([sp, save, redraw](const AddressHit& hit) {
        sp->lastStart.address = hit;
        save();
        redraw();
      });
      field->addClass("cm-address");
      if (El r = pickAddress.closest(".cm-row")) r.after(*field);
      if (El in = field->querySelector("input")) in.focus();
    });
    std::vector<El> townChips{pickAddress};
    for (const std::string& g : towns) {
      int first = -1;
      for (size_t i = 0; i < spawns.size(); i++)
        if (spawns[i].g == g) {
          first = static_cast<int>(i);
          break;
        }
      townChips.push_back(card("cm-chip", !address && g == chosenTown, {text(g.empty() ? "–" : g)}, [sp, first, save, redraw] {
        sp->lastStart.spawn = first;
        sp->lastStart.address.reset();
        save();
        redraw();
      }));
    }
    std::vector<El> places;
    for (size_t i = 0; i < spawns.size(); i++) {
      if (town(static_cast<int>(i)) != chosenTown) continue;
      const int idx = static_cast<int>(i);
      places.push_back(card("cm-chip", !address && idx == spawn, {text(spawns[i].name)}, [sp, idx, save, redraw] {
        sp->lastStart.spawn = idx;
        sp->lastStart.address.reset();
        save();
        redraw();
      }));
    }
    middle.push_back(row("town", t("startPoint"), townChips));
    middle.push_back(row("place", chosenTown.empty() ? t("startPoint") : chosenTown, places));
    request.address = address;
    const SpawnJson* at = spawn >= 0 && static_cast<size_t>(spawn) < spawns.size() ? &spawns[static_cast<size_t>(spawn)] : nullptr;
    if (address) preview = MenuPreview{.kind = kind, .x = address->x, .z = address->z, .yaw = at ? at->yaw : 0};
    else if (at) preview = MenuPreview{.kind = kind, .x = at->x, .z = at->z, .yaw = at->yaw};
  }

  // ---- the options of this vehicle: its gearbox, its engine, the make-up of the tram
  std::vector<El> options;
  if (def.model == "car" || def.model == "motorcycle") {
    for (const auto& [v, k] : std::vector<std::pair<std::string, std::string>>{{"assist", "trAssist"}, {"manual", "trManual"}, {"automatic", "trAutomatic"}}) {
      const std::string value = v;
      options.push_back(card("cm-chip", s.transmission == v, {text(t(k))}, [sp, value, save, redraw] {
        sp->transmission = value;
        save();
        redraw();
      }));
    }
  }
  if (def.powers.size() > 1) {
    const double now = powerOf(def, s.powers);
    for (double ps : def.powers) {
      const VehicleKind dk = def.kind;
      options.push_back(card(
          "cm-chip", ps == now, {text(jsStr(ps) + " PS")},
          [sp, dk, ps, save, redraw] {
            sp->powers[dk] = ps;
            save();
            redraw();
          },
          jsStr(ps) + " PS (" + jsStr(jsRound(ps * 0.7355)) + " kW)"));
    }
  }
  if (kind == "tram") {
    for (const auto& [v, k] : std::vector<std::pair<std::string, std::string>>{{"single", "tramSetSingle"}, {"double", "tramSetDouble"}, {"trailer", "tramSetTrailer"}}) {
      const std::string value = v;
      options.push_back(card("cm-chip", s.tramSet == v, {text(t(k))}, [sp, value, save, redraw] {
        sp->tramSet = value;
        save();
        redraw();
      }));
    }
  }
  if (!options.empty()) middle.push_back(row("options", t("menuOptions"), options));
  if (!*folded) rows.append(middle);
  else {
    // the choices of the folded rows, as their chosen cards say them
    std::string said;
    for (const El& r : middle)
      for (const El& e : r.querySelectorAll(".cm-strip .on")) {
        const std::string tc = e.textContent();
        if (tc.empty()) continue;
        if (!said.empty()) said += " · ";
        said += tc;
      }
    summary.textContent(said);
  }

  // ---- what to do: tap a card and the drive begins
  std::vector<El> modes;
  const auto start = c.start;
  const auto mode = [&modes, start, request](const std::string& titleText, const std::string& desc, const std::string& m, bool main = false) {
    StartRequest req = request;
    req.mode = m;
    modes.push_back(card(std::string("cm-mode") + (main ? " main" : ""), false, {el("b", "", titleText), el("small", "", desc)}, [start, req] { start(req); }));
  };
  mode(t(kind == "plane" ? "freeFly" : "free"), t(kind == "tram" ? "freeTramDesc" : kind == "pedestrian" ? "freeWalkDesc" : kind == "plane" ? "freeFlyDesc" : "freeDesc"), "free", true);
  // the forklift: its work on the forklift yard (game/forkliftplay.ts) – its free drive begins at the place chosen above
  if (kind == "forklift") mode(t("forkWork"), t("forkWorkDesc"), "forklift", true);
  if (kind == "tram") mode(t("demo"), t("demoTramDesc"), "demo");
  if (kind == "plane") mode(t("demoFly"), t("demoFlyDesc"), "demo");
  if (kind != "tram" && kind != "pedestrian" && kind != "plane" && kind != "forklift") {
    mode(t("demo"), t("demoDesc"), "demo");
    mode(t("tourTitle"), t("tourDesc"), "tour");
  }
  // the theory lessons: difficult situations shown on the practice yard (game/theorymode.ts)
  if (kind != "plane" && c.hasYard) mode(t("theory.title"), t("theory.desc"), "theory");
  // the test place: every model of the game on the practice yard
  if (kind != "tram" && kind != "plane" && c.hasYard) mode(t("showTitle"), t("showDesc"), "show");
  const auto rank = [](const std::string& k) { return k == "yard" ? 0 : k == "exam" ? 2 : 1; };
  std::vector<const LessonDef*> lessons;
  for (const LessonDef& l : LESSONS) {
    const bool forKind = l.vehicles ? std::find(l.vehicles->begin(), l.vehicles->end(), kind) != l.vehicles->end()
                         : l.vehicle ? *l.vehicle == kind
                                     : kind != "tram" && kind != "pedestrian" && kind != "plane" && kind != "forklift" &&
                                           !(l.notFor && std::find(l.notFor->begin(), l.notFor->end(), kind) != l.notFor->end());
    if (forKind && (l.kind != "yard" || c.hasYard)) lessons.push_back(&l);
  }
  std::stable_sort(lessons.begin(), lessons.end(), [&rank](const LessonDef* a, const LessonDef* b) { return rank(a->kind) < rank(b->kind); });
  for (const LessonDef* l : lessons) mode(lessonTitle(l->id, c.lang), l->id == "exam" ? t("examDesc") : lessonIntro(l->id, c.lang), l->id);
  rows.append(row("modes-" + kind, t("start"), modes));
  if (!c.learn.empty()) {
    std::vector<El> learn;
    for (const CarouselContext::Learn& l : c.learn) learn.push_back(card("cm-mode", false, {el("b", "", l.title), el("small", "", l.desc)}, l.run));
    rows.append(row("learn", t("learn.title"), learn));
  }

  root.append(top, rows);
  if (preview) c.preview(*preview);
  return root;
}

}  // namespace fs

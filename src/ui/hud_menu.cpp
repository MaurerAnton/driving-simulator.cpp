// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/hud.ts @ v1.7.7
/** The Hud's pages: the main menu, what's new, about, the pause menu, the result, the log of faults. */
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/date.h"
#include "core/js.h"
#include "game/nav.h"
#include "game/releases.h"
#include "i18n/violations.h"
#include "platform/intl.h"
#include "platform/plat.h"
#include "ui/addresspicker.h"
#include "ui/closex.h"
#include "ui/hud.h"
#include "ui/hud_parts.h"
#include "ui/learn.h"

namespace fs {

using dom::El;
using dom::el;
using hudParts::html;
using hudParts::run;

namespace {

/** `String(i)` of a place in a list */
std::string indexText(size_t i) { return std::to_string(i); }

/** `list[Number(text)]`: the place a select's value names (-1: none) */
int indexOf(const std::string& text, size_t size) {
  const double i = jsNumber(text);
  if (!(i >= 0) || i >= static_cast<double>(size) || i != std::floor(i)) return -1;
  return static_cast<int>(i);
}

/** what the main menu's rows choose between them */
struct MenuState {
  int spawn = 0;
  std::optional<AddressHit> address;
  std::optional<TramStopChoice> stop;
  std::optional<RunwayChoice> runway;
};

}  // namespace

namespace hudParts {

/**
 * `new Date(text)` for the moments the releases are written as: "2026-11-21T19:27:22Z" (UTC) and
 * "2026-11-23T12:00:00" (the reader's own time) – NaN for anything else.
 */
double dateOf(const std::string& text) {
  int year = 0, month = 0, day = 0, hours = 0, minutes = 0, seconds = 0;
  char zone = 0;
  const int n = std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%c", &year, &month, &day, &hours, &minutes, &seconds, &zone);
  if (n < 6) return JS_NAN;
  const double utc = utcMs(year, month - 1, day, hours, minutes, seconds);
  if (n == 7 && zone == 'Z') return utc;
  // (local time: the moment that reads so on the reader's clock)
  return utc + plat::timezoneOffsetMin(utc + plat::timezoneOffsetMin(utc) * 60000) * 60000;
}

}  // namespace hudParts

using hudParts::dateOf;

/** `load: () => this.loadAddresses()` as the address field takes it: the search of the list, once it is there */
static Task<AddressSearchOf<AddressHit>> searchOf(std::function<Task<AddressIndex*>()> load) {
  AddressIndex* index = co_await load();
  if (!index) co_return nullptr;
  co_return [index](const std::string& query, size_t limit, const std::optional<AddressIndex::Near>& near) { return index->search(query, limit, near); };
}

void Hud::showMenu(const std::vector<SpawnJson>& spawnList) {
  if (settings.menuStyle == "stage" && showStage) {
    run(showStage, spawnList);
    return;
  }
  if (settings.menuStyle == "carousel" && showCarousel) {
    run(showCarousel, spawnList);
    return;
  }
  run(onMenuPreview, nullptr);
  // (the pages opened from here come back to this menu: they keep the start points)
  const auto spawns = std::make_shared<const std::vector<SpawnJson>>(spawnList);
  const std::function<void()> again = [this, spawns] { showMenu(*spawns); };
  El o = el("div", "overlay");
  El p = el("div", "panel");
  // the language first: whoever cannot read the page must be able to find it
  p.append(languagePicker(again));
  p.append(hudParts::title(), el("p", "", t("subtitle")));
  // where the last drive began – also after the page was closed
  Settings::LastStart& last = settings.lastStart;
  const auto st = std::make_shared<MenuState>();
  st->spawn = std::min(last.spawn, std::max(0, static_cast<int>(spawns->size()) - 1));
  const std::function<void()> remember = [this, st] {
    settings.lastStart.spawn = st->spawn;
    settings.lastStart.address = st->address;
    saveSettings(settings);
  };

  El opts = el("div");
  El rowSpawn = el("div", "row");
  El selSpawn = el("select");
  // (a large map has hundreds of them: grouped by town, in the order the map lists them)
  {
    El group;
    std::string groupLabel;
    for (size_t i = 0; i < spawns->size(); i++) {
      const SpawnJson& s = (*spawns)[i];
      El op = el("option");
      op.value(indexText(i));
      op.textContent(s.name);
      if (!s.g.empty() && (!group || groupLabel != s.g)) {
        group = el("optgroup");
        group.set("label", s.g);
        groupLabel = s.g;
        selSpawn.append(group);
      } else if (s.g.empty()) group = El();
      (group ? group : selSpawn).append(op);
    }
  }
  selSpawn.value(std::to_string(st->spawn));
  st->address = last.address;
  El atAddress = el("option");
  atAddress.value("address");
  if (st->address) {
    atAddress.textContent(st->address->label);
    selSpawn.append(atAddress);
    selSpawn.value("address");
  }
  selSpawn.onchange([selSpawn, st, remember] {
    if (selSpawn.value() == "address") return;
    st->spawn = static_cast<int>(jsNumber(selSpawn.value()));
    st->address = std::nullopt;
    remember();
  });
  rowSpawn.append(el("label", "", t("startPoint")), selSpawn);
  // the tram starts at one of its stops: those of the map, by town (the car parks above are for road vehicles)
  const VehicleDef* chosen = VEHICLES.find(settings.vehicle);
  const VehicleDef& def = chosen ? *chosen : VEHICLES.at("car");
  const bool tramMode = def.kind == "tram";
  const bool planeMode = def.kind == "plane";
  El rowStop = el("div", "row");
  if (planeMode) {
    // the aeroplane begins at the end of a runway of one of the map's airfields
    El sel = el("select");
    if (!runways || runways->empty()) {
      El op = el("option");
      op.textContent(runways ? "–" : t("realityLoading"));
      op.disabled(true);
      sel.append(op);
    } else {
      const auto list = std::make_shared<const std::vector<RunwayChoice>>(*runways);
      El g;
      std::string label;
      for (size_t i = 0; i < list->size(); i++) {
        const RunwayChoice& r = (*list)[i];
        if (!g || label != r.field) {
          g = el("optgroup");
          g.set("label", r.field);
          label = r.field;
          sel.append(g);
        }
        El op = el("option");
        op.value(indexText(i));
        op.textContent(t("runwayEnd", {{"end", r.end}}));
        g.append(op);
      }
      auto key = [](const RunwayChoice& r) { return r.field + "|" + r.end; };
      size_t at = 0;
      for (size_t i = 0; i < list->size(); i++)
        if (last.runway && key((*list)[i]) == *last.runway) {
          at = i;
          break;
        }
      st->runway = (*list)[at];
      sel.value(indexText(at));
      sel.onchange([this, sel, st, list, key] {
        const int i = indexOf(sel.value(), list->size());
        if (i >= 0) st->runway = (*list)[static_cast<size_t>(i)];
        else st->runway = std::nullopt;
        if (st->runway) settings.lastStart.runway = key(*st->runway);
        else settings.lastStart.runway = std::nullopt;
        saveSettings(settings);
      });
    }
    rowStop.append(el("label", "", t("startPoint")), sel);
  } else if (tramMode) {
    El selStop = el("select");
    if (!tramStops || tramStops->empty()) {
      El op = el("option");
      op.textContent(tramStops ? "–" : t("realityLoading"));
      op.disabled(true);
      selStop.append(op);
    } else {
      const auto stops = std::make_shared<const std::vector<TramStopChoice>>(*tramStops);
      El g;
      std::string label;
      for (size_t i = 0; i < stops->size(); i++) {
        const TramStopChoice& s = (*stops)[i];
        El op = el("option");
        op.value(indexText(i));
        op.textContent(s.name);
        if (!g || label != s.town) {
          g = el("optgroup");
          g.set("label", s.town);
          label = s.town;
          selStop.append(g);
        }
        g.append(op);
      }
      // (the stop of the last drive – else one in the middle of Kassel)
      auto key = [](const TramStopChoice& s) { return s.name + "|" + s.town; };
      int at = -1;
      for (size_t i = 0; i < stops->size(); i++)
        if (last.tramStop && key((*stops)[i]) == *last.tramStop) {
          at = static_cast<int>(i);
          break;
        }
      if (at < 0) {
        for (size_t i = 0; i < stops->size(); i++)
          if ((*stops)[i].name == "Königsplatz") {
            at = static_cast<int>(i);
            break;
          }
        at = std::max(0, at);
      }
      st->stop = (*stops)[static_cast<size_t>(at)];
      selStop.value(std::to_string(at));
      selStop.onchange([this, selStop, st, stops, key] {
        const int i = indexOf(selStop.value(), stops->size());
        if (i >= 0) st->stop = (*stops)[static_cast<size_t>(i)];
        else st->stop = std::nullopt;
        if (st->stop) settings.lastStart.tramStop = key(*st->stop);
        else settings.lastStart.tramStop = std::nullopt;
        saveSettings(settings);
      });
    }
    rowStop.append(el("label", "", t("startPoint")), selStop);
  }
  // … or anywhere: street and house number
  El rowAddr = el("div", "row");
  const AddressPicker picker = addressPicker(AddressPickerOptions<AddressHit>{
      .placeholder = t("addressPlaceholder"),
      .loading = t("realityLoading"),
      .none = t("addressNone"),
      .load = [this] { return searchOf(loadAddresses); },
      .pick =
          [st, atAddress, selSpawn, remember](const AddressHit& hit) {
            st->address = hit;
            atAddress.textContent(hit.label);
            if (!atAddress.parentElement()) selSpawn.append(atAddress);
            selSpawn.value("address");
            remember();
          },
      // (every town has its Marktplatz: the one near the start point chosen comes first)
      .near = [spawns, st]() -> std::optional<AddressIndex::Near> {
        if (st->spawn < 0 || static_cast<size_t>(st->spawn) >= spawns->size()) return std::nullopt;
        const SpawnJson& s = (*spawns)[static_cast<size_t>(st->spawn)];
        return AddressIndex::Near{s.x, s.z};
      },
  });
  rowAddr.append(el("label", "", t("startAddress")), picker.node);
  El rowTr = el("div", "row");
  El selTr = el("select");
  for (const auto& [v, k] : std::vector<std::pair<std::string, std::string>>{{"assist", "trAssist"}, {"manual", "trManual"}, {"automatic", "trAutomatic"}}) {
    El op = el("option");
    op.value(v);
    op.textContent(t(k));
    selTr.append(op);
  }
  selTr.value(settings.transmission);
  selTr.onchange([this, selTr] {
    settings.transmission = selTr.value();
    saveSettings(settings);
  });
  rowTr.append(el("label", "", t("transmission")), selTr);
  // the vehicle of a licence class – the truck with its trailer (class CE) with lessons of its own
  const bool truck = def.model != "car" && def.model != "motorcycle";
  El rowVeh = el("div", "row");
  El selVeh = el("select");
  for (const VehicleKind& v : VEHICLE_KINDS) {
    // (a vehicle that is not in this version is not offered)
    const VehicleDef* d = VEHICLES.find(v);
    if (!d) continue;
    El op = el("option");
    op.value(v);
    op.textContent(t(d->name));
    selVeh.append(op);
  }
  selVeh.value(settings.vehicle);
  selVeh.onchange([this, selVeh, again] {
    settings.vehicle = selVeh.value();
    saveSettings(settings);
    again();
  });
  rowVeh.append(el("label", "", t("vehicle")), selVeh);
  // (trucks, buses and tractors have automated gearboxes, as nearly all of them today)
  if (tramMode || planeMode) opts.append(rowStop, rowVeh);
  else opts.append(rowSpawn, rowAddr, rowVeh);
  if (tramMode) {
    // the tram: one unit, two coupled, or one with a trailer car
    El rowSet = el("div", "row");
    El selSet = el("select");
    for (const auto& [v, k] : std::vector<std::pair<std::string, std::string>>{{"single", "tramSetSingle"}, {"double", "tramSetDouble"}, {"trailer", "tramSetTrailer"}}) {
      El op = el("option");
      op.value(v);
      op.textContent(t(k));
      selSet.append(op);
    }
    selSet.value(settings.tramSet);
    selSet.onchange([this, selSet] {
      settings.tramSet = selSet.value();
      saveSettings(settings);
    });
    rowSet.append(el("label", "", t("tramSet")), selSet);
    opts.append(rowSet);
  }
  // its engine: the powers a vehicle of its class may have, each kind of vehicle with its own
  if (def.powers.size() > 1) {
    El rowPw = el("div", "row");
    El selPw = el("select");
    for (const double ps : def.powers) {
      El op = el("option");
      op.value(jsStr(ps));
      op.textContent(jsStr(ps) + " PS (" + jsStr(jsRound(ps * 0.7355)) + " kW)");
      selPw.append(op);
    }
    selPw.value(jsStr(powerOf(def, settings.powers)));
    selPw.onchange([this, selPw, kind = def.kind] {
      settings.powers[kind] = jsNumber(selPw.value());
      saveSettings(settings);
    });
    rowPw.append(el("label", "", t("enginePower")), selPw);
    opts.append(rowPw);
  }
  if (!truck) opts.append(rowTr);
  p.append(opts);

  p.append(el("h2", "", t("start")));
  El cards = el("div", "cards");
  auto card = [this, cards, st, planeMode, tramMode](const std::string& title, const std::string& desc, const std::string& mode) {
    El c = el("button", "card");
    El b = el("b");
    b.textContent(title);
    El d = el("small");
    d.textContent(desc);
    c.append(b, d);
    c.onclick([this, st, planeMode, tramMode, mode] {
      StartRequest req{.mode = mode, .spawn = st->spawn};
      if (planeMode) req.runway = st->runway;
      else if (tramMode) req.stop = st->stop;
      else req.address = st->address;
      run(onStart, req);
    });
    cards.append(c);
  };
  card(t(def.kind == "plane" ? "freeFly" : "free"), t(def.kind == "tram" ? "freeTramDesc" : def.kind == "pedestrian" ? "freeWalkDesc" : def.kind == "plane" ? "freeFlyDesc" : "freeDesc"), "free");
  // the forklift: its work on the forklift yard (game/forkliftplay.ts) – its free drive begins at the place chosen above
  if (def.kind == "forklift") card(t("forkWork"), t("forkWorkDesc"), "forklift");
  // (the drives the car does by itself, and the tours, are on the roads – and by car)
  if (def.kind == "tram") card(t("demo"), t("demoTramDesc"), "demo");
  if (def.kind == "plane") card(t("demoFly"), t("demoFlyDesc"), "demo");
  if (def.kind != "tram" && def.kind != "pedestrian" && def.kind != "plane" && def.kind != "forklift") {
    card(t("demo"), t("demoDesc"), "demo");
    card(t("tourTitle"), t("tourDesc"), "tour");
  }
  // the theory lessons: difficult situations shown on the practice yard (game/theorymode.ts)
  if (def.kind != "plane" && world.json.yard) card(t("theory.title"), t("theory.desc"), "theory");
  // (the test place is not in this version: no card for it)
  // the lessons of the chosen vehicle: the practice yard first, the test last
  auto rank = [](const std::string& kind) { return kind == "yard" ? 0 : kind == "exam" ? 2 : 1; };
  const VehicleKind& kind = def.kind;
  const bool hasYard = world.json.yard.has_value();
  auto has = [&kind](const std::vector<VehicleKind>& list) { return std::find(list.begin(), list.end(), kind) != list.end(); };
  std::vector<const LessonDef*> lessons;
  for (const LessonDef& l : LESSONS) {
    const bool forKind = l.vehicles  ? has(*l.vehicles)
                         : l.vehicle ? *l.vehicle == kind
                                     : kind != "tram" && kind != "pedestrian" && kind != "plane" && kind != "forklift" && !(l.notFor && has(*l.notFor));
    if (forKind && (l.kind != "yard" || hasYard)) lessons.push_back(&l);
  }
  std::stable_sort(lessons.begin(), lessons.end(), [&rank](const LessonDef* a, const LessonDef* b) { return rank(a->kind) < rank(b->kind); });
  // (the lessons on the road were written for the car: what is different with this vehicle is said first)
  const std::string note = def.caps         ? t("lesson.capNote", {{"vehicle", t(def.name)}, {"rural", def.caps->rural}, {"motorway", def.caps->motorway}}) + " "
                           : def.noMotorway ? t("lesson.slowNote", {{"vehicle", t(def.name)}, {"max", def.governed}}) + " "
                                            : "";
  for (const LessonDef* l : lessons) card(lessonTitle(l->id, lang()), l->id == "exam" ? t("examDesc") : (l->kind == "route" ? note : "") + lessonIntro(l->id, lang()), l->id);
  p.append(cards);
  // the learning modes (ui/learn.ts): the theory exam, the sign trainer, spotting dangers, the lesson log
  {
    El learnHead = el("h2");
    learnHead.textContent(t("learn.title"));
    p.append(learnHead);
    El learn = el("div", "cards");
    for (const LearnMode& m : learnModes(again)) {
      El c = el("button", "card");
      El b = el("b");
      b.textContent(m.title);
      El d = el("small");
      d.textContent(m.desc);
      c.append(b, d);
      c.onclick(m.run);
      learn.append(c);
    }
    p.append(learn);
  }

  El b = el("div", "buttons");
  El bs = el("button", "btn ghost", t("settings"));
  bs.onclick([this, again] { showSettings(again); });
  El bc = el("button", "btn ghost", t("controls"));
  bc.onclick([this, again] { showControls(again); });
  El ba = el("button", "btn ghost", t("about"));
  ba.onclick([this, again] { showAbout(again); });
  El bn = el("button", "btn ghost", t("news"));
  bn.onclick([this, again] { showNews(again); });
  El bl = el("button", "btn ghost", t("logTitle"));
  bl.onclick([this, again] { showLog(again); });
  b.append(bs, bc, bl, bn, ba);
  p.append(b);
  p.append(el("p", "version", t("newsVersion", {{"v", VERSION}})));
  o.append(p);
  setOverlay(o);
  menuRedraw = again;
}

void Hud::showLearn(const std::string& screen, std::function<void()> back) {
  openLearn(LearnContext{.t = tFn(), .lang = lang(), .screen = screen, .show = [this](const El& page) { setOverlay(page); }, .back = std::move(back), .signBase = plat::locationBase() + "signs/"}).detach();
}

std::vector<Hud::LearnMode> Hud::learnModes(std::function<void()> back) {
  auto mode = [this, back](const std::string& id, const std::string& icon) {
    return LearnMode{"learn-" + id, t("learn." + id), t("learn." + id + "Desc"), icon, [this, id, back] { showLearn(id, back); }};
  };
  return {
      mode("exam", "<rect x=\"5\" y=\"4.5\" width=\"14\" height=\"17\" rx=\"2\"/><path d=\"M9 4.5V2.8h6v1.7M8.5 10h7M8.5 14h7M8.5 18h4\"/>"),
      mode("signs", "<path d=\"M12 2.5l9.5 16.5h-19z\"/><path d=\"M12 9v4.5M12 16.5h.01\"/>"),
      mode("danger", "<circle cx=\"11\" cy=\"11\" r=\"6.5\"/><path d=\"M16 16l5 5M11 8v3.5M11 14h.01\"/>"),
      mode("log", "<rect x=\"4.5\" y=\"3\" width=\"15\" height=\"18\" rx=\"2\"/><path d=\"M8 8l1.3 1.3L12 6.7M8 13l1.3 1.3L12 11.7M14 8h2.5M14 13h2.5M8 17.5h8.5\"/>"),
  };
}

void Hud::redrawMenu() {
  // (a copy: drawing the menu puts another `menuRedraw` in its place)
  const std::function<void()> redraw = menuRedraw;
  if (redraw) redraw();
}

void Hud::menuStartAt(const SpawnJson& spot) {
  if (!startFromSpot) return;
  const VehicleDef* chosen = VEHICLES.find(settings.vehicle);
  const VehicleKind kind = chosen ? chosen->kind : VEHICLES.at("car").kind;
  if (startFromSpot(kind, settings.lastStart, spot, tramStops ? &*tramStops : nullptr, runways ? &*runways : nullptr, t("mapPlace"))) saveSettings(settings);
}

/**
 * The list of languages, each under its own name.  Choosing one fetches its
 * texts and draws the current page again.
 */
El Hud::languagePicker(std::function<void()> redraw, bool inRow) {
  El sel = el("select", inRow ? "" : "lang");
  sel.setAttribute("aria-label", "Language · Sprache");
  for (const LangInfo& l : LANGS) {
    El op = el("option");
    op.value(l.code);
    op.textContent(l.name);
    op.lang(l.code);
    sel.append(op);
  }
  sel.value(settings.lang);
  sel.onchange([this, sel, redraw = std::move(redraw)] { pickLanguage(sel.value(), redraw).detach(); });
  return sel;
}

Task<void> Hud::pickLanguage(std::string code, std::function<void()> redraw) {
  const bool ok = co_await loadLang(code);
  if (ok) {
    settings.lang = code;
    saveSettings(settings);
    run(onSettingsChanged);
  }
  redraw();
}

std::vector<El> Hud::keycaps(const std::vector<std::string>& codes) {
  std::vector<El> out;
  for (const std::string& c : codes) {
    El k = el("kbd");
    k.textContent(keyName(c, lang()));
    out.push_back(k);
  }
  return out;
}

void Hud::showNews(std::function<void()> back) {
  El o = el("div", "overlay");
  El p = el("div", "panel news");
  p.append(closeX(t("back"), back), el("h1", "", t("news")));
  const std::string locale = localeOf(lang());
  const plat::DateTimeFormat date{.day = "numeric", .month = "long", .year = "numeric"};
  // (the moment of the release, where it is known: in the reader's time zone)
  const plat::DateTimeFormat dateTime{.day = "numeric", .month = "long", .year = "numeric", .hour = "numeric", .minute = "2-digit"};
  El devPart;
  for (const Release& r : RELEASES) {
    // the development versions (before 1.0.0): a part of their own below the releases, set apart by a line
    if (r.dev && !devPart) {
      devPart = el("div", "news-dev");
      devPart.append(el("h2", "news-dev-title", t("news.dev")), el("p", "note", t("news.devIntro")));
      p.append(devPart);
    }
    const El card = devPart ? devPart : p;
    El head = el("h2", "", t("newsVersion", {{"v", r.version}}) + " · " + (!r.at.empty() ? plat::formatDate(dateOf(r.at), locale, dateTime) : plat::formatDate(dateOf(r.date + "T12:00:00"), locale, date)));
    // how big the game was then (tools/release-stats.ts)
    if (r.size) {
      const std::vector<std::pair<std::string, std::string>> w = sizeWords(*r.size, locale);
      bool maps = false;
      for (const auto& [name, value] : w)
        if (name == "maps" && !value.empty()) maps = true;
      head.append(el("span", "news-size", t("news.size", w) + (maps ? " " + t("news.sizeMaps", w) : "")));
    }
    card.append(head);
    // every version can be played again (ui/versionpicker.ts) – the button shows where the site has it
    if (playVersionButton) {
      const El play = playVersionButton(r.version, tFn());
      if (play) card.append(play);
    }
    if (r.intro) card.append(el("p", "", t(*r.intro)));
    El list = el("ul");
    for (const std::string& key : r.items) list.append(el("li", "", t(key)));
    card.append(list);
  }
  El b = el("div", "buttons");
  El ok = el("button", "btn", t("back"));
  ok.onclick(back);
  b.append(ok);
  p.append(b);
  o.append(p);
  setOverlay(o);
}

void Hud::showAbout(std::function<void()> back) {
  El o = el("div", "overlay");
  El p = el("div", "panel");
  auto text = [this, p](const std::string& key) {
    El e = el("p");
    e.textContent(t(key));
    p.append(e);
  };
  p.append(closeX(t("back"), back), el("h1", "", t("about")));
  text("about.text");
  p.append(el("h2", "", t("credits")));
  text("about.dataOsm");
  text("about.dataHessen");
  text("about.timetables");
  text("about.signs");
  text("about.photos");
  p.append(el("h2", "", t("source")));
  p.append(html("p", "", "<a href=\"https://github.com/MaurerAnton/driving-simulator\" target=\"_blank\" rel=\"noopener\">github.com/MaurerAnton/driving-simulator</a>"));
  El b = el("div", "buttons");
  El bb = el("button", "btn", t("back"));
  bb.onclick(back);
  b.append(bb);
  p.append(b);
  o.append(p);
  setOverlay(o);
}

void Hud::showIntro(const std::string& title, const std::string& text, std::function<void()> go) {
  El o = el("div", "overlay");
  El p = el("div", "panel narrow");
  p.append(el("h1", "", title), el("p", "", text));
  El b = el("div", "buttons");
  El bb = el("button", "btn", t("start"));
  bb.onclick(std::move(go));
  b.append(bb);
  p.append(b);
  o.append(p);
  setOverlay(o);
  bb.focus();
}

void Hud::showPause() {
  // Three layers: going on (big), what can be done in the world (a grid of tiles with icons), and the
  // menu's own things (a bar at the bottom: the settings first, the main menu last).
  El o = el("div", "overlay");
  El p = el("div", "panel pause");
  El head = el("div", "pause-head");
  head.append(el("h1", "", t("paused")));
  if (last) head.append(el("p", "pause-score", t("faults") + ": " + std::to_string(last->faults) + " · " + t("points") + ": " + std::to_string(last->points)));
  El r = el("button", "btn pause-resume", "▶  " + t("resume"));
  r.onclick([this] { run(onResume); });
  El grid = el("div", "pause-grid");
  auto tile = [grid](const std::string& icon, const std::string& label, std::function<void()> act, const std::string& cls = "") {
    El tl = el("button", "pause-tile" + (!cls.empty() ? " " + cls : ""));
    tl.type("button");
    tl.append(el("span", "pause-icon", icon), el("span", "pause-label", label));
    tl.onclick(std::move(act));
    grid.append(tl);
  };
  tile("🗺️", t("mapTitle"), [this] { run(onMap); });
  if (dom::root().flag("requestFullscreen")) {
    const bool full = static_cast<bool>(dom::fullscreenElement());
    tile(full ? "🗗" : "⛶", t(full ? "view.fullscreenOff" : "view.fullscreen"), [this] {
      if (view) run(view->toggleFullscreen);
    });
  }
  // (the reality view is not in this version: its tile shows once the game offers it)
  if (onReality) tile("📷", t("realityButton"), [this] { run(onReality); });
  if (canTeleport) {
    tile("📍", t("teleport"), [this] { showTeleport(); });
    // … or have the car drive somewhere by itself and watch how it is done
    // (in the aeroplane the same button switches its autopilot)
    tile("🧭", t(root.closest(".flying") ? "tbAutopilot" : "demo"), [this] { run(onDemo); });
    // another vehicle, here and now (the world stays as it is)
    tile("🔁", t("changeVehicle"), [this] { showVehiclePicker(); });
    // on foot: the walker follows the player's real position (the browser asks first)
    if (liveOffer) tile("📡", t(liveOn ? "liveLocationOff" : "liveLocation"), [this] { run(onLiveLocation); }, liveOn ? "on" : "");
    // the arcade mode: the world at another moment, the time faster while the autopilot drives
    if (arcade && arcadePanel) tile("🕹️", t("arcadeTitle"), [this] { showArcade(); });
  }
  if (onReplay) {
    const std::function<void()> go = onReplay;
    tile("⏪", t("replay"), go);
  }
  if (onGetOut) {
    const std::function<void()> go = onGetOut;
    tile("🚪", t("getOut"), go);
  }
  // playing together on a server (ui/netpanels.ts)
  if (onTogether) {
    const std::function<void()> go = onTogether;
    tile("👥", t("net.together"), go);
  }
  // the parts of the display moved where they suit (ui/hudlayout.ts): over the picture, back in the drive
  if (hud.style("display") != "none")
    tile("✥", t("hudLayout.arrange"), [this] {
      run(onResume);
      layout.arrange();
    });
  El bar = el("div", "pause-bar");
  auto small = [bar](const std::string& icon, const std::string& label, std::function<void()> act, const std::string& cls = "") {
    El b = el("button", "btn ghost pause-small" + (!cls.empty() ? " " + cls : ""), icon + " " + label);
    b.onclick(std::move(act));
    bar.append(b);
    return b;
  };
  small("⚙️", t("settings"), [this] { showSettings([this] { showPause(); }); });
  small("⌨️", t("controls"), [this] { showControls([this] { showPause(); }); });
  small("📝", t("logTitle"), [this] { showLog([this] { showPause(); }); });
  // a free drive ends when the driver says so: the result, then the main menu
  if (canFinish) small("🏁", t("endDrive"), [this] { run(onFinish); });
  small("🏠", t("menu"), [this] { run(onQuit); }, "pause-quit");
  p.append(head, r, grid, bar);
  o.append(p);
  setOverlay(o);
  r.focus();
}

void Hud::showTeleport() {
  El o = el("div", "overlay");
  El p = el("div", "panel narrow");
  p.append(el("h1", "", t("teleport")), el("p", "", t("teleportText")));
  const AddressPicker picker = addressPicker(AddressPickerOptions<AddressHit>{
      .placeholder = t("addressPlaceholder"),
      .loading = t("realityLoading"),
      .none = t("addressNone"),
      .load = [this] { return searchOf(loadAddresses); },
      .pick = [this](const AddressHit& hit) { run(onTeleport, hit); },
      .near = [this] { return where(); },
  });
  p.append(picker.node);
  El b = el("div", "buttons");
  El back = el("button", "btn ghost", t("back"));
  back.onclick([this] { showPause(); });
  b.append(back);
  p.append(b);
  o.append(p);
  setOverlay(o);
  picker.input.focus();
}

void Hud::showResult(const ResultSummary& r) {
  El o = el("div", "overlay");
  El p = el("div", "panel");
  p.append(el("h1", "", t("result") + ": " + r.title));
  if (r.exam) p.append(el("div", std::string("verdict ") + (r.passed ? "ok" : "bad"), t(r.passed ? "passed" : "failed")));
  const std::string km = formatUnit(r.distanceKm, Unit::Kilometer, lang(), 1);
  const std::string min = formatUnit(jsRound(r.minutes), Unit::Minute, lang());
  p.append(el("p", "", t("distance") + ": " + km + " · " + t("duration") + ": " + min + " · " + t("points") + ": " + std::to_string(r.points)));
  if (r.extra) p.append(r.extra);
  if (r.log.empty()) p.append(el("p", "", t("noFaults")));
  // group identical faults
  struct Group {
    const Violation* v = nullptr;
    int n = 0;
  };
  std::vector<Group> groups;
  for (const Violation& v : r.log) {
    Group* g = nullptr;
    for (Group& have : groups)
      if (have.v->id == v.id) g = &have;
    if (g) g->n++;
    else groups.push_back(Group{&v, 1});
  }
  auto order = [](const std::string& severity) { return severity == "fail" ? 0 : severity == "major" ? 1 : severity == "minor" ? 2 : 3; };
  std::stable_sort(groups.begin(), groups.end(), [&order](const Group& a, const Group& b) { return order(a.v->severity) < order(b.v->severity); });
  for (const Group& g : groups) {
    El f = el("div", "fault " + g.v->severity);
    El em = el("em", "", t(g.v->severity) + " · ");
    em.append(ruleNode(g.v->id));
    f.append(el("b", "", violationTitle(g.v->id, lang(), hudParts::params(g.v->data)) + (g.n > 1 ? " (" + std::to_string(g.n) + "×)" : "")), el("small", "", violationWhy(g.v->id, lang())), em);
    p.append(f);
  }
  El b = el("div", "buttons");
  El q = el("button", "btn", t("menu"));
  q.onclick([this] { run(onQuit); });
  El log = el("button", "btn ghost", t("logTitle"));
  {
    // (the result to come back to: kept beside the page – what it shows more, `extra`, is of the page)
    ResultSummary kept = r;
    kept.extra = El();
    const auto summary = std::make_shared<const ResultSummary>(std::move(kept));
    log.onclick([this, summary, extra = r.extra] {
      showLog([this, summary, extra] {
        ResultSummary again = *summary;
        again.extra = extra;
        showResult(again);
      });
    });
  }
  b.append(q, log);
  if (onReplay) {
    El rp = el("button", "btn ghost", t("replay"));
    const std::function<void()> go = onReplay;
    rp.onclick(go);
    b.append(rp);
  }
  p.append(b);
  o.append(p);
  setOverlay(o);
}

/**
 * The rule a fault refers to.  A citation of the StVO is written as it is
 * in German law (left to right in every language), and each paragraph
 * opens its official text in a new tab.
 */
El Hud::ruleNode(const std::string& id) {
  const std::string* found = VIOLATION_REFS.find(id);
  const std::string ref = found ? *found : "";
  El node = el("span", "rule");
  if (startsWith(ref, "@") || ref.empty()) {
    node.textContent(violationRef(id, lang()));
    return node;
  }
  node.dir("ltr");
  for (const CitationPart& part : citationParts(ref)) {
    if (!part.url) {
      node.append(part.text);
      continue;
    }
    El a = el("a");
    a.href(*part.url);
    a.set("target", "_blank");
    a.set("rel", "noopener noreferrer");
    a.textContent(part.text);
    a.title(t("logRule"));
    node.append(a);
  }
  return node;
}

void Hud::showLog(std::function<void()> back) {
  El o = el("div", "overlay");
  El p = el("div", "panel");
  p.append(el("h1", "", t("logTitle")));
  static const std::vector<FaultEntry> none;
  const std::vector<FaultEntry>& entries = faultLog ? faultLog->entries : none;
  if (entries.empty()) p.append(el("p", "", t("logEmpty")));
  else p.append(el("p", "", t("logText")));
  const std::string locale = localeOf(lang());
  const plat::DateTimeFormat when{.dateStyle = "medium", .timeStyle = "short", .numberingSystem = "latn", .calendar = "gregory"};
  El list = el("div", "log");
  for (size_t i = entries.size(); i-- > 0;) {
    const FaultEntry& e = entries[i];
    El f = el("div", "fault " + e.severity);
    std::vector<std::string> parts;
    for (const std::string& part : {plat::formatDate(e.t, locale, when), e.street, e.mode == "free" ? t("free") : e.mode == "exam" ? t("exam") : lessonTitle(e.mode, lang())})
      if (!part.empty()) parts.push_back(part);
    const std::string where = join(parts, " · ");
    El em = el("em", "", t(e.severity) + " · ");
    em.append(ruleNode(e.id));
    f.append(el("small", "when", where), el("b", "", violationTitle(e.id, lang(), hudParts::params(e.data))), el("small", "", violationWhy(e.id, lang())), em);
    list.append(f);
  }
  p.append(list);
  El b = el("div", "buttons");
  El bb = el("button", "btn", t("back"));
  bb.onclick(back);
  b.append(bb);
  if (!entries.empty()) {
    El clear = el("button", "btn ghost", t("logClear"));
    clear.onclick([this, back] {
      if (faultLog) faultLog->clear();
      showLog(back);
    });
    b.append(clear);
  }
  p.append(b);
  o.append(p);
  setOverlay(o);
  bb.focus();
}

void Hud::showVehiclePicker() {
  El o = el("div", "overlay");
  El p = el("div", "panel narrow");
  p.append(el("h1", "", t("changeVehicle")));
  El grid = el("div", "veh-pick");
  for (const VehicleKind& k : VEHICLE_KINDS) {
    // (a vehicle that is not in this version is not offered)
    const VehicleDef* d = VEHICLES.find(k);
    if (!d) continue;
    El b = el("button", std::string("btn ghost") + (k == settings.vehicle ? " on" : ""), t(d->name));
    b.type("button");
    b.onclick([this, k] { run(onChangeVehicle, k); });
    grid.append(b);
  }
  El back = el("button", "btn", t("resume"));
  back.onclick([this] { showPause(); });
  p.append(grid, back);
  o.append(p);
  setOverlay(o);
}

void Hud::showArcade() {
  if (!arcade || !arcadePanel) return;
  El o = el("div", "overlay");
  o.append(arcadePanel(*arcade));
  setOverlay(o);
}

}  // namespace fs

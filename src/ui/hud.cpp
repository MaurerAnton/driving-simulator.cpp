// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/hud.ts @ v1.7.7
/** The Hud: its overlays' plumbing, the loading screen, and the display while driving. */
#include "ui/hud.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "game/netstats.h"
#include "i18n/unicode.h"
#include "input/input.h"
#include "platform/plat.h"
#include "sim/signals.h"
#include "ui/hud_parts.h"
#include "ui/minimap.h"

namespace fs {

using dom::El;
using dom::el;

static const std::unordered_map<std::string, std::string> ICON = {
    {"yield", "<svg viewBox=\"0 0 100 90\"><path d=\"M3 3h94L50 87z\" fill=\"#d0141e\"/><path d=\"M20 13h60L50 67z\" fill=\"#fff\"/></svg>"},
    {"stop",
     "<svg viewBox=\"0 0 100 100\"><path d=\"M30 2h40l28 28v40L70 98H30L2 70V30z\" fill=\"#d0141e\" stroke=\"#fff\" stroke-width=\"4\"/><text x=\"50\" y=\"62\" text-anchor=\"middle\" font-size=\"30\" font-weight=\"800\" fill=\"#fff\" "
     "font-family=\"sans-serif\">STOP</text></svg>"},
    {"priority", "<svg viewBox=\"0 0 100 100\"><path d=\"M50 3l47 47-47 47L3 50z\" fill=\"#fff\" stroke=\"#222\" stroke-width=\"2\"/><path d=\"M50 20l30 30-30 30-30-30z\" fill=\"#f6c817\"/></svg>"},
    {"rbl", "<svg viewBox=\"0 0 100 90\"><path d=\"M50 3l47 84H3z\" fill=\"#d0141e\"/><path d=\"M50 22l31 55H19z\" fill=\"#fff\"/><path d=\"M38 70l24-26M62 70L38 44\" stroke=\"#111\" stroke-width=\"7\"/></svg>"},
    {"minor", "<svg viewBox=\"0 0 100 90\"><path d=\"M3 3h94L50 87z\" fill=\"#888\"/><path d=\"M20 13h60L50 67z\" fill=\"#fff\"/></svg>"},
    {"roundabout",
     "<svg viewBox=\"0 0 100 100\"><circle cx=\"50\" cy=\"50\" r=\"47\" fill=\"#1b5fa8\" stroke=\"#fff\" stroke-width=\"3\"/><path d=\"M50 22a28 28 0 1 1-26 18\" fill=\"none\" stroke=\"#fff\" stroke-width=\"9\"/><path d=\"M14 36l12 12 "
     "8-18z\" fill=\"#fff\"/></svg>"},
};

/** `ICON[key] ?? ''` */
static std::string iconOf(const std::string& key) {
  const auto it = ICON.find(key);
  return it == ICON.end() ? std::string() : it->second;
}

namespace hudParts {

El html(const char* tag, const std::string& cls, const std::string& markup) {
  El e = el(tag, cls);
  if (!markup.empty()) e.innerHTML(markup);
  return e;
}

El title() { return html("h1", "", "Fahrsim <span>Baunatal</span>"); }

TrParams params(const ViolationData& data) {
  TrParams p;
  for (const auto& [name, value] : data) {
    if (const double* number = std::get_if<double>(&value)) p.add(TrParam(name, *number));
    else p.add(TrParam(name, std::get<std::string>(value)));
  }
  return p;
}

std::vector<std::string> words(const std::string& text) {
  // (`text.split(/\s+/).filter(Boolean)`: at the blanks of every script)
  std::vector<std::string> out;
  const std::u16string wide = i18n::toUtf16(text);
  std::u16string part;
  auto flush = [&] {
    if (!part.empty()) out.push_back(i18n::fromUtf16(part));
    part.clear();
  };
  for (const char16_t c : wide) {
    const bool space = c == u' ' || (c >= 0x09 && c <= 0x0d) || c == 0x00a0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000 || c == 0xfeff;
    if (space) flush();
    else part += c;
  }
  flush();
  return out;
}

}  // namespace hudParts

using hudParts::html;

Hud::Hud(const El& root, Settings& settings, World& world)
    : root(root),
      settings(settings),
      world(world),
      // (everything buildHud makes is declared before `hud`: it is there when this runs)
      hud(buildHud()),
      layout(hud, [this](const std::string& k) { return t(k); }, [this](const std::string& text) { message(text); }) {
  this->root.append(hud);
  setHudVisible(false);
}

Hud::~Hud() = default;

std::string Hud::t(std::string_view key, const TrParams& p) const { return tr(key, lang(), p); }

Hud::T Hud::tFn() const {
  return [this](const std::string& key, const TrParams& vars) { return t(key, vars); };
}

void Hud::gearbox(std::optional<Transmission> v) {
  _gearbox = std::move(v);
  // (a clutch pedal where there is a manual gearbox to use it on)
  if (clutchPedal) clutchPedal.hidden((_gearbox ? *_gearbox : settings.transmission) != "manual");
}

/**
 * Font size at which a word ("walk", "none" – in any language and script)
 * fits into the speed-limit roundel: it may wrap at spaces, the widest word
 * must fit the width and the lines the height.
 */
double Hud::fitRoundel(const std::string& text) {
  const double WIDTH = 43;
  const double HEIGHT = 38;
  if (!ruler) {
    // (`document.createElement('canvas')`: a canvas as the page makes it)
    rulerCanvas = Canvas(300, 150);
    ruler = rulerCanvas.getContext2D();
  }
  const Ctx2D& c = ruler;
  if (!c) return 11;
  std::string family = roundel.computedStyle("font-family");
  if (family.empty()) family = "sans-serif";
  const std::vector<std::string> words = hudParts::words(text);
  for (int px = 13; px > 7; px--) {
    c.font("800 " + std::to_string(px) + "px " + family);
    const double space = c.measureText(" ").width;
    int lines = 1;
    double line = 0;
    double widest = 0;
    for (const std::string& w : words) {
      const double width = c.measureText(w).width;
      widest = std::max(widest, width);
      if (line > 0 && line + space + width > WIDTH) {
        lines++;
        line = width;
      } else line += (line > 0 ? space : 0) + width;
    }
    if (widest <= WIDTH && lines * px * 1.1 <= HEIGHT) return px;
  }
  return 7;
}

// ---------------------------------------------------------------- overlays
void Hud::setRunways(std::vector<RunwayChoice> list) {
  runways = std::move(list);
  const VehicleDef* def = VEHICLES.find(settings.vehicle);
  // (a copy: drawing the menu again puts another `menuRedraw` in its place)
  const std::function<void()> redraw = menuRedraw;
  if (redraw && def && def->kind == "plane") redraw();
}

void Hud::setTramStops(std::vector<TramStopChoice> list) {
  tramStops = std::move(list);
  const VehicleDef* def = VEHICLES.find(settings.vehicle);
  const std::function<void()> redraw = menuRedraw;
  if (redraw && def && def->kind == "tram") redraw();
}

void Hud::showPanel(const El& node) { setOverlay(node); }

void Hud::setOverlay(const El& node) {
  menuRedraw = nullptr;
  overlay.remove();
  overlay = node;
  if (node) root.append(node);
}

void Hud::showLoading(const std::string& text) {
  loadShown = 0;
  El o = el("div", "overlay solid");
  El p = el("div", "panel narrow");
  El bar = el("div", "progress");
  bar.append(el("i"));
  El pct = el("small", "progress-pct", "");
  p.append(hudParts::title(), el("p", "", text), bar, pct);
  o.append(p);
  setOverlay(o);
  loadingBar = LoadingBar{bar, pct};
}

void Hud::setLoadingProgress(double share) {
  if (!loadingBar || !loadingBar->bar.isConnected()) return;
  const LoadingBar& l = *loadingBar;
  // (the share is put together from the roads and the tiles, which come at their own pace: it is shown only ever growing)
  loadShown = std::max(loadShown, std::max(0.0, std::min(1.0, share)));
  const std::string p = jsStr(jsRound(loadShown * 100));
  l.bar.addClass("known");
  l.bar.firstElementChild().style("width", p + "%");
  l.pct.textContent(p + " % · " + formatBytes(downloaded(), localeOf(lang())));
}

void Hud::setMenuLoading(std::optional<double> share) {
  const El e = root.querySelector(".cm-loading");
  if (!e) return;
  // (only ever growing while the place loads; null: done – the next place counts from 0)
  menuShown = !share ? 0 : std::max(menuShown, std::max(0.0, std::min(1.0, *share)));
  const std::string text = !share ? "" : t("loadingTiles") + " " + jsStr(jsRound(menuShown * 100)) + " % · " + formatBytes(downloaded(), localeOf(lang()));
  if (e.textContent() != text) e.textContent(text);
}

void Hud::showError(const std::string& text) {
  El o = el("div", "overlay solid");
  El p = el("div", "panel narrow");
  p.append(hudParts::title(), el("p", "", text));
  o.append(p);
  setOverlay(o);
}

void Hud::hideOverlay() { setOverlay(El()); }

// --------------------------------------------------------------------- HUD
El Hud::buildHud() {
  El h = el("div", "hud");
  navBox = el("div", "nav");
  navArrow = el("div", "arrow", "↑");
  El text = el("div", "text");
  navText = el("b");
  navSub = el("small");
  navMeta = el("em");
  navExtra = el("small", "next");
  text.append(navText, navSub, navMeta, navExtra);
  // tap / click: pause menu with the settings (there is no Esc key on a phone)
  El burger = el("span", "burger", "☰");
  navBox.append(navArrow, text, burger);
  navBox.title(t("menuHint"));
  navBox.onclick([this] { onPause(); });
  El assist = el("div", "assist");
  prio = el("div", "prio");
  zone = el("div", "zone");
  roundel = el("div", "roundel");
  assist.append(prio, zone, roundel);
  dash = el("div", "dash");
  indL = el("span", "ind", "◀");
  dashV = el("span", "v", "0");
  dashG = el("span", "g", "N");
  El rpm = el("span", "rpm");
  dashRpm = el("i");
  rpm.append(dashRpm);
  indR = el("span", "ind", "▶");
  // a tap on an arrow: the hazard lights off when they flash – otherwise that indicator on or off
  auto tapInd = [this](const std::string& code) {
    Input* i = input;
    if (!i) return;
    const std::string k = last && last->hazard ? "KeyX" : code;
    i->press(k, true);
    i->press(k, false);
  };
  indL.onclick([tapInd] { tapInd("KeyZ"); });
  indR.onclick([tapInd] { tapInd("KeyC"); });
  // warning lamps as in an instrument cluster: parking brake and seat belt
  lampBrake = html(
      "span", "lamp",
      "<svg viewBox=\"0 0 34 24\" aria-hidden=\"true\"><circle cx=\"17\" cy=\"12\" r=\"7.6\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.2\"/><path d=\"M6 4.2a11.5 11.5 0 0 0 0 15.6M28 4.2a11.5 11.5 0 0 1 0 15.6\" fill=\"none\" "
      "stroke=\"currentColor\" stroke-width=\"2.2\" stroke-linecap=\"round\"/><text x=\"17\" y=\"16\" text-anchor=\"middle\" font-size=\"10.5\" font-weight=\"800\" font-family=\"sans-serif\" fill=\"currentColor\">P</text></svg>");
  lampBelt = html("span", "lamp",
                  "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><circle cx=\"12\" cy=\"5\" r=\"3.2\" fill=\"currentColor\"/><path d=\"M6 22v-6.5a6 6 0 0 1 12 0V22\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.3\"/><path d=\"M7.2 11.2 17 "
                  "21\" stroke=\"currentColor\" stroke-width=\"2.3\" stroke-linecap=\"round\"/></svg>");
  dash.append(indL, lampBrake, dashV, el("span", "u", "km/h"), dashG, rpm, lampBelt, indR);
  fuelGauge = std::make_unique<FuelGauge>(t("hud.fuel"), t("hud.fuelLow"));
  dash.insertBefore(fuelGauge->el, lampBelt);
  tip = el("div", "tip");
  El map = el("div", "map");
  mapCanvas = Canvas(el("canvas"));
  mapCanvas.width(220);
  mapCanvas.height(220);
  // tap / click on the map: the whole map, full screen (the real photographs are in the pause menu and on the F key)
  map.append(mapCanvas.el());
  map.title(t("mapTitle"));
  map.onclick([this] { onMap(); });
  // … as does its button
  El expand = html("button", "expand",
                   "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" d=\"M14 4h6v6M10 20H4v-6M20 4l-7 7M4 20l7-7\"/></svg>");
  expand.type("button");
  expand.title(t("mapTitle"));
  expand.setAttribute("aria-label", t("mapTitle"));
  expand.onclick([this](dom::Event& e) {
    e.stopPropagation();
    onMap();
  });
  map.append(expand);
  const WorldMetaJson& meta = world.json.meta;
  mini = std::make_unique<MiniMap>(map, mapCanvas,
                                   MiniMapHost{
                                       .backLabel = [this] { return t("miniBack"); },
                                       .moveLabel = [this] { return t("miniMove"); },
                                       .photos = [this] { return this->settings.reality.aerial; },
                                       .meta = {.originE = meta.originE, .originN = meta.originN, .minX = meta.minX, .maxX = meta.maxX, .minZ = meta.minZ, .maxZ = meta.maxZ},
                                   });
  mapBox = map;
  score = el("div", "score");
  keysBox = el("div", "keyhelp");
  // the upper left corner: the street-name block and, under it, things to choose from while standing
  El side = el("div", "side");
  choiceBox = el("div", "choices");
  choiceBox.hidden(true);
  // (on a touch screen the list would lie over the steering and the pedals: there it is a sheet
  // that this button opens, and that is closed again to drive on)
  choiceBtn = el("button", "choose");
  choiceBtn.type("button");
  choiceBtn.hidden(true);
  choiceBtn.onclick([this](dom::Event& e) {
    e.stopPropagation();
    choiceBox.addClass("open");
  });
  side.append(navBox, choiceBtn, choiceBox);
  // how many pictures a second (only if the settings ask for it): out of the way, in the upper right corner
  fpsBox = el("div", "fps");
  clockBox = el("button", "clock-badge");
  clockBox.type("button");
  clockBox.hidden(true);
  clockBox.onclick([this] { onArcade(); });
  wreckBtn = el("button", "btn new-vehicle");
  wreckBtn.type("button");
  wreckBtn.hidden(true);
  wreckBtn.onclick([this] { onNewVehicle(); });
  // (while the autopilot drives by itself: a stop to take over)
  fpsBox.hidden(true);
  h.append(side, assist, dash, tip, map, score, keysBox, fpsBox, clockBox, wreckBtn);
  // full screen and hiding the display, in the top middle (ui/viewbuttons.ts)
  if (!view) {
    view = viewButtons(root, [this](const std::string& k) { return t(k); });
    root.append(view->node);
  }
  return h;
}

void Hud::setChoices(const std::vector<Choice>& items, std::function<void(const std::string& id)> pick, bool asked) {
  choicePick = std::move(pick);
  std::string key;
  if (!items.empty()) {
    key = lang() + "|";
    for (size_t i = 0; i < items.size(); i++) key += (i ? "," : "") + items[i].id;
  }
  // (`asked`: it is the driver's turn to do something on foot – the sheet of a touch screen opens by itself)
  if (asked && !choiceAsked && !items.empty()) choiceBox.addClass("open");
  choiceAsked = asked && !items.empty();
  if (key == choiceKey) return;
  choiceKey = key;
  choiceBox.replaceChildren();
  choiceBox.hidden(items.empty());
  choiceBtn.hidden(items.empty());
  choiceBtn.textContent(t("ce.choose", {{"n", items.size()}}));
  if (items.empty()) choiceBox.removeClass("open");
  for (const Choice& it : items) {
    El b = el("button");
    b.type("button");
    b.textContent(it.label);
    b.onclick([this, id = it.id](dom::Event& e) {
      e.stopPropagation();
      // (a copy: what is picked may set other things to choose from)
      const std::function<void(const std::string&)> chosen = choicePick;
      chosen(id);
    });
    choiceBox.append(b);
  }
  if (!items.empty()) {
    El back = el("button", "back", t("ce.backToSeat"));
    back.type("button");
    back.onclick([this](dom::Event& e) {
      e.stopPropagation();
      choiceBox.removeClass("open");
    });
    choiceBox.append(back);
  }
}

void Hud::setHudVisible(bool v) { hud.style("display", v ? "" : "none"); }

void Hud::toast(const Violation& v) { toast(v, v.severity == "hint" ? 5.5 : 8); }

void Hud::toast(const Violation& v, double seconds) {
  notices.add(violationTitle(v.id, lang(), hudParts::params(v.data)), violationWhy(v.id, lang()), t(v.severity) + " · " + violationRef(v.id, lang()), v.severity, seconds, plat::nowMs());
}

void Hud::message(const std::string& title, const std::string& sub) { notices.add(title, sub, "", "hint", 6, plat::nowMs()); }

void Hud::clearNotices() { notices.clear(); }

void Hud::setClock(const std::optional<std::string>& text) {
  if (!clockBox) return;
  clockBox.hidden(!text);
  if (text && clockText != "⏱ " + *text) {
    clockText = "⏱ " + *text;
    clockBox.textContent(clockText);
    clockBox.setAttribute("aria-label", t("arcadeTitle") + ": " + *text);
  }
}

void Hud::setWrecked(bool on) {
  if (!wreckBtn) return;
  wreckBtn.hidden(!on);
  wreckBtn.textContent("🚗 " + t("newVehicle"));
}

void Hud::setFps(double fps) {
  const bool show = fps >= 0 && settings.showFps;
  if (fpsBox.hidden() == show) fpsBox.hidden(!show);
  if (!show) return;
  const std::string text = (fps < 10 ? toFixed(fps, 1) : jsStr(jsRound(fps))) + " FPS";
  if (fpsText != text) {
    fpsText = text;
    fpsBox.textContent(text);
  }
}

void Hud::update(const HudState& s, double now) {
  layout.tick(now);
  if (now - lastDom > 0.09) {
    lastDom = now;
    const bool rules = settings.hudRules;
    // navigation box
    const std::string arrow = s.navArrow < 0 ? "↰" : s.navArrow > 0 ? "↱" : "↑";
    const std::shared_ptr<const Notice> note = notices.current(plat::nowMs());
    // an instruction of several lines is set smaller than a street name
    const bool isLong = !note && i18n::utf16Length(s.navText) > 64;
    if (isLong != navLong) {
      navLong = isLong;
      navText.toggleClass("long", isLong);
    }
    if (note) {
      // a fault or hint takes the place of the street name (red: serious, yellow: minor)
      if (note != noticeShown) {
        noticeShown = note;
        navBox.className("nav alert " + noticeColour(*note));
        navText.textContent(note->title);
        navSub.textContent(note->why);
        navMeta.textContent(note->meta);
      }
      // an announced turn stays readable underneath
      navArrow.textContent(s.navActive ? arrow : "!");
      navExtra.textContent(s.navActive ? arrow + " " + s.navText : "");
    } else {
      if (noticeShown) {
        noticeShown = nullptr;
        navBox.className("nav");
        navMeta.textContent("");
        navExtra.textContent("");
      }
      navArrow.textContent(arrow);
      navText.textContent(!s.navText.empty() ? s.navText : "Fahrsim Baunatal");
      navSub.textContent(s.navSub);
    }
    // speed limit
    if (rules && s.limit >= 0) {
      roundel.style("display", "");
      const bool none = s.limit == 0;
      roundel.className(std::string("roundel") + (none ? " none" : "") + (!none && s.speedKmh > s.limit + 6 ? " over" : ""));
      const std::string word = none ? t("limitNone") : s.zone == 7 ? t("walk") : "";
      const std::string shown = !word.empty() ? word : jsStr(s.limit);
      if (shown != roundelText) {
        roundelText = shown;
        roundel.textContent(shown);
        roundel.style("fontSize", !word.empty() ? jsStr(fitRoundel(word)) + "px" : "");
      }
      zone.style("display", s.zone == 30 || s.zone == 20 || !s.limitWhen.empty() ? "" : "none");
      zone.textContent(!s.limitWhen.empty() ? s.limitWhen : "ZONE");
    } else {
      roundel.style("display", "none");
      zone.style("display", "none");
    }
    // right of way at the next junction
    std::string key;
    if (rules && s.junction && s.junction->dist < 120) {
      const RuleContext::Junction& j = *s.junction;
      // (a light – with the state of the green arrow beside it, where there is one)
      if (j.signal >= 0) key = j.arrow > -2 ? "sig" + std::to_string(j.signal) + "a" + std::to_string(j.arrow) : "sig" + std::to_string(j.signal);
      else if (j.ctrl == CTRL_ROUNDABOUT && j.role != ROLE_PRIORITY) key = "roundabout";
      else if (j.role == ROLE_STOP) key = "stop";
      else if (j.role == ROLE_YIELD) key = "yield";
      else if (j.role == ROLE_RBL) key = "rbl";
      else if (j.role == ROLE_MINOR) key = "minor";
      else if (j.role == ROLE_PRIORITY && j.ctrl == CTRL_SIGNS) key = "priority";
      // the lights are out of order: a flashing amber, and beside it the sign that counts
      if (j.dark) key = "out:" + key;
    }
    if (key != lastPrio) {
      lastPrio = key;
      if (startsWith(key, "sig")) {
        const std::vector<std::string> parts = split(key.substr(3), "a");
        const double st = jsNumber(parts[0]);
        auto lamp = [](bool on, const std::string& c) {
          return "<circle cx=\"30\" cy=\"" + std::string(c == "#ff2a1a" ? "16" : c == "#ffb000" ? "44" : "72") + "\" r=\"11\" fill=\"" + (on ? c : "#2a2a2a") + "\"/>";
        };
        const std::string head = "<rect x=\"8\" y=\"1\" width=\"44\" height=\"86\" rx=\"8\" fill=\"#111\" stroke=\"#666\"/>" + lamp(st == SIG_RED || st == SIG_RED_AMBER, "#ff2a1a") +
                                 lamp(st == SIG_AMBER || st == SIG_RED_AMBER, "#ffb000") + lamp(st == SIG_GREEN, "#1aff6a");
        if (parts.size() < 2) prio.innerHTML("<svg viewBox=\"0 0 60 88\">" + head + "</svg>");
        else {
          // the green arrow: a second, smaller head on the right with an amber and a green arrow
          const double a = jsNumber(parts[1]);
          auto point = [](bool on, const std::string& c, double y) {
            return "<circle cx=\"80\" cy=\"" + jsStr(y) + "\" r=\"11\" fill=\"#1c1c1c\"/><path d=\"M71 " + jsStr(y - 3) + "h9v-6l10 9-10 9v-6h-9z\" fill=\"" + (on ? c : "#3a3a3a") + "\"/>";
          };
          prio.innerHTML("<svg viewBox=\"0 0 104 88\">" + head + "<rect x=\"58\" y=\"29\" width=\"44\" height=\"58\" rx=\"8\" fill=\"#111\" stroke=\"#666\"/>" + point(a == SIG_AMBER, "#ffb000", 44) + point(a == SIG_GREEN, "#1aff6a", 72) +
                         "</svg>");
        }
      } else if (startsWith(key, "out:")) {
        std::string sign = iconOf(key.substr(4));
        const std::string light =
            "<svg viewBox=\"0 0 60 88\" style=\"width:26px;height:38px\"><rect x=\"8\" y=\"1\" width=\"44\" height=\"86\" rx=\"8\" fill=\"#111\" stroke=\"#666\"/><circle cx=\"30\" cy=\"16\" r=\"11\" fill=\"#2a2a2a\"/><circle cx=\"30\" cy=\"44\" "
            "r=\"11\" fill=\"#ffb000\"><animate attributeName=\"opacity\" values=\"1;1;0.12;0.12\" keyTimes=\"0;0.5;0.5;1\" dur=\"1s\" repeatCount=\"indefinite\"/></circle><circle cx=\"30\" cy=\"72\" r=\"11\" fill=\"#2a2a2a\"/></svg>";
        const size_t at = sign.find("<svg ");
        if (at != std::string::npos) sign.replace(at, 5, "<svg style=\"width:38px;height:38px\" ");
        prio.innerHTML(light + sign);
      } else prio.innerHTML(!key.empty() ? iconOf(key) : "");
    }
    // dashboard strip: outside views (the cockpit has real instruments) – and on a
    // phone, where the instruments are too small to read
    const bool showDash = !s.onFoot && (s.showDash || mode == "touch");
    dash.style("display", showDash ? "" : "none");
    // (the key reminder keeps clear of the strip)
    hud.toggleClass("dashon", showDash);
    if (showDash) {
      dashV.textContent(jsStr(jsRound(std::abs(s.speedKmh))));
      dashG.textContent(s.gear);
      dashRpm.style("width", toFixed(std::min(100.0, (s.rpm / (s.rpmMax && *s.rpmMax != 0 ? *s.rpmMax : 7000)) * 100), 0) + "%");
      indL.className(std::string("ind") + (s.blinkOn && (s.hazard || s.indicator < 0) ? " on" : ""));
      indR.className(std::string("ind") + (s.blinkOn && (s.hazard || s.indicator > 0) ? " on" : ""));
      // lit while the handbrake is on; large and flashing when the car is driven against it
      lampBrake.className(std::string("lamp") + (s.handbrake ? " on" : "") + (s.handbrakeWarn ? " warn" : ""));
      lampBrake.title(s.handbrake ? t("tbHandbrake") : "");
      lampBelt.className(std::string("lamp") + (s.seatbelt ? "" : " on"));
      fuelGauge->update(s.fuel, s.fuelLow);
    }
    tip.style("display", !s.tip.empty() ? "" : "none");
    if (s.tip != tipText) {
      tipText = s.tip;
      tip.textContent(s.tip);
    }
    tipAction = s.tipAction;
    tip.toggleClass("tap", mode == "touch" && !s.tipAction.empty());
    // ("back to the road" is easy to miss: it pulses – still, with reduced motion – ui/style.css)
    tip.toggleClass("urgent", s.tipAction == "KeyT");
    // (while the autopilot drives by itself: a stop to take over – hidden again once it does not)
    if (autoStopItem) autoStopItem.hidden(!s.autopilotDriving);
    const std::string scoreNow = t("faults") + ": <b>" + std::to_string(s.faults) + "</b> · " + t("points") + ": <b>" + std::to_string(s.points) + "</b>";
    if (scoreNow != scoreText) {
      scoreText = scoreNow;
      score.innerHTML(scoreNow);
    }
    last = s;
    if (mode == "touch") {
      // the buttons show what is switched on
      auto on = [this](const std::string& code, bool v) {
        const auto it = lit.find(code);
        if (it != lit.end()) it->second.toggleClass("on", v);
      };
      on("KeyZ", s.blinkOn && (s.hazard || s.indicator < 0));
      on("KeyC", s.blinkOn && (s.hazard || s.indicator > 0));
      on("Space", s.handbrake);
      on("KeyB", s.seatbelt);
      on("KeyI", s.engineOn);
      if (starterBtn && engineOffBtn) {
        starterBtn.hidden(s.engineOn);
        engineOffBtn.hidden(!s.engineOn);
      }
      on("KeyL", s.lights);
      on("KeyX", s.hazard);
    } else {
      // key reminder: while standing or crawling, out of the way once the car is rolling
      const bool want = settings.keyHints && !s.onFoot && !s.noKeyHints && std::abs(s.speedKmh) < 12;
      keysBox.toggleClass("show", want);
      const std::string keyNow = lang() + ":" + (_gearbox ? *_gearbox : settings.transmission);
      if (want && keyNow != keysFor) {
        keysFor = keyNow;
        keysBox.replaceChildren();
        keysBox.append(keyReminder());
      }
    }
  }
  if (now - lastMap > 0.12) {
    lastMap = now;
    // (the map on the vehicle's own screen instead of in the corner: see `screen`)
    mapBox.toggleClass("onscreen", screen.has_value());
    if (screen) {
      drawMap(s, screen->canvas);
      // (a copy: the game may put the map elsewhere while it is told)
      const std::function<void()> drawn = screen->drawn;
      drawn();
    } else drawMap(s, mapCanvas);
  }
}

void Hud::drawMap(const HudState& s, const Canvas& c) {
  const Ctx2D g = c.getContext2D();
  if (!g) return;
  const double W = c.width();
  const double Hh = c.height();
  // (as many pixels per metre on a wide screen as in the round map of 220 px)
  const double scale = 0.42 * (Hh / 220);
  const double range = std::max(W, Hh) / 2 / scale + 20;
  const double cy = Hh / 2 + 28 * (Hh / 220);
  g.setTransform(1, 0, 0, 1, 0, 0);
  g.fillStyle("#1c2630");
  g.fillRect(0, 0, W, Hh);
  // heading up: rotate so that the car's forward (-sin yaw, -cos yaw) points to -y
  g.translate(W / 2, cy);
  g.rotate(s.yaw);
  g.scale(scale, scale);
  // (the middle: the vehicle – or where the map was dragged to, to look round)
  const std::array<double, 2> middle = mini->centre(s.x, s.z, s.yaw, scale, c == mapCanvas);
  const double mx = middle[0];
  const double mz = middle[1];
  g.translate(-mx, -mz);
  g.lineCap("round");
  g.lineJoin("round");
  // the aerial photographs under the roads (unless switched off): the roads over them only hinted at
  const bool photo = mini->photos(g, mx, mz, range, scale);
  g.globalAlpha(photo ? 0.5 : 1);
  // (a road's line: its points handed over in one piece)
  std::vector<double> xy;
  auto line = [&g, &xy](const Edge& e) {
    const std::vector<double>& p = e.pl.pts;
    xy.clear();
    for (size_t i = 0; i + 2 < p.size(); i += 3) {
      xy.push_back(p[i]);
      xy.push_back(p[i + 2]);
    }
    g.beginPath();
    g.polyline(xy);
    g.stroke();
  };
  auto draw = [&](const char* color, double minTier, double maxTier, double extra) {
    g.strokeStyle(color);
    world.edgesInBox(mx - range, mz - range, mx + range, mz + range, [&](const Edge& e) {
      if (e.tier < minTier || e.tier > maxTier) return;
      g.lineWidth(e.width + extra);
      line(e);
    });
  };
  draw("#55606b", 0, 1.5, 0);
  draw("#aab4bd", 2, 2.9, 1);
  draw("#f1f3f5", 3, 5.5, 2);
  draw("#f5b942", 6, 9, 3);
  g.globalAlpha(1);
  if (!routeLanes.empty()) {
    g.strokeStyle("#3d8bff");
    g.lineWidth(7);
    for (const Lane* lane : routeLanes) {
      const Edge& e = *lane->edge;
      if (e.maxX < mx - range || e.minX > mx + range || e.maxZ < mz - range || e.minZ > mz + range) continue;
      line(e);
    }
  }
  // the car (off the middle while the map is looked round on)
  g.setTransform(1, 0, 0, 1, 0, 0);
  const double cs = std::cos(s.yaw);
  const double sn = std::sin(s.yaw);
  g.translate(W / 2 + ((s.x - mx) * cs - (s.z - mz) * sn) * scale, cy + ((s.x - mx) * sn + (s.z - mz) * cs) * scale);
  g.scale(Hh / 220, Hh / 220);
  g.fillStyle("#ffcc00");
  g.strokeStyle("#111");
  g.lineWidth(2);
  g.beginPath();
  g.moveTo(0, -11);
  g.lineTo(7, 8);
  g.lineTo(0, 4);
  g.lineTo(-7, 8);
  g.closePath();
  g.fill();
  g.stroke();
}

}  // namespace fs

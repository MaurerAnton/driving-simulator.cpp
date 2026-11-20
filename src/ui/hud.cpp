// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Menus and the in-game HUD.
 *
 * Origin: src/ui/hud.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * The panel structure, the control table, the toasts and the mini map are
 * carried over; the DOM calls become HudNode widgets the platform renders.
 */
#include "ui/hud.h"

#include "ui/addresspicker.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "game/i18n.h"
#include "sim/signals.h"

namespace fs {

namespace {
double nowMs() {
  using clock = std::chrono::steady_clock;
  static const clock::time_point start = clock::now();
  return std::chrono::duration<double, std::milli>(clock::now() - start).count();
}

std::unique_ptr<HudNode> node(const std::string& tag, const std::string& cls = "",
                              const std::string& text = "") {
  auto n = std::make_unique<HudNode>();
  n->tag = tag;
  n->cls = cls;
  n->text = text;
  return n;
}

std::unique_ptr<HudNode> button(const std::string& cls, const std::string& text,
                                const std::function<void()>& click) {
  auto b = node("button", cls, text);
  b->click = click;
  return b;
}

/** The legal basis of a violation ("§ 37 StVO"), as the notices show it. */
std::string violationRef(const std::string& id) {
  for (const auto& e : VIOLATION_ENTRIES)
    if (e.first == id) return e.second.ref;
  return "";
}

NoticeSeverity severityOf(Severity s) {
  switch (s) {
    case Severity::Fail: return NoticeSeverity::Fail;
    case Severity::Major: return NoticeSeverity::Major;
    case Severity::Minor: return NoticeSeverity::Minor;
    case Severity::Hint: break;
  }
  return NoticeSeverity::Hint;
}
/** The notice clock lives in the anonymous namespace above (performance.now()). */

/** The SVG snippets the original kept as strings (signs and lamps). */
const char* const ICON_YIELD =
    "<svg viewBox=\"0 0 100 90\"><path d=\"M3 3h94L50 87z\" fill=\"#d0141e\"/>"
    "<path d=\"M20 13h60L50 67z\" fill=\"#fff\"/></svg>";
const char* const ICON_STOP =
    "<svg viewBox=\"0 0 100 100\"><path d=\"M30 2h40l28 28v40L70 98H30L2 70V30z\" fill=\"#d0141e\" "
    "stroke=\"#fff\" stroke-width=\"4\"/></svg>";
const char* const ICON_PRIORITY =
    "<svg viewBox=\"0 0 100 100\"><path d=\"M50 3l47 47-47 47L3 50z\" fill=\"#fff\" stroke=\"#222\" "
    "stroke-width=\"2\"/><path d=\"M50 20l30 30-30 30-30-30z\" fill=\"#f6c817\"/></svg>";
const char* const ICON_RBL =
    "<svg viewBox=\"0 0 100 90\"><path d=\"M50 3l47 84H3z\" fill=\"#d0141e\"/>"
    "<path d=\"M50 22l31 55H19z\" fill=\"#fff\"/></svg>";
const char* const ICON_MINOR =
    "<svg viewBox=\"0 0 100 90\"><path d=\"M3 3h94L50 87z\" fill=\"#888\"/>"
    "<path d=\"M20 13h60L50 67z\" fill=\"#fff\"/></svg>";
const char* const ICON_ROUNDABOUT =
    "<svg viewBox=\"0 0 100 100\"><circle cx=\"50\" cy=\"50\" r=\"47\" fill=\"#1b5fa8\" "
    "stroke=\"#fff\" stroke-width=\"3\"/></svg>";
}  // namespace

Hud::Hud(HudHost& host, Settings& settings, World& world)
    : host_(host), settings_(settings), world_(world) {
  hud_ = buildHud();
  host_.setRoot(hud_.get());
  setHudVisible(false);
}

Hud::~Hud() = default;

std::string Hud::t(const std::string& key) const {
  Lang l = settings_.lang == LangSetting::En ? Lang::En : Lang::De;
  return fs::tr(key, l);
}

std::string Hud::t(const std::string& key,
                   const std::vector<std::pair<std::string, std::string>>& p) const {
  Lang l = settings_.lang == LangSetting::En ? Lang::En : Lang::De;
  return fs::tr(key, l, p);
}

void Hud::setOverlay(std::unique_ptr<HudNode> nodeIn) {
  overlay_ = std::move(nodeIn);
  host_.setRoot(overlay_ ? overlay_.get() : hud_.get());
}

void Hud::showLoading(const std::string& text) {
  auto o = node("div", "overlay solid");
  auto p = node("div", "panel narrow");
  p->add(node("h1", "", "Fahrsim <span>Baunatal</span>"));
  p->add(node("p", "", text));
  auto bar = node("div", "progress");
  bar->add(node("i"));
  p->add(std::move(bar));
  o->add(std::move(p));
  setOverlay(std::move(o));
}

void Hud::showError(const std::string& text) {
  auto o = node("div", "overlay solid");
  auto p = node("div", "panel narrow");
  p->add(node("h1", "", "Fahrsim <span>Baunatal</span>"));
  p->add(node("p", "", text));
  o->add(std::move(p));
  setOverlay(std::move(o));
}

void Hud::showMenu(const std::vector<SpawnJson>& spawns, int lastSpawn) {
  hasPickedAddress_ = false;   // a new menu starts without an address picked
  auto o = node("div", "overlay");
  auto p = node("div", "panel");
  p->add(node("h1", "", "Fahrsim <span>Baunatal</span>"));
  p->add(node("p", "", t("subtitle")));

  int spawn = std::min(lastSpawn, std::max(0, static_cast<int>(spawns.size()) - 1));

  auto opts = node("div");
  auto rowSpawn = node("div", "row");
  auto selSpawn = node("select");
  selSpawn->id = "spawn";
  for (size_t i = 0; i < spawns.size(); i++)
    selSpawn->add(node("option", "", spawns[i].name));
  rowSpawn->add(node("label", "", t("startPoint")));
  rowSpawn->add(std::move(selSpawn));
  // … or anywhere: street and house number
  auto rowAddr = node("div", "row");
  menuPicker_ = std::make_unique<AddressPicker>(addressPicker(AddressPickerOptions{
      t("addressPlaceholder"),
      t("realityLoading"),
      t("addressNone"),
      [this]() { return loadAddresses ? loadAddresses() : nullptr; },
      [this](const AddressHit& hit) { pickedAddress_ = hit; hasPickedAddress_ = true; },
  }));
  {
    HudNode* pickRoot = menuPicker_->node.get();
    rowAddr->add(node("label", "", t("startAddress")));
    rowAddr->children.push_back(std::move(menuPicker_->node));
    (void)pickRoot;
  }
  auto rowTr = node("div", "row");
  auto selTr = node("select");
  selTr->id = "transmission";
  for (const auto& kv : {std::make_pair("assist", "trAssist"), std::make_pair("manual", "trManual"),
                         std::make_pair("automatic", "trAutomatic")}) {
    (void)kv;
  }
  selTr->add(node("option", "", t("trAssist")));
  selTr->add(node("option", "", t("trManual")));
  selTr->add(node("option", "", t("trAutomatic")));
  rowTr->add(node("label", "", t("transmission")));
  rowTr->add(std::move(selTr));
  opts->add(std::move(rowSpawn));
  opts->add(std::move(rowAddr));
  opts->add(std::move(rowTr));
  p->add(std::move(opts));

  p->add(node("h2", "", t("start")));
  auto cards = node("div", "cards");
  auto card = [&](const std::string& title, const std::string& desc, const std::string& mode) {
    auto c = button("card", "<b>" + title + "</b><small>" + desc + "</small>",
                    [this, mode, spawn]() {
                      if (!onStart) return;
                      StartRequest r;
                      r.mode = mode;
                      r.spawn = spawn;
                      r.address = pickedAddress_;
                      r.hasAddress = hasPickedAddress_;
                      onStart(r);
                    });
    cards->add(std::move(c));
  };
  card(t("free"), t("freeDesc"), "free");
  int li = settings_.lang == LangSetting::De ? 0 : 1;
  for (const auto& l : lessons()) {
    const std::string& title = li == 0 ? l.title.first : l.title.second;
    const std::string& intro = li == 0 ? l.intro.first : l.intro.second;
    card(title, l.kind == "exam" ? t("examDesc") : intro.substr(0, 118) + "...", l.id);
  }
  p->add(std::move(cards));

  auto b = node("div", "buttons");
  auto back = [this, spawns, spawn]() { showMenu(spawns, spawn); };
  b->add(button("btn ghost", t("settings"), [this, back]() { showSettings(back); }));
  b->add(button("btn ghost", t("controls"), [this, back]() { showControls(back); }));
  b->add(button("btn ghost", t("about"), [this, back]() { showAbout(back); }));
  p->add(std::move(b));
  o->add(std::move(p));
  setOverlay(std::move(o));
}

void Hud::showSettings(const std::function<void()>& back) {
  Settings& s = settings_;
  auto o = node("div", "overlay");
  auto p = node("div", "panel narrow");
  p->add(node("h1", "", t("settings")));

  auto select = [&](const std::string& label, const std::string& value,
                    const std::vector<std::pair<std::string, std::string>>& options,
                    const std::function<void(const std::string&)>& set) {
    auto row = node("div", "row");
    auto sel = node("select");
    for (const auto& kv : options) sel->add(node("option", "", kv.second));
    sel->text = value;
    sel->click = [set, value]() { set(value); };
    row->add(node("label", "", label));
    row->add(std::move(sel));
    p->add(std::move(row));
  };
  auto check = [&](const std::string& label, bool value, const std::function<void(bool)>& set) {
    auto row = node("div", "row");
    auto c = node("input");
    c->text = value ? "checked" : "";
    c->click = [set, value]() { set(!value); };
    auto l = node("label", "", label);
    row->add(std::move(c));
    row->add(std::move(l));
    p->add(std::move(row));
  };
  auto range = [&](const std::string& label, double value, const std::function<void(double)>& set) {
    auto row = node("div", "row");
    auto r = node("input");
    r->text = std::to_string(value);
    r->click = [set, value]() { set(value); };
    row->add(node("label", "", label));
    row->add(std::move(r));
    p->add(std::move(row));
  };

  select(t("language"), s.lang == LangSetting::De ? "de" : "en",
         {{"de", "Deutsch"}, {"en", "English"}}, [this, back](const std::string& v) {
           settings_.lang = v == "en" ? LangSetting::En : LangSetting::De;
           showSettings(back);
         });
  select(t("transmission"),
         s.transmission == Transmission::Assist
             ? "assist"
             : (s.transmission == Transmission::Manual ? "manual" : "automatic"),
         {{"assist", t("trAssist")}, {"manual", t("trManual")}, {"automatic", t("trAutomatic")}},
         [&s](const std::string& v) {
           s.transmission = v == "assist" ? Transmission::Assist
                                          : (v == "manual" ? Transmission::Manual
                                                           : Transmission::Automatic);
         });
  select(t("quality"),
         s.quality == Quality::Low ? "low" : (s.quality == Quality::Medium ? "medium" : "high"),
         {{"low", t("qLow")}, {"medium", t("qMedium")}, {"high", t("qHigh")}},
         [&s](const std::string& v) {
           s.quality = v == "low" ? Quality::Low
                                  : (v == "medium" ? Quality::Medium : Quality::High);
         });
  select(t("mirrors"),
         s.mirrors == Mirrors::Off ? "off"
                                   : (s.mirrors == Mirrors::Alternate ? "alternate" : "full"),
         {{"off", t("mOff")}, {"alternate", t("mAlt")}, {"full", t("mFull")}},
         [&s](const std::string& v) {
           s.mirrors = v == "off" ? Mirrors::Off
                                  : (v == "alternate" ? Mirrors::Alternate : Mirrors::Full);
         });
  range(t("traffic"), s.traffic, [&s](double v) { s.traffic = v; });
  range(t("volume"), s.volume, [&s](double v) { s.volume = v; });
  select(t("touchControls"), s.touchControls,
         {{"auto", t("tcAuto")}, {"on", t("tcOn")}, {"off", t("tcOff")}},
         [&s](const std::string& v) { s.touchControls = v; });
  range(t("touchOpacity"), s.touchOpacity, [&s](double v) { s.touchOpacity = std::max(0.15, v); });
  check(t("keyHints"), s.keyHints, [&s](bool v) { s.keyHints = v; });
  check(t("hudRules"), s.hudRules, [&s](bool v) { s.hudRules = v; });
  check(t("hudGear"), s.hudGear, [&s](bool v) { s.hudGear = v; });
  check(t("voice"), s.voice, [&s](bool v) { s.voice = v; });

  auto b = node("div", "buttons");
  b->add(button("btn", t("back"), back));
  p->add(std::move(b));
  o->add(std::move(p));
  setOverlay(std::move(o));
}

void Hud::showControls(const std::function<void()>& back) {
  bool de = settings_.lang == LangSetting::De;
  const std::vector<std::pair<std::string, std::string>> rows = {
      {"W S / arrows", de ? "Gas / Bremse" : "Throttle / brake"},
      {"A D / arrows", de ? "Lenken" : "Steer"},
      {"Shift", de ? "Kupplung" : "Clutch"},
      {"E Q", de ? "Gang hoch / runter" : "Gear up / down"},
      {"1-6, R, N", de ? "Gang direkt, Rueckwaerts, Leerlauf" : "Direct gear, reverse, neutral"},
      {"Space", de ? "Handbremse" : "Handbrake"},
      {"Enter / I", de ? "Motor starten / Zuedung aus" : "Start engine / ignition off"},
      {"Z C X", de ? "Blinker, Warnblinker" : "Indicators, hazards"},
      {"J K", de ? "Schulterblick" : "Shoulder check"},
      {"B", de ? "Gurt anlegen" : "Seat belt"},
      {"L / O", de ? "Abblendlicht / Fernlicht" : "Dipped / main beam"},
      {"H", de ? "Hupe" : "Horn"},
      {"V", de ? "Kamera wechseln" : "Change camera"},
      {"T", de ? "Auto zurueck auf die Strasse" : "Put the car back on the road"},
      {"F", de ? "Realansicht: echte Fotos dieser Stelle (oder die Karte anklicken)"
               : "Reality view: real photos of this place (or click the map)"},
      {"Esc", de ? "Pause" : "Pause"},
  };
  auto o = node("div", "overlay");
  auto p = node("div", "panel");
  p->add(node("h1", "", t("controls")));
  auto table = node("table", "keys");
  for (const auto& kv : rows) {
    auto tr = node("tr");
    tr->add(node("td", "", kv.first));
    tr->add(node("td", "", kv.second));
    table->add(std::move(tr));
  }
  p->add(std::move(table));
  p->add(node(
      "p", "",
      de
          ? "<b>Maus und Touchscreen:</b> Straßenname oben links antippen = Menü und "
            "Einstellungen, Karte antippen = Realansicht. Auf dem Touchscreen: links die "
            "Lenkleiste und die Blinker, rechts Gas, Bremse, Gänge (▲ ▼) und Handbremse (P); "
            "„⋯" " oben öffnet Gurt, Anlasser, Licht, Hupe, Kamera und mehr. Über das Bild "
            "wischen = umsehen (Schulterblick), loslassen = wieder geradeaus. Gelbe Hinweise "
            "unten lassen sich antippen. Deckkraft und Anzeige der Bedienelemente stehen in "
            "den Einstellungen."
          : "<b>Mouse and touch screen:</b> tap the street name at the top left for the menu "
            "and settings, tap the map for the reality view. On a touch screen: steering bar "
            "and indicators on the left; throttle, brake, gears (▲ ▼) and handbrake (P) on "
            "the right; \"⋯\" at the top opens seat belt, starter, lights, horn, camera and "
            "more. Swipe across the picture to look around (shoulder check), let go to look "
            "ahead again. Yellow tips at the bottom can be tapped. Opacity and visibility of "
            "the controls are in the settings."));
  p->add(node("p", "", de
                         ? "Gamepad: linker Stick lenken, RT Gas, LT Bremse, RB/LB schalten."
                         : "Gamepad: left stick steers, RT throttle, LT brake, RB/LB shift."));
  auto b = node("div", "buttons");
  b->add(button("btn", t("back"), back));
  p->add(std::move(b));
  o->add(std::move(p));
  setOverlay(std::move(o));
}

void Hud::showAbout(const std::function<void()>& back) {
  auto o = node("div", "overlay");
  auto p = node("div", "panel");
  p->add(node("h1", "", t("about")));
  p->add(node("p", "", "Fahrsim Baunatal - " + t("subtitle")));
  auto b = node("div", "buttons");
  b->add(button("btn", t("back"), back));
  p->add(std::move(b));
  o->add(std::move(p));
  setOverlay(std::move(o));
}

void Hud::showIntro(const std::string& title, const std::string& text,
                    const std::function<void()>& go) {
  auto o = node("div", "overlay");
  auto p = node("div", "panel narrow");
  p->add(node("h1", "", title));
  p->add(node("p", "", text));
  auto b = node("div", "buttons");
  b->add(button("btn", t("start"), go));
  p->add(std::move(b));
  o->add(std::move(p));
  setOverlay(std::move(o));
}

void Hud::showPause() {
  auto o = node("div", "overlay");
  auto p = node("div", "panel narrow");
  p->add(node("h1", "", t("paused")));
  if (hasLastState_)
    p->add(node("p", "", t("faults") + ": " + std::to_string(lastState_.faults) + " · " +
                           t("points") + ": " + std::to_string(lastState_.points)));
  auto b = node("div", "buttons");
  b->add(button("btn", t("resume"), [this]() {
    if (onResume) onResume();
    hideOverlay();
  }));
  b->add(button("btn ghost", t("realityButton"), [this]() {
    if (onReality) onReality();
  }));
  if (canTeleport) {
    b->add(button("btn ghost", t("teleport"), [this]() { showTeleport(); }));
  }
  b->add(button("btn ghost", t("settings"), [this]() { showSettings([this]() { showPause(); }); }));
  b->add(button("btn ghost", t("controls"), [this]() { showControls([this]() { showPause(); }); }));
  b->add(button("btn ghost", t("menu"), [this]() {
    if (onQuit) onQuit();
    hideOverlay();
  }));
  p->add(std::move(b));
  o->add(std::move(p));
  setOverlay(std::move(o));
}

/** Free drive: put the car in front of an address (0.5.0). */
void Hud::showTeleport() {
  auto o = node("div", "overlay");
  auto p = node("div", "panel narrow");
  p->add(node("h1", "", t("teleport")));
  p->add(node("p", "", t("teleportText")));
  auto picker = addressPicker(AddressPickerOptions{
      t("addressPlaceholder"),
      t("realityLoading"),
      t("addressNone"),
      [this]() { return loadAddresses ? loadAddresses() : nullptr; },
      [this](const AddressHit& hit) {
        if (onTeleport) onTeleport(hit);
      },
  });
  p->children.push_back(std::move(picker.node));
  auto b = node("div", "buttons");
  b->add(button("btn ghost", t("back"), [this]() { showPause(); }));
  p->add(std::move(b));
  o->add(std::move(p));
  setOverlay(std::move(o));
}

void Hud::showResult(const ResultSummary& r) {
  auto o = node("div", "overlay");
  auto p = node("div", "panel");
  p->add(node("h1", "", r.passed ? t("passed") : t("failed")));
  p->add(node("p", "", r.title));
  auto list = node("div");
  for (const auto& v : r.log) {
    Lang l = settings_.lang == LangSetting::En ? Lang::En : Lang::De;
    auto row = node("div", "row");
    row->add(node("b", "", violationTitle(v.id, l, v.data)));
    row->add(node("small", "", violationWhy(v.id, l)));
    list->add(std::move(row));
  }
  p->add(std::move(list));
  p->add(node("p", "", t("points") + ": " + std::to_string(r.points)));
  auto b = node("div", "buttons");
  b->add(button("btn", t("menu"), [this]() {
    if (onQuit) onQuit();
    hideOverlay();
  }));
  p->add(std::move(b));
  o->add(std::move(p));
  setOverlay(std::move(o));
}

void Hud::hideOverlay() { setOverlay(nullptr); }

std::unique_ptr<HudNode> Hud::buildHud() {
  auto h = node("div", "hud");
  auto navBox = node("div", "nav");
  navBox->id = "nav";
  auto arrow = node("div", "arrow", "up");
  arrow->id = "navArrow";
  auto text = node("div", "text");
  auto navText = node("b");
  navText->id = "navText";
  auto navSub = node("small");
  navSub->id = "navSub";
  auto navMeta = node("em");
  navMeta->id = "navMeta";
  auto navExtra = node("small", "next");
  navExtra->id = "navExtra";
  text->add(std::move(navText));
  text->add(std::move(navSub));
  text->add(std::move(navMeta));
  text->add(std::move(navExtra));
  // tap / click: pause menu with the settings (there is no Esc key on a phone)
  auto burger = node("span", "burger", "\u2630");
  burger->id = "navBurger";
  navBox->add(std::move(arrow));
  navBox->add(std::move(text));
  navBox->add(std::move(burger));
  navBox->click = [this]() {
    if (onPause) onPause();
  };

  auto assist = node("div", "assist");
  auto prio = node("div", "prio");
  prio->id = "prio";
  auto zone = node("div", "zone");
  zone->id = "zone";
  auto roundel = node("div", "roundel");
  roundel->id = "roundel";
  assist->add(std::move(prio));
  assist->add(std::move(zone));
  assist->add(std::move(roundel));

  auto dash = node("div", "dash");
  dash->id = "dash";
  auto indL = node("span", "ind", "L");
  indL->id = "indL";
  auto dashV = node("span", "v", "0");
  dashV->id = "dashV";
  auto dashG = node("span", "g", "N");
  dashG->id = "dashG";
  auto rpm = node("span", "rpm");
  auto dashRpm = node("i");
  dashRpm->id = "dashRpm";
  rpm->add(std::move(dashRpm));
  auto indR = node("span", "ind", "R");
  indR->id = "indR";
  dash->add(std::move(indL));
  dash->add(std::move(dashV));
  dash->add(node("span", "u", "km/h"));
  dash->add(std::move(dashG));
  dash->add(std::move(rpm));
  dash->add(std::move(indR));

  auto tip = node("div", "tip");
  tip->id = "tip";
  auto map = node("div", "map");
  auto mapCanvas = node("canvas");
  mapCanvas->id = "map";
  map->add(std::move(mapCanvas));
  // tap / click on the map: real photographs of this place (the F key)
  auto cam = node("span", "cam",
                  "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path fill=\"currentColor\" "
                  "d=\"M9 4 7.5 6H4a2 2 0 0 0-2 2v10a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2V8a2 2 0 0 "
                  "0-2-2h-3.5L15 4H9zm3 4.5a4.5 4.5 0 1 1 0 9 4.5 4.5 0 0 1 0-9zm0 2a2.5 2.5 0 "
                  "1 0 0 5 2.5 2.5 0 0 0 0-5z\"/></svg>");
  map->add(std::move(cam));
  map->click = [this]() {
    if (onReality) onReality();
  };
  auto score = node("div", "score");
  score->id = "score";
  auto keysBox = node("div", "keyhelp");
  keysBox->id = "keyhelp";

  h->add(std::move(navBox));
  h->add(std::move(assist));
  h->add(std::move(dash));
  h->add(std::move(tip));
  h->add(std::move(map));
  h->add(std::move(score));
  h->add(std::move(keysBox));
  return h;
}

void Hud::setHudVisible(bool v) { host_.setVisible("hud", v); }

void Hud::toast(const Violation& v, double seconds) {
  Lang l = settings_.lang == LangSetting::En ? Lang::En : Lang::De;
  std::string sev = v.severity == Severity::Hint ? "hint"
                     : v.severity == Severity::Minor ? "minor"
                     : v.severity == Severity::Major  ? "major"
                                                      : "fail";
  if (seconds < 0) seconds = v.severity == Severity::Hint ? 5.5 : 8;
  notices_.add(violationTitle(v.id, l, v.data), violationWhy(v.id, l),
               this->t(sev) + " \u00b7 " + violationRef(v.id), severityOf(v.severity), seconds,
               nowMs());
}

void Hud::message(const std::string& title, const std::string& sub) {
  notices_.add(title, sub, "", NoticeSeverity::Hint, 6, nowMs());
}

void Hud::update(const HudState& s, double now) {
  if (now - lastDom_ > 0.09) {
    lastDom_ = now;
    bool rules = settings_.hudRules;

    // navigation box: a fault or hint takes the place of the street name
    // (the original always shows the box; the title falls back to the game)
    host_.setVisible("nav", true);
    {
      const char* arrow = s.navArrow < 0 ? "left" : (s.navArrow > 0 ? "right" : "up");
      const Notice* note = notices_.current(nowMs());
      if (note) {
        if (note != noticeShown_) {
          noticeShown_ = note;
          host_.setClass("nav", std::string("nav alert ") + noticeColour(note->severity));
          host_.setText("navText", note->title);
          host_.setText("navSub", note->why);
          host_.setText("navMeta", note->meta);
        }
        // an announced turn stays readable underneath
        host_.setText("navArrow", s.navActive ? arrow : "!");
        host_.setText("navExtra",
                      s.navActive ? std::string(arrow) + " " + s.navText : std::string());
      } else {
        if (noticeShown_) {
          noticeShown_ = nullptr;
          host_.setClass("nav", "nav");
          host_.setText("navMeta", "");
          host_.setText("navExtra", "");
        }
        host_.setText("navArrow", arrow);
        host_.setText("navText", s.navText.empty() ? "Fahrsim Baunatal" : s.navText);
        host_.setText("navSub", s.navSub);
      }
    }

    // speed limit
    if (rules && s.limit >= 0) {
      host_.setVisible("roundel", true);
      bool none = s.limit == 0;
      std::string cls = "roundel";
      if (none) cls += " none";
      if (!none && s.speedKmh > s.limit + 6) cls += " over";
      host_.setClass("roundel", cls);
      host_.setText("roundel", none ? t("limitNone")
                                    : (s.zone == 7 ? t("walk") : std::to_string(s.limit)));
      host_.setVisible("zone", s.zone == 30 || s.zone == 20);
      if (s.zone == 30 || s.zone == 20) host_.setText("zone", "ZONE");
    } else {
      host_.setVisible("roundel", false);
      host_.setVisible("zone", false);
    }

    // right of way at the next junction
    std::string key;
    if (rules && s.junction.set && s.junction.dist < 120) {
      const HudState::Junction& j = s.junction;
      if (j.signal >= 0) key = "sig" + std::to_string(j.signal);
      else if (j.ctrl == CTRL_ROUNDABOUT && j.role != ROLE_PRIORITY) key = "roundabout";
      else if (j.role == ROLE_STOP) key = "stop";
      else if (j.role == ROLE_YIELD) key = "yield";
      else if (j.role == ROLE_RBL) key = "rbl";
      else if (j.role == ROLE_MINOR) key = "minor";
      else if (j.role == ROLE_PRIORITY && j.ctrl == CTRL_SIGNS) key = "priority";
    }
    if (key != lastPrio_) {
      lastPrio_ = key;
      if (key.rfind("sig", 0) == 0) {
        int st = std::atoi(key.c_str() + 3);
        std::string svg = "<svg viewBox=\"0 0 60 88\"><rect x=\"8\" y=\"1\" width=\"44\" "
                          "height=\"86\" rx=\"8\" fill=\"#111\" stroke=\"#666\"/>";
        auto lamp = [&](bool on, const char* c, int cy) {
          svg += "<circle cx=\"30\" cy=\"" + std::to_string(cy) + "\" r=\"11\" fill=\"" +
                 (on ? c : "#2a2a2a") + "\"/>";
        };
        lamp(st == SIG_RED || st == SIG_RED_AMBER, "#ff2a1a", 16);
        lamp(st == SIG_AMBER || st == SIG_RED_AMBER, "#ffb000", 44);
        lamp(st == SIG_GREEN, "#1aff6a", 72);
        svg += "</svg>";
        host_.setText("prio", svg);
      } else if (key.empty()) {
        host_.setText("prio", "");
      } else if (key == "yield") {
        host_.setText("prio", ICON_YIELD);
      } else if (key == "stop") {
        host_.setText("prio", ICON_STOP);
      } else if (key == "priority") {
        host_.setText("prio", ICON_PRIORITY);
      } else if (key == "rbl") {
        host_.setText("prio", ICON_RBL);
      } else if (key == "minor") {
        host_.setText("prio", ICON_MINOR);
      } else if (key == "roundabout") {
        host_.setText("prio", ICON_ROUNDABOUT);
      }
    }

    // dashboard strip: outside views (the cockpit has real instruments) – and
    // on a phone, where the instruments are too small to read
    const bool showDash = s.showDash || mode_ == "touch";
    host_.setVisible("dash", showDash);
    if (showDash) {
      host_.setText("dashV", std::to_string(std::lround(std::fabs(s.speedKmh))));
      host_.setText("dashG", s.gear);
      host_.setText("dashRpm",
                    std::to_string(std::min(100.0, (s.rpm / 7000.0) * 100.0)) + "%");
      host_.setClass("indL", s.blinkOn && (s.hazard || s.indicator < 0) ? "ind on" : "ind");
      host_.setClass("indR", s.blinkOn && (s.hazard || s.indicator > 0) ? "ind on" : "ind");
    }

    host_.setVisible("tip", !s.tip.empty());
    if (!s.tip.empty()) host_.setText("tip", s.tip);
    tipAction_ = s.tipAction;
    host_.setClass("tip", mode_ == "touch" && !s.tipAction.empty() ? "tip tap" : "tip");
    host_.setText("score", t("faults") + ": " + std::to_string(s.faults) + " · " + t("points") +
                               ": " + std::to_string(s.points));
    lastState_ = s;
    hasLastState_ = true;
    if (mode_ == "touch") {
      // the buttons show what is switched on
      auto lit = [&](const char* id, bool on) { host_.setClass(id, on ? "tb on" : "tb"); };
      lit("tbIndL", s.blinkOn && (s.hazard || s.indicator < 0));
      lit("tbIndR", s.blinkOn && (s.hazard || s.indicator > 0));
      lit("tbHb", s.handbrake);
      lit("mbBelt", s.seatbelt);
      lit("mbIgnition", s.engineOn);
      lit("mbLights", s.lights);
      lit("mbHazard", s.hazard);
      host_.setVisible("keyhelp", false);
      host_.setVisible("score", false);
    } else {
      // key reminder: while standing or crawling, out of the way once rolling
      const bool want = settings_.keyHints && std::fabs(s.speedKmh) < 12;
      host_.setVisible("keyhelp", want);
      const std::string key = std::string(settings_.lang == LangSetting::En ? "en" : "de") + ":" +
                              (settings_.transmission == Transmission::Automatic ? "automatic"
                               : settings_.transmission == Transmission::Manual   ? "manual"
                                                                                 : "assist");
      if (want && key != keysFor_) {
        keysFor_ = key;
        host_.setText("keyhelp", keyReminder());
      }
      host_.setVisible("score", true);
    }
  }
  if (now - lastMap_ > 0.12) {
    lastMap_ = now;
    drawMap(s);
  }
}

void Hud::drawMap(const HudState& s) {
  Canvas2D& g = mapCanvas_;
  int W = g.width();
  double scale = 0.42;  // px per metre
  double range = W / 2 / scale + 20;
  g.clear(parseColor("#1c2630"));

  auto draw = [&](const char* color, double minTier, double maxTier, double extra) {
    world_.edgesInBox(s.x - range, s.z - range, s.x + range, s.z + range, [&](Edge& e) {
      if (e.tier < minTier || e.tier > maxTier) return;
      const auto& p = e.pl.pts;
      if (p.size() < 6) return;
      // the canvas maps world (x, z) into the map's own frame below
      for (size_t i = 3; i < p.size(); i += 3) {
        double x0 = W / 2 + (p[i - 3] - s.x) * scale;
        double y0 = W / 2 + 28 - (p[i - 2] - s.z) * scale;
        double x1 = W / 2 + (p[i] - s.x) * scale;
        double y1 = W / 2 + 28 - (p[i + 2] - s.z) * scale;
        g.line(x0, y0, x1, y1, parseColor(color), e.width * scale + extra);
      }
    });
  };
  draw("#55606b", 0, 1.5, 0);
  draw("#aab4bd", 2, 2.9, 1);
  draw("#f1f3f5", 3, 5.5, 2);
  draw("#f5b942", 6, 9, 3);

  if (!routeLanes.empty()) {
    for (const Lane* lane : routeLanes) {
      if (!lane || !lane->edge) continue;
      const Edge& e = *lane->edge;
      if (e.maxX < s.x - range || e.minX > s.x + range || e.maxZ < s.z - range ||
          e.minZ > s.z + range)
        continue;
      const auto& p = e.pl.pts;
      for (size_t i = 3; i < p.size(); i += 3) {
        double x0 = W / 2 + (p[i - 3] - s.x) * scale;
        double y0 = W / 2 + 28 - (p[i - 2] - s.z) * scale;
        double x1 = W / 2 + (p[i] - s.x) * scale;
        double y1 = W / 2 + 28 - (p[i + 2] - s.z) * scale;
        g.line(x0, y0, x1, y1, parseColor("#3d8bff"), 7);
      }
    }
  }

  // the car, pointing up
  double cx = W / 2, cy = W / 2 + 28;
  g.fillPoly({{cx, cy - 11}, {cx + 7, cy + 8}, {cx, cy + 4}, {cx - 7, cy + 8}},
             parseColor("#ffcc00"));
  host_.paint("map", g);
}

/**
 * Steering bar, pedals and buttons for driving with the fingers.  They are
 * always built and shown only in touch mode (setInputMode).
 *
 * Left thumb: steering, indicators.  Right thumb: pedals, gears, handbrake.
 * Everything else is behind "⋯".  The middle of the screen stays free.
 */
void Hud::attachInput(Input& input) {
  input_ = &input;
  auto touchRoot = node("div", "touch");
  touchRoot->id = "touch";
  auto steer = node("div", "steer");
  steer->id = "touchSteer";
  steer->add(node("i"));
  touchRoot->add(std::move(steer));
  auto pedal = [&](const char* cls, const char* id, const char* label) {
    auto p = node("div", std::string("pedal ") + cls, label);
    p->id = id;
    touchRoot->add(std::move(p));
  };
  pedal("gas", "touchGas", t("tbGas").c_str());
  pedal("brk", "touchBrake", t("tbBrake").c_str());
  /** A button that stands in for a key: down while the finger is on it. */
  auto key = [&](const char* cls, const char* id, const char* label, const char* code,
                 const char* titleKey) {
    auto b = button(cls, label, [&input, code]() { input.press(code, true); });
    b->id = id;
    if (titleKey && *titleKey) b->html = t(titleKey);
    touchRoot->add(std::move(b));
  };
  key("tb ind-l", "tbIndL", "\u25c0", "KeyZ", "tbIndLeft");
  key("tb ind-r", "tbIndR", "\u25b6", "KeyC", "tbIndRight");
  key("tb gear-up", "tbGearUp", "\u25b2", "KeyE", "tbGearUp");
  key("tb gear-dn", "tbGearDn", "\u25bc", "KeyQ", "tbGearDown");
  key("tb hb", "tbHb", "P", "Space", "tbHandbrake");
  // everything that is needed now and then, behind "⋯"
  auto dots = button("tb more-btn", "\u22ef", [this]() {
    moreVisible_ = !moreVisible_;
    host_.setVisible("more", moreVisible_);
  });
  dots->id = "touchMore";
  auto more = node("div", "more");
  more->id = "more";
  auto item = [&](const char* id, const char* labelKey, const char* code, const char* litId) {
    auto b = button("mb", t(labelKey), [this, code]() {
      if (input_) input_->press(code, true);
    });
    b->id = id;
    (void)litId;
    more->add(std::move(b));
  };
  item("mbBelt", "tbBelt", "KeyB", "mbBelt");
  item("mbStarter", "tbStarter", "Enter", "mbStarter");
  item("mbIgnition", "tbIgnition", "KeyI", "mbIgnition");
  item("mbLights", "tbLights", "KeyL", "mbLights");
  item("mbHighBeam", "tbHighBeam", "KeyO", "mbHighBeam");
  item("mbHazard", "tbHazard", "KeyX", "mbHazard");
  item("mbHorn", "tbHorn", "KeyH", "mbHorn");
  item("mbLookLeft", "tbLookLeft", "KeyJ", "mbLookLeft");
  item("mbLookRight", "tbLookRight", "KeyK", "mbLookRight");
  item("mbCamera", "tbCamera", "KeyV", "mbCamera");
  item("mbReset", "tbReset", "KeyT", "mbReset");
  auto menu = button("mb", t("tbMenu"), [this]() {
    moreVisible_ = false;
    host_.setVisible("more", false);
    if (onPause) onPause();
  });
  menu->id = "mbMenu";
  more->add(std::move(menu));
  touchRoot->add(std::move(more));
  touchRoot->add(std::move(dots));
  hud_->add(std::move(touchRoot));
}

/** Fingers or keys?  Shows the on-screen controls or the key reminder. */
void Hud::setInputMode(const std::string& mode) {
  const std::string o = std::to_string(std::max(0.15, std::min(1.0, settings_.touchOpacity)));
  host_.setText("touchOpacity", o);
  if (mode == mode_) return;
  mode_ = mode;
  host_.setClass("hud", mode == "touch" ? "hud touchmode" : "hud");
  host_.setVisible("more", false);
  host_.setVisible("touch", mode == "touch");
}

/** The short reminder of the keys, depending on the gearbox chosen. */
std::string Hud::keyReminder() const {
  const bool de = settings_.lang == LangSetting::De;
  auto k = [&](std::initializer_list<const char*> keys) {
    std::string out;
    for (const char* x : keys) out += "<kbd>" + std::string(x) + "</kbd>";
    return out;
  };
  const std::string tr = settings_.transmission == Transmission::Automatic
                              ? "automatic"
                              : (settings_.transmission == Transmission::Manual ? "manual" : "assist");
  std::vector<std::string> parts;
  if (tr == "automatic") {
    parts.push_back(k({"E", "Q"}) + " " + (de ? "Wählhebel P·R·N·D" : "selector P·R·N·D"));
    parts.push_back(k({"P"}) + " " + (de ? "Parken" : "park"));
    parts.push_back(k({"R"}) + " " + (de ? "rückwärts" : "reverse"));
    parts.push_back(k({"N"}) + " " + (de ? "Leerlauf" : "neutral"));
  } else {
    if (tr == "manual") parts.push_back(k({"Shift"}) + " " + (de ? "Kupplung" : "clutch"));
    parts.push_back(k({"1"}) + "\u2013" + k({"6"}) + " " + (de ? "Gang" : "gear"));
    parts.push_back(k({"R"}) + " " + (de ? "rückwärts" : "reverse"));
    parts.push_back(k({"N"}) + " " + (de ? "Leerlauf" : "neutral"));
    parts.push_back(k({"E", "Q"}) + " " + (de ? "hoch / runter" : "up / down"));
  }
  parts.push_back(k({"Space"}) + " " + (de ? "Handbremse" : "handbrake"));
  parts.push_back(k({"Enter"}) + " " + (de ? "Motor starten" : "start engine"));
  parts.push_back(k({"Y", "C"}) + " " + (de ? "Blinker" : "indicators"));
  parts.push_back(k({"J", "K"}) + " " + (de ? "Schulterblick" : "shoulder check"));
  parts.push_back(k({"F1"}) + " " + (de ? "alle Tasten" : "all keys"));
  std::string out;
  for (const std::string& x : parts) out += "<span>" + x + "</span>";
  return out;
}


}  // namespace fs

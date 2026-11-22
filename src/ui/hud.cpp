// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Menus and the in-game HUD.
 *
 * Origin: src/ui/hud.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * The panel structure, the control table, the toasts and the mini map are
 * carried over; the DOM calls become HudNode widgets the platform renders.
 */
#include "ui/hud.h"

#include "i18n/index.h"
#include "i18n/violations.h"

#include "game/faultlog.h"

#include "ui/addresspicker.h"

#include <algorithm>
#include <chrono>
#include <cmath>

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

std::string Hud::t(const std::string& key) const { return fs::tr(key, settings_.lang); }

std::string Hud::t(const std::string& key,
                    const std::vector<std::pair<std::string, std::string>>& p) const {
  return fs::tr(key, settings_.lang, p);
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
  auto o = node("div", "overlay");
  auto p = node("div", "panel");
  // the language first: whoever cannot read the page must be able to find it
  p->add(languagePicker([this, spawns]() { showMenu(spawns, 0); }));
  p->add(node("h1", "", "Fahrsim <span>Baunatal</span>"));
  p->add(node("p", "", t("subtitle")));

  // where the last drive began – also after the page was closed
  const Settings::StartPlace& last = settings_.lastStart;
  int spawn = std::min(lastSpawn, std::max(0, static_cast<int>(spawns.size()) - 1));
  if (last.spawn < static_cast<int>(spawns.size())) spawn = last.spawn;
  hasPickedAddress_ = false;
  if (last.hasAddress) {
    hasPickedAddress_ = true;
    pickedAddress_.label = last.label;
    pickedAddress_.street = last.street;
    pickedAddress_.x = last.x;
    pickedAddress_.z = last.z;
    pickedAddress_.exact = last.exact;
  }
  spawn_chose_ = spawn;
  rememberNow_ = nullptr;
  auto remember = [this]() {
    Settings::StartPlace& l = settings_.lastStart;
    l.spawn = spawn_chose_;
    if (hasPickedAddress_) {
      l.hasAddress = true;
      l.label = pickedAddress_.label;
      l.street = pickedAddress_.street;
      l.x = pickedAddress_.x;
      l.z = pickedAddress_.z;
      l.exact = pickedAddress_.exact;
    } else {
      l.hasAddress = false;
    }
    saveSettings(settings_);
  };
  rememberNow_ = remember;

  auto opts = node("div");
  auto rowSpawn = node("div", "row");
  auto selSpawn = node("select");
  selSpawn->id = "spawn";
  std::vector<int> spawnIdxOf(spawns.size());
  for (size_t i = 0; i < spawns.size(); i++) {
    spawnIdxOf[i] = static_cast<int>(i);
    auto op = node("option", "", spawns[i].name);
    op->id = "spawn:" + std::to_string(i);
    op->click = [this, i]() {
      spawn_chose_ = static_cast<int>(i);
      hasPickedAddress_ = false;
      if (rememberNow_) rememberNow_();
    };
    selSpawn->add(std::move(op));
  }
  if (spawn >= 0 && spawn < static_cast<int>(spawns.size()))
    selSpawn->text = std::to_string(spawn);
  // (the platform shows the option the select's text names)
  rowSpawn->add(node("label", "", t("startPoint")));
  rowSpawn->add(std::move(selSpawn));
  // … or anywhere: street and house number
  auto rowAddr = node("div", "row");
  menuPicker_ = std::make_unique<AddressPicker>(addressPicker(AddressPickerOptions{
      t("addressPlaceholder"),
      t("realityLoading"),
      t("addressNone"),
      [this]() { return loadAddresses ? loadAddresses() : nullptr; },
      [this](const AddressHit& hit) {
        pickedAddress_ = hit;
        hasPickedAddress_ = true;
        if (rememberNow_) rememberNow_();
      },
  }));
  {
    HudNode* pickRoot = menuPicker_->node.get();
    rowAddr->add(node("label", "", t("startAddress")));
    rowAddr->children.push_back(std::move(menuPicker_->node));
    (void)pickRoot;
  }
  // a car, or the truck with its trailer (class CE) – each with lessons of its own
  const bool truck = settings_.vehicle == VehicleKind::Truck;
  auto rowVeh = node("div", "row");
  auto selVeh = node("select");
  selVeh->id = "vehicle";
  selVeh->add(node("option", "", t("vehCar")));
  selVeh->add(node("option", "", t("vehTruck")));
  selVeh->text = truck ? "1" : "0";
  for (int i = 0; i < 2; i++) {
    HudNode* op = selVeh->children[static_cast<size_t>(i)].get();
    op->click = [this, spawns, selVehPtr = selVeh.get(), i]() {
      settings_.vehicle = i == 1 ? VehicleKind::Truck : VehicleKind::Car;
      saveSettings(settings_);
      selVehPtr->text = std::to_string(i);
      showMenu(spawns, spawn_chose_);
    };
  }
  rowVeh->add(node("label", "", t("vehicle")));
  rowVeh->add(std::move(selVeh));
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
  selTr->text = settings_.transmission == Transmission::Assist
                    ? "0"
                    : (settings_.transmission == Transmission::Manual ? "1" : "2");
  for (int i = 0; i < 3; i++) {
    HudNode* op = selTr->children[static_cast<size_t>(i)].get();
    op->click = [this, spawns, selTrPtr = selTr.get(), i]() {
      selTrPtr->text = std::to_string(i);
      settings_.transmission = i == 0 ? Transmission::Assist
                                      : (i == 1 ? Transmission::Manual : Transmission::Automatic);
      saveSettings(settings_);
      showMenu(spawns, spawn_chose_);
    };
  }
  rowTr->add(node("label", "", t("transmission")));
  rowTr->add(std::move(selTr));
  opts->add(std::move(rowSpawn));
  opts->add(std::move(rowAddr));
  opts->add(std::move(rowVeh));
  // (the truck has an automated gearbox, as nearly all of them today)
  if (!truck) opts->add(std::move(rowTr));
  p->add(std::move(opts));

  p->add(node("h2", "", t("start")));
  auto cards = node("div", "cards");
  auto card = [&](const std::string& title, const std::string& desc, const std::string& mode) {
    auto c = button("card", "<b>" + title + "</b><small>" + desc + "</small>",
                    [this, mode]() {
                      if (!onStart) return;
                      StartRequest r;
                      r.mode = mode;
                      r.spawn = spawn_chose_;
                      r.address = pickedAddress_;
                      r.hasAddress = hasPickedAddress_;
                      onStart(r);
                    });
    cards->add(std::move(c));
  };
  card(t("free"), t("freeDesc"), "free");
  card(t("demo"), t("demoDesc"), "demo");
  // the lessons of the chosen vehicle: the practice yard first, the test last
  auto rank = [](const std::string& kind) {
    return kind == "yard" ? 0 : (kind == "exam" ? 2 : 1);
  };
  std::vector<LessonDef> ls;
  for (const auto& l : lessons()) {
    const std::string veh = l.vehicle.empty()
                                ? (truck ? "truck" : "car")
                                : l.vehicle;
    if (veh != (truck ? "truck" : "car")) continue;
    if (l.kind == "yard" && !world_.json().hasYard) continue;
    ls.push_back(l);
  }
  std::stable_sort(ls.begin(), ls.end(), [&](const LessonDef& a, const LessonDef& b) {
    return rank(a.kind) < rank(b.kind);
  });
  // (the lessons on the road were written for the car: what is different with the
  // truck is said first)
  const std::string note = truck ? t("lesson.truckNote") + " " : "";
  for (const auto& l : ls)
    card(lessonTitle(l.id, settings_.lang),
         l.id == "exam" ? t("examDesc")
                        : (l.kind == "route" ? note : "") + lessonIntro(l.id, settings_.lang),
         l.id);
  p->add(std::move(cards));

  auto b = node("div", "buttons");
  auto back = [this, spawns, spawn]() { showMenu(spawns, spawn); };
  b->add(button("btn ghost", t("settings"), [this, back]() { showSettings(back); }));
  b->add(button("btn ghost", t("controls"), [this, back]() { showControls(back); }));
  b->add(button("btn ghost", t("about"), [this, back]() { showAbout(back); }));
  b->add(button("btn ghost", t("logTitle"), [this, back]() { showLog(back); }));
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

  {
    // the list of languages, each under its own name (0.6.0)
    auto row = node("div", "row");
    row->add(node("label", "", t("language")));
    row->add(languagePicker([this, back]() { showSettings(back); }, /*inRow=*/true));
    p->add(std::move(row));
  }
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
  // the car
  {
    std::vector<std::pair<std::string, std::string>> powers;
    for (int ps : ENGINE_POWERS) {
      char buf[64];
      std::snprintf(buf, sizeof buf, "%d PS (%d kW)", ps, static_cast<int>(ps * 0.7355));
      powers.push_back({std::to_string(ps), buf});
    }
    select(t("enginePower"), std::to_string(s.power), powers,
           [&s](const std::string& v) { s.power = std::atoi(v.c_str()); });
  }
  p->add(node("p", "note", t("settingsNextDrive")));
  check(t("limiter"), s.limiter, [&s](bool v) { s.limiter = v; });
  // the picture
  select(t("startView"), s.view == View::Cockpit ? "cockpit" : "chase",
         {{"chase", t("viewChase")}, {"cockpit", t("viewCockpit")}},
         [&s](const std::string& v) { s.view = v == "cockpit" ? View::Cockpit : View::Chase; });
  range(t("cabinOpacity"), (s.cabinOpacity - 0.2) / 0.8,
        [&s](double v) { s.cabinOpacity = 0.2 + 0.8 * v; });
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
  // settings live in the browser's storage; say so if this browser keeps none
  if (!saveSettings(s)) p->add(node("p", "note warn", t("settingsNotSaved")));

  auto b = node("div", "buttons");
  b->add(button("btn", t("back"), back));
  p->add(std::move(b));
  o->add(std::move(p));
  setOverlay(std::move(o));
}

void Hud::showControls(const std::function<void()>& back) {
  // the keys as printed on the player's keyboard (0.6.0: [[KeyX]] codes -> caps)
  const std::vector<std::pair<std::vector<std::string>, std::string>> rows = {
      {{"KeyW", "KeyS", "/", "ArrowUp", "ArrowDown"}, "ctl.throttleBrake"},
      {{"KeyA", "KeyD", "/", "ArrowLeft", "ArrowRight"}, "ctl.steer"},
      {{"ShiftLeft"}, "ctl.clutch"},
      {{"KeyE", "KeyQ"}, "ctl.gearUpDown"},
      {{"Digit1", "\xe2\x80\x93", "Digit6", " ", "KeyR", " ", "KeyN"}, "ctl.gearDirect"},
      {{"KeyP"}, "ctl.park"},
      {{"Space"}, "ctl.handbrake"},
      {{"Enter", "/", "KeyI"}, "ctl.engine"},
      {{"KeyZ", "KeyC", "/", "KeyX"}, "ctl.indicators"},
      {{"KeyJ", "KeyK"}, "ctl.look"},
      {{"KeyU"}, "ctl.lookBack"},
      {{"KeyB"}, "ctl.belt"},
      {{"KeyL", "/", "KeyO"}, "ctl.lights"},
      {{"KeyH"}, "ctl.horn"},
      {{"KeyV"}, "ctl.camera"},
      {{"KeyT"}, "ctl.reset"},
      {{"KeyF"}, "ctl.reality"},
      {{"KeyM"}, "ctl.map"},
      {{"Escape"}, "ctl.pause"},
      {{"F1"}, "ctl.help"},
  };
  auto o = node("div", "overlay");
  auto p = node("div", "panel");
  p->add(node("h1", "", t("controls")));
  auto table = node("table", "keys");
  for (const auto& kv : rows) {
    auto tr = node("tr");
    // the caps, or the separators between them, exactly as the row lists them
    std::string caps;
    for (const std::string& code : kv.first) {
      if (code == "/" || code == "\xe2\x80\x93" || code == " ")
        caps += code == " " ? " " : (code == "/" ? " / " : "\xe2\x80\x93");
      else
        caps += "<kbd>" + keyName(code, settings_.lang) + "</kbd>";
    }
    tr->add(node("td", "", caps));
    tr->add(node("td", "", t(kv.second)));
    table->add(std::move(tr));
  }
  p->add(std::move(table));
  p->add(node("p", "", t("ctl.layoutNote")));
  p->add(node("p", "", "<b>" + t("ctl.touchTitle") + ":</b> " + t("ctl.touchText")));
  p->add(node("p", "", "<b>" + t("ctl.padTitle") + ":</b> " + t("ctl.padText")));
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
  p->add(node("p", "", t("about.text")));
  p->add(node("h2", "", t("credits")));
  p->add(node("p", "", t("about.dataOsm")));
  p->add(node("p", "", t("about.dataHessen")));
  p->add(node("p", "", t("about.signs")));
  p->add(node("p", "", t("about.photos")));
  p->add(node("h2", "", t("source")));
  p->add(node("p", "",
              "<a href=\"https://github.com/MaurerAnton/driving-simulator\" "
              "target=\"_blank\" rel=\"noopener\">github.com/MaurerAnton/driving-simulator</a>"));
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
  b->add(button("btn ghost", t("mapTitle"), [this]() {
    if (onMap) onMap();
  }));
  b->add(button("btn ghost", t("realityButton"), [this]() {
    if (onReality) onReality();
  }));
  if (canTeleport) {
    b->add(button("btn ghost", t("teleport"), [this]() { showTeleport(); }));
    // … or have the car drive somewhere by itself and watch how it is done
    if (canDemo)
      b->add(button("btn ghost", t("demo"), [this]() {
        if (onDemo) onDemo();
      }));
  }
  b->add(button("btn ghost", t("logTitle"), [this]() { showLog([this]() { showPause(); }); }));
  b->add(button("btn ghost", t("settings"), [this]() { showSettings([this]() { showPause(); }); }));
  b->add(button("btn ghost", t("controls"), [this]() { showControls([this]() { showPause(); }); }));
  if (canFinish) {
    // a free drive ends when the driver says so: the result, then the main menu
    b->add(button("btn ghost", t("endDrive"), [this]() {
      if (onFinish) onFinish();
    }));
  }
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
  p->add(node("h1", "", t("result") + ": " + r.title));
  if (r.exam)
    p->add(node("div", r.passed ? "verdict ok" : "verdict bad",
                r.passed ? t("passed") : t("failed")));
  // distance and duration the way the language writes them (0.6.0)
  const std::string km = formatUnit(r.distanceKm, "kilometer", settings_.lang, 1);
  const std::string min = formatUnit(r.minutes, "minute", settings_.lang);
  p->add(node("p", "", t("distance") + ": " + km + " \u00b7 " + t("duration") + ": " + min +
                         " \u00b7 " + t("points") + ": " + std::to_string(r.points)));
  if (r.log.empty()) p->add(node("p", "", t("noFaults")));
  // group identical faults
  std::vector<std::pair<const Violation*, int>> groups;
  for (const auto& v : r.log) {
    bool found = false;
    for (auto& g : groups)
      if (g.first->id == v.id) {
        g.second++;
        found = true;
        break;
      }
    if (!found) groups.push_back({&v, 1});
  }
  auto order = [](Severity sv) {
    return sv == Severity::Fail ? 0 : sv == Severity::Major ? 1
                                   : sv == Severity::Minor  ? 2
                                                            : 3;
  };
  std::stable_sort(groups.begin(), groups.end(),
                   [&](const auto& a, const auto& b) { return order(a.first->severity) < order(b.first->severity); });
  for (const auto& g : groups) {
    const Violation& v = *g.first;
    std::string sev = v.severity == Severity::Hint ? "hint"
                       : v.severity == Severity::Minor ? "minor"
                       : v.severity == Severity::Major  ? "major"
                                                        : "fail";
    auto f = node("div", "fault " + sev);
    f->add(node("b", "", violationTitle(v.id, settings_.lang, v.data) +
                            (g.second > 1 ? " (" + std::to_string(g.second) + "\u00d7)" : "")));
    f->add(node("small", "", violationWhy(v.id, settings_.lang)));
    auto em = node("em", "", this->t(sev) + " \u00b7 ");
    em->add(ruleNode(v.id));   // the citation, each paragraph a link
    f->add(std::move(em));
    p->add(std::move(f));
  }
  auto b = node("div", "buttons");
  b->add(button("btn", t("menu"), [this]() {
    if (onQuit) onQuit();
    hideOverlay();
  }));
  b->add(button("btn ghost", t("logTitle"), [this, r]() {
    showLog([this, r]() { showResult(r); });
  }));
  p->add(std::move(b));
  o->add(std::move(p));
  setOverlay(std::move(o));
}

/**
 * The rule a fault refers to: the citation of the StVO, each paragraph and
 * annex a link to the official text (the original: an <a> with the URL from
 * citationParts; a reference that is a text key just shows the text).
 */
std::unique_ptr<HudNode> Hud::ruleNode(const std::string& id) const {
  std::string ref;
  for (const auto& e : VIOLATION_REFS)
    if (e.first == id) {
      ref = e.second;
      break;
    }
  auto node_ = node("span", "rule");
  if (ref.rfind("@", 0) == 0 || ref.empty()) {
    node_->text = violationRef(id, settings_.lang);
    return node_;
  }
  for (const auto& part : citationParts(ref)) {
    if (part.url.empty()) {
      node_->text += part.text;
      continue;
    }
    auto a = node("a", "");
    a->text = part.text;
    // shown by the platform as a link; title = what opens it
    a->html = "<a href=\"" + part.url + "\" target=\"_blank\" rel=\"noopener noreferrer\">" +
              part.text + "</a>";
    a->id = part.url;                 // the platform opens exactly this URL
    a->click = [url = part.url]() {   // (native: the host decides what a URL means)
      (void)url;
    };
    node_->add(std::move(a));
  }
  return node_;
}

/** date and time of a fault, as its locale writes them (medium date, short time). */
std::string Hud::faultWhen(double ms) const {
  time_t secs = static_cast<time_t>(ms / 1000.0);
  struct tm tmv;
  gmtime_r(&secs, &tmv);
  static const char* const MONTHS[12] = {
      "January", "February", "March", "April", "May", "June",
      "July", "August", "September", "October", "November", "December"};
  static const char* const MONTHS_DE[12] = {
      "Januar", "Februar", "März", "April", "Mai", "Juni",
      "Juli", "August", "September", "Oktober", "November", "Dezember"};
  const bool german = settings_.lang == "de";
  char out[80];
  if (german)
    std::snprintf(out, sizeof out, "%d. %s %d, %02d:%02d", tmv.tm_mday,
                  MONTHS_DE[tmv.tm_mon], 1900 + tmv.tm_year, tmv.tm_hour, tmv.tm_min);
  else
    std::snprintf(out, sizeof out, "%s %d, %d, %d:%02d %s", MONTHS[tmv.tm_mon], tmv.tm_mday,
                  1900 + tmv.tm_year, tmv.tm_hour % 12 == 0 ? 12 : tmv.tm_hour % 12, tmv.tm_min,
                  tmv.tm_hour < 12 ? "AM" : "PM");
  return out;
}

void Hud::showLog(const std::function<void()>& back) {
  auto o = node("div", "overlay");
  auto p = node("div", "panel");
  p->add(node("h1", "", t("logTitle")));
  static const std::vector<FaultEntry> NO_ENTRIES;
  const std::vector<FaultEntry>& entries = faultLog ? faultLog->entries() : NO_ENTRIES;
  if (entries.empty())
    p->add(node("p", "", t("logEmpty")));
  else
    p->add(node("p", "", t("logText")));
  auto list = node("div", "log");
  for (size_t i = entries.size(); i-- > 0;) {
    const FaultEntry& e = entries[i];
    const std::string sevKey =
        e.severity == Severity::Hint ? "hint"
        : e.severity == Severity::Minor ? "minor"
        : e.severity == Severity::Major  ? "major"
                                         : "fail";
    auto f = node("div", "fault " + sevKey);
    // when · street · lesson or free drive
    std::string where = faultWhen(e.t);
    if (!e.street.empty()) where += " · " + e.street;
    where += std::string(" · ") + (e.mode == "free"    ? t("free")
                                        : e.mode == "exam"  ? t("exam")
                                                            : lessonTitle(e.mode, settings_.lang));
    f->add(node("small", "when", where));
    f->add(node("b", "", violationTitle(e.id, settings_.lang, e.data)));
    f->add(node("small", "", violationWhy(e.id, settings_.lang)));
    auto em = node("em", "", t(sevKey) + " · ");
    em->add(ruleNode(e.id));
    f->add(std::move(em));
    list->add(std::move(f));
  }
  p->add(std::move(list));
  auto b = node("div", "buttons");
  b->add(button("btn", t("back"), back));
  if (!entries.empty())
    b->add(button("btn ghost", t("logClear"), [this, back]() {
      if (faultLog) faultLog->clear();
      showLog(back);
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
  // warning lamps as in an instrument cluster: parking brake and seat belt
  auto lampBrake = node("span", "lamp",
                        "<svg viewBox=\"0 0 34 24\" aria-hidden=\"true\"><circle cx=\"17\" "
                        "cy=\"12\" r=\"7.6\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.2\"/>"
                        "<path d=\"M6 4.2a11.5 11.5 0 0 0 0 15.6M28 4.2a11.5 11.5 0 0 1 0 15.6\" "
                        "fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.2\" stroke-linecap=\"round\"/>"
                        "<text x=\"17\" y=\"16\" text-anchor=\"middle\" font-size=\"10.5\" font-weight=\"800\" "
                        "font-family=\"sans-serif\" fill=\"currentColor\">P</text></svg>");
  lampBrake->id = "lampBrake";
  auto lampBelt = node("span", "lamp",
                       "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><circle cx=\"12\" cy=\"5\" "
                       "r=\"3.2\" fill=\"currentColor\"/><path d=\"M6 22v-6.5a6 6 0 0 1 12 0V22\" "
                       "fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.3\"/>"
                       "<path d=\"M7.2 11.2 17 21\" stroke=\"currentColor\" stroke-width=\"2.3\" "
                       "stroke-linecap=\"round\"/></svg>");
  lampBelt->id = "lampBelt";
  dash->add(std::move(indL));
  dash->add(std::move(lampBrake));
  dash->add(std::move(dashV));
  dash->add(node("span", "u", "km/h"));
  dash->add(std::move(dashG));
  dash->add(std::move(rpm));
  dash->add(std::move(lampBelt));
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
  // … and a corner of it opens the whole map
  auto expand = node("button", "expand",
                     "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path fill=\"none\" "
                     "stroke=\"currentColor\" stroke-width=\"2.4\" stroke-linecap=\"round\" "
                     "stroke-linejoin=\"round\" d=\"M14 4h6v6M10 20H4v-6M20 4l-7 7M4 20l7-7\"/></svg>");
  expand->id = "expand";
  expand->click = [this]() {
    if (onMap) onMap();
  };
  map->add(std::move(expand));
  auto score = node("div", "score");
  score->id = "score";
  auto keysBox = node("div", "keyhelp");
  keysBox->id = "keyhelp";

  // the upper left corner: the street-name block and, under it, things to choose from while standing
  auto side = node("div", "side");
  auto choiceBox = node("div", "choices");
  choiceBox->id = "choices";
  choiceBox_ = choiceBox.get();
  side->add(std::move(navBox));
  side->add(std::move(choiceBox));
  h->add(std::move(side));
  h->add(std::move(assist));
  h->add(std::move(dash));
  h->add(std::move(tip));
  h->add(std::move(map));
  h->add(std::move(score));
  h->add(std::move(keysBox));
  return h;
}

/**
 * Font size at which a word ("walk", "none" – in any language and script)
 * fits into the speed-limit roundel: the original measures it with a canvas;
 * here the rule the checker enforces (a word of at most 9 letters fits the
 * width, wide scripts 4) gives the size in the same range 13 … 7 px.
 */
int Hud::fitRoundel(const std::string& text) const {
  size_t widest = 0, word = 0;
  for (char c : text) {
    if (c == ' ') {
      widest = std::max(widest, word);
      word = 0;
    } else {
      word++;
    }
  }
  widest = std::max(widest, word);
  if (widest <= 4) return 13;   // wide scripts: fewer letters fit
  if (widest <= 6) return 11;
  if (widest <= 8) return 9;
  if (widest <= 9) return 8;
  return 7;
}

/**
 * Things to choose from while the vehicle stands (class CE: what is done on
 * foot around truck and trailer; the street-name block above says what is
 * asked).  An empty list takes the panel away.
 * May be called every frame: the panel is rebuilt only when the list changes.
 */
void Hud::setChoices(const std::vector<std::pair<std::string, std::string>>& items,
                     const std::function<void(const std::string&)>& pick) {
  choicePick_ = pick;
  std::string key;
  for (const auto& it : items) {
    if (!key.empty()) key += ",";
    key += it.first;
  }
  if (items.size()) key = settings_.lang + "|" + key;
  if (key == choiceKey_) return;
  choiceKey_ = key;
  if (!choiceBox_) return;
  choiceBox_->children.clear();
  host_.setVisible("choices", !items.empty());
  for (const auto& it : items) {
    auto b = node("button");
    b->text = it.second;
    std::string id = it.first;
    b->click = [this, id]() {
      if (choicePick_) choicePick_(id);
    };
    choiceBox_->add(std::move(b));
  }
  // the panel's children changed: the host rebuilds its copy of the tree
  if (!overlay_) host_.setRoot(hud_.get());
}

void Hud::moved(const std::string& id, double fx, double fy) {
  if (!input_) return;
  Input& in = *input_;
  if (id == "touchSteer") {
    in.touch.steer = std::max(-1.0, std::min(1.0, fx * 2 - 1));
    in.touched();
  } else if (id == "touchGas") {
    in.touch.throttle = std::max(0.15, std::min(1.0, 1 - fy + 0.15));
    in.touched();
  } else if (id == "touchBrake") {
    in.touch.brake = std::max(0.15, std::min(1.0, 1 - fy + 0.15));
    in.touched();
  }
}

void Hud::setHudVisible(bool v) { host_.setVisible("hud", v); }

void Hud::toast(const Violation& v, double seconds) {
  const std::string& lang = settings_.lang;
  std::string sev = v.severity == Severity::Hint ? "hint"
                     : v.severity == Severity::Minor ? "minor"
                     : v.severity == Severity::Major  ? "major"
                                                      : "fail";
  if (seconds < 0) seconds = v.severity == Severity::Hint ? 5.5 : 8;
  notices_.add(violationTitle(v.id, lang, v.data), violationWhy(v.id, lang),
               this->t(sev) + " \u00b7 " + violationRef(v.id, lang), severityOf(v.severity),
               seconds, nowMs());
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
      // a word in any script, fitted to the roundel (0.6.0: its own font size)
      const std::string word = none ? t("limitNone") : (s.zone == 7 ? t("walk") : "");
      const std::string shown = word.empty() ? std::to_string(s.limit) : word;
      if (shown != roundelText_) {
        roundelText_ = shown;
        host_.setText("roundel", shown);
        host_.setText("roundelSize", word.empty() ? "" : std::to_string(fitRoundel(word)));
      }
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
                    std::to_string(std::min(100.0, (s.rpm / (s.rpmMax > 0 ? s.rpmMax : 7000.0)) * 100.0)) + "%");
      host_.setClass("indL", s.blinkOn && (s.hazard || s.indicator < 0) ? "ind on" : "ind");
      host_.setClass("indR", s.blinkOn && (s.hazard || s.indicator > 0) ? "ind on" : "ind");
      // lit while the handbrake is on; large and flashing when the car is driven against it
      host_.setClass("lampBrake",
                     std::string("lamp") + (s.handbrake ? " on" : "") + (s.handbrakeWarn ? " warn" : ""));
      host_.setText("lampBrake", s.handbrake ? t("tbHandbrake") : "");
      host_.setVisible("lampBrake", true);
      host_.setClass("lampBelt", std::string("lamp") + (s.seatbelt ? "" : " on"));
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
      // (the key reminder is shown only while standing or crawling)
      host_.setClass("keyhelp", std::string("keyhelp") + (want ? " show" : ""));
      host_.setVisible("keyhelp", want);
      // (the key reminder keeps clear of the strip)
      host_.setClass("hud", std::string("hud") + (showDash ? " dashon" : "") +
                                (mode_ == "touch" ? " touchmode" : ""));
      const std::string trn =
          gearboxAutomatic
              ? "automatic"
              : (settings_.transmission == Transmission::Automatic ? "automatic"
                 : settings_.transmission == Transmission::Manual   ? "manual"
                                                                    : "assist");
      const std::string key = settings_.lang + ":" + trn;
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
  // The steering bar and the pedals are driven by where the finger is (the
  // platform reports the position with the event); the buttons stand in for
  // keys and are down while the finger is on them.
  auto steer = node("div", "steer");
  steer->id = "touchSteer";
  steer->add(node("i"));
  steer->down = [&input]() { input.touched(); };
  steer->up = [&input]() { input.touch.steer = 0; };
  touchRoot->add(std::move(steer));
  auto pedal = [&](const char* cls, const char* id, const char* label, double* into) {
    auto p = node("div", std::string("pedal ") + cls, label);
    p->id = id;
    p->down = [&input, into]() {
      *into = 1;
      input.touched();
    };
    p->up = [into]() { *into = 0; };
    touchRoot->add(std::move(p));
  };
  pedal("gas", "touchGas", t("tbGas").c_str(), &input.touch.throttle);
  pedal("brk", "touchBrake", t("tbBrake").c_str(), &input.touch.brake);
  /** A button that stands in for a key: down while the finger is on it. */
  auto key = [&](const char* cls, const char* id, const char* label, const char* code,
                 const char* titleKey) {
    auto b = button(cls, label, [&input, code]() { input.press(code, true); });
    b->id = id;
    b->down = [&input, code]() { input.press(code, true); };
    b->up = [&input, code]() { input.press(code, false); };
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
  item("mbLookBack", "tbLookBack", "KeyU", "mbLookBack");
  item("mbMap", "mapTitle", "KeyM", "mbMap");
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

/**
 * The list of languages, each under its own name.  Choosing one registers its
 * texts and draws the current page again (the original fetches its file).
 */
std::unique_ptr<HudNode> Hud::languagePicker(const std::function<void()>& redraw, bool inRow) {
  auto sel = node("select", inRow ? "" : "lang");
  sel->id = "lang";
  for (const LangInfo& l : LANGS) {
    auto op = node("option", "", l.name);
    op->id = "lang:" + l.code;   // the option shows the name, carries the code
    sel->add(std::move(op));
  }
  sel->text = settings_.lang;
  for (auto& op : sel->children) {
    // choosing an option (the platform clicks it) registers the texts,
    // saves the setting and redraws the page in the new language
    std::string code = op->id.substr(5);   // after "lang:"
    op->click = [this, redraw, code]() {
      const bool ok = loadLang(code);
      if (ok) {
        settings_.lang = code;
        saveSettings(settings_);
        if (onSettingsChanged) onSettingsChanged();
      }
      redraw();
    };
  }
  return sel;
}

/** Key caps for the keys with these codes, as printed on the player's keyboard. */
std::string Hud::keycaps(const std::vector<std::string>& codes, const std::string& sep) const {
  // tokens that are not key codes ("/", "–", " ") are printed as they are
  auto isCode = [](const std::string& c) {
    return c.rfind("Key", 0) == 0 || c.rfind("Digit", 0) == 0 || c.rfind("Arrow", 0) == 0 ||
           c == "Space" || c == "Enter" || c == "NumpadEnter" || c == "ShiftLeft" ||
           c == "ShiftRight" || c == "Escape" || (c.size() == 2 && c[0] == 'F');
  };
  std::string out;
  for (size_t i = 0; i < codes.size(); i++) {
    if (i && sep == " ") out += " ";
    else if (i) out += sep;
    out += isCode(codes[i]) ? "<kbd>" + keyName(codes[i], settings_.lang) + "</kbd>" : codes[i];
  }
  return out;
}

/** The short reminder of the keys, depending on the gearbox chosen. */
std::string Hud::keyReminder() const {
  // the caps as the player's keyboard prints them, the words as the language
  // writes them (0.6.0: keyName + the kr.* texts)
  auto item = [&](const std::string& textKey, const std::vector<std::string>& codes,
                  const std::string& sep = " ") {
    return "<span>" + keycaps(codes, sep) + " " + t(textKey) + "</span>";
  };
  const std::string trn = gearboxAutomatic
                              ? "automatic"
                              : (settings_.transmission == Transmission::Automatic
                                     ? "automatic"
                                     : (settings_.transmission == Transmission::Manual ? "manual" : "assist"));
  std::vector<std::string> parts;
  if (trn == "automatic") {
    parts.push_back(item("kr.selector", {"KeyE", "KeyQ"}));
    parts.push_back(item("kr.park", {"KeyP"}));
    parts.push_back(item("kr.reverse", {"KeyR"}));
    parts.push_back(item("kr.neutral", {"KeyN"}));
  } else {
    if (trn == "manual") parts.push_back(item("kr.clutch", {"ShiftLeft"}));
    parts.push_back(item("kr.gear", {"Digit1", "\xe2\x80\x93", "Digit6"}));
    parts.push_back(item("kr.reverse", {"KeyR"}));
    parts.push_back(item("kr.neutral", {"KeyN"}));
    parts.push_back(item("kr.upDown", {"KeyE", "KeyQ"}));
  }
  parts.push_back(item("kr.handbrake", {"Space"}));
  parts.push_back(item("kr.startEngine", {"Enter"}));
  parts.push_back(item("kr.indicators", {"KeyZ", "KeyC"}));
  parts.push_back(item("kr.shoulder", {"KeyJ", "KeyK"}));
  parts.push_back(item("kr.camera", {"KeyV"}));
  parts.push_back(item("kr.map", {"KeyM"}));
  parts.push_back(item("kr.allKeys", {"F1"}));
  std::string out;
  for (const std::string& x : parts) out += x;
  return out;
}


}  // namespace fs

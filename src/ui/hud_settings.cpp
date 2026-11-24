// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/hud.ts @ v1.7.7
/**
 * The Hud's settings page and the table of the controls.
 *
 * Of the original's settings, those of parts that are not in 2.0.0 (docs/STATUS.md,
 * "Later") have no row here: the main-beam assist, the flight assist, the running
 * speed and the way of walking, beginning a drive on foot, the menu's style (stage,
 * rows), the vehicles' finer detail, real water, the weather on the vehicle and the
 * roads, the weather and the season with their credit, the key for the fuel prices,
 * the developer's map modes, and WebAssembly on or off (all of this port is).  The
 * version picker and the map packs come through their hooks (hud.h).  In the table of
 * the controls the sections of walking, the tram, the aeroplane, the cranes, the
 * forklift and the articulated lorry are left out, and the rows of the wipers, the
 * service, the doors, the reality view, replays and following one's real position.
 */
#include <algorithm>
#include <cmath>
#include <variant>

#include "audio/speech.h"
#include "core/js.h"
#include "input/camzoom.h"
#include "platform/intl.h"
#include "platform/plat.h"
#include "ui/closex.h"
#include "ui/hud.h"
#include "ui/hud_parts.h"
#include "ui/lessonroutes.h"
#include "ui/regions.h"
#include "ui/voicesettings.h"

namespace fs {

using dom::El;
using dom::el;
using hudParts::run;

/** the tilt steering was switched: once the game has done it, the settings are stored and everybody told */
static Task<void> tiltSteerSwitched(Hud* hud, Settings* s, bool on) {
  const std::function<Task<void>(bool)> ask = hud->onTiltSteer;
  co_await ask(on);
  saveSettings(*s);
  run(hud->onSettingsChanged);
}

void Hud::showSettings(std::function<void()> back) {
  Settings& s = settings;
  El o = el("div", "overlay");
  El p = el("div", "panel narrow");
  p.append(closeX(t("back"), back), el("h1", "", t("settings")));
  // the settings in tabs: one group of rows at a time, chosen by the bar (a page each,
  //  so a long list of settings is not one wall of rows)
  const std::vector<std::pair<std::string, std::string>> tabs = {
      {"drive", t("settingsTab.drive")}, {"graphics", t("settingsTab.graphics")}, {"sound", t("settingsTab.sound")}, {"display", t("settingsTab.display")}, {"map", t("settingsTab.map")},
  };
  El bar = el("div", "settings-tabs");
  std::vector<std::pair<std::string, El>> pages;
  for (const auto& [id, label] : tabs) {
    El page = el("div", "settings-tab");
    page.hidden(id != "drive");
    pages.emplace_back(id, page);
    p.append(page);
  }
  El target = pages[0].second;
  const std::function<void(const std::string&)> showTab = [pages, bar](const std::string& id) {
    for (const auto& [tid, page] : pages) {
      page.hidden(tid != id);
      const El btn = bar.querySelector("[data-tab=\"" + tid + "\"]");
      if (btn) {
        btn.toggleClass("on", tid == id);
        btn.setAttribute("aria-selected", tid == id ? "true" : "false");
      }
    }
  };
  auto goTab = [&](const std::string& id) {
    showTab(id);
    for (const auto& [tid, page] : pages)
      if (tid == id) target = page;
  };
  for (const auto& [id, label] : tabs) {
    El btn = el("button", "settings-tab-btn", label);
    btn.type("button");
    btn.dataset("tab", id);
    btn.setAttribute("role", "tab");
    btn.onclick([showTab, id = id] { showTab(id); });
    bar.append(btn);
  }
  p.append(bar);
  goTab("drive");
  using Options = std::vector<std::pair<std::string, std::string>>;
  auto select = [&](const std::string& label, const std::string& value, const Options& options, std::function<void(const std::string& v)> set) {
    El row = el("div", "row");
    El sel = el("select");
    for (const auto& [v, text] : options) {
      El op = el("option");
      op.value(v);
      op.textContent(text);
      sel.append(op);
    }
    sel.value(value);
    sel.onchange([this, sel, set = std::move(set)] {
      set(sel.value());
      saveSettings(settings);
      run(onSettingsChanged);
    });
    row.append(el("label", "", label), sel);
    target.append(row);
  };
  auto check = [&](const std::string& label, bool value, std::function<void(bool v)> set) {
    El row = el("div", "row");
    El c = el("input");
    c.type("checkbox");
    c.checked(value);
    c.onchange([this, c, set = std::move(set)] {
      set(c.checked());
      saveSettings(settings);
      run(onSettingsChanged);
    });
    El l = el("label", "", label);
    l.style("minWidth", "0");
    l.style("flex", "1");
    row.append(c, l);
    target.append(row);
  };
  auto range = [&](const std::string& label, double value, std::function<void(double v)> set) {
    El row = el("div", "row");
    El r = el("input");
    r.type("range");
    r.set("min", "0");
    r.set("max", "1");
    r.set("step", "0.05");
    r.value(jsStr(value));
    r.oninput([this, r, set = std::move(set)] {
      set(jsNumber(r.value()));
      saveSettings(settings);
      run(onSettingsChanged);
    });
    row.append(el("label", "", label), r);
    target.append(row);
  };
  // (the handlers below name the settings by `sp`: they outlive this call)
  Settings* sp = &settings;
  {
    El row = el("div", "row");
    row.append(el("label", "", t("language")), languagePicker([this, back] { showSettings(back); }, true));
    target.append(row);
  }
  select(t("transmission"), s.transmission, {{"assist", t("trAssist")}, {"manual", t("trManual")}, {"automatic", t("trAutomatic")}}, [sp](const std::string& v) { sp->transmission = v; });
  target.append(el("p", "note", t("settingsNextDrive")));
  // where the lessons take place (game/lessonplaces.ts)
  target.append(lessonPlaceRows(tFn(), s, world.json.spawns, [this] {
    saveSettings(settings);
    run(onSettingsChanged);
  }));
  check(t("limiter"), s.limiter, [sp](bool v) { sp->limiter = v; });
  check(t("autopilotAssist"), s.autopilotAssist, [sp](bool v) { sp->autopilotAssist = v; });
  // the simulator's chores left out (game/arcadedrive.ts) and the road physics off (sim/ground.ts) – also in the arcade panel
  check(t("arcadeDrive"), s.arcadeDrive, [sp](bool v) { sp->arcadeDrive = v; });
  target.append(el("p", "note", t("arcadeDriveNote")));
  check(t("evenRoads"), s.evenRoads, [sp](bool v) { sp->evenRoads = v; });
  // (the ground flattened: a field rides as flat as a road – its bumps and camber gone)
  check(t("evenTerrain"), s.evenTerrain, [sp](bool v) { sp->evenTerrain = v; });
  // the light: the game's fair afternoon, or the sun as it stands at the time (the arcade mode's chosen time always has its own)
  select(t("daylight"), s.daylight, {{"day", t("daylightDay")}, {"clock", t("daylightClock")}}, [sp](const std::string& v) { sp->daylight = v; });
  // steering sensitivity: how sharply A/D turn (0.5 gentle … 1 as built … 2 sharp)
  range(t("steerSensitivity"), (s.steerSensitivity - 0.5) / 1.5, [sp](double v) { sp->steerSensitivity = jsRound((0.5 + v * 1.5) * 100) / 100; });
  // --- display ---
  goTab("display");
  // the picture
  select(t("startView"), s.view, {{"chase", t("viewChase")}, {"cockpit", t("viewCockpit")}}, [sp](const std::string& v) { sp->view = v; });
  range(t("cabinOpacity"), (s.cabinOpacity - 0.2) / 0.8, [sp](double v) { sp->cabinOpacity = 0.2 + 0.8 * v; });
  // --- graphics ---
  goTab("graphics");
  // Graphics: a preset – and, apart from it, the resolutions; the parts of the preset one by one below
  select(t("quality"), s.quality, {{"low", t("qLow")}, {"medium", t("qMedium")}, {"high", t("qHigh")}, {"ultra", t("qUltra")}, {"nomercy", t("qNoMercy")}, {"custom", t("qCustom")}}, [this, sp, back](const std::string& v) {
    // ("No mercy" never comes by itself: only after a warning, and an OK – 1.7.1)
    if (v == "nomercy" && !plat::confirm(t("noMercyAsk"))) {
      plat::soon([this, back] { showSettings(back); });
      return;
    }
    sp->quality = v;
    if (v != "custom") {
      const Graphics* preset = GRAPHICS_PRESETS.find(v);
      if (preset) sp->graphics = *preset;
    }
    // (the resolutions are settings of their own: a graphics preset does not take them over –
    //  the low preset draws less, but the render and display resolution stay as chosen)
    // (the advanced settings below show the preset's values)
    plat::soon([this, back] { showSettings(back); });
  });
  auto pct = [](const std::vector<double>& list) {
    Options out;
    for (const double n : list) out.emplace_back(jsStr(n), jsStr(jsRound(n * 100)) + " %");
    return out;
  };
  select(t("uiScale"), jsStr(s.uiScale), pct({0.75, 0.85, 1, 1.15, 1.3, 1.5}), [sp](const std::string& v) { sp->uiScale = jsNumber(v); });
  // (a resolution of one's own takes the preset to "custom": it is no longer what the preset sets)
  select(t("pixelCap"), jsStr(s.pixelCap), {{"1", t("pixelStandard")}, {"1.5", t("pixelSharp")}, {"3", t("pixelNative")}}, [sp](const std::string& v) {
    sp->pixelCap = jsNumber(v);
    sp->quality = "custom";
  });
  select(t("render3d"), jsStr(s.render3d), pct({0.5, 0.6, 0.7, 0.75, 0.8, 0.9, 1}), [sp](const std::string& v) {
    sp->render3d = jsNumber(v);
    sp->quality = "custom";
  });
  {
    // the advanced graphics settings, folded away; changing one makes the preset "custom"
    El box = el("details", "advanced");
    box.append(el("summary", "", t("gfxAdvanced")));
    const int at = target.childElementCount();
    const Graphics& g = s.graphics;
    select(t("gfxShadows"), g.shadows, {{"off", t("shadowsOff")}, {"near", t("shadowsNear")}, {"far", t("shadowsFar")}}, [sp](const std::string& v) {
      sp->graphics.shadows = v;
      sp->quality = "custom";
    });
    {
      Options views;
      for (const double d : VIEW_DISTANCES) views.emplace_back(jsStr(d), jsStr(d) + " m");
      select(t("gfxView"), jsStr(g.viewDistance), views, [sp](const std::string& v) {
        sp->graphics.viewDistance = jsNumber(v);
        sp->quality = "custom";
      });
    }
    select(t("gfxTrees"), jsStr(g.propDensity), pct({0, 0.35, 0.6, 0.8, 1}), [sp](const std::string& v) {
      sp->graphics.propDensity = jsNumber(v);
      sp->quality = "custom";
    });
    check(t("gfxFacades"), g.facades, [sp](bool v) {
      sp->graphics.facades = v;
      sp->quality = "custom";
    });
    check(t("gfxPropShadows"), g.propShadows, [sp](bool v) {
      sp->graphics.propShadows = v;
      sp->quality = "custom";
    });
    // the ground beyond the map: the hills on the horizon
    {
      Options fars;
      for (const double m : FAR_VIEWS) fars.emplace_back(jsStr(m), m == 0 ? t("farOff") : jsStr(m / 1000) + " km");
      select(t("gfxFar"), jsStr(g.farView), fars, [sp](const std::string& v) {
        sp->graphics.farView = jsNumber(v);
        sp->quality = "custom";
      });
    }
    // (the rows were added to the panel: into the folded box with them)
    while (target.childElementCount() > at) box.append(target.child(at));
    target.append(box);
  }
  select(t("mirrors"), s.mirrors, {{"off", t("mOff")}, {"alternate", t("mAlt")}, {"full", t("mFull")}}, [sp](const std::string& v) { sp->mirrors = v; });
  range(t("traffic"), s.traffic, [sp](double v) { sp->traffic = v; });
  // --- sound ---
  goTab("sound");
  range(t("volume"), s.volume, [sp](double v) { sp->volume = v; });
  // music made as it plays: off unless asked for
  select(t("music"), s.music, {{"off", t("musicOff")}, {"quiet", t("musicQuiet")}, {"on", t("musicOn")}}, [sp](const std::string& v) { sp->music = v; });
  range(t("musicVolume"), s.musicVolume, [sp](double v) { sp->musicVolume = v; });
  // --- display (the on-screen controls, the hints, the picture's aids) ---
  goTab("display");
  select(t("touchControls"), s.touchControls, {{"auto", t("tcAuto")}, {"on", t("tcOn")}, {"off", t("tcOff")}}, [sp](const std::string& v) { sp->touchControls = v; });
  range(t("touchOpacity"), s.touchOpacity, [sp](double v) { sp->touchOpacity = std::max(0.15, v); });
  // steer by tilting the phone: the toggle's change is the gesture iOS asks its permission on
  {
    El row = el("div", "row");
    El c = el("input");
    c.type("checkbox");
    c.checked(s.tiltSteer);
    c.onchange([this, sp, c] { tiltSteerSwitched(this, sp, c.checked()).detach(); });
    row.append(c, el("label", "", t("tiltSteer")));
    target.append(row);
  }
  check(t("keyHints"), s.keyHints, [sp](bool v) { sp->keyHints = v; });
  check(t("showFps"), s.showFps, [sp](bool v) { sp->showFps = v; });
  // --- display again (the start, the screen's aids) ---
  // where a drive begins: where the player is (asked of the browser when the game starts), and exactly at the place chosen
  check(t("startHere"), s.startHere, [sp](bool v) { sp->startHere = v; });
  check(t("placeExact"), s.placeExact, [sp](bool v) { sp->placeExact = v; });
  // the button offered after a tap on the picture (ui/worldtap.ts)
  check(t("teleportTap"), s.teleportTap, [sp](bool v) { sp->teleportTap = v; });
  check(t("mapOnScreen"), s.mapOnScreen, [sp](bool v) { sp->mapOnScreen = v; });
  // a tap on the vehicle's screen (ui/screentap.ts), the zoom of the view (input/camzoom.ts), the inside mirror (ui/hudmirror.ts)
  select(t("screenTap"), s.screenTap, {{"bigmap", t("screenTap.bigmap")}, {"minimap", t("screenTap.minimap")}}, [sp](const std::string& v) { sp->screenTap = v; });
  {
    Options releases;
    for (const ZoomRelease& k : ZOOM_RELEASES) releases.emplace_back(k, t("zoomRelease." + k));
    select(t("zoomRelease"), s.zoomRelease, releases, [sp](const std::string& v) { sp->zoomRelease = v; });
  }
  select(t("insideMirror"), s.insideMirror.mode, {{"car", t("insideMirror.car")}, {"hud", t("insideMirror.hud")}}, [sp](const std::string& v) { sp->insideMirror.mode = v == "hud" ? "hud" : "car"; });
  range(t("insideMirror.shift"), (s.insideMirror.shift + 0.3) / 0.6, [sp](double v) { sp->insideMirror.shift = jsRound((v * 0.6 - 0.3) * 100) / 100; });
  range(t("insideMirror.size"), (s.insideMirror.size - 0.6) / 1.4, [sp](double v) { sp->insideMirror.size = jsRound((0.6 + 1.4 * v) * 100) / 100; });
  target.append(el("p", "note", t("insideMirror.note")));
  // arranging the HUD's parts (ui/hudlayout.ts): while driving, over the picture
  {
    El b = el("div", "buttons");
    if (hud.style("display") != "none") {
      El ar = el("button", "btn ghost", t("hudLayout.arrange"));
      ar.onclick([this] {
        run(onResume);
        layout.arrange();
      });
      b.append(ar);
    }
    El rs = el("button", "btn ghost", t("hudLayout.reset"));
    rs.onclick([this] { layout.reset(); });
    b.append(rs);
    target.append(b);
  }
  // the aerial photographs under the maps – and where they come from
  check(t("mapAerialSetting"), s.reality.aerial, [sp](bool v) { sp->reality.aerial = v; });
  // 2.0: they come from EOX and the states' surveys, and are off until switched on (reality/aerial.h): a note that says so
  target.append(el("p", "note", t("mapAerialSources")));
  check(t("hudRules"), s.hudRules, [sp](bool v) { sp->hudRules = v; });
  check(t("signalTimers"), s.signalTimers, [sp](bool v) { sp->signalTimers = v; });
  check(t("queueLabels"), s.queueLabels, [sp](bool v) { sp->queueLabels = v; });
  check(t("hudGear"), s.hudGear, [sp](bool v) { sp->hudGear = v; });
  // the voice of the spoken instructions – which, how fast, a test (ui/voicesettings.ts): while they are on
  El voiceBox = voiceSettings(VoiceSettingsHost{s.voice, s.volume, s.speech}, lang(), tFn(), [sp] { saveSettings(*sp); });
  voiceBox.hidden(!s.voice);
  check(t("voice"), s.voice, [sp, voiceBox](bool v) {
    sp->voice = v;
    voiceBox.hidden(!v);
    if (!v) speech.stop();
  });
  target.append(voiceBox);
  // --- map & region (the version, the map pack, the region) ---
  goTab("map");
  // the version of the game (an earlier one is a page of its own: ui/versionpicker.ts), the map (one stored from files: ui/mappacks.ts) and the region (ui/regions.ts)
  {
    const std::string locale = localeOf(lang());
    if (versionSettings) target.append(versionSettings(tFn(), [locale](const std::string& iso) { return plat::formatDate(hudParts::dateOf(iso), locale, {.dateStyle = "medium", .timeStyle = "short"}); }));
    if (mapPackSettings) target.append(mapPackSettings(s, tFn(), locale));
    target.append(regionSettings(s, tFn()));
  }
  // the server: an account, the progress kept there, photos, playing together (ui/netpanels.ts)
  if (onAccount) {
    const std::function<void(std::function<void()>)> go = onAccount;
    El acc = el("button", "btn ghost", "👤 " + t("net.account"));
    acc.onclick([this, go, back] { go([this, back] { showSettings(back); }); });
    target.append(acc);
  }
  // settings live in the browser's storage; say so if this browser keeps none
  if (!saveSettings(s)) p.append(el("p", "note warn", t("settingsNotSaved")));
  El b = el("div", "buttons");
  // everything back to the defaults – but the language and what is the player's own (game/settings.ts resetSettings); asked first
  El reset = el("button", "btn ghost", t("settingsReset"));
  reset.onclick([this, back] {
    if (!plat::confirm(t("settingsResetAsk"))) return;
    settings = resetSettings(settings);
    saveSettings(settings);
    run(onSettingsChanged);
    showSettings(back);
  });
  b.append(reset);
  El bb = el("button", "btn", t("back"));
  bb.onclick(back);
  b.append(bb);
  p.append(b);
  o.append(p);
  setOverlay(o);
}

void Hud::showControls(std::function<void()> back) {
  /** what stands for the keys of a row: key caps, and the words between them */
  using Part = std::variant<El, std::string>;
  using Parts = std::vector<Part>;
  auto k = [this](const std::vector<std::string>& codes) {
    Parts out;
    for (const El& cap : keycaps(codes)) out.emplace_back(cap);
    return out;
  };
  auto cap = [](const std::string& text) { return Parts{Part(el("kbd", "", text))}; };
  auto word = [](const std::string& text) { return Parts{Part(text)}; };
  auto all = [](const std::vector<Parts>& lists) {
    Parts out;
    for (const Parts& list : lists) out.insert(out.end(), list.begin(), list.end());
    return out;
  };
  // every key and every button of the game, by where it works – what it does, and when (the text of the row says both)
  struct Row {
    Parts keys;
    std::string text;
  };
  const std::vector<std::pair<std::string, std::vector<Row>>> sections = {
      {"ctl.section.drive",
       {
           {all({k({"KeyW", "KeyS"}), word(" / "), k({"ArrowUp", "ArrowDown"})}), "ctl.throttleBrake"},
           {all({k({"KeyA", "KeyD"}), word(" / "), k({"ArrowLeft", "ArrowRight"})}), "ctl.steer"},
           {k({"ShiftLeft"}), "ctl.clutch"},
           {k({"KeyE", "KeyQ"}), "ctl.gearUpDown"},
           {all({k({"Digit1"}), word("–"), k({"Digit6"}), word(" "), k({"KeyR"}), word(" "), k({"KeyN"})}), "ctl.gearDirect"},
           {k({"KeyP"}), "ctl.park"},
           {k({"Space"}), "ctl.handbrake"},
           {all({k({"Enter"}), word(" / "), k({"KeyI"})}), "ctl.engine"},
           {all({k({"KeyZ", "KeyC"}), word(" / "), k({"KeyX"})}), "ctl.indicators"},
           {k({"KeyJ", "KeyK"}), "ctl.look"},
           {k({"KeyU"}), "ctl.lookBack"},
           {k({"KeyY"}), "ctl.mirror"},
           {k({"KeyB"}), "ctl.belt"},
           {all({k({"KeyL"}), word(" / "), k({"KeyO"})}), "ctl.lights"},
           {k({"KeyO"}), "beam.ctlFlasher"},
           {k({"KeyH"}), "ctl.horn"},
           {k({"KeyV"}), "ctl.camera"},
           {k({"KeyW", "KeyA", "KeyS", "KeyD", "KeyQ", "KeyE"}), "ctl.freeCam"},
           {k({"KeyT"}), "ctl.reset"},
           {k({"KeyM"}), "ctl.map"},
           {k({"Escape"}), "ctl.pause"},
           {k({"F1"}), "ctl.help"},
           {k({"F2"}), "ctl.f2"},
           {k({"F3"}), "ctl.f3"},
       }},
      {"ctl.section.touch",
       {
           {cap("▭"), "ctl.tSteer"},
           {cap("▮ ▮"), "ctl.tPedals"},
           {all({cap("◀"), cap("▶")}), "ctl.tIndicators"},
           {all({cap("▲"), cap("▼")}), "ctl.tGears"},
           {cap("P"), "ctl.tHandbrake"},
           {cap("AP"), "ctl.tAutopilot"},
           {cap("◫"), "ctl.tMirror"},
           {cap("⋯"), "ctl.tMore"},
           {cap("↔"), "ctl.tSwipe"},
       }},
      {"ctl.section.hud",
       {
           {cap("↑ …"), "ctl.hStreet"},
           {cap("◯"), "ctl.hMap"},
           {cap("⬚"), "ctl.hTip"},
           {cap("◀ ▶"), "ctl.hArrows"},
           {cap("⏱"), "ctl.hClock"},
           {cap("●"), "ctl.hSignal"},
           {cap("🚗"), "ctl.hNewVehicle"},
       }},
      {"ctl.section.pause",
       {
           {cap("▶"), "ctl.pResume"},
           {cap("🗺"), "ctl.pMap"},
           {cap("⌖"), "ctl.pTeleport"},
           {cap("A"), "ctl.pDemo"},
           {cap("⏱"), "ctl.pArcade"},
           {cap("🚙"), "ctl.pChange"},
           {cap("≡"), "ctl.pLog"},
           {cap("⚙"), "ctl.pSettings"},
           {cap("⏹"), "ctl.pEnd"},
           {cap("⌂"), "ctl.pMenu"},
       }},
  };
  El o = el("div", "overlay");
  El p = el("div", "panel");
  p.append(closeX(t("back"), back), el("h1", "", t("controls")));
  for (const auto& [title, rows] : sections) {
    p.append(el("h2", "", t(title)));
    El table = el("table", "keys");
    for (const Row& r : rows) {
      El row = el("tr");
      El left = el("td");
      for (const Part& part : r.keys) {
        if (const El* key = std::get_if<El>(&part)) left.append(*key);
        else left.append(std::get<std::string>(part));
      }
      El right = el("td");
      right.textContent(t(r.text));
      row.append(left, right);
      table.append(row);
    }
    p.append(table);
  }
  auto para = [this, p](const std::string& title, const std::string& text) {
    El e = el("p");
    if (!title.empty()) {
      El b = el("b");
      b.textContent(t(title) + ": ");
      e.append(b);
    }
    e.append(t(text));
    p.append(e);
  };
  para("", "ctl.layoutNote");
  para("ctl.touchTitle", "ctl.touchText");
  para("ctl.padTitle", "ctl.padText");
  El b = el("div", "buttons");
  El bb = el("button", "btn", t("back"));
  bb.onclick(back);
  b.append(bb);
  p.append(b);
  o.append(p);
  setOverlay(o);
}

}  // namespace fs

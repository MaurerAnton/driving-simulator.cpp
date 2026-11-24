// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/hud.ts @ v1.7.7
/**
 * The Hud's on-screen controls and the reminder of the keys.
 *
 * (Of the original's sheet behind "⋯", the wipers and the washer are left out, and the
 * aeroplane's autopilot – there and as the "AP" button: docs/STATUS.md, "Later".)
 */
#include <algorithm>
#include <memory>
#include <variant>

#include "core/js.h"
#include "input/input.h"
#include "ui/hud.h"
#include "ui/hud_parts.h"

namespace fs {

using dom::El;
using dom::el;
using hudParts::run;

// ------------------------------------------------------- on-screen controls
void Hud::attachInput(Input& inputRef) {
  input = &inputRef;
  Input* in = &inputRef;
  El tc = el("div", "touch");
  El steer = el("div", "steer");
  El knob = el("i");
  steer.append(knob);
  auto move = [this, in, steer, knob](dom::Event& e) {
    const dom::Rect r = steer.getBoundingClientRect();
    const double v = std::max(-1.0, std::min(1.0, ((e.clientX - r.left) / r.width) * 2 - 1));
    in->touch.steer = v;
    // (flying, the bar is a stick: down the bar is pulled back)
    const double u = std::max(-1.0, std::min(1.0, ((e.clientY - r.top) / r.height) * 2 - 1));
    in->touch.pitch = root.hasClass("flying") ? u : 0;
    in->touched();
    knob.style("left", jsStr(43 + v * 43) + "%");
    knob.style("top", root.hasClass("flying") ? jsStr(40 + u * 40) + "%" : "");
  };
  steer.onpointerdown([steer, move](dom::Event& e) {
    steer.setPointerCapture(e.pointerId);
    move(e);
  });
  steer.onpointermove([move](dom::Event& e) {
    if (e.buttons || e.pointerType == "touch") move(e);
  });
  auto centre = [in, knob](dom::Event&) {
    in->touch.steer = 0;
    in->touch.pitch = 0;
    knob.style("left", "43%");
    knob.style("top", "");
  };
  steer.onpointerup(centre);
  steer.onpointercancel(centre);
  auto pedal = [in](const std::string& cls, const std::string& label, std::function<void(double v)> set) {
    El p = el("div", "pedal " + cls, label);
    auto mv = [in, p, set](dom::Event& e) {
      const dom::Rect r = p.getBoundingClientRect();
      set(std::max(0.15, std::min(1.0, 1 - (e.clientY - r.top) / r.height + 0.15)));
      in->touched();
    };
    p.onpointerdown([p, mv](dom::Event& e) {
      p.setPointerCapture(e.pointerId);
      mv(e);
    });
    p.onpointermove([mv](dom::Event& e) {
      if (e.buttons || e.pointerType == "touch") mv(e);
    });
    p.onpointerup([set](dom::Event&) { set(0); });
    p.onpointercancel([set](dom::Event&) { set(0); });
    return p;
  };
  /** A button that stands in for a key: down while the finger is on it. */
  auto key = [this, in](const std::string& cls, const std::string& label, const std::string& code, const std::string& title = "") {
    El b = el("button", cls, label);
    if (!title.empty()) b.title(title);
    const auto held = std::make_shared<bool>(false);
    auto up = [in, held, code](dom::Event&) {
      if (!*held) return;
      *held = false;
      in->press(code, false);
    };
    b.onpointerdown([in, b, held, code](dom::Event& e) {
      e.preventDefault();
      b.setPointerCapture(e.pointerId);
      *held = true;
      in->press(code, true);
    });
    b.onpointerup(up);
    b.onpointercancel(up);
    lit.insert_or_assign(code, b);
    return b;
  };
  // everything that is needed now and then
  El more = el("div", "more");
  more.hidden(true);
  auto item = [key, more](const std::string& label, const std::string& code) {
    El b = key("mb", label, code);
    b.on("pointerup", [more, code] {
      // a toggle is done with one tap; what must be held (horn, starter, glance) keeps the sheet open
      static const std::vector<std::string> held = {"KeyH", "Enter", "KeyJ", "KeyK", "Digit7", "Flash"};
      if (std::find(held.begin(), held.end(), code) == held.end()) more.hidden(true);
    });
    more.append(b);
  };
  item(t("tbBelt"), "KeyB");
  item(t("tbStarter"), "Enter");
  // (with the engine running the starter's place is taken by switching it off)
  starterBtn = more.lastElementChild();
  item(t("tbEngineOff"), "KeyI");
  engineOffBtn = more.lastElementChild();
  engineOffBtn.hidden(true);
  item(t("tbIgnition"), "KeyI");
  item(t("tbLights"), "KeyL");
  item(t("tbHighBeam"), "KeyO");
  // the headlight flasher: lit while held
  item(t("beam.flasher"), "Flash");
  item(t("tbHazard"), "KeyX");
  item(t("tbHorn"), "KeyH");
  item(t("tbLookLeft"), "KeyJ");
  item(t("tbLookRight"), "KeyK");
  item(t("tbCamera"), "KeyV");
  item(t("tbLookBack"), "KeyU");
  item(t("mapTitle"), "KeyM");
  item(t("tbReset"), "KeyT");
  // (in the aeroplane the same key is its autopilot: the button says so while flying)
  more.lastElementChild().addClass("road-only");
  // turn the vehicle round – into the lane of the other direction (F3, game.ts turnAround)
  item(t("turnAround"), "F3");
  more.lastElementChild().addClass("road-only");
  // full screen and hiding the display (ui/viewbuttons.ts): on a touch screen here, not over the ⋯ button
  auto viewItem = [more](const std::string& label, std::function<void()> act) {
    El b = el("button", "mb", label);
    b.type("button");
    b.onclick([more, act = std::move(act)] {
      more.hidden(true);
      act();
    });
    more.append(b);
    return b;
  };
  viewItem(t("view.fullscreen"), [this] {
    if (view) run(view->toggleFullscreen);
  }).hidden(!dom::root().flag("requestFullscreen"));
  viewItem(t("view.hudHide"), [this] {
    if (view) run(view->toggleHud);
  });
  // (the autopilot's stop: here in the sheet, while the autopilot drives – hidden again once it does not)
  autoStopItem = viewItem(t("autoStop"), [this] { run(onStopAutopilot); });
  autoStopItem.hidden(true);
  El menu = el("button", "mb", t("tbMenu"));
  menu.onclick([this, more] {
    more.hidden(true);
    run(onPause);
  });
  more.append(menu);
  // (a way out of the sheet without choosing anything: close it)
  El close = el("button", "mb mb-close", "✕");
  close.type("button");
  close.title(t("tbClose"));
  close.setAttribute("aria-label", t("tbClose"));
  close.onclick([more] { more.hidden(true); });
  more.append(close);
  moreBox = more;
  El dots = el("button", "tb more-btn", "⋯");
  dots.title(t("tbMore"));
  dots.onclick([more] { more.hidden(!more.hidden()); });
  // (the clutch of a manual gearbox: held to change gear with the ▲▼ keys –
  //  shown where there is a manual gearbox, hidden for an automatic)
  clutchPedal = pedal("clutch", t("tbClutch"), [in](double v) { in->touch.clutch = v; });
  tc.append(steer, pedal("gas", t("tbGas"), [in](double v) { in->touch.throttle = v; }), pedal("brk", t("tbBrake"), [in](double v) { in->touch.brake = v; }), clutchPedal, key("tb ind-l", "◀", "KeyZ", t("tbIndLeft")),
            key("tb ind-r", "▶", "KeyC", t("tbIndRight")), key("tb gear-up", "▲", "KeyE", t("tbGearUp")), key("tb gear-dn", "▼", "KeyQ", t("tbGearDown")), key("tb hb", "P", "Space", t("tbHandbrake")), dots, more);
  hud.append(tc);
  // (the clutch pedal's first state: shown where there is a manual gearbox)
  gearbox(_gearbox);
  // the tip line doubles as a button: "fasten your seat belt – tap here"
  const auto tipHeld = std::make_shared<std::string>();
  auto tipUp = [in, tipHeld](dom::Event&) {
    if (!tipHeld->empty()) in->press(*tipHeld, false);
    tipHeld->clear();
  };
  tip.onpointerdown([this, in, tipHeld](dom::Event& e) {
    if (tipAction.empty() || mode != "touch") return;
    e.preventDefault();
    tip.setPointerCapture(e.pointerId);
    *tipHeld = tipAction;
    in->press(*tipHeld, true);
  });
  tip.onpointerup(tipUp);
  tip.onpointercancel(tipUp);
}

void Hud::setInputMode(const std::string& modeNow) {
  const std::string o = jsStr(std::max(0.15, std::min(1.0, settings.touchOpacity)));
  if (hud.style("--touch-opacity") != o) hud.style("--touch-opacity", o);
  if (modeNow == mode) return;
  mode = modeNow;
  hud.toggleClass("touchmode", mode == "touch");
  if (moreBox) moreBox.hidden(true);
}

std::vector<El> Hud::keyReminder() {
  const Transmission tr = _gearbox ? *_gearbox : settings.transmission;
  /** what an item shows before its text: key caps, and the words between them */
  using Part = std::variant<El, std::string>;
  auto k = [this](const std::vector<std::string>& codes) {
    std::vector<Part> out;
    for (const El& cap : keycaps(codes)) out.emplace_back(cap);
    return out;
  };
  auto item = [this](const std::string& text, const std::vector<Part>& keys) {
    El e = el("span");
    for (const Part& part : keys) {
      if (const El* key = std::get_if<El>(&part)) e.append(*key);
      else e.append(std::get<std::string>(part));
    }
    e.append(" " + t(text));
    return e;
  };
  std::vector<El> out;
  if (tr == "automatic") {
    out.push_back(item("kr.selector", k({"KeyE", "KeyQ"})));
    out.push_back(item("kr.park", k({"KeyP"})));
    out.push_back(item("kr.reverse", k({"KeyR"})));
    out.push_back(item("kr.neutral", k({"KeyN"})));
  } else {
    if (tr == "manual") out.push_back(item("kr.clutch", k({"ShiftLeft"})));
    std::vector<Part> gears = k({"Digit1"});
    gears.emplace_back(std::string("–"));
    for (const Part& part : k({"Digit6"})) gears.push_back(part);
    out.push_back(item("kr.gear", gears));
    out.push_back(item("kr.reverse", k({"KeyR"})));
    out.push_back(item("kr.neutral", k({"KeyN"})));
    out.push_back(item("kr.upDown", k({"KeyE", "KeyQ"})));
  }
  out.push_back(item("kr.handbrake", k({"Space"})));
  out.push_back(item("kr.startEngine", k({"Enter"})));
  out.push_back(item("kr.indicators", k({"KeyZ", "KeyC"})));
  out.push_back(item("kr.shoulder", k({"KeyJ", "KeyK"})));
  out.push_back(item("kr.mirror", k({"KeyY"})));
  out.push_back(item("kr.camera", k({"KeyV"})));
  out.push_back(item("kr.map", k({"KeyM"})));
  out.push_back(item("turnAround", k({"F3"})));
  out.push_back(item("kr.allKeys", k({"F1"})));
  return out;
}

}  // namespace fs

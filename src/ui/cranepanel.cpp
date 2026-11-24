// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/cranepanel.ts @ v1.7.7
#include "ui/cranepanel.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "core/js.h"
#include "i18n/index.h"
#include "platform/intl.h"
#include "platform/plat.h"
#include "platform/sensors.h"

namespace fs {

using dom::El;
using dom::el;

namespace {

/** a key's state: held, and pressed since it was last asked */
std::unordered_set<std::string>& HELD() {
  static std::unordered_set<std::string> held;
  return held;
}

/** The keys of the crane (by position: KeyboardEvent.code). */
struct {
  const char* slewL = "KeyA";
  const char* slewR = "KeyD";
  const char* up = "KeyW";
  const char* down = "KeyS";
  const char* boomUp = "ArrowUp";
  const char* boomDown = "ArrowDown";
  const char* teleIn = "ArrowLeft";
  const char* teleOut = "ArrowRight";
  const char* fine = "ShiftLeft";
  const char* attach = "Space";
  const char* beamsOut = "Digit1";
  const char* beamsIn = "Digit2";
  const char* jacksDown = "Digit3";
  const char* jacksUp = "Digit4";
  const char* level = "Digit5";
} const KEYS;

/** A button that is held (a dead man's button: it moves only while held). */
El holdButton(const std::string& cls, const std::string& label, std::function<void(bool)> hold) {
  El b = el("button", cls, label);
  b.type("button");
  auto held = std::make_shared<bool>(false);
  const dom::Handler up = [b, held, hold](dom::Event&) {
    if (!*held) return;
    *held = false;
    b.removeClass("on");
    hold(false);
  };
  b.onpointerdown([b, held, hold](dom::Event& e) {
    e.preventDefault();
    e.stopPropagation();
    // (the pointer may be gone already)
    b.setPointerCapture(e.pointerId);
    *held = true;
    b.addClass("on");
    hold(true);
  });
  b.onpointerup(up);
  b.onpointercancel(up);
  return b;
}

/**
 * A button that is tapped.  It answers to the finger's (or the mouse's) own
 * pointer events – down on it, up on it – not to the click the browser makes
 * of them afterwards: on a phone that click may never come while the display
 * beside it is rewritten several times a second (Safari takes changing
 * content under the finger for a hover and swallows the tap).  The click
 * stays for the keyboard (Enter or Space on the focused button).
 */
El tapButton(const std::string& cls, const std::string& label, std::function<void()> tap) {
  El b = el("button", cls, label);
  b.type("button");
  struct State {
    int down = -1;
    /** when a pointer was last let go on it (ms) – the click the browser makes of it follows */
    double upAt = -1e18;
  };
  auto st = std::make_shared<State>();
  b.onpointerdown([b, st](dom::Event& e) {
    if (e.pointerType == "mouse" && e.button != 0) return;
    e.preventDefault();
    e.stopPropagation();
    st->down = e.pointerId;
    // (the pointer may be gone already)
    b.setPointerCapture(e.pointerId);
  });
  b.onpointerup([b, st, tap](dom::Event& e) {
    if (e.pointerId != st->down) return;
    st->down = -1;
    st->upAt = plat::nowMs();
    e.stopPropagation();
    // (let go on the button: tapped – drawn off it: not)
    const dom::Rect r = b.getBoundingClientRect();
    if (e.clientX >= r.left && e.clientX <= r.right() && e.clientY >= r.top && e.clientY <= r.bottom()) tap();
  });
  b.onpointercancel([st](dom::Event&) { st->down = -1; });
  b.onclick([st, tap](dom::Event& e) {
    e.stopPropagation();
    // (a click from the keyboard has no count of presses: the pointer's taps were answered above.  The port's
    //  events carry no `detail`: a click that follows a pointer let go on the button within a second is that
    //  pointer's, any other one the keyboard's)
    if (plat::nowMs() - st->upAt > 1000) tap();
  });
  return b;
}

/** Text written only when it changed (the panel is updated every frame: no work, no mutation, when nothing changed). */
void setText(const El& e, const std::string& text) {
  if (e.textContent() != text) e.textContent(text);
}

/** shaped for fine control: a little deflection moves a little */
double shape(double v, double dead = 0.08) {
  const double a = std::abs(v);
  if (a < dead) return 0;
  return (v > 0 ? 1 : v < 0 ? -1 : 0) * std::pow((a - dead) / (1 - dead), 1.6);
}

double sign(double v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

}  // namespace

// ---------------------------------------------------------------------------------------- Stick

CranePanel::Stick::Stick(const std::string& cls, const CraneStickLabels& labels) {
  el = dom::el("div", "crane-stick " + cls);
  knob = dom::el("i");
  const El ring = dom::el("b");
  labels.up.className("lbl up");
  labels.down.className("lbl down");
  labels.left.className("lbl left");
  labels.right.className("lbl right");
  el.append(ring, labels.up, labels.down, labels.left, labels.right, knob);
  el.onpointerdown([this](dom::Event& e) {
    e.preventDefault();
    if (id >= 0) return;
    id = e.pointerId;
    el.setPointerCapture(e.pointerId);
    move(e);
  });
  el.onpointermove([this](dom::Event& e) {
    if (e.pointerId == id) move(e);
  });
  const dom::Handler up = [this](dom::Event& e) {
    if (e.pointerId != id) return;
    id = -1;
    x = 0;
    y = 0;
    knob.style("transform", "");
  };
  el.onpointerup(up);
  el.onpointercancel(up);
}

void CranePanel::Stick::move(const dom::Event& e) {
  const dom::Rect r = el.getBoundingClientRect();
  const double half = r.width / 2;
  double dx = (e.clientX - (r.left + half)) / half;
  double dy = -(e.clientY - (r.top + r.height / 2)) / half;
  const double l = std::hypot(dx, dy);
  if (l > 1) {
    dx /= l;
    dy /= l;
  }
  x = dx;
  y = dy;
  knob.style("transform", "translate(" + jsStr(dx * 90) + "%, " + jsStr(-dy * 90) + "%)");
}

void CranePanel::Stick::release() {
  id = -1;
  x = 0;
  y = 0;
  knob.style("transform", "");
}

// ---------------------------------------------------------------------------------------- CranePanel

CranePanel::CranePanel(const El& parent, std::function<std::string()> lang) : lang(std::move(lang)) {
  const El r = (root = el("div", "crane-ui"));
  r.hidden(true);
  // ---- the limiter's display
  const El lmi = (lmiBox = el("div", "crane-lmi"));
  const El barBox = el("div", "crane-bar");
  bar = el("i");
  barText = el("b");
  barBox.append(bar, el("span", "mark"), barText);
  vals = el("div", "crane-vals");
  msg = el("div", "crane-msg");
  const El levelBox = el("div", "crane-level");
  level = el("span");
  bubble = el("i");
  const El vial = el("b");
  vial.append(bubble);
  levelBox.append(vial, level);
  const El tip = el("div", "crane-tip");
  tipBar = el("i");
  tip.append(tipBar);
  lmi.append(barBox, vals, levelBox, tip, msg);
  // ---- the set-up: outrigger beams, jacks, levelling
  setup = el("div", "crane-setup");
  const auto hold = [this](const std::string& cls, std::function<void(bool)> set) {
    El b = holdButton("crane-btn " + cls, "", [this, set](bool on) {
      set(on);
      if (on) onClick();
    });
    setupButtons.push_back(b);
    setup.append(b);
    return b;
  };
  hold("beams-out", [this](bool on) { touchHold.beams = on ? 1 : 0; });
  hold("beams-in", [this](bool on) { touchHold.beams = on ? -1 : 0; });
  hold("jacks-down", [this](bool on) { touchHold.jacks = on ? 1 : 0; });
  hold("jacks-up", [this](bool on) { touchHold.jacks = on ? -1 : 0; });
  hold("level", [this](bool on) { touchHold.level = on; });
  // ---- the actions: sling / let go, creep speed, crane ↔ driving
  acts = el("div", "crane-acts");
  attachBtn = tapButton("crane-btn attach", "", [this] { onAttach(); });
  fineBtn = tapButton("crane-btn fine", "", [this] {
    fineOn = !fineOn;
    onClick();
  });
  modeBtn = tapButton("crane-btn mode", "", [this] { onToggle(); });
  acts.append(attachBtn, fineBtn, modeBtn);
  // ---- the joysticks
  const auto labels = [] { return CraneStickLabels{el("span"), el("span"), el("span"), el("span")}; };
  leftLabels = labels();
  rightLabels = labels();
  left = std::make_unique<Stick>("left", leftLabels);
  right = std::make_unique<Stick>("right", rightLabels);
  // ---- the reminder of the keys
  keysBox = el("div", "crane-keys");
  r.append(lmi, setup, acts, left->el, right->el, keysBox);
  parent.append(r);
  keydownId = dom::window().on("keydown", [this](dom::Event& e) {
    // (e.target instanceof HTMLInputElement || … HTMLSelectElement || … HTMLTextAreaElement)
    if (const El target = e.target()) {
      const std::string tag = target.get("tagName");
      if (tag == "INPUT" || tag == "SELECT" || tag == "TEXTAREA") return;
    }
    if (!active || root.hidden()) return;
    if (e.repeat) return;
    HELD().insert(e.code);
    if (e.code == KEYS.attach) {
      e.preventDefault();
      onAttach();
    }
  });
  keyupId = dom::window().on("keyup", [](dom::Event& e) { HELD().erase(e.code); });
  blurId = dom::window().on("blur", [] { HELD().clear(); });
}

CranePanel::~CranePanel() {
  const El w = dom::window();
  w.off("keydown", keydownId);
  w.off("keyup", keyupId);
  w.off("blur", blurId);
}

std::string CranePanel::t(const std::string& key, const TrParams& p) const { return tr(key, lang(), p); }

std::string CranePanel::num(double v, int digits) const {
  // (the browser's host keeps the formatter for the locale and the options)
  try {
    return plat::formatNumber(v, localeOf(lang()), {.minimumFractionDigits = digits, .maximumFractionDigits = digits, .numberingSystem = "latn"});
  } catch (const plat::IntlError&) {
    return plat::formatNumber(v, "en", {.minimumFractionDigits = digits, .maximumFractionDigits = digits});
  }
}

void CranePanel::show(const std::optional<CraneType>& type) {
  root.hidden(!type);
  if (!type) {
    active = false;
    left->release();
    right->release();
    HELD().clear();
    return;
  }
  this->type = *type;
  root.dataset("crane", *type);
}

void CranePanel::setMode(const std::string& mode) {
  if (mode == this->mode) return;
  this->mode = mode;
  root.toggleClass("touch", mode == "touch");
}

void CranePanel::label() {
  const std::string lg = lang();
  if (lg == shownLang && type == shownType) return;
  shownLang = lg;
  shownType = type;
  const auto t = [this](const std::string& k) { return this->t(k); };
  const El &bo = setupButtons[0], &bi = setupButtons[1], &jd = setupButtons[2], &ju = setupButtons[3], &lv = setupButtons[4];
  bo.textContent(t("craneBtn.beamsOut"));
  bi.textContent(t("craneBtn.beamsIn"));
  jd.textContent(t("craneBtn.jacksDown"));
  ju.textContent(t("craneBtn.jacksUp"));
  lv.textContent(t("craneBtn.level"));
  fineBtn.textContent(t("craneBtn.fine"));
  const CraneStickLabels& L = leftLabels;
  const CraneStickLabels& R = rightLabels;
  L.left.textContent("⟲");
  L.right.textContent("⟳");
  L.up.textContent(type == "loader" ? t("craneAxis.innerUp") : t("craneAxis.hoistUp"));
  L.down.textContent(type == "loader" ? t("craneAxis.innerDown") : t("craneAxis.hoistDown"));
  R.up.textContent(type == "tower" ? t("craneAxis.trolleyOut") : type == "loader" ? t("craneAxis.outerUp") : t("craneAxis.luffUp"));
  R.down.textContent(type == "tower" ? t("craneAxis.trolleyIn") : type == "loader" ? t("craneAxis.outerDown") : t("craneAxis.luffDown"));
  R.left.textContent(type == "tower" ? "" : t("craneAxis.teleIn"));
  R.right.textContent(type == "tower" ? "" : t("craneAxis.teleOut"));
  // the keys
  const auto k = [&lg](const std::string& code) { return keyName(code, lg); };
  const auto item = [](const std::string& keys, const std::string& text) {
    El e = el("span");
    const El kb = el("kbd", "", keys);
    e.append(kb, " " + text);
    return e;
  };
  const std::string rightText = type == "tower" ? t("craneKeys.trolley") : type == "loader" ? t("craneKeys.outer") : t("craneKeys.luff");
  std::vector<El> items = {
      item(k(KEYS.slewL) + " " + k(KEYS.slewR), t("craneKeys.slew")),
      item(k(KEYS.up) + " " + k(KEYS.down), type == "loader" ? t("craneKeys.inner") : t("craneKeys.hoist")),
      item("↑ ↓", rightText),
  };
  if (type != "tower") items.push_back(item("← →", type == "loader" ? t("craneKeys.ext") : t("craneKeys.tele")));
  items.push_back(item(k("Space"), t("craneKeys.attach")));
  items.push_back(item(k("ShiftLeft"), t("craneKeys.fine")));
  if (type != "tower") {
    items.push_back(item("1 2", t("craneKeys.beams")));
    items.push_back(item("3 4", t("craneKeys.jacks")));
    items.push_back(item("5", t("craneKeys.level")));
    items.push_back(item(k("KeyG"), t("craneKeys.drive")));
  }
  items.push_back(item(k("KeyV"), t("craneKeys.camera")));
  keysBox.replaceChildren();
  keysBox.append(items);
}

CraneControls CranePanel::read(double dt) {
  CraneControls out{.lx = 0, .ly = 0, .rx = 0, .ry = 0, .fine = fineOn, .beams = touchHold.beams, .jacks = touchHold.jacks, .level = touchHold.level};
  if (!active) return out;
  const auto& held = HELD();
  const auto key = [&held](const char* c) -> double { return held.count(c) ? 1 : 0; };
  const bool shift = held.count("ShiftLeft") || held.count("ShiftRight");
  const auto ramp = [dt](double cur, double want) {
    const double step = dt * (want == 0 ? 4 : 1.6);
    return std::abs(want - cur) <= step ? want : cur + sign(want - cur) * step;
  };
  auto& ka = keyAxes;
  ka.lx = ramp(ka.lx, key(KEYS.slewR) - key(KEYS.slewL));
  ka.ly = ramp(ka.ly, key(KEYS.up) - key(KEYS.down));
  ka.rx = ramp(ka.rx, key(KEYS.teleOut) - key(KEYS.teleIn));
  ka.ry = ramp(ka.ry, key(KEYS.boomUp) - key(KEYS.boomDown));
  out.lx = ka.lx + shape(left->x);
  out.ly = ka.ly + shape(left->y);
  out.rx = ka.rx + shape(right->x);
  out.ry = ka.ry + shape(right->y);
  if (shift) out.fine = true;
  if (key(KEYS.beamsOut)) out.beams = 1;
  if (key(KEYS.beamsIn)) out.beams = -1;
  if (key(KEYS.jacksDown)) out.jacks = 1;
  if (key(KEYS.jacksUp)) out.jacks = -1;
  if (key(KEYS.level)) out.level = true;
  // a gamepad: the two sticks; A slings or lets go, B changes to driving, X the creep speed, Y levels, the cross the outriggers
  const std::vector<plat::GamepadState> pads = plat::getGamepads();
  const plat::GamepadState* gp = nullptr;
  for (const plat::GamepadState& g : pads) {
    if (g.mapping == "standard") {
      gp = &g;
      break;
    }
  }
  if (gp) {
    const auto ax = [gp](size_t i) { return shape(i < gp->axes.size() ? gp->axes[i] : 0, 0.12); };
    out.lx += ax(0);
    out.ly -= ax(1);
    out.rx += ax(2);
    out.ry -= ax(3);
    std::vector<bool> b;
    for (const plat::GamepadButton& x : gp->buttons) b.push_back(x.pressed);
    const auto on = [&b](size_t i) { return i < b.size() && b[i]; };
    const auto pressed = [&](size_t i) { return on(i) && !(i < padPrev.size() && padPrev[i]); };
    if (pressed(0)) onAttach();
    if (pressed(1)) onToggle();
    if (pressed(2)) fineOn = !fineOn;
    if (on(3)) out.level = true;
    if (on(15)) out.beams = 1;
    if (on(14)) out.beams = -1;
    if (on(13)) out.jacks = 1;
    if (on(12)) out.jacks = -1;
    padPrev = b;
  }
  const auto c = [](double v) { return std::max(-1.0, std::min(1.0, v)); };
  out.lx = c(out.lx);
  out.ly = c(out.ly);
  out.rx = c(out.rx);
  out.ry = c(out.ry);
  return out;
}

bool CranePanel::fine() const {
  const auto& held = HELD();
  return fineOn || held.count("ShiftLeft") || held.count("ShiftRight");
}

void CranePanel::update(const Lmi& l, const CraneInfo& info, double now) {
  if (root.hidden()) return;
  label();
  const bool op = info.operating;
  root.toggleClass("operating", op);
  // (the buttons that change between driving and working the crane, and sling the load)
  modeBtn.hidden(info.type == "tower" || !(op ? info.canDrive : info.canOperate));
  setText(modeBtn, t(op ? "craneBtn.drive" : "craneBtn.operate"));
  attachBtn.hidden(!op);
  setText(attachBtn, t(info.hooked ? "craneBtn.release" : "craneBtn.attach"));
  attachBtn.toggleClass("ready", info.canAttach || info.hooked);
  fineBtn.hidden(!op);
  fineBtn.toggleClass("on", info.fine);
  // (the set-up's buttons while it is being set up: once it stands on its outriggers, level, its boom out, they make room)
  setup.hidden(!op || info.type == "tower" || (info.onOutriggers && !info.stowed && l.incline <= l.levelGood));
  if (now - lastDom < 0.08) return;
  lastDom = now;
  lmiBox.hidden(!op);
  if (!op) return;
  // the bar: the load against the chart
  const double p = std::max(0.0, l.ratio);
  bar.style("width", toFixed(std::min(100.0, p * 100), 1) + "%");
  lmiBox.toggleClass("warn", l.warn && !l.stop);
  lmiBox.toggleClass("stop", l.stop);
  setText(barText, l.capacity > 0 ? jsStr(jsRound(p * 100)) + " %" : "–");
  const auto n = [this](double v, int d = 1) { return num(v, d); };
  std::vector<std::string> rows = {
      t("lmi.load", {{"load", n(l.load, 2)}, {"max", l.capacity > 0 ? n(l.capacity, 2) : "–"}}),
      t("lmi.radius", {{"r", n(l.radius)}}) + (info.type == "mobile"   ? " · " + t("lmi.boom", {{"l", n(l.length)}, {"a", n(l.angle, 0)}})
                                               : info.type == "loader" ? " · " + t("lmi.boomLoader", {{"a", n(l.angle, 0)}})
                                                                       : ""),
      t("lmi.hook", {{"h", n(l.hookHeight)}}) + " · " + t("lmi.swing", {{"s", n(info.swing)}}),
      t("lmi.wind", {{"v", n(l.wind)}, {"max", n(l.windLimit, 0)}}),
  };
  if (info.type != "tower") rows.push_back(t("lmi.setting." + l.setting));
  // (the rows written into the spans that are there – new ones only when their number changes)
  if (vals.childElementCount() != static_cast<int>(rows.size())) {
    vals.replaceChildren();
    for (const std::string& text : rows) vals.append(el("span", "", text));
  } else {
    for (size_t i = 0; i < rows.size(); i++) setText(vals.child(static_cast<int>(i)), rows[i]);
  }
  if (const El w = vals.child(3)) {
    w.toggleClass("bad", l.wind > l.windLimit);
    w.toggleClass("warn", l.wind > l.windLimit * 0.8 && l.wind <= l.windLimit);
  }
  // the bubble level: the slope along the carrier and across it
  const El lv = level.parentElement();
  lv.hidden(info.type == "tower");
  if (info.type != "tower") {
    const double k = 18;
    const double bx = std::max(-1.0, std::min(1.0, info.slopeX / 2)) * k;
    const double bz = std::max(-1.0, std::min(1.0, info.slopeZ / 2)) * k;
    bubble.style("transform", "translate(" + toFixed(bx, 1) + "px, " + toFixed(bz, 1) + "px)");
    lv.toggleClass("ok", l.incline <= l.levelGood);
    lv.toggleClass("bad", l.notLevel);
    setText(level, t("lmi.incline", {{"d", n(l.incline)}}));
  }
  // how near tipping: the crane's own measure (100 %: it tips)
  const double u = std::isfinite(l.tipping) ? l.tipping : 9.99;
  tipBar.style("width", toFixed(std::min(100.0, u * 100), 1) + "%");
  const El tipBox = tipBar.parentElement();
  tipBox.toggleClass("warn", u > 0.75);
  tipBox.title(t("lmi.tipping", {{"p", jsRound(u * 100)}}));
  tipBox.dataset("label", t("lmi.tipping", {{"p", jsRound(u * 100)}}));
  // what the limiter says
  const std::string m = l.stop                   ? (l.capacity <= 0 ? t("lmi.msg.range") : t("lmi.msg.stop"))
                        : l.notLevel             ? t("lmi.msg.level")
                        : l.wind > l.windLimit   ? t("lmi.msg.wind")
                        : l.warn                 ? t("lmi.msg.warn")
                                                 : "";
  setText(msg, m);
  msg.hidden(m.empty());
}

void CranePanel::dispose() { root.remove(); }

}  // namespace fs

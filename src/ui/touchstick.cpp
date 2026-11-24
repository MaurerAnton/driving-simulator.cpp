// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/touchstick.ts @ v1.7.7
#include "ui/touchstick.h"

#include <cmath>
#include <memory>

#include "core/js.h"
#include "platform/plat.h"

namespace fs {

namespace {

const char* const STYLE = R"css(
#ui.stickmode .touch .steer, #ui.stickmode .touch .pedal, #ui.stickmode .touch .ind-l, #ui.stickmode .touch .ind-r,
#ui.stickmode .touch .gear-up, #ui.stickmode .touch .gear-dn, #ui.stickmode .touch .hb, #ui.stickmode .touch .ap { display: none !important; }
.tstick { position: absolute; z-index: 6; left: calc(max(12px, env(safe-area-inset-left)) + 68px); bottom: max(4vh, env(safe-area-inset-bottom)); width: min(36vw, 30vh, 150px); aspect-ratio: 1; border-radius: 50%;
  background: rgba(20, 28, 38, 0.45); border: 2px solid rgba(255, 255, 255, 0.5); pointer-events: auto; touch-action: none; opacity: var(--touch-opacity, 0.8); }
.tstick > i { position: absolute; left: 50%; top: 50%; width: 42%; height: 42%; margin: -21% 0 0 -21%; border-radius: 50%; background: rgba(255, 204, 0, 0.92); box-shadow: 0 2px 8px rgba(0, 0, 0, 0.4); pointer-events: none; }
.tstick-btn { position: absolute; z-index: 6; width: 52px; height: 52px; border-radius: 50%; padding: 0; background: rgba(20, 28, 38, 0.75); border: 1px solid rgba(255, 255, 255, 0.5);
  color: #fff; font-size: 20px; font-weight: 700; pointer-events: auto; touch-action: none; display: flex; align-items: center; justify-content: center; }
.tstick-btn.on { background: var(--accent, #ffcc00); color: #111; }
/* (a display rule above beats the hidden attribute: hidden buttons and the hidden stick must really go) */
.tstick-btn[hidden], .tstick[hidden] { display: none !important; }
.tstick-badge { position: absolute; z-index: 7; left: 50%; top: calc(max(10px, env(safe-area-inset-top)) + 76px); transform: translateX(-50%); padding: 6px 14px; border-radius: 16px;
  background: rgba(10, 16, 24, 0.82); color: #fff; font-weight: 700; font-size: 15px; pointer-events: none; transition: opacity 0.3s; }
)css";

bool styled = false;

}  // namespace

TouchStick::TouchStick(dom::El root_, Input& input_, const std::function<std::string(const std::string& key)>& t) : root(root_), input(input_) {
  if (!styled) {
    styled = true;
    const dom::El st = dom::el("style");
    st.textContent(STYLE);
    // (document.head)
    dom::querySelector("head").append(st);
  }
  pad = dom::el("div");
  pad.className("tstick");
  pad.hidden(true);
  knob = dom::el("i");
  pad.append(knob);
  const dom::El pad_ = pad;
  const auto move = [this, pad_](dom::Event& e) {
    const dom::Rect r = pad_.getBoundingClientRect();
    const double rad = r.width / 2;
    const double x = (e.clientX - (r.left + rad)) / rad;
    const double y = -(e.clientY - (r.top + rad)) / rad;
    const double l = std::hypot(x, y);
    const double m = jsMin(l, 1);
    const double ux = l > 1e-6 ? x / l : 0;
    const double uy = l > 1e-6 ? y / l : 0;
    // (a little play in the middle, then all the way out a little before the edge)
    const double k = m < 0.12 ? 0 : jsMin(1, (m - 0.12) / 0.78);
    input.stick.x = ux * k;
    input.stick.y = uy * k;
    input.touched();
    knob.style("transform", "translate(" + jsStr(ux * m * rad * 0.62) + "px, " + jsStr(-uy * m * rad * 0.62) + "px)");
  };
  const auto release = [this](dom::Event& e) {
    if (e.pointerId != finger) return;
    finger = -1;
    centre();
  };
  pad.on("pointerdown", [this, pad_, move](dom::Event& e) {
    e.preventDefault();
    e.stopPropagation();
    if (finger >= 0) return;
    finger = e.pointerId;
    // (the pointer may be gone already: the host lets that pass)
    pad_.setPointerCapture(e.pointerId);
    move(e);
  });
  pad.on("pointermove", [this, move](dom::Event& e) {
    if (e.pointerId == finger) move(e);
  });
  pad.on("pointerup", release);
  pad.on("pointercancel", release);
  const auto button = [](const std::string& label, const std::string& title, const std::string& left, const std::string& bottom) {
    const dom::El b = dom::el("button");
    b.type("button");
    b.className("tstick-btn");
    b.textContent(label);
    b.title(title);
    b.setAttribute("aria-label", title);
    b.style("left", left);
    b.style("bottom", bottom);
    b.hidden(true);
    return b;
  };
  // (to the right of the stick, where the pedals were)
  const std::string beside = "calc(max(12px, env(safe-area-inset-left)) + 68px + min(36vw, 30vh, 150px) + 14px)";
  const std::string low = "max(4vh, env(safe-area-inset-bottom))";
  const std::string high = "calc(max(4vh, env(safe-area-inset-bottom)) + 62px)";
  run = button("🏃", t("walk.run"), beside, low);
  run.on("pointerdown", [this](dom::Event& e) {
    e.preventDefault();
    e.stopPropagation();
    input.runToggle = !input.runToggle;
    input.touched();
  });
  up = button("▲", t("fly.up"), beside, high);
  down = button("▼", t("fly.down"), beside, low);
  const auto hold = [this](const dom::El& b, const std::string& code) {
    const std::shared_ptr<bool> held = std::make_shared<bool>(false);
    const auto up_ = [this, held, code] {
      if (*held) input.press(code, false);
      *held = false;
    };
    b.on("pointerdown", [this, b, held, code](dom::Event& e) {
      e.preventDefault();
      e.stopPropagation();
      // (the pointer may be gone already: the host lets that pass)
      b.setPointerCapture(e.pointerId);
      *held = true;
      input.press(code, true);
    });
    b.on("pointerup", up_);
    b.on("pointercancel", up_);
  };
  hold(up, "FlyUp");
  hold(down, "FlyDown");
  badgeEl = dom::el("div");
  badgeEl.className("tstick-badge");
  badgeEl.style("opacity", "0");
  root.append(pad, run, up, down, badgeEl);
}

void TouchStick::centre() {
  input.stick.x = 0;
  input.stick.y = 0;
  knob.style("transform", "");
}

void TouchStick::frame(const StickUse& use_) {
  if (use_ != use) {
    use = use_;
    pad.hidden(!use_);
    root.toggleClass("stickmode", use_.has_value());
    const bool noFly = use_ != "fly";
    down.hidden(noFly);
    up.hidden(noFly);
    run.hidden(use_ != "walk");
    if (!use_) {
      finger = -1;
      centre();
    }
  }
  if (use_ == "walk") run.toggleClass("on", input.runToggle);
  if (badgeUntil > 0 && plat::nowMs() > badgeUntil) {
    badgeUntil = 0;
    badgeEl.style("opacity", "0");
  }
}

void TouchStick::badge(const std::string& text, double ms) {
  badgeEl.textContent(text);
  badgeEl.style("opacity", "1");
  badgeUntil = plat::nowMs() + ms;
}

}  // namespace fs

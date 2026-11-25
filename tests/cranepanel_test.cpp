// SPDX-License-Identifier: AGPL-3.0-only
// LINK: ui reality geo sim game i18n core platform
/**
 * The crane's display and controls (ui/cranepanel.ts – the original has no test of its own): built in the page
 * kept in memory, driven by keys, the on-screen buttons, a joystick and a gamepad, and filled with the limiter's readings.
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

#include "platform/native/dom_native.h"
#include "platform/native/sensors_native.h"
#include "ui/cranepanel.h"

using namespace fs;
namespace nd = fs::dom::native;

static dom::Event pointer(const std::string& type, double x, double y, int id = 1) {
  dom::Event e;
  e.type = type;
  e.clientX = x;
  e.clientY = y;
  e.pointerId = id;
  e.pointerType = "touch";
  return e;
}

int main() {
  nd::reset();
  const dom::El parent = dom::body();
  CranePanel p(parent, [] { return std::string("en"); });
  int attached = 0, toggled = 0, clicked = 0;
  p.onAttach = [&] { attached++; };
  p.onToggle = [&] { toggled++; };
  p.onClick = [&] { clicked++; };
  assert(p.root.hidden());
  p.show(CraneType("mobile"));
  assert(!p.root.hidden());
  assert(p.root.dataset("crane") == "mobile");

  // not active: the keys are not the crane's
  nd::key("KeyD");
  CraneControls c = p.read(0.1);
  assert(c.lx == 0);
  nd::key("KeyD", "", false);

  // active: the keys ramp up (1.6 a second), Space slings the load
  p.active = true;
  nd::key("KeyD");
  c = p.read(0.25);
  assert(std::abs(c.lx - 0.4) < 1e-9);
  c = p.read(0.5);
  assert(std::abs(c.lx - 1.0) < 1e-9);
  nd::key("KeyD", "", false);
  c = p.read(0.1);  // let go: back at 4 a second
  assert(std::abs(c.lx - 0.6) < 1e-9);
  nd::key("Space");
  nd::key("Space", "", false);
  assert(attached == 1);
  nd::key("Digit1");
  nd::key("ShiftLeft");
  c = p.read(0.1);
  assert(c.beams == 1 && c.fine && p.fine());
  nd::key("Digit1", "", false);
  nd::key("ShiftLeft", "", false);
  c = p.read(0.1);
  assert(c.beams == 0 && !c.fine);

  // the display: the limiter's readings, the buttons as working the crane allows
  Lmi l;
  l.load = 3.2;
  l.capacity = 4;
  l.ratio = 0.8;
  l.radius = 12.5;
  l.length = 20;
  l.angle = 45;
  l.hookHeight = 6;
  l.setting = "full";
  l.wind = 5;
  l.windLimit = 9;
  l.incline = 0.2;
  l.tipping = 0.6;
  l.levelGood = 0.3;
  CraneInfo info;
  info.type = "mobile";
  info.operating = true;
  info.canAttach = true;
  info.canDrive = true;
  info.onOutriggers = true;
  p.update(l, info, 1);
  const dom::El bar = p.root.querySelector(".crane-bar i");
  assert(bar.style("width") == "80.0%");
  assert(nd::text(p.root.querySelector(".crane-bar b")) == "80 %");
  assert(p.root.querySelector(".crane-vals").childElementCount() == 5);
  assert(p.root.hasClass("operating"));
  const dom::El attachBtn = p.root.querySelector(".crane-btn.attach");
  assert(!attachBtn.hidden() && attachBtn.hasClass("ready"));
  assert(!p.root.querySelector(".crane-btn.mode").hidden());
  // standing level on its outriggers: the set-up makes room
  assert(p.root.querySelector(".crane-setup").hidden());
  assert(p.root.querySelector(".crane-msg").hidden());
  // the limiter cuts off
  l.ratio = 1.05;
  l.warn = l.stop = true;
  p.update(l, info, 1.2);
  assert(p.root.querySelector(".crane-lmi").hasClass("stop"));
  assert(!p.root.querySelector(".crane-msg").hidden());
  std::printf("%s\n", nd::text(p.root.querySelector(".crane-vals")).c_str());

  // a tap on the attach button (down and up on it), and no second one from the click that follows
  nd::setRect(attachBtn, {100, 100, 60, 40});
  dom::Event d = pointer("pointerdown", 120, 120);
  nd::dispatch(attachBtn, d);
  dom::Event u = pointer("pointerup", 125, 118);
  nd::dispatch(attachBtn, u);
  assert(attached == 2);
  attachBtn.click();
  assert(attached == 2);
  // drawn off it: no tap
  d = pointer("pointerdown", 120, 120);
  nd::dispatch(attachBtn, d);
  u = pointer("pointerup", 300, 300);
  nd::dispatch(attachBtn, u);
  assert(attached == 2);

  // a set-up button is held
  const dom::El jacks = p.root.querySelector(".crane-btn.jacks-down");
  d = pointer("pointerdown", 0, 0, 2);
  nd::dispatch(jacks, d);
  assert(p.read(0.1).jacks == 1 && clicked == 1);
  u = pointer("pointerup", 0, 0, 2);
  nd::dispatch(jacks, u);
  assert(p.read(0.1).jacks == 0);

  // the right joystick pushed fully away
  const dom::El stick = p.root.querySelector(".crane-stick.right");
  nd::setRect(stick, {0, 0, 100, 100});
  d = pointer("pointerdown", 50, -100, 3);
  nd::dispatch(stick, d);
  c = p.read(0.1);
  assert(std::abs(c.ry - 1) < 1e-9 && c.rx == 0);
  u = pointer("pointerup", 50, -100, 3);
  nd::dispatch(stick, u);
  assert(p.read(0.1).ry == 0);

  // a gamepad: B changes to driving, the left stick slews
  plat::GamepadState pad = plat::native::gamepad("pad", "standard", 4, 17);
  pad.axes[0] = 1;
  pad.buttons[1].pressed = true;
  plat::native::setGamepads({pad});
  c = p.read(0.1);
  assert(toggled == 1 && c.lx == 1);
  c = p.read(0.1);
  assert(toggled == 1);  // held: once
  plat::native::setGamepads({});

  // hidden: inactive, nothing held
  p.show(std::nullopt);
  assert(p.root.hidden() && !p.active);
  p.dispose();
  std::printf("cranepanel_test: ok\n");
  return 0;
}

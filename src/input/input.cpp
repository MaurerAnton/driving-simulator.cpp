// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Driver input: keyboard, gamepad, steering wheel and touch.
 *
 * Origin: src/input/input.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * The control maths (speed-dependent steering limits, throttle/brake ramping,
 * clutch feel, indicator self-cancel) mirrors the JavaScript originals.
 */
#include "input/input.h"

#include <algorithm>
#include <cmath>

#include "core/geom.h"

namespace fs {

double approach(double cur, double target, double maxStep) {
  return std::fabs(target - cur) <= maxStep ? target : cur + signOf(target - cur) * maxStep;
}

Input::Input(Platform& platform, const Settings& settings)
    : platform_(platform), settings_(settings) {}

void Input::fire(Action a) {
  for (const auto& f : actions_) f(a);
}

bool Input::keyDown(const std::string& code) const {
  for (const auto& k : keys_)
    if (k == code) return true;
  return false;
}

void Input::onKey(const std::string& code, bool down, bool repeat) {
  if (down) {
    if (repeat) return;
    keys_.push_back(code);
    if (code == "Escape") fire(Action::Pause);
    if (code == "F1") fire(Action::Help);
    if (!enabled) return;
    Controls& c = this->c;
    if (code == "KeyE") c.gear = std::min(gearMax_, c.gear + 1);
    else if (code == "KeyQ") c.gear = std::max(gearMin_, c.gear - 1);
    else if (code == "KeyR") c.gear = -1;
    else if (code == "KeyN" || code == "Digit0") c.gear = 0;
    else if (code == "KeyP") {
      if (gearMin_ == -2) c.gear = -2;
    } else if (code == "Space") {
      c.handbrake = c.handbrake > 0.5 ? 0 : 1;
    } else if (code == "KeyZ") {
      c.indicator = c.indicator == -1 ? 0 : -1;
      indicatorArmed_ = false;
    } else if (code == "KeyC") {
      c.indicator = c.indicator == 1 ? 0 : 1;
      indicatorArmed_ = false;
    } else if (code == "KeyX") {
      c.hazard = !c.hazard;
    } else if (code == "KeyL") {
      c.lowBeam = !c.lowBeam;
      if (!c.lowBeam) c.highBeam = false;
    } else if (code == "KeyO") {
      c.highBeam = !c.highBeam && c.lowBeam;
    } else if (code == "KeyB") {
      c.seatbelt = !c.seatbelt;
    } else if (code == "KeyI") {
      c.ignition = !c.ignition;
    } else if (code == "KeyV") {
      fire(Action::View);
    } else if (code == "KeyM") {
      fire(Action::Map);
    } else if (code == "KeyT") {
      fire(Action::Reset);
    } else if (code.size() == 6 && code.compare(0, 5, "Digit") == 0 &&
               code[5] >= '1' && code[5] <= '6' && gearMax_ == 6) {
      c.gear = code[5] - '0';
    } else if (code == "Digit1" && gearMax_ == 1) {
      c.gear = 1;
    }
  } else {
    for (size_t i = 0; i < keys_.size(); i++) {
      if (keys_[i] == code) {
        keys_.erase(keys_.begin() + static_cast<long>(i));
        return;
      }
    }
  }
}

bool Input::anyDrivingKey() const {
  for (const char* k : {"KeyW", "KeyA", "KeyS", "KeyD", "ArrowUp", "ArrowDown",
                        "ArrowLeft", "ArrowRight"})
    if (keyDown(k)) return true;
  return false;
}

void Input::applyPadButtons(const std::vector<bool>& buttons, bool wheel) {
  Controls& c = this->c;
  auto pressed = [&](size_t i) -> bool {
    return i < buttons.size() && buttons[i] && (i >= prevButtons_.size() || !prevButtons_[i]);
  };
  if (wheel) {
    const auto& sh = settings_.wheel.shifter;
    // H-shifter: a gear is "in" while its button is held, neutral otherwise
    int gear = 0;
    bool any = false;
    for (size_t i = 0; i < sh.size(); i++) {
      size_t idx = static_cast<size_t>(sh[i]);
      if (idx < buttons.size() && buttons[idx]) {
        gear = i == 6 ? -1 : static_cast<int>(i) + 1;
        any = true;
      }
    }
    bool hadShifter = false;
    for (int idx : sh)
      if (idx >= 0 && static_cast<size_t>(idx) < prevButtons_.size() && prevButtons_[static_cast<size_t>(idx)]) hadShifter = true;
    if (any || hadShifter) c.gear = gear;
    // paddles
    if (pressed(4)) c.gear = std::min(gearMax_, c.gear + 1);
    if (pressed(5)) c.gear = std::max(gearMin_, c.gear - 1);
    if (pressed(0)) c.indicator = c.indicator == -1 ? 0 : -1;
    if (pressed(2)) c.indicator = c.indicator == 1 ? 0 : 1;
    if (pressed(3)) c.hazard = !c.hazard;
    if (pressed(1)) c.handbrake = c.handbrake > 0.5 ? 0 : 1;
    if (pressed(8)) fire(Action::View);
  } else {
    if (pressed(5)) c.gear = std::min(gearMax_, c.gear + 1);  // RB
    if (pressed(4)) c.gear = std::max(gearMin_, c.gear - 1);  // LB
    if (pressed(0)) c.handbrake = c.handbrake > 0.5 ? 0 : 1;  // A
    if (pressed(2)) c.indicator = c.indicator == -1 ? 0 : -1;  // X
    if (pressed(1)) c.indicator = c.indicator == 1 ? 0 : 1;    // B
    if (pressed(3)) c.hazard = !c.hazard;                      // Y
    if (pressed(12)) {
      c.lowBeam = !c.lowBeam;
      if (!c.lowBeam) c.highBeam = false;
    }
    if (pressed(13)) c.seatbelt = !c.seatbelt;
    if (pressed(8)) fire(Action::View);
    if (pressed(16)) fire(Action::Pause);
  }
  prevButtons_ = buttons;
}

Controls Input::update(double dt, double speed, double selfAlign) {
  Controls& c = this->c;
  if (!enabled) {
    c.throttle = 0;
    c.starter = false;
    c.horn = false;
    return c;
  }
  PadState pad = platform_.readPad();
  double absSpeed = std::fabs(speed);

  if (pad.present && pad.wheel) {
    c.device = "wheel";
    c.steer = pad.steer;
    c.throttle = pad.throttle;
    c.brake = pad.brake;
    c.clutch = pad.clutch;
  } else if (pad.present && (std::fabs(pad.steer) > 0.02 || pad.throttle > 0.02 ||
                             pad.brake > 0.02 || c.device == "gamepad") &&
             !anyDrivingKey()) {
    c.device = "gamepad";
    // gentle non-linearity and less lock at speed make a thumb stick usable
    double limit = 1.0 / (1.0 + (absSpeed / 22.0) * (absSpeed / 22.0));
    double shaped = signOf(pad.steer) * std::pow(std::fabs(pad.steer), 1.6);
    c.steer = approach(c.steer, shaped * (0.3 + 0.7 * limit), dt * 3.5);
    c.throttle = pad.throttle;
    c.brake = pad.brake;
    c.clutch = pad.clutch;
  } else if (platform_.touchActive()) {
    c.device = "touch";
    double limit = 1.0 / (1.0 + (absSpeed / 20.0) * (absSpeed / 20.0));
    c.steer = approach(c.steer, platform_.touchSteer() * (0.25 + 0.75 * limit), dt * 4);
    c.throttle = platform_.touchThrottle();
    c.brake = platform_.touchBrake();
    c.clutch = 0;
  } else {
    c.device = "keyboard";
    bool left = keyDown("KeyA") || keyDown("ArrowLeft");
    bool right = keyDown("KeyD") || keyDown("ArrowRight");
    // full lock is only available at walking pace; at speed the keys ask for less
    double limit = clamp(1.0 / (1.0 + (absSpeed / 9.0) * (absSpeed / 9.0)), 0.06, 1.0);
    int target = (right ? 1 : 0) - (left ? 1 : 0);
    if (target != 0) {
      double rate = 0.55 + 0.9 * limit;
      c.steer = approach(c.steer, target * limit, dt * rate);
      // counter-steering is quicker than winding on more lock
      if (signOf(c.steer) != target) c.steer = approach(c.steer, 0, dt * 1.6);
    } else {
      // hands off: the wheel centres itself when the car rolls, and stays put when it stands
      double centring = clamp(absSpeed / 6.0, 0, 1) * (1.4 + std::fabs(selfAlign) * 2.5);
      c.steer = approach(c.steer, 0, dt * centring);
    }
    bool gas = keyDown("KeyW") || keyDown("ArrowUp");
    bool brake = keyDown("KeyS") || keyDown("ArrowDown");
    // quick to half travel, slower to the floor: easier to dose with a key
    c.throttle = gas ? approach(c.throttle, 1, dt * (c.throttle < 0.5 ? 2.4 : 0.9))
                     : approach(c.throttle, 0, dt * 5);
    c.brake = brake ? approach(c.brake, 1, dt * (c.brake < 0.45 ? 2.6 : 1.3))
                    : approach(c.brake, 0, dt * 6);
    bool clutch = keyDown("ShiftLeft") || keyDown("ShiftRight");
    // press quickly, release smoothly (like lifting the foot with feeling)
    c.clutch = clutch ? approach(c.clutch, 1, dt * 7)
                      : approach(c.clutch, 0, dt * (c.clutch > 0.3 && c.clutch < 0.78 ? 0.55 : 2.2));
  }

  if (pad.present) {
    applyPadButtons(pad.buttons, pad.wheel);
    if (std::fabs(pad.lookX) > 0.15) mouseLookYaw_ = -pad.lookX * 1.9;
    else if (!pad.wheel && std::fabs(mouseLookYaw_) > 0 && !platform_.dragging())
      mouseLookYaw_ = approach(mouseLookYaw_, 0, dt * 6);
  }

  c.starter = keyDown("Enter") || keyDown("NumpadEnter") || pad.starter;
  if (c.starter) c.ignition = true;
  c.horn = keyDown("KeyH") || pad.horn;

  // shoulder check / look around
  double yaw = mouseLookYaw_;
  bool lookL = keyDown("KeyJ");
  bool lookR = keyDown("KeyK");
  if (lookL && lookR) yaw = 2.6;
  else if (lookL) yaw = 1.75;
  else if (lookR) yaw = -1.75;
  c.lookYaw = approach(c.lookYaw, yaw, dt * 7);
  c.lookPitch = approach(c.lookPitch, platform_.mouseLookPitchDelta(), dt * 6);

  // indicator self-cancel: arms once the wheel is turned into the bend, cancels when it comes back
  if (c.indicator != 0) {
    double turned = c.steer * c.indicator;
    if (turned > 0.22) indicatorArmed_ = true;
    else if (indicatorArmed_ && turned < 0.05) {
      c.indicator = 0;
      indicatorArmed_ = false;
    }
  }
  return c;
}

}  // namespace fs

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

namespace {
/** Lateral acceleration (m/s²) the steering keys may ask for: brisk in town, gentle on the motorway. */
double keyLateral(double v) {
  return 2.4 + 3.4 / (1 + (v / 15) * (v / 15));
}
/** … and what full deflection of a stick or of the steering bar asks for. */
constexpr double ANALOG_LATERAL = 7;
constexpr double TAU = 6.283185307179586;
/** an angle brought into -π … π */
double wrap(double a) { return a - TAU * std::round(a / TAU); }
}  // namespace

Input::Input(Platform& platform, const Settings& settings)
    : platform_(platform), settings_(settings) {
  // what the driver is using right now: a touch screen starts in touch mode
  mode = platform_.coarsePointer() ? "touch" : "keys";
}

void Input::fire(Action a) {
  for (const auto& f : actions_) f(a);
}

namespace {
/** keys the platform reports as held right now */
std::vector<std::string> platformKeys(const Platform& p) {
  std::vector<std::string> out;
  for (const char* k : {"KeyA", "KeyB", "KeyC", "KeyD", "KeyE", "KeyH", "KeyI", "KeyJ", "KeyK",
                        "KeyL", "KeyM", "KeyN", "KeyO", "KeyP", "KeyQ", "KeyR", "KeyT", "KeyV",
                        "KeyW", "KeyX", "KeyY", "KeyZ", "Space", "Enter", "Escape", "F1",
                        "ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "ShiftLeft",
                        "ShiftRight", "Digit0", "Digit1", "Digit2", "Digit3", "Digit4", "Digit5",
                        "Digit6"})
    if (p.key(k)) out.push_back(k);
  return out;
}
}  // namespace

bool Input::keyDown(const std::string& code) const {
  // The browser gets every KeyboardEvent in the DOM (the original's listeners
  // on window); the platform's hook reports the keys of the frame the way
  // Emscripten's emscripten_get_key_state does, so a host that has no event
  // callbacks still feeds the state machine the same stream of key presses.
  // Only the code matters; down/up comes from the platform's answer.
  if (platform_.key(code)) return true;
  for (const auto& k : keys_)
    if (k == code) return true;
  return false;
}

void Input::onKey(const std::string& code, bool down, bool repeat, bool trusted) {
  if (down) {
    if (repeat) return;
    keys_.push_back(code);
    // a real key (the on-screen buttons send untrusted ones): the driver is on
    // the keyboard now
    if (trusted && code != "Escape") {
      mode = "keys";
      touch.active = false;
    }
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

void Input::turnLook(double dYaw, double dPitch) {
  if (orbit) {
    mouseLookYaw_ = wrap(mouseLookYaw_ + dYaw);
    // from just above the road to almost straight down onto the roof
    mouseLookPitch_ = clamp(mouseLookPitch_ + dPitch, -1.15, 0.28);
  } else {
    mouseLookYaw_ = clamp(mouseLookYaw_ + dYaw, -2.6, 2.6);
    mouseLookPitch_ = clamp(mouseLookPitch_ + dPitch, -0.8, 0.6);
  }
}

/**
 * How much of the steering lock an input may ask for at this speed so that
 * the car turns with at most `lateral` m/s² (v²/r, r = wheelbase / angle).
 * Full lock up to walking pace.
 */
double Input::lockFor(double lateral, double speed) const {
  return clamp((lateral * wheelbase) / (std::max(speed * speed, 1.0) * maxWheelAngle), 0.003, 1.0);
}

void Input::pollPointer() {
  bool down = false, up = false, mouse = false;
  double mx = 0, my = 0, dx = 0, dy = 0;
  int id = -1;
  platform_.pointer(down, up, mouse, mx, my, dx, dy, id);
  if (down) {
    if (mouse) {
      dragging_ = true;
      return;
    }
    // a finger on the view: swipe to glance left or right (shoulder check),
    // let go to look ahead again
    mode = "touch";
    if (lookId_ >= 0) return;
    lookId_ = id;
    lookX_ = 0;
    lookY_ = 0;
    dragging_ = true;
    glance_ = true;
    return;
  }
  if (up) {
    if (mouse) {
      dragging_ = false;
    } else if (id == lookId_) {
      lookId_ = -1;
      dragging_ = false;
    }
    return;
  }
  if (!dragging_) return;
  const double inv = settings_.invertLookY ? -1 : 1;
  if (mouse) {
    mouseLookYaw_ = clamp(mouseLookYaw_ - mx * 0.005, -2.6, 2.6);
    mouseLookPitch_ = clamp(mouseLookPitch_ - my * 0.004 * inv, -0.8, 0.6);
  } else if (id == lookId_) {
    // a thumb travels less than a mouse: a third of the screen is a full
    // shoulder check
    const double k = 5.4 / std::max(320.0, platform_.innerWidth());
    mouseLookYaw_ = clamp(mouseLookYaw_ + dx * k, -2.6, 2.6);
    mouseLookPitch_ = clamp(mouseLookPitch_ + dy * k * 0.5 * inv, -0.8, 0.6);
    lookX_ += dx;
    lookY_ += dy;
  }
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
  } else if (touch.active || platform_.touchActive()) {
    c.device = "touch";
    double limit = lockFor(ANALOG_LATERAL, absSpeed);
    const double steerIn = touch.active ? touch.steer : platform_.touchSteer();
    c.steer = approach(c.steer, steerIn * limit, dt * std::max(4.0 * limit, 0.05));
    c.throttle = touch.active ? touch.throttle : platform_.touchThrottle();
    c.brake = touch.active ? touch.brake : platform_.touchBrake();
    c.clutch = 0;
  } else {
    c.device = "keyboard";
    bool left = keyDown("KeyA") || keyDown("ArrowLeft");
    bool right = keyDown("KeyD") || keyDown("ArrowRight");
    // A key is all or nothing: the keys ask for the lock that turns the car
    // with a comfortable lateral acceleration at this speed – full lock at
    // walking pace, a fraction of a degree on the motorway – and wind it on
    // within about half a second.
    double limit = lockFor(keyLateral(absSpeed), absSpeed);
    int target = (right ? 1 : 0) - (left ? 1 : 0);
    if (target != 0) {
      double rate = limit * (1.2 + 1.3 * (1 - limit));
      // counter-steering is quicker than winding on more lock
      if (signOf(c.steer) == -target)
        c.steer = approach(c.steer, 0, dt * std::max(1.6 * limit, std::fabs(c.steer) * 4));
      c.steer = approach(c.steer, target * limit, dt * rate);
    } else {
      // hands off: the wheel centres itself when the car rolls, and stays put
      double centring =
          clamp(absSpeed / 6.0, 0, 1) *
          std::max((1.4 + std::fabs(selfAlign) * 2.5) * std::max(limit, 0.15),
                   std::fabs(c.steer) * 3);
      c.steer = approach(c.steer, 0, dt * centring);
    }
    bool gas = keyDown("KeyW") || keyDown("ArrowUp");
    bool brake = keyDown("KeyS") || keyDown("ArrowDown");
    // quick to half travel, slower to the floor: easier to dose with a key
    c.throttle = gas ? approach(c.throttle, 1, dt * (c.throttle < 0.35 ? 2.4 : 0.6))
                     : approach(c.throttle, 0, dt * 5);
    c.brake = brake ? approach(c.brake, 1, dt * (c.brake < 0.45 ? 2.6 : 1.3))
                    : approach(c.brake, 0, dt * 6);
    bool clutch = keyDown("ShiftLeft") || keyDown("ShiftRight");
    // press quickly, release smoothly (like lifting the foot with feeling)
    c.clutch = clutch ? approach(c.clutch, 1, dt * 7)
                      : approach(c.clutch, 0, dt * (c.clutch > 0.3 && c.clutch < 0.78 ? 0.55 : 2.2));
  }

  pollPointer();

  if (pad.present) {
    applyPadButtons(pad.buttons, pad.wheel);
    if (std::fabs(pad.lookX) > 0.15) mouseLookYaw_ = -pad.lookX * 1.9;
    else if (!pad.wheel && std::fabs(mouseLookYaw_) > 0 && !dragging_ &&
             !platform_.dragging())
      mouseLookYaw_ = approach(mouseLookYaw_, 0, dt * 6);
  }

  c.starter = keyDown("Enter") || keyDown("NumpadEnter") || pad.starter;
  if (c.starter) c.ignition = true;
  c.horn = keyDown("KeyH") || pad.horn;

  // a finger glance swings back by itself
  if (glance_ && !dragging_) {
    mouseLookYaw_ = approach(mouseLookYaw_, 0, dt * 5);
    mouseLookPitch_ = approach(mouseLookPitch_, 0, dt * 2.5);
    if (mouseLookYaw_ == 0 && mouseLookPitch_ == 0) glance_ = false;
  }
  // shoulder check / look around
  double yaw = mouseLookYaw_;
  bool lookL = keyDown("KeyJ");
  bool lookR = keyDown("KeyK");
  const bool back = keyDown("KeyU") || (lookL && lookR);
  // looking back: over the left shoulder through the rear window – or, seen
  // from outside, the camera in front of the car
  if (back)
    yaw = orbit ? M_PI : 2.6;
  else if (lookL)
    yaw = 1.75;
  else if (lookR)
    yaw = -1.75;
  if (orbit) {
    // once the car is driving and nobody is looking around, the camera swings
    // back behind it
    if (dragging_ || back || lookL || lookR)
      lookIdle = 0;
    else
      lookIdle += dt;
    if (lookIdle > 2.2 && absSpeed > 2.5 && !glance_) {
      mouseLookYaw_ = approach(mouseLookYaw_, 0, dt * 1.6);
      mouseLookPitch_ = approach(mouseLookPitch_, 0, dt * 0.6);
    }
    // the short way round
    c.lookYaw = wrap(c.lookYaw + clamp(wrap(yaw - c.lookYaw), -dt * 7, dt * 7));
  } else {
    c.lookYaw = approach(c.lookYaw, yaw, dt * 7);
  }
  c.lookPitch = approach(c.lookPitch, mouseLookPitch_, dt * 6);

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

// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/input/input.ts @ v1.7.7
#include "input/input.h"

#include <cmath>
#include <numbers>

#include "core/js.h"
#include "game/settings.h"
#include "platform/plat.h"
#include "platform/sensors.h"

namespace fs {

namespace {

/** scrolled on and on, the camera behind the vehicle goes this many times further back than usual: a view from high above */
const double ZOOM_FAR = 5;
/** the keys that fly the flying camera (they do not drive meanwhile) */
const std::unordered_set<std::string> FLY_KEYS = {"KeyW", "KeyA", "KeyS", "KeyD", "KeyQ", "KeyE", "ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "PageUp", "PageDown"};

const double TAU = std::numbers::pi * 2;
/** O held longer than this (ms) is the flasher, not a switch of the main beam */
const double FLASH_HOLD_MS = 350;
/** an angle brought into -π … π */
double wrap(double a) { return a - TAU * jsRound(a / TAU); }

double clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
double approach(double cur, double target, double maxStep) { return std::abs(target - cur) <= maxStep ? target : cur + jsSign(target - cur) * maxStep; }

/** `buttons[i]` of an array that may be shorter (undefined: not pressed) */
bool at(const std::vector<bool>& buttons, int i) { return i >= 0 && static_cast<size_t>(i) < buttons.size() && buttons[static_cast<size_t>(i)]; }

/** The zoom keys: + (1), − (−1), anything else 0 – by the character, so that + on a German keyboard counts too. */
int zoomKey(const dom::Event& e) {
  if (e.ctrlKey || e.metaKey || e.altKey) return 0;
  if (e.key == "+" || e.key == "=" || e.code == "NumpadAdd") return 1;
  if (e.key == "-" || e.code == "NumpadSubtract") return -1;
  return 0;
}

/** /wheel|racing|g2[579]|g920|g923|t150|t300|t248|tmx|driving force|fanatec|simucube|moza/i */
bool wheelName(const std::string& id) {
  const std::string s = toLowerAscii(id);
  for (const char* part : {"wheel", "racing", "g25", "g27", "g29", "g920", "g923", "t150", "t300", "t248", "tmx", "driving force", "fanatec", "simucube", "moza"})
    if (s.find(part) != std::string::npos) return true;
  return false;
}

}  // namespace

Input::Input(dom::El canvas, const Settings& settings_) : mode(plat::matchMedia("(pointer: coarse)") ? "touch" : "keys"), settings(settings_) {
  dom::window().on("keydown", [this](dom::Event& e) { onKey(e, true, plat::isTrusted(e)); });
  dom::window().on("keyup", [this](dom::Event& e) { onKey(e, false, plat::isTrusted(e)); });
  dom::window().on("blur", [this] { keys.clear(); });
  canvas.on("pointerdown", [this, canvas](dom::Event& e) {
    if (e.pointerType == "mouse") {
      if (e.button == 0) dragging = true;
      // (a drag goes on when the pointer passes over a part of the HUD)
      // (the pointer may be gone already: the host lets that pass)
      canvas.setPointerCapture(e.pointerId);
      return;
    }
    // A finger on the view.  From the driver's seat: swipe to glance left or right (shoulder
    // check), let go to look ahead again.  From outside: it turns the camera, which stays.
    // Two fingers move the free camera, and pinching brings a camera closer or takes it away.
    mode = "touch";
    if (Finger* f = finger(e.pointerId)) *f = Finger{e.clientX, e.clientY};
    else fingers.emplace_back(e.pointerId, Finger{e.clientX, e.clientY});
    if (fingers.size() == 2) pinch = fingerSpread();
    if (lookId >= 0) return;
    lookId = e.pointerId;
    lookX = e.clientX;
    lookY = e.clientY;
    dragging = true;
    glance = !orbit && !freeCam;
  });
  const auto release = [this](dom::Event& e) {
    if (e.pointerType == "mouse") {
      dragging = false;
      return;
    }
    for (size_t i = 0; i < fingers.size(); i++)
      if (fingers[i].first == e.pointerId) {
        fingers.erase(fingers.begin() + static_cast<long>(i));
        break;
      }
    if (fingers.size() < 2) zoom.fingersOff();
    if (e.pointerId == lookId) {
      // (another finger still on the view goes on turning it)
      if (fingers.empty()) {
        lookId = -1;
        dragging = false;
      } else {
        lookId = fingers[0].first;
        lookX = fingers[0].second.x;
        lookY = fingers[0].second.y;
      }
    }
  };
  dom::window().on("pointerup", release);
  dom::window().on("pointercancel", release);
  canvas.on("pointermove", [this](dom::Event& e) {
    const double inv = settings.invertLookY ? -1 : 1;
    if (e.pointerType == "mouse") {
      // the left button held – or Alt, with no button at all – turns the view;
      // the right or the middle button moves the free camera
      const bool turn = dragging || (e.altKey && e.buttons == 0);
      if (freeCam && (e.buttons & 6) != 0) {
        const double k = 1 / jsMax(240, plat::innerHeight());
        cam.right -= e.movementX * k;
        cam.fwd += e.movementY * k;
      } else if (turn) {
        lookIdle = 0;
        turnLook(-e.movementX * 0.005, -e.movementY * 0.004 * inv);
      }
      return;
    }
    Finger* f = finger(e.pointerId);
    if (!f) return;
    const double dx = e.clientX - f->x;
    const double dy = e.clientY - f->y;
    f->x = e.clientX;
    f->y = e.clientY;
    lookIdle = 0;
    if (fingers.size() >= 2) {
      const double spread = fingerSpread();
      const double scale = pinch > 8 && spread > 8 ? spread / pinch : 1;
      pinch = spread;
      if (viewZoom) zoom.pinch(scale);
      else if (flyCam) {
        // (the flying camera: both fingers up or down lift or lower it – each moves the middle by half –, a pinch sets the speed)
        cam.lift -= (dy * 0.5) / jsMax(240, plat::innerHeight());
        cam.speed += std::log(scale) * 4;
      } else if (freeCam) {
        // (each finger moves the middle between the two by half of what it moves)
        const double k = 0.5 / jsMax(240, plat::innerHeight());
        cam.right -= dx * k;
        cam.fwd += dy * k;
        cam.dolly += std::log(scale);
      } else if (orbit) c.lookZoom = clamp(c.lookZoom / scale, 3.5, zoomMax * ZOOM_FAR);
    } else if (e.pointerId == lookId) {
      // a thumb travels less than a mouse: a third of the screen is a full shoulder check – and it turns
      // the view the way the mouse does (to the right when drawn to the right, down when drawn down)
      const double k = 5.4 / jsMax(320, plat::innerWidth());
      turnLook(-dx * k, -dy * k * 0.5 * inv);
    }
  });
  // the wheel of the mouse moves the camera closer to the car or further away
  dom::ListenOptions active;
  active.passive = false;
  canvas.on(
      "wheel",
      [this](dom::Event& e) {
        // (the flying camera: the wheel sets how fast it flies – a notch a step)
        if (flyCam && enabled) {
          e.preventDefault();
          cam.speed -= clamp(e.deltaY, -120, 120) / 100;
          return;
        }
        // (on foot and in the driver's seat the wheel zooms the view)
        if (viewZoom && enabled) {
          e.preventDefault();
          zoom.wheel(clamp(e.deltaY, -120, 120) * 0.0016);
          return;
        }
        if ((!orbit && !freeCam) || !enabled) return;
        e.preventDefault();
        const double step = clamp(e.deltaY, -120, 120) * 0.0016;
        if (freeCam) cam.dolly -= step;
        else c.lookZoom = clamp(c.lookZoom * std::exp(step), 3.5, zoomMax * ZOOM_FAR);
      },
      active);
  // (the right button moves the free camera: no menu of the browser over the view)
  canvas.on("contextmenu", [](dom::Event& e) { e.preventDefault(); });
  canvas.on("dblclick", [this] {
    mouseLookYaw = orbit ? orbitRest : 0;
    mouseLookPitch = 0;
    cam.reset = true;
  });
}

Input::Finger* Input::finger(int pointerId) {
  for (auto& f : fingers)
    if (f.first == pointerId) return &f.second;
  return nullptr;
}

double Input::fingerSpread() const {
  if (fingers.size() < 2) return 0;
  const Finger& a = fingers[0].second;
  const Finger& b = fingers[1].second;
  return std::hypot(a.x - b.x, a.y - b.y);
}

Input::Walk Input::walkInput(const std::string& mode_) {
  // (classic, and tap to walk: A D a step to the side, the arrows ← → turn – game/walkmodes.ts)
  const bool classic = mode_ != "tank";
  double fwd = (key({"KeyW", "ArrowUp"}) ? 1 : 0) - (key({"KeyS", "ArrowDown"}) ? 1 : 0);
  double turn = classic ? (key({"ArrowLeft"}) ? 1 : 0) - (key({"ArrowRight"}) ? 1 : 0) : (key({"KeyA", "ArrowLeft"}) ? 1 : 0) - (key({"KeyD", "ArrowRight"}) ? 1 : 0);
  double side = classic ? (key({"KeyD"}) ? 1 : 0) - (key({"KeyA"}) ? 1 : 0) : 0;
  bool run = key({"ShiftLeft", "ShiftRight"}) || runToggle;
  if (key({"AltLeft", "AltRight"})) fwd = turn = side = 0;
  // the stick on the screen (shown for the classic way and tap to walk)
  fwd += stick.y;
  side += stick.x;
  if (touch.active) {
    fwd += touch.throttle - touch.brake;
    turn -= touch.steer;
    // (the pedal pressed near its top: running)
    run = run || touch.throttle > 0.85;
  }
  const std::optional<Pad> pad = readPad();
  if (pad) {
    fwd += pad->throttle - pad->brake;
    if (classic && pad->kind == "standard") {
      // (the left stick walks and steps aside; the right one turns)
      if (std::abs(pad->pitch) > 0.12) fwd -= pad->pitch;
      if (std::abs(pad->steer) > 0.12) side += pad->steer;
      if (std::abs(pad->lookX) > 0.15) turn -= pad->lookX;
    } else if (std::abs(pad->steer) > 0.12) turn -= pad->steer;
    run = run || pad->throttle > 0.95;
  }
  return Walk{clamp(fwd, -1, 1), clamp(turn, -1, 1), clamp(side, -1, 1), run};
}

Input::Fork Input::forkInput() {
  if (!enabled) return Fork{0, 0, 0};
  double lift = (key({"PageUp"}) ? 1 : 0) - (key({"PageDown"}) ? 1 : 0);
  double tilt = (key({"Home"}) ? 1 : 0) - (key({"End"}) ? 1 : 0);
  const double shift_ = (key({"Delete"}) ? 1 : 0) - (key({"Insert"}) ? 1 : 0);
  const std::optional<Pad> pad = readPad();
  if (pad && pad->kind == "standard") {
    const std::vector<plat::GamepadState> pads = plat::getGamepads();
    const double ry = !pads.empty() && pads[0].axes.size() > 3 ? pads[0].axes[3] : 0;
    if (std::abs(ry) > 0.15) lift -= ry;
    tilt += (at(pad->buttons, 15) ? 1 : 0) - (at(pad->buttons, 14) ? 1 : 0);
  }
  return Fork{clamp(lift, -1, 1), clamp(tilt, -1, 1), clamp(shift_, -1, 1)};
}

Input::Flight Input::flightInput() {
  double pitch = (key({"ArrowDown"}) ? 1 : 0) - (key({"ArrowUp"}) ? 1 : 0);
  double roll = (key({"ArrowRight"}) ? 1 : 0) - (key({"ArrowLeft"}) ? 1 : 0);
  const double yaw = (key({"KeyD"}) ? 1 : 0) - (key({"KeyA"}) ? 1 : 0);
  double throttle = (key({"KeyW"}) ? 1 : 0) - (key({"KeyS"}) ? 1 : 0);
  double brake = key({"Space"}) ? 1 : 0;
  if (touch.active) {
    roll += touch.steer;
    pitch += touch.pitch;
    throttle += (touch.throttle > 0.2 ? 1 : 0) - (touch.brake > 0.2 ? 1 : 0);
    brake = jsMax(brake, touch.brake > 0.2 ? touch.brake : 0);
  }
  const std::optional<Pad> pad = readPad();
  if (pad && pad->kind == "standard") {
    roll += pad->steer;
    pitch += pad->pitch;
    throttle += pad->throttle - pad->brake;
    brake = jsMax(brake, pad->brake);
  }
  return Flight{clamp(pitch, -1, 1), clamp(roll, -1, 1), clamp(yaw, -1, 1), clamp(throttle, -1, 1), clamp(brake, 0, 1)};
}

const Input::Cam& Input::takeCam() {
  Cam& c_ = cam;
  Cam& o = camOut;
  o.yaw = c_.yaw;
  o.pitch = c_.pitch;
  o.right = c_.right;
  o.fwd = c_.fwd;
  o.dolly = c_.dolly;
  o.keyRight = c_.keyRight;
  o.keyFwd = c_.keyFwd;
  o.keyUp = c_.keyUp;
  o.fast = c_.fast;
  o.reset = c_.reset;
  o.lift = c_.lift;
  o.speed = c_.speed;
  c_.yaw = c_.pitch = c_.right = c_.fwd = c_.dolly = c_.keyRight = c_.keyFwd = c_.keyUp = c_.lift = c_.speed = 0;
  c_.reset = false;
  return o;
}

double Input::glanceYaw() const {
  const bool l = key({"KeyJ"});
  const bool r = key({"KeyK"});
  if (key({"KeyU"}) || (l && r)) return std::numbers::pi;
  return l ? 1.6 : r ? -1.6 : 0;
}

void Input::turnLook(double dYaw, double dPitch) {
  if (freeCam) {
    cam.yaw += dYaw;
    cam.pitch += dPitch;
  } else if (orbit) {
    mouseLookYaw = wrap(mouseLookYaw + dYaw);
    // from just above the road to almost straight down onto the roof
    mouseLookPitch = clamp(mouseLookPitch + dPitch, -1.15, 0.28);
  } else {
    mouseLookYaw = clamp(mouseLookYaw + dYaw, -2.6, 2.6);
    mouseLookPitch = clamp(mouseLookPitch + dPitch, -0.8, 0.6);
  }
}

void Input::resetLook(bool orbit_, bool freeCam_, double rest) {
  orbit = orbit_;
  freeCam = freeCam_;
  orbitRest = orbit_ ? rest : 0;
  takeCam();
  mouseLookYaw = orbitRest;
  mouseLookPitch = 0;
  c.lookYaw = orbitRest;
  c.lookPitch = 0;
  glance = false;
}

void Input::setSteering(double wheelbase, double maxWheelAngle, const SteerSpec* spec) {
  if (spec) {
    SteerSpec s = *spec;
    s.wheelbase = wheelbase;
    s.maxWheelAngle = maxWheelAngle;
    feel = steerFeel(&s, wheelbase, maxWheelAngle);
  } else feel = steerFeel(nullptr, wheelbase, maxWheelAngle);
}

void Input::onAction(std::function<void(const Action& a)> fn) { actions.push_back(std::move(fn)); }

void Input::touched() {
  mode = "touch";
  touch.active = true;
}

void Input::untouched() {
  touch.active = false;
  touch.steer = 0;
}

void Input::press(const std::string& code, bool down) {
  mode = "touch";
  // (`new KeyboardEvent(down ? 'keydown' : 'keyup', { code })`: an event of the game's own making, not trusted)
  dom::Event e;
  e.type = down ? "keydown" : "keyup";
  e.code = code;
  onKey(e, down, false);
}

void Input::fire(const Action& a) {
  // (by index: a listener may add another)
  for (size_t i = 0; i < actions.size(); i++) {
    const std::function<void(const Action& a)> f = actions[i];
    f(a);
  }
}

int Input::shift(int g, int dir) const {
  if (gearMin != -2) return dir > 0 ? std::min(gearMax, g + 1) : std::max(gearMin, g - 1);
  if (dir > 0) return g == -1 ? 0 : 1;
  return g == 1 ? 0 : g == 0 ? -1 : -2;
}

void Input::setAutomatic(bool automatic) {
  gearMin = automatic ? -2 : -1;
  gearMax = automatic ? 1 : 6;
  c.gear = automatic ? -2 : 0;
}

void Input::onKey(dom::Event& e, bool down, bool trusted) {
  // (e.target instanceof HTMLInputElement || … HTMLSelectElement || … HTMLTextAreaElement)
  if (const dom::El target = e.target()) {
    const std::string tag = target.get("tagName");
    if (tag == "INPUT" || tag == "SELECT" || tag == "TEXTAREA") return;
  }
  const std::string& code = e.code;
  if (down) {
    if (e.repeat) {
      if (startsWith(code, "Arrow") || code == "Space") e.preventDefault();
      return;
    }
    // a real key (the on-screen buttons send untrusted ones): the driver is on the keyboard now
    if (trusted && !e.ctrlKey && !e.metaKey && !e.altKey && code != "Escape") {
      mode = "keys";
      touch.active = false;
    }
    keys.insert(code);
    // (Alt alone must not put the browser's menu in charge of the keyboard)
    if (code == "AltLeft" || code == "AltRight") e.preventDefault();
    if (code == "Escape") fire("pause");
    if (code == "F1") {
      e.preventDefault();
      fire("help");
    }
    // F2: hide or show the whole display (ui/viewbuttons.ts)
    if (code == "F2") {
      e.preventDefault();
      fire("hud");
    }
    // F3: turn the vehicle round (game.ts turnAround – into the lane of the other direction)
    if (code == "F3") {
      e.preventDefault();
      fire("turn");
    }
    // "Foto": what does this place really look like? (also from the pause menu)
    if (code == "KeyF" && !e.ctrlKey && !e.metaKey && !e.altKey) fire("reality");
    if (!enabled) return;
    // (on foot: Caps Lock switches running on or off)
    if (code == "CapsLock") runToggle = !runToggle;
    // Alt held with the free camera – or the flying camera: the keys fly the camera, they do not drive
    if ((freeCam && e.altKey) || (flyCam && FLY_KEYS.count(code))) {
      e.preventDefault();
      return;
    }
    // (working a crane: its own keys – but the camera, the map, the door, and the lights, hazard lights, horn and ignition still work)
    if (crane && !(code == "KeyV" || code == "KeyM" || code == "KeyG" || code == "KeyL" || code == "KeyO" || code == "KeyX" || code == "KeyH" || code == "KeyI")) {
      if (startsWith(code, "Arrow") || code == "Space") e.preventDefault();
      return;
    }
    // + and −: the view closer or wider (on foot, in the driver's seat; held, the zoom stays)
    const int zoomDir = zoomKey(e);
    if (zoomDir != 0 && viewZoom) {
      zoom.press(zoomDir, true);
      return;
    }
    if (code == "KeyE") c.gear = shift(c.gear, 1);
    else if (code == "KeyQ") c.gear = shift(c.gear, -1);
    else if (code == "KeyR") c.gear = -1;
    else if (code == "KeyN" || code == "Digit0") c.gear = 0;
    else if (code == "KeyP") {
      if (gearMin == -2) c.gear = -2;
      // (in the tram: the pantograph up or down)
      fire("panto");
    } else if (code == "Space") {
      c.handbrake = c.handbrake > 0.5 ? 0 : 1;
      e.preventDefault();
    } else if (code == "KeyZ") {
      c.indicator = c.indicator == -1 ? 0 : -1;
      indicatorArmed = false;
    } else if (code == "KeyC") {
      c.indicator = c.indicator == 1 ? 0 : 1;
      indicatorArmed = false;
    } else if (code == "KeyX") c.hazard = !c.hazard;
    else if (code == "KeyL") {
      c.lowBeam = !c.lowBeam;
      if (!c.lowBeam) c.highBeam = false;
    } else if (code == "KeyO") {
      // (a tap switches the main beam when it is let go; held, it is the flasher – see `flasher`)
      oDownAt = plat::nowMs();
    } else if (code == "KeyB") c.seatbelt = !c.seatbelt;
    else if (code == "KeyI") c.ignition = !c.ignition;
    else if (code == "KeyV") fire("view");
    else if (code == "KeyG") fire("door");
    else if (code == "Period") {
      // (on foot: open or shut the door, the bonnet, the boot where one stands – game/openplay.ts)
      fire("open");
    } else if (code == "KeyM") fire("map");
    else if (code == "KeyY") fire("mirror");
    else if (code == "KeyT") fire("reset");
    else if (code == "Digit9") fire("service");
    else if (code == "Digit8") {
      // the wiper stalk one place on (Digit7 held: the washer, see `washer`)
      fire("wiper");
    } else {
      // (/^Digit[1-6]$/)
      if (code.size() == 6 && startsWith(code, "Digit") && code[5] >= '1' && code[5] <= '6' && gearMax == 6) c.gear = code[5] - '0';
      else if (code == "Digit1" && gearMax == 1) c.gear = 1;
    }
    if (startsWith(code, "Arrow")) e.preventDefault();
  } else {
    if (zoomKey(e) != 0) zoom.press(0, false);
    // the main beam on or off: O tapped, not held as the flasher
    if (code == "KeyO" && keys.count("KeyO") && enabled && plat::nowMs() - oDownAt < FLASH_HOLD_MS) c.highBeam = !c.highBeam && c.lowBeam;
    keys.erase(code);
    if (code == "AltLeft" || code == "AltRight") e.preventDefault();
  }
}

bool Input::flasher() const { return enabled && (keys.count("Flash") || keys.count("KeyO")); }

bool Input::washer() const { return enabled && key({"Digit7"}); }

bool Input::key(std::initializer_list<const char*> codes) const {
  for (const char* k : codes)
    if (keys.count(k)) return true;
  return false;
}

Controls& Input::update(double dt, double speed, double selfAlign) {
  if (!enabled) {
    c.throttle = 0;
    c.starter = false;
    c.horn = false;
    return c;
  }
  const std::optional<Pad> pad = readPad();
  const double absSpeed = std::abs(speed);
  // the free camera, flown with the keys while Alt is held (the car rolls on by itself meanwhile)
  const bool fly = (freeCam && key({"AltLeft", "AltRight"})) || flyCam;
  if (fly) {
    cam.keyFwd += ((key({"KeyW", "ArrowUp"}) ? 1 : 0) - (key({"KeyS", "ArrowDown"}) ? 1 : 0)) * dt;
    cam.keyRight += ((key({"KeyD", "ArrowRight"}) ? 1 : 0) - (key({"KeyA", "ArrowLeft"}) ? 1 : 0)) * dt;
    cam.keyUp += ((key({"KeyE", "PageUp", "FlyUp"}) ? 1 : 0) - (key({"KeyQ", "PageDown", "FlyDown"}) ? 1 : 0)) * dt;
    cam.fast = key({"ShiftLeft", "ShiftRight"});
    // (the stick on the screen, and a gamepad's left stick)
    cam.keyFwd += stick.y * dt;
    cam.keyRight += stick.x * dt;
    if (flyCam && pad && pad->kind == "standard") {
      cam.keyFwd -= pad->pitch * dt;
      cam.keyRight += pad->steer * dt;
      cam.keyUp += (pad->throttle - pad->brake) * dt;
    }
  }

  if (flyCam) {
    // the camera flies: nobody drives – the vehicle is braked to a stop and waits (the clutch down: the engine does not stall)
    c.throttle = 0;
    c.brake = approach(c.brake, 1, dt * 2);
    c.clutch = approach(c.clutch, 1, dt * 7);
  } else if (pad && pad->kind == "wheel") {
    c.device = "wheel";
    c.steer = pad->steer;
    c.throttle = pad->throttle;
    c.brake = pad->brake;
    c.clutch = pad->clutch;
  } else if (pad && (std::abs(pad->steer) > 0.02 || pad->throttle > 0.02 || pad->brake > 0.02 || c.device == "gamepad") && (fly || !anyDrivingKey())) {
    c.device = "gamepad";
    // gentle non-linearity and less lock at speed make a thumb stick usable
    const double shaped = jsSign(pad->steer) * std::pow(std::abs(pad->steer), 1.6);
    c.steer = analogSteer(feel, c.steer, shaped, absSpeed, dt, 3.5);
    c.throttle = pad->throttle;
    c.brake = pad->brake;
    c.clutch = pad->clutch;
  } else if (touch.active) {
    c.device = "touch";
    c.steer = analogSteer(feel, c.steer, touch.steer, absSpeed, dt, 4);
    c.throttle = touch.throttle;
    c.brake = touch.brake;
    c.clutch = touch.clutch;
  } else {
    c.device = "keyboard";
    const bool left = !fly && key({"KeyA", "ArrowLeft"});
    const bool right = !fly && key({"KeyD", "ArrowRight"});
    // A key is all or nothing, a steering wheel is not: the keys ask for the lock that turns the car
    // with a comfortable lateral acceleration at this speed – full lock at walking pace, a fraction
    // of a degree on the motorway – and wind it on within about half a second.
    // (hands off: the wheel centres itself when the car rolls, and stays put when it stands)
    // the sensitivity is read here so a slider change takes effect at once
    feel.sens = settings.steerSensitivity;
    c.steer = keySteer(feel, c.steer, (right ? 1 : 0) - (left ? 1 : 0), absSpeed, selfAlign, dt);
    const bool gas = !fly && key({"KeyW", "ArrowUp"});
    const bool brake = !fly && key({"KeyS", "ArrowDown"});
    // quick to half travel, slower to the floor: easier to dose with a key
    const double gasTarget = gas ? 1 : 0;
    c.throttle = gas ? approach(c.throttle, gasTarget, dt * (c.throttle < 0.35 ? 2.4 : 0.6)) : approach(c.throttle, 0, dt * 5);
    c.brake = brake ? approach(c.brake, 1, dt * (c.brake < 0.45 ? 2.6 : 1.3)) : approach(c.brake, 0, dt * 6);
    const bool clutch = !fly && key({"ShiftLeft", "ShiftRight"});
    // press quickly, release smoothly (like lifting the foot with feeling)
    c.clutch = clutch ? approach(c.clutch, 1, dt * 7) : approach(c.clutch, 0, dt * (c.clutch > 0.3 && c.clutch < 0.78 ? 0.55 : 2.2));
  }

  if (pad && crane) {
    // (the sticks and buttons work the crane: only the camera and the pause stay the game's)
    const std::vector<bool>& b = pad->buttons;
    if (at(b, 8) && !at(prevButtons, 8)) fire("view");
    if (at(b, 16) && !at(prevButtons, 16)) fire("pause");
    prevButtons = b;
  } else if (pad) {
    applyPadButtons(pad->buttons, pad->kind == "wheel");
    if (std::abs(pad->lookX) > 0.15) mouseLookYaw = -pad->lookX * 1.9;
    else if (pad->kind == "standard" && mouseLookYaw != orbitRest && !dragging) mouseLookYaw = orbitRest + approach(wrap(mouseLookYaw - orbitRest), 0, dt * 6);
  }

  c.starter = key({"Enter", "NumpadEnter"}) || (pad ? pad->starter : false);
  if (c.starter) c.ignition = true;
  c.horn = key({"KeyH"}) || (pad ? pad->horn : false);

  // a finger glance swings back by itself
  if (freeCam) glance = false;
  if (glance && !dragging) {
    mouseLookYaw = approach(mouseLookYaw, 0, dt * 5);
    mouseLookPitch = approach(mouseLookPitch, 0, dt * 2.5);
    if (mouseLookYaw == 0 && mouseLookPitch == 0) glance = false;
  }
  // shoulder check / look around
  double yaw = mouseLookYaw;
  const bool lookL = key({"KeyJ"});
  const bool lookR = key({"KeyK"});
  const bool back = key({"KeyU"}) || (lookL && lookR);
  // looking back: over the left shoulder through the rear window – or, seen from outside, the camera in front of the car
  if (back) yaw = orbit ? std::numbers::pi : 2.6;
  else if (lookL) yaw = 1.75;
  else if (lookR) yaw = -1.75;
  if (orbit) {
    // once the car is driving and nobody is looking around, the camera swings back behind it
    if (dragging || back || lookL || lookR) lookIdle = 0;
    else lookIdle += dt;
    if (lookIdle > 2.2 && absSpeed > 2.5 && !glance) {
      mouseLookYaw = orbitRest + approach(wrap(mouseLookYaw - orbitRest), 0, dt * 1.6);
      mouseLookPitch = approach(mouseLookPitch, 0, dt * 0.6);
    }
    // the short way round
    c.lookYaw = wrap(c.lookYaw + clamp(wrap(yaw - c.lookYaw), -dt * 7, dt * 7));
  } else c.lookYaw = approach(c.lookYaw, yaw, dt * 7);
  c.lookPitch = approach(c.lookPitch, mouseLookPitch, dt * 6);

  // indicator self-cancel: arms once the wheel is turned into the bend, cancels when it comes back
  if (c.indicator != 0) {
    const double turned = c.steer * c.indicator;
    if (turned > 0.22) indicatorArmed = true;
    else if (indicatorArmed && turned < 0.05) {
      c.indicator = 0;
      indicatorArmed = false;
    }
  }
  return c;
}

bool Input::anyDrivingKey() const { return key({"KeyW", "KeyA", "KeyS", "KeyD", "ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight"}); }

std::optional<Input::Pad> Input::readPad() {
  // (plat::getGamepads lists the pads that are connected – none where the browser has no gamepads at all: the first is the one)
  const std::vector<plat::GamepadState> pads = plat::getGamepads();
  if (pads.empty()) return std::nullopt;
  const plat::GamepadState& gp = pads[0];
  lastGamepadId = gp.id;
  std::vector<bool> buttons;
  buttons.reserve(gp.buttons.size());
  for (const plat::GamepadButton& b : gp.buttons) buttons.push_back(b.pressed);
  const auto dead = [](double v, double d) { return std::abs(v) < d ? 0 : (v - jsSign(v) * d) / (1 - d); };
  const bool isWheel = gp.mapping != "standard" && wheelName(gp.id);
  if (isWheel) {
    const WheelMapping& m = settings.wheel;
    const auto axis = [&gp](int i) { return i >= 0 && static_cast<size_t>(i) < gp.axes.size() ? gp.axes[static_cast<size_t>(i)] : 0.0; };
    const auto pedal = [&](int i) {
      if (i < 0 || static_cast<size_t>(i) >= gp.axes.size()) return 0.0;
      const double v = axis(i);
      return clamp(m.pedalsInverted ? (1 - v) / 2 : (v + 1) / 2, 0, 1);
    };
    // the wheel reports its full range as -1…1; map it onto the car's 2.66 turns
    const double carRange = 957;
    const double steer = clamp((axis(m.steerAxis) * m.wheelRange) / carRange, -1, 1);
    Pad p;
    p.kind = "wheel";
    p.steer = steer;
    p.pitch = 0;
    p.throttle = dead(pedal(m.throttleAxis), 0.03);
    p.brake = dead(pedal(m.brakeAxis), 0.03);
    p.clutch = dead(pedal(m.clutchAxis), 0.05);
    p.lookX = 0;
    p.starter = at(buttons, 9);
    p.horn = at(buttons, 10);
    p.buttons = std::move(buttons);
    return p;
  }
  const auto b = [&gp](size_t i) { return i < gp.buttons.size() ? gp.buttons[i].value : 0.0; };
  const auto ax = [&gp](size_t i) { return i < gp.axes.size() ? gp.axes[i] : 0.0; };
  Pad p;
  p.kind = "standard";
  p.steer = dead(ax(0), 0.1);
  // (the left stick's height: back is pulled)
  p.pitch = dead(ax(1), 0.1);
  p.throttle = dead(b(7), 0.04);
  p.brake = dead(b(6), 0.04);
  p.clutch = 0;
  p.lookX = ax(2);
  p.starter = at(buttons, 9);
  p.horn = at(buttons, 10);
  p.buttons = std::move(buttons);
  return p;
}

void Input::applyPadButtons(const std::vector<bool>& buttons, bool wheel) {
  const auto pressed = [&](int i) { return at(buttons, i) && !at(prevButtons, i); };
  if (wheel) {
    const std::vector<int>& sh = settings.wheel.shifter;
    // H-shifter: a gear is "in" while its button is held, neutral otherwise
    int gear = 0;
    bool any = false;
    for (size_t i = 0; i < sh.size(); i++) {
      if (at(buttons, sh[i])) {
        gear = i == 6 ? -1 : static_cast<int>(i) + 1;
        any = true;
      }
    }
    bool hadShifter = false;
    for (int i : sh)
      if (at(prevButtons, i)) hadShifter = true;
    if (any || hadShifter) c.gear = gear;
    // paddles
    if (pressed(4)) c.gear = shift(c.gear, 1);
    if (pressed(5)) c.gear = shift(c.gear, -1);
    if (pressed(0)) c.indicator = c.indicator == -1 ? 0 : -1;
    if (pressed(2)) c.indicator = c.indicator == 1 ? 0 : 1;
    if (pressed(3)) c.hazard = !c.hazard;
    if (pressed(1)) c.handbrake = c.handbrake > 0.5 ? 0 : 1;
    if (pressed(8)) fire("view");
  } else {
    if (pressed(5)) c.gear = shift(c.gear, 1);   // RB
    if (pressed(4)) c.gear = shift(c.gear, -1);   // LB
    if (pressed(0)) c.handbrake = c.handbrake > 0.5 ? 0 : 1;   // A
    if (pressed(2)) c.indicator = c.indicator == -1 ? 0 : -1;   // X
    if (pressed(1)) c.indicator = c.indicator == 1 ? 0 : 1;   // B
    if (pressed(3)) c.hazard = !c.hazard;   // Y
    if (pressed(12)) {
      c.lowBeam = !c.lowBeam;
      if (!c.lowBeam) c.highBeam = false;
    }
    if (pressed(13)) c.seatbelt = !c.seatbelt;
    if (pressed(8)) fire("view");
    if (pressed(16)) fire("pause");
  }
  prevButtons = buttons;
}

}  // namespace fs

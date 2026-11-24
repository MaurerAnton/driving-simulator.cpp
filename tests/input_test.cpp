// LINK: input core platform
// SPDX-License-Identifier: AGPL-3.0-only
// Origin: tests/input/camzoom.test.ts, tests/game/forkliftmodes.test.ts (the camera circling the vehicle) @ v1.7.7
/**
 * The player's controls (src/input): zooming the view – the pinch holds while the
 * fingers are on, the wheel and the keys let go after the time the settings choose –,
 * the camera circling a vehicle resting beside it (Input.orbitRest), and then the
 * Input class itself, driven through the page as a player drives it: keys, the
 * mouse, fingers, a gamepad, a steering wheel.
 *
 * (How the keys and the bar steer – tests/vehicle/steerfeel.test.ts – is
 * tests/steerfeel_test.cpp: it drives the real vehicles.)
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

#include "game/settings.h"
#include "input/camzoom.h"
#include "input/input.h"
#include "input/steerfeel.h"
#include "platform/dom.h"
#include "platform/native/dom_native.h"
#include "platform/native/native.h"
#include "platform/native/sensors_native.h"
#include "platform/plat.h"

using namespace fs;

namespace {

const double PI = std::numbers::pi;

bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }

void test(const char* name) { std::printf("  %s\n", name); }

// --- tests/input/camzoom.test.ts ---------------------------------------------------

void run(CamZoom& z, double seconds, const ZoomRelease& release = "auto", bool active = true) {
  for (double t = 0; t < seconds; t += 1.0 / 60) z.update(1.0 / 60, release, active);
}

void camzoom() {
  test("a pinch zooms and holds while the fingers are on, and lets go when they lift (auto)");
  {
    CamZoom z;
    z.pinch(2);
    run(z, 10);
    assert(std::abs(z.factor - 2) < 0.01);
    z.fingersOff();
    run(z, 2);
    assert(z.factor == 1);
  }

  test("the wheel lets go after a few seconds (auto), never with \"never\"");
  {
    CamZoom z;
    z.wheel(-0.2);
    run(z, 2);
    assert(z.factor > 1.2);
    run(z, 4);
    assert(z.factor == 1);
    z.wheel(-0.2);
    run(z, 30, "never");
    assert(z.factor > 1.2);
  }

  test("a key held keeps the zoom; the limits hold; inactive resets");
  {
    CamZoom z;
    for (int i = 0; i < 20; i++) z.press(1, true);
    assert(z.target == ZOOM_MAX);
    run(z, 10, "release");
    assert(std::abs(z.factor - ZOOM_MAX) < 0.01);
    z.press(0, false);
    run(z, 2, "release");
    assert(z.factor == 1);
    z.press(-1, true);
    assert(z.target == 1);
    z.pinch(3);
    run(z, 0.1, "auto", false);
    assert(z.factor == 1);
    assert(z.held() == false);
  }

  test("the angle of view narrows by the factor");
  {
    CamZoom z;
    assert(z.fov(60) == 60);
    z.factor = 2;
    const double half = std::tan((z.fov(60) * PI) / 360);
    assert(std::abs(half - std::tan(PI / 6) / 2) < 1e-9);
  }

  // (the settings' choices, as the original lists them)
  assert((ZOOM_RELEASES == std::vector<ZoomRelease>{"auto", "release", "3", "6", "12", "never"}));
  {
    CamZoom z;
    z.wheel(-0.2);
    assert(z.delay("auto") == 4 && z.delay("release") == 1.5 && z.delay("3") == 3 && z.delay("12") == 12 && std::isinf(z.delay("never")));
    z.pinch(2);
    assert(z.delay("auto") == 0 && z.delay("release") == 0 && z.delay("6") == 6);
  }
}

// --- the page a player sits at -------------------------------------------------------

/** a page with the game's canvas in it, a clock that the test moves, no gamepad, a mouse (not a phone) */
dom::El page() {
  dom::native::reset();
  plat::native::resetSensors();
  plat::native::manualClock(true);
  plat::native::setCoarsePointer(false);
  plat::native::setViewport(1280, 720);
  dom::El canvas = dom::el("canvas");
  dom::body().append(canvas);
  return canvas;
}

dom::Event pointer(const std::string& type, const std::string& pointerType, int id, double x, double y, int button = 0, int buttons = 0) {
  dom::Event e;
  e.type = type;
  e.pointerType = pointerType;
  e.pointerId = id;
  e.clientX = x;
  e.clientY = y;
  e.button = button;
  e.buttons = buttons;
  return e;
}

/** send it; false: a handler prevented the default */
bool send(const dom::El& to, dom::Event e) { return dom::native::dispatch(to, e); }

/** the mouse moved by (dx, dy) over the canvas */
void mouseMove(const dom::El& canvas, double dx, double dy, int buttons = 0, bool alt = false) {
  dom::Event e = pointer("pointermove", "mouse", 1, 0, 0, 0, buttons);
  e.movementX = dx;
  e.movementY = dy;
  e.altKey = alt;
  send(canvas, e);
}

bool keyDown(const std::string& code, const std::string& text = "") { return dom::native::key(code, text, true); }
bool keyUp(const std::string& code, const std::string& text = "") { return dom::native::key(code, text, false); }
/** a key pressed and let go */
void tap(const std::string& code) {
  keyDown(code);
  keyUp(code);
}

void frames(Input& input, int n, double speed = 0, double dt = 1.0 / 60) {
  for (int i = 0; i < n; i++) input.update(dt, speed, 0);
}

// --- tests/game/forkliftmodes.test.ts: the camera circling the vehicle ------------------

/** game/forkliftmodes.ts (the forklift is not part of 2.0.0: its view from the side is this angle) */
const double FORK_SIDE_YAW = -(PI / 2 + 0.35);

void orbitRest() {
  test("the camera circling the vehicle starts where it rests – beside it – and swings back there, not behind it");
  dom::El canvas = page();
  Settings settings;
  Input input(canvas, settings);
  input.resetLook(true, false, FORK_SIDE_YAW);
  assert(input.orbit == true);
  assert(input.c.lookYaw == FORK_SIDE_YAW);
  // standing: it stays beside it
  for (int i = 0; i < 300; i++) input.update(1.0 / 60, 0, 0);
  assert(std::abs(input.c.lookYaw - FORK_SIDE_YAW) < 1e-9);
  // driving after a look round (here: put behind it), nobody touching the view: back to the side, not behind it
  // (the original writes mouseLookYaw = 0; here the mouse drags the view there)
  send(canvas, pointer("pointerdown", "mouse", 1, 400, 300, 0, 1));
  mouseMove(canvas, FORK_SIDE_YAW / 0.005, 0, 1);
  send(canvas, pointer("pointerup", "mouse", 1, 400, 300, 0, 0));
  input.update(1.0 / 60, 0, 0);
  assert(std::abs(input.c.lookYaw - FORK_SIDE_YAW) > 0.1);   // (it is on its way behind the vehicle)
  for (int i = 0; i < 600; i++) input.update(1.0 / 60, 4, 0);
  assert(std::abs(input.c.lookYaw - FORK_SIDE_YAW) < 0.02);
  // another camera: behind again as before
  input.resetLook(true);
  assert(input.orbitRest == 0);
  assert(input.c.lookYaw == 0);
  // the driver's head: no resting place but straight ahead
  input.resetLook(false, false, 1.2);
  assert(input.orbitRest == 0);
  assert(input.c.lookYaw == 0);
}

// --- the Input class, driven through the page (not among the original's tests) -----------

void keysAndGears() {
  test("the keys: the gear lever of a manual gearbox and the selector of an automatic");
  dom::El canvas = page();
  Settings settings;
  Input input(canvas, settings);
  Controls& c = input.c;
  assert(c.handbrake == 1 && c.gear == 0 && c.ignition && c.lookZoom == 7 && c.device == "keyboard" && input.mode == "keys");
  tap("KeyE");
  assert(c.gear == 1);
  for (int i = 0; i < 9; i++) tap("KeyE");
  assert(c.gear == 6);
  tap("Digit3");
  assert(c.gear == 3);
  tap("KeyR");
  assert(c.gear == -1);
  tap("KeyQ");
  assert(c.gear == -1);   // (no further down)
  tap("KeyN");
  assert(c.gear == 0);
  tap("KeyP");
  assert(c.gear == 0);    // (no P in a manual gearbox)
  // an automatic: P R N D – up from P or R towards D, down from D through N to R, then P
  input.setAutomatic(true);
  assert(c.gear == -2);
  tap("KeyE");
  assert(c.gear == 1);
  tap("Digit3");
  assert(c.gear == 1);
  tap("KeyQ");
  assert(c.gear == 0);
  tap("KeyQ");
  assert(c.gear == -1);
  tap("KeyE");
  assert(c.gear == 0);
  tap("KeyQ");
  tap("KeyQ");
  assert(c.gear == -2);
  tap("Digit1");
  assert(c.gear == 1);
  tap("KeyP");
  assert(c.gear == -2);
  input.setAutomatic(false);
  assert(c.gear == 0);

  test("the keys: switches, and what the browser must not do with them");
  assert(!keyDown("Space") && c.handbrake == 0);   // (the page does not scroll)
  keyUp("Space");
  {
    // (a key that repeats while held switches nothing again)
    dom::Event e;
    e.type = "keydown";
    e.code = "Space";
    e.repeat = true;
    assert(!send(dom::body(), e) && c.handbrake == 0);
  }
  tap("Space");
  assert(c.handbrake == 1);
  tap("KeyZ");
  assert(c.indicator == -1);
  tap("KeyC");
  assert(c.indicator == 1);
  tap("KeyC");
  assert(c.indicator == 0);
  tap("KeyX");
  assert(c.hazard);
  tap("KeyB");
  assert(c.seatbelt);
  tap("KeyI");
  assert(!c.ignition);
  keyDown("Enter");
  input.update(1.0 / 60, 0, 0);
  assert(c.starter && c.ignition);
  keyUp("Enter");
  keyDown("KeyH");
  input.update(1.0 / 60, 0, 0);
  assert(c.horn && !c.starter);
  keyUp("KeyH");
  assert(!keyDown("ArrowUp") && !keyDown("AltLeft") && keyDown("KeyW"));
  keyUp("ArrowUp");
  keyUp("AltLeft");
  keyUp("KeyW");
  keyDown("Digit7");
  assert(input.washer());
  keyUp("Digit7");
  assert(!input.washer());

  test("the lights: L the dipped beam, O tapped the main beam, O held the flasher");
  tap("KeyO");
  assert(!c.highBeam);   // (no main beam without the dipped beam)
  tap("KeyL");
  assert(c.lowBeam);
  keyDown("KeyO");
  assert(input.flasher());
  plat::native::advance(100);
  keyUp("KeyO");
  assert(c.highBeam && !input.flasher());
  keyDown("KeyO");
  plat::native::advance(400);
  keyUp("KeyO");
  assert(c.highBeam);    // (held: the flasher, no switch)
  tap("KeyL");
  assert(!c.lowBeam && !c.highBeam);
  input.press("Flash", true);
  assert(input.flasher());
  input.press("Flash", false);
  assert(!input.flasher());

  test("what the keys ask of the game (actions) – and what stays when the driving is suspended");
  std::vector<std::string> got;
  input.onAction([&got](const Action& a) { got.push_back(a); });
  for (const char* code : {"KeyV", "KeyG", "Period", "KeyM", "KeyY", "KeyT", "Digit9", "Digit8", "KeyP", "Escape", "KeyF"}) tap(code);
  assert((got == std::vector<std::string>{"view", "door", "open", "map", "mirror", "reset", "service", "wiper", "panto", "pause", "reality"}));
  got.clear();
  assert(!keyDown("F1") && !keyDown("F2") && !keyDown("F3"));
  assert((got == std::vector<std::string>{"help", "hud", "turn"}));
  got.clear();
  input.enabled = false;
  for (const char* code : {"KeyV", "KeyM", "Escape", "F1", "KeyF", "KeyE", "KeyX"}) tap(code);
  assert((got == std::vector<std::string>{"pause", "help", "reality"}));
  assert(c.gear == 0 && c.hazard);
  c.throttle = 0.5;
  keyDown("Enter");
  assert(&input.update(1.0 / 60, 0, 0) == &c && c.throttle == 0 && !c.starter && !c.horn);
  keyUp("Enter");
  input.enabled = true;
  {
    // (Ctrl+F is the browser's search, not the photo)
    got.clear();
    dom::Event e;
    e.type = "keydown";
    e.code = "KeyF";
    e.ctrlKey = true;
    send(dom::body(), e);
    assert(got.empty());
    keyUp("KeyF");
  }

  test("a key typed into a field is the field's");
  {
    dom::El field = dom::el("input");
    dom::body().append(field);
    field.focus();
    got.clear();
    tap("KeyE");
    tap("KeyV");
    assert(c.gear == 0 && got.empty());
    field.blur();
    field.remove();
    tap("KeyE");
    assert(c.gear == 1);
  }

  test("the window loses the focus: no key stays down");
  keyDown("KeyW");
  frames(input, 5);
  assert(c.throttle > 0);
  dom::native::dispatch(dom::window(), "blur");
  frames(input, 60);
  assert(c.throttle == 0);
}

void keyboardDriving() {
  test("driving with the keys: the pedals are dosed, the lock is wound on");
  dom::El canvas = page();
  Settings settings;
  Input input(canvas, settings);
  Controls& c = input.c;
  keyDown("KeyW");
  frames(input, 60);
  // quick to half travel (0.04 a frame up to 0.35), slower to the floor (0.01 a frame)
  assert(near(c.throttle, 0.36 + 51 * 0.01, 1e-9));
  keyUp("KeyW");
  frames(input, 12);
  assert(c.throttle == 0);
  keyDown("ArrowDown");
  frames(input, 30);
  // (11 frames of 2.6/60 reach 0.4767, then 1.3/60 a frame)
  assert(near(c.brake, 11 * 2.6 / 60 + 19 * 1.3 / 60, 1e-9));
  keyUp("ArrowDown");
  keyDown("ShiftLeft");
  frames(input, 9);
  assert(c.clutch == 1);
  keyUp("ShiftLeft");
  frames(input, 7, 0, 1.0 / 64);
  // (let go: quick to the biting point – 2.2/s above 0.78 –, slowly through it)
  assert(near(c.clutch, 1 - 7 * 2.2 / 64, 1e-9));
  frames(input, 1, 0, 1.0 / 64);
  assert(near(c.clutch, 1 - 7 * 2.2 / 64 - 0.55 / 64, 1e-9));
  // standing, the keys ask for the full lock: 1.2 of it a second
  keyDown("KeyD");
  frames(input, 30);
  assert(near(c.steer, 0.6, 1e-9) && c.device == "keyboard");
  keyUp("KeyD");
  frames(input, 30);
  assert(near(c.steer, 0.6, 1e-9));   // (standing: the wheel stays where it is)
  frames(input, 120, 10);
  assert(c.steer == 0);               // (rolling: it centres itself)
  // at speed the keys ask for little: the lock that turns the car with keyLateral(v)
  keyDown("ArrowLeft");
  frames(input, 120, 30);
  const SteerFeel f = steerFeel();
  assert(near(c.steer, -lockFor(f, keyLateral(30), 30), 1e-9) && c.steer > -0.02);
  // the sensitivity of the settings is read every frame
  settings.steerSensitivity = 2;
  frames(input, 120, 30);
  assert(near(c.steer, -2 * lockFor(f, keyLateral(30), 30), 1e-9));
  keyUp("ArrowLeft");

  test("the indicator cancels itself when the wheel comes back");
  settings.steerSensitivity = 1;
  frames(input, 120, 10);
  tap("KeyC");
  keyDown("KeyD");
  frames(input, 60);
  assert(c.steer > 0.22 && c.indicator == 1);
  keyUp("KeyD");
  frames(input, 120, 10);
  assert(c.steer == 0 && c.indicator == 0);
  // (set, but the wheel never turned into the bend: it stays on)
  tap("KeyZ");
  frames(input, 60, 10);
  assert(c.indicator == -1);

  test("a heavy vehicle is steered slower (setSteering with its spec)");
  const SteerSpec lorry(18000, 3.8, 0.7, 20);
  input.setSteering(lorry.wheelbase, lorry.maxWheelAngle, lorry);
  c.steer = 0;
  keyDown("KeyD");
  frames(input, 30);
  // (a lorry's steering wheel: 500°/s over a lock of 0.7 × 20 rad)
  assert(near(c.steer, 0.5 * LORRY_WHEEL_SPEED / 14, 1e-9));
  input.setSteering(2.63, 0.576);
  c.steer = 0;
  frames(input, 30);
  assert(near(c.steer, 0.6, 1e-9));
  keyUp("KeyD");
}

void lookingRound() {
  test("the mouse: the left button turns the head (with stops), the glance keys look over the shoulder");
  dom::El canvas = page();
  Settings settings;
  Input input(canvas, settings);
  Controls& c = input.c;
  mouseMove(canvas, 100, 0);
  frames(input, 30);
  assert(c.lookYaw == 0);   // (no button: nothing turns)
  send(canvas, pointer("pointerdown", "mouse", 1, 400, 300, 0, 1));
  assert(canvas.hasPointerCapture(1));
  mouseMove(canvas, -100, 50, 1);
  frames(input, 30);
  assert(near(c.lookYaw, 0.5) && near(c.lookPitch, -0.2));
  mouseMove(canvas, -1000, 1000, 1);
  frames(input, 60);
  assert(near(c.lookYaw, 2.6) && near(c.lookPitch, -0.8));
  send(canvas, pointer("pointerup", "mouse", 1, 400, 300, 0, 0));
  mouseMove(canvas, 100, 0);
  frames(input, 30);
  assert(near(c.lookYaw, 2.6));   // (let go: the head stays turned)
  mouseMove(canvas, 20, 0, 0, true);   // (Alt, with no button at all, turns it too)
  frames(input, 30);
  assert(near(c.lookYaw, 2.5));
  send(canvas, pointer("pointerdown", "mouse", 1, 400, 300, 0, 1));
  settings.invertLookY = true;
  mouseMove(canvas, 0, 50, 1);
  frames(input, 30);
  assert(near(c.lookPitch, -0.6));
  send(canvas, pointer("pointerup", "mouse", 1, 400, 300, 0, 0));
  settings.invertLookY = false;
  dom::native::dispatch(canvas, "dblclick");
  frames(input, 60);
  assert(c.lookYaw == 0 && c.lookPitch == 0);
  keyDown("KeyJ");
  assert(input.glanceYaw() == 1.6);
  frames(input, 60);
  assert(near(c.lookYaw, 1.75));
  keyDown("KeyK");
  assert(input.glanceYaw() == PI);
  frames(input, 60);
  assert(near(c.lookYaw, 2.6));
  keyUp("KeyJ");
  assert(input.glanceYaw() == -1.6);
  frames(input, 120);
  assert(near(c.lookYaw, -1.75));
  keyUp("KeyK");
  frames(input, 60);
  assert(c.lookYaw == 0 && input.glanceYaw() == 0);

  test("the camera behind the car: no stops, the short way round, the wheel moves it away, and it swings back when driving");
  input.resetLook(true);
  send(canvas, pointer("pointerdown", "mouse", 1, 400, 300, 0, 1));
  mouseMove(canvas, -700, 0, 1);   // (3.5 rad to the left: past the front of the car)
  send(canvas, pointer("pointerup", "mouse", 1, 400, 300, 0, 0));
  frames(input, 120);
  assert(near(c.lookYaw, 3.5 - 2 * PI));
  keyDown("KeyU");
  frames(input, 60);
  assert(near(std::abs(c.lookYaw), PI));
  keyUp("KeyU");
  frames(input, 60);
  assert(near(c.lookYaw, 3.5 - 2 * PI));
  frames(input, 200);   // (standing: it stays)
  assert(near(c.lookYaw, 3.5 - 2 * PI));
  frames(input, 300, 10);
  assert(c.lookYaw == 0);
  {
    dom::Event e;
    e.type = "wheel";
    e.deltaY = 100;
    assert(!send(canvas, e) && near(c.lookZoom, 7 * std::exp(0.16)));
    e = dom::Event();
    e.type = "wheel";
    e.deltaY = 100000;   // (a notch at most at a time, and no further than five times the vehicle's measure)
    for (int i = 0; i < 40; i++) send(canvas, e);
    assert(c.lookZoom == 90);
    input.zoomMax = 30;
    for (int i = 0; i < 40; i++) send(canvas, e);
    assert(c.lookZoom == 150);
    e = dom::Event();
    e.type = "wheel";
    e.deltaY = -100000;
    for (int i = 0; i < 40; i++) send(canvas, e);
    assert(c.lookZoom == 3.5);
    // the driver's seat: the wheel is the page's
    input.resetLook(false);
    e = dom::Event();
    e.type = "wheel";
    e.deltaY = 100;
    assert(send(canvas, e) && c.lookZoom == 3.5);
  }
  {
    dom::Event e;
    e.type = "contextmenu";
    assert(!send(canvas, e));
  }

  test("the free camera: what was asked of it since it was last read (takeCam)");
  input.resetLook(false, true);
  send(canvas, pointer("pointerdown", "mouse", 1, 400, 300, 0, 1));
  mouseMove(canvas, 40, -20, 1);
  send(canvas, pointer("pointerup", "mouse", 1, 400, 300, 0, 0));
  mouseMove(canvas, 72, 36, 2);   // (the right button moves it: parts of the screen's height)
  {
    dom::Event e;
    e.type = "wheel";
    e.deltaY = -50;
    assert(!send(canvas, e));
  }
  dom::native::dispatch(canvas, "dblclick");
  {
    // Alt held: the keys fly the camera, they do not drive
    dom::Event e;
    e.type = "keydown";
    e.code = "AltLeft";
    e.altKey = true;
    send(dom::body(), e);
    e = dom::Event();
    e.type = "keydown";
    e.code = "KeyW";
    e.altKey = true;
    assert(!send(dom::body(), e));
    e = dom::Event();
    e.type = "keydown";
    e.code = "KeyE";
    e.altKey = true;
    send(dom::body(), e);
    keyDown("ShiftLeft");
  }
  frames(input, 30);
  assert(c.gear == 0 && c.throttle == 0 && c.clutch == 0);
  {
    const Input::Cam& t = input.takeCam();
    assert(near(t.yaw, -0.2) && near(t.pitch, 0.08) && near(t.right, -0.1) && near(t.fwd, 0.05) && near(t.dolly, 0.08) && t.reset && t.fast);
    assert(near(t.keyFwd, 0.5) && t.keyRight == 0 && near(t.keyUp, 0.5) && t.lift == 0 && t.speed == 0);
    const Input::Cam& again = input.takeCam();
    assert(&again == &t && again.yaw == 0 && again.right == 0 && again.dolly == 0 && again.keyFwd == 0 && !again.reset);
  }
  for (const char* code : {"AltLeft", "KeyW", "KeyE", "ShiftLeft"}) keyUp(code);
  assert(c.lookYaw == 0);

  test("the flying camera: its keys fly without Alt, the wheel sets its speed, the vehicle is held");
  input.flyCam = true;
  assert(!keyDown("KeyD") && !keyDown("PageUp"));
  input.press("FlyDown", true);
  input.stick.y = 0.5;
  c.brake = 0;
  frames(input, 60);
  assert(c.throttle == 0 && c.brake == 1 && c.clutch == 1 && c.steer == 0);
  {
    dom::Event e;
    e.type = "wheel";
    e.deltaY = 100;
    assert(!send(canvas, e));
    const Input::Cam& t = input.takeCam();
    assert(near(t.keyRight, 1) && near(t.keyFwd, 0.5) && near(t.keyUp, 0) && near(t.speed, -1));
  }
  keyUp("KeyD");
  keyUp("PageUp");
  input.press("FlyDown", false);
  input.stick.y = 0;
  input.flyCam = false;
}

void fingers() {
  test("a phone: fingers are in use from the start, a real key takes over, a button on the screen does not");
  dom::El canvas = page();
  plat::native::setCoarsePointer(true);
  Settings settings;
  Input input(canvas, settings);
  Controls& c = input.c;
  assert(input.mode == "touch");
  input.touched();
  assert(input.touch.active);
  input.press("KeyE", true);
  input.press("KeyE", false);
  assert(c.gear == 1 && input.mode == "touch" && input.touch.active);
  tap("KeyE");
  assert(c.gear == 2 && input.mode == "keys" && !input.touch.active);
  input.touched();
  tap("Escape");   // (Escape is the menu's, on a phone too)
  assert(input.mode == "touch" && input.touch.active);
  plat::native::setTrusted(false);   // (a key a script sends is no keyboard)
  tap("KeyQ");
  assert(c.gear == 1 && input.mode == "touch" && input.touch.active);
  plat::native::setTrusted(true);

  test("the steering bar and the pedals on the screen");
  input.touch.steer = -1;
  input.touch.throttle = 0.7;
  input.touch.brake = 0.1;
  input.touch.clutch = 0.3;
  frames(input, 15);
  // (standing: the bar winds the full lock on at four times a second)
  assert(near(c.steer, -1) && c.throttle == 0.7 && c.brake == 0.1 && c.clutch == 0.3 && c.device == "touch");
  frames(input, 300, 20);
  const SteerFeel f = steerFeel();
  // (at speed: no more than the lock that turns the car with 7 m/s²)
  assert(near(c.steer, -lockFor(f, ANALOG_LATERAL, 20)) && c.steer > -0.09);
  input.untouched();
  assert(!input.touch.active && input.touch.steer == 0);
  frames(input, 120, 20);
  assert(c.steer == 0 && c.throttle == 0 && c.device == "keyboard");

  test("a finger on the view glances (and the view swings back), a second one takes over when the first lifts");
  input.mode = "keys";
  send(canvas, pointer("pointerdown", "touch", 7, 600, 300));
  assert(input.mode == "touch");
  send(canvas, pointer("pointermove", "touch", 7, 700, 340));
  frames(input, 30);
  // (5.4 rad over the width of the screen; half of it up and down)
  assert(near(c.lookYaw, -100 * 5.4 / 1280) && near(c.lookPitch, -40 * 5.4 / 1280 * 0.5));
  send(canvas, pointer("pointerdown", "touch", 8, 900, 300));
  send(canvas, pointer("pointerup", "touch", 7, 700, 340));
  send(canvas, pointer("pointermove", "touch", 8, 800, 300));
  frames(input, 30);
  assert(near(c.lookYaw, 0));
  send(canvas, pointer("pointermove", "touch", 8, 600, 300));
  frames(input, 30);
  assert(near(c.lookYaw, 200 * 5.4 / 1280));
  send(canvas, pointer("pointercancel", "touch", 8, 600, 300));
  frames(input, 60);
  assert(c.lookYaw == 0 && c.lookPitch == 0);
  send(canvas, pointer("pointermove", "touch", 8, 100, 300));   // (a finger that is not down moves nothing)
  frames(input, 10);
  assert(c.lookYaw == 0);

  test("two fingers: a pinch moves the camera behind the car, zooms the view, moves the free camera, sets the flying camera's speed");
  input.resetLook(true);
  send(canvas, pointer("pointerdown", "touch", 1, 500, 300));
  send(canvas, pointer("pointerdown", "touch", 2, 600, 300));
  send(canvas, pointer("pointermove", "touch", 2, 700, 300));
  assert(near(c.lookZoom, 3.5));   // (twice as far apart: half as far away)
  send(canvas, pointer("pointermove", "touch", 2, 550, 300));
  assert(near(c.lookZoom, 14));
  frames(input, 30);
  assert(c.lookYaw == 0);          // (two fingers do not turn the view)
  input.viewZoom = true;
  send(canvas, pointer("pointermove", "touch", 2, 600, 300));
  assert(near(input.zoom.target, 2) && input.zoom.held() && near(c.lookZoom, 14));
  send(canvas, pointer("pointerup", "touch", 2, 600, 300));
  assert(!input.zoom.held());
  input.viewZoom = false;
  input.zoom.reset();
  input.resetLook(false, true);
  send(canvas, pointer("pointerdown", "touch", 2, 600, 300));
  send(canvas, pointer("pointermove", "touch", 2, 700, 372));
  {
    const Input::Cam& t = input.takeCam();
    assert(near(t.right, -100 * 0.5 / 720) && near(t.fwd, 0.05) && near(t.dolly, std::log(std::hypot(200, 72) / 100)) && t.yaw == 0);
  }
  input.flyCam = true;
  send(canvas, pointer("pointermove", "touch", 2, 700, 300));
  {
    const Input::Cam& t = input.takeCam();
    assert(near(t.lift, 0.05) && near(t.speed, std::log(200 / std::hypot(200, 72)) * 4) && t.right == 0);
  }
  input.flyCam = false;
  send(canvas, pointer("pointerup", "touch", 1, 500, 300));
  send(canvas, pointer("pointerup", "touch", 2, 700, 300));
  // (one finger turns the free camera, and it stays where it is put)
  send(canvas, pointer("pointerdown", "touch", 3, 500, 300));
  send(canvas, pointer("pointermove", "touch", 3, 400, 300));
  send(canvas, pointer("pointerup", "touch", 3, 400, 300));
  frames(input, 30);
  assert(near(input.takeCam().yaw, 100 * 5.4 / 1280) && c.lookYaw == 0);
}

void zoomingTheView() {
  test("on foot and in the driver's seat: + and −, the wheel and a pinch zoom the view");
  dom::El canvas = page();
  Settings settings;
  Input input(canvas, settings);
  keyDown("BracketRight", "+");
  assert(input.zoom.target == 1);   // (only while the game says the view can be zoomed)
  keyUp("BracketRight", "+");
  input.viewZoom = true;
  keyDown("BracketRight", "+");   // (+ on a German keyboard: by the character)
  assert(near(input.zoom.target, 1.25) && input.zoom.held());
  keyUp("BracketRight", "+");
  assert(!input.zoom.held());
  keyDown("NumpadAdd");
  keyUp("NumpadAdd");
  keyDown("Equal", "=");
  keyUp("Equal", "=");
  assert(near(input.zoom.target, 1.25 * 1.25 * 1.25));
  keyDown("Slash", "-");
  keyUp("Slash", "-");
  keyDown("NumpadSubtract");
  keyUp("NumpadSubtract");
  assert(near(input.zoom.target, 1.25));
  {
    dom::Event e;
    e.type = "keydown";
    e.code = "Equal";
    e.key = "+";
    e.ctrlKey = true;   // (Ctrl and + is the browser's own zoom)
    send(dom::body(), e);
    assert(near(input.zoom.target, 1.25));
    e = dom::Event();
    e.type = "wheel";
    e.deltaY = -100;
    assert(!send(canvas, e));
    assert(near(input.zoom.target, 1.25 * std::exp(0.16 * 1.6)) && input.c.lookZoom == 7);
    input.enabled = false;
    e = dom::Event();
    e.type = "wheel";
    e.deltaY = -100;
    assert(send(canvas, e));
  }
}

void gamepad() {
  test("a gamepad: the stick steers (shaped), the triggers are the pedals, the buttons switch once a press");
  dom::El canvas = page();
  Settings settings;
  Input input(canvas, settings);
  Controls& c = input.c;
  std::vector<std::string> got;
  input.onAction([&got](const Action& a) { got.push_back(a); });
  plat::GamepadState pad = plat::native::gamepad("Xbox 360 Controller (XInput STANDARD GAMEPAD)", "standard", 4, 17);
  plat::native::setGamepads({pad});
  frames(input, 10);
  assert(c.device == "keyboard" && input.lastGamepadId == pad.id);   // (nothing moved on it: the keys still drive)
  pad.axes[0] = 0.55;
  pad.buttons[7] = {true, 0.52};
  pad.buttons[6] = {false, 0.03};
  plat::native::setGamepads({pad});
  frames(input, 60);
  assert(c.device == "gamepad" && near(c.steer, std::pow(0.5, 1.6)) && near(c.throttle, 0.5) && c.brake == 0 && c.clutch == 0);
  pad.axes[0] = 0.05;   // (the stick's play)
  pad.buttons[7] = {false, 0};
  plat::native::setGamepads({pad});
  frames(input, 60);
  assert(c.steer == 0 && c.throttle == 0 && c.device == "gamepad");
  // the buttons: RB and LB shift, A the parking brake, X and B the indicators, Y the hazard lights, the pad the lights and the belt
  for (int b : {5, 0, 2, 3, 12, 13, 8, 16}) pad.buttons[static_cast<size_t>(b)] = {true, 1};
  plat::native::setGamepads({pad});
  frames(input, 5);   // (held: still one press)
  assert(c.gear == 1 && c.handbrake == 0 && c.indicator == -1 && c.hazard && c.lowBeam && c.seatbelt);
  assert((got == std::vector<std::string>{"view", "pause"}));
  for (plat::GamepadButton& b : pad.buttons) b = {false, 0};
  plat::native::setGamepads({pad});
  frames(input, 1);
  pad.buttons[4] = {true, 1};
  pad.buttons[1] = {true, 1};
  pad.buttons[9] = {true, 1};
  pad.buttons[10] = {true, 1};
  plat::native::setGamepads({pad});
  frames(input, 2);
  assert(c.gear == 0 && c.indicator == 1 && c.starter && c.horn);
  for (plat::GamepadButton& b : pad.buttons) b = {false, 0};
  // the right stick looks round, and the view comes back when it is let go
  pad.axes[2] = -1;
  plat::native::setGamepads({pad});
  frames(input, 60);
  assert(near(c.lookYaw, 1.9) && !c.starter && !c.horn);
  pad.axes[2] = 0;
  plat::native::setGamepads({pad});
  frames(input, 60);
  assert(c.lookYaw == 0);
  // a driving key takes the car back from the pad
  pad.axes[0] = 1;
  plat::native::setGamepads({pad});
  keyDown("KeyW");
  frames(input, 5);
  assert(c.device == "keyboard" && c.throttle > 0);
  keyUp("KeyW");
  frames(input, 5);
  assert(c.device == "gamepad");
  // working a crane: only the camera and the pause stay the game's
  pad.axes[0] = 0;
  plat::native::setGamepads({pad});
  frames(input, 60);
  input.crane = true;
  got.clear();
  for (int b : {5, 8, 16}) pad.buttons[static_cast<size_t>(b)] = {true, 1};
  plat::native::setGamepads({pad});
  frames(input, 3);
  assert(c.gear == 0 && (got == std::vector<std::string>{"view", "pause"}));
  tap("KeyE");
  tap("KeyM");
  assert(c.gear == 0 && got.size() == 3 && got[2] == "map");
  input.crane = false;

  test("on foot, flying, a forklift's mast: what the keys, the screen and the pad ask for");
  for (plat::GamepadButton& b : pad.buttons) b = {false, 0};
  pad.axes = {0.55, -1, 0.5, 0.6};
  pad.buttons[7] = {true, 1};
  pad.buttons[15] = {true, 1};
  plat::native::setGamepads({pad});
  {
    const Input::Walk tank = input.walkInput();
    assert(tank.fwd == 1 && near(tank.turn, -0.5) && tank.side == 0 && tank.run);
    const Input::Walk classic = input.walkInput("classic");
    assert(classic.fwd == 1 && near(classic.side, 0.5) && near(classic.turn, -0.5) && classic.run);
    const Input::Flight fl = input.flightInput();
    assert(near(fl.roll, 0.5) && fl.pitch == -1 && fl.throttle == 1 && fl.brake == 0 && fl.yaw == 0);
    const Input::Fork fk = input.forkInput();
    assert(near(fk.lift, -0.6) && fk.tilt == 1 && fk.shift == 0);
  }
  plat::native::setGamepads({});
  keyDown("KeyW");
  keyDown("KeyA");
  keyDown("ArrowRight");
  {
    const Input::Walk tank = input.walkInput("tank");
    assert(tank.fwd == 1 && tank.turn == 0 && tank.side == 0 && !tank.run);
    const Input::Walk tapWalk = input.walkInput("tap");
    assert(tapWalk.fwd == 1 && tapWalk.turn == -1 && tapWalk.side == -1);
    const Input::Flight fl = input.flightInput();
    assert(fl.roll == 1 && fl.yaw == -1 && fl.throttle == 1 && fl.pitch == 0);
  }
  tap("CapsLock");
  assert(input.runToggle && input.walkInput().run);
  tap("CapsLock");
  keyDown("ShiftRight");
  assert(input.walkInput().run);
  for (const char* code : {"KeyW", "KeyA", "ArrowRight", "ShiftRight"}) keyUp(code);
  input.stick.x = -0.4;
  input.stick.y = 0.9;
  input.touched();
  input.touch.throttle = 0.9;
  input.touch.steer = 0.25;
  input.touch.pitch = 0.5;
  input.touch.brake = 0.3;
  {
    const Input::Walk w = input.walkInput("classic");
    assert(w.fwd == 1 && near(w.side, -0.4) && near(w.turn, -0.25) && w.run);
    const Input::Flight fl = input.flightInput();
    assert(near(fl.roll, 0.25) && fl.pitch == 0.5 && fl.throttle == 0 && fl.brake == 0.3);
  }
  input.untouched();
  input.stick = {};
  keyDown("PageUp");
  keyDown("End");
  keyDown("Delete");
  {
    const Input::Fork fk = input.forkInput();
    assert(fk.lift == 1 && fk.tilt == -1 && fk.shift == 1);
    input.enabled = false;
    const Input::Fork off = input.forkInput();
    assert(off.lift == 0 && off.tilt == 0 && off.shift == 0);
    input.enabled = true;
  }
}

void steeringWheel() {
  test("a steering wheel: its angle is the car's, the pedals rest at +1, the H-shifter holds a gear");
  dom::El canvas = page();
  Settings settings;
  Input input(canvas, settings);
  Controls& c = input.c;
  std::vector<std::string> got;
  input.onAction([&got](const Action& a) { got.push_back(a); });
  plat::GamepadState wheel = plat::native::gamepad("Logitech G29 Driving Force Racing Wheel (Vendor: 046d Product: c24f)", "", 6, 25);
  wheel.axes = {0.5, 0, -1, 0, 0, 1};   // (the wheel a quarter to the right, the clutch half down, the throttle on the floor, the brake at rest)
  plat::native::setGamepads({wheel});
  frames(input, 1);
  assert(c.device == "wheel" && near(c.steer, 0.5 * 900 / 957) && c.throttle == 1 && c.brake == 0 && near(c.clutch, 0.45 / 0.95));
  wheel.axes[0] = 1;   // (900° of wheel is nearly all of the car's 957°)
  wheel.axes[2] = 1;
  wheel.axes[5] = 0.94;   // (a foot resting on the brake: 3 % of play)
  plat::native::setGamepads({wheel});
  frames(input, 1);
  assert(near(c.steer, 900.0 / 957) && c.throttle == 0 && near(c.brake, 0));
  settings.wheel.wheelRange = 1080;
  settings.wheel.pedalsInverted = false;
  frames(input, 1);
  assert(c.steer == 1 && c.throttle == 1);
  settings.wheel.throttleAxis = 9;   // (an axis the wheel does not have)
  frames(input, 1);
  assert(c.throttle == 0);
  // the H-shifter: a gear is in while its button is held, neutral otherwise
  wheel.buttons[14] = {true, 1};
  plat::native::setGamepads({wheel});
  frames(input, 3);
  assert(c.gear == 3);
  wheel.buttons[14] = {false, 0};
  plat::native::setGamepads({wheel});
  frames(input, 1);
  assert(c.gear == 0);
  wheel.buttons[18] = {true, 1};
  plat::native::setGamepads({wheel});
  frames(input, 1);
  assert(c.gear == -1);
  wheel.buttons[18] = {false, 0};
  // the paddles and the buttons on the wheel
  wheel.buttons[4] = {true, 1};
  for (int b : {0, 3, 1, 8, 9, 10}) wheel.buttons[static_cast<size_t>(b)] = {true, 1};
  plat::native::setGamepads({wheel});
  frames(input, 3);
  assert(c.gear == 1 && c.indicator == -1 && c.hazard && c.handbrake == 0 && c.starter && c.horn && (got == std::vector<std::string>{"view"}));
  // (a pad the browser has a layout for is no wheel, whatever it is called)
  wheel.mapping = "standard";
  for (plat::GamepadButton& b : wheel.buttons) b = {false, 0};
  wheel.axes = {0, 0, 0, 0, 0, 0};
  plat::native::setGamepads({wheel});
  frames(input, 1);
  assert(c.device != "wheel");
}

}  // namespace

int main() {
  camzoom();
  orbitRest();
  keysAndGears();
  keyboardDriving();
  lookingRound();
  fingers();
  zoomingTheView();
  gamepad();
  steeringWheel();
  std::printf("input_test: ok\n");
  return 0;
}

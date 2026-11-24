// LINK: input ui engine game sim world core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
// Origin: tests/ui/tiltsteer.test.ts @ v1.7.7
/**
 * The phone's controls (src/ui): steering by tilt – the phone's left/right tilt
 * as the steer −1 … +1, calibrated at the moment it was switched on –, and then,
 * driven through the page as a player's fingers drive them: the tilt steering's
 * listeners and its permission, the stick on the screen, a tap on the vehicle's
 * screen and a tap on the world.
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "engine/geometries.h"
#include "game/settings.h"
#include "game/spawnplace.h"
#include "input/input.h"
#include "platform/dom.h"
#include "platform/native/dom_native.h"
#include "platform/native/native.h"
#include "platform/native/sensors_native.h"
#include "platform/plat.h"
#include "render/navscreen.h"
#include "ui/screentap.h"
#include "ui/tiltsteer.h"
#include "ui/touchstick.h"
#include "ui/worldtap.h"

using namespace fs;

namespace {

bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }

void test(const char* name) { std::printf("  %s\n", name); }

// --- tests/ui/tiltsteer.test.ts ------------------------------------------------------

void tiltArithmetic() {
  test("the calibrated angle is straight ahead: no steer");
  assert(steerFromTilt(12, 12) == 0);
  assert(steerFromTilt(0, 0) == 0);

  test("tilted right turns right, tilted left turns left");
  assert(steerFromTilt(TILT_DEG, 0) > 0);    // 30° right = full lock right
  assert(steerFromTilt(-TILT_DEG, 0) < 0);   // 30° left = full lock left
  assert(steerFromTilt(10, 0) > 0);
  assert(steerFromTilt(-10, 0) < 0);

  test("TILT_DEG of tilt is full lock, and more is clipped");
  assert(steerFromTilt(TILT_DEG, 0) == 1);
  assert(steerFromTilt(-TILT_DEG, 0) == -1);
  assert(steerFromTilt(TILT_DEG * 3, 0) == 1);
  assert(steerFromTilt(-TILT_DEG * 3, 0) == -1);
  // half the tilt is half the lock
  assert(std::abs(steerFromTilt(TILT_DEG / 2, 0) - 0.5) < 1e-9);

  test("calibration shifts with the held angle");
  // held at 15° when switched on: 15° is straight, 30° is 15° of turn
  assert(steerFromTilt(15, 15) == 0);
  assert(steerFromTilt(30, 15) > 0);
  assert(steerFromTilt(0, 15) < 0);

  test("a device that reports nothing (null gamma) does not steer");
  assert(steerFromTilt(JS_NAN, 0) == 0);
}

// --- the page a player holds -----------------------------------------------------------

/** a page with the game's #ui element in it, a clock that the test moves, a phone */
dom::El page() {
  dom::native::reset();
  plat::native::resetSensors();
  plat::native::manualClock(true);
  plat::native::setCoarsePointer(true);
  plat::native::setViewport(800, 600);
  dom::El ui = dom::el("div");
  ui.id("ui");
  dom::body().append(ui);
  dom::native::setRect(ui, dom::Rect{0, 0, 800, 600});
  return ui;
}

dom::Event pointer(const std::string& type, const std::string& pointerType, int id, double x, double y, int button = 0) {
  dom::Event e;
  e.type = type;
  e.pointerType = pointerType;
  e.pointerId = id;
  e.clientX = x;
  e.clientY = y;
  e.button = button;
  return e;
}

/** send it; false: a handler prevented the default */
bool send(const dom::El& to, dom::Event e) { return dom::native::dispatch(to, e); }

/** the phone is held at these angles (an event of the orientation sensor) */
void tilt(double gamma, double beta) {
  dom::Event e;
  e.type = "deviceorientation";
  e.gamma = gamma;
  e.beta = beta;
  e.hasOrientation = true;
  dom::native::dispatch(dom::window(), e, false);
}

/** a finger down and up again at the same place, `ms` later */
void tapAt(const dom::El& canvas, double x, double y, double ms = 80, int id = 1) {
  send(canvas, pointer("pointerdown", "touch", id, x, y));
  plat::native::advance(ms);
  send(canvas, pointer("pointerup", "touch", id, x, y));
}

// --- ui/tiltsteer: the listeners -------------------------------------------------------

void tiltSteer() {
  test("tilt steering: no sensor, no steering");
  page();
  std::vector<double> out;
  {
    TiltSteer t([&out](double steer) { out.push_back(steer); });
    assert(plat::native::wait(t.start()) == false);
    assert(!t.steer());
    tilt(20, 40);
    assert(out.empty() && !t.steer());
    t.stop();
    assert(out.empty());
  }

  test("tilt steering: the first reading is straight ahead, the tilt from there steers");
  page();
  plat::native::setDeviceOrientation(true);
  {
    TiltSteer t([&out](double steer) { out.push_back(steer); });
    assert(plat::native::wait(t.start()) == true);
    assert(plat::native::wait(t.start()) == true);   // (once is enough)
    assert(!t.steer() && out.empty());
    {
      // (an event without readings: a device that reports nothing)
      dom::Event e;
      e.type = "deviceorientation";
      dom::native::dispatch(dom::window(), e, false);
      assert(!t.steer() && out.empty());
    }
    tilt(12, 40);
    assert(t.steer() == 0.0 && out.size() == 1 && out[0] == 0);
    tilt(27, 41);
    assert(near(*t.steer(), 0.5) && out.size() == 2 && near(out[1], 0.5));   // (the events arrive once: one listener)
    tilt(-48, 40);
    assert(*t.steer() == -1);
    // held another way – flat, or in a mount – the turn moves beta: whichever strays further is the turn
    tilt(15, 60);
    assert(near(*t.steer(), 20.0 / 30));
    tilt(15, 46);
    assert(near(*t.steer(), 3.0 / 30));   // (beta strays 6°, gamma 3°: not 4° more – gamma it is)
    // a line in the console now and then (once a second at most)
    const std::vector<std::string>& lines = plat::native::debugLines();
    assert(lines.size() == 2 && lines[0] == "[tilt] listening for deviceorientation");
    assert(lines[1] == "[tilt] gamma 12.0° (Δ0.0) beta 40.0° (Δ0.0) → steer 0.00");
    plat::native::advance(1001);
    tilt(-3, 40);
    assert(lines.size() == 3 && lines[2] == "[tilt] gamma -3.0° (Δ-15.0) beta 40.0° (Δ0.0) → steer -0.50");
    // switched off: the steer is centred, nothing is listened for
    const size_t n = out.size();
    t.stop();
    assert(!t.steer() && out.size() == n + 1 && out.back() == 0);
    tilt(30, 40);
    assert(!t.steer() && out.size() == n + 1);
    // switched on again: calibrated anew
    assert(plat::native::wait(t.start()) == true);
    tilt(30, 40);
    assert(t.steer() == 0.0);
    tilt(45, 40);
    assert(near(*t.steer(), 0.5));
    t.stop();
  }

  test("tilt steering: where the browser asks (iOS), it is asked at the first touch or key – once");
  for (const std::string answer : {"granted", "denied", "throw"}) {
    page();
    plat::native::setDeviceOrientation(true, true, answer);
    out.clear();
    TiltSteer t([&out](double steer) { out.push_back(steer); });
    assert(plat::native::wait(t.start()) == true);
    assert(plat::native::permissionAsked() == 0);
    tilt(10, 40);   // (before the asking: the listener is there already)
    assert(t.steer() == 0.0);
    if (answer == "throw") dom::native::key("KeyW");
    else send(dom::body(), pointer("pointerdown", "touch", 1, 100, 100));
    assert(plat::native::permissionAsked() == 1);
    send(dom::body(), pointer("pointerdown", "touch", 1, 100, 100));
    dom::native::key("KeyW");
    assert(plat::native::permissionAsked() == 1);
    assert(t.steer().has_value());   // (the answer has not come yet)
    plat::native::drain();
    tilt(25, 40);
    if (answer == "granted") assert(near(*t.steer(), 0.5));
    else assert(!t.steer() && out.back() == 0);   // (refused: switched off, the steer centred)
    t.stop();
  }
}

// --- ui/touchstick ---------------------------------------------------------------------

void touchStick() {
  test("the stick on the screen: hidden until it is of use, and what hides with it");
  dom::El ui = page();
  dom::El canvas = dom::el("canvas");
  ui.append(canvas);
  Settings settings;
  Input input(canvas, settings);
  input.mode = "keys";
  TouchStick stick(ui, input, [](const std::string& key) { return "<" + key + ">"; });
  const dom::El pad = ui.querySelector(".tstick");
  const std::vector<dom::El> buttons = ui.querySelectorAll(".tstick-btn");
  const dom::El badge = ui.querySelector(".tstick-badge");
  assert(pad && buttons.size() == 3 && badge && pad.hidden() && pad.childElementCount() == 1);
  const dom::El knob = pad.firstElementChild();
  const dom::El run = buttons[0], up = buttons[1], down = buttons[2];
  assert(run.hidden() && up.hidden() && down.hidden());
  assert(dom::native::text(run) == "🏃" && run.get("title") == "<walk.run>" && run.getAttribute("aria-label") == "<walk.run>" && run.get("type") == "button");
  assert(dom::native::text(up) == "▲" && up.get("title") == "<fly.up>" && dom::native::text(down) == "▼" && down.get("title") == "<fly.down>");
  assert(run.style("left") == "calc(max(12px, env(safe-area-inset-left)) + 68px + min(36vw, 30vh, 150px) + 14px)" && run.style("bottom") == "max(4vh, env(safe-area-inset-bottom))");
  assert(up.style("bottom") == "calc(max(4vh, env(safe-area-inset-bottom)) + 62px)" && down.style("bottom") == run.style("bottom"));
  assert(badge.style("opacity") == "0");
  stick.frame("walk");
  assert(!pad.hidden() && ui.hasClass("stickmode") && !run.hidden() && up.hidden() && down.hidden());
  stick.frame("fly");
  assert(!pad.hidden() && ui.hasClass("stickmode") && run.hidden() && !up.hidden() && !down.hidden());
  stick.frame(std::nullopt);
  assert(pad.hidden() && !ui.hasClass("stickmode") && run.hidden() && up.hidden() && down.hidden());

  test("the stick: pushed up it goes forwards, to the side it steps aside, let go it springs back");
  stick.frame("walk");
  dom::native::setRect(pad, dom::Rect{100, 400, 150, 150});   // (its middle at 175, 475; 75 px to the edge)
  int below = 0;
  ui.on("pointerdown", [&below] { below++; });
  assert(!send(pad, pointer("pointerdown", "touch", 5, 175, 400)));   // (the page does not scroll, and the view below is not turned)
  assert(below == 0 && pad.hasPointerCapture(5));
  assert(input.stick.x == 0 && input.stick.y == 1 && input.touch.active && input.mode == "touch");
  assert(knob.style("transform") == "translate(0px, -46.5px)");
  {
    const Input::Walk w = input.walkInput("classic");
    assert(w.fwd == 1 && w.side == 0);
  }
  send(pad, pointer("pointermove", "touch", 5, 212.5, 475));
  // (a little play in the middle – 12 % –, then all the way out a little before the edge – at 90 %)
  assert(near(input.stick.x, (0.5 - 0.12) / 0.78) && input.stick.y == 0);
  assert(knob.style("transform") == "translate(23.25px, 0px)");
  send(pad, pointer("pointermove", "touch", 5, 182.5, 475));
  assert(input.stick.x == 0 && input.stick.y == 0);
  send(pad, pointer("pointermove", "touch", 5, 175 - 67.5, 475));
  assert(near(input.stick.x, -1) && input.stick.y == 0);
  send(pad, pointer("pointermove", "touch", 5, 325, 325));   // (beyond the edge: the knob stays on it)
  assert(near(input.stick.x, std::sqrt(0.5)) && near(input.stick.y, std::sqrt(0.5)));
  assert(near(input.walkInput("classic").side, std::sqrt(0.5)));
  // (another finger is not the stick's)
  send(pad, pointer("pointerdown", "touch", 6, 175, 550));
  send(pad, pointer("pointermove", "touch", 6, 175, 550));
  send(pad, pointer("pointerup", "touch", 6, 175, 550));
  assert(near(input.stick.x, std::sqrt(0.5)) && near(input.stick.y, std::sqrt(0.5)));
  send(pad, pointer("pointerup", "touch", 5, 325, 325));
  assert(input.stick.x == 0 && input.stick.y == 0 && knob.style("transform").empty());
  send(pad, pointer("pointermove", "touch", 5, 175, 400));   // (no finger down: nothing moves)
  assert(input.stick.y == 0);
  send(pad, pointer("pointerdown", "touch", 7, 175, 550));
  assert(input.stick.y == -1);
  send(pad, pointer("pointercancel", "touch", 7, 175, 550));
  assert(input.stick.y == 0);
  // hidden while a finger is on it: let go
  send(pad, pointer("pointerdown", "touch", 8, 175, 400));
  assert(input.stick.y == 1);
  stick.frame(std::nullopt);
  assert(input.stick.y == 0 && knob.style("transform").empty());
  send(pad, pointer("pointermove", "touch", 8, 175, 400));
  assert(input.stick.y == 0);
  send(pad, pointer("pointerup", "touch", 8, 175, 400));

  test("the runner's button switches running on and off; the buttons for up and down are held");
  stick.frame("walk");
  assert(!run.hasClass("on"));
  input.untouched();
  assert(!send(run, pointer("pointerdown", "touch", 2, 300, 550)));
  assert(input.runToggle && input.touch.active);
  stick.frame("walk");
  assert(run.hasClass("on") && input.walkInput("classic").run);
  send(run, pointer("pointerdown", "touch", 2, 300, 550));
  stick.frame("walk");
  assert(!input.runToggle && !run.hasClass("on"));
  stick.frame("fly");
  input.flyCam = true;
  assert(!send(up, pointer("pointerdown", "touch", 3, 300, 480)) && up.hasPointerCapture(3));
  for (int i = 0; i < 30; i++) input.update(1.0 / 60, 0, 0);
  assert(near(input.takeCam().keyUp, 0.5));
  send(up, pointer("pointerup", "touch", 3, 300, 480));
  send(down, pointer("pointerdown", "touch", 3, 300, 550));
  for (int i = 0; i < 30; i++) input.update(1.0 / 60, 0, 0);
  assert(near(input.takeCam().keyUp, -0.5));
  send(down, pointer("pointercancel", "touch", 3, 300, 550));
  for (int i = 0; i < 30; i++) input.update(1.0 / 60, 0, 0);
  assert(input.takeCam().keyUp == 0 && input.mode == "touch");

  test("the badge says something for a moment");
  stick.badge("12 m/s");
  assert(dom::native::text(badge) == "12 m/s" && badge.style("opacity") == "1");
  plat::native::advance(1300);
  stick.frame("fly");
  assert(badge.style("opacity") == "1");
  plat::native::advance(200);
  stick.frame("fly");
  assert(badge.style("opacity") == "0");
  stick.badge("3 m/s", 100);
  plat::native::advance(150);
  stick.frame(std::nullopt);
  assert(dom::native::text(badge) == "3 m/s" && badge.style("opacity") == "0");
}

// --- ui/screentap ----------------------------------------------------------------------

/** a vehicle's screen with a picture in the scene (or none) */
class Screen : public VehicleScreen {
 public:
  std::shared_ptr<Mesh> mesh;
  Screen() : VehicleScreen(Canvas()) {}
  void drawn() override {}
  void setLive(bool) override {}
  Object3D* hitMesh() override { return mesh.get(); }
};

void screenTap() {
  test("a tap on the vehicle's screen opens the big map – or, as the settings choose, brings the corner map back");
  dom::El ui = page();
  dom::El canvas = dom::el("canvas");
  ui.append(canvas);
  dom::native::setRect(canvas, dom::Rect{0, 0, 800, 600});
  const std::shared_ptr<Camera> camera = perspectiveCamera(60, 800.0 / 600, 0.1, 100);
  camera->updateMatrixWorld();
  // the screen: 1 m × 0.5 m, 2 m ahead – on the picture about 260 × 130 px round the middle
  Screen screen;
  screen.mesh = make<Mesh>(planeGeometry(1, 0.5), basic());
  screen.mesh->position.set(0, 0, -2);
  screen.mesh->updateMatrixWorld();
  std::string mode = "bigmap";
  int opened = 0;
  ScreenTap tap(canvas, camera, [&mode] { return mode; }, [&opened] { opened++; });
  tapAt(canvas, 430, 310);
  assert(opened == 0 && !tap.covers(430, 310));   // (no screen named)
  tap.setScreen(&screen);
  assert(tap.covers(430, 310) && tap.covers(300, 250) && !tap.covers(560, 310) && !tap.covers(430, 400) && !tap.covers(50, 50));
  tapAt(canvas, 430, 310);
  assert(opened == 1 && !tap.peek);
  tapAt(canvas, 560, 310);   // (beside it)
  assert(opened == 1);
  mode = "minimap";
  tapAt(canvas, 430, 310);
  assert(opened == 1 && tap.peek);
  tapAt(canvas, 430, 310);
  assert(opened == 1 && !tap.peek);
  mode = "bigmap";
  // only a tap: let go soon and near where it came down, with the left button
  tap.setScreen(&screen);
  tapAt(canvas, 430, 310, 451);
  assert(opened == 1);
  tap.setScreen(&screen);
  send(canvas, pointer("pointerdown", "touch", 1, 430, 310));
  send(canvas, pointer("pointerup", "touch", 1, 443, 310));
  assert(opened == 1);
  send(canvas, pointer("pointerdown", "touch", 1, 430, 310));
  send(canvas, pointer("pointerup", "touch", 2, 430, 310));   // (another finger)
  assert(opened == 1);
  send(canvas, pointer("pointerup", "touch", 1, 440, 316));   // (11.7 px: still a tap)
  assert(opened == 2);
  send(canvas, pointer("pointerdown", "mouse", 1, 430, 310, 2));
  send(canvas, pointer("pointerup", "mouse", 1, 430, 310, 2));
  assert(opened == 2);
  send(canvas, pointer("pointerdown", "mouse", 1, 430, 310, 0));
  send(canvas, pointer("pointerup", "mouse", 1, 430, 310, 0));
  assert(opened == 3);
  // only a screen the game named a moment ago: not after the drive has ended
  plat::native::advance(500);
  assert(!tap.covers(430, 310));
  tapAt(canvas, 430, 310);
  assert(opened == 3);
  // a screen without a picture in the scene, and none at all
  Screen blank;
  tap.setScreen(&blank);
  tapAt(canvas, 430, 310);
  assert(opened == 3 && !tap.covers(430, 310));
  tap.setScreen(&screen);
  assert(tap.covers(430, 310));
  tap.setScreen(nullptr);
  tapAt(canvas, 430, 310);
  assert(opened == 3 && !tap.covers(430, 310));
}

// --- ui/worldtap -----------------------------------------------------------------------

/** the number in '123.5px' */
double px(const std::string& css) { return std::stod(css); }

void worldTap() {
  test("a tap on a place in the picture offers to teleport or to drive there");
  dom::El ui = page();
  dom::El canvas = dom::el("canvas");
  ui.append(canvas);
  dom::native::setRect(canvas, dom::Rect{0, 0, 800, 600});
  // the camera 10 m above flat ground, looking down at 45° to the north: the middle of the picture is 10 m ahead
  const std::shared_ptr<Camera> camera = perspectiveCamera(60, 800.0 / 600, 0.1, 4500);
  camera->position.set(0, 10, 0);
  camera->lookAt(0, 0, -10);
  camera->updateMatrixWorld();
  bool allowed = true, canDrive = true, claimedDown = false, claimedUp = false, ground = true;
  std::vector<GroundPoint> tapped, teleported, driven;
  std::vector<double> yaws;
  std::string lang = "en";
  WorldTapHost host;
  host.canvas = canvas;
  host.camera = camera;
  host.ui = ui;
  host.heightAt = [&ground](double, double) { return ground ? 0.0 : JS_NAN; };
  host.allowed = [&allowed] { return allowed; };
  host.canDrive = [&canDrive] { return canDrive; };
  host.claimed = [&claimedDown, &claimedUp](double, double, bool down) { return down ? claimedDown : claimedUp; };
  host.tapped = [&tapped](const GroundPoint& p) { tapped.push_back(p); };
  host.teleport = [&teleported, &yaws](const GroundPoint& p, double yaw) {
    teleported.push_back(p);
    yaws.push_back(yaw);
  };
  host.drive = [&driven](const GroundPoint& p) { driven.push_back(p); };
  host.t = [&lang](const std::string& key) { return lang + ":" + key; };
  WorldTap tap(host);
  const dom::El bar = ui.querySelector(".world-tap");
  assert(bar && bar.childElementCount() == 2 && bar.style("display") == "none" && bar.style("z-index") == "7" && bar.style("touch-action") == "manipulation");
  dom::native::setRect(bar, dom::Rect{0, 0, 200, 44});
  const dom::El ring = bar.previousElementSibling();
  const dom::El teleportBtn = bar.child(0), driveBtn = bar.child(1);
  assert(ring && ring.style("display") == "none" && ring.style("pointer-events") == "none" && ring.style("border-radius") == "50%");
  assert(dom::native::text(teleportBtn) == "⤓ en:teleportHere" && teleportBtn.get("title") == "en:teleportHere" && teleportBtn.getAttribute("aria-label") == "en:teleportHere");
  assert(dom::native::text(driveBtn) == "🚗 en:tapDrive" && driveBtn.get("type") == "button" && driveBtn.style("min-height") == "44px");
  tap.frame();
  assert(bar.style("display") == "none");

  lang = "de";
  tapAt(canvas, 400, 300);
  assert(tapped.size() == 1 && near(tapped[0].x, 0, 1e-3) && tapped[0].y == 0 && near(tapped[0].z, -10, 1e-3));
  assert(bar.style("display") == "flex" && ring.style("display") == "block");
  assert(teleportBtn.style("display") == "flex" && driveBtn.style("display") == "flex");
  // (the ring at the place, the bar with it – and the words of the language in use now)
  assert(near(px(ring.style("left")), 400, 0.01) && near(px(ring.style("top")), 300, 0.01) && near(px(bar.style("left")), 400, 0.01) && near(px(bar.style("top")), 300, 0.01));
  assert(dom::native::text(teleportBtn) == "⤓ de:teleportHere" && driveBtn.get("title") == "de:tapDrive");

  test("the buttons do what they offer, once, where it is allowed");
  int below = 0;
  ui.on("click", [&below] { below++; });
  teleportBtn.click();
  assert(teleported.size() == 1 && near(teleported[0].z, -10, 1e-3) && near(yaws[0], 0) && driven.empty() && below == 0);
  assert(bar.style("display") == "none" && ring.style("display") == "none");
  teleportBtn.click();
  driveBtn.click();
  assert(teleported.size() == 1 && driven.empty());   // (nothing is offered any more)
  // a place to the right of the middle, further away
  tapAt(canvas, 600, 200);
  assert(tapped.size() == 2 && tapped[1].x > 3 && tapped[1].z < -12);
  driveBtn.click();
  assert(driven.size() == 1 && driven[0].x == tapped[1].x && driven[0].z == tapped[1].z && teleported.size() == 1);
  tapAt(canvas, 400, 300);
  allowed = false;   // (it was allowed when it was offered, and is not any more)
  teleportBtn.click();
  assert(teleported.size() == 1);
  // the camera looked to the east: that way a lane is chosen
  allowed = true;
  camera->lookAt(10, 0, 0);
  camera->updateMatrixWorld();
  tapAt(canvas, 400, 300);
  teleportBtn.click();
  assert(teleported.size() == 2 && near(teleported[1].x, 10, 1e-3) && near(teleported[1].z, 0, 1e-3) && near(yaws[1], -std::numbers::pi / 2));
  camera->lookAt(0, 0, -10);
  camera->updateMatrixWorld();

  test("each button only where it may be used; neither: the place is told (tap to walk), nothing is offered");
  canDrive = false;
  tapAt(canvas, 400, 300);
  assert(bar.style("display") == "flex" && teleportBtn.style("display") == "flex" && driveBtn.style("display") == "none");
  canDrive = true;
  allowed = false;
  tap.frame();
  assert(teleportBtn.style("display") == "none" && driveBtn.style("display") == "flex");
  canDrive = false;
  tap.frame();
  assert(bar.style("display") == "none");
  canDrive = true;
  tap.frame();
  assert(bar.style("display") == "none");   // (gone is gone)
  canDrive = false;
  size_t n = tapped.size();
  tapAt(canvas, 400, 300);
  assert(tapped.size() == n + 1 && bar.style("display") == "none");
  allowed = canDrive = true;

  test("the bar follows its place, keeps clear of the edges, and goes after a few seconds or at the next touch");
  tapAt(canvas, 400, 300);
  camera->lookAt(0, 0, -20);   // (the camera looks further ahead: the place is lower in the picture)
  camera->updateMatrixWorld();
  tap.frame();
  assert(near(px(ring.style("left")), 400, 0.01) && px(ring.style("top")) > 400 && px(bar.style("top")) == px(ring.style("top")));
  camera->lookAt(-8, 0, -7);   // (the place near the right edge of the picture: the bar stays within it – half its width and 8 px)
  camera->updateMatrixWorld();
  tap.frame();
  assert(px(ring.style("left")) > 700 && px(ring.style("left")) < 800 && px(bar.style("left")) == 800 - 108 && bar.style("display") == "flex");
  camera->lookAt(0, 0, 10);   // (behind the camera: not shown until it is back)
  camera->updateMatrixWorld();
  tap.frame();
  assert(bar.style("display") == "none" && ring.style("display") == "none");
  camera->lookAt(0, 0, -3);   // (the camera looks down before its feet: the place is near the upper edge, the bar stays 70 px below it)
  camera->updateMatrixWorld();
  tap.frame();
  assert(bar.style("display") == "flex" && px(ring.style("top")) > 0 && px(ring.style("top")) < 40 && px(bar.style("top")) == 70);
  camera->lookAt(0, 0, -10);
  camera->updateMatrixWorld();
  tap.frame();
  assert(bar.style("display") == "flex" && ring.style("display") == "block" && near(px(ring.style("top")), 300, 0.01));
  plat::native::advance(4400);
  tap.frame();
  assert(bar.style("display") == "flex");
  plat::native::advance(200);
  tap.frame();
  assert(bar.style("display") == "none");
  tapAt(canvas, 400, 300);
  assert(bar.style("display") == "flex");
  send(canvas, pointer("pointerdown", "touch", 1, 100, 100));   // (another touch elsewhere: the button offered before goes)
  assert(bar.style("display") == "none");
  send(canvas, pointer("pointercancel", "touch", 1, 100, 100));
  assert(bar.style("display") == "none");
  tapAt(canvas, 400, 300);
  tap.hide();
  assert(bar.style("display") == "none" && ring.style("display") == "none");
  teleportBtn.click();
  assert(teleported.size() == 2);

  test("only a tap counts – and not one that is somebody else's");
  n = tapped.size();
  tapAt(canvas, 400, 300, 451);   // (held too long)
  send(canvas, pointer("pointerdown", "touch", 1, 400, 300));
  send(canvas, pointer("pointerup", "touch", 1, 400, 311));   // (a drag that looks round)
  send(canvas, pointer("pointerdown", "mouse", 1, 400, 300, 2));   // (the right button)
  send(canvas, pointer("pointerup", "mouse", 1, 400, 300, 2));
  send(canvas, pointer("pointerdown", "touch", 1, 400, 300));   // (two fingers: a pinch)
  send(canvas, pointer("pointerdown", "touch", 2, 500, 300));
  send(canvas, pointer("pointerup", "touch", 2, 500, 300));
  send(canvas, pointer("pointerup", "touch", 1, 400, 300));
  claimedDown = true;   // (a place for a car is being picked)
  tapAt(canvas, 400, 300);
  claimedDown = false;
  claimedUp = true;     // (the vehicle's screen, a parked car)
  tapAt(canvas, 400, 300);
  claimedUp = false;
  ground = false;       // (the ground is not loaded there)
  tapAt(canvas, 400, 300);
  ground = true;
  dom::native::setRect(canvas, dom::Rect{0, 0, 0, 0});   // (no picture)
  tapAt(canvas, 400, 300);
  dom::native::setRect(canvas, dom::Rect{0, 0, 800, 600});
  send(ui, pointer("pointerdown", "touch", 1, 400, 300));   // (on the HUD, not on the picture)
  send(ui, pointer("pointerup", "touch", 1, 400, 300));
  assert(tapped.size() == n && bar.style("display") == "none");
  send(canvas, pointer("pointerdown", "mouse", 1, 400, 300, 0));   // (a click is a tap; let go over the HUD it still counts: the window hears it)
  send(ui, pointer("pointerup", "mouse", 1, 404, 309, 0));
  assert(tapped.size() == n + 1 && bar.style("display") == "flex");
  int reached = 0;
  ui.on("pointerdown", [&reached] { reached++; });
  send(teleportBtn, pointer("pointerdown", "touch", 1, 400, 280));   // (a finger on the button is the button's)
  assert(reached == 0 && bar.style("display") == "flex");
}

}  // namespace

int main() {
  tiltArithmetic();
  tiltSteer();
  touchStick();
  screenTap();
  worldTap();
  std::printf("touch_test: ok\n");
  return 0;
}

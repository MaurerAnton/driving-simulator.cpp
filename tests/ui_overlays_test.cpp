// SPDX-License-Identifier: AGPL-3.0-only
// LINK: ui render engine game input sim world platform core i18n geo audio
/**
 * The four overlays that hang over the driving picture (game/game_frame.cpp:616):
 * the timers over the traffic lights (ui/signaltimers), the labels over the
 * queues in the traffic (ui/queuelabels), the inside rear-view mirror where
 * the player put it (ui/hudmirror) and looking into one of the vehicle's
 * mirrors large (ui/mirrorzoom).
 *
 * The original has no test file of its own for any of them (their cases are
 * part of tests/game/cockpit.test.ts and tests/sim/signals-timer.test.ts,
 * mirrored in game_cockpit_test.cpp and sim_test.cpp): these are made here,
 * and ask after what each overlay is for – the seconds a light still shows
 * what it shows and the colour that comes next, how long a queue is and how
 * long its last car waits, and where a mirror's picture is drawn.
 */
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "engine/renderer.h"
#include "game/settings.h"
#include "input/input.h"
#include "platform/dom.h"
#include "platform/native/dom_native.h"
#include "platform/glhost.h"
#include "render/mirrors.h"
#include "sim/queues.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "tests/sim_roads.h"
#include "tests/vehicle_harness.h"
#include "ui/hudmirror.h"
#include "ui/mirrorzoom.h"
#include "ui/queuelabels.h"
#include "ui/signaltimers.h"

using namespace fs;
using namespace fs::harness;

namespace nd = fs::dom::native;

namespace {

/** The picture: 1280 × 720 px, as the game has it on a laptop. */
const int W = 1280;
const int H = 720;

/** A pointer at (x, y) of the window. */
dom::Event pointer(const std::string& type, double x, double y, int id = 1) {
  dom::Event e;
  e.type = type;
  e.pointerId = id;
  e.pointerType = "touch";
  e.clientX = x;
  e.clientY = y;
  return e;
}

/** A tap or a stroke of the pointer on `el`. */
void tap(const dom::El& el, const std::string& type, double x, double y, int id = 1) {
  dom::Event e = pointer(type, x, y, id);
  nd::dispatch(el, e);
}

/** The elements of `layer` whose display is not none: the labels put up this frame. */
std::vector<dom::El> shown(const dom::El& layer, const std::string& selector) {
  std::vector<dom::El> out;
  for (const dom::El& e : layer.querySelectorAll(selector))
    if (e.style("display") != "none") out.push_back(e);
  return out;
}

// ---------------------------------------------------------------------------------------------
// the timers over the traffic lights

/** one stream of a junction: 20 s red, 1 s red-amber, 30 s green, 3 s amber */
SignalControllerJson oneStream() {
  SignalControllerJson c;
  c.groups = 1;
  c.phases = {{20, "R"}, {1, "Y"}, {30, "G"}, {3, "A"}};
  return c;
}

/** the pedestrians' light of a crossing: green for the vehicles until somebody presses */
SignalControllerJson crossing() {
  SignalControllerJson c;
  c.groups = 1;
  c.ped = true;
  c.phases = {{48, "G"}, {3, "A"}, {13, "R"}, {1, "Y"}};
  return c;
}

/** the camera of the driving picture: 18 m south of the origin, eye level */
std::shared_ptr<Camera> driveCamera() {
  std::shared_ptr<Camera> cam = perspectiveCamera(50, static_cast<double>(W) / H, 0.1, 2000);
  cam->position.set(0, 1.5, 18);
  cam->lookAt(0, 3.4, 0);
  cam->updateMatrixWorld();
  return cam;
}

void signalTimers() {
  test("the timers over the traffic lights: the seconds left, the colour that comes next, and a tap");
  nd::reset();
  const dom::El parent = dom::body();
  const Sparse<SignalControllerJson> list = roads::controllers({oneStream()});
  SignalSystem s(list);
  SignalTimers timers(parent);
  const dom::El layer = parent.querySelector(".sigtimes");
  ok(!!layer, "the layer the labels live in");
  // a head over the road 18 m ahead of the camera, its lamps facing it (south)
  std::vector<TimerHead> heads = {TimerHead{0, 3.2, 0, M_PI, 0, 0, nullptr}};
  std::shared_ptr<Camera> cam = driveCamera();
  timers.update(*cam, W, H, 1, heads, nullptr, s);
  std::vector<dom::El> labels = shown(layer, ".sigt");
  ok(labels.size() == 1, jsStr(labels.size()) + " label for one head");
  const dom::El& l = labels[0];
  ok(l.className() == "sigt main", "its class: " + l.className());
  const std::vector<dom::El> parts = l.children();
  ok(parts.size() == 2, "the dot and the number");
  // red for another 20 s, then red-amber
  ok(parts[1].textContent() == "20", "the seconds: " + parts[1].textContent());
  ok(parts[0].className() == "ra", "the colour that comes next: " + parts[0].className());
  ok(l.style("transform").find("translate(") == 0, "put in place: " + l.style("transform"));

  // a tap on the label puts the lights forward: through their least red first
  int tapped = -1;
  timers.onTap = [&](int controller) { tapped = controller; };
  tap(l, "pointerdown", 640, 300);
  ok(tapped == 0, "the tap names the controller: " + jsStr(tapped));
  s.skip(0);
  s.update(0);
  timers.update(*cam, W, H, 1, heads, nullptr, s);
  ok(parts[1].textContent() == "6", "a red that has hardly begun: " + parts[1].textContent() + " s more of it");
  ok(parts[0].className() == "ra", "still red-amber next");
  for (double t = 0; t < 6.1; t += 0.05) s.update(0.05);
  timers.update(*cam, W, H, 1, heads, nullptr, s);
  ok(parts[1].textContent() == "1", "red-amber: " + parts[1].textContent() + " s");
  ok(parts[0].className() == "g", "green next: " + parts[0].className());

  // a light that is out of order has no timer
  SignalSystem dark(list, [](int) { return true; });
  timers.update(*cam, W, H, 1, heads, nullptr, dark);
  ok(shown(layer, ".sigt").empty(), "out of order: no label");

  // on foot: the pedestrians' lights instead – nothing to count while they rest on the vehicles' green
  const Sparse<SignalControllerJson> pedList = roads::controllers({crossing()});
  SignalSystem peds(pedList);
  SignalTimers pedTimers(dom::body());
  const dom::El pedLayer = dom::body().querySelector(".sigtimes");
  std::vector<TimerHead> poles = {TimerHead{0, 2.6, 0, M_PI, 0, 0, nullptr}};
  pedTimers.update(*cam, W, H, 1, heads, &poles, peds);
  std::vector<dom::El> pedLabels = shown(pedLayer, ".sigt");
  ok(pedLabels.size() == 1, jsStr(pedLabels.size()) + " label for the pedestrians' light");
  const std::vector<dom::El> pedParts = pedLabels[0].children();
  ok(pedLabels[0].className() == "sigt ped", "the pedestrians' kind: " + pedLabels[0].className());
  ok(pedParts[1].textContent() == "–", "no time while it waits for the button: " + pedParts[1].textContent());
  ok(pedParts[0].className() == "g", "green for them next: " + pedParts[0].className());
  peds.request(0);
  peds.update(0.05);
  pedTimers.update(*cam, W, H, 1, heads, &poles, peds);
  ok(pedParts[1].textContent() != "–", "asked: green in " + pedParts[1].textContent() + " s");
  ok(pedParts[0].className() == "g", "green next still: " + pedParts[0].className());
  // their green has come and is running out: the vehicles get moving again, the dot turns red
  while (peds.get(0, 0) != SIG_RED) peds.update(0.05);
  for (double t = 0; t < 2; t += 0.05) peds.update(0.05);
  pedTimers.update(*cam, W, H, 1, heads, &poles, peds);
  ok(pedParts[0].className() == "r", "their time is up: " + pedParts[0].className());

  // another state of the game: nothing at all
  timers.update(*cam, W, H, 1, heads, nullptr, s);
  ok(!shown(layer, ".sigt").empty(), "a label again");
  timers.hide();
  ok(shown(layer, ".sigt").empty(), "hidden");
}

// ---------------------------------------------------------------------------------------------
// the labels over the queues in the traffic

void queueLabels() {
  test("the labels over the queues: how long it is, how many cars stand in it, how long the last one waits");
  Queue long_;
  long_.length = 118;
  long_.cars = 18;
  long_.wait = 44;
  ok(QueueLabels::text(long_, "en") == "120 m · 18 cars · wait about 45 s", QueueLabels::text(long_, "en"));
  ok(QueueLabels::text(long_, "de") == "120 m · 18 Autos · etwa 45 s warten", QueueLabels::text(long_, "de"));
  Queue three;
  three.length = 197;
  three.cars = 3;
  ok(QueueLabels::text(three, "en") == "195 m · 3 cars", QueueLabels::text(three, "en"));
  Queue waits;
  waits.length = 61;
  waits.cars = 7;
  waits.wait = 125;
  const std::string said = QueueLabels::text(waits, "en");
  ok(said == "60 m · 7 cars · wait about 2 min", said);
  // no light at its head: the wait cannot be told
  Queue nowhere;
  nowhere.length = 33;
  nowhere.cars = 4;
  ok(QueueLabels::text(nowhere, "en") == "35 m · 4 cars", QueueLabels::text(nowhere, "en"));

  test("the label over a queue of four cars standing in the traffic");
  nd::reset();
  const dom::El parent = dom::body();
  roads::Net net = roads::straight();
  const Sparse<SignalControllerJson> noLights;
  SignalSystem signals(noLights);
  Traffic traffic(*net.world, *net.lanes, signals);
  traffic.targetCount = 0;
  std::vector<TrafficCar*> cars;
  for (int i = 0; i < 4; i++) {
    TrafficCar* c = traffic.addCar(*net.lanes->edgeLanes[0]->fwd[0], 40 + i * 9);
    c->crashed = 1e6;
    c->v = 0;
    cars.push_back(c);
  }
  QueueLabels labels(parent);
  const dom::El layer = parent.querySelector(".queuelabels");
  ok(!!layer, "the layer the labels live in");
  // behind the queue, a little above it, looking along the road
  // the player's car behind them (they were added up the road one after the other: the last one is the head)
  TrafficCar* back = cars.front();
  std::shared_ptr<Camera> cam = perspectiveCamera(50, static_cast<double>(W) / H, 0.1, 2000);
  cam->position.set(back->x - 30, back->y + 6, back->z);
  cam->lookAt(cars.back()->x, cars.back()->y + 1, cars.back()->z);
  cam->updateMatrixWorld();
  labels.update(*cam, W, H, 1, traffic.cars, nullptr, "en");
  std::vector<dom::El> put = shown(layer, "div");
  ok(put.size() == 1, jsStr(put.size()) + " label for one queue");
  ok(put[0].textContent() == "30 m · 4 cars", put[0].textContent());
  ok(put[0].style("transform").find("translate(") == 0, "put in place: " + put[0].style("transform"));
  labels.hide();
  ok(shown(layer, "div").empty(), "hidden");
}

// ---------------------------------------------------------------------------------------------
// the mirrors

/** The vehicle's mirrors: left, right and inside, each hanging in a group as in the car. */
struct Mirrors {
  std::vector<std::unique_ptr<Mirror>> list;
  std::shared_ptr<Group> car = make<Group>();
  explicit Mirrors(int howMany = 3) {
    const Vector3 eye(0, 1.2, 0.6);
    auto add = [&](const MirrorSpec& s) {
      list.push_back(std::make_unique<Mirror>(s));
      car->add(list.back()->mesh);
    };
    if (howMany >= 2) {
      for (const int side : {-1, 1})
        add(MirrorSpec{
            .name = side < 0 ? "left" : "right",
            .centre = Vector3(0.8 * side, 1.05, 0.35),
            .width = 0.18,
            .height = 0.11,
            .normal = mirrorNormal(Vector3(0.8 * side, 1.05, 0.35), eye, Vector3(0, -0.1, 1)),
            .convex = 1.35,
            .resolution = {256, 160},
        });
    }
    if (howMany >= 3)
      add(MirrorSpec{
          .name = "inside",
          .centre = Vector3(0.17, 1.32, 0.5),
          .width = 0.15,
          .height = 0.042,
          .normal = mirrorNormal(Vector3(0.17, 1.32, 0.5), eye, Vector3(0, -0.03, 1)),
          .convex = 1.5,
          .resolution = {512, 160},
      });
    car->updateMatrixWorld(true);
  }
};

/** Where a mirror's glass is on the picture (px of the window), as ui/mirrorzoom.cpp asks. */
bool glassAt(const Mirror& m, const Camera& cam, const dom::Rect& r, double& outX, double& outY) {
  const AttrPtr pos = m.mesh->geometry->getAttribute("position");
  if (!pos) return false;
  Vector3 p;
  double x0 = JS_INF, y0 = JS_INF, x1 = -JS_INF, y1 = -JS_INF;
  for (size_t i = 0; i < pos->count(); i++) {
    p.fromBufferAttribute(*pos, i).applyMatrix4(m.mesh->matrixWorld).project(cam);
    if (p.z > 1 || p.z < -1) return false;
    x0 = std::min(x0, r.left + ((p.x + 1) / 2) * r.width);
    x1 = std::max(x1, r.left + ((p.x + 1) / 2) * r.width);
    y0 = std::min(y0, r.top + ((1 - p.y) / 2) * r.height);
    y1 = std::max(y1, r.top + ((1 - p.y) / 2) * r.height);
  }
  outX = (x0 + x1) / 2;
  outY = (y0 + y1) / 2;
  return true;
}

void hudMirror(Settings& settings, Renderer& renderer, Scene& scene, const std::shared_ptr<Camera>& cam) {
  test("the inside rear-view mirror: on its rail in the car, and its picture in a frame of the HUD");
  nd::reset();
  const dom::El canvas = dom::el("canvas");
  const dom::El ui = dom::el("div");
  const dom::El hud = dom::el("div");
  hud.className("hud");
  ui.append(hud);
  nd::setRect(canvas, dom::Rect{0, 0, static_cast<double>(W), static_cast<double>(H)});
  Mirrors car;
  Mirror* inside = car.list.back().get();
  settings.insideMirror.mode = "car";
  settings.insideMirror.shift = 0.22;
  settings.insideMirror.size = 1.4;
  HudMirror mirror(ui, MirrorHost{renderer, scene, *cam, canvas}, settings);
  const dom::El frameEl = ui.querySelector(".hudmirror");
  ok(!!frameEl, "the frame over the picture");
  HudMirrorView view;
  view.mirrors = &car.list;
  view.cockpit = true;
  view.eye = Vector3(0, 1.2, 0.6);
  view.eyeLocal = Vector3(0, 1.2, 0.6);
  view.far = 800;
  view.lang = "en";
  // in the car: the glass moves along the top of the windscreen and grows
  const Vector3 before = inside->spec.centre;
  mirror.frame(view);
  ok(frameEl.hidden(), "no frame in the HUD while the glass is in the car");
  ok(inside->mesh->visible, "the glass is drawn in the car");
  ok(std::abs(inside->spec.centre.x - (before.x + 0.22)) < 1e-9, "on its rail: " + jsStr(inside->spec.centre.x));
  ok(std::abs(inside->spec.width - 0.15 * 1.4) < 1e-9, "larger: " + jsStr(inside->spec.width));
  ok(std::abs(inside->spec.height - 0.042 * 1.4) < 1e-9, "and taller: " + jsStr(inside->spec.height));

  // in the HUD: the glass is taken out of the car and put into a frame instead
  settings.insideMirror.mode = "hud";
  settings.insideMirror.size = 1;
  settings.insideMirror.fx = 0.5;
  settings.insideMirror.fy = 0.09;
  mirror.frame(view);
  ok(!inside->mesh->visible, "the glass in the car is hidden");
  // its middle at (0.5, 0.09) of the picture, 230 px wide and 1 : 3.2
  ok(frameEl.style("left") == "522px", "left " + frameEl.style("left"));
  ok(frameEl.style("top") == "26px", "top " + frameEl.style("top"));
  ok(frameEl.style("width") == "230px", "width " + frameEl.style("width"));
  ok(frameEl.style("height") == "72px", "height " + frameEl.style("height"));
  ok(frameEl.get("title") == "Inside mirror – drag to move it", "its tooltip: " + frameEl.get("title"));
  if (renderer.available()) ok(!frameEl.hidden(), "the picture is drawn: the frame is shown");

  // dragged where the player wants it
  const dom::El grip = frameEl.querySelector(".hm-grip");
  ok(!!grip, "the grip in the corner");
  nd::setRect(frameEl, dom::Rect{525, 29, 230, 72});
  tap(frameEl, "pointerdown", 600, 60);
  dom::Event move = pointer("pointermove", 700, 80);
  nd::dispatch(frameEl, move);
  dom::Event up = pointer("pointerup", 700, 80);
  nd::dispatch(frameEl, up);
  ok(std::abs(settings.insideMirror.fx - (0.5 + 100.0 / W)) < 1e-9, "moved across: " + jsStr(settings.insideMirror.fx));
  ok(std::abs(settings.insideMirror.fy - (0.09 + 20.0 / H)) < 1e-9, "and down: " + jsStr(settings.insideMirror.fy));
  ok(std::abs(settings.insideMirror.size - 1) < 1e-9, "not resized: " + jsStr(settings.insideMirror.size));
  // by the grip: larger or smaller
  tap(grip, "pointerdown", 750, 95, 2);
  move = pointer("pointermove", 770, 95, 2);
  nd::dispatch(grip, move);
  tap(frameEl, "pointerup", 770, 95, 2);
  // (20 px out on the grip is 20 px more on each side of the frame)
  ok(std::abs(settings.insideMirror.size - std::min(2.0, (230.0 + 2 * 20) / 230.0)) < 1e-9,
     "by the grip: 20 px on each side – " + jsStr(settings.insideMirror.size));
  // larger than twice, or smaller than 0.6, it does not get
  tap(grip, "pointerdown", 750, 95, 3);
  tap(frameEl, "pointermove", 1500, 95, 3);
  tap(frameEl, "pointerup", 1500, 95, 3);
  ok(settings.insideMirror.size == 2, "at most twice: " + jsStr(settings.insideMirror.size));
  tap(grip, "pointerdown", 750, 95, 4);
  tap(frameEl, "pointermove", 300, 95, 4);
  tap(frameEl, "pointerup", 300, 95, 4);
  ok(settings.insideMirror.size == 0.6, "at least 0.6: " + jsStr(settings.insideMirror.size));

  // out of the driver's seat (or in a menu) there is no frame at all
  view.cockpit = false;
  mirror.frame(view);
  ok(frameEl.hidden(), "no frame outside the driver's seat");
  // and with the mirrors switched off, the glass stays in the car again
  settings.mirrors = "off";
  view.cockpit = true;
  mirror.frame(view);
  ok(frameEl.hidden(), "no picture without mirrors");
  ok(inside->mesh->visible, "the glass back in the car");
}

void mirrorZoomPanel(Settings& settings, Renderer& renderer, Scene& scene, const std::shared_ptr<Camera>& cam) {
  test("looking into one of the vehicle's mirrors: the button, the key, the panel, the × and a tap on the glass");
  nd::reset();
  const dom::El canvas = dom::el("canvas");
  const dom::El ui = dom::el("div");
  const dom::El hud = dom::el("div");
  hud.className("hud");
  ui.append(hud);
  nd::setRect(canvas, dom::Rect{0, 0, static_cast<double>(W), static_cast<double>(H)});
  const dom::El body = dom::body();
  Input input(body, settings);
  MirrorZoomPanel panel(ui, input, MirrorHost{renderer, scene, *cam, canvas});
  const dom::El frameEl = ui.querySelector(".mirrorzoom");
  const dom::El button = ui.querySelector(".mirrorbtn");
  const dom::El label = frameEl.querySelector(".mz-label");
  const dom::El close = frameEl.querySelector(".mz-close");
  ok(!!frameEl && !!button && !!label && !!close, "the panel, the button and the ×");
  ok(button.hidden(), "no mirrors, no button");
  ok(frameEl.hidden(), "and no panel");

  Mirrors car;
  MirrorView view;
  view.mirrors = &car.list;
  view.cockpit = true;
  view.eye = Vector3(0, 1.2, 0.6);
  view.far = 800;
  view.lang = "en";
  panel.frame(view);
  ok(!button.hidden(), "the button once the vehicle has mirrors");
  ok(frameEl.hidden(), "and still no panel");
  ok(button.get("title") == "Look into a mirror", "its tooltip: " + button.get("title"));

  // the mirror key goes round them: left → right → inside → none
  const char* const want[4] = {"Left mirror", "Right mirror", "Rear-view mirror", ""};
  for (int i = 0; i < 4; i++) {
    nd::key("KeyY");
    panel.frame(view);
    const bool none = want[i][0] == '\0';
    if (none) {
      ok(frameEl.hidden(), "after the last one: put away");
      ok(!button.hasClass("on"), "the button is off again");
      break;
    }
    if (renderer.available()) ok(!frameEl.hidden(), "the panel with " + std::string(want[i]));
    ok(label.textContent() == want[i], "its name: " + label.textContent());
    ok(button.hasClass("on"), "the button shows that one is looked into");
    // the shape of the panel: as large as the upper half of the picture allows
    const double w = jsNumber(frameEl.style("width").substr(0, frameEl.style("width").size() - 2), false);
    const double h = jsNumber(frameEl.style("height").substr(0, frameEl.style("height").size() - 2), false);
    ok(w > 100 && w <= W * 0.94 + 0.5, "as wide as it may be: " + jsStr(w));
    ok(h > 50 && h <= H * 0.5 + 8.5, "as high as it may be: " + jsStr(h));
    ok(std::abs(w / h - zoomAspect(car.list[static_cast<size_t>(i)]->spec.width, car.list[static_cast<size_t>(i)]->spec.height)) < 0.02,
       "in the shape of the glass: " + jsStr(w / h));
  }

  // the round button goes round them as the key does
  tap(button, "pointerdown", 20, 20);
  panel.frame(view);
  ok(label.textContent() == "Left mirror", "the button: " + label.textContent());
  // the × puts it away
  tap(close, "pointerdown", 640, 100);
  panel.frame(view);
  ok(frameEl.hidden(), "put away with the ×");

  // a tap on a mirror in the driver's seat shows that one
  const dom::Rect r{0, 0, static_cast<double>(W), static_cast<double>(H)};
  double x = 0, y = 0;
  ok(glassAt(*car.list[0], *cam, r, x, y), "the left mirror is in the picture");
  tap(canvas, "pointerdown", x, y, 5);
  tap(canvas, "pointerup", x, y, 5);
  panel.frame(view);
  ok(label.textContent() == "Left mirror", "a tap on the left glass: " + label.textContent());
  // beside it is none: the panel is put away
  tap(canvas, "pointerdown", x, y, 6);
  tap(canvas, "pointerup", x, y, 6);
  panel.frame(view);
  ok(frameEl.hidden(), "a second tap on the same glass puts it away");

  // a tap where there is no mirror changes nothing
  tap(canvas, "pointerdown", 5, 700, 7);
  tap(canvas, "pointerup", 5, 700, 7);
  panel.frame(view);
  ok(frameEl.hidden(), "nothing tapped: nothing shown");

  // the vehicle changed: a mirror it does not have is shown no more
  nd::key("KeyY");
  panel.frame(view);
  if (renderer.available()) ok(!frameEl.hidden(), "the left mirror shown");
  Mirrors truck(2);
  view.mirrors = &truck.list;
  panel.frame(view);
  if (renderer.available()) ok(!frameEl.hidden(), "the truck has a left mirror too");
  view.mirrors = nullptr;
  panel.frame(view);
  ok(frameEl.hidden(), "nothing to look into: the panel is put away");
  ok(button.hidden(), "and the button goes");
}

}  // namespace

int main() {
  signalTimers();
  queueLabels();
  // (the two mirrors draw into the picture: without OpenGL ES here there is nothing to draw into)
  const bool gl = plat::gl::createContext("ui_overlays_test", W, H);
  if (!gl) {
    std::printf("(no OpenGL ES here: the two mirror overlays are not drawn)\n");
    return 0;
  }
  Renderer renderer;
  renderer.init();
  renderer.setSize(W, H);
  Scene scene;
  std::shared_ptr<Camera> cam = perspectiveCamera(50, static_cast<double>(W) / H, 0.1, 2000);
  cam->position.set(0, 1.2, 2.2);
  cam->lookAt(0.17, 1.25, 0.45);
  cam->updateMatrixWorld();
  Settings settings;
  hudMirror(settings, renderer, scene, cam);
  mirrorZoomPanel(settings, renderer, scene, cam);
  std::printf("ok\n");
  return 0;
}

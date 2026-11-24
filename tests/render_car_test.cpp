// LINK: render engine sim world game core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The cars that are drawn (src/render/carBody, car, instruments, mirrors, navscreen):
 *
 *  - the model builds: its parts, their number and their sizes; which mirrors there are; the
 *    lamps, the wheels, the levers and the pedals follow the state; the see-through cabin;
 *  - the finer model and the parts that open: nothing is made while they are off and shut, and the
 *    car's flow runs through them once they are there;
 *  - looking into a mirror: the last case of the original's tests/game/cockpit.test.ts (the others
 *    there are the game's);
 *  - everything, number by number, against the original: tools/diff/render_cars.ts --part car
 *    prints what the original's src/render at v1.7.7 makes (tests/fixtures/render-car.txt), this prints the same.
 *
 * Nothing is drawn here: the canvases of the native host count what they are asked.
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "render/car.h"
#include "render/carBody.h"
#include "render/instruments.h"
#include "render/mirrors.h"
#include "render/navscreen.h"
#include "tests/render_dump.h"

using namespace fs;
using fs::dump::out;
using fs::rdump::P;
using fs::rdump::P3;
using fs::rdump::word;

namespace {

CarDims dimsWith(double length, double height, double wheelbase, int tail) {
  CarDims d = COMPACT_DIMS;
  d.length = length;
  d.height = height;
  d.wheelbase = wheelbase;
  d.tail = tail;
  return d;
}
/** the three bodies of the traffic's cars (src/render/actors.ts STYLES) */
const CarDims ESTATE = dimsWith(4.6, 1.5, 2.7, 1);
const CarDims SALOON = dimsWith(4.45, 1.42, 2.68, 2);
const CarDims* const STYLES[] = {&COMPACT_DIMS, &ESTATE, &SALOON};

/** a car standing with its engine running, as the script's `state()` */
CarVisualState state() {
  CarVisualState s;
  s.rpm = 800;
  s.ignition = true;
  s.engineRunning = true;
  s.seatbelt = true;
  s.fuel = 0.6;
  s.wheelSteer = {0, 0, 0, 0};
  s.wheelSpin = {0, 0, 0, 0};
  s.wheelY = {0.316, 0.316, 0.316, 0.316};
  return s;
}

Box3 boxOf(const BufferGeometry& g) {
  Box3 box;
  const AttrPtr p = g.getAttribute("position");
  for (size_t i = 0; i < p->count(); i++) box.expandByPoint(Vector3(p->getX(i), p->getY(i), p->getZ(i)));
  return box;
}

bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

Object3D* find(Object3D& top, const std::string& name) { return top.getObjectByName(name); }

int countMeshes(Object3D& top) {
  int n = 0;
  top.traverse([&n](Object3D& o) {
    if (rdump::isMesh(o)) n++;
  });
  return n;
}

// ------------------------------------------------------------------------------------------

void theShellAndTheWheels() {
  for (const CarDims* d : STYLES) {
    const CarShell sh = buildCarShell(*d);
    // as long, as wide and as high as its measures say
    const Box3 paint = boxOf(*sh.paint);
    assert(near(paint.min.z, sh.zFront, 1e-5) && near(paint.max.z, sh.zRear, 1e-5));
    assert(near(sh.zRear - sh.zFront, d->length));
    assert(near(paint.max.x, d->width / 2, 1e-5) && near(paint.min.x, -d->width / 2, 1e-5));
    assert(near(paint.max.y, d->height, 1e-5));
    assert(near(sh.zAxleR - sh.zAxleF, d->wheelbase));
    assert(sh.zFront < sh.zAxleF && sh.zAxleF < sh.zCowl && sh.zCowl < sh.zRoofFront && sh.zRoofFront < sh.zPillarB && sh.zPillarB < sh.zRoofRear && sh.zRoofRear <= sh.zTail && sh.zTail < sh.zRear);
    // the body is welded (an index), the hand-built pillars are not; every part has a normal for each corner
    for (const GeometryPtr& g : {sh.paint, sh.glass, sh.trim, sh.lining, sh.headliner}) {
      assert(g->index && g->index->count() % 3 == 0 && g->index->count() > 0);
      assert(g->normal() && g->normal()->count() == g->position()->count());
    }
    for (const GeometryPtr& g : {sh.pillars, sh.pillarTrim}) {
      assert(!g->index && g->position()->count() % 3 == 0 && g->position()->count() > 0);
      const AttrPtr n = g->normal();
      for (size_t i = 0; i < n->count(); i++) assert(near(std::hypot(n->getX(i), n->getY(i), n->getZ(i)), 1, 1e-5));
    }
    // the glass lies above the belt line, inside the body
    const Box3 glass = boxOf(*sh.glass);
    assert(glass.min.y > sh.cowlY - 0.05 && glass.max.y < sh.roofY + 1e-6 && glass.min.z > sh.zCowl - 1e-6 && glass.max.z < sh.zTail + 1e-6);
    // the bumpers' outlines: from the middle of the floor to the middle of the top
    assert(sh.noseOutline.size() == 7 && sh.tailOutline.size() == 7);
    assert(sh.noseOutline.front()[0] == 0 && sh.noseOutline.back()[0] == 0 && sh.noseOutline.front()[1] < sh.noseOutline.back()[1]);
    // the outline of the body anywhere along it: the same seven points, never wider than the car
    const auto at = bodyProfile(*d);
    for (double z = sh.zFront; z <= sh.zRear; z += 0.25) {
      const auto pts = at(z);
      assert(pts.size() == 7);
      for (const auto& q : pts) assert(q[0] >= 0 && q[0] <= d->width / 2 + 1e-9 && q[1] > 0.15 && q[1] <= d->height + 1e-9);
    }
    // a wheel lies along x: as wide as the tyre, as high as twice its radius
    const WheelGeometry w = buildWheel(*d);
    const Box3 tyre = boxOf(*w.tyre);
    assert(near(tyre.max.x - tyre.min.x, d->tyreWidth, 1e-5) && near(tyre.max.y, d->wheelRadius, 1e-5) && near(tyre.min.z, -d->wheelRadius, 1e-5));
    assert(!w.tyre->uv() && w.tyre->normal() && w.rim->index && w.rim->normal());
  }
  // the hatchback's tail is shorter than the estate's roof is long
  assert(buildCarShell(ESTATE).zRoofRear > buildCarShell(COMPACT_DIMS).zRoofRear + 0.3);
}

void sculpting() {
  // a square tube with both ends closed: 4 sides of two triangles, two fans of four
  const std::vector<Vec3> a = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
  std::vector<Vec3> b = a;
  for (Vec3& p : b) p[2] = 2;
  const GeometryPtr tube = Sculpt().tube({a, b}, true, true).build();
  assert(tube->position()->count() == (8 + 8) * 3 && !tube->index);
  // sharp edges at 90°: every normal is one of the six axes, and looks outwards
  const AttrPtr p = tube->position();
  const AttrPtr n = tube->normal();
  for (size_t i = 0; i < n->count(); i++) {
    const Vector3 normal(n->getX(i), n->getY(i), n->getZ(i));
    assert(near(normal.length(), 1, 1e-6));
    assert(near(std::fabs(normal.x) + std::fabs(normal.y) + std::fabs(normal.z), 1, 1e-6));
    const Vector3 out = Vector3(p->getX(i), p->getY(i), p->getZ(i)).sub(Vector3(0.5, 0.5, 1));
    assert(normal.dot(out) > 0);
  }
  // … seen from inside with `inward`
  const GeometryPtr recess = Sculpt().tube({a, b}, false, false, true).build();
  for (size_t i = 0; i < recess->normal()->count(); i++) {
    const Vector3 normal(recess->normal()->getX(i), recess->normal()->getY(i), recess->normal()->getZ(i));
    const Vector3 out = Vector3(recess->position()->getX(i), recess->position()->getY(i), recess->position()->getZ(i)).sub(Vector3(0.5, 0.5, 1));
    assert(normal.dot(out) < 0);
  }
  // a gentle fold is shaded smooth (the corners on the fold share a normal), a slim sliver is dropped
  const GeometryPtr sheet = Sculpt().sheet({{{0, 0, 0}, {1, 0, 0}, {2, 0.1, 0}}, {{0, 0, 1}, {1, 0, 1}, {2, 0.1, 1}}}, {0, 1, 0}).tri({0, 0, 0}, {1, 0, 0}, {2, 0, 0}).build();
  assert(sheet->position()->count() == 12);
  bool smooth = false;
  for (size_t i = 0; i < sheet->normal()->count(); i++) {
    const double ny = sheet->normal()->getY(i);
    assert(ny > 0.9);
    if (ny < 0.9999 && ny > 0.996) smooth = true;
  }
  assert(smooth);
  // a fan looks where it is told to
  const GeometryPtr fan = Sculpt().fan(a, {0, 0, -1}).build();
  assert(fan->position()->count() == 12);
  for (size_t i = 0; i < fan->normal()->count(); i++) assert(near(fan->normal()->getZ(i), -1, 1e-6));
}

void theCarOfTheTraffic() {
  CarExterior ex(0x8a8f96);
  assert(&ex.dims == &COMPACT_DIMS);
  assert(ex.wheels.size() == 4 && ex.wheelSpinners.size() == 4);
  // the body's three parts, ten lamps and the grille are solid; the glass is not
  assert(ex.solids.size() == 14 && ex.lampMeshes.size() == 10);
  // … and with the two mirror housings, the glass and the four wheels in the group
  assert(ex.group->children.size() == 14 + 1 + 4 + 2);
  // two cars of the same measures share their shell, each has its own paint
  CarExterior other(0x203040);
  assert(&other.shell == &ex.shell && other.solids[0]->geometry == ex.solids[0]->geometry);
  assert(other.paint != ex.paint && other.paint->color.getHex() == 0x203040 && ex.paint->color.getHex() == 0x8a8f96);
  CarExterior estate(0x203040, {.dims = &ESTATE});
  assert(&estate.shell != &ex.shell && &estate.dims == &ESTATE);
  // no plates, no lettering, no sign unless asked for
  assert(!find(*ex.group, "lettering front"));
  // the wheels stand at the corners, on the ground
  const double sides[4] = {-1, 1, -1, 1};
  for (size_t i = 0; i < 4; i++) {
    assert(near(ex.wheels[i]->position.x, sides[i] * COMPACT_DIMS.track / 2) && near(ex.wheels[i]->position.y, COMPACT_DIMS.wheelRadius));
    assert(near(ex.wheels[i]->position.z, i < 2 ? ex.shell.zAxleF : ex.shell.zAxleR));
  }
  // the front wheels steer (to the right: about −y), all four turn (forwards: about −x) and stand as high as told
  ex.setWheels({0.3, 0.25, 0.2, 0.2}, {1, 2, 3, 4}, {0.31, 0.32, 0.33, 0.34});
  assert(near(ex.wheels[0]->rotation.y, -0.3) && near(ex.wheels[1]->rotation.y, -0.25) && near(ex.wheels[2]->rotation.y, 0) && near(ex.wheels[3]->rotation.y, 0));
  for (size_t i = 0; i < 4; i++) {
    assert(near(ex.wheelSpinners[i]->rotation.x, -(1.0 + static_cast<double>(i))));
    assert(near(ex.wheels[i]->position.y, 0.31 + 0.01 * static_cast<double>(i)));
  }
  // the lamps change with the state
  ex.setLamps({});
  assert(ex.head->color.getHex() == 0xcfd6da && ex.tail->color.getHex() == 0x5a0d0d && ex.reverse->color.getHex() == 0x777777);
  assert(ex.blinkL->color.getHex() == 0x6a3a08 && ex.blinkR->color.getHex() == 0x6a3a08);
  ex.setLamps({.low = true});
  assert(ex.head->color.getHex() == 0xf4f6f0 && ex.tail->color.getHex() == 0xa01410);
  ex.setLamps({.low = true, .high = true, .brake = true, .reverse = true, .left = true});
  assert(ex.head->color.getHex() == 0xffffff && ex.tail->color.getHex() == 0xff2a1e && ex.reverse->color.getHex() == 0xffffff);
  assert(ex.blinkL->color.getHex() == 0xffa010 && ex.blinkR->color.getHex() == 0x6a3a08);
  ex.setLamps({.right = true});
  assert(ex.blinkL->color.getHex() == 0x6a3a08 && ex.blinkR->color.getHex() == 0xffa010 && ex.tail->color.getHex() == 0x5a0d0d);
  // the body throws shadow from both of its sides
  assert(ex.paint->shadowSide && *ex.paint->shadowSide == Side::Double && ex.solids[0]->castShadow);
}

void thePlayersCar() {
  PlayerCar car;
  assert(car.root->children.size() == 2 && car.root->children[0] == car.exterior.group && car.root->children[1] == car.cockpit);
  // the driving-school car: plates, the sign on the roof, the lettering on both bumpers – and no plain mirror housings
  assert(find(*car.root, "lettering front") && find(*car.root, "lettering rear"));
  assert(car.exterior.solids.size() == 14 + 3 && car.exterior.group->children.size() == 17 + 1 + 4 + 2 + 2);
  // which mirrors there are: inside, left, right – each with its glass and its housing in the cockpit
  assert(car.mirrors.size() == 3);
  const char* names[3] = {"inside", "left", "right"};
  for (size_t i = 0; i < 3; i++) {
    const Mirror& m = *car.mirrors[i];
    assert(m.spec.name == names[i] && m.mesh->name == std::string("mirror ") + names[i] && m.enabled);
    assert(m.mesh->parent == car.cockpit.get() && m.attached.size() == 1 && m.attached[0]->name == std::string("mirror housing ") + names[i]);
    assert(m.target->width == (i == 0 ? 512 : 320) && m.target->height == (i == 0 ? 160 : 200) && m.mesh->material->map == m.target->texture);
    assert(near(m.spec.normal.length(), 1, 1e-12));
  }
  assert(car.mirrors[1]->spec.centre.x < -car.exterior.shell.halfWidth && car.mirrors[2]->spec.centre.x > car.exterior.shell.halfWidth);
  assert(car.mirrors[0]->spec.centre.y > car.eye.y && find(*car.cockpit, "mirror feet"));
  // the driver sits on the left, the instruments and the screen are there
  assert(car.eye.x < 0);
  Object3D* cluster = find(*car.cockpit, "cluster");
  assert(cluster && cluster->position.x == car.eye.x);
  assert(car.screenMesh() && car.screenMesh()->name == "screen" && car.mapCanvas.width() == 512 && car.mapCanvas.height() == 288 && car.mapTexture->canvas == car.mapCanvas);
  // the door panels, kept apart by door for the doors that open: of five materials, four doors each
  assert(car.doorTrim.size() == 5);
  for (const DoorTrim& t : car.doorTrim) {
    assert(t.mesh && t.rest && t.doors.size() == 4);
    size_t corners = t.rest->position()->count();
    for (const GeometryPtr& g : t.doors) corners += g ? g->position()->count() : 0;
    assert(corners == t.mesh->geometry->position()->count());
  }

  // ---- what the driver does ----
  const auto rotationOf = [&car](size_t group, int axis) {
    // the column, the lever, the handbrake and the three pedals follow the dial and the display in the cockpit
    Object3D& o = *car.cockpit->children[group];
    return axis == 0 ? static_cast<double>(o.rotation.x) : axis == 1 ? static_cast<double>(o.rotation.y) : static_cast<double>(o.rotation.z);
  };
  size_t column = 0;
  for (size_t i = 0; i < car.cockpit->children.size(); i++)
    if (car.cockpit->children[i].get() == cluster) column = i + 2;
  Object3D& steering = *car.cockpit->children[column]->children[0];
  const size_t lever = column + 1, handbrake = column + 2, pedals = column + 3;
  CarVisualState s = state();
  s.gear = 1;
  s.steerWheel = 0.8;
  s.clutch = 1;
  s.brake = 0.5;
  s.throttle = 0.25;
  s.handbrakeLever = 1;
  s.lowBeam = true;
  s.braking = true;
  s.indicator = 1;
  s.blinkOn = true;
  for (int i = 0; i < 100; i++) car.update(s, 0.05, 1 + i * 0.05);
  assert(near(steering.rotation.z, -0.8));
  // first gear: to the left and forward in the gate
  assert(near(rotationOf(lever, 2), 0.17, 1e-6) && near(rotationOf(lever, 0), -0.2, 1e-6));
  assert(near(rotationOf(handbrake, 0), 0.42));
  assert(near(rotationOf(pedals, 0), -0.5 + 0.32) && near(rotationOf(pedals + 1, 0), -0.5 + 0.11) && near(rotationOf(pedals + 2, 0), -0.5 + 0.07));
  assert(car.cockpit->children[pedals]->visible);
  assert(car.exterior.head->color.getHex() == 0xf4f6f0 && car.exterior.tail->color.getHex() == 0xff2a1e);
  assert(car.exterior.blinkR->color.getHex() == 0xffa010 && car.exterior.blinkL->color.getHex() == 0x6a3a08);
  s.blinkOn = false;
  car.update(s, 0.05, 10);
  assert(car.exterior.blinkR->color.getHex() == 0x6a3a08);
  // sixth: to the right and back; neutral: the lever comes to the middle; an automatic has no clutch pedal
  s.gear = 6;
  for (int i = 0; i < 100; i++) car.update(s, 0.05, 20 + i);
  assert(near(rotationOf(lever, 2), -0.17, 1e-6) && near(rotationOf(lever, 0), 0.2, 1e-6));
  s.gear = 0;
  for (int i = 0; i < 100; i++) car.update(s, 0.05, 200 + i);
  assert(near(rotationOf(lever, 2), 0, 1e-6) && near(rotationOf(lever, 0), 0, 1e-6));
  s.automatic = true;
  s.gear = -2;
  for (int i = 0; i < 100; i++) car.update(s, 0.05, 400 + i);
  assert(!car.cockpit->children[pedals]->visible && near(rotationOf(lever, 0), -0.3, 1e-6));

  // ---- the instruments are drawn at most some twenty times a second ----
  const Ctx2D dial = static_cast<Mesh*>(cluster)->material->map->canvas.getContext2D();
  const long drawn = canvasNative::calls(dial);
  car.update(s, 0.01, 499.01);
  assert(canvasNative::calls(dial) == drawn);
  car.update(s, 0.01, 499.06);
  assert(canvasNative::calls(dial) > drawn);

  // ---- the map on the screen, or its still picture ----
  Mesh& screen = *static_cast<Mesh*>(car.screenMesh());
  const TexturePtr still = screen.material->map;
  assert(still && still != car.mapTexture && !screen.material->toneMapped);
  car.setMapOnScreen(true);
  assert(screen.material->map == car.mapTexture);
  car.setMapOnScreen(false);
  assert(screen.material->map == still);

  // ---- the see-through cabin: every part of it has a twin that draws its depth, hidden while the cabin is solid ----
  std::vector<Mesh*> twins;
  car.root->traverse([&twins](Object3D& o) {
    if (!rdump::isMesh(o) || !o.parent || !rdump::isMesh(*o.parent)) return;
    Mesh& m = static_cast<Mesh&>(o);
    if (m.geometry == static_cast<Mesh*>(o.parent)->geometry && !m.material->colorWrite) twins.push_back(&m);
  });
  assert(twins.size() > 40);
  for (Mesh* t : twins) assert(!t->visible && !static_cast<Mesh*>(t->parent)->material->transparent && t->parent->renderOrder == 0);
  car.setCabinOpacity(0.3);
  for (Mesh* t : twins) {
    assert(t->visible && static_cast<Mesh*>(t->parent)->material->transparent);
    // depth of the cabin, cabin, depth of the controls, controls
    assert((t->renderOrder == 1 && t->parent->renderOrder == 2) || (t->renderOrder == 3 && t->parent->renderOrder == 4));
  }
  // the instruments, the screen and the mirrors with their housings stay solid; the steering wheel is of the controls in front
  assert(!static_cast<Mesh*>(cluster)->material->transparent && !screen.material->transparent && cluster->children.empty());
  for (const auto& m : car.mirrors) assert(!m->mesh->material->transparent && !static_cast<Mesh&>(*m->attached[0]).material->transparent && m->attached[0]->children.empty());
  assert(steering.children[0]->renderOrder == 4 && car.exterior.solids[0]->renderOrder == 2);
  car.setCabinOpacity(1);
  for (Mesh* t : twins) assert(!t->visible && !static_cast<Mesh*>(t->parent)->material->transparent && t->parent->renderOrder == 0);
  const size_t ghosts = twins.size();

  // ---- the finer model (render/cardetail) and the parts that open (render/caropen) ----
  const int meshes = countMeshes(*car.root);
  // nothing is made while the finer model is off and every part is shut
  car.setDetail(false);
  car.setOpenings({0, 0, 0, 0, 0, 0});
  car.update(s, 0.05, 600);
  assert(countMeshes(*car.root) == meshes);
  // made the first time it is asked for, taken into the see-through cabin, told what the car does
  car.setCabinOpacity(0.5);
  car.setDetail(true);
  const int fine = countMeshes(*car.root);
  assert(fine > meshes + 20);
  assert(find(*car.root, "body detail") && find(*car.root, "lamps") && find(*car.root, "detailed wheels") && find(*car.root, "shift pattern"));
  car.update(s, 0.05, 700);
  car.setDetail(false);
  assert(!find(*car.root, "body detail")->visible);
  car.setDetail(true);
  assert(countMeshes(*car.root) == fine && find(*car.root, "body detail")->visible);
  // the parts that open: cut out the first time one opens (the finer model's meshes go with them)
  car.setOpenings({0, 0.5, 0, 0, 0, 0});
  CarOpenings* opener = car.openings();
  assert(opener && countMeshes(*car.root) > fine);
  car.setOpenings({0, 0, 0, 0, 0, 0});
  assert(car.openings() == opener);
  car.setCabinOpacity(1);
  car.dispose();
}

/** tests/game/cockpit.test.ts: 'looking into a mirror: what the glass shows, in the shape of the panel – at least so much of it' */
void lookingIntoAMirror() {
  // the car's door mirrors and the driver's eye (render/car.ts): a metre and more from the eye, they show only a few degrees
  const Vector3 eye(-0.36, 1.2, 0.2);
  const auto door = [&eye](double side, double convex) {
    const Vector3 centre(side * 0.995, 1.0163, -0.675);
    return std::make_unique<Mirror>(MirrorSpec{.name = side < 0 ? "left" : "right",
                                               .centre = centre,
                                               .width = 0.17,
                                               .height = 0.105,
                                               .normal = mirrorNormal(centre, eye, Vector3(side * 0.16, -0.06, 1)),
                                               .convex = convex,
                                               .resolution = {16, 16}});
  };
  const Matrix4 car;
  // (the frustum's tangents across and up, from the projection: 2 / m00, 2 / m11 – width and height at a distance of 1)
  const auto tans = [](const Mirror& m) {
    const double* e = m.camera->projectionMatrix.elements;
    return std::pair<double, double>(2 / e[0], 2 / e[5]);
  };
  const double minTan = std::tan((7 * PI) / 180);
  for (const auto& m : {door(-1, 1.25), door(1, 1.6)}) {
    assert(m->aim(car, eye, 300) == true);
    const auto [w0, h0] = tans(*m);
    assert(std::fabs(w0 / h0 - 0.17 / 0.105) < 1e-6);   // the glass itself
    assert(h0 / 2 < minTan);                           // a narrow view of its own
    assert(m->aim(car, eye, 300, 1, 2, minTan) == true);
    const auto [w1, h1] = tans(*m);
    assert(std::fabs(w1 / h1 - 2) < 1e-6);             // the shape of the panel
    assert(std::fabs(h1 / 2 - minTan) < 1e-9);         // widened to the least angle
    // a car moved and turned: the same view, in its new place
    const Matrix4 moved = Matrix4().makeRotationY(0.7).setPosition(30, 2, -40);
    assert(m->aim(moved, eye.clone().applyMatrix4(moved), 300, 1, 2, minTan) == true);
    const auto [w2, h2] = tans(*m);
    assert(std::fabs(w2 - w1) < 1e-9 && std::fabs(h2 - h1) < 1e-9);
  }
  // a wide view of its own is not narrowed: the least angle only widens
  const auto wide = door(-1, 6);
  wide->aim(car, eye, 300);
  const double hw = tans(*wide).second;
  wide->aim(car, eye, 300, 1, 0, minTan);
  assert(std::fabs(tans(*wide).second - hw) < 1e-9);
  // an eye behind the glass sees nothing in it
  assert(door(-1, 1.25)->aim(car, Vector3(-1.2, 1.0, -1.2), 300) == false);
}

void theMirrorsThemselves() {
  const Vector3 eye(-0.36, 1.2, 0.2);
  const Vector3 centre(-0.19, 1.38, -0.17);
  Mirror m(MirrorSpec{.name = "inside", .centre = centre, .width = 0.15, .height = 0.042, .normal = mirrorNormal(centre, eye, Vector3(0, -0.03, 1)), .convex = 1.5, .resolution = {512, 160}});
  // the picture: half floats, no mip-maps, drawn without antialiasing; the glass shows it, without fog
  assert(m.target->width == 512 && m.target->height == 160 && m.target->depthBuffer && m.target->samples == 0);
  assert(m.target->texture->type == PixelType::HalfFloat && !m.target->texture->generateMipmaps && m.target->texture->minFilter == Filter::Linear);
  assert(m.mesh->material->map == m.target->texture && !m.mesh->material->fog && m.mesh->material->type == MaterialType::Basic);
  assert(!m.camera->matrixAutoUpdate);
  // the glass: four corners around its centre, as wide and as high as told, at right angles to its normal
  const AttrPtr p = m.mesh->geometry->position();
  assert(p->count() == 4 && m.mesh->geometry->index->count() == 6);
  Vector3 mid;
  for (size_t i = 0; i < 4; i++) mid.add(Vector3(p->getX(i), p->getY(i), p->getZ(i)).multiplyScalar(0.25));
  assert(mid.distanceTo(centre) < 1e-6);
  const Vector3 a(p->getX(0), p->getY(0), p->getZ(0)), b(p->getX(1), p->getY(1), p->getZ(1)), c(p->getX(2), p->getY(2), p->getZ(2));
  assert(near(a.distanceTo(b), 0.15, 1e-6) && near(b.distanceTo(c), 0.042, 1e-6));
  assert(std::fabs(b.clone().sub(a).dot(m.spec.normal)) < 1e-6 && std::fabs(c.clone().sub(b).dot(m.spec.normal)) < 1e-6);
  // the eye's mirror image is the camera's place; it looks away from the eye, through the glass
  assert(m.aim(Matrix4(), eye, 300));
  const Vector3 cam = Vector3().setFromMatrixPosition(m.camera->matrixWorld);
  const double d = eye.clone().sub(centre).dot(m.spec.normal);
  assert(cam.distanceTo(eye.clone().addScaledVector(m.spec.normal, -2 * d)) < 1e-9);
  Vector3 look(0, 0, -1);
  look.transformDirection(m.camera->matrixWorld);
  assert(look.dot(m.spec.normal) > 0.999);
  // switched off: a grey glass without a picture; on again: the picture
  m.setEnabled(false);
  assert(!m.enabled && !m.mesh->material->map && m.mesh->material->color.getHex() == 0x1c2228);
  m.setEnabled(true);
  assert(m.enabled && m.mesh->material->map == m.target->texture && m.mesh->material->color.getHex() == 0xffffff);
  // put elsewhere, with what is attached to it: 10 cm to the right, half as large again
  const std::shared_ptr<Mesh> housing = m.makeHousing(0.03, basic());
  assert(!housing->geometry->uv());
  m.attached.push_back(housing);
  m.place(0.1, 1.5, nullptr);
  assert(near(m.spec.centre.x, centre.x + 0.1) && near(m.spec.width, 0.225) && near(m.spec.height, 0.063));
  assert(near(m.mesh->scale.x, 1.5, 1e-9) && near(housing->scale.y, 1.5, 1e-9));
  Vector3 corner(p->getX(0), p->getY(0), p->getZ(0));
  corner.applyMatrix4(m.mesh->matrix);
  assert(near(corner.distanceTo(m.spec.centre), 0.5 * std::hypot(0.225, 0.063), 1e-6));
  m.place(0, 1, nullptr);
  assert(m.mesh->position.length() < 1e-9 && near(m.mesh->scale.x, 1, 1e-12) && m.spec.centre.distanceTo(centre) < 1e-12);
  m.dispose();
}

void theInstruments() {
  InstrumentPanel panel;
  assert(panel.canvas.width() == 768 && panel.canvas.height() == 256 && panel.texture->canvas == panel.canvas && panel.texture->srgb && panel.texture->anisotropy == 4);
  const Ctx2D g = panel.canvas.getContext2D();
  // never a blank cluster: the dials are there before the first update
  assert(canvasNative::calls(g) == 1);
  const uint32_t version = panel.texture->version;
  CarVisualState s = state();
  s.speedKmh = 87.4;
  s.gear = 4;
  s.showGear = true;
  s.odometerKm = 7.2;
  panel.update(s, 1);
  assert(panel.texture->version == version + 1);
  std::vector<std::string> texts = canvasNative::texts(g);
  const auto has = [&texts](const std::string& t) { return std::find(texts.begin(), texts.end(), t) != texts.end(); };
  assert(has("87") && has("km/h") && has("4") && has("7.2 km") && has("ABS"));
  // a manual car shows its gear only as a driver aid; an automatic always, by its letters
  canvasNative::forget(g);
  s.showGear = false;
  panel.update(s, 2);
  texts = canvasNative::texts(g);
  assert(!has("4") && has("87"));
  canvasNative::forget(g);
  s.automatic = true;
  s.gear = -2;
  panel.update(s, 3);
  texts = canvasNative::texts(g);
  assert(has("P"));
  canvasNative::forget(g);
  s.gear = 2;
  panel.update(s, 4);
  texts = canvasNative::texts(g);
  assert(has("D2"));
  // the ignition off: no speed, no gear – the telltales are still drawn, dark
  canvasNative::forget(g);
  s.ignition = false;
  panel.update(s, 5);
  texts = canvasNative::texts(g);
  assert(!has("87") && !has("D2") && !has("km/h") && has("ABS"));
  // drawn a moment ago: not again; a clock that jumped back does not stop the instruments
  const long calls = canvasNative::calls(g);
  panel.update(s, 5.03);
  assert(canvasNative::calls(g) == calls);
  panel.update(s, -100);
  assert(canvasNative::calls(g) > calls);
}

void aVehiclesScreen() {
  NavScreen nav(0.27);
  VehicleScreen& screen = nav;
  assert(screen.canvas.width() == 512 && screen.canvas.height() == 288 && screen.hitMesh() && screen.hitMesh()->name == "screen");
  Mesh& face = static_cast<Mesh&>(*screen.hitMesh());
  // 16 : 9, a self-lit picture: the still one until the live map is asked for
  const Box3 box = boxOf(*face.geometry);
  assert(near(box.max.x - box.min.x, 0.27, 1e-6) && near(box.max.y - box.min.y, 0.27 * 288 / 512, 1e-6));
  const TexturePtr still = face.material->map;
  assert(still && still->canvas && !(still->canvas == screen.canvas) && !face.material->toneMapped);
  screen.setLive(true);
  assert(face.material->map->canvas == screen.canvas);
  const uint32_t version = face.material->map->version;
  screen.drawn();
  assert(face.material->map->version == version + 1);
  screen.setLive(false);
  assert(face.material->map == still);
  // every screen shows the same still picture
  NavScreen other(0.22);
  assert(static_cast<Mesh&>(*other.hitMesh()).material->map == still);
  // fixed in the cab: it faces the eye, an arm leads back to its mount
  const std::shared_ptr<Group> cab = make<Group>();
  const Vector3 at(0.62, 1.5, -1.2), eye(-0.4, 1.9, -0.6), mount(0.62, 1.3, -1.25);
  nav.place(*cab, at, eye, &mount);
  assert(cab->children.size() == 2 && cab->children[0] == nav.group && nav.group->position.equals(at));
  Vector3 facing(0, 0, 1);
  facing.applyQuaternion(nav.group->quaternion);
  assert(facing.distanceTo(eye.clone().sub(at).normalize()) < 1e-9);
  other.place(*cab, at, eye);
  assert(cab->children.size() == 3);
}

/** everything, number by number: the lines tools/diff/render_cars.ts --part car prints of the original */
void againstTheOriginal() {
  dump::lines().clear();
  // ---- the body shells, the wheels ----
  for (int i = 0; i < 3; i++) {
    const CarDims& d = *STYLES[i];
    const CarShell sh = buildCarShell(d);
    out("shell", i, sh.zFront, sh.zRear, sh.zAxleF, sh.zAxleR, sh.zCowl, sh.zRoofFront, sh.zRoofRear, sh.zPillarB, sh.zTail, sh.halfWidth, sh.beltY, sh.roofY, sh.cowlY, sh.cowlCrownY);
    const auto flat = [](const std::vector<std::array<double, 2>>& pts) {
      std::vector<double> v;
      for (const auto& q : pts) v.insert(v.end(), {q[0], q[1]});
      return v;
    };
    out("nose", flat(sh.noseOutline));
    out("tail", flat(sh.tailOutline));
    const std::pair<const char*, GeometryPtr> parts[] = {{"paint", sh.paint}, {"glass", sh.glass}, {"trim", sh.trim}, {"lining", sh.lining}, {"headliner", sh.headliner}, {"pillars", sh.pillars}, {"pillarTrim", sh.pillarTrim}};
    for (const auto& [name, g] : parts) {
      out("part", name);
      rdump::geometry(*g);
    }
    const auto at = bodyProfile(d);
    for (const double z : {-2.0, -1.3, -0.5, 0.0, 0.7, 1.4, 2.0}) out("profile", z, flat(at(z)));
    const WheelGeometry w = buildWheel(d);
    out("wheel", i);
    rdump::geometry(*w.tyre);
    rdump::geometry(*w.rim);
  }

  // ---- the car of the traffic, in each style ----
  for (int i = 0; i < 3; i++) {
    CarExterior ex(0x8a8f96, {.dims = STYLES[i]});
    const std::string name = "exterior " + std::to_string(i);
    rdump::tree(name, *ex.group);
    out("lists", static_cast<int>(ex.wheels.size()), static_cast<int>(ex.wheelSpinners.size()), static_cast<int>(ex.solids.size()), static_cast<int>(ex.lampMeshes.size()));
    ex.setWheels({0.3, 0.25, 0, 0}, {1, 2, 3, 4}, {0.31, 0.32, 0.33, 0.34});
    ex.setLamps({.low = true, .high = false, .brake = true, .reverse = false, .left = true, .right = false});
    rdump::pose(name + " driving", *ex.group);
    ex.setLamps({.low = false, .high = true, .brake = false, .reverse = true, .left = false, .right = true});
    rdump::pose(name + " reversing", *ex.group);
    ex.setLamps({});
    rdump::pose(name + " dark", *ex.group);
  }
  // (with the mirror housings left out, a plate, glass one sees through, and materials of its own)
  {
    CarExterior plain(0x203040, {.plate = "KS AB 123", .seeThroughGlass = true, .noMirrorHousings = true, .ownMaterials = true});
    rdump::tree("exterior plain", *plain.group);
  }
  for (const auto& [hex, shine] : {std::pair<int, double>{0x40444b, 0}, std::pair<int, double>{0xb9bec4, 50}}) {
    out("cabinMaterial", hex, shine);
    rdump::material(*cabinMaterial(hex, shine));
  }

  // ---- the player's car ----
  PlayerCar car;
  rdump::tree("car", *car.root);
  out("eye", car.eye.x, car.eye.y, car.eye.z);
  out("doorTrim", static_cast<int>(car.doorTrim.size()));
  for (const DoorTrim& t : car.doorTrim) {
    out("trim", t.mesh->material->color.r, t.rest ? 1 : 0, static_cast<int>(t.doors.size()));
    if (t.rest) rdump::geometry(*t.rest);
    for (const GeometryPtr& g : t.doors) {
      out("door", g ? 1 : 0);
      if (g) rdump::geometry(*g);
    }
  }
  out("mirrors", static_cast<int>(car.mirrors.size()));
  for (const auto& m : car.mirrors) {
    const MirrorSpec& s = m->spec;
    out("mirror", s.name, P3(s.centre), s.width, s.height, P3(s.normal), s.convex, s.resolution[0], s.resolution[1], m->target->width, m->target->height, m->enabled, static_cast<int>(m->attached.size()));
  }
  Mesh& screen = static_cast<Mesh&>(*car.screenMesh());
  out("screen", screen.name, screen.material->map == car.mapTexture ? 1 : 0, car.mapCanvas.width(), car.mapCanvas.height());

  // ---- what the driver does, on the car ----
  const Ctx2D cluster = static_cast<Mesh*>(car.cockpit->getObjectByName("cluster"))->material->map->canvas.getContext2D();
  struct Step {
    const char* name;
    CarVisualState s;
    double dt;
    double now;
  };
  std::vector<Step> steps;
  {
    CarVisualState s = state();
    s.speedKmh = 47.6, s.rpm = 2300, s.gear = 3, s.indicator = -1, s.blinkOn = true, s.lowBeam = true, s.showGear = true, s.odometerKm = 12.34, s.steerWheel = -1.2, s.clutch = 0.2, s.throttle = 0.6;
    CarVisualState turning = s;
    turning.wheelSteer = {-0.21, -0.19, 0, 0};
    turning.wheelSpin = {5, 6, 7, 8};
    turning.wheelY = {0.31, 0.32, 0.316, 0.3};
    steps.push_back({"third gear, turning left", turning, 0.016, 10});
    steps.push_back({"the same, a moment later", s, 0.016, 10.02});
    s = state();
    s.speedKmh = 21, s.rpm = 1500, s.gear = 2, s.hazard = true, s.blinkOn = true, s.highBeam = true, s.lowBeam = true, s.abs = true, s.seatbelt = false, s.fuel = 0.1, s.brake = 0.8, s.braking = true, s.handbrakeLever = 0.5,
    s.handbrake = true;
    steps.push_back({"braking in second, hazard lights", s, 0.1, 11});
    s = state();
    s.gear = 0, s.engineRunning = false;
    steps.push_back({"neutral", s, 0.05, 12});
    s = state();
    s.gear = -1, s.reversing = true, s.speedKmh = -3;
    steps.push_back({"reverse", s, 1, 13});
    s = state();
    s.gear = 6, s.speedKmh = 131.5, s.rpm = 2900;
    steps.push_back({"sixth", s, 0.03, 14});
    s = state();
    s.automatic = true, s.gear = -2, s.ignition = false;
    steps.push_back({"an automatic in P", s, 0.2, 15});
    s = state();
    s.automatic = true, s.gear = 4, s.speedKmh = 80.5, s.indicator = 1, s.blinkOn = true;
    steps.push_back({"an automatic in D", s, 0.2, 16});
    s = state();
    s.automatic = true, s.gear = -1;
    steps.push_back({"a clock that jumped back", s, 0.2, 3});
  }
  for (const Step& step : steps) {
    car.update(step.s, step.dt, step.now);
    rdump::pose(step.name, *car.root);
    const std::vector<std::string> texts = canvasNative::texts(cluster);
    out("cluster", canvasNative::calls(cluster), static_cast<int>(texts.size()), rdump::words(texts, texts.size() > 12 ? texts.size() - 12 : 0));
  }

  // ---- the cabin seen through, the map on the screen ----
  for (const double o : {0.4, 0.4, 1.0, JS_NAN, 0.999, 0.01}) {
    car.setCabinOpacity(o);
    rdump::pose("cabin " + jsStr(o), *car.root);
  }
  car.setMapOnScreen(true);
  out("screen", screen.name, screen.material->map == car.mapTexture ? 1 : 0, static_cast<int>(screen.material->version));
  car.setMapOnScreen(true);
  car.setMapOnScreen(false);
  out("screen", screen.name, screen.material->map == car.mapTexture ? 1 : 0, static_cast<int>(screen.material->version));

  // ---- the mirrors: where their cameras look, the glass put elsewhere ----
  const auto aimed = [](const Mirror& m, bool ok) { out("aim", m.spec.name, ok, rdump::places(m.camera->matrix), rdump::elements(m.camera->projectionMatrix)); };
  const Matrix4 moved = Matrix4().makeRotationY(0.7).setPosition(30, 2, -40);
  for (const auto& mp : car.mirrors) {
    Mirror& m = *mp;
    aimed(m, m.aim(Matrix4(), car.eye, 300));
    aimed(m, m.aim(moved, car.eye.clone().applyMatrix4(moved), 450, 1.5));
    aimed(m, m.aim(moved, car.eye.clone().applyMatrix4(moved), 450, 1, 2, std::tan((7 * PI) / 180)));
    out("behind", m.spec.name, m.aim(Matrix4(), m.spec.centre.clone().addScaledVector(m.spec.normal, -0.5), 300));
    const std::shared_ptr<Mesh> housing = m.makeHousing(0.05, basic());
    out("housing", m.spec.name);
    rdump::geometry(*housing->geometry);
  }
  const auto placed = [](const Mirror& m, const std::string& name) {
    out("placed", word(name), P3(m.spec.centre), m.spec.width, m.spec.height, P3(m.spec.normal));
    std::vector<Object3D*> all = {m.mesh.get()};
    for (const ObjectPtr& o : m.attached) all.push_back(o.get());
    for (const Object3D* o : all) out("at", P3(o->position), rdump::PQ(o->quaternion), rdump::xyz(o->scale), rdump::places(o->matrix));
  };
  Mirror& inside = *car.mirrors[0];
  inside.place(0.05, 1.2, &car.eye);
  placed(inside, "shifted, larger, turned to the eye");
  aimed(inside, inside.aim(Matrix4(), car.eye, 300));
  inside.place(-0.1, 0.8, nullptr);
  placed(inside, "shifted the other way, smaller");
  inside.place(0, 1, nullptr);
  placed(inside, "back");
  for (const auto& m : car.mirrors) {
    m->setEnabled(false);
    out("off", m->spec.name, m->enabled, m->mesh->material->map ? 1 : 0, rdump::rgb(m->mesh->material->color));
    m->setEnabled(true);
    out("on", m->spec.name, m->enabled, m->mesh->material->map == m->target->texture ? 1 : 0, m->mesh->material->color.r);
  }
  {
    const Vector3 eye(-0.36, 1.2, 0.2);
    const Vector3 centre(0.995, 1.0163, -0.675);
    out("mirrorNormal", P3(mirrorNormal(centre, eye, Vector3(0.16, -0.06, 1))));
    Mirror m(MirrorSpec{.name = "test", .centre = centre, .width = 0.17, .height = 0.105, .normal = mirrorNormal(centre, eye, Vector3(0.16, -0.06, 1)), .convex = 1.6, .resolution = {16, 16}});
    rdump::tree("mirror", *m.mesh);
  }

  // ---- the instruments by themselves ----
  {
    InstrumentPanel panel;
    const Ctx2D g = panel.canvas.getContext2D();
    out("panel", panel.canvas.width(), panel.canvas.height(), canvasNative::calls(g), static_cast<int>(canvasNative::texts(g).size()));
    std::vector<std::pair<CarVisualState, double>> shows;
    CarVisualState s = state();
    s.ignition = false;
    shows.push_back({s, 1});
    s = state();
    s.speedKmh = 99.5, s.rpm = 6900, s.gear = 5, s.showGear = true, s.odometerKm = 1234.56, s.fuel = 0.05, s.lowBeam = true, s.highBeam = true, s.handbrake = true, s.abs = true, s.engineRunning = false, s.seatbelt = false,
    s.hazard = true, s.blinkOn = true;
    shows.push_back({s, 2});
    s = state();
    s.speedKmh = 12, s.automatic = true, s.gear = 0;
    shows.push_back({s, 2.01});
    s = state();
    s.speedKmh = 12, s.automatic = true, s.gear = -1, s.indicator = 1, s.blinkOn = true, s.fuel = 2;
    shows.push_back({s, 2.1});
    s = state();
    s.speedKmh = -4, s.automatic = true, s.gear = 3, s.fuel = -1;
    shows.push_back({s, -5});
    for (const auto& [shown, now] : shows) {
      const size_t before = canvasNative::texts(g).size();
      panel.update(shown, now);
      out("shown", canvasNative::calls(g), rdump::words(canvasNative::texts(g), before));
    }
  }

  // ---- a vehicle's screen ----
  {
    const std::shared_ptr<Group> parent = make<Group>();
    const Vector3 mount(0.62, 1.3, -1.25);
    NavScreen nav(0.27);
    nav.place(*parent, Vector3(0.62, 1.5, -1.2), Vector3(-0.4, 1.9, -0.6), &mount);
    rdump::tree("nav screen", *parent);
    out("nav", nav.canvas.width(), nav.canvas.height(), nav.hitMesh()->name);
    nav.setLive(true);
    rdump::material(*static_cast<Mesh*>(nav.hitMesh())->material);
    nav.setLive(false);
    rdump::material(*static_cast<Mesh*>(nav.hitMesh())->material);
    const std::shared_ptr<Group> bare = make<Group>();
    NavScreen other(0.22);
    other.place(*bare, Vector3(0, 1, 0), Vector3(0, 1, 2));
    rdump::tree("nav screen without an arm", *bare);
  }

  const int bad = rdump::against("tests/fixtures/render-car.txt", 1e-6);
  if (bad) {
    std::fprintf(stderr, "render_car: %d lines differ from the original's\n", bad);
    std::exit(1);
  }
}

}  // namespace

int main() {
  // (the comparison first: it counts what was drawn on the canvases that all cars share, from the beginning)
  againstTheOriginal();
  theShellAndTheWheels();
  sculpting();
  theCarOfTheTraffic();
  thePlayersCar();
  lookingIntoAMirror();
  theMirrorsThemselves();
  theInstruments();
  aVehiclesScreen();
  std::printf("render_car: all checks passed (%zu lines the same as the original's)\n", dump::lines().size());
  return 0;
}

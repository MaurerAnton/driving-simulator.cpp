// LINK: render engine sim world game core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The aeroplane that is drawn (src/render/plane, src/render/flightpanel) – the original has no
 * test of these files; this one checks what the game relies on:
 *
 *  - the model builds: the outside, and the cockpit only when asked for;
 *  - the wheels stand where the physics has them (TRAINER.gear), and a strut that is pushed in
 *    lifts the aeroplane's lowest point by as much;
 *  - the propeller becomes a disc once it turns fast; the lamps follow the switches (the strobes
 *    and the beacon flash);
 *  - the see-through cabin;
 *  - the panel redraws at most 15 times a second (and again when the clock jumps back).
 *
 * Nothing is drawn here: the canvases of the native host count what they are asked.
 */
#include <cassert>
#include <cmath>
#include <cstdio>

#include "render/flightpanel.h"
#include "render/plane.h"
#include "sim/flightspec.h"

using namespace fs;

namespace {

int countVisible(Object3D& root, ObjectType type) {
  int n = 0;
  root.traverseVisible([&n, type](Object3D& o) {
    if (o.type == type) n++;
  });
  return n;
}

int count(Object3D& root, ObjectType type) {
  int n = 0;
  root.traverse([&n, type](Object3D& o) {
    if (o.type == type) n++;
  });
  return n;
}

double lowest(PlaneModel& m) {
  m.root->updateMatrixWorld(true);
  Box3 box;
  box.setFromObject(*m.root, true);
  return box.min.y;
}

PlaneVisualState parked() {
  PlaneVisualState s;
  s.wheels = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
  s.fuel = 0.5;
  s.parkingBrake = true;
  return s;
}

}  // namespace

int main() {
  // ---- the model builds, with and without its cockpit
  PlaneModel plane;
  assert(plane.root->name == "aeroplane");
  assert(plane.root->getObjectByName("cockpit") != nullptr);
  assert(plane.mirrors.empty());
  assert(std::fabs(plane.eye.x - TRAINER.eye[0]) < 1e-12 && std::fabs(plane.eye.y - TRAINER.eye[1]) < 1e-12);
  const int meshes = count(*plane.root, ObjectType::Mesh);
  PlaneModel scenery(PlaneModelOptions{.interior = false});
  assert(scenery.root->getObjectByName("cockpit") == nullptr);
  const int sceneryMeshes = count(*scenery.root, ObjectType::Mesh);
  std::printf("meshes: %d with the cockpit, %d without\n", meshes, sceneryMeshes);
  assert(sceneryMeshes > 30 && meshes > sceneryMeshes + 10);

  // ---- the wheels stand on TRAINER.gear: the lowest point is the bottom of the main wheels
  PlaneVisualState s = parked();
  plane.update(s, 0, 0);
  const double low = lowest(plane);
  const double wheelBottom = TRAINER.gear[1].y - TRAINER.gear[1].radius;
  std::printf("lowest point %.4f (main wheels' bottom %.4f)\n", low, wheelBottom);
  assert(std::fabs(low - wheelBottom) < 0.01);
  // struts pushed in by 8 cm: the aeroplane's lowest point is 8 cm higher in its own frame
  for (auto& w : s.wheels) w.compression = 0.08;
  plane.update(s, 0, 0);
  assert(std::fabs(lowest(plane) - (wheelBottom + 0.08)) < 0.01);
  // flaps out, nose wheel steered, the wheels turning: nothing comes loose
  s.flapsDeg = 30;
  s.wheels[0].steer = 0.3;
  for (auto& w : s.wheels) w.spin = 1.7;
  s.aileron = 0.2;
  s.elevator = -0.1;
  s.rudder = 0.15;
  plane.update(s, 0, 0);
  // (the flaps hang below the wing, the wheels stay the lowest point)
  assert(std::fabs(lowest(plane) - (wheelBottom + 0.08)) < 0.02);

  // ---- the propeller: blades while it turns slowly, a faint disc once it is fast
  s = parked();
  s.rpm = 0;
  plane.update(s, 0, 0);
  const int still = countVisible(*plane.root, ObjectType::Mesh);
  s.rpm = 2300;
  s.propAngle = 12.3;
  plane.update(s, 0, 0);
  assert(countVisible(*plane.root, ObjectType::Mesh) == still + 1);

  // ---- the lamps: nothing glows while the switches are off
  s = parked();
  plane.update(s, 0, 0.5);
  assert(countVisible(*plane.root, ObjectType::Sprite) == 0);
  // navigation lights: both wing tips and the tail
  s.lights.nav = true;
  plane.update(s, 0, 0.5);
  assert(countVisible(*plane.root, ObjectType::Sprite) == 3);
  // the strobes flash (at the beginning of each 1.2 s) and the beacon (at the beginning of each 1.1 s)
  s.lights.strobe = true;
  s.lights.beacon = true;
  plane.update(s, 0, 0.5);
  assert(countVisible(*plane.root, ObjectType::Sprite) == 3);
  plane.update(s, 0, 1.2 + 0.02);
  assert(countVisible(*plane.root, ObjectType::Sprite) == 3 + 2 + 1);
  // landing and taxi light
  const int lamps = countVisible(*plane.root, ObjectType::Mesh);
  s.lights.landing = true;
  s.lights.taxi = true;
  plane.update(s, 0, 0.5);
  assert(countVisible(*plane.root, ObjectType::Mesh) == lamps + 2);

  // ---- the see-through cabin: the cockpit's materials turn transparent, and back
  const auto cockpitTransparent = [&plane]() {
    int n = 0;
    plane.root->getObjectByName("cockpit")->traverse([&n](Object3D& o) {
      if (o.type == ObjectType::Mesh) {
        auto* m = static_cast<Mesh*>(&o);
        if (m->material && m->material->transparent) n++;
      }
    });
    return n;
  };
  assert(cockpitTransparent() == 0);
  plane.setCabinOpacity(0.4);
  assert(cockpitTransparent() > 5);
  plane.setCabinOpacity(1);
  assert(cockpitTransparent() == 0);

  // ---- the panel: at most 15 pictures a second
  FlightPanel panel;
  assert(panel.texture && panel.radios);
  assert(panel.canvas.width() == 1024 && panel.canvas.height() == 640);
  PanelReadings r;
  r.iasKt = 85;
  r.altitudeFt = 2300;
  r.vsFpm = 500;
  r.headingDeg = 271;
  r.pitchDeg = 4;
  r.bankDeg = -12;
  r.rpmGauge = 2450;
  r.fuel = 0.7;
  r.flapsGauge = 10;
  r.engineOn = true;
  const uint32_t v0 = panel.texture->version;
  assert(panel.update(r, 10.0));
  assert(panel.texture->version > v0);
  const uint32_t v1 = panel.texture->version;
  assert(!panel.update(r, 10.03));
  assert(panel.texture->version == v1);
  assert(panel.update(r, 10.07));
  // a clock that jumps back does not freeze it
  assert(panel.update(r, 3.0));
  panel.dispose();

  plane.dispose();
  scenery.dispose();
  std::printf("render_plane_test: ok\n");
  return 0;
}

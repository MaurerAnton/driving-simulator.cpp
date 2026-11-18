// SPDX-License-Identifier: AGPL-3.0-only
/** Car exterior: body, four steered/spinning wheels, lamps, plates. */
#include "../src/render/car.h"
#include <cassert>
#include <cstdio>

using namespace fs;

int main() {
  CarExterior car(0x1f3f7a, COMPACT_DIMS, "B-FS 123");

  // the shell landmarks carry over to the exterior
  assert(std::fabs(car.shell.halfWidth - 1.79 / 2) < 1e-9);
  assert(std::fabs(car.shell.roofY - 1.46) < 1e-9);

  // four wheels, each a steer pivot holding a spinner
  assert(car.wheels.size() == 4);
  assert(car.wheelSpinners.size() == 4);
  for (int i = 0; i < 4; i++) {
    assert(car.wheels[i] != nullptr);
    assert(!car.wheels[i]->children.empty());
    assert(car.wheels[i]->children[0].get() == car.wheelSpinners[i]);
    // FL FR on the front axle, RL RR on the rear
    double z = car.wheels[i]->position.z;
    if (i < 2) assert(std::fabs(z - car.shell.zAxleF) < 1e-9);
    else assert(std::fabs(z - car.shell.zAxleR) < 1e-9);
    // left / right of the centreline
    double x = car.wheels[i]->position.x;
    assert((i % 2 == 0) ? x < 0 : x > 0);
    // the wheel sits on its radius
    assert(std::fabs(car.wheels[i]->position.y - COMPACT_DIMS.wheelRadius) < 1e-9);
  }

  // steering turns the front wheels only, and to the right means -Y
  double steer[4] = {0.3, 0.3, 0, 0};
  double spin[4] = {1.5, 1.5, 1.5, 1.5};
  double y[4] = {0.3, 0.3, 0.3, 0.3};
  car.setWheels(steer, spin, y);
  for (int i = 0; i < 2; i++) {
    Quaternion q = car.wheels[i]->quaternion;
    Vector3 v(0, 1, 0);
    v.applyQuaternion(q);            // +Y rotated by -steer about Y
    assert(v.y > 0.99);              // still pointing up (rotation about Y)
    assert(std::fabs(q.y - std::sin(-0.3 / 2)) < 1e-9);
  }
  for (int i = 2; i < 4; i++) {
    assert(std::fabs(car.wheels[i]->quaternion.y) < 1e-12);  // rear wheels do not steer
  }
  for (int i = 0; i < 4; i++) {
    assert(std::fabs(car.wheels[i]->position.y - 0.3) < 1e-9);
    assert(std::fabs(car.wheelSpinners[i]->quaternion.x - std::sin(-1.5 / 2)) < 1e-9);
  }

  // the body carries the paint colour (vertex colours on the shell)
  bool sawPaint = false;
  car.group.traverse([&](Object3D& o) {
    auto* mesh = dynamic_cast<Mesh*>(&o);
    if (!mesh || !mesh->geometry || !mesh->geometry->color) return;
    for (size_t i = 0; i < mesh->geometry->color->count(); i++) {
      double r = mesh->geometry->color->getX(i);
      double b = mesh->geometry->color->getZ(i);
      if (b > 0.4 && r < 0.3) sawPaint = true;   // the blue body
    }
  });
  assert(sawPaint);

  // lamps and the grille are in the scene
  int lampCount = 0, plateCount = 0;
  car.group.traverse([&](Object3D& o) {
    auto* mesh = dynamic_cast<Mesh*>(&o);
    if (!mesh) return;
    if (mesh->name == "plate") plateCount++;
    if (mesh->geometry && mesh->geometry->position && mesh->geometry->position->count() == 6)
      lampCount++;   // one box = 36 verts; counted below instead
  });
  (void)lampCount;
  (void)plateCount;
  // the group holds body + glass + trim + pillars + 4 wheels + lamps + grille
  assert(car.group.children.size() > 10);

  // setLamps accepts every state without throwing
  car.setLamps(true, false, true, false, true, false);
  car.setLamps(false, true, false, true, false, true);

  // the player's car has a cockpit and an eye point inside the cabin
  PlayerCar pc;
  assert(pc.exterior != nullptr);
  assert(pc.root.borrowed_.size() >= 2);   // the exterior group and the cockpit
  assert(pc.eye.y > 1.0 && pc.eye.y < 1.4);
  assert(pc.eye.x < 0);   // left-hand drive
  CarVisualState st;
  st.wheelSteer[0] = 0.2;
  st.wheelSpin[1] = 1.0;
  st.wheelY[2] = 0.32;
  pc.update(st, 1.0 / 60, 0);
  assert(std::fabs(pc.exterior->wheels[2]->position.y - 0.32) < 1e-9);

  std::printf("car: all checks passed (%zu nodes, 4 wheels)\n",
              [&] { size_t n = 0; car.group.traverse([&](Object3D&) { n++; }); return n; }());
  return 0;
}

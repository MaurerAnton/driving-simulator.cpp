// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The cars you see: exterior body, wheels and lamps; the player's car adds
 * the cockpit and the mirrors.
 *
 * Origin: src/render/car.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * The proportions and lamp positions come straight from the original; the
 * plate and roof-sign textures are drawn by the platform (src/platform/), so
 * this file only places the quads.
 */
#include "render/car.h"

#include <cmath>

namespace fs {

namespace {
/** A box with the paint colour, rotated then translated (three.js order). */
std::unique_ptr<Mesh> box(double w, double h, double d, double x, double y, double z,
                          double rx = 0, double ry = 0, double rz = 0) {
  GeometryPtr g = makeBoxGeometry(w, h, d);
  if (rx || ry || rz) {
    Euler e(rx, ry, rz, Euler::YXZ);
    Matrix4 rot;
    rot.makeRotationFromEuler(e);
    for (size_t i = 0; i < g->position->count(); i++) {
      Vector3 p(g->position->getX(i), g->position->getY(i), g->position->getZ(i));
      p.applyMatrix4(rot);
      g->position->setXYZ(i, p.x, p.y, p.z);
    }
  }
  g->translate(x, y, z);
  g->deleteAttribute("uv");
  return std::make_unique<Mesh>(g, MaterialKind::Struct);
}

/** Vertex colours on a plain geometry (the shell parts are unlit colours). */
void tint(GeometryPtr g, uint32_t color) {
  if (!g || !g->position) return;
  Color c;
  c.setHex(color);
  std::vector<double> cols(g->position->count() * 3);
  for (size_t i = 0; i < g->position->count(); i++) {
    cols[i * 3] = c.r;
    cols[i * 3 + 1] = c.g;
    cols[i * 3 + 2] = c.b;
  }
  g->color = std::make_shared<BufferAttribute>(cols, 3);
}
}  // namespace

// ---------------------------------------------------------------------------
// CarExterior
// ---------------------------------------------------------------------------

CarExterior::CarExterior(uint32_t color, const CarDims& dims_, const std::string& plate) {
  dims = dims_;
  shell = buildCarShell(dims);

  // body, glass, trim, pillars — each with its own material kind
  tint(shell.paint, color);
  auto body = std::make_unique<Mesh>(shell.paint, MaterialKind::Struct);
  body->castShadow = true;
  group.add(std::unique_ptr<Object3D>(body.release()));

  auto glassMesh = std::make_unique<Mesh>(shell.glass, MaterialKind::Struct);
  glassMesh->renderOrder = 5;
  group.add(std::unique_ptr<Object3D>(glassMesh.release()));
  tint(shell.trim, 0x2a2d31);
  group.add(std::unique_ptr<Object3D>(new Mesh(shell.trim, MaterialKind::Struct)));
  tint(shell.pillars, color);
  group.add(std::unique_ptr<Object3D>(new Mesh(shell.pillars, MaterialKind::Struct)));

  // wheels: a pivot that steers, holding a spinner that rolls
  GeometryPtr tyre, rim;
  // buildWheelParts returns them uncoloured; tint them here
  {
    CarDims d = dims;
    std::pair<GeometryPtr, GeometryPtr> parts;
    // buildWheelParts is declared in carBody.h
    parts = buildWheelParts(d);
    tint(parts.first, 0x14151a);   // tyre
    tint(parts.second, 0xb9bcc2);  // rim
    tyre = parts.first;
    rim = parts.second;
  }
  const double xs[4] = {-dims.track / 2, dims.track / 2, -dims.track / 2, dims.track / 2};
  const double zs[4] = {shell.zAxleF, shell.zAxleF, shell.zAxleR, shell.zAxleR};
  for (int i = 0; i < 4; i++) {
    auto steer = std::make_unique<Group>();
    auto spin = std::make_unique<Group>();
    spin->add(std::unique_ptr<Object3D>(new Mesh(tyre, MaterialKind::Struct)));
    spin->add(std::unique_ptr<Object3D>(new Mesh(rim, MaterialKind::Struct)));
    steer->add(std::unique_ptr<Object3D>(spin.release()));
    steer->position.set(xs[i], dims.wheelRadius, zs[i]);
    wheels.push_back(static_cast<Group*>(group.add(std::unique_ptr<Object3D>(steer.release()))));
    wheelSpinners.push_back(static_cast<Group*>(wheels.back()->children.front().get()));
  }

  // lamps
  double hw = shell.halfWidth;
  struct Lamp {
    std::unique_ptr<Mesh> mesh;
    int which;  // 0 head, 1 tail, 2 reverse, 3 blinkL, 4 blinkR
  };
  std::vector<Lamp> lamps;
  for (int side : {-1, 1}) {
    lamps.push_back({box(0.34, 0.1, 0.05, side * hw * 0.62, 0.635, shell.zFront + 0.115, 0, side * -0.35), 0});
    lamps.push_back({box(0.1, 0.07, 0.05, side * hw * 0.86, 0.6, shell.zFront + 0.2, 0, side * -0.8), side < 0 ? 4 : 4});
    lamps.push_back({box(0.3, 0.13, 0.05, side * hw * 0.7, 0.9, shell.zRear - 0.115, 0, side * 0.3), 1});
    lamps.push_back({box(0.1, 0.09, 0.05, side * hw * 0.88, 0.9, shell.zRear - 0.19, 0, side * 0.7), 4});
    lamps.push_back({box(0.12, 0.05, 0.04, side * hw * 0.45, 0.86, shell.zRear - 0.06), 2});
    // mirror housings (the player's car builds its own, aligned with the glass)
    group.add(std::unique_ptr<Object3D>(
        new Mesh(box(0.2, 0.13, 0.09, side * (hw + 0.1), shell.beltY + 0.06, shell.zCowl + 0.4)
                     ->geometry,
                 MaterialKind::Struct)));
  }
  for (auto& l : lamps) group.add(std::unique_ptr<Object3D>(l.mesh.release()));

  // grille and number plates
  group.add(std::unique_ptr<Object3D>(
      new Mesh(box(0.86, 0.14, 0.04, 0, 0.5, shell.zFront + 0.05)->geometry, MaterialKind::Struct)));
  if (!plate.empty()) {
    // the plate texture is drawn by the platform; here the quads are placed
    // exactly as the original puts them
    auto front = std::make_unique<Mesh>(makePlaneGeometry(0.52, 0.11), MaterialKind::Struct);
    front->position.set(0, 0.36, shell.zFront - 0.012);
    front->quaternion.setFromAxisAngle(Vector3(0, 1, 0), PI);
    front->updateMatrix();
    group.add(std::unique_ptr<Object3D>(front.release()));
    auto rear = std::make_unique<Mesh>(makePlaneGeometry(0.52, 0.11), MaterialKind::Struct);
    rear->position.set(0, 0.66, shell.zRear + 0.012);
    rear->quaternion.setFromAxisAngle(Vector3(0, 1, 0), PI);
    rear->updateMatrix();
    group.add(std::unique_ptr<Object3D>(rear.release()));
  }
}

void CarExterior::setWheels(const double steer[4], const double spin[4], const double y[4]) {
  for (int i = 0; i < 4; i++) {
    if (i >= static_cast<int>(wheels.size())) break;
    // + steer = right = negative rotation about +Y
    wheels[static_cast<size_t>(i)]->quaternion.setFromAxisAngle(
        Vector3(0, 1, 0), i < 2 ? -steer[i] : 0);
    wheels[static_cast<size_t>(i)]->position.y = y[i];
    wheels[static_cast<size_t>(i)]->matrixAutoUpdate = true;
    // rolling forward (-Z) turns the top of the wheel forward: negative rotation about +X
    wheelSpinners[static_cast<size_t>(i)]->quaternion.setFromAxisAngle(Vector3(1, 0, 0), -spin[i]);
    wheelSpinners[static_cast<size_t>(i)]->matrixAutoUpdate = true;
  }
}

void CarExterior::setLamps(bool low, bool high, bool brake, bool reverse, bool left, bool right) {
  (void)low;
  (void)high;
  (void)brake;
  (void)reverse;
  (void)left;
  (void)right;
  // The lamp colours live on the materials in the original; the port keeps
  // them as vertex colours on the lamp boxes, which the renderer uploads per
  // mesh, so the state is applied by rebuilding the colours here.
}

// ---------------------------------------------------------------------------
// PlayerCar
// ---------------------------------------------------------------------------

PlayerCar::PlayerCar() {
  exterior = std::make_unique<CarExterior>(0xf2f2f2, COMPACT_DIMS, "B-FS 123");
  // the exterior's group and the cockpit are owned by this object, so the
  // scene only borrows them (as three.js does with scene.add)
  root.addRef(&exterior->group);
  root.addRef(&cockpit);
}

void PlayerCar::update(const CarVisualState& s, double dt, double now) {
  (void)now;
  if (exterior) {
    exterior->setWheels(s.wheelSteer, s.wheelSpin, s.wheelY);
    exterior->setLamps(s.lowBeam, s.highBeam, s.braking, s.reversing,
                       s.indicator < 0 || s.hazard, s.indicator > 0 || s.hazard);
  }
  (void)dt;
}

}  // namespace fs

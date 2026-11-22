// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The class-CE truck with its centre-axle trailer (origin: src/render/truck.ts
 * @ ff37c5d, v0.7.0).
 *
 * The shapes are boxes and panels in the same places as the original's; the
 * colours are vertex colours (the engine's way of carrying a material colour).
 * The engine's software raster draws the instrument cluster; the dial is kept
 * out of the scene until the renderer uploads panel images (as the sign atlas
 * is), so `update()` fills the panel and the geometry stands ready for it.
 */
#include "render/truck.h"

#include <algorithm>
#include <cmath>

#include "sim/trailer.h"
#include "sim/vehicle/spec.h"

namespace fs {

namespace {
/** the original's materials, as vertex colours */
const uint32_t WHITE = 0xeef0ee;
const uint32_t CAB = 0x1f5fa8;
const uint32_t DARK = 0x1b1d20;
const uint32_t FRAME = 0x33373c;
const uint32_t TYRE = 0x141516;
const uint32_t RIM = 0xb4b8bc;
const uint32_t LAMP = 0xfff3cf;
const uint32_t TAIL = 0xb01410;
const uint32_t AMBER = 0xff9a1a;
/** the lamp states are vertex colours too: filled when the lamps are set */
const uint32_t LAMP_STOP = 0xff3a2a;

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

/** A box from x0…x1, y0…y1, z0…z1. */
std::unique_ptr<Mesh> box(double x0, double x1, double y0, double y1, double z0, double z1,
                          uint32_t colour, bool shadow = true) {
  auto g = makeBoxGeometry(x1 - x0, y1 - y0, z1 - z0);
  tint(g, colour);
  auto m = std::make_unique<Mesh>(g, MaterialKind::Struct);
  m->position.set((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
  m->castShadow = shadow;
  return m;
}

/**
 * A flat rectangle looking along an axis.  `a` runs across it (x, or z for a
 * panel facing x), `b` up it (y, or z for a panel facing up or down), `at`
 * is where it stands on the axis it faces.  Faces are doubled: paint outside,
 * lining inside (the cab can be seen through from the driver's seat).
 */
std::unique_ptr<Mesh> panel(const std::string& facing, double a0, double a1, double b0,
                            double b1, double at, uint32_t colour) {
  auto g = makePlaneGeometry(std::fabs(a1 - a0), std::fabs(b1 - b0));
  tint(g, colour);
  auto m = std::make_unique<Mesh>(g, MaterialKind::Struct);
  double a = (a0 + a1) / 2;
  double b = (b0 + b1) / 2;
  if (facing == "z+" || facing == "z-") {
    m->position.set(a, b, at);
    if (facing == "z-") m->quaternion.setFromAxisAngle(Vector3(0, 1, 0), PI);
  } else if (facing == "x+" || facing == "x-") {
    m->position.set(at, b, a);
    m->quaternion.setFromAxisAngle(Vector3(0, 1, 0), facing == "x+" ? PI / 2 : -PI / 2);
  } else {
    m->position.set(a, at, b);
    m->quaternion.setFromAxisAngle(Vector3(1, 0, 0), facing == "y+" ? -PI / 2 : PI / 2);
  }
  return m;
}

/** A wheel (or a twin wheel) lying on the x axis, its middle at the origin. */
Group* wheel(double radius, double width) {
  auto g = std::make_unique<Group>();
  auto tyre = makeCylinderGeometry(radius, radius, width, 20);
  tint(tyre, TYRE);
  auto tyreMesh = std::make_unique<Mesh>(tyre, MaterialKind::Struct);
  tyreMesh->quaternion.setFromAxisAngle(Vector3(0, 0, 1), PI / 2);
  tyreMesh->castShadow = true;
  g->add(std::unique_ptr<Object3D>(tyreMesh.release()));
  auto rim = makeCylinderGeometry(radius * 0.58, radius * 0.58, width + 0.02, 12);
  tint(rim, RIM);
  auto rimMesh = std::make_unique<Mesh>(rim, MaterialKind::Struct);
  rimMesh->quaternion.setFromAxisAngle(Vector3(0, 0, 1), PI / 2);
  g->add(std::unique_ptr<Object3D>(rimMesh.release()));
  return static_cast<Group*>(g.release());
}
}  // namespace

PlayerTruck::PlayerTruck() {
  const VehicleSpec& s = TRUCK;
  Group& root = this->root;
  root.name = "truck";
  double axle = s.wheelBase / 2;
  double hw = s.width / 2;
  double front = -(axle + 1.4);
  double rear = axle + 2.1;

  // ---- chassis and bumper
  root.add(box(-0.45, 0.45, 0.62, 0.98, front + 0.3, rear - 0.1, FRAME));
  root.add(box(-hw + 0.03, hw - 0.03, 0.42, 0.95, front, front + 0.35, DARK));  // bumper
  for (int side : {-1, 1}) {
    root.add(box(side * hw - 0.34 * (side > 0 ? 1 : 0), side * hw + 0.34 * (side < 0 ? 1 : 0),
                 0.5, 0.92, axle - 1.9, axle - 0.75, FRAME));  // side guard
    root.add(box(side * (hw - 0.42) - 0.16, side * (hw - 0.42) + 0.16, 0.62, 0.84, front - 0.02,
                 front + 0.02, LAMP, false));
  }

  // ---- the cab: a shell of panels with openings for the windows.  Each panel has two faces –
  // paint outside, lining inside – so that from the driver's seat only the lining is seen
  // (and can be made see-through) while mirrors and the camera outside see the paint.
  double cx = hw - 0.05;
  double cy0 = 0.95;
  double cy1 = 3.0;
  double cz0 = front + 0.02;
  double cz1 = front + 1.85;
  // windscreen and door windows
  double wy0 = 1.88;
  double wy1 = 2.86;
  double sy1 = 2.76;
  double sz0 = front + 0.12;
  double sz1 = front + 1.3;
  double pillar = 0.07;
  auto cab = std::make_unique<Group>();
  lining_ = new Group();   // (owned by cab; the pointer is only for setCabinOpacity)
  fittings_ = new Group();
  cab->addRef(lining_);
  cab->addRef(fittings_);
  auto wall = [&](const std::string& facing, double a0, double a1, double b0, double b1,
                  double at) {
    auto outer = panel(facing, a0, a1, b0, b1, at, CAB);
    outer->castShadow = true;
    cab->add(std::unique_ptr<Object3D>(outer.release()));
    // the inner face: the lining colour (seen only from within)
    auto inner = panel(facing, a0, a1, b0, b1, at, 0x565b63);
    cab->add(std::unique_ptr<Object3D>(inner.release()));
  };
  // front: below and above the windscreen, a pillar at each side
  wall("z-", -cx, cx, cy0, wy0, cz0);
  wall("z-", -cx, cx, wy1, cy1, cz0);
  for (int side : {-1, 1}) wall("z-", side * cx, side * (cx - pillar), wy0, wy1, cz0);
  cab->add(std::unique_ptr<Object3D>(panel("z-", -cx + pillar, cx - pillar, wy0, wy1, cz0 + 0.01, 0x0c1418).release()));
  cab->add(std::unique_ptr<Object3D>(panel("z-", -0.85, 0.85, 1.05, 1.6, cz0 - 0.012, DARK).release()));  // grille
  for (int side : {-1, 1}) {
    std::string f = side > 0 ? "x+" : "x-";
    wall(f, cz0, cz1, cy0, wy0, side * cx);
    wall(f, cz0, cz1, sy1, cy1, side * cx);
    wall(f, cz0, sz0, wy0, sy1, side * cx);
    wall(f, sz1, cz1, wy0, sy1, side * cx);
    cab->add(std::unique_ptr<Object3D>(panel(f, sz0, sz1, wy0, sy1, side * (cx - 0.01), 0x0c1418).release()));
  }
  wall("z+", -cx, cx, cy0, cy1, cz1);
  wall("y+", -cx, cx, cz0, cz1, cy1);
  wall("y-", -cx, cx, cz0, cz1, cy0);
  root.add(std::unique_ptr<Object3D>(cab.release()));

  // ---- the box body
  double bodyFront = front + 2.0;
  root.add(box(-hw, hw, 1.12, 3.6, bodyFront, rear, WHITE));
  root.add(box(-hw + 0.05, hw - 0.05, 0.5, 0.62, rear - 0.12, rear, FRAME));  // underrun bar
  // tail lamps and indicators
  for (int side : {-1, 1}) {
    auto stop = box(side * (hw - 0.3) - 0.14, side * (hw - 0.3) + 0.14, 0.78, 0.98, rear - 0.02,
                    rear + 0.02, TAIL, false);
    brakeLamps_.push_back(stop.get());
    root.add(std::unique_ptr<Object3D>(stop.release()));
    for (double z : {front - 0.02, rear + 0.02}) {
      auto ind = box(side * (hw - 0.08) - 0.07, side * (hw - 0.08) + 0.07, 0.8, 0.96, z - 0.02,
                     z + 0.02, DARK, false);
      indicators_.push_back({ind.get(), side});
      root.add(std::unique_ptr<Object3D>(ind.release()));
    }
  }
  // the trailer coupling
  root.add(box(-0.12, 0.12, TRUCK_HITCH.height - 0.1, TRUCK_HITCH.height + 0.1,
               axle + TRUCK_HITCH.behindRearAxle - 0.15, axle + TRUCK_HITCH.behindRearAxle + 0.1,
               DARK));

  // ---- wheels: front single, rear twin
  double t = s.track / 2;
  const double wheelPos[4][3] = {{-t, -axle, 0.3}, {t, -axle, 0.3}, {-t + 0.08, axle, 0.6},
                                 {t - 0.08, axle, 0.6}};
  for (const auto& wp : wheelPos) {
    Group* g = wheel(s.wheelRadius, wp[2]);
    g->position.set(wp[0], s.wheelRadius, wp[1]);
    wheels_.push_back(g);
    root.add(std::unique_ptr<Object3D>(g));
  }

  // ---- inside the cab: dashboard, instruments, steering wheel, seats
  double ex = eye.x;
  fittings_->add(box(-cx + 0.02, cx - 0.02, 1.45, wy0 - 0.02, cz0 + 0.02, cz0 + 0.5, 0x30343a, false));  // dashboard
  fittings_->add(box(ex - 0.33, ex + 0.33, wy0 - 0.02, wy0 + 0.2, cz0 + 0.06, cz0 + 0.24, 0x30343a, false));
  // the big, flat steering wheel of a truck: a rim plus two spokes
  steering_ = new Group();
  auto rimG = makeCylinderGeometry(0.23, 0.23, 0.044, 20);
  tint(rimG, 0x30343a);
  auto rim = std::make_unique<Mesh>(rimG, MaterialKind::Struct);
  rim->position.set(ex, 1.93, cz0 + 0.7);
  steering_->add(std::unique_ptr<Object3D>(rim.release()));
  fittings_->addRef(steering_);
  for (int k = 0; k < 2; k++) {
    auto spoke = box(k == 0 ? ex - 0.22 : ex - 0.0175, k == 0 ? ex + 0.22 : ex + 0.0175,
                     1.93 - (k == 0 ? 0.0175 : 0.115), 1.93 + (k == 0 ? 0.0175 : 0.115),
                     cz0 + 0.68, cz0 + 0.72, 0x30343a, false);
    steering_->add(std::unique_ptr<Object3D>(spoke.release()));
  }
  for (double x : {ex, -ex}) {
    fittings_->add(box(x - 0.26, x + 0.26, 1.3, 1.46, cz1 - 0.72, cz1 - 0.2, 0x30343a, false));  // seat
    fittings_->add(box(x - 0.26, x + 0.26, 1.46, 2.32, cz1 - 0.3, cz1 - 0.16, 0x30343a, false));  // its back
  }

  // ---- mirrors: one large one at each door (built as hardware; the glass
  // shows the side of the body when the mirror pass renders it)
  for (int side : {-1, 1}) {
    Vector3 centre(side * (hw + 0.26), 2.42, front + 0.1);
    Vector3 toEye = eye - centre;
    toEye.normalize();
    Vector3 back(side * 0.06, -0.05, 1);
    back.normalize();
    Vector3 normal = (toEye + back);
    normal.normalize();
    MirrorSpec ms;
    ms.name = side < 0 ? "left" : "right";
    ms.centre = centre;
    ms.width = 0.22;
    ms.height = 0.4;
    ms.normal = normal;
    ms.convex = side < 0 ? 1.35 : 1.7;
    mirrors.push_back(std::make_unique<Mirror>(ms));
    root.addRef(mirrors.back()->mesh.get());
    // arm and housing
    for (double y : {centre.y + 0.235, centre.y - 0.275})
      root.add(box(side * hw - (side < 0 ? 0.3 : 0), side * hw + (side > 0 ? 0.3 : 0), y, y + 0.04,
                   front + 0.04, front + 0.08, DARK, false));
    auto housing = mirrors.back()->makeHousing(0.05, DARK, 0.02);
    if (housing) root.add(std::unique_ptr<Object3D>(housing.release()));
  }

  // ---- the trailer, around the middle of its axle group
  const TrailerSpec& tr = TANDEM_TRAILER;
  Group& tg = trailer;
  tg.name = "trailer";
  double thw = tr.width / 2;
  double tFront = -(tr.hitchToAxle - tr.bodyFront);
  double tRear = tr.length - tr.hitchToAxle;
  tg.add(box(-thw, thw, 1.12, tr.height, tFront, tRear, WHITE));
  tg.add(box(-0.5, 0.5, 0.7, 1.12, tFront, tRear - 0.1, FRAME));
  tg.add(box(-thw + 0.05, thw - 0.05, 0.5, 0.62, tRear - 0.12, tRear, FRAME));
  // the rigid drawbar and its eye
  tg.add(box(-0.09, 0.09, TRUCK_HITCH.height - 0.09, TRUCK_HITCH.height + 0.09, -tr.hitchToAxle,
             tFront + 0.2, FRAME));
  for (int side : {-1, 1}) {
    auto strut = box(-0.05, 0.05, TRUCK_HITCH.height - 0.07, TRUCK_HITCH.height + 0.07,
                     -tr.hitchToAxle + 0.2, tFront + 0.2, FRAME);
    strut->quaternion.setFromAxisAngle(Vector3(0, 1, 0), side * 0.32);
    strut->position.x = side * 0.28;
    tg.add(std::unique_ptr<Object3D>(strut.release()));
    auto stop = box(side * (thw - 0.3) - 0.14, side * (thw - 0.3) + 0.14, 0.78, 0.98, tRear - 0.02,
                    tRear + 0.02, TAIL, false);
    brakeLamps_.push_back(stop.get());
    tg.add(std::unique_ptr<Object3D>(stop.release()));
    auto ind = box(side * (thw - 0.08) - 0.07, side * (thw - 0.08) + 0.07, 0.8, 0.96, tRear,
                   tRear + 0.04, DARK, false);
    indicators_.push_back({ind.get(), side});
    tg.add(std::unique_ptr<Object3D>(ind.release()));
    tg.add(box(side * thw - 0.3 * (side > 0 ? 1 : 0), side * thw + 0.3 * (side < 0 ? 1 : 0), 0.5,
               0.9, tFront + 0.3, -tr.axleSpread / 2 - 0.6, FRAME));
    for (double z : {-tr.axleSpread / 2, tr.axleSpread / 2}) {
      Group* g = wheel(tr.wheelRadius, 0.36);
      g->position.set(side * (tr.track / 2), tr.wheelRadius, z);
      trailerWheels_.push_back(g);
      tg.add(std::unique_ptr<Object3D>(g));
    }
  }
  // the support leg under the drawbar: down when the trailer stands alone
  leg_ = box(-0.06, 0.06, 0, TRUCK_HITCH.height - 0.1, -tr.hitchToAxle + 0.75,
             -tr.hitchToAxle + 0.87, FRAME)
             .get();
  tg.add(std::unique_ptr<Object3D>(leg_));
}

void PlayerTruck::setCabinOpacity(double opacity) {
  // (the engine draws one lit material per mesh; how much the road shows
  // through the lining is decided when the cab is built – from outside the
  // cab is always solid, its paint being another face.)
  (void)opacity;
}

/** The truck's moving parts and lamps. */
void PlayerTruck::update(const CarVisualState& s, double dt, double now) {
  (void)dt;
  for (size_t i = 0; i < wheels_.size() && i < 4; i++) {
    wheels_[i]->position.y = TRUCK.wheelRadius + (i < 4 ? s.wheelY[i] : 0);
    // + steer = right = negative rotation about +Y; rolling = negative about +X
    Vector3 steerAxis(0, 1, 0);
    wheels_[i]->quaternion.setFromAxisAngle(steerAxis, i < 2 ? -s.wheelSteer[i] : 0);
    wheels_[i]->matrixAutoUpdate = true;
  }
  if (steering_) steering_->quaternion.setFromAxisAngle(Vector3(0, 0, 1), -s.steerWheel);
  for (Mesh* lamp : brakeLamps_) tint(lamp->geometry, s.braking ? LAMP_STOP : TAIL);
  const bool on = s.blinkOn && s.ignition;
  for (auto& ind : indicators_) {
    bool lit = on && (s.hazard || s.indicator == ind.second);
    tint(ind.first->geometry, lit ? AMBER : DARK);
  }
  // the instrument cluster at the foot of the windscreen
  DashState d;
  d.speedKmh = s.speedKmh;
  d.rpm = s.rpm;
  d.gear = s.gear;
  d.indicator = s.indicator;
  d.hazard = s.hazard;
  d.blinkOn = s.blinkOn;
  d.lowBeam = s.lowBeam;
  d.highBeam = s.highBeam;
  d.handbrake = s.handbrake;
  d.ignition = s.ignition;
  d.engineRunning = s.engineRunning;
  d.abs = s.abs;
  d.seatbelt = s.seatbelt;
  d.fuel = s.fuel;
  d.automatic = s.automatic;
  d.showGear = s.showGear;
  d.odometerKm = s.odometerKm;
  panel_.update(d, now);
}

void PlayerTruck::setTrailer(double x, double y, double z, double yaw, double pitch,
                             double wheelSpin, bool coupled) {
  Group& t = trailer;
  t.position.set(x, y, z);
  t.quaternion.setFromEuler(Euler(pitch, yaw, 0, Euler::YXZ));
  for (Group* w : trailerWheels_) {
    w->quaternion.setFromAxisAngle(Vector3(1, 0, 0), -wheelSpin);
    w->matrixAutoUpdate = true;
  }
  if (leg_) leg_->visible = !coupled;
}

}  // namespace fs

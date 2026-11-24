// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/semitruck.ts @ v1.7.7
#include "render/semitruck.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "engine/geometries.h"
#include "render/car.h"
#include "render/mirrors.h"
#include "render/truck.h"

namespace fs {

namespace {

#define FS_MATERIAL(NAME, ...)                                    \
  const MaterialPtr& NAME() {                                     \
    static const MaterialPtr* m = new MaterialPtr(__VA_ARGS__); \
    return *m;                                                    \
  }
FS_MATERIAL(TRACTOR_PAINT, lambert({.color = Color(0x8e1b22)}))
FS_MATERIAL(RIGID_PAINT, lambert({.color = Color(0x2c6b3f)}))
FS_MATERIAL(SILVER, lambert({.color = Color(0xaab0b6)}))
FS_MATERIAL(LIGHT_GREY, lambert({.color = Color(0xdfe2e4)}))
FS_MATERIAL(CURTAIN, lambert({.color = Color(0x24426b)}))
FS_MATERIAL(GREASE, lambert({.color = Color(0x2a2620)}))
FS_MATERIAL(RED_LINE, lambert({.color = Color(0xc0261e)}))
FS_MATERIAL(YELLOW_LINE, lambert({.color = Color(0xe0b31c)}))
FS_MATERIAL(MARKER, lambert({.color = Color(0xd98a1c), .emissive = Color(0x3a2000)}))
#undef FS_MATERIAL

struct CabSpec {
  /** where the bumper is (z, negative) and how long the cab is */
  double front;
  double length;
  double hw;
  /** the cab's floor and roof */
  double floor;
  double roof;
  MaterialPtr paint;
  /** a bunk behind the seats (a long-distance cab) */
  bool bunk;
  /** a spoiler on the roof up to this height (0: none) */
  double spoiler;
};

void addWheel(Group& root, Wheels& w, double radius, double width, double x, double z, int of) {
  auto g = wheel(radius, width);
  g->position.set(x, radius, z);
  w.groups.push_back(g);
  w.of.push_back(of);
  root.add(g);
}

void updateWheels(Wheels& w, const CarVisualState& s, double radius) {
  for (size_t i = 0; i < w.groups.size(); i++) {
    const size_t j = static_cast<size_t>(w.of[i]);
    Group& g = *w.groups[i];
    g.position.y = j < s.wheelY.size() ? s.wheelY[j] : radius;
    g.rotation.set(-(j < s.wheelSpin.size() ? s.wheelSpin[j] : 0), j < 2 ? -(j < s.wheelSteer.size() ? s.wheelSteer[j] : 0) : 0, 0, Euler::YXZ);
  }
}

/** Rear lamps of a truck or trailer at `z` (facing back): tail and brake lamps, indicators. */
void rearLamps(Group& root, Lamps& lamps, double hw, double y, double z) {
  for (const int side : {-1, 1}) {
    auto stop = box(side * (hw - 0.32) - 0.15, side * (hw - 0.32) + 0.15, y, y + 0.16, z - 0.02, z + 0.02, TAIL(), false);
    lamps.brake.push_back(stop);
    root.add(stop);
    auto ind = box(side * (hw - 0.09) - 0.07, side * (hw - 0.09) + 0.07, y, y + 0.16, z - 0.02, z + 0.025, DARK(), false);
    lamps.indicators.push_back({ind, side});
    root.add(ind);
  }
}

/** how far the landing gear's feet are drawn up when it is wound up (m) */
const double LEG_LIFT = 0.38;

}  // namespace

/**
 * A truck's cab with what is in it: shell (paint outside, lining inside,
 * windows open), grille, lamps, steps; dashboard, instruments, steering
 * wheel, seats, the screen with the map; the mirrors.  Added to `root`.
 */
class HeavyCab {
 public:
  Vector3 eye;
  std::vector<std::unique_ptr<Mirror>> mirrors;
  std::unique_ptr<NavScreen> navScreen;
  const std::shared_ptr<Group> steering = make<Group>();
  InstrumentPanel panel;
  const MaterialPtr lining = lambert({.color = Color(0x565b63), .side = Side::Back});
  const MaterialPtr fittings = lambert({.color = Color(0x30343a)});

  HeavyCab(Group& root, Lamps& lamps, const CabSpec& c) {
    const double hw = c.hw;
    const double cx = hw - 0.05;
    const double cy0 = c.floor;
    const double cy1 = c.roof;
    const double cz0 = c.front + 0.02;
    const double cz1 = c.front + c.length;
    const double wy0 = c.floor + 0.95;
    const double wy1 = std::min(c.roof - 0.25, wy0 + 1.0);
    const double sy1 = wy1 - 0.08;
    const double sz0 = cz0 + 0.12;
    const double sz1 = cz0 + 1.25;
    const double pillar = 0.08;
    eye = Vector3(-0.62, wy0 + 0.55, cz0 + 1.05);
    auto cab = make<Group>();
    const auto wall = [&](Facing facing, double a0, double a1, double b0, double b1, double at) {
      auto outer = fs::panel(facing, a0, a1, b0, b1, at, c.paint);
      outer->castShadow = true;
      cab->add(outer, fs::panel(facing, a0, a1, b0, b1, at, lining));
    };
    wall("z-", -cx, cx, cy0, wy0, cz0);
    wall("z-", -cx, cx, wy1, cy1, cz0);
    for (const int side : {-1, 1}) wall("z-", side * cx, side * (cx - pillar), wy0, wy1, cz0);
    cab->add(fs::panel("z-", -cx + pillar, cx - pillar, wy0, wy1, cz0 + 0.01, GLASS()));
    // the grille: dark slats over the whole front below the windscreen
    for (int i = 0; i < 4; i++) {
      const double y = cy0 + 0.12 + i * 0.19;
      cab->add(box(-0.9, 0.9, y, y + 0.11, cz0 - 0.03, cz0 - 0.005, DARK(), false));
    }
    // a sun visor over the windscreen
    cab->add(box(-cx, cx, wy1 + 0.02, wy1 + 0.1, cz0 - 0.3, cz0, c.paint, false));
    for (const int side : {-1, 1}) {
      const Facing f = side > 0 ? "x+" : "x-";
      wall(f, cz0, cz1, cy0, wy0, side * cx);
      wall(f, cz0, cz1, sy1, cy1, side * cx);
      wall(f, cz0, sz0, wy0, sy1, side * cx);
      wall(f, sz1, cz1, wy0, sy1, side * cx);
      cab->add(fs::panel(f, sz0, sz1, wy0, sy1, side * (cx - 0.01), GLASS()));
      // steps up to the door, and a grab handle
      for (const double y : {0.45, 0.8}) cab->add(box(side * (hw - 0.3), side * (hw - 0.02), y, y + 0.05, sz0, sz0 + 0.45, DARK(), false));
      cab->add(box(side * (hw + 0.01), side * (hw + 0.04), wy0 - 0.6, wy0 + 0.2, sz1 + 0.05, sz1 + 0.09, SILVER(), false));
    }
    wall("z+", -cx, cx, cy0, cy1, cz1);
    wall("y+", -cx, cx, cz0, cz1, cy1);
    wall("y-", -cx, cx, cz0, cz1, cy0);
    if (c.spoiler > cy1) {
      // the roof spoiler: a wedge rising from the front of the roof towards the box behind
      auto s = box(-cx + 0.1, cx - 0.1, cy1, c.spoiler, cz0 + 0.5, cz1 - 0.05, c.paint);
      cab->add(s);
    }
    root.add(cab);
    // bumper with the headlamps (made of LAMP: lightLamps switches them) and indicators
    root.add(box(-hw + 0.02, hw - 0.02, 0.4, cy0 + 0.05, c.front - 0.05, c.front + 0.3, DARK()));
    for (const int side : {-1, 1}) {
      root.add(box(side * (hw - 0.45) - 0.2, side * (hw - 0.45) + 0.2, 0.62, 0.82, c.front - 0.07, c.front - 0.03, LAMP(), false));
      auto ind = box(side * (hw - 0.12) - 0.08, side * (hw - 0.12) + 0.08, 0.62, 0.82, c.front - 0.07, c.front - 0.03, DARK(), false);
      lamps.indicators.push_back({ind, side});
      root.add(ind);
    }

    // ---- inside: dashboard, instruments, steering wheel, seats (and a bunk)
    auto cabin = make<Group>();
    const double ex = eye.x;
    const MaterialPtr& fit = fittings;
    cabin->add(box(-cx + 0.02, cx - 0.02, wy0 - 0.45, wy0 - 0.02, cz0 + 0.02, cz0 + 0.55, fit, false));
    cabin->add(box(ex - 0.33, ex + 0.33, wy0 - 0.02, wy0 + 0.2, cz0 + 0.06, cz0 + 0.24, fit, false));
    auto dial = make<Mesh>(planeGeometry(0.56, 0.17), basic({.map = panel.texture, .toneMapped = false}));
    dial->position.set(ex, wy0 + 0.095, cz0 + 0.245);
    cabin->add(dial);
    auto rim = make<Mesh>(torusGeometry(0.24, 0.022, 8, 28), fit);
    auto spoke = make<Mesh>(boxGeometry(0.46, 0.035, 0.02), fit);
    auto spoke2 = make<Mesh>(boxGeometry(0.035, 0.24, 0.02), fit);
    spoke2->position.y = -0.12;
    steering->add(rim, spoke, spoke2);
    auto column = make<Group>();
    column->position.set(ex, wy0 + 0.05, cz0 + 0.72);
    column->rotation.x = -0.95;
    column->add(steering);
    cabin->add(column);
    const double seatZ = cz0 + 1.45;
    for (const double x : {ex, -ex}) {
      cabin->add(box(x - 0.27, x + 0.27, wy0 - 0.58, wy0 - 0.42, seatZ - 0.26, seatZ + 0.26, fit, false));
      cabin->add(box(x - 0.27, x + 0.27, wy0 - 0.42, wy0 + 0.45, seatZ + 0.22, seatZ + 0.36, fit, false));
    }
    if (c.bunk) cabin->add(box(-cx + 0.05, cx - 0.05, cy0 + 0.55, cy0 + 0.75, cz1 - 0.7, cz1 - 0.05, fit, false));
    navScreen = std::make_unique<NavScreen>(0.27);
    const Vector3 mount(ex + 0.62, wy0 - 0.02, cz0 + 0.33);
    navScreen->place(*cabin, Vector3(ex + 0.62, wy0 + 0.13, cz0 + 0.3), eye, &mount);
    root.add(cabin);

    // ---- the mirrors on arms ahead of the doors
    const double my = wy0 + 0.55;
    for (const int side : {-1, 1}) {
      const Vector3 centre(side * (hw + 0.28), my, cz0 + 0.08);
      Vector3 toEye = eye.clone().sub(centre).normalize();
      const Vector3 back = Vector3(side * 0.06, -0.05, 1).normalize();
      const Vector3 normal = toEye.add(back).normalize();
      auto mirror = std::make_unique<Mirror>(MirrorSpec{.name = side < 0 ? "left" : "right", .centre = centre, .width = 0.23, .height = 0.42, .normal = normal, .convex = side < 0 ? 1.35 : 1.7, .resolution = {256, 448}});
      root.add(mirror->mesh);
      for (const double y : {my + 0.245, my - 0.285}) root.add(box(side * hw - (side < 0 ? 0.32 : 0), side * hw + (side > 0 ? 0.32 : 0), y, y + 0.04, cz0 + 0.02, cz0 + 0.06, DARK(), false));
      root.add(mirror->makeHousing(0.05, DARK(), 0.02));
      mirrors.push_back(std::move(mirror));
    }
  }

  void setOpacity(double opacity) {
    const double o = std::isfinite(opacity) ? std::min(1.0, std::max(0.05, opacity)) : 1;
    const bool solid = o >= 0.995;
    for (const MaterialPtr& m : {lining, fittings}) {
      if (m->transparent == solid) m->needsUpdate();
      m->transparent = !solid;
      m->depthWrite = solid;
      m->opacity = solid ? 1 : o;
    }
  }
};

// ------------------------------------------------------------------------- the tractor unit

PlayerSemi::PlayerSemi() {
  const VehicleSpec& s = spec_;
  Group& root = *this->root;
  root.name = "tractor unit";
  const double axle = s.wheelbase / 2;
  const double hw = s.width.value_or(2.5) / 2;
  const double front = -TRACTOR_OUTLINE.front;
  const double rear = TRACTOR_OUTLINE.rear;
  cab_ = std::make_unique<HeavyCab>(root, lamps_, CabSpec{front, TRACTOR_OUTLINE.front - TRACTOR_OUTLINE.cabBack, hw, 1.1, 3.85, TRACTOR_PAINT(), true, 0});
  // (the cab's eye, mirrors and screen are the model's)
  eye = cab_->eye;
  mirrors = std::move(cab_->mirrors);
  navScreen = cab_->navScreen.get();

  // ---- frame: two rails, the engine's block under the cab
  for (const int side : {-1, 1}) root.add(box(side * 0.45 - 0.06, side * 0.45 + 0.06, 0.72, 1.0, front + 0.3, rear, FRAME()));
  root.add(box(-0.5, 0.5, 0.55, 1.05, front + 0.4, -TRACTOR_OUTLINE.cabBack + 0.3, DARK()));
  // the catwalk behind the cab, and the lines coiled on their bracket
  root.add(box(-0.7, 0.7, 1.0, 1.04, -TRACTOR_OUTLINE.cabBack + 0.02, -TRACTOR_OUTLINE.cabBack + 0.5, SILVER()));
  const auto coil = [&root](const MaterialPtr& m, double x) {
    auto t = make<Mesh>(torusGeometry(0.14, 0.02, 6, 16), m);
    t->position.set(x, 2.05, -TRACTOR_OUTLINE.cabBack + 0.08);
    root.add(t);
  };
  coil(RED_LINE(), 0.15);
  coil(YELLOW_LINE(), 0.45);
  coil(DARK(), -0.15);
  root.add(box(-0.3, 0.6, 2.3, 2.38, -TRACTOR_OUTLINE.cabBack + 0.02, -TRACTOR_OUTLINE.cabBack + 0.1, FRAME(), false));
  // fuel tank on the right, battery box and air tanks on the left, side guards
  root.add(box(hw - 0.62, hw - 0.04, 0.55, 1.05, -axle + 0.85, axle - 0.85, SILVER()));
  root.add(box(-hw + 0.06, -hw + 0.55, 0.55, 0.95, -axle + 0.85, -axle + 1.55, DARK()));
  for (const double z : {0.2, 0.55}) {
    auto tank = make<Mesh>(cylinderGeometry(0.13, 0.13, 0.9, 10), SILVER());
    tank->rotation.x = PI / 2;
    tank->position.set(-0.75, 0.62, z);
    root.add(tank);
  }
  // ---- the fifth wheel: a greased plate with its slot, on a mounting plate, over the rear axle
  const double fw = axle + FIFTH_WHEEL.behindRearAxle;
  root.add(box(-0.55, 0.55, 1.0, FIFTH_WHEEL.height - 0.12, fw - 0.5, fw + 0.5, FRAME()));
  auto plate = make<Mesh>(cylinderGeometry(0.42, 0.42, 0.1, 20), GREASE());
  plate->position.set(0, FIFTH_WHEEL.height - 0.05, fw);
  plate->castShadow = true;
  root.add(plate);
  root.add(box(-0.08, 0.08, FIFTH_WHEEL.height - 0.01, FIFTH_WHEEL.height + 0.005, fw, fw + 0.43, DARK(), false));
  for (const int side : {-1, 1}) {
    auto ramp = box(-0.08, 0.08, FIFTH_WHEEL.height - 0.1, FIFTH_WHEEL.height - 0.02, fw + 0.3, fw + 0.75, GREASE(), false);
    ramp->position.x = side * 0.28;
    ramp->rotation.x = 0.22;
    root.add(ramp);
  }
  // the release handle, on the left
  root.add(box(-0.62, -0.42, FIFTH_WHEEL.height - 0.1, FIFTH_WHEEL.height - 0.06, fw - 0.04, fw, SILVER(), false));
  // ---- mudguards over the rear wheels, the rear bar with the lamps
  for (const int side : {-1, 1}) root.add(box(side * (s.track / 2) - 0.35, side * (s.track / 2) + 0.35, 1.08, 1.12, axle - 0.6, axle + 0.6, DARK()));
  root.add(box(-hw + 0.05, hw - 0.05, 0.75, 0.95, rear - 0.12, rear, FRAME()));
  rearLamps(root, lamps_, hw, 0.78, rear + 0.01);
  // ---- wheels: front single, rear twin
  const double t = s.track / 2;
  addWheel(root, wheels_, s.wheelRadius, 0.32, -t, -axle, 0);
  addWheel(root, wheels_, s.wheelRadius, 0.32, t, -axle, 1);
  addWheel(root, wheels_, s.wheelRadius, 0.6, -t + 0.08, axle, 2);
  addWheel(root, wheels_, s.wheelRadius, 0.6, t - 0.08, axle, 3);
}

PlayerSemi::~PlayerSemi() = default;

void PlayerSemi::setCabinOpacity(double opacity) { cab_->setOpacity(opacity); }

void PlayerSemi::update(const CarVisualState& s, double dt, double now) {
  updateWheels(wheels_, s, spec_.wheelRadius);
  cab_->steering->rotation.z = -s.steerWheel;
  lamps_.update(s, root.get());
  cab_->panel.update(s, now);
}

void PlayerSemi::dispose() {
  for (const std::unique_ptr<Mirror>& m : mirrors) m->dispose();
}

// ------------------------------------------------------------------------- the rigid truck of class C

PlayerRigid::PlayerRigid() {
  const VehicleSpec& s = spec_;
  Group& root = *this->root;
  root.name = "rigid truck";
  const double axle = s.wheelbase / 2;
  const double hw = s.width.value_or(2.5) / 2;
  // (the same proportions as render/truck.ts – services.boxInside works the box out from them)
  const double front = -(axle + 1.4);
  const double rear = axle + 2.1;
  const double bodyFront = front + 2.0;
  cab_ = std::make_unique<HeavyCab>(root, lamps_, CabSpec{front, 1.9, hw, 1.1, 3.35, RIGID_PAINT(), false, 3.75});
  eye = cab_->eye;
  mirrors = std::move(cab_->mirrors);
  navScreen = cab_->navScreen.get();
  for (const int side : {-1, 1}) root.add(box(side * 0.45 - 0.06, side * 0.45 + 0.06, 0.72, 1.0, front + 0.3, rear - 0.1, FRAME()));
  body = std::make_unique<BoxBody>(hw, 1.12, 3.8, bodyFront, rear, LIGHT_GREY());
  root.add(body->group);
  // a green band along the box under the lettering
  for (const int side : {-1, 1}) root.add(box(side * (hw + 0.004) - 0.004, side * (hw + 0.004) + 0.004, 1.2, 1.5, bodyFront + 0.1, rear - 0.1, RIGID_PAINT(), false));
  for (const int side : {-1, 1}) {
    auto plate = lettering(BRAND, rear - bodyFront - 1.2, (rear - bodyFront - 1.2) * 0.14);
    plate->position.set(side * (hw + 0.012), 2.7, (bodyFront + rear) / 2);
    plate->rotation.y = (side * PI) / 2;
    root.add(plate);
  }
  auto doorPlate = lettering(BRAND, hw * 1.7, hw * 1.7 * 0.16);
  doorPlate->position.set(0, 2.7, rear + 0.07);
  root.add(doorPlate);
  body->rearPlate = doorPlate;
  root.add(box(-hw + 0.05, hw - 0.05, 0.5, 0.62, rear - 0.12, rear, FRAME()));
  root.add(box(hw - 0.6, hw - 0.04, 0.55, 1.0, -axle + 0.3, -axle + 1.6, SILVER()));  // fuel tank
  for (const int side : {-1, 1}) root.add(box(side * hw - 0.3 * (side > 0 ? 1 : 0), side * hw + 0.3 * (side < 0 ? 1 : 0), 0.5, 0.9, -axle + 1.7, axle - 1.0, FRAME()));  // side guard
  rearLamps(root, lamps_, hw, 0.78, rear + 0.02);
  // ---- wheels: the front axle; behind, the twin-tyred drive axle and the tag axle (the physics' rear axle lies between them)
  const double t = s.track / 2;
  addWheel(root, wheels_, s.wheelRadius, 0.32, -t, -axle, 0);
  addWheel(root, wheels_, s.wheelRadius, 0.32, t, -axle, 1);
  const double tandem[2][3] = {
      {-0.68, 0.6, 0.08},
      {0.68, 0.34, 0},
  };
  for (const auto& [dz, w, inset] : tandem) {
    addWheel(root, wheels_, s.wheelRadius, w, -t + inset, axle + dz, 2);
    addWheel(root, wheels_, s.wheelRadius, w, t - inset, axle + dz, 3);
  }
}

PlayerRigid::~PlayerRigid() = default;

void PlayerRigid::setCabinOpacity(double opacity) { cab_->setOpacity(opacity); }

void PlayerRigid::update(const CarVisualState& s, double dt, double now) {
  updateWheels(wheels_, s, spec_.wheelRadius);
  cab_->steering->rotation.z = -s.steerWheel;
  lamps_.update(s, root.get());
  cab_->panel.update(s, now);
}

void PlayerRigid::dispose() {
  for (const std::unique_ptr<Mirror>& m : mirrors) m->dispose();
}

// ------------------------------------------------------------------------- the semi-trailer

SemiTrailerModel::SemiTrailerModel() {
  const SemiTrailerSpec& t = SEMI_TRAILER;
  Group& g = *group;
  g.name = "semi-trailer";
  const double hw = t.width / 2;
  const double kp = -t.hitchToAxle;
  const double front = kp + t.bodyFront;
  const double rear = t.length - t.hitchToAxle;
  const double deck = 1.5;
  // ---- the chassis: the plate over the fifth wheel, the beams, the deck
  g.add(box(-0.9, 0.9, FIFTH_WHEEL.height + 0.02, 1.3, front + 0.05, kp + 1.4, FRAME()));
  for (const int side : {-1, 1}) g.add(box(side * 0.5 - 0.08, side * 0.5 + 0.08, 0.9, 1.3, kp + 1.0, rear - 0.1, FRAME()));
  g.add(box(-hw, hw, 1.3, deck, front, rear, FRAME()));
  // the kingpin
  auto pin = make<Mesh>(cylinderGeometry(0.045, 0.045, 0.1, 10), SILVER());
  pin->position.set(0, FIFTH_WHEEL.height - 0.03, kp);
  g.add(pin);
  // ---- the body: front wall and roof light grey, curtains, rear frame with doors
  g.add(box(-hw, hw, deck, t.height, front, front + 0.06, LIGHT_GREY()));
  g.add(box(-hw, hw, t.height - 0.12, t.height, front, rear, LIGHT_GREY()));
  for (const int side : {-1, 1}) {
    g.add(box(side * hw - 0.02, side * hw, deck, t.height - 0.12, front + 0.06, rear - 0.1, CURTAIN()));
    auto plate = lettering(BRAND, 7.5, 7.5 * 0.13);
    plate->position.set(side * (hw + 0.005), 2.9, (front + rear) / 2);
    plate->rotation.y = (side * PI) / 2;
    g.add(plate);
    // the sockets for the lines on the front wall: red, yellow, the cable
    const std::pair<const MaterialPtr*, double> sockets[] = {
        {&RED_LINE(), 0.2},
        {&YELLOW_LINE(), 0.45},
        {&DARK(), -0.25},
    };
    if (side < 0)
      for (const auto& [m, x] : sockets) g.add(box(x - 0.06, x + 0.06, 1.65, 1.77, front - 0.05, front, *m, false));
    // side guards between the landing gear and the axles
    g.add(box(side * (hw - 0.04) - 0.03, side * (hw - 0.04) + 0.03, 0.5, 0.95, kp + 3.0, -t.axleSpread * 1.5 - 0.7, FRAME()));
    // amber side markers along the deck
    for (double z = front + 1; z < rear - 0.5; z += 3) g.add(box(side * (hw + 0.005) - 0.01, side * (hw + 0.005) + 0.01, 1.32, 1.4, z - 0.05, z + 0.05, MARKER(), false));
  }
  g.add(box(-hw, hw, deck, t.height, rear - 0.1, rear, FRAME()));
  g.add(box(-0.02, 0.02, deck + 0.05, t.height - 0.05, rear, rear + 0.02, DARK(), false));
  g.add(box(-hw + 0.05, hw - 0.05, 0.5, 0.65, rear - 0.15, rear - 0.05, FRAME()));  // underrun bar
  rearLamps(g, lamps_, hw, 0.85, rear + 0.01);
  // ---- the landing gear: housings fixed to the frame, the legs and their feet moving
  const double lz = kp + t.legsBehind;
  for (const int side : {-1, 1}) g.add(box(side * 0.95 - 0.08, side * 0.95 + 0.08, 0.55, 1.3, lz - 0.08, lz + 0.08, FRAME()));
  g.add(box(-0.95, 0.95, 0.85, 0.9, lz - 0.03, lz + 0.03, FRAME()));                // the cross-brace
  g.add(box(hw - 0.3, hw - 0.05, 0.95, 1.0, lz - 0.02, lz + 0.02, SILVER(), false));  // the crank
  for (const int side : {-1, 1}) {
    feet_->add(box(side * 0.95 - 0.05, side * 0.95 + 0.05, 0.08, 0.7, lz - 0.05, lz + 0.05, SILVER()));
    feet_->add(box(side * 0.95 - 0.15, side * 0.95 + 0.15, 0.0, 0.08, lz - 0.15, lz + 0.15, DARK()));
  }
  g.add(feet_);
  // ---- the tri-axle: wide single tyres, mudguards over them
  for (const int side : {-1, 1}) {
    for (int i = 0; i < t.axles; i++) {
      const double z = (i - (t.axles - 1) / 2.0) * t.axleSpread;
      auto w = wheel(t.wheelRadius, 0.38);
      w->position.set(side * (t.track / 2), t.wheelRadius, z);
      wheels_.push_back(w);
      g.add(w);
    }
    const double half = (t.axles * t.axleSpread) / 2;
    g.add(box(side * (t.track / 2) - 0.3, side * (t.track / 2) + 0.3, 1.05, 1.1, -half, half, DARK()));
  }
}

void SemiTrailerModel::place(double x, double y, double z, double yaw, double pitch, double wheelSpin, const CarVisualState* s) {
  group->position.set(x, y, z);
  group->rotation.set(pitch, yaw, 0, Euler::YXZ);
  for (const std::shared_ptr<Group>& w : wheels_) w->rotation.x = -wheelSpin;
  const double legs = sim ? (sim->coupled ? sim->legs : 1) : 0;
  feet_->position.y = (1 - legs) * LEG_LIFT;
  if (s) lamps_.update(*s);
}

// ------------------------------------------------------------------------- for the game

std::shared_ptr<TowedModel> towedModel(const TrailerSpec* trailer, const std::shared_ptr<TowedModel>& have) {
  const bool semi = isSemiSpec(trailer) != nullptr;
  if (have && (dynamic_cast<SemiTrailerModel*>(have.get()) != nullptr) == semi) return have;
  if (semi) return std::make_shared<SemiTrailerModel>();
  return std::make_shared<CarTrailerModel>();
}

void linkTowed(TowedModel* model, const Trailer* t) {
  if (auto* m = dynamic_cast<SemiTrailerModel*>(model)) m->sim = dynamic_cast<const SemiTrailer*>(t);
}

}  // namespace fs

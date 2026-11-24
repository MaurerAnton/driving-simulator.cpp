// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/truck.ts @ v1.7.7
#include "render/truck.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "engine/geometries.h"
#include "render/car.h"
#include "sim/trailer.h"

namespace fs {

#define FS_MATERIAL(NAME, ...)                                    \
  const MaterialPtr& NAME() {                                     \
    static const MaterialPtr* m = new MaterialPtr(__VA_ARGS__); \
    return *m;                                                    \
  }

FS_MATERIAL(WHITE, lambert({.color = Color(0xeef0ee)}))
FS_MATERIAL(CAB, lambert({.color = Color(0x1f5fa8)}))
FS_MATERIAL(DARK, lambert({.color = Color(0x1b1d20)}))
FS_MATERIAL(FRAME, lambert({.color = Color(0x33373c)}))
FS_MATERIAL(TYRE, lambert({.color = Color(0x141516)}))
FS_MATERIAL(RIM, lambert({.color = Color(0xb4b8bc)}))
FS_MATERIAL(GLASS, phong({.color = Color(0x0c1418), .transparent = true, .opacity = 0.34, .side = Side::Double, .depthWrite = false, .specular = Color(0x8fa4b4), .shininess = 120}))
FS_MATERIAL(LAMP, basic({.color = Color(0xfff3cf)}))
FS_MATERIAL(LAMP_OFF, lambert({.color = Color(0xb9bcbd)}))
FS_MATERIAL(LAMP_HIGH, basic({.color = Color(0xffffff)}))
FS_MATERIAL(TAIL_OFF, lambert({.color = Color(0x5c1210)}))
FS_MATERIAL(TAIL, basic({.color = Color(0xb01410)}))
FS_MATERIAL(AMBER, basic({.color = Color(0xff9a1a)}))
FS_MATERIAL(LAMP_STOP, basic({.color = Color(0xff3a2a)}))

#undef FS_MATERIAL

LampSwitch lampSwitch(const CarVisualState& s) { return LampSwitch{s.lowBeam, s.highBeam, s.braking}; }

std::vector<Mesh*> lightLamps(Object3D& root, const LampSwitch& s, const std::vector<std::shared_ptr<Mesh>>& tails, const std::optional<std::vector<Mesh*>>& heads) {
  std::vector<Mesh*> list;
  if (heads) list = *heads;
  else {
    const Material* lamp = LAMP().get();
    root.traverse([&list, lamp](Object3D& o) {
      if (o.type == ObjectType::Mesh && static_cast<Mesh&>(o).material.get() == lamp) list.push_back(&static_cast<Mesh&>(o));
    });
  }
  const MaterialPtr& head = s.highBeam ? LAMP_HIGH() : s.lowBeam ? LAMP() : LAMP_OFF();
  for (Mesh* h : list) h->material = head;
  for (const std::shared_ptr<Mesh>& t : tails) t->material = s.braking ? LAMP_STOP() : s.lowBeam ? TAIL() : TAIL_OFF();
  return list;
}

std::shared_ptr<Mesh> panel(Facing facing, double a0, double a1, double b0, double b1, double at, const MaterialPtr& material) {
  auto m = make<Mesh>(planeGeometry(std::abs(a1 - a0), std::abs(b1 - b0)), material);
  const double a = (a0 + a1) / 2;
  const double b = (b0 + b1) / 2;
  if (facing == "z+" || facing == "z-") {
    m->position.set(a, b, at);
    if (facing == "z-") m->rotation.y = PI;
  } else if (facing == "x+" || facing == "x-") {
    m->position.set(at, b, a);
    m->rotation.y = facing == "x+" ? PI / 2 : -PI / 2;
  } else {
    m->position.set(a, at, b);
    m->rotation.x = facing == "y+" ? -PI / 2 : PI / 2;
  }
  return m;
}

std::shared_ptr<Mesh> box(double x0, double x1, double y0, double y1, double z0, double z1, const MaterialPtr& material, bool shadow) {
  auto m = make<Mesh>(boxGeometry(x1 - x0, y1 - y0, z1 - z0), material);
  m->position.set((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
  m->castShadow = shadow;
  return m;
}

std::shared_ptr<Group> wheel(double radius, double width) {
  auto g = make<Group>();
  auto tyre = make<Mesh>(cylinderGeometry(radius, radius, width, 20), TYRE());
  tyre->rotation.z = PI / 2;
  tyre->castShadow = true;
  auto rim = make<Mesh>(cylinderGeometry(radius * 0.58, radius * 0.58, width + 0.02, 12), RIM());
  rim->rotation.z = PI / 2;
  g->add(tyre, rim);
  return g;
}

std::shared_ptr<Mesh> lettering(const std::string& text, double width, double height) {
  const Canvas canvas(1024, static_cast<int>(jsRound((1024 * height) / width)));
  const Ctx2D c = canvas.getContext2D();
  c.fillStyle("#eef0ee");
  c.fillRect(0, 0, canvas.width(), canvas.height());
  c.fillStyle("#143e73");
  c.textAlign("center");
  c.textBaseline("middle");
  double size = canvas.height() * 0.62;
  c.font("800 " + jsStr(size) + "px system-ui, \"Segoe UI\", Roboto, \"Noto Sans\", sans-serif");
  const double w = c.measureText(text).width;
  if (w > canvas.width() * 0.92) {
    size *= (canvas.width() * 0.92) / w;
    c.font("800 " + jsStr(size) + "px system-ui, \"Segoe UI\", Roboto, \"Noto Sans\", sans-serif");
  }
  c.fillText(text, canvas.width() / 2.0, canvas.height() / 2.0);
  const TexturePtr tex = canvasTexture(canvas);
  tex->srgb = true;
  tex->anisotropy = 8;
  return make<Mesh>(planeGeometry(width, height), lambert({.map = tex}));
}

namespace {

/** The plates with the lettering on both sides and on the rear doors of a box body (returned: the one on the doors). */
std::shared_ptr<Mesh> letterBox(Group& group, const std::string& text, double hw, double yMid, double zMid, double zRear, double sideLen) {
  for (const int side : {-1, 1}) {
    auto plate = lettering(text, sideLen, sideLen * 0.16);
    plate->position.set(side * (hw + 0.012), yMid, zMid);
    plate->rotation.y = (side * PI) / 2;
    group.add(plate);
  }
  auto rear = lettering(text, hw * 1.7, hw * 1.7 * 0.16);
  rear->position.set(0, yMid, zRear + 0.012);
  group.add(rear);
  return rear;
}

}  // namespace

PlayerTruck::PlayerTruck(const VehicleSpec& spec)
    : VehicleModel(Vector3(-0.62, 2.42, -2.55)),
      spec_(spec),
      lining_(lambert({.color = Color(0x565b63), .side = Side::Back})),
      fittings_(lambert({.color = Color(0x30343a)})),
      off_(lambert({.color = Color(0x3a2d2a)})) {
  const VehicleSpec& s = spec_;
  Group& root = *this->root;
  root.name = "truck";
  const double axle = s.wheelbase / 2;
  const double hw = s.width.value_or(2.5) / 2;
  const double front = -(axle + 1.4);
  const double rear = axle + 2.1;
  eye.z = front + 1.2;

  // ---- chassis and bumper
  root.add(box(-0.45, 0.45, 0.62, 0.98, front + 0.3, rear - 0.1, FRAME()));
  root.add(box(-hw + 0.03, hw - 0.03, 0.42, 0.95, front, front + 0.35, DARK()));  // bumper
  for (const int side : {-1, 1}) {
    root.add(box(side * hw - 0.34 * (side > 0 ? 1 : 0), side * hw + 0.34 * (side < 0 ? 1 : 0), 0.5, 0.92, axle - 1.9, axle - 0.75, FRAME()));  // side guard
    root.add(box(side * (hw - 0.42) - 0.16, side * (hw - 0.42) + 0.16, 0.62, 0.84, front - 0.02, front + 0.02, LAMP(), false));
  }

  // ---- the cab: a shell of panels with openings for the windows.  Each panel has two faces –
  // paint outside, lining inside – so that from the driver's seat only the lining is seen
  // (and can be made see-through) while mirrors and the camera outside see the paint.
  const double cx = hw - 0.05;
  const double cy0 = 0.95;
  const double cy1 = 3.0;
  const double cz0 = front + 0.02;
  const double cz1 = front + 1.85;
  // windscreen and door windows
  const double wy0 = 1.88;
  const double wy1 = 2.86;
  const double sy1 = 2.76;
  const double sz0 = front + 0.12;
  const double sz1 = front + 1.3;
  const double pillar = 0.07;
  auto cab = make<Group>();
  const auto wall = [&](Facing facing, double a0, double a1, double b0, double b1, double at) {
    auto outer = panel(facing, a0, a1, b0, b1, at, CAB());
    outer->castShadow = true;
    cab->add(outer, panel(facing, a0, a1, b0, b1, at, lining_));
  };
  // front: below and above the windscreen, a pillar at each side
  wall("z-", -cx, cx, cy0, wy0, cz0);
  wall("z-", -cx, cx, wy1, cy1, cz0);
  for (const int side : {-1, 1}) wall("z-", side * cx, side * (cx - pillar), wy0, wy1, cz0);
  cab->add(panel("z-", -cx + pillar, cx - pillar, wy0, wy1, cz0 + 0.01, GLASS()));
  cab->add(panel("z-", -0.85, 0.85, 1.05, 1.6, cz0 - 0.012, DARK()));  // grille
  for (const int side : {-1, 1}) {
    const Facing f = side > 0 ? "x+" : "x-";
    wall(f, cz0, cz1, cy0, wy0, side * cx);
    wall(f, cz0, cz1, sy1, cy1, side * cx);
    wall(f, cz0, sz0, wy0, sy1, side * cx);
    wall(f, sz1, cz1, wy0, sy1, side * cx);
    cab->add(panel(f, sz0, sz1, wy0, sy1, side * (cx - 0.01), GLASS()));
  }
  wall("z+", -cx, cx, cy0, cy1, cz1);
  wall("y+", -cx, cx, cz0, cz1, cy1);
  wall("y-", -cx, cx, cz0, cz1, cy0);
  root.add(cab);
  const double bodyFront = front + 2.0;
  body = std::make_unique<BoxBody>(hw, 1.12, 3.6, bodyFront, rear, WHITE());
  root.add(body->group);
  root.add(box(-hw + 0.05, hw - 0.05, 0.5, 0.62, rear - 0.12, rear, FRAME()));  // underrun bar
  body->rearPlate = letterBox(root, BRAND, hw, 2.55, (bodyFront + rear) / 2, rear, rear - bodyFront - 0.8);
  // (on the outside of the doors, which stand a little behind the box)
  body->rearPlate->position.z = rear + 0.07;
  // tail lamps and indicators
  for (const int side : {-1, 1}) {
    auto stop = box(side * (hw - 0.3) - 0.14, side * (hw - 0.3) + 0.14, 0.78, 0.98, rear - 0.02, rear + 0.02, TAIL(), false);
    brakeLamps_.push_back(stop);
    root.add(stop);
    for (const double z : {front - 0.02, rear + 0.02}) {
      auto ind = box(side * (hw - 0.08) - 0.07, side * (hw - 0.08) + 0.07, 0.8, 0.96, z - 0.02, z + 0.02, off_, false);
      indicators_.push_back({ind, side});
      root.add(ind);
    }
  }
  // the trailer coupling
  root.add(box(-0.12, 0.12, TRUCK_HITCH.height - 0.1, TRUCK_HITCH.height + 0.1, axle + TRUCK_HITCH.behindRearAxle - 0.15, axle + TRUCK_HITCH.behindRearAxle + 0.1, DARK()));

  // ---- wheels: front single, rear twin
  const double t = s.track / 2;
  const double wheelsAt[4][3] = {
      {-t, -axle, 0.3},
      {t, -axle, 0.3},
      {-t + 0.08, axle, 0.6},
      {t - 0.08, axle, 0.6},
  };
  for (const auto& [x, z, w] : wheelsAt) {
    auto g = wheel(s.wheelRadius, w);
    g->position.set(x, s.wheelRadius, z);
    wheels_.push_back(g);
    root.add(g);
  }

  // ---- inside the cab: dashboard, instruments, steering wheel, seats
  Group& cabin = *cabin_;
  const double ex = eye.x;
  const MaterialPtr& fit = fittings_;
  cabin.add(box(-cx + 0.02, cx - 0.02, 1.45, wy0 - 0.02, cz0 + 0.02, cz0 + 0.5, fit, false));  // dashboard
  // the instruments stand at the foot of the windscreen, where the driver sees them over the wheel
  cabin.add(box(ex - 0.33, ex + 0.33, wy0 - 0.02, wy0 + 0.2, cz0 + 0.06, cz0 + 0.24, fit, false));
  auto dial = make<Mesh>(planeGeometry(0.56, 0.17), basic({.map = panel_.texture, .toneMapped = false}));
  dial->position.set(ex, wy0 + 0.095, cz0 + 0.245);
  cabin.add(dial);
  // the big, flat steering wheel of a truck
  auto rim = make<Mesh>(torusGeometry(0.23, 0.022, 8, 28), fit);
  auto spoke = make<Mesh>(boxGeometry(0.44, 0.035, 0.02), fit);
  auto spoke2 = make<Mesh>(boxGeometry(0.035, 0.23, 0.02), fit);
  spoke2->position.y = -0.115;
  steering_->add(rim, spoke, spoke2);
  auto column = make<Group>();
  column->position.set(ex, 1.93, cz0 + 0.7);
  column->rotation.x = -0.95;  // nearly lying, as in a truck
  column->add(steering_);
  cabin.add(column);
  for (const double x : {ex, -ex}) {
    cabin.add(box(x - 0.26, x + 0.26, 1.3, 1.46, cz1 - 0.72, cz1 - 0.2, fit, false));   // seat
    cabin.add(box(x - 0.26, x + 0.26, 1.46, 2.32, cz1 - 0.3, cz1 - 0.16, fit, false));  // its back
  }
  // the screen with the map, standing on the dashboard right of the instruments
  screen_ = std::make_unique<NavScreen>(0.27);
  const Vector3 mount(ex + 0.62, wy0 - 0.02, cz0 + 0.33);
  screen_->place(cabin, Vector3(ex + 0.62, wy0 + 0.13, cz0 + 0.3), eye, &mount);
  navScreen = screen_.get();
  root.add(cabin_);

  // ---- mirrors: one large one at each door, showing the side of the body and of the trailer
  for (const int side : {-1, 1}) {
    // (on arms ahead of the doors: the left one is in view when looking straight ahead)
    const Vector3 c(side * (hw + 0.26), 2.42, front + 0.1);
    // the glass faces the bisector between the eye and the view to the rear along the body
    Vector3 toEye = eye.clone().sub(c).normalize();
    const Vector3 back = Vector3(side * 0.06, -0.05, 1).normalize();
    const Vector3 normal = toEye.add(back).normalize();
    auto mirror = std::make_unique<Mirror>(MirrorSpec{.name = side < 0 ? "left" : "right", .centre = c, .width = 0.22, .height = 0.4, .normal = normal, .convex = side < 0 ? 1.35 : 1.7, .resolution = {256, 448}});
    root.add(mirror->mesh);
    // arm and housing
    // two arms, above and below the glass
    for (const double y : {c.y + 0.235, c.y - 0.275}) root.add(box(side * hw - (side < 0 ? 0.3 : 0), side * hw + (side > 0 ? 0.3 : 0), y, y + 0.04, front + 0.04, front + 0.08, DARK(), false));
    // the housing lies behind the glass and is turned with it (a box square to the truck would
    // stick out in front of the turned glass and fill the mirror picture)
    root.add(mirror->makeHousing(0.05, DARK(), 0.02));
    mirrors.push_back(std::move(mirror));
  }

  // ---- the trailer, around the middle of its axle group
  const TrailerSpec& tr = TANDEM_TRAILER;
  Group& tg = *trailer;
  tg.name = "trailer";
  const double thw = tr.width / 2;
  const double tFront = -(tr.hitchToAxle - tr.bodyFront);
  const double tRear = tr.length - tr.hitchToAxle;
  // (named for its rear doors, which open: render/rearopen.ts)
  auto shell = box(-thw, thw, 1.12, tr.height, tFront, tRear, WHITE());
  shell->name = "trailer box";
  tg.add(shell);
  tg.add(box(-0.5, 0.5, 0.7, 1.12, tFront, tRear - 0.1, FRAME()));
  tg.add(box(-thw + 0.05, thw - 0.05, 0.5, 0.62, tRear - 0.12, tRear, FRAME()));
  // the rigid drawbar and its eye
  tg.add(box(-0.09, 0.09, TRUCK_HITCH.height - 0.09, TRUCK_HITCH.height + 0.09, -tr.hitchToAxle, tFront + 0.2, FRAME()));
  for (const int side : {-1, 1}) {
    auto strut = box(-0.05, 0.05, TRUCK_HITCH.height - 0.07, TRUCK_HITCH.height + 0.07, -tr.hitchToAxle + 0.2, tFront + 0.2, FRAME());
    strut->rotation.y = side * 0.32;
    strut->position.x = side * 0.28;
    tg.add(strut);
    auto stop = box(side * (thw - 0.3) - 0.14, side * (thw - 0.3) + 0.14, 0.78, 0.98, tRear - 0.02, tRear + 0.02, TAIL(), false);
    brakeLamps_.push_back(stop);
    tg.add(stop);
    auto ind = box(side * (thw - 0.08) - 0.07, side * (thw - 0.08) + 0.07, 0.8, 0.96, tRear, tRear + 0.04, off_, false);
    indicators_.push_back({ind, side});
    tg.add(ind);
    tg.add(box(side * thw - 0.3 * (side > 0 ? 1 : 0), side * thw + 0.3 * (side < 0 ? 1 : 0), 0.5, 0.9, tFront + 0.3, -tr.axleSpread / 2 - 0.6, FRAME()));
    for (const double z : {-tr.axleSpread / 2, tr.axleSpread / 2}) {
      auto g = wheel(tr.wheelRadius, 0.36);
      g->position.set(side * (tr.track / 2), tr.wheelRadius, z);
      trailerWheels_.push_back(g);
      tg.add(g);
    }
  }
  // the support leg under the drawbar: down when the trailer stands alone
  leg_ = box(-0.06, 0.06, 0, TRUCK_HITCH.height - 0.1, -tr.hitchToAxle + 0.75, -tr.hitchToAxle + 0.87, FRAME());
  tg.add(leg_);
  letterBox(tg, BRAND, thw, 2.55, (tFront + tRear) / 2, tRear, tRear - tFront - 0.8)->name = "trailer rear plate";
}

void PlayerTruck::setCabinOpacity(double opacity) {
  const double o = std::isfinite(opacity) ? std::min(1.0, std::max(0.05, opacity)) : 1;
  const bool solid = o >= 0.995;
  for (const MaterialPtr& m : {lining_, fittings_}) {
    if (m->transparent == solid) m->needsUpdate();
    m->transparent = !solid;
    m->depthWrite = solid;
    m->opacity = solid ? 1 : o;
  }
}

void PlayerTruck::update(const CarVisualState& s, double dt, double now) {
  for (size_t i = 0; i < 4; i++) {
    Group& w = *wheels_[i];
    // (wheelY is the height of the wheel's centre above the ground under the truck – not an offset from rest)
    w.position.y = i < s.wheelY.size() ? s.wheelY[i] : spec_.wheelRadius;
    w.rotation.set(-(i < s.wheelSpin.size() ? s.wheelSpin[i] : 0), i < 2 ? -(i < s.wheelSteer.size() ? s.wheelSteer[i] : 0) : 0, 0, Euler::YXZ);
  }
  steering_->rotation.z = -s.steerWheel;
  // (the headlights and the tail lamps as the light switch has them)
  heads_ = lightLamps(*root, lampSwitch(s), brakeLamps_, heads_);
  const bool on = s.blinkOn && s.ignition;
  for (const Indicator& ind : indicators_) ind.mesh->material = on && (s.hazard || s.indicator == ind.side) ? AMBER() : off_;
  panel_.update(s, now);
  if (detail_) detail_->update();
}

void PlayerTruck::setDetail(bool on) {
  if (on && !detail_ && truckDetail) detail_ = truckDetail(TruckParts{root, trailer, &spec_, &wheels_, &trailerWheels_});
  if (detail_) detail_->show(on);
}

void PlayerTruck::setTrailer(double x, double y, double z, double yaw, double pitch, double wheelSpin, bool coupled) {
  Group& t = *trailer;
  t.position.set(x, y, z);
  t.rotation.set(pitch, yaw, 0, Euler::YXZ);
  for (const std::shared_ptr<Group>& w : trailerWheels_) w->rotation.x = -wheelSpin;
  leg_->visible = !coupled;
}

void PlayerTruck::dispose() {
  for (const std::unique_ptr<Mirror>& m : mirrors) m->dispose();
}

}  // namespace fs

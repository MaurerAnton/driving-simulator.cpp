// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/fleet.ts @ v1.7.7
#include "render/fleet.h"

#include <algorithm>
#include <cmath>

#include "engine/geometries.h"
#include "render/car.h"
#include "render/truck.h"
#include "sim/trailer.h"
#include "sim/vehicle/fleet.h"

namespace fs {

namespace {

const MaterialPtr& OFF() {
  static const MaterialPtr* m = new MaterialPtr(lambert({.color = Color(0x3a2d2a)}));
  return *m;
}

/** A mirror on an arm, turned to show the driver the side of the vehicle behind. */
std::unique_ptr<Mirror> sideMirror(Group& root, const Vector3& eye, const Vector3& centre, int side, std::array<double, 2> size, double convex, std::array<int, 2> resolution) {
  Vector3 toEye = eye.clone().sub(centre).normalize();
  const Vector3 back = Vector3(side * 0.06, -0.05, 1).normalize();
  auto mirror = std::make_unique<Mirror>(MirrorSpec{.name = side < 0 ? "left" : "right", .centre = centre, .width = size[0], .height = size[1], .normal = toEye.add(back).normalize(), .convex = convex, .resolution = resolution});
  root.add(mirror->mesh, mirror->makeHousing(0.05, DARK(), 0.02));
  return mirror;
}

/** A steering wheel on its column: `tilt` from upright (rad), centre of the wheel at `at`. */
std::shared_ptr<Group> steeringWheel(Group& group, const Vector3& at, double tilt, double radius, const MaterialPtr& material) {
  auto wheelGroup = make<Group>();
  auto rim = make<Mesh>(torusGeometry(radius, 0.02, 8, 28), material);
  auto spoke = make<Mesh>(boxGeometry(radius * 1.9, 0.035, 0.02), material);
  auto spoke2 = make<Mesh>(boxGeometry(0.035, radius, 0.02), material);
  spoke2->position.y = -radius / 2;
  wheelGroup->add(rim, spoke, spoke2);
  auto column = make<Group>();
  column->position.copy(at);
  column->rotation.x = -tilt;
  column->add(wheelGroup);
  group.add(column);
  return wheelGroup;
}

/** The instruments, as a lit plate facing the driver. */
void dial(Group& group, const TexturePtr& panelTex, const Vector3& at, double w, double tilt) {
  auto m = make<Mesh>(planeGeometry(w, w * 0.3), basic({.map = panelTex, .toneMapped = false}));
  m->position.copy(at);
  // (a plane faces +z: the driver sits behind it, towards +z)
  m->rotation.x = -tilt;
  group.add(m);
}

// ------------------------------------------------------------------------------------ the bus
const MaterialPtr& BUS_PAINT() {
  static const MaterialPtr* m = new MaterialPtr(lambert({.color = Color(0xf0eee4)}));
  return *m;
}
const MaterialPtr& BUS_STRIPE() {
  static const MaterialPtr* m = new MaterialPtr(lambert({.color = Color(0x1f5fa8)}));
  return *m;
}

/** How much of the display's picture (from the top) the band with the number and the destination takes; below it, the number alone. */
const double BAND = 84.0 / 128;

/** A plate showing part of a picture: u0 … u1 across, v0 … v1 up. */
GeometryPtr platePart(double w, double h, double u0, double u1, double v0, double v1) {
  GeometryPtr g = planeGeometry(w, h);
  const AttrPtr uv = g->getAttribute("uv");
  for (size_t i = 0; i < uv->count(); i++) uv->setXY(i, u0 + uv->getX(i) * (u1 - u0), v0 + uv->getY(i) * (v1 - v0));
  return g;
}

// -------------------------------------------------------------------------------- the tractor
const MaterialPtr& TRACTOR_PAINT() {
  static const MaterialPtr* m = new MaterialPtr(lambert({.color = Color(0xc8442a)}));
  return *m;
}
const MaterialPtr& TRACTOR_RIM() {
  static const MaterialPtr* m = new MaterialPtr(lambert({.color = Color(0xd8d8d0)}));
  return *m;
}

// ------------------------------------------------------------------------- the car's trailer
const MaterialPtr& TRAILER_PAINT() {
  static const MaterialPtr* m = new MaterialPtr(lambert({.color = Color(0x8a9096)}));
  return *m;
}
const MaterialPtr& TARP() {
  static const MaterialPtr* m = new MaterialPtr(lambert({.color = Color(0x2f5d3a)}));
  return *m;
}

double at(const std::vector<double>& v, size_t i, double dflt) { return i < v.size() ? v[i] : dflt; }

}  // namespace

// ---------------------------------------------------------------------------------- the lamps

void Lamps::update(const CarVisualState& s, Object3D* root) {
  if (root) heads_ = lightLamps(*root, lampSwitch(s), brake, heads_);
  else
    for (const std::shared_ptr<Mesh>& l : brake) l->material = s.braking ? LAMP_STOP() : TAIL();
  const bool on = s.blinkOn && s.ignition;
  for (const Indicator& i : indicators) i.mesh->material = on && (s.hazard || s.indicator == i.side) ? AMBER() : OFF();
}

// ---------------------------------------------------------------------------------- the shell

Shell::Shell(MaterialPtr paint)
    : lining(lambert({.color = Color(0x5d636b), .side = Side::Back})), fittings(lambert({.color = Color(0x30343a)})), paint_(std::move(paint)) {}

void Shell::wall(std::string_view facing, double a0, double a1, double b0, double b1, double at, const MaterialPtr& paint) {
  if (std::abs(a1 - a0) < 1e-3 || std::abs(b1 - b0) < 1e-3) return;
  auto outer = panel(facing, a0, a1, b0, b1, at, paint ? paint : paint_);
  outer->castShadow = true;
  group->add(outer, panel(facing, a0, a1, b0, b1, at, lining));
}

void Shell::glass(std::string_view facing, double a0, double a1, double b0, double b1, double at) { group->add(panel(facing, a0, a1, b0, b1, at, GLASS())); }

void Shell::setOpacity(double opacity) {
  const double o = std::isfinite(opacity) ? std::min(1.0, std::max(0.05, opacity)) : 1;
  const bool solid = o >= 0.995;
  for (const MaterialPtr& m : {lining, fittings}) {
    if (m->transparent == solid) m->needsUpdate();
    m->transparent = !solid;
    m->depthWrite = solid;
    m->opacity = solid ? 1 : o;
  }
}

// ------------------------------------------------------------------------------------ the bus

/**
 * The destination display of a city bus: its line and where it goes, amber
 * on black.  One small picture per bus – the band with both above (for the
 * front and the side), the number alone below it (for the back) –, drawn
 * anew only when the line changes.
 */
class Destination {
 public:
  const Canvas canvas{512, 128};
  TexturePtr texture;
  MaterialPtr material;
  std::string text;

  Destination() {
    texture = canvasTexture(canvas);
    texture->srgb = true;
    texture->anisotropy = 4;
    material = basic({.map = texture});
  }

  void draw(const std::string& ref, const std::string& to) {
    const Ctx2D g = canvas.getContext2D();
    if (!g) return;
    const double band = 128 * BAND;
    g.fillStyle("#0b0b0b");
    g.fillRect(0, 0, 512, 128);
    g.fillStyle("#ffb21a");
    g.textBaseline("middle");
    g.textAlign("left");
    double x = 14;
    if (!ref.empty()) {
      g.font("bold 62px \"Arial Narrow\", Arial, sans-serif");
      const double w = std::min(150.0, g.measureText(ref).width);
      g.fillText(ref, x, band / 2 + 2, 150);
      x += w + 24;
    }
    g.font("bold 44px \"Arial Narrow\", Arial, sans-serif");
    g.fillText(to, x, band / 2 + 2, 512 - x - 12);
    g.textAlign("center");
    g.font("bold 38px \"Arial Narrow\", Arial, sans-serif");
    g.fillText(ref, 64, (band + 128) / 2 + 1, 116);
    texture->needsUpdate();
  }
};

PlayerBus::PlayerBus() : PlayerBus(BUS) {}
PlayerBus::~PlayerBus() = default;

PlayerBus::PlayerBus(const VehicleSpec& spec) : VehicleModel(Vector3(-0.72, 2.02, -4.75)), shell_(BUS_PAINT()), spec_(spec) {
  const VehicleSpec& s = spec_;
  Group& root = *this->root;
  root.name = "bus";
  const double hw = s.width.value_or(2.55) / 2 - 0.01;
  const double axle = s.wheelbase / 2;
  const bool long_ = s.length.value_or(12) > 9;
  // overhangs: 2.7 m at the front of the big bus, 1.2 m of the minibus
  const double zf = -(axle + (long_ ? 2.7 : 1.2));
  const double zr = zf + s.length.value_or(12);
  const double y0 = long_ ? 0.32 : 0.36;
  const double sill = long_ ? 1.18 : 1.1;
  const double top = long_ ? 2.62 : 2.32;
  const double y1 = long_ ? 3.0 : 2.7;
  eye.set(-0.62, long_ ? 2.02 : 1.75, zf + (long_ ? 0.9 : 1.15));
  Shell& sh = shell_;
  // --- front: below and above the big windscreen, pillars at its sides
  sh.wall("z-", -hw, hw, y0, 1.0, zf);
  sh.wall("z-", -hw, hw, 2.86, y1, zf);
  for (const int side : {-1, 1}) sh.wall("z-", side * hw, side * (hw - 0.09), 1.0, 2.86, zf);
  sh.glass("z-", -hw + 0.09, hw - 0.09, 1.0, 2.86, zf + 0.01);
  // --- the sides: below the windows (with arches over the wheels), the windows, above them
  const double arches[] = {-axle, axle};
  for (const int side : {-1, 1}) {
    const Facing f = side > 0 ? "x+" : "x-";
    const double x = side * hw;
    // below the windows, and the stripe on it
    double z = zf;
    for (const double a : {arches[0], arches[1], zr + 1}) {
      const double from = std::min(a - 0.62, zr);
      sh.wall(f, z, from, y0, sill, x);
      root.add(panel(f, z, from, 0.62, 0.78, x + side * 0.004, BUS_STRIPE()));
      if (a < zr) sh.wall(f, a - 0.62, a + 0.62, 1.08, sill, x);
      z = a + 0.62;
    }
    sh.wall(f, zf, zr, top, y1, x);
    // windows between pillars – on the right the doors take their place (front and middle)
    std::vector<std::array<double, 2>> doors;
    if (side > 0) {
      if (long_) doors = {{zf + 0.25, zf + 1.45}, {-0.65, 0.65}};
      else doors = {{zf + 1.6, zf + 2.6}};
    }
    double z0 = zf + 0.1;
    while (z0 < zr - 1.7) {
      const double z1 = std::min(z0 + 1.55, zr - 1.7);
      sh.wall(f, z0, z0 + 0.1, sill, top, x);
      const bool door = std::any_of(doors.begin(), doors.end(), [&](const std::array<double, 2>& d) { return z0 + 0.1 < d[1] && z1 > d[0]; });
      if (!door) sh.glass(f, z0 + 0.1, z1, sill, top, x - side * 0.01);
      z0 = z1;
    }
    sh.wall(f, z0, zr, sill, top, x);
    for (const auto& [a, b] : doors) {
      // a door: glass almost down to the floor, in a dark frame
      sh.wall(f, a, b, y0, sill, x, DARK());
      sh.glass(f, a + 0.05, b - 0.05, 0.42, top - 0.04, x + side * 0.006);
    }
  }
  // --- the back: the engine, no window below the roof edge but a narrow one
  sh.wall("z+", -hw, hw, y0, 2.1, zr);
  sh.wall("z+", -hw, hw, 2.7, y1, zr);
  for (const int side : {-1, 1}) sh.wall("z+", side * hw, side * (hw - 0.12), 2.1, 2.7, zr);
  sh.glass("z+", -hw + 0.12, hw - 0.12, 2.1, 2.7, zr - 0.01);
  sh.wall("y+", -hw, hw, zf, zr, y1);
  sh.wall("y-", -hw, hw, zf, zr, y0);
  root.add(sh.group);
  // roof equipment, bumpers, the destination display
  if (long_) root.add(box(-hw + 0.25, hw - 0.25, y1, y1 + 0.32, -1.2, 2.4, FRAME()));
  root.add(box(-hw, hw, 0.28, 0.55, zf - 0.06, zf + 0.05, DARK()));
  root.add(box(-hw, hw, 0.28, 0.55, zr - 0.05, zr + 0.06, DARK()));
  auto sign = lettering(BRAND, 1.9, 0.24);
  sign->position.set(0, top + 0.12, zf - 0.012);
  sign->rotation.y = PI;
  root.add(sign);
  brand_ = sign;
  body_ = Body{hw, top, y1, zf, zr, long_};
  for (const int side : {-1, 1}) {
    auto plate = lettering(BRAND, long_ ? 4.2 : 2.6, long_ ? 0.42 : 0.3);
    plate->position.set(side * (hw + 0.008), 0.95, long_ ? 1.6 : zf + 4.4);
    plate->rotation.y = (side * PI) / 2;
    root.add(plate);
  }
  // --- lamps
  for (const int side : {-1, 1}) {
    root.add(box(side * (hw - 0.35) - 0.2, side * (hw - 0.35) + 0.2, 0.6, 0.78, zf - 0.03, zf, LAMP(), false));
    auto stop = box(side * (hw - 0.25) - 0.12, side * (hw - 0.25) + 0.12, 1.0, 1.45, zr, zr + 0.03, TAIL(), false);
    lamps_.brake.push_back(stop);
    root.add(stop);
    const double zy[2][2] = {
        {zf - 0.03, 0.85},
        {zr + 0.03, 1.6},
    };
    for (const auto& [z, y] : zy) {
      auto ind = box(side * (hw - 0.2) - 0.08, side * (hw - 0.2) + 0.08, y, y + 0.14, z - 0.015, z + 0.015, OFF(), false);
      lamps_.indicators.push_back({ind, side});
      root.add(ind);
    }
  }
  // --- wheels: single at the front, twin at the back
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
  // --- inside: the floor, rows of seats, the driver's place
  auto inside = make<Group>();
  const MaterialPtr& fit = sh.fittings;
  for (double z = zf + 3.2; z < zr - 0.8; z += 0.85) {
    if (long_ && z > -1.1 && z < 1.1) continue;  // the space at the middle door
    for (const int side : {-1, 1}) {
      inside->add(box(side * 0.55 - 0.42, side * 0.55 + 0.42, 0.75, 0.88, z, z + 0.45, fit, false));
      inside->add(box(side * 0.55 - 0.42, side * 0.55 + 0.42, 0.88, 1.5, z + 0.4, z + 0.48, fit, false));
    }
  }
  const double ex = eye.x;
  inside->add(box(-hw + 0.04, 0.3, 0.75, 1.32, zf + 0.04, zf + 0.55, fit, false));  // dashboard
  dial(*inside, panelTex_.texture, Vector3(ex, 1.36, zf + 0.62), 0.5, 1.15);
  steering_ = steeringWheel(*inside, Vector3(ex, 1.38, zf + 0.82), 1.05, 0.26, fit);
  inside->add(box(ex - 0.28, ex + 0.28, 0.95, 1.12, zf + 1.1, zf + 1.65, fit, false));  // seat
  inside->add(box(ex - 0.28, ex + 0.28, 1.12, 2.0, zf + 1.55, zf + 1.69, fit, false));
  // the screen with the map, on an arm from the dashboard right of the wheel – low in the driver's view
  const double sy = std::max(1.42, eye.y - 0.445 * (eye.z - zf - 0.18));
  screen_ = std::make_unique<NavScreen>(0.26);
  const Vector3 mount(-0.1, 1.32, zf + 0.22);
  screen_->place(*inside, Vector3(-0.1, sy, zf + 0.18), eye, &mount);
  navScreen = screen_.get();
  root.add(inside);
  // --- mirrors on arms in front of the windscreen
  for (const int side : {-1, 1}) {
    const Vector3 c(side * (hw + 0.32), 2.2, zf - 0.32);
    mirrors.push_back(sideMirror(root, eye, c, side, {0.24, 0.42}, side < 0 ? 1.35 : 1.7, {256, 448}));
    root.add(box(side * hw - (side < 0 ? 0.34 : 0), side * hw + (side > 0 ? 0.34 : 0), 2.62, 2.66, zf - 0.34, zf + 0.02, DARK(), false));
  }
}

void PlayerBus::setCabinOpacity(double opacity) { shell_.setOpacity(opacity); }

void PlayerBus::setLine(const std::string& ref, const std::string& to) {
  const std::string text = !ref.empty() || !to.empty() ? ref + "\n" + to : "";
  if (text == (dest_ ? dest_->text : "")) return;
  if (text.empty()) {
    if (dest_) {
      dest_->text = "";
      for (const std::shared_ptr<Mesh>& p : plates_) p->visible = false;
    }
    brand_->visible = true;
    return;
  }
  if (!dest_) {
    dest_ = std::make_unique<Destination>();
    const auto [hw, top, roof, zf, zr, long_] = body_;
    auto front = make<Mesh>(platePart(long_ ? 2.0 : 1.7, long_ ? 0.33 : 0.28, 0, 1, 1 - BAND, 1), dest_->material);
    front->position.set(0, top + 0.12, zf - 0.016);
    front->rotation.y = PI;
    auto side = make<Mesh>(platePart(1.3, 0.215, 0, 1, 1 - BAND, 1), dest_->material);
    side->position.set(hw + 0.012, (top + roof) / 2, zf + (long_ ? 2.3 : 3.2));
    side->rotation.y = PI / 2;
    auto back = make<Mesh>(platePart(0.6, 0.205, 0, 0.25, 0, 1 - BAND), dest_->material);
    back->position.set(0, long_ ? (2.7 + roof) / 2 : roof - 0.2, zr + 0.012);
    plates_ = {front, side, back};
    root->add(front, side, back);
  }
  dest_->draw(ref, to);
  dest_->text = text;
  for (const std::shared_ptr<Mesh>& p : plates_) p->visible = true;
  brand_->visible = false;
}

void PlayerBus::update(const CarVisualState& s, double dt, double now) {
  for (size_t i = 0; i < 4; i++) {
    Group& w = *wheels_[i];
    // (wheelY is the height of the wheel's centre above the ground under the bus)
    w.position.y = at(s.wheelY, i, spec_.wheelRadius);
    w.rotation.set(-at(s.wheelSpin, i, 0), i < 2 ? -at(s.wheelSteer, i, 0) : 0, 0, Euler::YXZ);
  }
  steering_->rotation.z = -s.steerWheel;
  lamps_.update(s, root.get());
  panelTex_.update(s, now);
  if (detail_) detail_->update();
}

void PlayerBus::setDetail(bool on) {
  if (on && !detail_ && busDetail) detail_ = busDetail(BusParts{root, &spec_, &wheels_});
  if (detail_) detail_->show(on);
}

// -------------------------------------------------------------------------------- the tractor

PlayerTractor::PlayerTractor() : VehicleModel(Vector3(0, 2.42, 0.62)), shell_(TRACTOR_PAINT()) {
  const VehicleSpec& s = TRACTOR;
  Group& root = *this->root;
  root.name = "tractor";
  const double axle = s.wheelbase / 2;
  // --- bonnet with grille and lamps, the chassis under it, weights at the front
  root.add(box(-0.46, 0.46, 0.85, 1.62, -2.25, -0.35, TRACTOR_PAINT()));
  root.add(box(-0.5, 0.5, 0.55, 0.9, -2.35, 0.9, FRAME()));
  root.add(box(-0.44, 0.44, 0.9, 1.5, -2.27, -2.24, DARK()));    // grille
  root.add(box(-0.55, 0.55, 0.45, 0.95, -2.42, -2.25, FRAME()));  // front weights
  for (const int side : {-1, 1}) root.add(box(side * 0.3 - 0.09, side * 0.3 + 0.09, 1.38, 1.52, -2.28, -2.25, LAMP(), false));
  // exhaust pipe at the right front corner of the cab
  root.add(box(0.36, 0.44, 1.6, 3.05, -0.42, -0.34, DARK()));
  // --- the cab: posts at the corners, glass all round, a roof
  Shell& sh = shell_;
  const double cx = 0.68;
  const double z0 = -0.38;
  const double z1 = 1.12;
  const double floor = 1.3;
  const double roof = 2.95;
  sh.wall("y-", -cx, cx, z0, z1, floor);
  sh.wall("y+", -cx - 0.08, cx + 0.08, z0 - 0.08, z1 + 0.08, roof);
  for (const int side : {-1, 1}) {
    const Facing f = side > 0 ? "x+" : "x-";
    sh.wall(f, z0, z1, floor, 1.52, side * cx);  // below the glass
    sh.glass(f, z0 + 0.06, z1 - 0.06, 1.52, roof - 0.05, side * cx);
    for (const double z : {z0, z1 - 0.06}) root.add(box(side * cx - 0.035, side * cx + 0.035, floor, roof, z, z + 0.06, DARK(), false));
  }
  sh.wall("z-", -cx, cx, floor, 1.6, z0);
  sh.glass("z-", -cx + 0.04, cx - 0.04, 1.6, roof - 0.05, z0);
  sh.wall("z+", -cx, cx, floor, 1.55, z1);
  sh.glass("z+", -cx + 0.04, cx - 0.04, 1.55, roof - 0.05, z1);
  root.add(sh.group);
  // --- mudguards over the rear wheels and a step to the door on the left
  for (const int side : {-1, 1}) {
    root.add(box(side * 0.68, side * 0.68 + side * 0.62, 1.58, 1.64, axle - 0.85, axle + 0.75, TRACTOR_PAINT()));
    root.add(box(side * 0.68 - 0.03, side * 0.68 + 0.03, 0.95, 1.6, axle - 0.85, axle - 0.15, TRACTOR_PAINT()));
  }
  root.add(box(-0.95, -0.7, 0.62, 0.66, -0.25, 0.3, DARK()));
  // --- lamps at the back (on the mudguards) and indicators on the cab corners
  for (const int side : {-1, 1}) {
    auto stop = box(side * 1.12 - 0.08, side * 1.12 + 0.08, 1.48, 1.58, axle + 0.74, axle + 0.77, TAIL(), false);
    lamps_.brake.push_back(stop);
    root.add(stop);
    for (const double z : {z0 - 0.04, axle + 0.78}) {
      auto ind = box(side * (z < 0 ? cx + 0.05 : 1.12) - 0.05, side * (z < 0 ? cx + 0.05 : 1.12) + 0.05, z < 0 ? 2.8 : 1.36, z < 0 ? 2.9 : 1.46, z - 0.02, z + 0.02, OFF(), false);
      lamps_.indicators.push_back({ind, side});
      root.add(ind);
    }
  }
  // --- wheels: small and narrow at the front, big and wide at the back
  const double wheelsAt[4][4] = {
      {-0.93, -axle, 0.38, radii_[0]},
      {0.93, -axle, 0.38, radii_[1]},
      {-0.92, axle, 0.55, radii_[2]},
      {0.92, axle, 0.55, radii_[3]},
  };
  for (const auto& [x, z, w, r] : wheelsAt) {
    auto g = wheel(r, w);
    // (light rims, and the lugs of a tractor tyre as a darker ring)
    std::static_pointer_cast<Mesh>(g->children[1])->material = TRACTOR_RIM();
    g->position.set(x, r, z);
    wheels_.push_back(g);
    root.add(g);
  }
  // --- inside: the seat in the middle, the wheel before it, the instruments on the column
  auto inside = make<Group>();
  const MaterialPtr& fit = sh.fittings;
  inside->add(box(-0.27, 0.27, 1.62, 1.76, 0.42, 0.92, fit, false));
  inside->add(box(-0.27, 0.27, 1.76, 2.35, 0.86, 0.98, fit, false));
  inside->add(box(-0.18, 0.18, floor, 2.0, z0 + 0.02, z0 + 0.32, fit, false));  // the console before the wheel
  dial(*inside, panelTex_.texture, Vector3(0, 2.02, z0 + 0.36), 0.36, 0.55);
  steering_ = steeringWheel(*inside, Vector3(0, 2.0, 0.02), 0.62, 0.21, fit);
  // the screen with the map, on an arm from the right front post of the cab
  screen_ = std::make_unique<NavScreen>(0.22);
  const Vector3 mount(cx - 0.04, 2.16, z0 + 0.03);
  screen_->place(*inside, Vector3(0.47, 2.16, z0 + 0.22), eye, &mount);
  navScreen = screen_.get();
  root.add(inside);
  // --- mirrors on arms at the front corners of the cab
  for (const int side : {-1, 1}) {
    const Vector3 c(side * (cx + 0.42), 2.35, z0 - 0.05);
    mirrors.push_back(sideMirror(root, eye, c, side, {0.2, 0.3}, 1.4, {192, 288}));
    root.add(box(side * cx - (side < 0 ? 0.4 : 0), side * cx + (side > 0 ? 0.4 : 0), 2.5, 2.53, z0 - 0.07, z0 - 0.03, DARK(), false));
  }
}

void PlayerTractor::setCabinOpacity(double opacity) { shell_.setOpacity(opacity); }

void PlayerTractor::update(const CarVisualState& s, double dt, double now) {
  for (size_t i = 0; i < 4; i++) {
    Group& w = *wheels_[i];
    // (the physics' wheels are all of one size, and wheelY is where their centres are: the drawn ones,
    // of two sizes, move up and down with them and keep touching the ground)
    w.position.y = at(s.wheelY, i, TRACTOR.wheelRadius) - TRACTOR.wheelRadius + radii_[i];
    // the big wheels turn more slowly for the same speed
    const double spin = at(s.wheelSpin, i, 0) * (TRACTOR.wheelRadius / radii_[i]);
    w.rotation.set(-spin, i < 2 ? -at(s.wheelSteer, i, 0) : 0, 0, Euler::YXZ);
  }
  steering_->rotation.z = -s.steerWheel;
  lamps_.update(s, root.get());
  panelTex_.update(s, now);
  if (detail_) detail_->update();
}

void PlayerTractor::setDetail(bool on) {
  if (on && !detail_ && tractorDetail) detail_ = tractorDetail(TractorParts{root, &wheels_, radii_});
  if (detail_) detail_->show(on);
}

// ------------------------------------------------------------------------- the car's trailer

CarTrailerModel::CarTrailerModel() {
  const TrailerSpec& t = CAR_TRAILER;
  Group& g = *group;
  g.name = "car trailer";
  const double hw = t.width / 2;
  const double front = -(t.hitchToAxle - t.bodyFront);
  const double rear = t.length - t.hitchToAxle;
  const double hitchY = t.hitch ? t.hitch->height : 0.45;
  // (named for the tailgate and the tarpaulin's rear flap that open: render/rearopen.ts)
  auto body = box(-hw, hw, 0.55, 0.95, front, rear, TRAILER_PAINT());  // the box
  body->name = "trailer box";
  auto tarp = box(-hw + 0.02, hw - 0.02, 0.95, t.height, front + 0.02, rear - 0.02, TARP());  // the tarpaulin
  tarp->name = "trailer tarp";
  g.add(body, tarp);
  g.add(box(-0.6, 0.6, 0.42, 0.55, front - 0.1, rear - 0.2, FRAME()));
  // the drawbar (an A-frame) up to the coupling head
  for (const int side : {-1, 1}) {
    auto bar = box(-0.04, 0.04, hitchY - 0.05, hitchY + 0.05, -t.hitchToAxle + 0.15, front + 0.05, FRAME());
    bar->position.x = side * 0.32;
    bar->rotation.y = side * 0.27;
    g.add(bar);
  }
  g.add(box(-0.07, 0.07, hitchY - 0.06, hitchY + 0.08, -t.hitchToAxle - 0.05, -t.hitchToAxle + 0.3, DARK()));
  // wheels with mudguards
  for (const int side : {-1, 1}) {
    for (const double z : {-t.axleSpread / 2, t.axleSpread / 2}) {
      auto w = wheel(t.wheelRadius, 0.2);
      w->position.set(side * (t.track / 2), t.wheelRadius, z);
      wheels_.push_back(w);
      g.add(w);
    }
    g.add(box(side * (hw - 0.02) - 0.11, side * (hw - 0.02) + 0.11, 0.66, 0.69, -0.85, 0.85, DARK()));
    auto stop = box(side * (hw - 0.22) - 0.1, side * (hw - 0.22) + 0.1, 0.62, 0.74, rear, rear + 0.03, TAIL(), false);
    lamps_.brake.push_back(stop);
    g.add(stop);
    auto ind = box(side * (hw - 0.06) - 0.05, side * (hw - 0.06) + 0.05, 0.62, 0.74, rear, rear + 0.035, OFF(), false);
    lamps_.indicators.push_back({ind, side});
    g.add(ind);
  }
}

void CarTrailerModel::place(double x, double y, double z, double yaw, double pitch, double wheelSpin, const CarVisualState* s) {
  group->position.set(x, y, z);
  group->rotation.set(pitch, yaw, 0, Euler::YXZ);
  for (const std::shared_ptr<Group>& w : wheels_) w->rotation.x = -wheelSpin;
  if (s) lamps_.update(*s);
}

// --------------------------------------------------------------------------------- the tram's cab

TramCab::TramCab() : VehicleModel(Vector3(0, 2.15, -3.35)), shell_(FRAME()) {
  Group& root = *this->root;
  root.name = "tram cab";
  Shell& sh = shell_;
  const double hw = 1.17;
  const double zf = -4.62;
  const double zb = -2.1;
  const double floor = 0.95;
  const double sill = 1.35;
  const double top = 2.7;
  const double roof = 3.2;
  // (only the lining: the paint outside is the tram's own)
  const auto lining = [&sh](Facing facing, double a0, double a1, double b0, double b1, double at) { sh.group->add(panel(facing, a0, a1, b0, b1, at, sh.lining)); };
  lining("z-", -hw, hw, floor, sill - 0.1, zf);
  lining("z-", -hw, hw, top + 0.1, roof, zf);
  for (const int side : {-1, 1}) {
    const Facing f = side > 0 ? "x+" : "x-";
    lining(f, zf, zb, floor, sill, side * hw);
    lining(f, zf, zb, top, roof, side * hw);
    // the pillars beside the windscreen
    lining("z-", side * hw, side * (hw - 0.12), sill - 0.1, top + 0.1, zf);
  }
  lining("y+", -hw, hw, zf, zb, roof);
  lining("y-", -hw, hw, zf, zb, floor);
  // the wall behind the driver, with the door to the passengers
  lining("z+", -hw, -0.4, floor, roof, zb);
  lining("z+", 0.4, hw, floor, roof, zb);
  lining("z+", -0.4, 0.4, 2.2, roof, zb);
  root.add(sh.group);
  const MaterialPtr& fit = sh.fittings;
  // the desk across the cab, the instruments on it, the controller on its left
  root.add(box(-0.95, 0.95, floor, 1.62, zf + 0.05, zf + 0.62, fit, false));
  dial(root, panelTex_.texture, Vector3(0.18, 1.7, zf + 0.5), 0.42, 0.7);
  handle_->position.set(-0.55, 1.62, zf + 0.42);
  auto stick = box(-0.025, 0.025, 0, 0.2, -0.025, 0.025, DARK(), false);
  auto knob = box(-0.05, 0.05, 0.2, 0.26, -0.04, 0.04, fit, false);
  handle_->add(stick, knob);
  root.add(handle_);
  // the seat
  root.add(box(-0.28, 0.28, floor + 0.55, floor + 0.68, zf + 1.05, zf + 1.6, fit, false));
  root.add(box(-0.28, 0.28, floor + 0.68, floor + 1.45, zf + 1.52, zf + 1.66, fit, false));
  // the screen with the map, on the desk right of the instruments
  screen_ = std::make_unique<NavScreen>(0.26);
  const Vector3 mount(0.6, 1.62, zf + 0.46);
  screen_->place(root, Vector3(0.6, 1.8, zf + 0.42), eye, &mount);
  navScreen = screen_.get();
}

void TramCab::setCabinOpacity(double opacity) { shell_.setOpacity(opacity); }

void TramCab::setLever(double lever) { handle_->rotation.x = -lever * 0.55; }

void TramCab::update(const CarVisualState& s, double dt, double now) { panelTex_.update(s, now); }

}  // namespace fs

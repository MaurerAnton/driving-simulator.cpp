// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/emergency.ts @ v1.7.7
#include "render/emergency.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <utility>

#include "core/js.h"
#include "engine/curves.h"
#include "engine/geometries.h"
#include "platform/canvas.h"
#include "render/car.h"
#include "render/carBody.h"
#include "sim/emergency.h"
#include "sim/traffic.h"

namespace fs {

namespace {

/** the materials all of them share (made once, kept to the end: statics' destructors run in no fixed order) */
struct EmMaterials {
  MaterialPtr WHITE = lambert({.color = Color(0xf2f2ee)});
  MaterialPtr FIRE_RED = lambert({.color = Color(0xb3201c)});
  MaterialPtr DARK = lambert({.color = Color(0x1b1d20)});
  MaterialPtr FRAME = lambert({.color = Color(0x33373c)});
  MaterialPtr STEEL = lambert({.color = Color(0xb9bec4)});
  MaterialPtr TYRE = lambert({.color = Color(0x141516)});
  MaterialPtr RIM = lambert({.color = Color(0xb4b8bc)});
  MaterialPtr GLASS = phong({.color = Color(0x0c1418), .specular = Color(0x9fb4c4), .shininess = 140});
  /** the housing of a light bar, and the clear lamp in its middle */
  MaterialPtr BAR = lambert({.color = Color(0x24282d)});
  MaterialPtr CLEAR = basic({.color = Color(0xd9dfe4)});
  MaterialPtr HEAD = basic({.color = Color(0xfff3cf)});
  /** a headlight flashing (it is drawn over the headlight while it is lit) */
  MaterialPtr FLASH = basic({.color = Color(0xffffff)});
  MaterialPtr TAIL = basic({.color = Color(0xa01410)});
  MaterialPtr STOP = basic({.color = Color(0xff3a2a)});
  MaterialPtr AMBER = basic({.color = Color(0xffa010)});
  MaterialPtr AMBER_OFF = lambert({.color = Color(0x6a3a08)});
};
const EmMaterials& M() {
  static const auto* m = new EmMaterials();
  return *m;
}
const Color BLUE_ON(0x6a9cff);
const Color BLUE_OFF(0x1c2a52);

/** the blue of the police's livery, and the fluorescent red of the rescue services' */
const std::string POLICE_BLUE = "#1d4f9c";
const std::string SIGNAL_RED = "#e8321e";

/** A box from x0…x1, y0…y1, z0…z1. */
std::shared_ptr<Mesh> box(double x0, double x1, double y0, double y1, double z0, double z1, const MaterialPtr& material, bool shadow = true) {
  auto m = make<Mesh>(boxGeometry(x1 - x0, y1 - y0, z1 - z0), material);
  m->position.set((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
  m->castShadow = shadow;
  return m;
}

/** From `inner` to `outer` metres out from the middle on one side (1 right, −1 left), as x0 < x1. */
std::pair<double, double> across(double side, double inner, double outer) { return side > 0 ? std::pair{inner, outer} : std::pair{-outer, -inner}; }

/** A wheel lying on the x axis (a twin wheel when wide), its middle at the origin; turned about x it rolls. */
std::shared_ptr<Group> wheel(double radius, double width) {
  auto g = make<Group>();
  auto tyre = make<Mesh>(cylinderGeometry(radius, radius, width, 20), M().TYRE);
  tyre->rotation.z = PI / 2;
  tyre->castShadow = true;
  auto rim = make<Mesh>(cylinderGeometry(radius * 0.58, radius * 0.58, width + 0.02, 10), M().RIM);
  rim->rotation.z = PI / 2;
  // (a spoke, so that it is seen to turn)
  auto spoke = make<Mesh>(boxGeometry(width + 0.03, radius * 0.9, radius * 0.14), M().DARK);
  g->add(tyre, rim, spoke);
  return g;
}

/**
 * A body seen from the side – an outline of (z, y) points – made solid
 * across the vehicle, `width` wide.  (The outline is a shape in its own
 * plane, pushed out along its third axis: turned, that axis runs across.)
 */
std::shared_ptr<Mesh> sideSolid(const std::vector<std::array<double, 2>>& outline, double width, const MaterialPtr& material) {
  Shape s;
  for (size_t i = 0; i < outline.size(); i++) {
    if (i == 0) s.moveTo(outline[i][0], outline[i][1]);
    else s.lineTo(outline[i][0], outline[i][1]);
  }
  s.closePath();
  GeometryPtr g = extrudeGeometry(s, {.depth = width, .bevelEnabled = false});
  g->rotateY(-PI / 2);
  g->translate(width / 2, 0, 0);
  auto m = make<Mesh>(g, material);
  m->castShadow = true;
  return m;
}

/** A flat picture or pane: `w` × `h`, its middle at (x, y, z), facing along +x, −x, +z or −z. */
std::shared_ptr<Mesh> plate(double w, double h, double x, double y, double z, const std::string& facing, const MaterialPtr& material) {
  auto m = make<Mesh>(planeGeometry(w, h), material);
  m->position.set(x, y, z);
  // (a plane faces +z: turned to face its way – facing ±x its +u runs towards −z on the right side, +z on the left)
  m->rotation.y = facing == "z+" ? 0 : facing == "z-" ? PI : facing == "x+" ? PI / 2 : -PI / 2;
  return m;
}

TexturePtr texture(const Canvas& c) {
  TexturePtr t = canvasTexture(c);
  t->srgb = true;
  t->anisotropy = 4;
  return t;
}

const std::string FONT = "\"Helvetica Neue\", Helvetica, Arial, \"Liberation Sans\", \"Noto Sans\", \"DejaVu Sans\", sans-serif";

/** Write a word, as large as fits the box (x, y the middle; w × h at most), in one colour. */
void word(const Ctx2D& g, const std::string& text, double x, double y, double w, double h, const std::string& colour) {
  double size = h;
  g.font("800 " + jsStr(size) + "px " + FONT);
  const double tw = g.measureText(text).width;
  if (tw > w) {
    size *= w / tw;
    g.font("800 " + jsStr(size) + "px " + FONT);
  }
  g.fillStyle(colour);
  g.textAlign("center");
  g.textBaseline("middle");
  g.fillText(text, x, y);
}

TexturePtr cached(const std::string& key, const std::function<TexturePtr()>& makeIt) {
  static auto* cache = new std::map<std::string, TexturePtr>();
  auto it = cache->find(key);
  if (it == cache->end()) it = cache->emplace(key, makeIt()).first;
  return it->second;
}

/** The soft round glow of a lamp that is lit (white: the sprite gives it its colour). */
TexturePtr glow() {
  return cached("glow", [] {
    const Canvas c(64, 64);
    const Ctx2D g = c.getContext2D();
    const CanvasGradient r = g.createRadialGradient(32, 32, 0, 32, 32, 32);
    r.addColorStop(0, "rgba(255,255,255,1)");
    r.addColorStop(0.2, "rgba(255,255,255,0.7)");
    r.addColorStop(0.5, "rgba(255,255,255,0.16)");
    r.addColorStop(1, "rgba(255,255,255,0)");
    g.fillStyle(r);
    g.fillRect(0, 0, 64, 64);
    return canvasTexture(c);
  });
}

/** A band of colour with a word on it in white (the doors of the police car and of the doctor's car). */
TexturePtr bandTexture(const std::string& text, const std::string& colour) {
  return cached("band:" + text, [&] {
    const Canvas c(1024, 160);
    const Ctx2D g = c.getContext2D();
    g.fillStyle(colour);
    g.fillRect(0, 0, 1024, 160);
    // (a thin light line along its top edge, as the reflecting strips do)
    g.fillStyle("rgba(255,255,255,0.55)");
    g.fillRect(0, 0, 1024, 10);
    word(g, text, 512, 88, 860, 112, "#ffffff");
    return texture(c);
  });
}

/**
 * The side of the ambulance's box: white, the fluorescent red band with the
 * word over it, a frosted window, the blue star of life.  `frontRight`: the
 * front of the vehicle is at the right of the picture (its right side).
 */
TexturePtr ambulanceSide(bool frontRight) {
  return cached(std::string("rtw:") + (frontRight ? "true" : "false"), [&] {
    const Canvas c(1024, 464);
    const Ctx2D g = c.getContext2D();
    g.fillStyle("#f2f2ee");
    g.fillRect(0, 0, 1024, 464);
    const auto x = [&](double u, double w) { return frontRight ? 1024 - u - w : u; };
    // the frosted window towards the front, a narrow one at the back
    g.fillStyle("#c9d3d8");
    g.fillRect(x(40, 300), 56, 300, 120);
    g.fillRect(x(840, 120), 56, 120, 120);
    // the band, and a thin one lower down
    g.fillStyle(SIGNAL_RED);
    g.fillRect(0, 250, 1024, 62);
    g.fillRect(0, 380, 1024, 22);
    word(g, "RETTUNGSDIENST", 512, 222, 760, 46, SIGNAL_RED);
    // the star of life
    const double sx = frontRight ? 1024 - 470 - 70 : 470 + 70;
    g.save();
    g.translate(sx, 110);
    g.fillStyle("#1f4fa8");
    for (int k = 0; k < 3; k++) {
      g.rotate(PI / 3);
      g.fillRect(-18, -62, 36, 124);
    }
    g.fillStyle("#ffffff");
    g.fillRect(-3, -44, 6, 88);
    g.restore();
    return texture(c);
  });
}

/** The back of a box body: two doors with windows (or a roller shutter), the red and white warning stripes below. */
TexturePtr rearTexture(bool shutter) {
  return cached(std::string("rear:") + (shutter ? "true" : "false"), [&] {
    const Canvas c(512, 512);
    const Ctx2D g = c.getContext2D();
    g.fillStyle(shutter ? "#b3201c" : "#f2f2ee");
    g.fillRect(0, 0, 512, 512);
    if (shutter) {
      g.fillStyle("#aeb4ba");
      g.fillRect(40, 30, 432, 330);
      g.fillStyle("#8d949b");
      for (int y = 40; y < 360; y += 16) g.fillRect(40, y, 432, 4);
    } else {
      g.fillStyle("#c9d3d8");
      g.fillRect(40, 40, 190, 150);
      g.fillRect(282, 40, 190, 150);
      g.fillStyle("#9aa1a8");
      g.fillRect(253, 0, 6, 512);
    }
    // warning stripes, slanting down to the outside on each half
    g.save();
    g.beginPath();
    g.rect(0, 392, 512, 120);
    g.clip();
    g.fillStyle("#ffffff");
    g.fillRect(0, 392, 512, 120);
    g.fillStyle(shutter ? "#d01e1e" : SIGNAL_RED);
    for (int k = -6; k < 12; k++) {
      for (const int half : {0, 1}) {
        const double x0 = half == 0 ? k * 56 : 512 - k * 56;
        const double dir = half == 0 ? 1 : -1;
        g.beginPath();
        g.moveTo(x0, 392);
        g.lineTo(x0 + dir * 28, 392);
        g.lineTo(x0 + dir * 28 - dir * 120, 512);
        g.lineTo(x0 - dir * 120, 512);
        g.closePath();
        g.fill();
      }
    }
    g.restore();
    return texture(c);
  });
}

/** The side of the fire engine's body: roller shutters between red posts, a white strip along the bottom. */
TexturePtr fireSide() {
  return cached("hlf", [] {
    const Canvas c(1024, 400);
    const Ctx2D g = c.getContext2D();
    g.fillStyle("#b3201c");
    g.fillRect(0, 0, 1024, 400);
    for (int k = 0; k < 3; k++) {
      const double x0 = 24 + k * 336;
      g.fillStyle("#b8bec4");
      g.fillRect(x0, 24, 312, 300);
      g.fillStyle("#939aa1");
      for (int y = 34; y < 324; y += 14) g.fillRect(x0, y, 312, 3);
      // the handle bar at the foot of the shutter
      g.fillStyle("#5c6268");
      g.fillRect(x0, 300, 312, 14);
    }
    g.fillStyle("#f4f4f0");
    g.fillRect(0, 346, 1024, 26);
    return texture(c);
  });
}

/** White letters on the red of the fire engine: on its doors and over its windscreen. */
TexturePtr fireWord() {
  return cached("feuerwehr", [] {
    const Canvas c(1024, 192);
    const Ctx2D g = c.getContext2D();
    g.fillStyle("#b3201c");
    g.fillRect(0, 0, 1024, 192);
    word(g, "FEUERWEHR", 512, 100, 940, 150, "#ffffff");
    return texture(c);
  });
}

/** The police's sign that rises from the roof: red lamps on black, "STOP" over "POLIZEI". */
TexturePtr stopSignTexture() {
  return cached("stop", [] {
    const Canvas c(512, 160);
    const Ctx2D g = c.getContext2D();
    g.fillStyle("#060606");
    g.fillRect(0, 0, 512, 160);
    word(g, "STOP", 256, 52, 440, 84, "#ff2412");
    word(g, "POLIZEI", 256, 124, 440, 54, "#ff2412");
    // a grid of dark lines: the letters made of lamps
    g.fillStyle("#060606");
    for (int x = 0; x < 512; x += 6) g.fillRect(x, 0, 2, 160);
    for (int y = 0; y < 160; y += 6) g.fillRect(0, y, 512, 2);
    return texture(c);
  });
}

/** A light bar across a roof, `width` wide, its foot at height y, its middle at z: two blue halves (left group 0, right 1), a clear lamp between them. */
void lightBar(Object3D& parent, Flashers& f, double width, double y, double z, double depth = 0.26) {
  const double h = 0.1;
  parent.add(box(-width / 2, width / 2, y, y + 0.035, z - depth / 2, z + depth / 2, M().BAR, false));
  const double half = (width - 0.18) / 2;
  const double y0 = y + 0.035;
  f.lamp(parent, -width / 2, -width / 2 + half, y0, y0 + h, z - depth / 2, z + depth / 2, 0, 1.1);
  f.lamp(parent, width / 2 - half, width / 2, y0, y0 + h, z - depth / 2, z + depth / 2, 1, 1.1);
  parent.add(box(-0.09, 0.09, y0, y0 + h * 0.8, z - depth / 2 + 0.02, z + depth / 2 - 0.02, M().CLEAR, false));
}

/**
 * A strip lying on the flank of a car between z0 and z1, following the body
 * across from `y0` up to `y1` (`xs`: how far out the body is at y0, at the
 * widest crease `ym` and at y1), on the right (side 1) or the left (−1).  Its
 * picture reads from the back to the front on the right, the other way on
 * the left – from outside, left to right either way.
 */
std::shared_ptr<Mesh> flankStrip(const std::array<double, 3>& xs, double y0, double ym, double y1, double z0, double z1, double side, const MaterialPtr& material) {
  const std::array<double, 3> ys = {y0, ym, y1};
  const std::array<double, 3> vs = {0, (ym - y0) / (y1 - y0), 1};
  std::vector<float> pos;
  std::vector<float> uv;
  for (size_t i = 0; i < 3; i++) {
    for (const auto& [z, u] : {std::pair{z1, 0.0}, std::pair{z0, 1.0}}) {
      pos.insert(pos.end(), {static_cast<float>(side * (xs[i] + 0.006)), static_cast<float>(ys[i]), static_cast<float>(z)});
      uv.insert(uv.end(), {static_cast<float>(side > 0 ? u : 1 - u), static_cast<float>(vs[i])});
    }
  }
  std::vector<uint32_t> idx;
  for (uint32_t i = 0; i < 2; i++) {
    const uint32_t a = i * 2;
    // (facing out: to the right on the right side; the other way round on the left)
    if (side > 0) idx.insert(idx.end(), {a, a + 1, a + 2, a + 1, a + 3, a + 2});
    else idx.insert(idx.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
  }
  GeometryPtr g = make<BufferGeometry>();
  g->setAttribute("position", attr(std::move(pos), 3));
  g->setAttribute("uv", attr(std::move(uv), 2));
  g->setIndex(idx);
  g->computeVertexNormals();
  auto m = make<Mesh>(g, material);
  m->renderOrder = 4;
  return m;
}

/** the patrol car: an estate; the doctor's car: a taller one, a little bigger */
CarDims withDims(double length, double width, double height, double wheelbase, double track, double wheelRadius, double tyreWidth, double frontOverhang, int tail) {
  CarDims d = COMPACT_DIMS;
  d.length = length;
  d.width = width;
  d.height = height;
  d.wheelbase = wheelbase;
  d.track = track;
  d.wheelRadius = wheelRadius;
  d.tyreWidth = tyreWidth;
  d.frontOverhang = frontOverhang;
  d.tail = tail;
  return d;
}
const CarDims POLICE_DIMS = withDims(4.75, 1.84, 1.5, 2.8, 1.58, 0.33, 0.225, 0.93, 1);
const CarDims DOCTOR_DIMS = withDims(4.85, 1.9, 1.66, 2.85, 1.62, 0.35, 0.235, 0.95, 1);

/** a pane on a sloping cab front, from a up to b (y, z), facing forwards and up: away from the slope of the cab */
void slopedScreen(Group& g, double w, double h, const Vector3& a, const Vector3& b) {
  auto screen = make<Mesh>(planeGeometry(w, h), M().GLASS);
  screen->position.copy(a).add(b).multiplyScalar(0.5);
  const Vector3 n = Vector3(0, b.z - a.z, -(b.y - a.y)).normalize();
  screen->position.addScaledVector(n, 0.012);
  screen->lookAt(screen->position.clone().add(n));
  g.add(screen);
}

}  // namespace

// ------------------------------------------------------------------ Flashers ----

Flashers::Flashers() {
  for (int k = 0; k < 2; k++) {
    lens[k] = basic({.color = BLUE_OFF, .toneMapped = false});
    glowMat[k] = spriteMaterial({.color = Color(0x4f86ff), .map = glow(), .transparent = true, .depthWrite = false, .toneMapped = false, .blending = Blending::Additive});
  }
}

void Flashers::lamp(Object3D& parent, double x0, double x1, double y0, double y1, double z0, double z1, int k, double size) {
  parent.add(box(x0, x1, y0, y1, z0, z1, lens[k], false));
  auto s = make<Sprite>(glowMat[k]);
  s->position.set((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
  s->scale.setScalar(size);
  s->visible = false;
  parent.add(s);
  glows[k].push_back(s);
}

void Flashers::head(Object3D& parent, const ObjectPtr& flash, int k) {
  flash->visible = false;
  parent.add(flash);
  heads[k].push_back(flash);
}

void Flashers::update(bool on, double t) {
  int st = 0;
  if (on) {
    // double flashes, the two groups in turn; the headlights in turn, the same rhythm
    const double ph = std::fmod(std::fmod(t, 0.8) + 0.8, 0.8);
    const bool a = ph < 0.07 || (ph >= 0.13 && ph < 0.2);
    const bool b = (ph >= 0.4 && ph < 0.47) || (ph >= 0.53 && ph < 0.6);
    st = 1 | (a ? 2 : 0) | (b ? 4 : 0) | (ph < 0.4 ? 8 : 16);
  }
  if (st == state) return;
  state = st;
  for (int k = 0; k < 2; k++) {
    const bool lit = (st & (k == 0 ? 2 : 4)) != 0;
    lens[k]->color.copy(lit ? BLUE_ON : BLUE_OFF);
    for (const ObjectPtr& s : glows[k]) s->visible = lit;
    const bool head_ = (st & (k == 0 ? 8 : 16)) != 0;
    for (const ObjectPtr& h : heads[k]) h->visible = head_;
  }
}

// ------------------------------------------------------------ EmergencyModel ----

EmergencyModel::EmergencyModel(int kind_) : kind(kind_) {
  static const char* const names[] = {"police car", "ambulance", "fire engine", "emergency doctor's car"};
  group->name = kind >= 0 && kind < 4 ? names[kind] : "emergency vehicle";
  if (kind == EM_AMBULANCE) buildAmbulance();
  else if (kind == EM_FIRE) buildFireEngine();
  else buildCar(kind == EM_DOCTOR);
}

EmergencyModel::~EmergencyModel() = default;

void EmergencyModel::setLights(bool on, double time) { flash.update(on, time); }

void EmergencyModel::setSign(bool on) {
  if (sign) sign->visible = on;
}

void EmergencyModel::setMotion(double spin, bool braking, bool left, bool right, double steer) {
  if (car) {
    const double r = car->dims.wheelRadius;
    const double a = (spin * 0.31) / r;
    car->setWheels({steer, steer, 0, 0}, {a, a, a, a}, {r, r, r, r});
    car->setLamps({.low = true, .high = false, .brake = braking, .reverse = false, .left = left, .right = right});
    return;
  }
  const double a = (spin * 0.31) / wheelRadius;
  // (the front two steered: + to the right, as the car models take it)
  for (size_t i = 0; i < wheels.size(); i++) wheels[i]->rotation.set(-a, i < 2 ? -steer : 0, 0, Euler::YXZ);
  for (const std::shared_ptr<Mesh>& b : brakes) b->material = braking ? M().STOP : M().TAIL;
  for (const Blinker& k : blinkers) k.mesh->material = (k.side < 0 ? left : right) ? M().AMBER : M().AMBER_OFF;
}

/**
 * The police car (silver, a blue band along the doors with POLIZEI on it)
 * or the doctor's car (white, a red band, NOTARZT): a car body with a light
 * bar on the roof, blue flashers in the grille.
 */
void EmergencyModel::buildCar(bool doctor) {
  const CarDims& dims = doctor ? DOCTOR_DIMS : POLICE_DIMS;
  const std::string text = doctor ? "NOTARZT" : "POLIZEI";
  const std::string colour = doctor ? SIGNAL_RED : POLICE_BLUE;
  CarExteriorOptions opts;
  opts.dims = &dims;
  opts.lettering = text;
  car = std::make_unique<CarExterior>(doctor ? 0xf2f2ee : 0xc9ced3, opts);
  const CarShell& sh = car->shell;
  Group& body = *car->group;
  // (the car is built around the middle of its wheelbase: moved to the middle of its length)
  body.position.z = -(sh.zFront + sh.zRear) / 2;
  group->add(car->group);
  // the word on the bumpers in the colour of the livery
  body.traverse([&](Object3D& o) {
    auto* m = dynamic_cast<Mesh*>(&o);
    if (m && m->name.rfind("lettering", 0) == 0 && m->material) m->material->color.set(colour);
  });
  // the band along the doors, between the wheel arches
  const double hw = sh.halfWidth;
  const double z0 = sh.zAxleF + dims.wheelRadius + 0.14;
  const double z1 = sh.zAxleR - dims.wheelRadius - 0.14;
  const MaterialPtr band = lambert({.map = bandTexture(text, colour), .polygonOffset = true, .polygonOffsetFactor = -2, .polygonOffsetUnits = -4});
  const double ym = 0.63;
  const double y0 = 0.46;
  const double y1 = std::min(sh.beltY - 0.06, 0.82);
  // (how far out the flank is: the widest crease at ym, a little less above and below)
  const auto out = [&](double y) { return y < ym ? hw * (0.985 + (0.015 * (y - 0.33)) / (ym - 0.33)) : hw * (1 - (0.025 * (y - ym)) / std::max(0.05, sh.beltY - ym)); };
  for (const double side : {-1.0, 1.0}) body.add(flankStrip({out(y0), out(ym), out(y1)}, y0, ym, y1, z0, z1, side, band));
  // the light bar on the roof, a little behind the top of the windscreen
  const double barZ = sh.zRoofFront + 0.32;
  lightBar(body, flash, 1.12, sh.roofY - 0.025, barZ);
  // blue flashers in the grille, low on the nose
  for (const double side : {-1.0, 1.0}) flash.lamp(body, side * 0.3 - 0.075, side * 0.3 + 0.075, 0.42, 0.48, sh.zFront - 0.02, sh.zFront + 0.01, side < 0 ? 0 : 1, 0.55);
  // the headlights flashing in turn: a bright cover over each
  for (const double side : {-1.0, 1.0}) {
    GeometryPtr g = boxGeometry(0.37, 0.115, 0.06);
    g->applyMatrix4(Matrix4().makeRotationY(side * -0.35));
    g->translate(side * hw * 0.62, 0.635, sh.zFront + 0.11);
    flash.head(body, make<Mesh>(g, M().FLASH), side < 0 ? 0 : 1);
  }
  if (doctor) return;
  // the sign that rises behind the light bar: "STOP POLIZEI" to the traffic behind
  auto sign_ = make<Group>();
  sign_->add(box(-0.47, 0.47, 0, 0.3, -0.025, 0.015, M().BAR, false));
  auto face = plate(0.9, 0.27, 0, 0.15, 0.017, "z+", basic({.map = stopSignTexture(), .toneMapped = false}));
  sign_->add(face);
  sign_->position.set(0, sh.roofY + 0.06, barZ + 0.24);
  sign_->visible = false;
  body.add(sign_);
  sign = sign_;
}

/**
 * The ambulance: a van's cab with a box body behind it, white with the
 * fluorescent red band; blue lamps at the corners of the box, in the grille,
 * and a light bar on the cab.
 */
void EmergencyModel::buildAmbulance() {
  const double len = 6.95;
  const double wb = 3.66;
  const double zf = -(wb / 2 + 1.0);
  const double zr = zf + len;
  auto gp = make<Group>();
  Group& g = *gp;
  g.position.z = -(zf + zr) / 2;
  group->add(gp);
  const double cw = 2.02;
  const double zBox = zf + 2.0;
  const EmMaterials& m = M();
  // the cab: bonnet, sloping windscreen, roof
  g.add(sideSolid({{zf, 0.42}, {zf, 0.98}, {zf + 0.22, 1.12}, {zf + 0.85, 1.2}, {zf + 1.45, 2.22}, {zBox + 0.05, 2.28}, {zBox + 0.05, 0.42}}, cw, m.WHITE));
  // its windscreen and side windows, the grille, the bumper
  slopedScreen(g, cw - 0.16, 1.08, Vector3(0, 1.26, zf + 0.89), Vector3(0, 2.16, zf + 1.42));
  for (const double side : {-1.0, 1.0}) g.add(plate(0.52, 0.72, side * (cw / 2 + 0.006), 1.72, zf + 1.68, side > 0 ? "x+" : "x-", m.GLASS));
  g.add(plate(1.1, 0.3, 0, 0.8, zf - 0.006, "z-", m.DARK));
  g.add(box(-cw / 2 - 0.02, cw / 2 + 0.02, 0.3, 0.56, zf - 0.06, zf + 0.25, m.DARK));
  for (const double side : {-1.0, 1.0}) {
    g.add(box(side * 0.74 - 0.16, side * 0.74 + 0.16, 0.8, 0.94, zf - 0.03, zf + 0.01, m.HEAD, false));
    // (the headlights flashing in turn: drawn a little in front of them)
    auto flashBox = box(side * 0.74 - 0.17, side * 0.74 + 0.17, 0.795, 0.945, zf - 0.04, zf + 0.005, m.FLASH, false);
    flash.head(g, flashBox, side < 0 ? 0 : 1);
    auto ind = box(side * 0.95 - 0.04, side * 0.95 + 0.04, 0.82, 0.92, zf - 0.03, zf + 0.01, m.AMBER_OFF, false);
    blinkers.push_back({ind, static_cast<int>(side)});
    g.add(ind);
    // the mirror on its arm
    g.add(box(side * (cw / 2) - 0.02, side * (cw / 2 + 0.24), 1.86, 1.9, zf + 1.3, zf + 1.34, m.DARK, false));
    g.add(box(side * (cw / 2 + 0.2) - 0.05, side * (cw / 2 + 0.2) + 0.05, 1.62, 1.98, zf + 1.28, zf + 1.36, m.DARK, false));
    // blue flashers in the grille
    flash.lamp(g, side * 0.36 - 0.08, side * 0.36 + 0.08, 0.66, 0.74, zf - 0.03, zf + 0.01, side < 0 ? 0 : 1, 0.6);
  }
  // the box: white, the livery on its sides, doors at the back
  const double bw = 2.3;
  g.add(box(-bw / 2, bw / 2, 0.62, 2.88, zBox, zr, m.WHITE));
  for (const double side : {-1.0, 1.0}) {
    const MaterialPtr mat = lambert({.map = ambulanceSide(side > 0)});
    g.add(plate(zr - zBox - 0.1, 2.06, side * (bw / 2 + 0.006), 1.72, (zBox + zr) / 2, side > 0 ? "x+" : "x-", mat));
  }
  g.add(plate(bw - 0.12, 2.1, 0, 1.72, zr + 0.006, "z+", lambert({.map = rearTexture(false)})));
  g.add(box(-bw / 2, bw / 2, 0.4, 0.62, zr - 0.25, zr + 0.06, m.DARK));
  // lamps at the back
  for (const double side : {-1.0, 1.0}) {
    auto stop = box(side * 1.02 - 0.1, side * 1.02 + 0.1, 0.72, 1.02, zr + 0.006, zr + 0.03, m.TAIL, false);
    brakes.push_back(stop);
    g.add(stop);
    auto ind = box(side * 1.02 - 0.1, side * 1.02 + 0.1, 1.04, 1.16, zr + 0.006, zr + 0.03, m.AMBER_OFF, false);
    blinkers.push_back({ind, static_cast<int>(side)});
    g.add(ind);
    // blue lamps at the four upper corners of the box (seen from the front, the side and the back)
    const auto [x0, x1] = across(side, bw / 2 - 0.13, bw / 2 + 0.05);
    flash.lamp(g, x0, x1, 2.7, 2.86, zBox - 0.05, zBox + 0.2, side < 0 ? 0 : 1, 0.95);
    flash.lamp(g, x0, x1, 2.7, 2.86, zr - 0.2, zr + 0.05, side < 0 ? 0 : 1, 0.95);
  }
  // a low light bar on the cab, in front of the box
  lightBar(g, flash, 1.5, 2.25, zf + 1.78, 0.24);
  addWheels(g, 0.36, wb, 1.72, 0.24, 0.42);
}

/**
 * The fire engine: a crew cab in front of a body with roller shutters,
 * red with white strips, a ladder on the roof; a light bar on the cab,
 * blue lamps at the back corners and in the bumper.
 */
void EmergencyModel::buildFireEngine() {
  const double len = 8.6;
  const double wb = 4.2;
  const double zf = -(wb / 2 + 1.45);
  const double zr = zf + len;
  auto gp = make<Group>();
  Group& g = *gp;
  g.position.z = -(zf + zr) / 2;
  group->add(gp);
  const double w = 2.5;
  const double zBody = zf + 2.8;
  const EmMaterials& m = M();
  // the cab, nearly upright, the crew's seats behind the driver's
  g.add(sideSolid({{zf, 0.72}, {zf, 1.92}, {zf + 0.16, 2.96}, {zf + 0.3, 3.04}, {zf + 2.75, 3.04}, {zf + 2.75, 0.72}}, w, m.FIRE_RED));
  slopedScreen(g, w - 0.3, 0.86, Vector3(0, 1.98, zf + 0.01), Vector3(0, 2.86, zf + 0.15));
  const MaterialPtr word_ = lambert({.map = fireWord()});
  g.add(plate(1.5, 0.28, 0, 1.68, zf - 0.008, "z-", word_));
  for (const double side : {-1.0, 1.0}) {
    const std::string f = side > 0 ? "x+" : "x-";
    // the windows: the driver's and the crew's
    g.add(plate(0.86, 0.78, side * (w / 2 + 0.006), 2.42, zf + 0.78, f, m.GLASS));
    g.add(plate(1.0, 0.72, side * (w / 2 + 0.006), 2.45, zf + 2.05, f, m.GLASS));
    g.add(plate(1.0, 0.19, side * (w / 2 + 0.006), 1.78, zf + 0.78, f, word_));
    // the steps up to the doors, the mirrors
    const auto [s0, s1] = across(side, w / 2 - 0.1, w / 2 + 0.12);
    g.add(box(s0, s1, 0.5, 0.56, zf + 0.35, zf + 2.55, m.FRAME));
    g.add(box(side * (w / 2 + 0.1) - 0.06, side * (w / 2 + 0.1) + 0.06, 2.0, 2.5, zf + 0.1, zf + 0.18, m.DARK, false));
    // headlights (and their flashing), indicators, blue flashers in the bumper
    g.add(box(side * 0.92 - 0.16, side * 0.92 + 0.16, 1.05, 1.22, zf - 0.03, zf + 0.02, m.HEAD, false));
    flash.head(g, box(side * 0.92 - 0.17, side * 0.92 + 0.17, 1.045, 1.225, zf - 0.04, zf + 0.01, m.FLASH, false), side < 0 ? 0 : 1);
    auto ind = box(side * 1.14 - 0.05, side * 1.14 + 0.05, 1.06, 1.2, zf - 0.03, zf + 0.02, m.AMBER_OFF, false);
    blinkers.push_back({ind, static_cast<int>(side)});
    g.add(ind);
    flash.lamp(g, side * 0.42 - 0.09, side * 0.42 + 0.09, 0.66, 0.76, zf - 0.11, zf - 0.07, side < 0 ? 0 : 1, 0.65);
  }
  g.add(box(-w / 2 - 0.02, w / 2 + 0.02, 0.52, 0.9, zf - 0.08, zf + 0.25, m.DARK));
  // the body with its shutters, the ladder on the roof
  g.add(box(-w / 2, w / 2, 0.85, 3.22, zBody, zr, m.FIRE_RED));
  const MaterialPtr shutters = lambert({.map = fireSide()});
  for (const double side : {-1.0, 1.0}) g.add(plate(zr - zBody - 0.12, 2.3, side * (w / 2 + 0.006), 2.0, (zBody + zr) / 2, side > 0 ? "x+" : "x-", shutters));
  g.add(plate(w - 0.1, 2.3, 0, 2.0, zr + 0.006, "z+", lambert({.map = rearTexture(true)})));
  for (const double x : {-0.26, 0.26}) g.add(box(x - 0.03, x + 0.03, 3.26, 3.34, zBody + 0.3, zr - 0.2, m.STEEL, false));
  for (double z = zBody + 0.45; z < zr - 0.3; z += 0.32) g.add(box(-0.26, 0.26, 3.28, 3.31, z - 0.015, z + 0.015, m.STEEL, false));
  g.add(box(-w / 2 + 0.1, w / 2 - 0.1, 0.45, 0.85, zr - 0.3, zr + 0.05, m.DARK));
  for (const double side : {-1.0, 1.0}) {
    auto stop = box(side * 1.08 - 0.1, side * 1.08 + 0.1, 0.95, 1.25, zr + 0.006, zr + 0.03, m.TAIL, false);
    brakes.push_back(stop);
    g.add(stop);
    auto ind = box(side * 1.08 - 0.1, side * 1.08 + 0.1, 1.27, 1.39, zr + 0.006, zr + 0.03, m.AMBER_OFF, false);
    blinkers.push_back({ind, static_cast<int>(side)});
    g.add(ind);
    // blue lamps at the back corners, high up
    const auto [x0, x1] = across(side, w / 2 - 0.16, w / 2 + 0.04);
    flash.lamp(g, x0, x1, 3.1, 3.3, zr - 0.2, zr + 0.05, side < 0 ? 0 : 1, 1.0);
  }
  lightBar(g, flash, 1.75, 3.04, zf + 0.5, 0.28);
  addWheels(g, 0.52, wb, 2.06, 0.32, 0.6);
}

/** The wheels of a vehicle built of boxes: single at the front, twin at the back. */
void EmergencyModel::addWheels(Group& g, double r, double wb, double track, double front, double rear) {
  wheelRadius = r;
  const std::array<std::array<double, 3>, 4> spots = {{{-track / 2, -wb / 2, front}, {track / 2, -wb / 2, front}, {-track / 2 + 0.04, wb / 2, rear}, {track / 2 - 0.04, wb / 2, rear}}};
  for (const auto& [x, z, wd] : spots) {
    auto wh = wheel(r, wd);
    wh->position.set(x, r, z);
    wheels.push_back(wh);
    g.add(wh);
  }
}

std::shared_ptr<EmergencyModel> buildPoliceCar() { return std::make_shared<EmergencyModel>(EM_POLICE); }

// ------------------------------------------------------------ EmergencyFleet ----

EmergencyFleet::EmergencyFleet() { group->name = "emergency vehicles"; }

void EmergencyFleet::update(const std::vector<TrafficCar*>& cars, double time) {
  const bool blinkOn = static_cast<int64_t>(std::floor(time * 3)) % 2 == 0;
  seen.clear();
  for (const TrafficCar* c : cars) {
    const int kind = emKind(c->style);
    if (kind < 0) continue;
    seen.insert(c->id);
    size_t i = 0;
    while (i < shown.size() && shown[i].first != c->id) i++;
    EmergencyModel* m = i < shown.size() ? shown[i].second.get() : nullptr;
    if (m && m->kind != kind) {
      release(i);
      m = nullptr;
    }
    if (!m) {
      std::vector<std::unique_ptr<EmergencyModel>>& spare = pool[static_cast<size_t>(kind)];
      std::unique_ptr<EmergencyModel> made;
      if (!spare.empty()) {
        made = std::move(spare.back());
        spare.pop_back();
      } else made = std::make_unique<EmergencyModel>(kind);
      made->setSign(false);
      m = made.get();
      shown.emplace_back(c->id, std::move(made));
      group->add(m->group);
    }
    // (where it is drawn: eased through the corners of its way – sim/smoothpose.ts)
    const double dx = c->drawn ? c->drawn->x : c->x;
    const double dz = c->drawn ? c->drawn->z : c->z;
    const double dyaw = c->drawn ? c->drawn->yaw : c->yaw;
    m->group->position.set(dx, c->y, dz);
    m->group->rotation.set(c->pitch, dyaw, 0, Euler::YXZ);
    m->setMotion(c->wheelSpin, c->braking, blinkOn && c->blinker < 0, blinkOn && c->blinker > 0, c->drawn && c->drawn->steer ? *c->drawn->steer : 0);
    // (each in a rhythm of its own)
    m->setLights(emLit(c->style), time + static_cast<double>(c->id % 7) * 0.13);
  }
  for (size_t i = shown.size(); i-- > 0;)
    if (!seen.count(shown[i].first)) release(i);
}

void EmergencyFleet::release(size_t i) {
  std::unique_ptr<EmergencyModel> m = std::move(shown[i].second);
  shown.erase(shown.begin() + static_cast<std::ptrdiff_t>(i));
  group->remove(m->group);
  pool[static_cast<size_t>(m->kind)].push_back(std::move(m));
}

}  // namespace fs

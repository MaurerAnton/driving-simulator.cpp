// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/theory.ts @ v1.7.7
#include "render/theory.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "engine/curves.h"
#include "engine/geometries.h"
#include "game/theoryscenes.h"
#include "game/theorystage.h"
#include "platform/canvas.h"
#include "render/carBody.h"
#include "render/cyclist.h"
#include "render/fleet.h"
#include "render/signs.h"
#include "render/tiles.h"
#include "render/trams.h"
#include "render/truck.h"
#include "world/types.h"

namespace fs {

namespace {
constexpr double kPi = 3.14159265358979323846;

/** the three bodies of the traffic's cars */
const std::vector<CarDims>& STYLES() {
  static const auto* s = [] {
    CarDims b = COMPACT_DIMS;
    b.length = 4.6;
    b.height = 1.5;
    b.wheelbase = 2.7;
    b.tail = 1;
    CarDims c = COMPACT_DIMS;
    c.length = 4.45;
    c.height = 1.42;
    c.wheelbase = 2.68;
    c.tail = 2;
    return new std::vector<CarDims>{COMPACT_DIMS, b, c};
  }();
  return *s;
}
/** an ambulance or a police van: a tall estate (its measures are FOOT.van's) */
const CarDims& VAN() {
  static const auto* v = new CarDims{.length = 5.9, .width = 2.02, .height = 2.35, .wheelbase = 3.66, .track = 1.74, .wheelRadius = 0.36, .tyreWidth = 0.225, .frontOverhang = 1.0, .tail = 1};
  return *v;
}

struct Mats {
  std::map<std::string, MaterialPtr> surface = {
      {"asphalt", lambert({.color = Color(0x3d4044), .polygonOffset = true, .polygonOffsetFactor = -2, .polygonOffsetUnits = -6})},
      {"walk", lambert({.color = Color(0x9d9c96), .polygonOffset = true, .polygonOffsetFactor = -2, .polygonOffsetUnits = -6})},
      {"island", lambert({.color = Color(0x55803d), .polygonOffset = true, .polygonOffsetFactor = -2, .polygonOffsetUnits = -6})},
      {"apron", lambert({.color = Color(0x7c776f), .polygonOffset = true, .polygonOffsetFactor = -2, .polygonOffsetUnits = -6})},
      {"paint", basic({.color = Color(0xeeeeea), .polygonOffset = true, .polygonOffsetFactor = -4, .polygonOffsetUnits = -12})},
      {"red", lambert({.color = Color(0xa84a3c), .polygonOffset = true, .polygonOffsetFactor = -3, .polygonOffsetUnits = -9})},
      {"steel", lambert({.color = Color(0xb4b8bc), .polygonOffset = true, .polygonOffsetFactor = -4, .polygonOffsetUnits = -12})},
  };
  MaterialPtr kerb = lambert({.color = Color(0xb9b8b1), .side = Side::Double});
  MaterialPtr metal = lambert({.color = Color(0xa3a8ad)});
  MaterialPtr kerbMetal = lambert({.color = Color(0xa3a8ad), .side = Side::Double});
  MaterialPtr pole = lambert({.color = Color(0x6f757a)});
  MaterialPtr housing = lambert({.color = Color(0x1d2022)});
  MaterialPtr zone = basic({.color = Color(0xff8a1f),
                            .transparent = true,
                            .opacity = 0.38,
                            .side = Side::Double,
                            .depthWrite = false,
                            .polygonOffset = true,
                            .polygonOffsetFactor = -6,
                            .polygonOffsetUnits = -18});
  MaterialPtr gap = basic({.color = Color(0xffcc00), .transparent = true, .opacity = 0.95, .depthTest = false});
};

const Mats& MAT() {
  static const Mats* m = new Mats();
  return *m;
}

/** LAMP_ON (red, amber, green), lamp::OFF */
namespace lamp {
constexpr uint32_t RED = 0xff2a1a;
constexpr uint32_t AMBER = 0xffb000;
constexpr uint32_t GREEN = 0x1aff6a;
constexpr uint32_t OFF = 0x161616;
}  // namespace lamp

/** the height of a label, as a part of the height of the picture (roughly) */
constexpr double LABEL_H = 0.062;

/** The longest edge of a draped triangle (m): short enough to follow the ground. */
constexpr double CELL = 2.4;

/** A label floating over a vehicle: its name in a dark pill, pointing down. */
std::shared_ptr<Sprite> labelSprite(const std::string& text, bool accent) {
  Canvas c(8, 8);
  Ctx2D g = c.getContext2D();
  const std::string font = "700 44px system-ui, sans-serif";
  g.font(font);
  const int w = static_cast<int>(std::min(1000.0, std::ceil(g.measureText(text).width) + 56));
  c.width(w);
  c.height(110);
  g = c.getContext2D();
  g.font(font);
  g.fillStyle(accent ? "rgba(255, 204, 0, 0.96)" : "rgba(16, 24, 34, 0.9)");
  g.beginPath();
  g.roundRect(3, 3, w - 6, 74, 22);
  g.fill();
  g.beginPath();
  g.moveTo(w / 2.0 - 16, 76);
  g.lineTo(w / 2.0 + 16, 76);
  g.lineTo(w / 2.0, 104);
  g.closePath();
  g.fill();
  g.fillStyle(accent ? "#141000" : "#ffffff");
  g.textAlign("center");
  g.textBaseline("middle");
  g.fillText(text, w / 2.0, 42, w - 30);
  TexturePtr tex = canvasTexture(c);
  tex->srgb = true;
  // (the same size on the screen however far away: about a sixteenth of its height)
  MaterialPtr mat = spriteMaterial({.map = tex, .transparent = true, .depthWrite = false, .depthTest = false});
  mat->sizeAttenuation = false;
  auto sprite = make<Sprite>(mat);
  const double h = LABEL_H;
  sprite->scale.set((h * w) / 110, h, 1);
  sprite->center.set(0.5, 0.02);
  sprite->renderOrder = 20;
  return sprite;
}

void disposeSprite(const std::shared_ptr<Sprite>& s) {
  if (s->material->map) s->material->map->dispose();
  s->material->dispose();
}

/** The look of a child: the first child among the looks from `seed` on. */
int childLook(int seed) {
  for (int id = seed; id < seed + 600; id++)
    if (lookOf(id).scale < 0.82) return id;
  return seed;
}
}  // namespace

TheoryView::TheoryView(SignRenderer& s) : signs(s) {
  people = std::make_unique<PeopleRenderer>(24);
  rail = std::make_unique<RailActors>();
  extras = std::make_unique<TheoryExtras>();
  visual.speedKmh = 0;
  visual.rpm = 800;
  visual.gear = 1;
  visual.lowBeam = true;
  visual.ignition = true;
  visual.engineRunning = true;
  visual.seatbelt = true;
  visual.fuel = 0.6;
  visual.automatic = true;
  visual.wheelSteer = {0, 0, 0, 0};
  visual.wheelSpin = {0, 0, 0, 0};
  visual.wheelY = {0.5, 0.5, 0.5, 0.5};
  group->name = "theory lessons";
  group->visible = false;
  group->add(place, movers, people->group, rail->group, extras->group);
  // the blind spot beside and behind the right rear of a car (its own frame: −z ahead, +x right)
  const Shape shape({Vector2(0.95, -0.4), Vector2(3.9, 1.6), Vector2(4.3, 6.4), Vector2(0.95, 3.4)});
  GeometryPtr zg = shapeGeometry(shape);
  zg->rotateX(kPi / 2);
  zone = make<Mesh>(zg, MAT().zone);
  zone->visible = false;
  zone->renderOrder = 6;
  gapBar = make<Mesh>(boxGeometry(1, 0.05, 0.16), MAT().gap);
  gapBar->visible = false;
  gapBar->renderOrder = 19;
  group->add(zone, gapBar);
}

TheoryView::~TheoryView() = default;

bool TheoryView::build(const std::shared_ptr<theory::Scene>& sc, const StageFrame& f, const std::function<double(double x, double z)>& heightAt) {
  const std::array<double, 4>& b = sc->stage.bounds;
  // (the ground under the corners and the middle must be there)
  for (const auto& [x, z] : std::vector<std::array<double, 2>>{{b[0], b[1]}, {b[2], b[1]}, {b[0], b[3]}, {b[2], b[3]}, {(b[0] + b[2]) / 2, (b[1] + b[3]) / 2}}) {
    const auto [wx, wz] = f.toWorld(x, z);
    if (std::isnan(heightAt(wx, wz))) return false;
  }
  clear();
  scene = sc;
  frame = f;
  const auto [cx, cz] = f.toWorld((b[0] + b[2]) / 2, (b[1] + b[3]) / 2);
  const double base = heightAt(cx, cz);
  height = [heightAt, base](double x, double z) {
    const double h = heightAt(x, z);
    return std::isnan(h) ? base : h;
  };
  buildPlace(*sc);
  buildActors(*sc);
  group->visible = true;
  return true;
}

void TheoryView::clear() {
  for (const GeometryPtr& m : made) m->dispose();
  made.clear();
  for (const auto& im : madeInstanced) im->dispose();
  madeInstanced.clear();
  madeMaterials.clear();
  place->clear();
  movers->clear();
  if (signTile) {
    signs.remove(*signTile);
    signTile = nullptr;
  }
  for (const Label& l : labels) {
    disposeSprite(l.sprite);
    group->remove(l.sprite);
  }
  labels.clear();
  if (gapLabel) {
    disposeSprite(gapLabel);
    group->remove(gapLabel);
    gapLabel = nullptr;
    gapText = "";
  }
  for (int k = 0; k < 3; k++) {
    for (auto& c : usedCars[k]) pool.cars[k].push_back(std::move(c));
    usedCars[k].clear();
  }
  for (auto& v : usedVans) pool.vans.push_back(std::move(v));
  usedVans.clear();
  for (auto& c : usedBikes) pool.bikes.push_back(std::move(c));
  usedBikes.clear();
  extras->clear();
  actors.clear();
  lights.clear();
  people->update({}, 0);
  trams.clear();
  rail->update(trams);
  zone->visible = false;
  gapBar->visible = false;
  scene = nullptr;
}

void TheoryView::hide() {
  clear();
  group->visible = false;
}

// ------------------------------------------------------------------ the place

void TheoryView::buildPlace(const theory::Scene& sc) {
  const StageFrame& f = *frame;
  const theory::Stage& st = sc.stage;
  // the surfaces, one mesh per kind (in the order the kinds first come)
  std::vector<std::pair<std::string, std::vector<float>>> byKind;
  for (const theory::Surface& s : st.surfaces) {
    auto it = std::find_if(byKind.begin(), byKind.end(), [&s](const auto& e) { return e.first == s.kind; });
    if (it == byKind.end()) {
      byKind.emplace_back(s.kind, std::vector<float>{});
      it = byKind.end() - 1;
    }
    drape(s.ring, s.holes, theory::LIFT(s.kind), it->second);
  }
  for (auto& [kind, pos] : byKind) {
    if (pos.empty()) continue;
    auto geo = make<BufferGeometry>();
    geo->setAttribute("position", attr(std::move(pos), 3));
    geo->computeVertexNormals();
    made.push_back(geo);
    auto mesh = make<Mesh>(geo, MAT().surface.at(kind));
    mesh->receiveShadow = kind == "asphalt" || kind == "walk";
    place->add(mesh);
  }
  // the kerbs: a low wall from one surface up to the other
  std::vector<float> kerb;
  for (const theory::Kerb& k : st.kerbs) {
    const double lo = theory::LIFT(k.lo);
    const double hi = theory::LIFT(k.hi);
    for (size_t i = 0; i + 3 < k.line.size(); i += 2) {
      const auto [ax, az] = f.toWorld(k.line[i], k.line[i + 1]);
      const auto [bx, bz] = f.toWorld(k.line[i + 2], k.line[i + 3]);
      const double ya = height(ax, az);
      const double yb = height(bx, bz);
      for (const double v : {ax, ya + lo, az, bx, yb + lo, bz, bx, yb + hi, bz, ax, ya + lo, az, bx, yb + hi, bz, ax, ya + hi, az}) kerb.push_back(static_cast<float>(v));
    }
  }
  if (!kerb.empty()) {
    auto geo = make<BufferGeometry>();
    geo->setAttribute("position", attr(std::move(kerb), 3));
    geo->computeVertexNormals();
    made.push_back(geo);
    place->add(make<Mesh>(geo, MAT().kerb));
  }
  // crash barriers: posts every 4 m and the rail along them
  for (const std::vector<double>& gd : st.guards) {
    const auto [ax, az] = f.toWorld(gd[0], gd[1]);
    const auto [bx, bz] = f.toWorld(gd[2], gd[3]);
    const double len = std::hypot(bx - ax, bz - az);
    const int n = std::max(1, static_cast<int>(jsRound(len / 4)));
    GeometryPtr postGeo = boxGeometry(0.1, 0.75, 0.1);
    postGeo->translate(0, 0.375, 0);
    auto posts = make<InstancedMesh>(postGeo, MAT().metal, n + 1);
    Matrix4 m4;
    std::vector<float> beam;
    for (int i = 0; i <= n; i++) {
      const double x = ax + ((bx - ax) * i) / n;
      const double z = az + ((bz - az) * i) / n;
      const double y = height(x, z);
      m4.makeTranslation(x, y, z);
      posts->setMatrixAt(i, m4);
      if (i < n) {
        const double x2 = ax + ((bx - ax) * (i + 1)) / n;
        const double z2 = az + ((bz - az) * (i + 1)) / n;
        const double y2 = height(x2, z2);
        for (const double v : {x, y + 0.48, z, x2, y2 + 0.48, z2, x2, y2 + 0.78, z2, x, y + 0.48, z, x2, y2 + 0.78, z2, x, y + 0.78, z}) beam.push_back(static_cast<float>(v));
      }
    }
    posts->instanceMatrix->needsUpdate();
    posts->computeBoundingSphere();
    made.push_back(postGeo);
    madeInstanced.push_back(posts);
    auto geo = make<BufferGeometry>();
    geo->setAttribute("position", attr(std::move(beam), 3));
    geo->computeVertexNormals();
    made.push_back(geo);
    place->add(posts, make<Mesh>(geo, MAT().kerbMetal));
  }
  // the signs, drawn by the signs of the map (one "tile" of their own)
  std::vector<SignPost> posts;
  for (const theory::SignPlace& s : st.signs) {
    const auto [x, z] = f.toWorld(s.x, s.z);
    posts.push_back(SignPost{.x = x, .y = height(x, z) + theory::liftAt(st, s.x, s.z), .z = z, .yaw = f.yawToWorld(s.yaw), .s = s.codes, .h = 2.0});
  }
  // the traffic lights: a pole, a box, three lamps – or two for people on foot; the green-arrow sign beside the red lamp
  for (const theory::LightPlace& l : st.lights) {
    const auto [x, z] = f.toWorld(l.x, l.z);
    const double y = height(x, z) + theory::liftAt(st, l.x, l.z);
    const double yaw = f.yawToWorld(l.yaw);
    auto g = make<Group>();
    g->position.set(x, y, z);
    g->rotation.y = yaw;
    const double top = l.ped ? 2.45 : 3.4;
    GeometryPtr poleGeo = cylinderGeometry(0.055, 0.065, top, 8);
    poleGeo->translate(0, top / 2, 0);
    auto pole = make<Mesh>(poleGeo, MAT().pole);
    const int n = l.ped ? 2 : 3;
    const double boxH = l.ped ? 0.62 : 0.92;
    GeometryPtr boxGeo = boxGeometry(0.3, boxH, 0.2);
    boxGeo->translate(0, top - boxH / 2, 0.06);
    auto box = make<Mesh>(boxGeo, MAT().housing);
    g->add(pole, box);
    made.push_back(poleGeo);
    made.push_back(boxGeo);
    std::vector<MaterialPtr> lamps;
    for (int k = 0; k < n; k++) {
      MaterialPtr mat = basic({.color = Color(lamp::OFF)});
      // (the box faces the way the light's yaw points: its lamps on that side, −z)
      GeometryPtr lampGeo = circleGeometry(l.ped ? 0.1 : 0.095, 16);
      auto lamp = make<Mesh>(lampGeo, mat);
      lamp->position.set(0, top - boxH / 2 + (n == 3 ? 0.29 - k * 0.29 : 0.15 - k * 0.3), -0.045);
      lamp->rotation.y = kPi;
      g->add(lamp);
      lamps.push_back(mat);
      made.push_back(lampGeo);
      madeMaterials.push_back(mat);
    }
    place->add(g);
    lights.push_back(Light{.id = l.id, .ped = l.ped, .lamps = lamps, .shown = ""});
    if (l.arrow) {
      // Zeichen 720 to the right of the head (seen by the traffic coming), level with its red lamp
      const double right = 0.42;
      posts.push_back(SignPost{.x = x - std::cos(yaw) * right, .y = y, .z = z + std::sin(yaw) * right, .yaw = yaw, .s = {"720"}, .h = top - 0.5});
    }
  }
  if (!posts.empty()) {
    const auto [cx, cz] = f.toWorld(0, 0);
    signTile = std::make_unique<LoadedTile>();
    signTile->key = "theory";
    signTile->header.signs = posts;
    signTile->header.cx = cx;
    signTile->header.cz = cz;
    signTile->header.yBase = height(cx, cz);
    signTile->group = make<Group>();
    signs.add(*signTile);
    place->add(signTile->group);
  }
  // trees, posts, a warning triangle, roadworks, a tunnel
  extras->build(st, f, height);
}

void TheoryView::drape(const std::vector<double>& ring, const std::vector<std::vector<double>>& holes, double lift, std::vector<float>& out) const {
  const StageFrame& f = *frame;
  std::vector<Vector2> contour;
  for (size_t i = 0; i + 1 < ring.size(); i += 2) contour.emplace_back(ring[i], ring[i + 1]);
  std::vector<std::vector<Vector2>> hs;
  for (const std::vector<double>& h : holes) {
    std::vector<Vector2> v;
    for (size_t i = 0; i + 1 < h.size(); i += 2) v.emplace_back(h[i], h[i + 1]);
    hs.push_back(std::move(v));
  }
  std::vector<std::array<int, 3>> faces;
  try {
    faces = ShapeUtils::triangulateShape(contour, hs);
  } catch (...) {
    return;
  }
  // (after cutting: it drops an end point that repeats the first, and the faces count without it)
  std::vector<Vector2> all = contour;
  for (const auto& h : hs) all.insert(all.end(), h.begin(), h.end());
  const auto put = [&](double x, double z) {
    const auto [wx, wz] = f.toWorld(x, z);
    out.push_back(static_cast<float>(wx));
    out.push_back(static_cast<float>(height(wx, wz) + lift));
    out.push_back(static_cast<float>(wz));
  };
  std::function<void(double, double, double, double, double, double, int)> tri = [&](double ax, double az, double bx, double bz, double cx, double cz, int depth) {
    const double ab = std::hypot(bx - ax, bz - az);
    const double bc = std::hypot(cx - bx, cz - bz);
    const double ca = std::hypot(ax - cx, az - cz);
    if (depth < 7 && std::max({ab, bc, ca}) > CELL) {
      const double mx = (ax + bx) / 2;
      const double mz = (az + bz) / 2;
      const double nx = (bx + cx) / 2;
      const double nz = (bz + cz) / 2;
      const double ox = (cx + ax) / 2;
      const double oz = (cz + az) / 2;
      tri(ax, az, mx, mz, ox, oz, depth + 1);
      tri(mx, mz, bx, bz, nx, nz, depth + 1);
      tri(ox, oz, nx, nz, cx, cz, depth + 1);
      tri(mx, mz, nx, nz, ox, oz, depth + 1);
      return;
    }
    // (facing up on the map, whichever way the outline ran and the stage is turned)
    const auto [wax, waz] = f.toWorld(ax, az);
    const auto [wbx, wbz] = f.toWorld(bx, bz);
    const auto [wcx, wcz] = f.toWorld(cx, cz);
    const bool up = (wbz - waz) * (wcx - wax) - (wbx - wax) * (wcz - waz) > 0;
    if (up) {
      put(ax, az);
      put(bx, bz);
      put(cx, cz);
    } else {
      put(ax, az);
      put(cx, cz);
      put(bx, bz);
    }
  };
  for (const auto& [a, b, c] : faces) {
    const Vector2& A = all[static_cast<size_t>(a)];
    const Vector2& B = all[static_cast<size_t>(b)];
    const Vector2& C = all[static_cast<size_t>(c)];
    tri(A.x, A.y, B.x, B.y, C.x, C.y, 0);
  }
}

// ----------------------------------------------------------------- the actors

void TheoryView::buildActors(const theory::Scene& sc) {
  int style = 0;
  for (size_t ri = 0; ri < sc.actors.size(); ri++) {
    const theory::ActorRun& r = sc.actors[ri];
    const theory::ActorDef& d = *r.def;
    ActorView v;
    const std::string& kind = d.kind;
    if (kind == "me") {
      if (!pool.me) pool.me = std::make_unique<CarExterior>(0xf3f3ef, CarExteriorOptions{.roofSign = true});
      v.car = pool.me.get();
      v.root = pool.me->group;
      v.height = 1.5;
    } else if (kind == "car") {
      const int k = style++ % static_cast<int>(STYLES().size());
      std::unique_ptr<CarExterior> car;
      if (!pool.cars[k].empty()) {
        car = std::move(pool.cars[k].back());
        pool.cars[k].pop_back();
      } else car = std::make_unique<CarExterior>(d.colour.value_or(0x8a8f96), CarExteriorOptions{.dims = &STYLES()[static_cast<size_t>(k)]});
      car->paint->color = Color(static_cast<uint32_t>(d.colour.value_or(0x8a8f96)));
      v.car = car.get();
      v.root = car->group;
      v.height = 1.5;
      usedCars[k].push_back(std::move(car));
    } else if (kind == "van") {
      std::unique_ptr<Van> van;
      if (!pool.vans.empty()) {
        van = std::move(pool.vans.back());
        pool.vans.pop_back();
      } else {
        // An ambulance's or a police van's body: a tall estate with a band round it and two blue lamps on the roof.
        van = std::make_unique<Van>();
        van->car = std::make_unique<CarExterior>(0xf2f2ee, CarExteriorOptions{.dims = &VAN(), .ownMaterials = true});
        van->band = lambert({.color = Color(0xd2261c)});
        GeometryPtr bandGeo = boxGeometry(VAN().width + 0.02, 0.24, VAN().length * 0.86);
        bandGeo->translate(0, 1.0, 0.1);
        van->car->group->add(make<Mesh>(bandGeo, van->band));
        for (const double side : {-1.0, 1.0}) {
          MaterialPtr mat = basic({.color = Color(0x1b3d9a)});
          auto lamp = make<Mesh>(boxGeometry(0.42, 0.16, 0.3), mat);
          lamp->position.set(side * 0.42, VAN().height + 0.06, -VAN().length / 2 + 1.6);
          van->car->group->add(lamp);
          van->blue.push_back(mat);
          van->lamps.push_back(lamp);
        }
      }
      van->car->paint->color = Color(static_cast<uint32_t>(d.colour.value_or(0xf2f2ee)));
      // (a red band round an ambulance, a light one round the police)
      van->band->color = Color(d.colour == 0x2d5d9a ? 0xe8edf2u : 0xd2261cu);
      for (const auto& l : van->lamps) l->visible = d.blue;
      v.van = van.get();
      v.car = van->car.get();
      v.root = van->car->group;
      v.height = 2.4;
      usedVans.push_back(std::move(van));
    } else if (kind == "bus") {
      if (!pool.bus) pool.bus = std::make_unique<PlayerBus>();
      pool.bus->setLine("", d.display);
      v.bus = pool.bus.get();
      v.root = pool.bus->root;
      v.height = 3.2;
    } else if (kind == "truck") {
      if (!pool.truck) pool.truck = std::make_unique<PlayerTruck>();
      v.truck = pool.truck.get();
      v.root = pool.truck->root;
      v.height = 3.4;
    } else if (kind == "bike") {
      std::unique_ptr<CyclistModel> bike;
      if (!pool.bikes.empty()) {
        bike = std::move(pool.bikes.back());
        pool.bikes.pop_back();
      } else bike = makeCyclistModel(d.colour.value_or(0x2f8a4a));
      bike->setColour(d.colour.value_or(0x2f8a4a));
      v.bike = bike.get();
      v.root = bike->group;
      v.height = 1.85;
      usedBikes.push_back(std::move(bike));
    } else if (kind == "tram") {
      const Consist* consist = consistOf(RAIL_TRAM, "single");
      auto tram = std::make_unique<RailVehicle>();
      tram->id = -900;
      tram->kind = RAIL_TRAM;
      tram->consist = consist;
      tram->cars.assign(carLayout(*consist).size(), RailCar{});
      tram->panto = 1;
      v.tram = tram.get();
      trams.push_back(std::move(tram));
      v.height = 3.6;
    } else if (kind == "person" || kind == "child") {
      v.look = kind == "child" ? childLook(d.look.value_or(1)) : d.look.value_or(1);
      v.height = kind == "child" ? 1.3 : 1.8;
      if (!d.role.empty()) {
        // (in their clothes of work or of age: a look of its own number, worn by the people renderer)
        const int id = 700000 + static_cast<int>(ri) * 1000 + d.look.value_or(1);
        if (d.role == "police" || d.role == "senior") people->wear(roleLook(id, d.look.value_or(1), d.role));
        else {
          Look l = workerLook(d.look.value_or(1), "orange");
          l.id = id;
          people->wear(l);
        }
        v.look = id;
      }
    } else if (kind == "deer" || kind == "boar") {
      AnimalModel* a = extras->animal(kind);
      v.animal = a;
      v.root = a->group;
      v.height = kind == "deer" ? 1.3 : 0.9;
    }
    if (v.root) {
      v.root->traverse([](Object3D& o) {
        if (o.type == ObjectType::Mesh || o.type == ObjectType::InstancedMesh) o.castShadow = true;
      });
      movers->add(v.root);
    }
    actors.push_back(std::move(v));
  }
}

void TheoryView::update(double t, double now) {
  if (!scene || !frame) return;
  const StageFrame& f = *frame;
  const bool blinkOn = static_cast<long long>(std::floor(now * 3)) % 2 == 0;
  poses.clear();
  for (size_t i = 0; i < scene->actors.size(); i++) {
    const theory::ActorRun& r = scene->actors[i];
    ActorView& v = actors[i];
    const theory::Pose& p = theory::poseAt(r, t, v.pose);
    const ObjectPtr& root = v.root;
    const int ii = static_cast<int>(i);
    if (!p.visible) {
      if (root) root->visible = false;
      extras->drop(ii);
      extras->light(ii, *r.def, -1, 0, 0, 0, 0, 0);
      continue;
    }
    const auto [x, z] = f.toWorld(p.x, p.z);
    const double yaw = f.yawToWorld(p.yaw);
    const double lift = theory::actorLift(*scene, r, p.x, p.z);
    if (r.def->kind == "person" || r.def->kind == "child") {
      const double y = height(x, z) + lift;
      const std::optional<std::array<double, 4>> arms = theory::armsAt(*r.def, t);
      PersonPose pp{.x = x, .y = y, .z = z, .yaw = yaw, .walk = p.v > 0.05 ? p.s : -1, .look = v.look.value_or(1)};
      if (arms) {
        pp.arms = *arms;
        pp.head = 0;
      }
      poses.push_back(pp);
      v.at.set(x, y + v.height * 0.6, z);
      extras->hold(ii, *r.def, t, x, y, z, yaw);
      continue;
    }
    if (v.tram) {
      placeTram(r, p, *v.tram);
      const RailCar& mid = v.tram->cars[v.tram->cars.size() / 2];
      v.at.set(mid.x, mid.y + 2, mid.z);
      continue;
    }
    if (!root) continue;
    root->visible = true;
    // the height of the ground under both axles: the body's height and its slope
    const double wb = r.foot.wheelbase;
    const double fx = x - std::sin(yaw) * (wb / 2);
    const double fz = z - std::cos(yaw) * (wb / 2);
    const double bx = x + std::sin(yaw) * (wb / 2);
    const double bz = z + std::cos(yaw) * (wb / 2);
    const double yf = height(fx, fz);
    const double yb = height(bx, bz);
    const double pitch = std::atan2(yf - yb, std::max(1.0, wb));
    const double y = (yf + yb) / 2 + lift;
    root->position.set(x, y, z);
    root->rotation.set(pitch, yaw, 0, Euler::Order::YXZ);
    v.at.set(x, y + v.height * 0.62, z);
    const bool braking = p.v < 0.15 || p.a < -0.6;
    const bool left = blinkOn && (p.blink == 2 || p.blink < 0);
    const bool right = blinkOn && (p.blink == 2 || p.blink == 1);
    if (v.car) {
      const double rad = v.car->dims.wheelRadius;
      const double spin = (p.s - r.foot.front) / rad;
      v.car->setWheels({p.steer, p.steer, 0, 0}, {spin, spin, spin, spin}, {rad, rad, rad, rad});
      v.car->setLamps({.low = true, .high = false, .brake = braking, .reverse = false, .left = left, .right = right});
      if (v.van && r.def->blue) {
        const long long k = static_cast<long long>(std::floor(now * 6)) % 2;
        v.van->blue[0]->color = Color(k == 0 ? 0x4d8bffu : 0x0b1a44u);
        v.van->blue[1]->color = Color(k == 1 ? 0x4d8bffu : 0x0b1a44u);
      }
    } else if (v.bus || v.truck) {
      CarVisualState& s = visual;
      const double rad = v.bus ? 0.5 : 0.49;
      std::fill(s.wheelSpin.begin(), s.wheelSpin.end(), (p.s - r.foot.front) / rad);
      s.wheelSteer[0] = s.wheelSteer[1] = p.steer;
      std::fill(s.wheelY.begin(), s.wheelY.end(), rad);
      s.indicator = p.blink == 2 ? 0 : p.blink;
      s.hazard = p.blink == 2;
      s.blinkOn = blinkOn;
      s.braking = braking;
      s.speedKmh = p.v * 3.6;
      s.steerWheel = p.steer * 12;
      if (v.bus) v.bus->update(s, 0, now);
      else v.truck->update(s, 0, now);
    } else if (v.bike) {
      // (the pedals go round once in about four metres, the wheels as they roll)
      v.bike->pose(p.s * 1.6, p.s / 0.34, false);
    } else if (v.animal) v.animal->pose(p.s, p.v);
    // the light of its headlights on the road, in the dark
    if (!r.def->beam.empty()) extras->light(ii, *r.def, t, x, y, z, yaw, r.foot.front);
  }
  extras->update(t);
  people->update(poses, now);
  // (the trams shown: those of the actors that are there now)
  std::vector<std::unique_ptr<RailVehicle>> shown;
  std::vector<size_t> shownAt;
  for (size_t k = 0; k < trams.size(); k++) {
    const bool there = std::any_of(actors.begin(), actors.end(), [&](const ActorView& v) { return v.tram == trams[k].get() && v.pose.visible; });
    if (!there) continue;
    shownAt.push_back(k);
    shown.push_back(std::move(trams[k]));
  }
  rail->update(shown);
  for (size_t k = 0; k < shownAt.size(); k++) trams[shownAt[k]] = std::move(shown[k]);
  // the lamps of the lights
  for (Light& l : lights) {
    const theory::LightState st = theory::lightAt(*scene, l.id, t);
    const std::string key = std::string(1, st) + (l.ped ? "p" : "") + (st == 'A' || st == 'Y' ? (blinkOn ? "1" : "0") : "");
    if (key == l.shown) continue;
    l.shown = key;
    if (l.ped) {
      l.lamps[0]->color = Color(st == 'R' ? lamp::RED : lamp::OFF);
      l.lamps[1]->color = Color(st == 'G' ? lamp::GREEN : lamp::OFF);
    } else {
      l.lamps[0]->color = Color(st == 'R' || st == 'Y' ? lamp::RED : lamp::OFF);
      l.lamps[1]->color = Color(st == 'A' || st == 'Y' ? lamp::AMBER : lamp::OFF);
      l.lamps[2]->color = Color(st == 'G' ? lamp::GREEN : lamp::OFF);
    }
  }
}

void TheoryView::placeTram(const theory::ActorRun& r, const theory::Pose& p, RailVehicle& v) {
  const StageFrame& f = *frame;
  const std::vector<CarRole>& roles = carLayout(*v.consist);
  theory::PathPoint pp;
  for (size_t i = 0; i < roles.size(); i++) {
    const CarRole& role = roles[i];
    const double s0 = p.s - role.start;
    const double s1 = s0 - role.len;
    theory::pathPoint(*r.path, s0, pp);
    const auto [ax, az] = f.toWorld(pp.x, pp.z);
    theory::pathPoint(*r.path, s1, pp);
    const auto [bx, bz] = f.toWorld(pp.x, pp.z);
    const double ya = height(ax, az);
    const double yb = height(bx, bz);
    RailCar& c = v.cars[i];
    c.x = (ax + bx) / 2;
    c.z = (az + bz) / 2;
    c.y = (ya + yb) / 2 + theory::LIFT("asphalt");
    c.yaw = std::atan2(-(ax - bx), -(az - bz));
    c.pitch = std::atan2(ya - yb, role.len);
  }
}

// ------------------------------------------------------------------ overlays

int TheoryView::actorIndex(const std::string& id) const {
  if (!scene || id.empty()) return -1;
  for (size_t i = 0; i < scene->actors.size(); i++)
    if (scene->actors[i].def->id == id) return static_cast<int>(i);
  return -1;
}

void TheoryView::setLabels(const std::vector<std::pair<std::string, std::string>>& names, const std::string& you) {
  if (!scene) return;
  std::vector<std::pair<std::string, std::string>> want = names;
  const bool hasMe = std::any_of(want.begin(), want.end(), [](const auto& e) { return e.first == "me"; });
  if (!hasMe && !you.empty()) want.emplace_back("me", you);
  const auto wanted = [&want](const std::string& id) -> const std::string* {
    for (const auto& [k, text] : want)
      if (k == id) return &text;
    return nullptr;
  };
  for (auto it = labels.begin(); it != labels.end();) {
    const std::string* w = wanted(it->id);
    if (w && *w == it->sprite->name) {
      ++it;
      continue;
    }
    disposeSprite(it->sprite);
    group->remove(it->sprite);
    it = labels.erase(it);
  }
  for (const auto& [id, text] : want) {
    if (std::any_of(labels.begin(), labels.end(), [&id](const Label& l) { return l.id == id; })) continue;
    auto s = labelSprite(text, id == "me");
    s->name = text;
    labels.push_back(Label{id, s});
    group->add(s);
  }
}

void TheoryView::placeLabels(double now) {
  if (!scene) return;
  for (const Label& l : labels) {
    const int i = actorIndex(l.id);
    Sprite& s = *l.sprite;
    if (i < 0 || !actors[static_cast<size_t>(i)].pose.visible) {
      s.visible = false;
      continue;
    }
    const ActorView& v = actors[static_cast<size_t>(i)];
    s.visible = true;
    s.position.copy(v.at);
    s.position.y += v.height * 0.45 + 0.3 + std::sin(now * 3) * 0.06;
  }
}

void TheoryView::showZone(const std::string& id) {
  const int i = actorIndex(id);
  if (!scene || !frame || i < 0 || !actors[static_cast<size_t>(i)].root || !actors[static_cast<size_t>(i)].pose.visible) {
    zone->visible = false;
    return;
  }
  const ActorView& v = actors[static_cast<size_t>(i)];
  const StageFrame& f = *frame;
  zone->visible = true;
  zone->position.copy(v.root->position);
  zone->position.y += 0.03;
  zone->rotation.set(0, f.yawToWorld(v.pose.yaw), 0);
  // (drawn for a car: a lorry's is as much wider and longer as the lorry is, and begins at its cab – beside the front)
  const theory::Footprint& foot = theory::FOOT(scene->actors[static_cast<size_t>(i)].def->kind);
  zone->scale.set(foot.width / theory::FOOT("car").width, 1, foot.length / theory::FOOT("car").length);
  if (scene->actors[static_cast<size_t>(i)].def->kind == "truck") {
    const double yaw = f.yawToWorld(v.pose.yaw);
    zone->position.x -= std::sin(yaw) * foot.front;
    zone->position.z -= std::cos(yaw) * foot.front;
  }
}

double TheoryView::showGap(const std::string& a, const std::string& b, const std::function<std::string(double m)>& text) {
  const int ia = actorIndex(a);
  const int ib = actorIndex(b);
  if (!scene || !frame || ia < 0 || ib < 0 || !actors[static_cast<size_t>(ia)].pose.visible || !actors[static_cast<size_t>(ib)].pose.visible) {
    gapBar->visible = false;
    if (gapLabel) gapLabel->visible = false;
    return 0;
  }
  const StageFrame& f = *frame;
  // across the way b goes: from b's left side to a's right side (a passes on b's left)
  const theory::Pose& pa = actors[static_cast<size_t>(ia)].pose;
  const theory::Pose& pb = actors[static_cast<size_t>(ib)].pose;
  const double lx = -std::cos(pb.yaw);
  const double lz = std::sin(pb.yaw);
  const theory::Footprint& fa = theory::FOOT(scene->actors[static_cast<size_t>(ia)].def->kind);
  const theory::Footprint& fb = theory::FOOT(scene->actors[static_cast<size_t>(ib)].def->kind);
  const double across = (pa.x - pb.x) * lx + (pa.z - pb.z) * lz;
  const double gap = std::max(0.0, across - fa.width / 2 - fb.width / 2);
  const double sx = pb.x + lx * (fb.width / 2);
  const double sz = pb.z + lz * (fb.width / 2);
  const double ex = sx + lx * gap;
  const double ez = sz + lz * gap;
  const auto [wsx, wsz] = f.toWorld(sx, sz);
  const auto [wex, wez] = f.toWorld(ex, ez);
  const double y = height((wsx + wex) / 2, (wsz + wez) / 2) + 0.75;
  gapBar->visible = gap > 0.05;
  gapBar->position.set((wsx + wex) / 2, y, (wsz + wez) / 2);
  gapBar->scale.set(std::max(0.05, gap), 1, 1);
  gapBar->rotation.set(0, std::atan2(-(wez - wsz), wex - wsx), 0);
  const std::string label = text(gap);
  if (label != gapText) {
    if (gapLabel) {
      disposeSprite(gapLabel);
      group->remove(gapLabel);
    }
    gapLabel = labelSprite(label, true);
    group->add(gapLabel);
    gapText = label;
  }
  if (gapLabel) {
    gapLabel->visible = true;
    gapLabel->position.set((wsx + wex) / 2, y + 0.4, (wsz + wez) / 2);
  }
  return gap;
}

bool TheoryView::where(const std::string& id, Vector3& out) const {
  const int i = actorIndex(id);
  if (i < 0 || !actors[static_cast<size_t>(i)].pose.visible) return false;
  out.copy(actors[static_cast<size_t>(i)].at);
  return true;
}

std::optional<double> TheoryView::heading(const std::string& id) const {
  const int i = actorIndex(id);
  if (!frame || i < 0) return std::nullopt;
  return frame->yawToWorld(actors[static_cast<size_t>(i)].pose.yaw);
}

std::optional<std::string> TheoryView::pick(const std::vector<std::string>& ids, const Camera& camera, double px, double py, double w, double h) {
  if (!scene) return std::nullopt;
  std::optional<std::string> best;
  double bestD = std::max(44.0, std::min(w, h) * 0.09);
  for (const std::string& id : ids) {
    const int i = actorIndex(id);
    if (i < 0 || !actors[static_cast<size_t>(i)].pose.visible) continue;
    const ActorView& v = actors[static_cast<size_t>(i)];
    const Label* label = nullptr;
    for (const Label& l : labels)
      if (l.id == id) label = &l;
    std::vector<std::pair<Vector3, double>> spots = {{v.at, 0}, {Vector3(v.at.x, v.at.y - v.height * 0.4, v.at.z), 0}};
    // (the label stands over its anchor: half its height higher on the screen)
    if (label && label->sprite->visible) spots.emplace_back(label->sprite->position, (LABEL_H * h) / 2);
    for (const auto& [p, up] : spots) {
      Vector3 tmp = p;
      tmp.project(camera);
      if (tmp.z > 1) continue;
      const double sx = ((tmp.x + 1) / 2) * w;
      const double sy = ((1 - tmp.y) / 2) * h - up;
      const double d = std::hypot(sx - px, sy - py);
      if (d < bestD) {
        bestD = d;
        best = id;
      }
    }
  }
  return best;
}

}  // namespace fs

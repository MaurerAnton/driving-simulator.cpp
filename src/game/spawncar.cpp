// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/spawncar.ts @ v1.7.7
#include "game/spawncar.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "platform/plat.h"
#include "render/car.h"
#include "sim/collision.h"

namespace fs {

namespace {

/** a car's height (m), for a tap on it */
const double CAR_HEIGHT = 1.6;

StandingBox boxOf(const Parked& p) {
  StandingBox b;
  b.x = p.x;
  b.y = p.y;
  b.z = p.z;
  b.yaw = p.yaw;
  b.height = p.height;
  b.front = p.outline.front;
  b.rear = p.outline.rear;
  b.hw = p.outline.hw;
  return b;
}

}  // namespace

SpawnCars::SpawnCars(SpawnCarHost host_) : host(std::move(host_)) {
  const dom::El b = button = dom::el("button");
  b.type("button");
  b.className("spawn-car");
  // (styled here: one round button at the left edge, below the middle – the middle of the screen stays free)
  b.style("position", "absolute");
  b.style("left", "12px");
  b.style("top", "58%");
  b.style("width", "56px");
  b.style("height", "56px");
  b.style("borderRadius", "50%");
  b.style("border", "2px solid rgba(255,255,255,0.75)");
  b.style("background", "rgba(16,24,34,0.72)");
  b.style("color", "#fff");
  b.style("display", "none");
  b.style("alignItems", "center");
  b.style("justifyContent", "center");
  b.style("padding", "0");
  b.style("zIndex", "6");
  b.style("pointerEvents", "auto");
  b.style("touchAction", "manipulation");
  b.innerHTML(
      "<svg viewBox=\"0 0 24 24\" width=\"30\" height=\"30\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.8\" stroke-linecap=\"round\" "
      "stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M3 15v-3l2-4h9l3 4h2a2 2 0 0 1 2 2v1h-2\"/><circle cx=\"7\" cy=\"16\" r=\"1.8\"/><circle "
      "cx=\"16\" cy=\"16\" r=\"1.8\"/><path d=\"M9 16h5\"/><path d=\"M19 3v6M16 6h6\"/></svg>");
  b.onclick([this](dom::Event& e) {
    e.stopPropagation();
    pressed();
  });
  host.ui.append(b);
  const dom::El c = host.canvas;
  c.on("pointerdown", [this](dom::Event& e) { down = Down{e.pointerId, e.clientX, e.clientY, plat::nowMs()}; });
  c.on("pointerup", [this](dom::Event& e) {
    const std::optional<Down> d = down;
    down = std::nullopt;
    if (!d || d->id != e.pointerId || std::hypot(e.clientX - d->x, e.clientY - d->y) > 10 || plat::nowMs() - d->t > 450) return;
    tap(e.clientX, e.clientY);
  });
}

SpawnCars::~SpawnCars() = default;

void SpawnCars::frame() {
  const bool on = host.canSpawn();
  if (!on) picking = false;
  if (on != shown) {
    shown = on;
    button.style("display", on ? "flex" : "none");
  }
  if (on) {
    const std::string label = host.t("spawnCar");
    if (button.get("title") != label) {
      button.title(label);
      button.setAttribute("aria-label", label);
    }
    button.style("background", picking ? "rgba(56,182,255,0.85)" : "rgba(16,24,34,0.72)");
  }
}

void SpawnCars::pressed() {
  if (!host.canSpawn()) return;
  if (!picking) {
    picking = true;
    host.tip(host.t("spawnCarPick"));
    return;
  }
  picking = false;
  spawnBeside();
}

bool SpawnCars::free(double x, double z, double yaw, const Outline* o_, bool others) {
  const SpawnCarHost& h = host;
  const Outline& o = o_ ? *o_ : h.carOutline;
  if (!area.has(x, z, std::max(o.front, o.rear) + 2) || std::isnan(h.heightAt(x, z))) return false;
  if (inBuilding(x, z, yaw, o, [this](double px, double pz) { return footprints.inside(px, pz); })) return false;
  const double y = h.ground(x, z);
  const double fx = -std::sin(yaw);
  const double fz = -std::cos(yaw);
  // (circles along its axis, as wide as the car, a hand's breadth to spare)
  const double r = o.hw + 0.15;
  const std::vector<SpawnCarCircle> near = others ? h.others() : std::vector<SpawnCarCircle>{};
  for (double a = -o.rear + o.hw; a <= o.front - o.hw + 0.01; a += std::max(0.5, o.hw)) {
    const double cx = x + fx * a;
    const double cz = z + fz * a;
    if (h.collider->circleBlocked(cx, y, cz, r)) return false;
    for (const SpawnCarCircle& k : near)
      if (std::hypot(cx - k.x, cz - k.z) < r + k.r) return false;
  }
  return true;
}

bool SpawnCars::canPlaceAt(double x, double z) { return area.has(x, z, 6) && !std::isnan(host.heightAt(x, z)); }

bool SpawnCars::spawnBeside() {
  const SpawnCarPlace p = host.player();
  const auto ok = [this](double x, double z, double yaw) { return free(x, z, yaw); };
  std::optional<FreePlace> at;
  if (!host.exact()) {
    const std::optional<SpawnCarPlace> road = host.roadSpot(p.x, p.z, 40);
    if (road) at = freeNear(road->x, road->z, road->yaw, ok, 12);
  }
  if (!at) at = besidePlace(p.x, p.z, p.yaw, ok);
  return put(at);
}

bool SpawnCars::spawnAt(double x, double z, std::optional<double> yaw) {
  const SpawnCarHost& h = host;
  if (!canPlaceAt(x, z)) {
    h.tip(h.t("spawnCarOutside"));
    return false;
  }
  const auto ok = [this](double px, double pz, double py) { return free(px, pz, py); };
  std::optional<FreePlace> at;
  if (!h.exact()) {
    const std::optional<SpawnCarPlace> road = h.roadSpot(x, z, 60);
    if (road) at = freeNear(road->x, road->z, road->yaw, ok, 15);
  }
  if (!at) {
    // (facing along the road nearest, if one is near; else the way the player faces)
    const std::optional<SpawnCarPlace> road = h.roadSpot(x, z, 30);
    at = freeNear(x, z, yaw ? *yaw : road ? road->yaw : h.player().yaw, ok, 20);
  }
  return put(at);
}

bool SpawnCars::put(const std::optional<FreePlace>& at) {
  const SpawnCarHost& h = host;
  if (!at) {
    h.tip(h.t("spawnCarNoRoom"));
    return false;
  }
  if (car) drop(*car, "car");
  CarExterior& ext = exterior();
  const double y = h.ground(at->x, at->z);
  car = std::make_unique<Parked>(Parked{"car", at->x, y, at->z, at->yaw, h.carOutline, CAR_HEIGHT, ext.group, true});
  stand(*car, "car");
  h.tip(h.t("spawnCarReady"));
  return true;
}

CarExterior& SpawnCars::exterior() {
  CarExterior* e = nullptr;
  for (const std::unique_ptr<CarExterior>& x : pool)
    if (!x->group->parent) {
      e = x.get();
      break;
    }
  if (!e) {
    CarExteriorOptions opts;
    opts.plate = "KS FS 2026";
    opts.seeThroughGlass = true;
    opts.roofSign = true;
    opts.noMirrorHousings = true;
    opts.lettering = "maureranton.com";
    opts.ownMaterials = true;
    pool.push_back(std::make_unique<CarExterior>(0xc9cdd2, opts));
    e = pool.back().get();
  }
  e->paint->color.setHex(host.paint());
  return *e;
}

void SpawnCars::stand(Parked& p, const std::string& slot) {
  if (p.own) {
    p.root->position.set(p.x, p.y + 0.02, p.z);
    p.root->rotation.set(0, p.yaw, 0);
    p.root->visible = true;
    host.scene->add(p.root);
    p.root->updateMatrixWorld(true);
  }
  const std::vector<double> pts0 = outlinePoints(p.x, p.z, p.yaw, p.outline);
  const std::vector<double> pts(pts0.begin() + 6, pts0.end());
  // (the corners in order round it: front right, front left, rear left, rear right)
  std::vector<float>& ring = slot == "car" ? ringCar : ringLeft;
  ring = {static_cast<float>(pts[0]), static_cast<float>(pts[1]), static_cast<float>(pts[2]), static_cast<float>(pts[3]),
          static_cast<float>(pts[6]), static_cast<float>(pts[7]), static_cast<float>(pts[4]), static_cast<float>(pts[5])};
  CollisionSource src;
  src.key = "parked:" + slot;
  src.rings = &ring;
  src.ringCounts = &ringCounts;
  host.collider->add(src);
}

void SpawnCars::drop(Parked& p, const std::string& slot) {
  host.collider->remove("parked:" + slot);
  if (p.own) host.scene->remove(p.root);
  if (slot == "car") car = nullptr;
  else left = nullptr;
}

bool SpawnCars::keeps(const Object3D* root) const { return left && !left->own && left->root.get() == root; }

void SpawnCars::swap(const Parked* p, const LeftVehicle* from) {
  if (car && car.get() == p) drop(*car, "car");
  if (left && left.get() == p) drop(*left, "left");
  if (left) drop(*left, "left");
  if (!from) return;
  const ObjectPtr root = from->root ? from->root : ObjectPtr(exterior().group);
  left = std::make_unique<Parked>(Parked{from->kind, from->x, from->y, from->z, from->yaw, from->outline, from->height, root, from->root == nullptr});
  if (left->own) left->y = host.ground(from->x, from->z);
  stand(*left, "left");
}

void SpawnCars::clear() {
  if (car) drop(*car, "car");
  if (left) drop(*left, "left");
  picking = false;
}

Parked* SpawnCars::near(double x, double z, double reach) {
  for (Parked* p : {car.get(), left.get()}) {
    if (!p) continue;
    const double dx = x - p->x;
    const double dz = z - p->z;
    const double along = -dx * std::sin(p->yaw) - dz * std::cos(p->yaw);
    const double side = dx * std::cos(p->yaw) - dz * std::sin(p->yaw);
    if (along <= p->outline.front + reach && along >= -p->outline.rear - reach && std::abs(side) <= p->outline.hw + reach) return p;
  }
  return nullptr;
}

bool SpawnCars::claims(double cx, double cy) {
  if (picking) return true;
  const SpawnCarHost& h = host;
  if ((!car && !left) || !h.canEnter()) return false;
  const dom::Rect r = h.canvas.getBoundingClientRect();
  if (r.width == 0 || r.height == 0) return false;
  ndc.set(((cx - r.left) / r.width) * 2 - 1, -((cy - r.top) / r.height) * 2 + 1);
  ray.setFromCamera(ndc, *h.camera);
  const Vector3& o = ray.ray.origin;
  const Vector3& d = ray.ray.direction;
  for (const Parked* p : {car.get(), left.get()}) {
    if (!p) continue;
    const double t = rayBox(o.x, o.y, o.z, d.x, d.y, d.z, boxOf(*p));
    if (t >= 0 && t < 400) return true;
  }
  return false;
}

void SpawnCars::tap(double cx, double cy) {
  const SpawnCarHost& h = host;
  const bool enter = h.canEnter();
  if (!enter && !picking) return;
  if (!car && !left && !picking) return;
  const dom::Rect r = h.canvas.getBoundingClientRect();
  if (r.width == 0 || r.height == 0) return;
  ndc.set(((cx - r.left) / r.width) * 2 - 1, -((cy - r.top) / r.height) * 2 + 1);
  ray.setFromCamera(ndc, *h.camera);
  const Vector3 o = ray.ray.origin;
  const Vector3 d = ray.ray.direction;
  if (enter) {
    Parked* best = nullptr;
    double bt = JS_INF;
    for (Parked* p : {car.get(), left.get()}) {
      if (!p) continue;
      const double t = rayBox(o.x, o.y, o.z, d.x, d.y, d.z, boxOf(*p));
      if (t >= 0 && t < bt) {
        bt = t;
        best = p;
      }
    }
    if (best && bt < 400) {
      picking = false;
      h.enter(*best);
      return;
    }
  }
  if (picking && h.canSpawn()) {
    const std::optional<GroundPoint> g = rayGround(o.x, o.y, o.z, d.x, d.y, d.z, [&h](double x, double z) { return h.heightAt(x, z); });
    if (!g) return;
    picking = false;
    spawnAt(g->x, g->z);
  }
}

}  // namespace fs

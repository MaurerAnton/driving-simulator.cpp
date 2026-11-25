// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/forkliftyard.ts @ v1.7.7
#include "render/forkliftyard.h"

#include <cmath>
#include <cstdio>

#include "engine/geometries.h"
#include "game/ce.h"
#include "game/forklifttasks.h"
#include "platform/canvas.h"
#include "platform/intl.h"
#include "render/loading.h"
#include "sim/forklift.h"

namespace fs {

namespace {

/** (the original's module constants: made when first asked for, kept to the end) */
struct YardMaterials {
  const MaterialPtr upright = lambert({.color = 0x1f4fa0});
  const MaterialPtr beam = lambert({.color = 0xe0661a});
  const MaterialPtr yellow = basic({.color = 0xf2c200});
  const MaterialPtr white = basic({.color = 0xe9ece8});
  const MaterialPtr green = basic({.color = 0x2fd36a});
  const MaterialPtr greenFill = basic({.color = 0x2fd36a, .transparent = true, .opacity = 0.25, .depthWrite = false});
  const MaterialPtr cab = lambert({.color = 0xdfe3e6});
  const MaterialPtr cabStripe = lambert({.color = 0x1f5fa8});
  const MaterialPtr glass = lambert({.color = 0x1d2a35});
  const MaterialPtr chassis = lambert({.color = 0x1d1f22});
  const MaterialPtr floor = lambert({.color = 0x6e5236});
  const MaterialPtr tyre = lambert({.color = 0x1b1c1e});
  const MaterialPtr rim = lambert({.color = 0xa9adb2});
  const MaterialPtr arrow = basic({.color = 0x35e07a});
  const MaterialPtr floorPaint = lambert({.color = 0x7d8186});
};

const YardMaterials& M() {
  static const YardMaterials* m = new YardMaterials();
  return *m;
}

std::shared_ptr<Mesh> box(Object3D& g, const MaterialPtr& m, double u0, double y0, double w0, double u1, double y1, double w1, bool shadow = true) {
  auto mesh = make<Mesh>(boxGeometry(std::abs(u1 - u0), std::abs(y1 - y0), std::abs(w1 - w0)), m);
  mesh->position.set((u0 + u1) / 2, (y0 + y1) / 2, (w0 + w1) / 2);
  mesh->castShadow = shadow;
  mesh->receiveShadow = shadow;
  g.add(mesh);
  return mesh;
}

/** A line painted on the floor from (u0, w0) to (u1, w1). */
void line(Object3D& g, const MaterialPtr& m, double u0, double w0, double u1, double w1, double width = 0.1) {
  const double len = std::hypot(u1 - u0, w1 - w0);
  auto mesh = make<Mesh>(planeGeometry(len, width), m);
  mesh->rotation.x = -PI / 2;
  mesh->rotation.z = -std::atan2(w1 - w0, u1 - u0);
  mesh->position.set((u0 + u1) / 2, 0.012, (w0 + w1) / 2);
  g.add(mesh);
}

void frameLines(Object3D& g, const MaterialPtr& m, double u0, double w0, double u1, double w1, double width = 0.1) {
  line(g, m, u0, w0, u1, w0, width);
  line(g, m, u1, w0, u1, w1, width);
  line(g, m, u1, w1, u0, w1, width);
  line(g, m, u0, w1, u0, w0, width);
}

/** The weight written on the crate's side. */
std::shared_ptr<Mesh> weightSign(const std::string& text) {
  const Canvas c(256, 128);
  const Ctx2D x = c.getContext2D();
  x.fillStyle("#f2c200");
  x.fillRect(0, 0, 256, 128);
  x.fillStyle("#111");
  x.font("700 64px \"Roboto Condensed\", Arial, sans-serif");
  x.textAlign("center");
  x.textBaseline("middle");
  x.fillText(text, 128, 66);
  TexturePtr tex = canvasTexture(c);
  tex->srgb = true;
  return make<Mesh>(planeGeometry(0.6, 0.3), basic({.map = tex}));
}

}  // namespace

ForkYardScene::ForkYardScene() {
  group->name = "forklift place";
  group->add(fixed, palletGroup);
  group->visible = false;
}

void ForkYardScene::show(const YardFrame& frame, double y0, bool low) {
  char y0s[32];
  std::snprintf(y0s, sizeof y0s, "%.2f", y0);
  const std::string key = jsStr(frame.yard.x) + "|" + jsStr(frame.yard.z) + "|" + std::to_string(frame.yard.rot) + "|" + y0s + "|" + (low ? "true" : "false");
  group->visible = true;
  if (key == builtFor) return;
  builtFor = key;
  clearFixed();
  const auto [ox, oz] = frame.toWorld(0, 0);
  fixed->position.set(ox, y0, oz);
  fixed->rotation.y = frame.yawToWorld(0);
  build(low);
}

void ForkYardScene::hide() { group->visible = false; }

void ForkYardScene::clearFixed() {
  // (the geometries are given back with their last reference)
  fixed->clear();
  // (the arrows hang in the group, not in the fixed parts: forgotten here, as in the original, and made anew)
  arrows.clear();
  zoneMark->clear();
  bedMark->clear();
  slotMark->clear();
}

void ForkYardScene::build(bool low) {
  const YardMaterials& K = M();
  Group& g = *fixed;
  // ---- the floor: the area's edge, the aisle in front of the rack, the places the pallets wait in
  const ForkArea& A = FORK_AREA;
  frameLines(g, K.yellow, A.u0, A.w0, A.u1, A.w1, 0.12);
  line(g, K.yellow, RACK.u0 - 1, RACK.wFront + 0.6, RACK.u0 + RACK.bay * RACK.bays + 1, RACK.wFront + 0.6, 0.1);
  frameLines(g, K.white, -20.3, -4.8, -16.1, -3.2, 0.06);
  frameLines(g, K.white, -28.6, -3.8, -27.4, -2.2, 0.06);
  // ---- the rack
  const RackPlan& R = RACK;
  for (int k = 0; k <= R.bays; k++) {
    const double u = R.u0 + k * R.bay;
    for (double w : {R.wFront, R.wBack}) box(g, K.upright, u - 0.05, 0, w - 0.05, u + 0.05, R.height, w + 0.05);
    // the bracing between the front and the back upright, and its foot plates
    if (!low) {
      for (int i = 0; i < 5; i++) {
        const double y0 = 0.2 + i * 0.72;
        const double y1 = y0 + 0.72;
        auto d = make<Mesh>(cylinderGeometry(0.018, 0.018, std::hypot(y1 - y0, R.wFront - R.wBack), 6), K.upright);
        d->position.set(u, (y0 + y1) / 2, (R.wFront + R.wBack) / 2);
        d->rotation.x = (i % 2 == 0 ? 1 : -1) * std::atan2(R.wFront - R.wBack, y1 - y0);
        g.add(d);
      }
      for (double w : {R.wFront, R.wBack}) box(g, K.upright, u - 0.08, 0, w - 0.08, u + 0.08, 0.02, w + 0.08, false);
    }
    box(g, K.upright, u - 0.03, 0.15, R.wBack, u + 0.03, 0.2, R.wFront, false);
    box(g, K.upright, u - 0.03, R.height - 0.2, R.wBack, u + 0.03, R.height - 0.15, R.wFront, false);
  }
  for (int b = 0; b < R.bays; b++) {
    const double ua = R.u0 + b * R.bay + 0.05;
    const double ub = R.u0 + (b + 1) * R.bay - 0.05;
    for (double lv : R.levels) {
      box(g, K.beam, ua, lv - R.beam, R.wFront - 0.08, ub, lv, R.wFront);
      box(g, K.beam, ua, lv - R.beam, R.wBack, ub, lv, R.wBack + 0.08);
      if (!low)
        for (double t : {0.25, 0.5, 0.75}) box(g, K.upright, ua + (ub - ua) * t - 0.02, lv - 0.04, R.wBack, ua + (ub - ua) * t + 0.02, lv, R.wFront, false);
    }
  }
  // ---- the truck: its cab, chassis, wheels and its floor, the headboard; the side towards the rack open
  const TruckPlan& T = FORK_TRUCK;
  box(g, K.chassis, T.chassis.u0, T.chassis.y0, T.chassis.w0, T.chassis.u1 + 2.4, T.chassis.y1, T.chassis.w1);
  box(g, K.floor, T.bed.u0, T.bed.y - 0.12, T.bed.w0, T.bed.u1, T.bed.y, T.bed.w1);
  box(g, K.chassis, T.bed.u0, T.bed.y - 0.22, T.bed.w0, T.bed.u1, T.bed.y - 0.12, T.bed.w0 + 0.06);
  box(g, K.chassis, T.bed.u0, T.bed.y - 0.22, T.bed.w1 - 0.06, T.bed.u1, T.bed.y - 0.12, T.bed.w1);
  box(g, K.cabStripe, T.head.u0, T.head.y0, T.head.w0, T.head.u1, T.head.y1, T.head.w1);
  // (the curtain's frame: the posts along the far side, the roof rail – the near side is open)
  if (!low) {
    for (double u = T.bed.u0 + 0.05; u <= T.bed.u1; u += 1.85) box(g, K.chassis, u - 0.04, T.bed.y, T.bed.w1 - 0.08, u + 0.04, T.bed.y + 2.5, T.bed.w1);
    box(g, K.chassis, T.bed.u0, T.bed.y + 2.5, T.bed.w1 - 0.1, T.bed.u1, T.bed.y + 2.58, T.bed.w1);
    box(g, K.chassis, T.bed.u0, T.bed.y, T.bed.w0, T.bed.u0 + 0.08, T.bed.y + 0.5, T.bed.w1);
  }
  box(g, K.cab, T.cab.u0, T.cab.y0 + 0.5, T.cab.w0, T.cab.u1, T.cab.y1, T.cab.w1);
  box(g, K.cabStripe, T.cab.u0, T.cab.y0 + 0.5, T.cab.w0 - 0.01, T.cab.u1, T.cab.y0 + 0.75, T.cab.w1 + 0.01);
  box(g, K.glass, T.cab.u1 - 0.02, 1.85, T.cab.w0 + 0.15, T.cab.u1 + 0.01, 2.7, T.cab.w1 - 0.15, false);
  for (double w : {T.cab.w0 - 0.01, T.cab.w1 + 0.01}) box(g, K.glass, T.cab.u1 - 1.0, 1.9, w - 0.005, T.cab.u1 - 0.2, 2.65, w + 0.005, false);
  box(g, K.chassis, T.cab.u1 - 0.1, 0.45, T.cab.w0, T.cab.u1 + 0.12, 0.85, T.cab.w1);
  for (double u : T.wheels) {
    for (double w : {T.bed.w0 + 0.25, T.bed.w1 - 0.25}) {
      auto t = make<Mesh>(cylinderGeometry(T.wheelR, T.wheelR, 0.4, low ? 12 : 20), K.tyre);
      t->rotation.x = PI / 2;
      t->position.set(u, T.wheelR, w);
      t->castShadow = true;
      g.add(t);
      auto r = make<Mesh>(cylinderGeometry(T.wheelR * 0.55, T.wheelR * 0.55, 0.42, 12), K.rim);
      r->rotation.x = PI / 2;
      r->position.copy(t->position);
      g.add(r);
    }
  }
  // ---- the marks shown when they are wanted: the green place, the truck's floor, the slot up in the rack
  const GuideZone& Z = GUIDE_ZONE;
  frameLines(*zoneMark, K.green, Z.u - Z.hu, Z.w - Z.hw, Z.u + Z.hu, Z.w + Z.hw, 0.12);
  auto fill = make<Mesh>(planeGeometry(Z.hu * 2, Z.hw * 2), K.greenFill);
  fill->rotation.x = -PI / 2;
  fill->position.set(Z.u, 0.014, Z.w);
  zoneMark->add(fill);
  auto bedFill = make<Mesh>(planeGeometry(T.bed.u1 - T.bed.u0, T.bed.w1 - T.bed.w0), K.greenFill);
  bedFill->rotation.x = -PI / 2;
  bedFill->position.set((T.bed.u0 + T.bed.u1) / 2, T.bed.y + 0.01, (T.bed.w0 + T.bed.w1) / 2);
  bedMark->add(bedFill);
  const SlotPlace ts = slotOf(1, 0);
  const double top = R.levels[1];
  box(*slotMark, K.green, ts.u - 0.45, top + 0.005, R.wFront - 0.1, ts.u + 0.45, top + 0.04, R.wFront - 0.06, false);
  box(*slotMark, K.green, ts.u - 0.45, top + 0.005, R.wBack + 0.06, ts.u + 0.45, top + 0.04, R.wBack + 0.1, false);
  auto slotFill = make<Mesh>(planeGeometry(0.9, R.wFront - R.wBack), K.greenFill);
  slotFill->rotation.x = -PI / 2;
  slotFill->position.set(ts.u, top + 0.02, (R.wFront + R.wBack) / 2);
  slotMark->add(slotFill);
  g.add(zoneMark, bedMark, slotMark);
}

Group& ForkYardScene::palletOf(const Pallet& p) {
  auto it = pallets_.find(p.id);
  if (it == pallets_.end()) {
    std::shared_ptr<Group> m = palletModel({.height = p.def.goods, .kind = p.def.kind, .mass = p.def.mass});
    if (p.def.mass > 2500) {
      // the crate: its weight on both sides
      for (double s : {-1.0, 1.0}) {
        auto sign = weightSign(plat::formatNumber(p.def.mass / 1000, "de-DE", {.minimumFractionDigits = 1}) + " t");
        sign->position.set(s * (p.def.wid / 2 - 0.01), p.def.base + p.def.goods * 0.6, 0);
        sign->rotation.y = (s * PI) / 2;
        m->add(sign);
      }
    }
    it = pallets_.emplace(p.id, m).first;
    palletGroup->add(m);
  }
  return *it->second;
}

void ForkYardScene::update(const std::deque<Pallet>& pallets, const std::optional<ForkLean>& fork, const ForkYardTargets& targets, double now) {
  for (const Pallet& p : pallets) {
    Group& m = palletOf(p);
    m.position.set(p.x, p.y, p.z);
    if (p.fork && fork) {
      q.setFromEuler(e.set(0, fork->yaw, 0));
      q2.setFromEuler(e.set(fork->tilt, 0, 0));
      q.multiply(q2);
      q2.setFromEuler(e.set(0, p.yaw - fork->yaw, 0));
      m.quaternion.copy(q.multiply(q2));
    } else
      m.quaternion.setFromEuler(e.set(0, p.yaw, 0));
  }
  // the arrows: one over each pallet to take, bobbing
  while (arrows.size() < targets.pallets.size()) {
    auto a = make<Mesh>(coneGeometry(0.22, 0.45, 12), M().arrow);
    a->rotation.x = PI;
    arrows.push_back(a);
    group->add(a);
  }
  for (size_t i = 0; i < arrows.size(); i++) {
    Mesh& a = *arrows[i];
    const Pallet* p = nullptr;
    if (i < targets.pallets.size())
      for (const Pallet& x : pallets)
        if (x.id == targets.pallets[i]) {
          p = &x;
          break;
        }
    a.visible = p != nullptr;
    if (p) a.position.set(p->x, p->y + p->def.base + p->def.goods + 0.55 + std::sin(now * 3 + double(i)) * 0.1, p->z);
  }
  zoneMark->visible = targets.zone && !targets.slot;
  bedMark->visible = targets.bed;
  slotMark->visible = targets.slot;
}

}  // namespace fs

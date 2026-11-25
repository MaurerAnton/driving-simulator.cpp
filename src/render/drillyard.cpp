// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/drillyard.ts @ v1.7.7
#include "render/drillyard.h"

#include <array>
#include <cmath>

#include "engine/geometries.h"
#include "game/ce.h"
#include "game/drills.h"

namespace fs {

namespace {

/** (the original's module constants: made when first asked for, kept to the end) */
struct DrillMaterials {
  const std::array<MaterialPtr, 5> BODIES = {lambert({.color = 0x8a1c1c}), lambert({.color = 0x2c4f7c}), lambert({.color = 0xb9bcbf}), lambert({.color = 0x2f3a2f}), lambert({.color = 0xd9d4c4})};
  const MaterialPtr GLASS = lambert({.color = 0x1f2a33});
  const MaterialPtr TYRE = lambert({.color = 0x151515});
  const MaterialPtr KERB = lambert({.color = 0xb4b2ac});
  const MaterialPtr BANK = lambert({.color = 0x8d9093});
  const GeometryPtr bodyGeo = boxGeometry(1.8, 0.72, 4.3);
  const GeometryPtr cabinGeo = boxGeometry(1.6, 0.55, 2.2);
  const GeometryPtr wheelGeo = [] {
    GeometryPtr g = cylinderGeometry(0.32, 0.32, 0.2, 12);
    g->rotateZ(PI / 2);
    return g;
  }();
  const GeometryPtr kerbGeo = boxGeometry(1, 0.14, 0.2);
};

const DrillMaterials& M() {
  static const DrillMaterials* m = new DrillMaterials();
  return *m;
}

}  // namespace

DrillScene::DrillScene() {
  group->name = "basic tasks";
  group->visible = false;
}

bool DrillScene::show(const YardFrame& frame, const DrillLayout& layout, const std::function<double(double, double)>& heightAt) {
  hide();
  const DrillMaterials& K = M();
  Group& g = *group;
  bool complete = true;
  const auto y = [&](double u, double w) {
    const auto [x, z] = frame.toWorld(u, w);
    const double h = heightAt(x, z);
    if (std::isnan(h)) {
      complete = false;
      return 0.0;
    }
    return h;
  };

  // the parked cars: a body, a cabin, four wheels
  for (size_t i = 0; i < layout.parked.size(); i++) {
    const Box& b = layout.parked[i];
    auto car = make<Group>();
    const auto [x, z] = frame.toWorld(b.x, b.z);
    car->position.set(x, y(b.x, b.z) + 0.05, z);
    car->rotation.y = frame.yawToWorld(b.yaw);
    auto body = make<Mesh>(K.bodyGeo, K.BODIES[i % K.BODIES.size()]);
    body->position.y = 0.3 + 0.36;
    body->castShadow = true;
    auto cabin = make<Mesh>(K.cabinGeo, K.GLASS);
    cabin->position.set(0, 0.3 + 0.72 + 0.27, 0.25);
    car->add(body, cabin);
    for (const auto& [wx, wz] : {std::pair{-0.78, -1.35}, std::pair{0.78, -1.35}, std::pair{-0.78, 1.3}, std::pair{0.78, 1.3}}) {
      auto wheel = make<Mesh>(K.wheelGeo, K.TYRE);
      wheel->position.set(wx, 0.32, wz);
      car->add(wheel);
    }
    g.add(car);
  }

  // kerb stones, in pieces of at most 4 m following the ground
  for (const auto& [u0, w0, u1, w1] : layout.kerbs) {
    const double len = std::hypot(u1 - u0, w1 - w0);
    const int n = std::max(1, int(std::ceil(len / 4)));
    const auto [ax, az] = frame.toWorld(u0, w0);
    const auto [bx, bz] = frame.toWorld(u1, w1);
    const double turn = std::atan2(-(bz - az), bx - ax);
    for (int i = 0; i < n; i++) {
      const double u = u0 + ((u1 - u0) * (i + 0.5)) / n;
      const double w = w0 + ((w1 - w0) * (i + 0.5)) / n;
      const auto [x, z] = frame.toWorld(u, w);
      auto stone = make<Mesh>(K.kerbGeo, K.KERB);
      stone->scale.x = len / n;
      stone->position.set(x, y(u, w) + 0.07, z);
      stone->rotation.y = turn;
      stone->receiveShadow = true;
      g.add(stone);
    }
  }

  // the ramp's sides: from the edge of its deck down to the ground
  if (const std::optional<Hill>& h = layout.hill) {
    for (const auto& [edge, toe] : {std::pair{h->w1, h->w1 + h->side}, std::pair{h->w0, h->w0 - h->side}}) {
      std::vector<double> us;
      for (double u = h->u0; u < h->u3 - 1e-6; u += 2.5) us.push_back(u);
      us.push_back(h->u3);
      std::vector<float> pos(us.size() * 2 * 3);
      for (size_t i = 0; i < us.size(); i++) {
        const double u = us[i];
        for (const auto& [k, w] : {std::pair{0, edge}, std::pair{1, toe}}) {
          const auto [x, z] = frame.toWorld(u, w);
          const size_t o = (i * 2 + size_t(k)) * 3;
          pos[o] = float(x);
          pos[o + 1] = float(y(u, w) + 0.05);
          pos[o + 2] = float(z);
        }
      }
      std::vector<uint32_t> index;
      for (size_t i = 0; i + 1 < us.size(); i++) {
        const uint32_t a = uint32_t(i * 2);
        index.insert(index.end(), {a, a + 1, a + 2, a + 2, a + 1, a + 3});
      }
      auto geo = make<BufferGeometry>();
      geo->setAttribute("position", attr(std::move(pos), 3));
      geo->setIndex(index);
      geo->computeVertexNormals();
      // (whichever way the yard is turned, the slope must face up)
      double up = 0;
      const AttrPtr nrm = geo->getAttribute("normal");
      for (size_t i = 0; i < nrm->count(); i++) up += nrm->getY(i);
      if (up < 0) {
        std::vector<uint32_t> flipped(index.size());
        static const int ORDER[3] = {0, 2, 1};
        for (size_t k = 0; k < index.size(); k++) flipped[k] = index[k - (k % 3) + size_t(ORDER[k % 3])];
        geo->setIndex(flipped);
        geo->computeVertexNormals();
      }
      made.push_back(geo);
      auto mesh = make<Mesh>(geo, K.BANK);
      mesh->receiveShadow = true;
      g.add(mesh);
    }
  }
  g.visible = true;
  return complete;
}

void DrillScene::hide() {
  group->visible = false;
  group->clear();
  // (the geometries are given back with their last reference)
  made.clear();
}

}  // namespace fs

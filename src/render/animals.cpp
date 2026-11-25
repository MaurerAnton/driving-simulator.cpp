// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/animals.ts @ v1.7.7
#include "render/animals.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#include "engine/geometries.h"

namespace fs {

namespace {
constexpr double PI_ = 3.14159265358979323846;

GeometryPtr tint(const GeometryPtr& geo, uint32_t hex) {
  const Color c(hex);
  const size_t n = geo->getAttribute("position")->count();
  std::vector<float> col(n * 3);
  for (size_t i = 0; i < n; i++) {
    col[i * 3] = static_cast<float>(c.r);
    col[i * 3 + 1] = static_cast<float>(c.g);
    col[i * 3 + 2] = static_cast<float>(c.b);
  }
  geo->setAttribute("color", attr(std::move(col), 3));
  if (geo->getAttribute("uv")) geo->deleteAttribute("uv");
  return geo->index ? geo->toNonIndexed() : geo;
}

/** A box w × h × l with its middle at (x, y, z), tipped by rx (nose up: positive). */
GeometryPtr box(double w, double h, double l, double x, double y, double z, uint32_t hex, double rx = 0) {
  GeometryPtr g = boxGeometry(w, h, l);
  if (rx != 0) g->rotateX(rx);
  g->translate(x, y, z);
  return tint(g, hex);
}
}  // namespace

/** The measures of a kind: the body, where the legs hang from and how long they are, the length of a stride. */
struct AnimalBuild {
  GeometryPtr body;
  GeometryPtr leg;
  /** the hips and shoulders (x, z) and their height */
  std::vector<std::array<double, 2>> hips;
  double hipY = 0;
  /** metres per stride; how far the legs swing (rad) at a run */
  double stride = 0;
  double swing = 0;
  /** a gallop (front and hind legs in pairs) or a trot (diagonal pairs) */
  bool gallop = false;
};

namespace {
AnimalBuild deer() {
  const uint32_t coat = 0x8b5a32;
  const uint32_t belly = 0xb08660;
  const uint32_t dark = 0x2b211b;
  const std::vector<GeometryPtr> parts = {
      box(0.3, 0.34, 0.86, 0, 0.68, 0.02, coat),
      box(0.26, 0.1, 0.7, 0, 0.53, 0.02, belly),
      // the neck, slanting up and forwards, and the head
      box(0.13, 0.42, 0.16, 0, 0.92, -0.42, coat, -0.45),
      box(0.15, 0.16, 0.3, 0, 1.12, -0.6, coat, 0.15),
      box(0.09, 0.09, 0.08, 0, 1.08, -0.77, dark),
      // the ears
      box(0.04, 0.15, 0.07, 0.08, 1.24, -0.52, coat, 0.2),
      box(0.04, 0.15, 0.07, -0.08, 1.24, -0.52, coat, 0.2),
      // the white patch on the rump, the tiny tail
      box(0.24, 0.24, 0.06, 0, 0.7, 0.46, 0xeae6dc),
      box(0.05, 0.06, 0.04, 0, 0.8, 0.5, coat),
  };
  const GeometryPtr leg = mergeGeometries({box(0.07, 0.3, 0.08, 0, -0.15, 0, coat), box(0.045, 0.26, 0.05, 0, -0.42, 0.01, 0x6e4a2c), box(0.05, 0.04, 0.07, 0, -0.55, -0.01, dark)});
  return AnimalBuild{.body = mergeGeometries(parts), .leg = leg, .hips = {{0.1, -0.33}, {-0.1, -0.33}, {0.1, 0.36}, {-0.1, 0.36}}, .hipY = 0.57, .stride = 2.2, .swing = 0.75, .gallop = true};
}

AnimalBuild boar(bool young) {
  const uint32_t coat = young ? 0x7a5a3a : 0x3a302a;
  const uint32_t dark = young ? 0x4a3524 : 0x241e1a;
  const double s = young ? 0.5 : 1;
  const std::vector<GeometryPtr> parts = {
      // the body, higher at the shoulders, the bristly ridge along the back
      box(0.46 * s, 0.5 * s, 0.95 * s, 0, 0.6 * s, 0.02 * s, coat, 0.06),
      box(0.12 * s, 0.1 * s, 0.7 * s, 0, 0.88 * s, -0.04 * s, dark, 0.06),
      // the head, long and pointing down, the snout
      box(0.3 * s, 0.3 * s, 0.38 * s, 0, 0.56 * s, -0.6 * s, coat, -0.35),
      box(0.16 * s, 0.14 * s, 0.14 * s, 0, 0.43 * s, -0.8 * s, dark, -0.35),
      box(0.05 * s, 0.1 * s, 0.06 * s, 0.11 * s, 0.76 * s, -0.5 * s, dark),
      box(0.05 * s, 0.1 * s, 0.06 * s, -0.11 * s, 0.76 * s, -0.5 * s, dark),
      box(0.03 * s, 0.18 * s, 0.03 * s, 0, 0.58 * s, 0.52 * s, dark, 0.4),
  };
  const GeometryPtr leg = mergeGeometries({box(0.1 * s, 0.22 * s, 0.11 * s, 0, -0.11 * s, 0, coat), box(0.07 * s, 0.16 * s, 0.07 * s, 0, -0.3 * s, 0, dark)});
  return AnimalBuild{.body = mergeGeometries(parts),
                     .leg = leg,
                     .hips = {{0.14 * s, -0.3 * s}, {-0.14 * s, -0.3 * s}, {0.14 * s, 0.33 * s}, {-0.14 * s, 0.33 * s}},
                     .hipY = 0.38 * s,
                     .stride = 1.1 * s,
                     .swing = 0.6,
                     .gallop = false};
}

const AnimalBuild& buildOf(const AnimalKind& kind) {
  static auto* builds = new std::map<AnimalKind, AnimalBuild>();
  auto it = builds->find(kind);
  if (it == builds->end()) {
    AnimalBuild b = kind == "deer" ? deer() : boar(kind == "piglet");
    b.body->computeVertexNormals();
    b.leg->computeVertexNormals();
    it = builds->emplace(kind, std::move(b)).first;
  }
  return it->second;
}

const MaterialPtr& SKIN() {
  static auto* m = new MaterialPtr(lambert({.vertexColors = true}));
  return *m;
}
}  // namespace

AnimalModel::AnimalModel(const AnimalKind& k) : kind(k), b(buildOf(k)) {
  body = make<Mesh>(b.body, SKIN());
  body->castShadow = true;
  group->add(body);
  for (const auto& [x, z] : b.hips) {
    auto pivot = make<Group>();
    pivot->position.set(x, b.hipY, z);
    auto m = make<Mesh>(b.leg, SKIN());
    m->castShadow = true;
    pivot->add(m);
    group->add(pivot);
    legs.push_back(pivot);
  }
  group->name = kind;
}

void AnimalModel::pose(double dist, double speed) {
  const double run = std::min(1.0, std::max(0.0, speed / 3));
  const double phase = (dist / b.stride) * PI_ * 2;
  const double amp = b.swing * run;
  // gallop: the front pair, then the hind pair (a little apart); trot: the diagonals together
  const std::array<double, 4> off = b.gallop ? std::array<double, 4>{0, 0.35, PI_, PI_ + 0.35} : std::array<double, 4>{0, PI_, PI_, 0};
  for (size_t i = 0; i < 4; i++) legs[i]->rotation.x = std::sin(phase + off[i]) * amp;
  // the body rises and falls with the bounds, and pitches a little
  body->position.y = (b.gallop ? 0.06 : 0.02) * run * std::abs(std::sin(phase));
  body->rotation.x = (b.gallop ? 0.08 : 0.02) * run * std::sin(phase + 0.6);
}

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Instanced scenery per tile: trees and street lamps.
 *
 * Origin: src/render/props.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).  The tree/lamp shapes are built from the same primitives, and the
 * per-tile thinning shuffle reproduces the original hash01 walk.
 */
#include "render/props.h"

#include <algorithm>
#include <cmath>

#include "core/geom.h"

namespace fs {

namespace {
/** Vertex colours a geometry, fading towards the bottom (the tree canopies). */
GeometryPtr colored(GeometryPtr geo, uint32_t color, double darkenBottom = 0) {
  auto g = geo;
  if (!g->position) return g;
  size_t count = g->position->count();
  Color c;
  c.setHex(color);
  double minY = 1e300, maxY = -1e300;
  for (size_t i = 0; i < count; i++) {
    double y = g->position->getY(i);
    minY = std::min(minY, y);
    maxY = std::max(maxY, y);
  }
  std::vector<double> arr(count * 3);
  for (size_t i = 0; i < count; i++) {
    double y = g->position->getY(i);
    double t = maxY > minY ? (y - minY) / (maxY - minY) : 1;
    double f = 1 - darkenBottom * (1 - t);
    arr[i * 3] = c.r * f;
    arr[i * 3 + 1] = c.g * f;
    arr[i * 3 + 2] = c.b * f;
  }
  g->color = std::make_shared<BufferAttribute>(arr, 3);
  g->uv.reset();
  return g;
}

GeometryPtr merged(const std::vector<GeometryPtr>& parts) {
  auto g = mergeGeometries(parts, false);
  g->computeVertexNormals();
  return g;
}

/** Unit-height tree shapes (1 = full tree height), detailed and reduced. */
void treeGeometries(GeometryPtr hi[4], GeometryPtr lo[4]) {
  auto trunk = [](double h, double r) {
    auto g = makeCylinderGeometry(r * 0.7, r, h, 5, 1, true);
    g->translate(0, h / 2, 0);
    return colored(g, 0x4a3b2c);
  };
  auto blob = [](double y, double rx, double ry, int detail, uint32_t color, double darken) {
    auto g = makeIcosahedronGeometry(1, detail);
    g->scale(rx, ry, rx);
    g->translate(0, y, 0);
    return colored(g, color, darken);
  };
  auto cone = [](double y0, double y1, double r, uint32_t color, int sides, double darken) {
    auto g = makeConeGeometry(r, y1 - y0, sides, 1, true);
    g->translate(0, (y0 + y1) / 2, 0);
    return colored(g, color, darken);
  };

  hi[0] = merged({trunk(0.42, 0.035), blob(0.66, 0.3, 0.34, 1, 0x4c7a36, 0.35)});
  hi[1] = merged({trunk(0.2, 0.03), cone(0.14, 0.62, 0.23, 0x2f5a34, 7, 0.3),
                  cone(0.42, 1.0, 0.16, 0x356338, 7, 0.3)});
  hi[2] = merged({blob(0.5, 0.62, 0.5, 1, 0x4f7a3a, 0.35)});
  hi[3] = merged({trunk(0.5, 0.04), blob(0.72, 0.36, 0.3, 1, 0x5d8c3d, 0.35)});

  lo[0] = merged({blob(0.6, 0.3, 0.4, 0, 0x4c7a36, 0.35)});
  lo[1] = merged({cone(0.1, 1.0, 0.21, 0x31603a, 5, 0.3)});
  lo[2] = merged({blob(0.5, 0.62, 0.5, 0, 0x4f7a3a, 0.35)});
  lo[3] = merged({blob(0.68, 0.36, 0.33, 0, 0x5d8c3d, 0.35)});
}

GeometryPtr lampGeometry() {
  auto pole = makeCylinderGeometry(0.05, 0.075, 7.2, 6, 1, true);
  pole->translate(0, 3.6, 0);
  auto poleC = colored(pole, 0x70757a);
  // arm reaches along -Z (the instance yaw turns it towards the road)
  auto arm = makeCylinderGeometry(0.035, 0.035, 1.3, 5, 1, true);
  arm->rotateX(PI / 2);
  arm->translate(0, 7.2, -0.6);
  auto armC = colored(arm, 0x70757a);
  auto head = makeBoxGeometry(0.22, 0.09, 0.55);
  head->translate(0, 7.18, -1.3);
  auto headC = colored(head, 0xd8d8d0);
  return merged({poleC, armC, headC});
}
}  // namespace

PropRenderer::PropRenderer() {
  treeGeometries(trees_.hi, trees_.lo);
  lampGeo_ = lampGeometry();
}

void PropRenderer::add(LoadedTile& tile) {
  TileProps props;
  props.group = new Group();
  props.cx = tile.header.cx;
  props.cz = tile.header.cz;

  const std::vector<double>* trees = tile.data.get("trees");
  if (trees && trees->size() >= 5) {
    size_t n = trees->size() / 5;
    // shuffle once so that drawing only the first k instances thins the stand evenly
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; i++) order[i] = i;
    for (size_t i = n - 1; i > 0; i--) {
      size_t j = static_cast<size_t>(hash01(static_cast<int>(i * 7919 + tile.header.ix * 31 +
                                                             tile.header.iz)) *
                                    (i + 1));
      std::swap(order[i], order[j]);
    }
    int counts[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < n; i++)
      counts[static_cast<int>((*trees)[i * 5 + 4])]++;

    for (int kind = 0; kind < 4; kind++) {
      if (!counts[kind]) continue;
      auto mesh = std::make_unique<Mesh>(trees_.hi[kind], kindTree_);
      mesh->name = "trees" + std::to_string(kind);
      for (size_t oi = 0; oi < n; oi++) {
        size_t i = order[oi];
        if (static_cast<int>((*trees)[i * 5 + 4]) != kind) continue;
        double h = (*trees)[i * 5 + 3];
        double r = hash01(static_cast<int>(i * 13 + kind));
        Quaternion q;
        q.setFromAxisAngle(Vector3(0, 1, 0), r * 6.283);
        double wide = h * (0.85 + 0.4 * hash01(static_cast<int>(i * 29 + 5)));
        Matrix4 m4;
        m4.compose(Vector3((*trees)[i * 5], (*trees)[i * 5 + 1] - 0.15, (*trees)[i * 5 + 2]), q,
                   Vector3(wide, h, wide));
        mesh->instances.push_back(m4);
        double v = 0.82 + 0.36 * hash01(static_cast<int>(i * 3 + 11));
        Color col;
        col.set(v * (0.94 + 0.12 * r), v, v * (0.9 + 0.1 * r));
        mesh->instanceColors.push_back(col);
      }
      props.trees.push_back(mesh.get());
      props.treeKind.push_back(kind);
      props.treeTotal.push_back(counts[kind]);
      props.group->add(std::unique_ptr<Object3D>(mesh.release()));
    }
  }

  // street lamps come from the tile header's object list
  std::vector<const TileObject*> lamps;
  for (const auto& o : tile.header.objects)
    if (o.k == "lamp") lamps.push_back(&o);
  if (!lamps.empty()) {
    auto mesh = std::make_unique<Mesh>(lampGeo_, kindTree_);
    mesh->name = "lamps";
    for (size_t i = 0; i < lamps.size(); i++) {
      Quaternion q;
      q.setFromAxisAngle(Vector3(0, 1, 0), lamps[i]->hasYaw ? lamps[i]->yaw : 0);
      Matrix4 m4;
      m4.compose(Vector3(lamps[i]->x, lamps[i]->y, lamps[i]->z), q, Vector3(1, 1, 1));
      mesh->instances.push_back(m4);
    }
    props.group->add(std::unique_ptr<Object3D>(mesh.release()));
  }

  tile.group->add(std::unique_ptr<Object3D>(props.group));
  perTile_[tile.key] = props;
}

void PropRenderer::remove(const std::string& key) {
  auto it = perTile_.find(key);
  if (it == perTile_.end()) return;
  for (Mesh* m : it->second.trees) m->instances.clear();
  perTile_.erase(it);
}

void PropRenderer::update(double x, double z) {
  for (auto& kv : perTile_) {
    TileProps& p = kv.second;
    double d = std::hypot(p.cx - x, p.cz - z);
    bool near = d < 420;
    double frac = (d < 420 ? 1 : (d < 800 ? 0.55 : (d < 1200 ? 0.3 : 0.16))) * density;
    for (size_t i = 0; i < p.trees.size(); i++) {
      Mesh* mesh = p.trees[i];
      int kind = p.treeKind[i];
      GeometryPtr geo = near ? trees_.hi[kind] : trees_.lo[kind];
      if (mesh->geometry != geo) mesh->geometry = geo;
      int total = p.treeTotal[i];
      mesh->instances.resize(static_cast<size_t>(
          std::max(1, std::min(total, static_cast<int>(std::lround(total * frac))))));
    }
  }
}

}  // namespace fs

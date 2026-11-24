// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/props.ts @ v1.7.7
#include "render/props.h"

#include <cmath>

#include "core/geom.h"
#include "core/js.h"
#include "engine/geometries.h"
#include "render/tiles.h"

namespace fs {

namespace {

GeometryPtr colored(const GeometryPtr& geo, uint32_t color, double darkenBottom = 0) {
  GeometryPtr g = geo->index ? geo->toNonIndexed() : geo;
  const AttrPtr pos = g->getAttribute("position");
  const Color c(color);
  std::vector<float> arr(pos->count() * 3);
  double minY = JS_INF;
  double maxY = -JS_INF;
  for (size_t i = 0; i < pos->count(); i++) {
    minY = jsMin(minY, pos->getY(i));
    maxY = jsMax(maxY, pos->getY(i));
  }
  for (size_t i = 0; i < pos->count(); i++) {
    const double t = maxY > minY ? (pos->getY(i) - minY) / (maxY - minY) : 1;
    const double f = 1 - darkenBottom * (1 - t);
    arr[i * 3] = static_cast<float>(c.r * f);
    arr[i * 3 + 1] = static_cast<float>(c.g * f);
    arr[i * 3 + 2] = static_cast<float>(c.b * f);
  }
  g->setAttribute("color", attr(std::move(arr), 3));
  g->deleteAttribute("uv");
  return g;
}

GeometryPtr merged(const std::vector<GeometryPtr>& parts) {
  GeometryPtr g = mergeGeometries(parts, false);
  g->computeVertexNormals();
  return g;
}

GeometryPtr lampGeometry() {
  const GeometryPtr pole = colored(cylinderGeometry(0.05, 0.075, 7.2, 6, 1, true)->translate(0, 3.6, 0).shared_from_this(), 0x70757a);
  // arm reaches along -Z (the instance yaw turns it towards the road)
  const GeometryPtr arm = colored(cylinderGeometry(0.035, 0.035, 1.3, 5, 1, true)->rotateX(PI / 2).translate(0, 7.2, -0.6).shared_from_this(), 0x70757a);
  const GeometryPtr head = colored(boxGeometry(0.22, 0.09, 0.55)->translate(0, 7.18, -1.3).shared_from_this(), 0xd8d8d0);
  return merged({pole, arm, head});
}

}  // namespace

double treeWidth(int kind, double h, double r) {
  if (kind != 0) return h * (0.85 + 0.4 * r);
  const double crown = jsMin(14, 0.6 * h + 2.2) * (0.9 + 0.2 * r);
  return jsMax(h * 0.85, crown / 0.6);
}

PropRenderer::TreeGeometries PropRenderer::treeGeometries() {
  const auto trunk = [](double h, double r) -> GeometryPtr {
    return colored(cylinderGeometry(r * 0.7, r, h, 5, 1, true)->translate(0, h / 2, 0).shared_from_this(), 0x4a3b2c);
  };
  const auto blob = [](double y, double rx, double ry, int detail, uint32_t color) -> GeometryPtr {
    return colored(icosahedronGeometry(1, detail)->scale(rx, ry, rx).translate(0, y, 0).shared_from_this(), color, 0.35);
  };
  const auto cone = [](double y0, double y1, double r, uint32_t color, int sides) -> GeometryPtr {
    return colored(coneGeometry(r, y1 - y0, sides, 1, true)->translate(0, (y0 + y1) / 2, 0).shared_from_this(), color, 0.3);
  };
  TreeGeometries geos;
  geos.hi = {
      // (broadleaf: drawn wider than high – treeWidth – so its trunk is slim in the unit shape)
      merged({trunk(0.42, 0.02), blob(0.66, 0.3, 0.34, 1, 0x4c7a36)}),
      merged({trunk(0.2, 0.03), cone(0.14, 0.62, 0.23, 0x2f5a34, 7), cone(0.42, 1.0, 0.16, 0x356338, 7)}),
      merged({blob(0.5, 0.62, 0.5, 1, 0x4f7a3a)}),
      merged({trunk(0.5, 0.04), blob(0.72, 0.36, 0.3, 1, 0x5d8c3d)}),
  };
  geos.lo = {
      merged({blob(0.6, 0.3, 0.4, 0, 0x4c7a36)}),
      merged({cone(0.1, 1.0, 0.21, 0x31603a, 5)}),
      merged({blob(0.5, 0.62, 0.5, 0, 0x4f7a3a)}),
      merged({blob(0.68, 0.36, 0.33, 0, 0x5d8c3d)}),
  };
  return geos;
}

PropRenderer::PropRenderer() : geos_(treeGeometries()), lampGeo_(lampGeometry()), lampMat_(lambert({.vertexColors = true})) {
  for (int i = 0; i < 4; i++) treeMats_.push_back(lambert({.vertexColors = true}));
}

void PropRenderer::setSeason(const std::function<void(Material&, TreeMaterialKind)>& patch) {
  patch(*treeMats_[0], TreeMaterialKind::Deciduous);
  patch(*treeMats_[1], TreeMaterialKind::Evergreen);
  patch(*treeMats_[2], TreeMaterialKind::Deciduous);
  patch(*treeMats_[3], TreeMaterialKind::Orchard);
}

void PropRenderer::setShadows(bool on) {
  if (shadows_ == on) return;
  shadows_ = on;
  for (auto& p : perTile_)
    for (TreeMesh& t : p.second.trees) t.mesh->castShadow = on;
}

void PropRenderer::add(LoadedTile& tile) {
  auto group = make<Group>();
  TileProps props;
  props.group = group;
  props.cx = tile.header.cx;
  props.cz = tile.header.cz;
  const SectionArray* treesSection = tile.data.get("trees");
  if (treesSection) {
    const std::vector<float>& trees = treesSection->f32;
    const size_t n = trees.size() / 5;
    // shuffle once so that drawing only the first k instances thins the stand evenly
    std::vector<uint32_t> order(n);
    for (size_t i = 0; i < n; i++) order[i] = static_cast<uint32_t>(i);
    for (size_t i = n; i-- > 1;) {
      const size_t j = static_cast<size_t>(std::floor(hash01(static_cast<double>(i) * 7919 + tile.header.ix * 31 + tile.header.iz) * static_cast<double>(i + 1)));
      const uint32_t t = order[i];
      order[i] = order[j];
      order[j] = t;
    }
    // (a kind that is none of the four has no mesh: such a tree is not drawn)
    const auto kindOf = [&trees](size_t i) { return jsInt32(trees[i * 5 + 4]); };
    int counts[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < n; i++) {
      const int kind = kindOf(i);
      if (kind >= 0 && kind < 4) counts[kind]++;
    }
    Matrix4 m4;
    Quaternion q;
    const Vector3 up(0, 1, 0);
    Color col;
    for (int kind = 0; kind < 4; kind++) {
      if (!counts[kind]) continue;
      auto mesh = make<InstancedMesh>(geos_.hi[static_cast<size_t>(kind)], treeMats_[static_cast<size_t>(kind)], counts[kind]);
      int k = 0;
      for (size_t oi = 0; oi < n; oi++) {
        const size_t i = order[oi];
        if (kindOf(i) != kind) continue;
        const double h = trees[i * 5 + 3];
        const double r = hash01(static_cast<double>(i) * 13 + kind);
        q.setFromAxisAngle(up, r * 6.283);
        const double wide = treeWidth(kind, h, hash01(static_cast<double>(i) * 29 + 5));
        m4.compose(Vector3(trees[i * 5], trees[i * 5 + 1] - 0.15, trees[i * 5 + 2]), q, Vector3(wide, h, wide));
        mesh->setMatrixAt(k, m4);
        const double v = 0.82 + 0.36 * hash01(static_cast<double>(i) * 3 + 11);
        col.setRGB(v * (0.94 + 0.12 * r), v, v * (0.9 + 0.1 * r));
        mesh->setColorAt(k, col);
        k++;
      }
      mesh->instanceMatrix->needsUpdate();
      if (mesh->instanceColor) mesh->instanceColor->needsUpdate();
      mesh->computeBoundingSphere();
      mesh->castShadow = shadows_;
      mesh->name = "trees" + std::to_string(kind);
      group->add(mesh);
      props.trees.push_back({mesh, kind, counts[kind]});
    }
  }
  std::vector<const TileObject*> lamps;
  for (const TileObject& o : tile.header.objects)
    if (o.k == "lamp") lamps.push_back(&o);
  if (!lamps.empty()) {
    auto mesh = make<InstancedMesh>(lampGeo_, lampMat_, static_cast<int>(lamps.size()));
    Matrix4 m4;
    Quaternion q;
    const Vector3 up(0, 1, 0);
    for (size_t i = 0; i < lamps.size(); i++) {
      const TileObject& l = *lamps[i];
      q.setFromAxisAngle(up, l.yaw.value_or(0));
      m4.compose(Vector3(l.x, l.y, l.z), q, Vector3(1, 1, 1));
      mesh->setMatrixAt(static_cast<int>(i), m4);
    }
    mesh->instanceMatrix->needsUpdate();
    mesh->computeBoundingSphere();
    mesh->name = "lamps";
    group->add(mesh);
  }
  parked.add(tile, group);
  tile.group->add(group);
  perTile_[tile.key] = std::move(props);
}

void PropRenderer::remove(const LoadedTile& tile) {
  auto p = perTile_.find(tile.key);
  if (p == perTile_.end()) return;
  parked.remove(tile);
  for (TreeMesh& t : p->second.trees) t.mesh->dispose();
  for (const ObjectPtr& c : p->second.group->children)
    if (c->type == ObjectType::InstancedMesh) static_cast<InstancedMesh&>(*c).dispose();
  perTile_.erase(p);
}

void PropRenderer::update(double x, double z) {
  // (the low preset draws a fifth of the trees: its parked cars are plain boxes)
  parked.detail = density >= 0.5;
  parked.update(x, z);
  for (auto& entry : perTile_) {
    TileProps& p = entry.second;
    const double d = std::hypot(p.cx - x, p.cz - z);
    const bool near = d < 420;
    const double frac = (d < 420 ? 1 : d < 800 ? 0.55 : d < 1200 ? 0.3 : 0.16) * density;
    for (TreeMesh& t : p.trees) {
      const GeometryPtr& geo = near ? geos_.hi[static_cast<size_t>(t.kind)] : geos_.lo[static_cast<size_t>(t.kind)];
      if (t.mesh->geometry != geo) t.mesh->geometry = geo;
      t.mesh->count = static_cast<int>(jsMax(1, jsMin(t.total, jsRound(t.total * frac))));
    }
  }
}

}  // namespace fs

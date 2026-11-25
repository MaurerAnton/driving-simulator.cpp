// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/grass.ts @ v1.7.7
#include "render/grass.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/geom.h"
#include "core/js.h"
#include "world/cover.h"

namespace fs {

namespace {

/** A clump of grass: a few blades bent from the ground, each a thin quad. */
GeometryPtr grassGeometry() {
  auto g = make<BufferGeometry>();
  // (one blade: a quad of two triangles, from the ground up; x is across the blade, y up)
  std::vector<float> pos = {
      -0.5, 0, 0, 0.5, 0, 0, 0.5, 1, 0,
      -0.5, 0, 0, 0.5, 1, 0, -0.5, 1, 0,
  };
  // (the tip leans back a little, so a blade reads as grass and not a wall)
  for (size_t i = 0; i < pos.size(); i += 3) {
    const double y = pos[i + 1];
    pos[i + 2] = static_cast<float>(pos[i + 2] + y * y * 0.35);
  }
  g->setAttribute("position", attr(std::move(pos), 3));
  // (0 at the root, 1 at the tip: how far the wind may bend it)
  g->setAttribute("bend", attr(std::vector<float>{0, 0, 0, 1, 1, 1}, 1));
  // (a blade's own colour, a little apart from its neighbours')
  g->setAttribute("color", attr(std::vector<float>(18, 1.0f), 3));
  g->setIndex(std::vector<uint32_t>{0, 1, 2, 3, 4, 5});
  return g;
}

// (the original's ShaderMaterial, as it is written there)
const char* const GRASS_VERT = R"GLSL(
        attribute float bend;
        attribute vec3 color;
        uniform float uTime;
        uniform vec2 uWind;
        varying vec3 vCol;
        void main() {
          vCol = color;
          vec3 p = position;
          // (a wind that travels across the world: a wave over the field, the blades bending with it)
          vec4 world = instanceMatrix * vec4(p, 1.0);
          float phase = world.x * 0.15 + world.z * 0.11 + uTime * 1.7;
          float sway = sin(phase) * 0.16 + sin(phase * 0.5 + 1.3) * 0.06;
          p.x += uWind.x * sway * bend;
          p.z += uWind.y * sway * bend;
          gl_Position = projectionMatrix * modelViewMatrix * instanceMatrix * vec4(p, 1.0);
        }
      )GLSL";
const char* const GRASS_FRAG = R"GLSL(
        varying vec3 vCol;
        void main() {
          gl_FragColor = vec4(vCol, 1.0);
        }
      )GLSL";

}  // namespace

GrassRenderer::GrassRenderer() : geo_(grassGeometry()) {
  // (as in the original: vertex colours on – three.js then declares `color` itself, before the shader's own line)
  mat_ = shaderMaterial(GRASS_VERT, GRASS_FRAG, {{"uTime", uTime_}, {"uWind", uWind_}}, {.vertexColors = true, .side = Side::Double});
}

void GrassRenderer::setEnabled(bool on) {
  if (enabled_ == on) return;
  enabled_ = on;
  for (auto& [key, g] : perTile_) g.mesh->visible = on;
}

void GrassRenderer::add(LoadedTile& tile) {
  if (perTile_.count(tile.key)) return;
  const TileHeader& h = tile.header;
  const int cn = h.coverN;
  const std::vector<uint8_t>& cov = tile.cover;
  const int n = h.terrainN;
  const double step = h.size / (n - 1);
  const double half = h.size / 2;
  const std::vector<float>& heights = tile.heights;
  // (a blade about every 3.5 m in each direction – a meadow one can see the ground through,
  //  not a wall of grass: one InstancedMesh per tile, thinned further with the distance)
  const double spacing = 3.5;
  const int stride = std::max(1, static_cast<int>(jsRound(spacing / (h.size / cn))));
  struct Spot {
    double x, y, z, r;
  };
  std::vector<Spot> spots;
  for (int j = 0; j < cn; j += stride) {
    for (int i = 0; i < cn; i += stride) {
      const size_t c = static_cast<size_t>(j) * static_cast<size_t>(cn) + static_cast<size_t>(i);
      if (c >= cov.size() || cov[c] != COVER_GRASS) continue;
      const double fx = (i + 0.5) / cn;
      const double fz = (j + 0.5) / cn;
      const double lx = (fx - 0.5) * h.size;
      const double lz = (fz - 0.5) * h.size;
      // (the ground under it: the terrain grid, nearest sample)
      const int ti = static_cast<int>(std::min(static_cast<double>(n - 1), std::max(0.0, jsRound((lx + half) / step))));
      const int tj = static_cast<int>(std::min(static_cast<double>(n - 1), std::max(0.0, jsRound((lz + half) / step))));
      const size_t hi = static_cast<size_t>(tj) * static_cast<size_t>(n) + static_cast<size_t>(ti);
      spots.push_back({h.cx + lx, hi < heights.size() ? static_cast<double>(heights[hi]) : JS_NAN, h.cz + lz, hash01(i * 7 + j * 13 + h.ix)});
    }
  }
  const int total = static_cast<int>(spots.size());
  auto mesh = std::make_shared<InstancedMesh>(geo_, mat_, std::max(1, total));
  Matrix4 m4;
  Quaternion q;
  const Vector3 up(0, 1, 0);
  Color col;
  Vector3 sc;
  for (int k = 0; k < total; k++) {
    const Spot& s = spots[static_cast<size_t>(k)];
    q.setFromAxisAngle(up, s.r * 6.283);
    const double hh = 0.22 + s.r * 0.16;
    sc.set(0.05 + s.r * 0.03, hh, 1);
    m4.compose(Vector3(s.x, s.y, s.z), q, sc);
    mesh->setMatrixAt(k, m4);
    // (a little colour apart: darker at the root, lighter at the tip, a touch of yellow)
    const double v = 0.7 + s.r * 0.5;
    col.setRGB(v * 0.82, v, v * 0.55);
    mesh->setColorAt(k, col);
  }
  mesh->instanceMatrix->needsUpdate();
  if (mesh->instanceColor) mesh->instanceColor->needsUpdate();
  mesh->computeBoundingSphere();
  mesh->name = "grass";
  mesh->visible = enabled_;
  tile.group->add(mesh);
  perTile_[tile.key] = {mesh, h.cx, h.cz, total};
}

void GrassRenderer::remove(const std::string& key) {
  auto it = perTile_.find(key);
  if (it == perTile_.end()) return;
  it->second.mesh->removeFromParent();
  perTile_.erase(it);
}

void GrassRenderer::update(double x, double z, double dt) {
  time_ += dt;
  uTime_->value = time_;
  for (auto& [key, g] : perTile_) {
    const double d = std::hypot(g.cx - x, g.cz - z);
    const double frac = (d < 260 ? 1 : d < 500 ? 0.5 : d < 900 ? 0.2 : 0) * density;
    g.mesh->count = static_cast<int>(std::max(0.0, std::min(static_cast<double>(g.total), jsRound(g.total * frac))));
  }
}

void GrassRenderer::dispose() {
  std::vector<std::string> keys;
  for (const auto& [key, g] : perTile_) keys.push_back(key);
  for (const std::string& key : keys) remove(key);
  geo_->dispose();
  mat_->dispose();
}

const InstancedMesh* GrassRenderer::meshOf(const std::string& key) const {
  auto it = perTile_.find(key);
  return it == perTile_.end() ? nullptr : it->second.mesh.get();
}

}  // namespace fs

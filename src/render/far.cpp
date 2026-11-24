// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/far.ts @ v1.7.7
#include "render/far.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "core/js.h"
#include "render/glsl_gen.h"
#include "world/cover.h"

namespace fs {

namespace {

/** cells (of 100 m) along a side of a chunk */
constexpr int CHUNK = 32;

}  // namespace

void FarTerrain::setSeason(const std::function<void(Material&)>& patch) { patch(*material_); }

FarTerrain::FarTerrain() {
  group->name = "far ground";
  // (both sides: the skirts that close the gaps between chunks of other steps face either way)
  material_ = lambert({.vertexColors = true, .side = Side::Double, .polygonOffset = true, .polygonOffsetFactor = 2, .polygonOffsetUnits = 4});
  // (the original's hook leaves out what lies within uInner of uCam: the shader is the captured one)
  material_->patch = gamePatch("far");
  material_->patch->uniforms["uInner"] = uInner_;
  material_->patch->uniforms["uCam"] = uCam_;
  Color c;
  const Color haze(0x9fb0bf);
  for (const auto hex : COVER_COLORS) {
    c.setHex(hex).lerp(haze, 0.12);
    colours_.push_back({c.r, c.g, c.b});
  }
}

void FarTerrain::set(std::optional<FarGround> data) {
  this->data = std::move(data);
  for (auto& entry : chunks_) drop(entry.second);
  chunks_.clear();
}

void FarTerrain::update(double x, double z, int budget) {
  uInner_->value = inner;
  std::get<Vector2>(uCam_->value).set(x, z);
  if (!data || !group->visible) return;
  const FarGround& f = *data;
  const double size = CHUNK * f.cell;
  const double ci0 = std::floor((x - radius - f.minX) / size);
  const double ci1 = std::floor((x + radius - f.minX) / size);
  const double cj0 = std::floor((z - radius - f.minZ) / size);
  const double cj1 = std::floor((z + radius - f.minZ) / size);
  std::unordered_set<std::string> want;
  struct Todo {
    std::string key;
    int i;
    int j;
    double d;
    int step;
  };
  std::vector<Todo> todo;
  // (counted in numbers as the original counts: a camera that is nowhere wants no chunk)
  for (double j = jsMax(0, cj0); j <= cj1 && j * CHUNK < f.h - 1; j++) {
    for (double i = jsMax(0, ci0); i <= ci1 && i * CHUNK < f.w - 1; i++) {
      // the nearest point of the chunk
      const double x0 = f.minX + i * size;
      const double z0 = f.minZ + j * size;
      const double nx = jsMax(x0, jsMin(x0 + size, x));
      const double nz = jsMax(z0, jsMin(z0 + size, z));
      const double d = std::hypot(nx - x, nz - z);
      if (d > radius) continue;
      // (wholly inside the tiles' circle: not needed)
      const double fx = jsMax(std::fabs(x0 - x), std::fabs(x0 + size - x));
      const double fz = jsMax(std::fabs(z0 - z), std::fabs(z0 + size - z));
      if (std::hypot(fx, fz) < inner) continue;
      const int step = d < 5000 ? 1 : d < 14000 ? 2 : 4;
      const std::string key = jsStr(i) + "_" + jsStr(j);
      want.insert(key);
      const auto have = chunks_.find(key);
      if (have == chunks_.end() || have->second.step != step) todo.push_back({key, static_cast<int>(i), static_cast<int>(j), d, step});
    }
  }
  for (auto it = chunks_.begin(); it != chunks_.end();) {
    if (want.count(it->first)) {
      ++it;
      continue;
    }
    drop(it->second);
    it = chunks_.erase(it);
  }
  std::stable_sort(todo.begin(), todo.end(), [](const Todo& a, const Todo& b) { return a.d < b.d; });
  for (size_t k = 0; k < todo.size() && static_cast<double>(k) < budget; k++) {
    const Todo& t = todo[k];
    const auto old = chunks_.find(t.key);
    if (old != chunks_.end()) drop(old->second);
    const std::shared_ptr<Mesh> mesh = build(f, t.i, t.j, t.step);
    if (!mesh) {
      chunks_.erase(t.key);
      continue;
    }
    group->add(mesh);
    chunks_[t.key] = Chunk{mesh, t.step};
  }
}

void FarTerrain::drop(Chunk& ch) {
  group->remove(ch.mesh);
  ch.mesh->geometry->dispose();
}

std::shared_ptr<Mesh> FarTerrain::build(const FarGround& f, int ci, int cj, int step) {
  const int i0 = ci * CHUNK;
  const int j0 = cj * CHUNK;
  const int n = CHUNK / step + 1;
  const double ox = f.minX + i0 * f.cell;
  const double oz = f.minZ + j0 * f.cell;
  std::vector<float> pos(static_cast<size_t>(n * n + 4 * n) * 3);
  std::vector<float> col(static_cast<size_t>(n * n + 4 * n) * 3);
  int known = 0;
  const auto at = [&](int a, int b) {
    const int i = std::min(f.w - 1, i0 + a * step);
    const int j = std::min(f.h - 1, j0 + b * step);
    return static_cast<size_t>(j) * static_cast<size_t>(f.w) + static_cast<size_t>(i);
  };
  for (int b = 0; b < n; b++) {
    for (int a = 0; a < n; a++) {
      const size_t k = at(a, b);
      const double v = f.heights[k];
      const double y = v == FAR_NONE ? JS_NAN : f.base + v * f.step;
      if (!std::isnan(y)) known++;
      const size_t o = static_cast<size_t>(b * n + a) * 3;
      pos[o] = static_cast<float>(a * step * f.cell);
      // (a little below the ground as it is: where both are drawn the tiles win)
      pos[o + 1] = static_cast<float>(std::isnan(y) ? 0 : y - 1.5);
      pos[o + 2] = static_cast<float>(b * step * f.cell);
      const std::array<double, 3>& c = f.cover[k] < colours_.size() ? colours_[f.cover[k]] : colours_[0];
      col[o] = static_cast<float>(c[0]);
      col[o + 1] = static_cast<float>(c[1]);
      col[o + 2] = static_cast<float>(c[2]);
    }
  }
  if (known == 0) return nullptr;
  // the skirt: each edge point again, 40 m lower
  std::vector<uint32_t> edge;
  for (int a = 0; a < n; a++) edge.push_back(static_cast<uint32_t>(a));
  for (int b = 1; b < n; b++) edge.push_back(static_cast<uint32_t>(b * n + n - 1));
  for (int a = n - 2; a >= 0; a--) edge.push_back(static_cast<uint32_t>((n - 1) * n + a));
  for (int b = n - 2; b > 0; b--) edge.push_back(static_cast<uint32_t>(b * n));
  for (size_t e = 0; e < edge.size(); e++) {
    const size_t k = edge[e];
    const size_t o = (static_cast<size_t>(n * n) + e) * 3;
    pos[o] = pos[k * 3];
    pos[o + 1] = static_cast<float>(pos[k * 3 + 1] - 40.0);
    pos[o + 2] = pos[k * 3 + 2];
    col[o] = col[k * 3];
    col[o + 1] = col[k * 3 + 1];
    col[o + 2] = col[k * 3 + 2];
  }
  std::vector<uint32_t> idx;
  for (int b = 0; b + 1 < n; b++) {
    for (int a = 0; a + 1 < n; a++) {
      const uint32_t k = static_cast<uint32_t>(b * n + a);
      const uint32_t un = static_cast<uint32_t>(n);
      idx.insert(idx.end(), {k, k + un, k + 1, k + 1, k + un, k + un + 1});
    }
  }
  for (size_t e = 0; e < edge.size(); e++) {
    const uint32_t a = edge[e];
    const uint32_t b = edge[(e + 1) % edge.size()];
    const uint32_t sa = static_cast<uint32_t>(static_cast<size_t>(n * n) + e);
    const uint32_t sb = static_cast<uint32_t>(static_cast<size_t>(n * n) + ((e + 1) % edge.size()));
    idx.insert(idx.end(), {a, sa, b, b, sa, sb});
  }
  auto g = make<BufferGeometry>();
  g->setAttribute("position", attr(std::move(pos), 3));
  g->setAttribute("color", attr(std::move(col), 3));
  g->setIndex(idx);
  g->computeVertexNormals();
  g->computeBoundingSphere();
  auto mesh = make<Mesh>(g, material_);
  mesh->position.set(ox, 0, oz);
  mesh->matrixAutoUpdate = false;
  mesh->updateMatrix();
  mesh->receiveShadow = false;
  mesh->castShadow = false;
  return mesh;
}

}  // namespace fs

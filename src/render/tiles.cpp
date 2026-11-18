// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Map tiles → scene objects (terrain with three detail levels, roads, paint,
 * structures, buildings) and terrain height queries.
 *
 * Origin: src/render/tiles.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).  The maths mirrors the JavaScript original: the same skirt, the
 * same index strides, the same bilinear height lookup.
 */
#include "render/tiles.h"

#include <algorithm>
#include <cmath>

#include "world/cover.h"

namespace fs {

namespace {
/** Cover class → sRGB (from the COVER_COLORS table of world/cover.cpp). */
uint32_t coverRgb(int c) {
  if (c < 0 || c >= 18) c = 0;
  return COVER_COLORS[c];
}

PolySample gTmp;
}  // namespace

TileManager::TileManager(const WorldJson& world, const std::string& baseUrl, TileSource& source)
    : meta_(world.meta), baseUrl_(baseUrl), source_(source) {
  available_.insert(world.tiles.begin(), world.tiles.end());
  group.name = "tiles";
}

void TileManager::update(double x, double z) {
  const double minX = meta_.minX, minZ = meta_.minZ, tileSize = meta_.tileSize;
  double r = viewRadius;
  long ix0 = static_cast<long>(std::floor((x - r - minX) / tileSize));
  long ix1 = static_cast<long>(std::floor((x + r - minX) / tileSize));
  long iz0 = static_cast<long>(std::floor((z - r - minZ) / tileSize));
  long iz1 = static_cast<long>(std::floor((z + r - minZ) / tileSize));
  std::vector<std::pair<std::string, double>> wanted;
  std::set<std::string> keep;
  for (long iz = iz0; iz <= iz1; iz++) {
    for (long ix = ix0; ix <= ix1; ix++) {
      std::string key = tileKey(static_cast<int>(ix), static_cast<int>(iz));
      if (!available_.count(key)) continue;
      double cx = minX + (ix + 0.5) * tileSize;
      double cz = minZ + (iz + 0.5) * tileSize;
      double d = std::hypot(cx - x, cz - z);
      if (d > r + tileSize * 0.75) continue;
      keep.insert(key);
      if (!tiles.count(key) && !loading_.count(key) && !failed_.count(key))
        wanted.push_back({key, d});
    }
  }
  std::stable_sort(wanted.begin(), wanted.end(),
                   [](const auto& a, const auto& b) { return a.second < b.second; });
  wantCount_ = static_cast<int>(wanted.size());
  for (const auto& w : wanted) {
    if (static_cast<int>(loading_.size()) >= maxConcurrent) break;
    wantCount_--;
    load(w.first);
  }
  for (auto it = tiles.begin(); it != tiles.end();) {
    const std::string& key = it->first;
    LoadedTile& tile = *it->second;
    if (keep.count(key)) {
      ++it;
      continue;
    }
    double dx = tile.header.cx - x;
    double dz = tile.header.cz - z;
    if (std::hypot(dx, dz) > r + tileSize * 1.6) {
      unload(tile);
      it = tiles.erase(it);
    } else {
      ++it;
    }
  }
}

void TileManager::load(const std::string& key) {
  loading_.insert(key);
  std::vector<uint8_t> buf;
  if (!source_.read(key, buf)) {
    failed_.insert(key);
    loading_.erase(key);
    return;
  }
  try {
    UnpackedTile data = unpackTile(buf);
    std::unique_ptr<LoadedTile> tile = build(key, data);
    LoadedTile* raw = tile.get();
    group.addRef(tile->group.get());
    tiles[key] = std::move(tile);
    for (auto& l : listeners_) l(*raw, true);
  } catch (...) {
    failed_.insert(key);
  }
  loading_.erase(key);
}

void TileManager::unload(LoadedTile& tile) {
  for (auto& l : listeners_) l(tile, false);
  group.remove(tile.group.get());
}

std::unique_ptr<LoadedTile> TileManager::build(const std::string& key, UnpackedTile& data) {
  auto out = std::make_unique<LoadedTile>();
  out->key = key;
  out->header = data.header;
  const TileHeader& h = data.header;
  out->group = std::make_unique<Group>();
  out->group->name = "tile " + key;

  // ---- terrain ----------------------------------------------------------
  int n = h.terrainN;
  const std::vector<double>* hgt = data.get("hgt");
  out->heights.assign(static_cast<size_t>(n) * n, 0);
  if (hgt)
    for (size_t i = 0; i < out->heights.size() && i < hgt->size(); i++)
      out->heights[i] = h.terrainMin + (*hgt)[i] * h.terrainScale;
  double step = h.size / (n - 1);
  double half = h.size / 2;
  size_t skirtBase = static_cast<size_t>(n) * n;
  size_t total = skirtBase + 4 * static_cast<size_t>(n);
  std::vector<double> pos(total * 3, 0), nor(total * 3, 0), uv(total * 2, 0);
  for (int j = 0; j < n; j++) {
    for (int i = 0; i < n; i++) {
      size_t k = static_cast<size_t>(j) * n + i;
      pos[k * 3] = -half + i * step;
      pos[k * 3 + 1] = out->heights[k] - h.yBase;
      pos[k * 3 + 2] = -half + j * step;
      uv[k * 2] = i / static_cast<double>(n - 1);
      uv[k * 2 + 1] = j / static_cast<double>(n - 1);
      double hl = out->heights[static_cast<size_t>(j) * n + std::max(0, i - 1)];
      double hr = out->heights[static_cast<size_t>(j) * n + std::min(n - 1, i + 1)];
      double hu = out->heights[static_cast<size_t>(std::max(0, j - 1)) * n + i];
      double hd = out->heights[static_cast<size_t>(std::min(n - 1, j + 1)) * n + i];
      double sx = (i > 0 && i < n - 1 ? 2 : 1) * step;
      double sz = (j > 0 && j < n - 1 ? 2 : 1) * step;
      double nx = -(hr - hl) / sx;
      double nz = -(hd - hu) / sz;
      double l = std::hypot(nx, 1.0, nz);
      nor[k * 3] = nx / l;
      nor[k * 3 + 1] = 1 / l;
      nor[k * 3 + 2] = nz / l;
    }
  }
  // skirt vertices (copies of the rim, pushed down) hide cracks between detail levels
  auto rim = [&](int side, int t) -> size_t {
    return side == 0 ? static_cast<size_t>(t)
                     : (side == 1 ? static_cast<size_t>((n - 1) * n + t)
                                  : (side == 2 ? static_cast<size_t>(t * n)
                                               : static_cast<size_t>(t * n + n - 1)));
  };
  for (int side = 0; side < 4; side++) {
    for (int t = 0; t < n; t++) {
      size_t src = rim(side, t);
      size_t dst = skirtBase + static_cast<size_t>(side) * n + t;
      pos[dst * 3] = pos[src * 3];
      pos[dst * 3 + 1] = pos[src * 3 + 1] - 12;
      pos[dst * 3 + 2] = pos[src * 3 + 2];
      nor[dst * 3] = nor[src * 3];
      nor[dst * 3 + 1] = nor[src * 3 + 1];
      nor[dst * 3 + 2] = nor[src * 3 + 2];
      uv[dst * 2] = uv[src * 2];
      uv[dst * 2 + 1] = uv[src * 2 + 1];
    }
  }
  auto posAttr = std::make_shared<BufferAttribute>(pos, 3);
  auto norAttr = std::make_shared<BufferAttribute>(nor, 3);
  auto uvAttr = std::make_shared<BufferAttribute>(uv, 2);

  auto indexFor = [&](int stride) -> std::shared_ptr<BufferAttribute> {
    int m = (n - 1) / stride;
    std::vector<double> idx;
    for (int j = 0; j < m; j++) {
      for (int i = 0; i < m; i++) {
        size_t a = static_cast<size_t>(j) * stride * n + i * stride;
        size_t b = a + stride;
        size_t c = a + static_cast<size_t>(stride) * n;
        size_t d = c + stride;
        // diagonal b–c: must match TerrainGrid.at / heightAt
        for (size_t v : {a, c, b, b, c, d}) idx.push_back(static_cast<double>(v));
      }
    }
    for (int side = 0; side < 4; side++) {
      for (int t = 0; t < n - 1; t += stride) {
        size_t a = rim(side, t);
        size_t b = rim(side, t + stride);
        size_t sa = skirtBase + static_cast<size_t>(side) * n + t;
        size_t sb = skirtBase + static_cast<size_t>(side) * n + t + stride;
        for (size_t v : {a, b, sa, b, sb, sa, a, sa, b, b, sa, sb})
          idx.push_back(static_cast<double>(v));
      }
    }
    return std::make_shared<BufferAttribute>(idx, 1);
  };

  const std::vector<double>* cov = data.get("cov");
  out->cover.assign(static_cast<size_t>(h.coverN) * h.coverN, 0);
  if (cov)
    for (size_t i = 0; i < out->cover.size() && i < cov->size(); i++)
      out->cover[i] = static_cast<uint8_t>((*cov)[i]);

  int strides[3] = {1, 2, 4};
  int strideCount = ((n - 1) % 4 == 0) ? 3 : 1;
  double dists[3] = {0, 650, 1150};
  auto lod = std::make_unique<LOD>();
  for (int li = 0; li < strideCount; li++) {
    auto g = std::make_shared<BufferGeometry>();
    g->position = posAttr;
    g->normal = norAttr;
    g->uv = uvAttr;
    g->index = indexFor(strides[li]);
    auto mesh = std::make_unique<Mesh>(g, kindTerrain_);
    mesh->receiveShadow = true;
    mesh->matrixAutoUpdate = false;
    lod->addLevel(mesh.release(), dists[li]);
  }
  lod->position.set(h.cx, h.yBase, h.cz);
  lod->updateMatrix();
  lod->matrixAutoUpdate = false;
  out->group->add(std::unique_ptr<Object3D>(lod.release()));

  // ---- indexed meshes ---------------------------------------------------
  auto mesh = [&](const char* name, MaterialKind kind,
                  const std::function<void(BufferGeometry&)>& setup) -> Mesh* {
    std::string pn = std::string(name) + "_pos";
    std::string in = std::string(name) + "_idx";
    const std::vector<double>* p = data.get(pn);
    const std::vector<double>* idx = data.get(in);
    if (!p || !idx) return nullptr;
    auto g = std::make_shared<BufferGeometry>();
    g->position = std::make_shared<BufferAttribute>(*p, 3);
    g->index = std::make_shared<BufferAttribute>(*idx, 1);
    if (setup) setup(*g);
    g->computeVertexNormals();
    auto m = std::make_unique<Mesh>(g, kind);
    m->position.set(h.cx, h.yBase, h.cz);
    m->scale.set(POS_XZ_SCALE, POS_Y_SCALE, POS_XZ_SCALE);
    m->updateMatrix();
    m->matrixAutoUpdate = false;
    m->name = name;
    out->group->add(std::unique_ptr<Object3D>(m.release()));
    return static_cast<Mesh*>(out->group->children.back().get());
  };

  Mesh* ground = mesh("ground", kindGround_, [&](BufferGeometry& g) {
    const std::vector<double>* a8 = data.get("ground_a8");
    if (a8) g.setAttribute("amat", std::make_shared<BufferAttribute>(*a8, 1));
  });
  if (ground) ground->receiveShadow = true;

  Mesh* mark = mesh("mark", kindPaint_, [&](BufferGeometry& g) {
    const std::vector<double>* a8 = data.get("mark_a8");
    if (a8) g.setAttribute("amat", std::make_shared<BufferAttribute>(*a8, 1));
  });
  if (mark) mark->renderOrder = 1;

  Mesh* st = mesh("struct", kindStruct_, [&](BufferGeometry& g) {
    const std::vector<double>* a8 = data.get("struct_a8");
    if (a8) g.setAttribute("color", std::make_shared<BufferAttribute>(*a8, 3));
  });
  if (st) {
    st->castShadow = true;
    st->receiveShadow = true;
  }

  Mesh* bldg = mesh("bldg", kindBldg_, [&](BufferGeometry& g) {
    const std::vector<double>* a8 = data.get("bldg_a8");
    const std::vector<double>* a16 = data.get("bldg_a16");
    if (a8) g.setAttribute("astyle", std::make_shared<BufferAttribute>(*a8, 4));
    if (a16) g.setAttribute("auvd", std::make_shared<BufferAttribute>(*a16, 4));
  });
  if (bldg) {
    bldg->castShadow = true;
    bldg->receiveShadow = true;
  }

  out->group->matrixAutoUpdate = false;
  return out;
}

int TileManager::coverAt(double x, double z) const {
  double minX = meta_.minX, minZ = meta_.minZ, tileSize = meta_.tileSize;
  long ix = static_cast<long>(std::floor((x - minX) / tileSize));
  long iz = static_cast<long>(std::floor((z - minZ) / tileSize));
  auto it = tiles.find(tileKey(static_cast<int>(ix), static_cast<int>(iz)));
  if (it == tiles.end()) return 0;
  const LoadedTile& tile = *it->second;
  int cn = tile.header.coverN;
  if (cn <= 0 || tile.cover.empty()) return 0;
  double lx = x - (minX + ix * tileSize);
  double lz = z - (minZ + iz * tileSize);
  int cx = std::max(0, std::min(cn - 1, static_cast<int>(std::floor(lx / tileSize * cn))));
  int cz = std::max(0, std::min(cn - 1, static_cast<int>(std::floor(lz / tileSize * cn))));
  return tile.cover[static_cast<size_t>(cz) * cn + cx];
}

double TileManager::heightAt(double x, double z) const {
  double minX = meta_.minX, minZ = meta_.minZ, tileSize = meta_.tileSize;
  long ix = static_cast<long>(std::floor((x - minX) / tileSize));
  long iz = static_cast<long>(std::floor((z - minZ) / tileSize));
  auto it = tiles.find(tileKey(static_cast<int>(ix), static_cast<int>(iz)));
  if (it == tiles.end()) return NAN;
  const LoadedTile& tile = *it->second;
  int n = tile.header.terrainN;
  if (n < 2) return NAN;
  double step = tileSize / (n - 1);
  double fx = std::max(0.0, std::min(n - 1.0001, (x - (minX + ix * tileSize)) / step));
  double fz = std::max(0.0, std::min(n - 1.0001, (z - (minZ + iz * tileSize)) / step));
  int i = static_cast<int>(std::floor(fx));
  int j = static_cast<int>(std::floor(fz));
  double tx = fx - i;
  double tz = fz - j;
  const auto& hh = tile.heights;
  size_t k = static_cast<size_t>(j) * n + i;
  if (k + static_cast<size_t>(n) + 1 >= hh.size()) return NAN;
  double h00 = hh[k], h10 = hh[k + 1], h01 = hh[k + n], h11 = hh[k + n + 1];
  return tx + tz <= 1 ? h00 + (h10 - h00) * tx + (h01 - h00) * tz
                      : h11 + (h01 - h11) * (1 - tx) + (h10 - h11) * (1 - tz);
}

}  // namespace fs

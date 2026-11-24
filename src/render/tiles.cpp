// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/tiles.ts @ v1.7.7
#include "render/tiles.h"

#include <cmath>

#include "core/js.h"
#include "platform/plat.h"
#include "render/coveredge.h"
#include "render/materials.h"
#include "world/cover.h"

namespace fs {

namespace {

/** The bytes of a tile (gunzipped where the server did not do it for us). */
Task<Bytes> fetchTile(std::function<Task<plat::Response>(std::string)> fetch, std::string url) {
  // (from a map stored in this browser, if one is in use: world/mappack)
  plat::Response res = co_await (fetch ? fetch(url) : plat::fetch(url));
  if (!res.ok()) throw std::runtime_error(url + ": HTTP " + std::to_string(res.status));
  if (plat::isGzip(res.body)) co_return plat::gunzip(res.body);
  co_return std::move(res.body);
}

/** a section's array as an attribute of a geometry (the array moves: the section is left empty) */
AttrPtr attrOf(SectionArray* s, int itemSize, bool normalized = false) {
  if (!s) return nullptr;
  switch (s->type) {
    case SectionType::U8: return attr(std::move(s->u8), itemSize, normalized);
    case SectionType::I8: return attr(std::move(s->i8), itemSize, normalized);
    case SectionType::U16: return attr(std::move(s->u16), itemSize, normalized);
    case SectionType::I16: return attr(std::move(s->i16), itemSize, normalized);
    case SectionType::U32: return attr(std::move(s->u32), itemSize, normalized);
    case SectionType::I32: return attr(std::move(s->i32), itemSize, normalized);
    case SectionType::F32: return attr(std::move(s->f32), itemSize, normalized);
  }
  return nullptr;
}

}  // namespace

TileManager::TileManager(const TileGrid& grid, const std::string& baseUrl, const std::string& version)
    : grid_(grid), baseUrl_(baseUrl), query_(version.empty() ? "" : "?v=" + version) {
  groundMat_ = makeGroundMaterial(false);
  paintMat_ = makeGroundMaterial(true);
  structMat_ = makeStructMaterial();
  bldgMat_ = makeBuildingMaterial();
  plainMat_ = makeBuildingMaterial(true);
  group->name = "tiles";
}

void TileManager::setMaterialHook(std::function<void(Material&, TileMaterialKind)> hook) {
  materialHook_ = std::move(hook);
  materialHook_(*groundMat_, TileMaterialKind::Ground);
  materialHook_(*paintMat_, TileMaterialKind::Paint);
  materialHook_(*structMat_, TileMaterialKind::Struct);
  materialHook_(*bldgMat_, TileMaterialKind::Building);
  materialHook_(*plainMat_, TileMaterialKind::Building);
}

void TileManager::setFacades(bool on) {
  if (on == facades_) return;
  facades_ = on;
  const MaterialPtr& mat = on ? bldgMat_ : plainMat_;
  group->traverse([&](Object3D& o) {
    if (o.name == "bldg" && o.type == ObjectType::Mesh) static_cast<Mesh&>(o).material = mat;
  });
  // the measured details of walls: only with facades
  for (auto& kv : tiles)
    if (kv.second->detail) kv.second->detail->visible = on;
}

void TileManager::update(double x, double z) {
  const double minX = grid_.x0, minZ = grid_.z0, tileSize = grid_.size;
  const double r = viewRadius;
  const int ix0 = static_cast<int>(std::floor((x - r - minX) / tileSize));
  const int ix1 = static_cast<int>(std::floor((x + r - minX) / tileSize));
  const int iz0 = static_cast<int>(std::floor((z - r - minZ) / tileSize));
  const int iz1 = static_cast<int>(std::floor((z + r - minZ) / tileSize));
  struct Want {
    std::string key;
    double d;
  };
  std::vector<Want> wanted;
  std::unordered_set<std::string> keep;
  for (int iz = iz0; iz <= iz1; iz++) {
    for (int ix = ix0; ix <= ix1; ix++) {
      if (!grid_.has(ix, iz)) continue;
      std::string key = tileKey(ix, iz);
      const double cx = minX + (ix + 0.5) * tileSize;
      const double cz = minZ + (iz + 0.5) * tileSize;
      const double d = std::hypot(cx - x, cz - z);
      if (d > r + tileSize * 0.75) continue;
      keep.insert(key);
      if (!tiles.count(key) && !loading_.count(key) && mayLoad(key)) wanted.push_back({key, d});
    }
  }
  std::stable_sort(wanted.begin(), wanted.end(), [](const Want& a, const Want& b) { return a.d < b.d; });
  wantCount_ = static_cast<int>(wanted.size());
  int have = 0;
  for (const std::string& key : keep)
    if (tiles.count(key)) have++;
  progress = keep.empty() ? 1 : static_cast<double>(have) / static_cast<double>(keep.size());
  for (const Want& w : wanted) {
    if (static_cast<int>(loading_.size()) >= maxConcurrent) break;
    wantCount_--;
    load(w.key).detach();
  }
  std::vector<LoadedTile*> gone;
  for (auto& kv : tiles) {
    if (keep.count(kv.first)) continue;
    const double dx = kv.second->header.cx - x;
    const double dz = kv.second->header.cz - z;
    if (std::hypot(dx, dz) > r + tileSize * 1.6) gone.push_back(kv.second.get());
  }
  for (LoadedTile* t : gone) unload(*t);
}

bool TileManager::mayLoad(const std::string& key) {
  auto f = failed_.find(key);
  return f == failed_.end() || (f->second.n < 5 && plat::nowMs() - f->second.at > 1500 * std::pow(2.0, f->second.n - 1));
}

Task<void> TileManager::load(std::string key) {
  loading_.insert(key);
  std::shared_ptr<bool> alive = alive_;
  bool ok = false;
  std::string why;
  Bytes buf;
  try {
    buf = co_await fetchTile(fetch, baseUrl_ + "tiles/" + key + ".fsb" + query_);
    ok = true;
  } catch (const std::exception& err) {
    why = err.what();
  }
  if (!*alive) co_return;
  if (ok) {
    try {
      failed_.erase(key);
      std::unique_ptr<LoadedTile> tile = build(key, unpackTile(buf));
      LoadedTile& t = *tile;
      tiles[key] = std::move(tile);
      forgetLast();
      group->add(t.group);
      for (const TileListener& l : listeners_) l(t, true);
    } catch (const std::exception& err) {
      ok = false;
      why = err.what();
    }
  }
  if (!ok) {
    plat::warn("tile failed " + key + " " + why);
    Failed& f = failed_[key];
    f.n++;
    f.at = plat::nowMs();
  }
  loading_.erase(key);
}

void TileManager::unload(LoadedTile& tile) {
  for (const TileListener& l : listeners_) l(tile, false);
  group->remove(tile.group);
  // (its geometries, the cover texture and the terrain's material go with the last reference to them)
  tiles.erase(tile.key);
  forgetLast();
}

void TileManager::forgetLast() {
  lastValid_ = false;
  lastTile_ = nullptr;
}

LoadedTile* TileManager::tileAt(int ix, int iz) {
  if (lastValid_ && ix == lastIx_ && iz == lastIz_) return lastTile_;
  auto it = tiles.find(tileKey(ix, iz));
  lastIx_ = ix;
  lastIz_ = iz;
  lastValid_ = true;
  lastTile_ = it == tiles.end() ? nullptr : it->second.get();
  return lastTile_;
}

std::unique_ptr<LoadedTile> TileManager::build(const std::string& key, UnpackedTile data) {
  const TileHeader& h = data.header;
  auto group = make<Group>();
  group->name = "tile " + key;

  // ---- terrain ----------------------------------------------------------
  const int n = h.terrainN;
  const SectionArray* hgtS = data.get("hgt");
  if (!hgtS || n < 2) throw std::runtime_error("a tile without heights");
  const std::vector<uint16_t>& hgt = hgtS->u16;
  std::vector<float> heights(static_cast<size_t>(n) * static_cast<size_t>(n));
  for (size_t i = 0; i < heights.size(); i++) heights[i] = static_cast<float>(h.terrainMin + hgt[i] * h.terrainScale);
  const double step = h.size / (n - 1);
  const double half = h.size / 2;
  const int skirtBase = n * n;
  std::vector<float> pos(static_cast<size_t>(n * n + 4 * n) * 3);
  std::vector<float> nor(static_cast<size_t>(n * n + 4 * n) * 3);
  std::vector<float> uv(static_cast<size_t>(n * n + 4 * n) * 2);
  for (int j = 0; j < n; j++) {
    for (int i = 0; i < n; i++) {
      const size_t k = static_cast<size_t>(j * n + i);
      pos[k * 3] = static_cast<float>(-half + i * step);
      pos[k * 3 + 1] = static_cast<float>(heights[k] - h.yBase);
      pos[k * 3 + 2] = static_cast<float>(-half + j * step);
      uv[k * 2] = static_cast<float>(static_cast<double>(i) / (n - 1));
      uv[k * 2 + 1] = static_cast<float>(static_cast<double>(j) / (n - 1));
      const double hl = heights[static_cast<size_t>(j * n + std::max(0, i - 1))];
      const double hr = heights[static_cast<size_t>(j * n + std::min(n - 1, i + 1))];
      const double hu = heights[static_cast<size_t>(std::max(0, j - 1) * n + i)];
      const double hd = heights[static_cast<size_t>(std::min(n - 1, j + 1) * n + i)];
      const double sx = (i > 0 && i < n - 1 ? 2 : 1) * step;
      const double sz = (j > 0 && j < n - 1 ? 2 : 1) * step;
      const double nx = -(hr - hl) / sx;
      const double nz = -(hd - hu) / sz;
      const double l = std::hypot(nx, 1.0, nz);
      nor[k * 3] = static_cast<float>(nx / l);
      nor[k * 3 + 1] = static_cast<float>(1 / l);
      nor[k * 3 + 2] = static_cast<float>(nz / l);
    }
  }
  // skirt vertices (copies of the rim, pushed down) hide cracks between detail levels
  auto rim = [n](int side, int t) { return side == 0 ? t : side == 1 ? (n - 1) * n + t : side == 2 ? t * n : t * n + n - 1; };
  for (int side = 0; side < 4; side++) {
    for (int t = 0; t < n; t++) {
      const size_t src = static_cast<size_t>(rim(side, t));
      const size_t dst = static_cast<size_t>(skirtBase + side * n + t);
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
  AttrPtr posAttr = attr(std::move(pos), 3);
  AttrPtr norAttr = attr(std::move(nor), 3);
  AttrPtr uvAttr = attr(std::move(uv), 2);
  auto indexFor = [&](int stride) -> AttrPtr {
    const int m = (n - 1) / stride;
    std::vector<uint32_t> idx;
    idx.reserve(static_cast<size_t>(m) * static_cast<size_t>(m) * 6 + static_cast<size_t>(4 * (n - 1) / stride) * 12);
    for (int j = 0; j < m; j++) {
      for (int i = 0; i < m; i++) {
        const uint32_t a = static_cast<uint32_t>(j * stride * n + i * stride);
        const uint32_t b = a + static_cast<uint32_t>(stride);
        const uint32_t c = a + static_cast<uint32_t>(stride * n);
        const uint32_t d = c + static_cast<uint32_t>(stride);
        // diagonal b–c: must match TerrainGrid.at / heightAt
        idx.insert(idx.end(), {a, c, b, b, c, d});
      }
    }
    for (int side = 0; side < 4; side++) {
      for (int t = 0; t < n - 1; t += stride) {
        const uint32_t a = static_cast<uint32_t>(rim(side, t));
        const uint32_t b = static_cast<uint32_t>(rim(side, t + stride));
        const uint32_t sa = static_cast<uint32_t>(skirtBase + side * n + t);
        const uint32_t sb = static_cast<uint32_t>(skirtBase + side * n + t + stride);
        idx.insert(idx.end(), {a, b, sa, b, sb, sa, a, sa, b, b, sa, sb});   // both windings: visible from either side
      }
    }
    return attr(std::move(idx), 1);
  };
  const SectionArray* covS = data.get("cov");
  if (!covS) throw std::runtime_error("a tile without ground cover");
  std::vector<uint8_t> cov = covS->u8;
  const int cn = h.coverN;
  std::vector<uint8_t> tex(static_cast<size_t>(cn) * static_cast<size_t>(cn) * 4);
  // (alpha: how far the cell's centre lies inside its class, for smooth edges – render/coveredge; 0 where the map has none)
  const SectionArray* cedge = data.get("cedge");
  const size_t classes = sizeof(COVER_COLORS) / sizeof(COVER_COLORS[0]);
  for (size_t i = 0; i < static_cast<size_t>(cn) * static_cast<size_t>(cn); i++) {
    const uint32_t c = COVER_COLORS[cov[i] < classes ? cov[i] : 0];
    tex[i * 4] = static_cast<uint8_t>((c >> 16) & 255);
    tex[i * 4 + 1] = static_cast<uint8_t>((c >> 8) & 255);
    tex[i * 4 + 2] = static_cast<uint8_t>(c & 255);
    tex[i * 4 + 3] = static_cast<uint8_t>(coverEdgeAlpha(cedge && i < cedge->u8.size() ? cedge->u8[i] : 0));
  }
  TexturePtr coverTex = dataTexture(std::move(tex), cn, cn, PixelFormat::RGBA);
  coverTex->srgb = true;
  coverTex->magFilter = Filter::Linear;
  coverTex->minFilter = Filter::LinearMipmapLinear;
  coverTex->generateMipmaps = true;
  coverTex->needsUpdate();
  MaterialPtr terrainMat = makeTerrainMaterial(coverTex);
  if (materialHook_) materialHook_(*terrainMat, TileMaterialKind::Terrain);
  auto lod = make<LOD>();
  const std::vector<int> strides = (n - 1) % 4 == 0 ? std::vector<int>{1, 2, 4} : std::vector<int>{1};
  const double dists[3] = {0, 650, 1150};
  for (size_t li = 0; li < strides.size(); li++) {
    auto g = make<BufferGeometry>();
    g->setAttribute("position", posAttr);
    g->setAttribute("normal", norAttr);
    g->setAttribute("uv", uvAttr);
    g->setIndex(indexFor(strides[li]));
    g->computeBoundingSphere();
    auto mesh = make<Mesh>(g, terrainMat);
    mesh->receiveShadow = true;
    mesh->matrixAutoUpdate = false;
    lod->addLevel(mesh, dists[li]);
  }
  lod->position.set(h.cx, h.yBase, h.cz);
  lod->updateMatrix();
  lod->matrixAutoUpdate = false;
  group->add(lod);

  // ---- indexed meshes ---------------------------------------------------
  auto mesh = [&](const std::string& name, const MaterialPtr& mat, const std::function<void(BufferGeometry&)>& setup) -> std::shared_ptr<Mesh> {
    SectionArray* p = data.get(name + "_pos");
    SectionArray* idx = data.get(name + "_idx");
    if (!p || !idx || p->size() == 0 || idx->size() == 0) return nullptr;
    auto g = make<BufferGeometry>();
    g->setAttribute("position", attrOf(p, 3));
    g->setIndex(attrOf(idx, 1));
    if (setup) setup(*g);
    g->computeVertexNormals();
    g->computeBoundingSphere();
    auto m = make<Mesh>(g, mat);
    m->position.set(h.cx, h.yBase, h.cz);
    m->scale.set(POS_XZ_SCALE, POS_Y_SCALE, POS_XZ_SCALE);
    m->updateMatrix();
    m->matrixAutoUpdate = false;
    m->name = name;
    group->add(m);
    return m;
  };
  auto a8 = [&](const std::string& name) { return data.get(name + "_a8"); };
  auto ground = mesh("ground", groundMat_, [&](BufferGeometry& g) { g.setAttribute("amat", attrOf(a8("ground"), 1)); });
  if (ground) ground->receiveShadow = true;
  auto mark = mesh("mark", paintMat_, [&](BufferGeometry& g) { g.setAttribute("amat", attrOf(a8("mark"), 1)); });
  if (mark) mark->renderOrder = 1;
  auto strct = mesh("struct", structMat_, [&](BufferGeometry& g) { g.setAttribute("color", attrOf(a8("struct"), 3, true)); });
  if (strct) {
    strct->castShadow = true;
    strct->receiveShadow = true;
  }
  // (the facades' section is worked out from the buildings' corners when it is first asked for:
  // ask before mesh() moves the corners into the geometry – the original moves nothing)
  SectionArray* bldgA16 = data.get("bldg_a16");
  auto bldg = mesh("bldg", facades_ ? bldgMat_ : plainMat_, [&](BufferGeometry& g) {
    g.setAttribute("astyle", attrOf(a8("bldg"), 4));
    g.setAttribute("auvd", attrOf(bldgA16, 4));
  });
  if (bldg) {
    bldg->castShadow = true;
    bldg->receiveShadow = true;
  }

  group->matrixAutoUpdate = false;
  auto tile = std::make_unique<LoadedTile>();
  tile->key = key;
  tile->header = h;
  tile->group = group;
  tile->heights = std::move(heights);
  tile->cover = std::move(cov);
  tile->data = std::move(data);
  return tile;
}

int TileManager::coverAt(double x, double z) {
  const double minX = grid_.x0, minZ = grid_.z0, tileSize = grid_.size;
  const int ix = static_cast<int>(std::floor((x - minX) / tileSize));
  const int iz = static_cast<int>(std::floor((z - minZ) / tileSize));
  LoadedTile* tile = tileAt(ix, iz);
  if (!tile) return 0;
  const int cn = tile->header.coverN;
  const int cx = std::max(0, std::min(cn - 1, static_cast<int>(std::floor(((x - (minX + ix * tileSize)) / tileSize) * cn))));
  const int cz = std::max(0, std::min(cn - 1, static_cast<int>(std::floor(((z - (minZ + iz * tileSize)) / tileSize) * cn))));
  return tile->cover[static_cast<size_t>(cz * cn + cx)];
}

double TileManager::heightAt(double x, double z) {
  const double minX = grid_.x0, minZ = grid_.z0, tileSize = grid_.size;
  const int ix = static_cast<int>(std::floor((x - minX) / tileSize));
  const int iz = static_cast<int>(std::floor((z - minZ) / tileSize));
  LoadedTile* tile = tileAt(ix, iz);
  if (!tile) return JS_NAN;
  const int n = tile->header.terrainN;
  const double step = tileSize / (n - 1);
  const double fx = std::max(0.0, std::min(n - 1.0001, (x - (minX + ix * tileSize)) / step));
  const double fz = std::max(0.0, std::min(n - 1.0001, (z - (minZ + iz * tileSize)) / step));
  const int i = static_cast<int>(std::floor(fx));
  const int j = static_cast<int>(std::floor(fz));
  const double tx = fx - i;
  const double tz = fz - j;
  const std::vector<float>& hh = tile->heights;
  const size_t k = static_cast<size_t>(j * n + i);
  const double h00 = hh[k];
  const double h10 = hh[k + 1];
  const double h01 = hh[k + static_cast<size_t>(n)];
  const double h11 = hh[k + static_cast<size_t>(n) + 1];
  return tx + tz <= 1 ? h00 + (h10 - h00) * tx + (h01 - h00) * tz : h11 + (h01 - h11) * (1 - tx) + (h10 - h11) * (1 - tz);
}

}  // namespace fs

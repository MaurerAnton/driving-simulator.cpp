// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/facadedetail.ts @ v1.7.7
#include "render/facadedetail.h"

#include <cmath>
#include <stdexcept>

#include "core/js.h"

namespace fs {

namespace {

/** the squares the details of a tile are cut into, per side */
constexpr int CELLS = 4;

}  // namespace

int flagObservedWalls(std::vector<uint8_t>* a8, const SectionArray* runs) {
  if (!a8 || !runs) return 0;
  const double corners = static_cast<double>(a8->size()) / 4;
  int marked = 0;
  for (size_t r = 0; r + 1 < runs->size(); r += 2) {
    const double end = jsMin(corners, runs->at(r) + runs->at(r + 1));
    for (double i = runs->at(r); i < end; i++) {
      // (a corner before the first one: there is none)
      if (i < 0) continue;
      uint8_t& style = (*a8)[static_cast<size_t>(i) * 4];
      if (style < OBSERVED) {
        style = static_cast<uint8_t>(style + OBSERVED);
        marked++;
      }
    }
  }
  return marked;
}

MaterialPtr makeDetailMaterial() {
  MaterialPtr m = lambert({.vertexColors = true, .side = Side::Double});
  m->polygonOffset = true;
  m->polygonOffsetFactor = -1;
  m->polygonOffsetUnits = -2;
  return m;
}

std::shared_ptr<Group> buildFacadeDetail(UnpackedTile& data, const TileHeader& h, const MaterialPtr& mat) {
  SectionArray* posS = data.get("fdet_pos");
  SectionArray* idxS = data.get("fdet_idx");
  SectionArray* colS = data.get("fdet_a8");
  if (!posS || !idxS || !colS || idxS->size() < 3) return nullptr;
  const std::vector<int16_t>& pos = posS->i16;
  const size_t indices = idxS->size();
  // (an index beyond the list: read as the original reads it, as nothing)
  const auto idx = [&](size_t t) -> size_t { return t < indices ? static_cast<size_t>(idxS->at(t)) : 0; };
  // which square each triangle lies in (by its first corner)
  const double cellSize = h.size / CELLS / POS_XZ_SCALE;
  const double half = CELLS / 2.0;
  const auto cellOf = [&](size_t v) -> size_t {
    // (a corner that is not there: the original stops here too, with an error of the language)
    if (v * 3 + 2 >= pos.size()) throw std::runtime_error("facade details: a triangle names a corner that is not there");
    const double i = jsMax(0, jsMin(CELLS - 1, std::floor(pos[v * 3] / cellSize + half)));
    const double j = jsMax(0, jsMin(CELLS - 1, std::floor(pos[v * 3 + 2] / cellSize + half)));
    return static_cast<size_t>(j * CELLS + i);
  };
  std::vector<uint32_t> count(CELLS * CELLS);
  for (size_t t = 0; t < indices; t += 3) count[cellOf(idx(t))] += 3;
  std::vector<std::vector<uint32_t>> lists(CELLS * CELLS);
  for (size_t c = 0; c < lists.size(); c++) lists[c].reserve(count[c]);
  for (size_t t = 0; t < indices; t += 3) {
    const size_t c = cellOf(idx(t));
    lists[c].push_back(static_cast<uint32_t>(idx(t)));
    lists[c].push_back(static_cast<uint32_t>(idx(t + 1)));
    lists[c].push_back(static_cast<uint32_t>(idx(t + 2)));
  }
  // (the lists are of the kind of array the tile's own list is: 16 bits wide, or 32)
  const bool wide = idxS->type == SectionType::U32;
  const auto indexAttr = [wide](std::vector<uint32_t> list) -> AttrPtr {
    if (wide) return attr(std::move(list), 1);
    return attr(std::vector<uint16_t>(list.begin(), list.end()), 1);
  };
  const AttrPtr position = attr(std::move(posS->i16), 3);
  const AttrPtr color = attr(std::move(colS->u8), 3, true);
  // (the normals once, for all the squares)
  auto all = make<BufferGeometry>();
  all->setAttribute("position", position);
  all->setIndex(wide ? attr(std::move(idxS->u32), 1) : attr(std::move(idxS->u16), 1));
  all->computeVertexNormals();
  const AttrPtr normal = all->getAttribute("normal");
  all->dispose();
  auto group = make<Group>();
  group->name = "fdet";
  for (size_t c = 0; c < lists.size(); c++) {
    std::vector<uint32_t>& list = lists[c];
    if (list.empty()) continue;
    auto g = make<BufferGeometry>();
    g->setAttribute("position", position);
    g->setAttribute("color", color);
    g->setAttribute("normal", normal);
    // (the sphere around this square's corners only – the others share the list of corners)
    Box3 box;
    Vector3 p;
    for (size_t k = 0; k < list.size(); k++) box.expandByPoint(p.fromBufferAttribute(*position, list[k]));
    g->setIndex(indexAttr(std::move(list)));
    g->boundingBox = box;
    Sphere sphere;
    g->boundingSphere = box.getBoundingSphere(sphere);
    auto mesh = make<Mesh>(g, mat);
    mesh->name = "fdet";
    mesh->receiveShadow = true;
    // (the mesh in the tile's frame, the level of detail at the middle of its square)
    const double mx = h.cx + (static_cast<double>(c % CELLS) + 0.5 - half) * (h.size / CELLS);
    const double mz = h.cz + (static_cast<double>(c / CELLS) + 0.5 - half) * (h.size / CELLS);
    mesh->position.set(h.cx - mx, 0, h.cz - mz);
    mesh->scale.set(POS_XZ_SCALE, POS_Y_SCALE, POS_XZ_SCALE);
    mesh->updateMatrix();
    mesh->matrixAutoUpdate = false;
    auto lod = make<LOD>();
    lod->addLevel(mesh, 0);
    lod->addLevel(make<Object3D>(), DETAIL_DISTANCE);
    lod->position.set(mx, h.yBase, mz);
    lod->updateMatrix();
    lod->matrixAutoUpdate = false;
    group->add(lod);
  }
  group->matrixAutoUpdate = false;
  return group;
}

}  // namespace fs

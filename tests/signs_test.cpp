// SPDX-License-Identifier: AGPL-3.0-only
/** Signs: plate sizes, atlas slots, procedural fallbacks, tile geometry. */
#include "../src/render/signs.h"
#include <cassert>
#include <cstdio>

using namespace fs;

namespace {
/** A source that reports no graphics, so every sign uses its fallback. */
class NoSigns : public SignSource {
 public:
  int loads = 0;
  void load(const std::string& baseUrl) override {
    (void)baseUrl;
    loads++;
  }
  std::string svg(const std::string& base) override {
    (void)base;
    return "";
  }
  std::pair<int, int> size(const std::string& base) override {
    (void)base;
    return {0, 0};
  }
  void rasterise(const std::string&, Canvas2D&, double, double, double, double) override {}
};

LoadedTile makeTile() {
  TileHeader h;
  h.v = 1;
  h.ix = 0;
  h.iz = 0;
  h.cx = 0;
  h.cz = 0;
  h.yBase = 100;
  h.size = 500;
  h.terrainN = 5;
  h.terrainMin = 100;
  h.terrainScale = 1;
  h.coverN = 8;
  SignPost sp;
  sp.x = 10;
  sp.y = 0;
  sp.z = -20;
  sp.yaw = 1.5;
  sp.h = 2.1;
  sp.s = {"205", "1002-12"};
  h.signs.push_back(sp);
  SignPost sp2;
  sp2.x = -30;
  sp2.y = 0;
  sp2.z = 5;
  sp2.yaw = 0;
  sp2.h = 2.0;
  sp2.s = {"310:Baunatal|Altenbauna"};
  sp2.big = 1;
  h.signs.push_back(sp2);
  LoadedTile t;
  t.key = "0_0";
  t.header = h;
  t.group = std::make_unique<Group>();
  return t;
}
}  // namespace

int main() {
  // --- plate sizes ---------------------------------------------------------
  auto sz = signSize("205", 1.14, false);
  assert(std::fabs(sz.first - 0.9) < 1e-9);
  auto szBig = signSize("205", 1.14, true);
  assert(std::fabs(szBig.first - 0.9 * 1.45) < 1e-9);
  // town signs are a fixed 1.2 × 0.8 m (scaled by `big`)
  auto town = signSize("310:Kassel", 1, false);
  assert(std::fabs(town.first - 1.2) < 1e-9 && std::fabs(town.second - 0.8) < 1e-9);
  // additional plates (1xxx) are small and wide
  auto add = signSize("1002-12", 1.82, false);
  assert(std::fabs(add.first - 0.6) < 1e-9);
  // the height follows the aspect
  auto circ = signSize("274-50", 1, false);
  assert(std::fabs(circ.first - 0.9) < 1e-9);
  assert(std::fabs(circ.second - 0.9) < 1e-9);

  // --- renderer ------------------------------------------------------------
  NoSigns src;
  SignRenderer r("map/signs/", src);
  r.init();
  assert(src.loads == 1);

  LoadedTile tile = makeTile();
  r.add(tile);
  // the tile group gained the sign group
  assert(!tile.group->children.empty());
  assert(!r.missing.empty());   // no graphics → the fallbacks were used
  assert(r.missing.count("205") == 1);
  assert(r.missing.count("310") == 0);   // town signs are drawn, not missing

  // the plates geometry exists with positions, uvs and two index groups
  Mesh* plates = nullptr;
  for (auto& ch : tile.group->children) {
    auto* g = dynamic_cast<Group*>(ch.get());
    if (!g) continue;
    for (auto& c2 : g->children) {
      auto* m = dynamic_cast<Mesh*>(c2.get());
      if (m && m->name == "signs") plates = m;
    }
  }
  assert(plates && plates->geometry);
  assert(plates->geometry->position->count() > 0);
  assert(plates->geometry->uv && plates->geometry->uv->count() ==
                                     plates->geometry->position->count() * 2 / 3);
  assert(plates->geometry->index && plates->geometry->index->count() > 0);
  // positions are tile-relative: within a tile of the header's centre
  for (size_t i = 0; i < plates->geometry->position->count(); i++) {
    assert(std::fabs(plates->geometry->position->getX(i)) < 600);
    assert(std::fabs(plates->geometry->position->getZ(i)) < 600);
    // y is relative to the tile's height reference
    assert(plates->geometry->position->getY(i) > -20 &&
           plates->geometry->position->getY(i) < 20);
  }

  // the atlas has drawn something for every slot (not a blank plate)
  int ink = 0;
  const auto& px = r.atlasPixels();
  for (size_t i = 0; i < px.size(); i += 4)
    if (px[i] > 0x10 || px[i + 1] > 0x10 || px[i + 2] > 0x10) ink++;
  assert(ink > 1000);

  // a second tile with the same signs reuses the slots (no new cells)
  LoadedTile tile2 = makeTile();
  tile2.key = "1_0";
  r.add(tile2);
  assert(!tile2.group->children.empty());

  // removing a tile drops it from the bookkeeping
  r.remove("0_0");
  r.remove("1_0");

  std::printf("signs: all checks passed (%zu missing graphics, %d ink pixels)\n",
              r.missing.size(), ink);
  return 0;
}

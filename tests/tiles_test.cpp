// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Tile manager: builds a tile from a packed FSB1 container, answers height
 * and cover queries, and streams tiles in and out around the viewer.
 */
#include "../src/render/tiles.h"
#include <cassert>
#include <cstdio>
#include <map>

using namespace fs;

namespace {
/** In-memory tile store: one small tile at (0,0) and one at (1,0). */
class MemSource : public TileSource {
 public:
  std::map<std::string, std::vector<uint8_t>> files;

  bool read(const std::string& key, std::vector<uint8_t>& out) override {
    auto it = files.find(key);
    if (it == files.end()) return false;
    out = it->second;
    return true;
  }
};

/** A tile with a 5×5 terrain grid (stride 4 works: 5-1 = 4). */
std::vector<uint8_t> makeTile(int ix, int iz, double yBase) {
  TileHeader h;
  h.v = 1;
  h.ix = ix;
  h.iz = iz;
  h.cx = ix * 500 + 250;
  h.cz = iz * 500 + 250;
  h.yBase = yBase;
  h.size = 500;
  h.terrainN = 5;
  h.terrainMin = 100;
  h.terrainScale = 0.5;
  h.coverN = 8;
  std::map<std::string, std::vector<double>> secs;
  // a gentle ramp: height rises with the column index
  std::vector<double> hgt;
  for (int i = 0; i < 25; i++) hgt.push_back(i);
  secs["hgt"] = hgt;
  std::vector<double> cov;
  for (int i = 0; i < 64; i++) cov.push_back((i / 8) % 3);   // meadow, farmland, grass
  secs["cov"] = cov;
  // a road quad and a building quad
  secs["ground_pos"] = {0, 0, 0, 10, 0, 0, 10, 0, 10, 0, 0, 10};
  secs["ground_idx"] = {0, 1, 2, 0, 2, 3};
  secs["ground_a8"] = {0, 0, 0, 0};
  secs["bldg_pos"] = {0, 0, 0, 4, 0, 0, 4, 6, 0, 0, 6, 0};
  secs["bldg_idx"] = {0, 1, 2, 0, 2, 3};
  secs["bldg_a8"] = {0, 0, 0, 0};
  secs["bldg_a16"] = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
                      100, 100, 100, 100};
  return packTile(h, secs);
}
}  // namespace

int main() {
  MemSource src;
  src.files["0_0"] = makeTile(0, 0, 100);
  src.files["1_0"] = makeTile(1, 0, 100);

  WorldJson world;
  world.meta.minX = -1000;
  world.meta.maxX = 1000;
  world.meta.minZ = -1000;
  world.meta.maxZ = 1000;
  world.meta.tileSize = 500;
  world.tiles = {"0_0", "1_0"};

  TileManager tm(world, "map/", src);
  assert(tm.settled());

  // asking for the tiles around the origin loads them synchronously
  tm.update(0, 0);
  assert(tm.tiles.count("0_0"));
  assert(tm.settled());
  assert(!tm.tiles.empty());

  const LoadedTile& tile = *tm.tiles.at("0_0");
  assert(tile.header.terrainN == 5);
  assert(tile.heights.size() == 25);
  assert(tile.cover.size() == 64);
  assert(tile.group != nullptr);
  // the group holds the LOD plus the indexed meshes (ground and bldg here)
  assert(tile.group->children.size() >= 3);

  // heightAt: bilinear over the ramp (heights are 100 + i*0.5)
  double h = tm.heightAt(-250 + 125, -250 + 125);   // centre of the tile
  assert(!std::isnan(h));
  assert(h > 100 && h < 120);
  // a point on a node is exact
  double h0 = tm.heightAt(-250, -250);
  assert(std::fabs(h0 - 100) < 1e-9);
  // outside the loaded tiles: NaN (the ground then pretends to continue)
  assert(std::isnan(tm.heightAt(600, 600)));

  // coverAt reads the raster (the first row is meadow = 0)
  assert(tm.coverAt(-250, -250) == COVER_MEADOW);
  assert(tm.coverAt(600, 600) == 0);

  // moving far away unloads the tiles
  tm.update(900, 900);
  assert(tm.tiles.empty());

  // listeners see the load
  MemSource src2;
  src2.files["0_0"] = makeTile(0, 0, 100);
  TileManager tm2(world, "map/", src2);
  int loaded = 0, unloaded = 0;
  tm2.onTile([&](LoadedTile&, bool up) { up ? loaded++ : unloaded++; });
  tm2.update(0, 0);
  assert(loaded == 1);
  tm2.update(900, 900);
  assert(unloaded == 1);

  std::printf("tiles: all checks passed (%d loaded, %d unloaded)\n", loaded, unloaded);
  return 0;
}

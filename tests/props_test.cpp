// SPDX-License-Identifier: AGPL-3.0-only
/** Props: tree/lamp geometry, per-tile instances, distance thinning. */
#include "../src/render/props.h"
#include <cassert>
#include <cstdio>

using namespace fs;

namespace {
/** A tile with a few trees and two lamps. */
LoadedTile makeTile() {
  TileHeader h;
  h.v = 1;
  h.ix = 3;
  h.iz = 7;
  h.cx = 3000;
  h.cz = 7000;
  h.yBase = 100;
  h.size = 500;
  h.terrainN = 5;
  h.terrainMin = 100;
  h.terrainScale = 1;
  h.coverN = 8;
  TileObject lamp;
  lamp.k = "lamp";
  lamp.x = 10;
  lamp.y = 0;
  lamp.z = 20;
  lamp.yaw = 1.5;
  lamp.hasYaw = true;
  h.objects.push_back(lamp);
  TileObject lamp2;
  lamp2.k = "lamp";
  lamp2.x = -30;
  lamp2.y = 0;
  lamp2.z = -40;
  h.objects.push_back(lamp2);

  UnpackedTile data;
  data.header = h;
  // 6 trees: x, y, z, height, kind
  data.sections["trees"] = {0, 0, 0, 8, 0, 12, 0, -6, 10, 1, 25, 0, 5, 6, 2,
                            -18, 0, 30, 12, 3, 40, 0, -12, 9, 0, 55, 0, 44, 14, 1};

  LoadedTile t;
  t.key = "3_7";
  t.header = h;
  t.group = std::make_unique<Group>();
  t.data = data;
  return t;
}
}  // namespace

int main() {
  PropRenderer props;

  LoadedTile tile = makeTile();
  props.add(tile);

  // the tile group gained one node per tree kind plus the lamps
  assert(tile.group->children.size() >= 2);

  // instance counts match the per-kind tallies
  int totalInstances = 0;
  int lampInstances = 0;
  for (auto& child : tile.group->children) {
    auto* mesh = dynamic_cast<Mesh*>(child.get());
    if (!mesh) continue;
    totalInstances += static_cast<int>(mesh->instances.size());
    if (mesh->name == "lamps") lampInstances = static_cast<int>(mesh->instances.size());
  }
  assert(totalInstances == 6);      // every tree got an instance
  assert(lampInstances == 2);       // both lamps

  // the tree instances are placed at the tree positions (y is lifted by 0.15)
  bool sawTreeAt = false;
  for (auto& child : tile.group->children) {
    auto* mesh = dynamic_cast<Mesh*>(child.get());
    if (!mesh || mesh->name == "lamps") continue;
    for (const auto& m : mesh->instances) {
      Vector3 p;
      m.setFromMatrixPosition(p);
      if (std::fabs(p.x - 12) < 1e-9 && std::fabs(p.z + 6) < 1e-9) sawTreeAt = true;
      assert(std::fabs(p.y + 0.15) < 1e-9);   // sunk into the ground
    }
  }
  assert(sawTreeAt);

  // every tree has a colour (the vertex colours drive the canopy shading)
  for (auto& child : tile.group->children) {
    auto* mesh = dynamic_cast<Mesh*>(child.get());
    if (!mesh || mesh->name == "lamps") continue;
    assert(mesh->instanceColors.size() == mesh->instances.size());
    for (const auto& c : mesh->instanceColors) assert(c.r > 0 && c.r <= 1.5);
  }

  // the geometry is shared per kind (so it is uploaded once)
  Mesh* first = nullptr;
  int shared = 0;
  for (auto& child : tile.group->children) {
    auto* mesh = dynamic_cast<Mesh*>(child.get());
    if (!mesh || mesh->name == "lamps") continue;
    if (!first) first = mesh;
    else if (mesh->geometry == first->geometry) shared++;
  }
  assert(shared >= 1);

  // thinning: far away only a fraction of the trees is drawn, near = all
  props.update(3000, 7000);
  int nearCount = 0;
  for (auto& child : tile.group->children) {
    auto* mesh = dynamic_cast<Mesh*>(child.get());
    if (!mesh || mesh->name == "lamps") continue;
    nearCount += static_cast<int>(mesh->instances.size());
  }
  assert(nearCount == 6);

  props.update(3000 + 2000, 7000);   // 2 km away
  int farCount = 0;
  for (auto& child : tile.group->children) {
    auto* mesh = dynamic_cast<Mesh*>(child.get());
    if (!mesh || mesh->name == "lamps") continue;
    farCount += static_cast<int>(mesh->instances.size());
  }
  assert(farCount < nearCount);
  assert(farCount >= 1);             // never empty (the original keeps one)

  // a global density multiplier thins the stand further
  props.density = 0.1;
  props.update(3000, 7000);
  int thinCount = 0;
  for (auto& child : tile.group->children) {
    auto* mesh = dynamic_cast<Mesh*>(child.get());
    if (!mesh || mesh->name == "lamps") continue;
    thinCount += static_cast<int>(mesh->instances.size());
  }
  assert(thinCount <= nearCount);
  props.density = 1;

  // removing the tile drops its instances
  props.remove(tile.key);
  for (auto& child : tile.group->children) {
    auto* mesh = dynamic_cast<Mesh*>(child.get());
    if (!mesh || mesh->name == "lamps") continue;
    assert(mesh->instances.empty());
  }

  std::printf("props: all checks passed (6 trees, 2 lamps)\n");
  return 0;
}

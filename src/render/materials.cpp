// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/materials.ts @ v1.7.7
#include "render/materials.h"

#include "render/glsl_gen.h"

namespace fs {

MaterialPtr makeGroundMaterial(bool paint) {
  MaterialPtr m = lambert({.color = Color(0xffffff)});
  if (paint) {
    // paint lies ~2 cm above the road; bias it so it never z-fights at distance
    m->polygonOffset = true;
    m->polygonOffsetFactor = -2;
    m->polygonOffsetUnits = -4;
  }
  m->patch = gamePatch(paint ? "paint" : "ground");
  m->name = paint ? "paint" : "ground";
  return m;
}

MaterialPtr makeTerrainMaterial(const TexturePtr& cover) {
  MaterialPtr m = lambert({.color = Color(0xffffff), .map = cover});
  m->patch = gamePatch("terrain");
  m->name = "terrain";
  return m;
}

MaterialPtr makeStructMaterial() {
  MaterialPtr m = lambert({.vertexColors = true});
  m->name = "struct";
  return m;
}

MaterialPtr makeBuildingMaterial(bool plain) {
  MaterialPtr m = lambert({.color = Color(0xffffff)});
  // Plain buildings (graphics setting): walls and roofs in their colours, without the windows, doors
  // and roof tiles the facade shader works out for every pixel – much less work for the graphics chip.
  m->patch = gamePatch(plain ? "buildingPlain" : "building");
  m->name = plain ? "building (plain)" : "building";
  return m;
}

}  // namespace fs

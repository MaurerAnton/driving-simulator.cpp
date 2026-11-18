// SPDX-License-Identifier: AGPL-3.0-only
/** Material shader sources: the procedural GLSL must be present verbatim. */
#include "../src/engine/materials.h"
#include <cassert>
#include <cstdio>

using namespace fs;

int main() {
  // the shared noise/ground/facade GLSL is carried over from materials.ts
  std::string noise = NOISE_GLSL;
  assert(noise.find("float fsHash(vec2 p)") != std::string::npos);
  assert(noise.find("float fsNoise(vec2 p)") != std::string::npos);
  assert(noise.find("vec3 fsLin(vec3 c)") != std::string::npos);

  std::string ground = GROUND_GLSL;
  assert(ground.find("vec3 fsGround(float m, vec3 wp, float dist)") != std::string::npos);
  // every surface branch from the original is present
  for (const char* marker : {"m < 0.5", "m < 1.5", "m < 2.5", "m < 3.5", "m < 4.5",
                             "m < 5.5", "m < 6.5", "m < 7.5", "m < 8.5", "m < 9.5",
                             "m < 10.5", "m < 11.5", "m < 12.5", "m < 13.5",
                             "m < 20.5", "m < 21.5", "m < 22.5"})
    assert(ground.find(marker) != std::string::npos);

  std::string facade = FACADE_GLSL;
  assert(facade.find("vec3 fsFacade(float style") != std::string::npos);
  assert(facade.find("roofs: 8 tiles, 9 flat, 10 metal") != std::string::npos);
  assert(facade.find("garage: one sectional door") != std::string::npos);

  // assembled shaders: every kind produces a main() and the right varyings
  for (MaterialKind k : {MaterialKind::Struct, MaterialKind::Ground, MaterialKind::Paint,
                         MaterialKind::Terrain, MaterialKind::Building, MaterialKind::Sky}) {
    std::string vs = vertexShaderFor(k);
    std::string fs = fragmentShaderFor(k);
    assert(vs.find("void main()") != std::string::npos);
    assert(fs.find("void main()") != std::string::npos);
    assert(vs.find("gl_Position") != std::string::npos);
    assert(fs.find("gl_FragColor") != std::string::npos);
    if (k != MaterialKind::Sky) {
      assert(vs.find("vFsWorld") != std::string::npos);
      assert(fs.find("vFsWorld") != std::string::npos);
      assert(fs.find("fogColor") != std::string::npos);
    }
  }

  // ground carries the material id, buildings carry style/uvd
  assert(vertexShaderFor(MaterialKind::Ground).find("attribute float amat") != std::string::npos);
  assert(fragmentShaderFor(MaterialKind::Ground).find("fsGround(vFsMat") != std::string::npos);
  assert(vertexShaderFor(MaterialKind::Building).find("attribute vec4 astyle") != std::string::npos);
  assert(fragmentShaderFor(MaterialKind::Building).find("fsFacade(vFsStyle.x") != std::string::npos);
  assert(fragmentShaderFor(MaterialKind::Sky).find("uSunDir") != std::string::npos);
  assert(fragmentShaderFor(MaterialKind::Sky).find("gl_FragColor") != std::string::npos);

  // program cache keys match the original customProgramCacheKey values
  assert(programCacheKey(MaterialKind::Ground) == "fs-ground");
  assert(programCacheKey(MaterialKind::Paint) == "fs-paint");
  assert(programCacheKey(MaterialKind::Terrain) == "fs-terrain");
  assert(programCacheKey(MaterialKind::Building) == "fs-bldg");

  std::printf("materials: all checks passed\n");
  return 0;
}

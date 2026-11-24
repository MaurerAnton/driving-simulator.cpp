// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The shader of a material, put together the way three.js puts it together
 * (renderers/webgl/WebGLProgram.js): three.js's own GLSL (engine/three_glsl.cpp),
 * the defines that say what this material and this frame need, the material's
 * patch (the original's onBeforeCompile), the includes resolved, the light loops
 * unrolled.  The picture is three.js's; only the program that draws it is ours.
 */
#pragma once

#include <string>

#include "engine/engine.h"

namespace fs {

/** Everything that decides which program a draw needs (three.js: the program's parameters). */
struct ShaderParams {
  MaterialType type = MaterialType::Lambert;
  bool map = false;
  bool vertexColors = false;
  bool instancing = false;
  bool instancingColor = false;
  bool fog = false;
  /** the object receives shadows and a light casts them */
  bool shadowMap = false;
  bool doubleSided = false;
  bool flipSided = false;
  bool flatShading = false;
  bool alphaTest = false;
  /** not transparent: the alpha written is 1 */
  bool opaque = true;
  /** ACES filmic tone mapping (the screen, a material with toneMapped) */
  bool toneMapping = false;
  /** the output is sRGB (the screen); false: linear numbers (a render target) */
  bool srgbOutput = true;
  /** the geometry has normals */
  bool vertexNormals = true;
  bool sizeAttenuation = false;
  int numDirLights = 0;
  int numHemiLights = 0;
  int numSpotLights = 0;
  int numPointLights = 0;
  int numDirLightShadows = 0;
  /** the material's patch (may be null) */
  const ShaderPatch* patch = nullptr;
  /** a ShaderMaterial: its own shaders and defines (else null) */
  const Material* custom = nullptr;
};

/** A text that is the same for two parameter sets exactly when they need the same program. */
std::string shaderKey(const ShaderParams& p);

/** The two shaders, ready for glShaderSource. */
void buildShader(const ShaderParams& p, std::string& vertex, std::string& fragment);

/** The shadow map's own small shaders (`map`: discard where the picture is see-through). */
void buildDepthShader(bool instancing, bool alphaTestedMap, std::string& vertex, std::string& fragment);

/**
 * The attribute locations every program is linked with: 'position' 0, 'normal' 1,
 * 'uv' 2, 'color' 3, 'instanceMatrix' 4…7, 'instanceColor' 8, and the game's own
 * attributes ('amat', 'astyle' …) from 9 on in the order they are first seen.
 * Returns -1 when there is no location left for a new name.
 */
int attributeLocation(const std::string& name);
/** the names that have a location, for glBindAttribLocation */
const std::vector<std::pair<std::string, int>>& attributeLocations();

namespace shaderText {
/** `#include <chunk>` lines replaced by the chunks (and theirs) */
std::string resolveIncludes(const std::string& source);
/** `#pragma unroll_loop_start … #pragma unroll_loop_end` written out */
std::string unrollLoops(const std::string& source);
/** replace every occurrence of a whole word */
std::string replaceWord(const std::string& source, const std::string& word, const std::string& by);
}  // namespace shaderText

}  // namespace fs

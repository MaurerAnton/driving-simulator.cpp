// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Materials.  All surface detail is procedural (computed from world position
 * in the fragment shader), so the game ships without a single texture file.
 *
 * The GLSL sources below are the original shaders, carried over verbatim:
 * the WebGL2 backend compiles them as-is, and the onBeforeCompile hooks the
 * TypeScript materials use are reproduced here as the shader text itself.
 *
 * Origin: src/render/materials.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).
 */
#pragma once

#include <string>

namespace fs {

// ---------------------------------------------------------------------------
// Shader sources (verbatim from materials.ts)
// ---------------------------------------------------------------------------

extern const char* const NOISE_GLSL;
extern const char* const GROUND_GLSL;
extern const char* const FACADE_GLSL;

/** Common vertex/fragment plumbing: world position varying + optional flat material id. */
extern const char* const WORLDPOS_VS_HEAD;
extern const char* const WORLDPOS_VS_BODY;
extern const char* const WORLDPOS_FS_HEAD;

// ---------------------------------------------------------------------------
// Material descriptions the WebGL2 backend turns into programs
// ---------------------------------------------------------------------------

enum class MaterialKind {
  /** plain vertex colour, lambert (bridges, walls, rails) */
  Struct,
  /** ground: per-vertex material id → procedural surface colour */
  Ground,
  /** ground with polygon offset (paint lying ~2 cm above the road) */
  Paint,
  /** terrain: ground-cover texture + procedural detail */
  Terrain,
  /** buildings: procedural windows, doors and roof tiles */
  Building,
  /** sky dome (custom shader, no lighting) */
  Sky,
};

/** Build the vertex shader source for a material kind. */
std::string vertexShaderFor(MaterialKind kind);

/** Build the fragment shader source for a material kind. */
std::string fragmentShaderFor(MaterialKind kind);

/** Stable program cache key, as the JS customProgramCacheKey does. */
std::string programCacheKey(MaterialKind kind);

}  // namespace fs

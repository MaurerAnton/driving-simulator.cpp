// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Materials.  All surface detail is procedural (computed from world position
 * in the fragment shader), so the game ships without a single texture file.
 *
 * Origin: src/render/materials.ts @ v1.7.7.  The shaders themselves are the
 * original's, captured from it (src/render/glsl_gen.cpp).
 */
#pragma once

#include "engine/engine.h"

namespace fs {

/** Roads, sidewalks, kerbs, verges: colour chosen per vertex material id. */
MaterialPtr makeGroundMaterial(bool paint);
/** Terrain: ground-cover colour texture modulated by procedural detail. */
MaterialPtr makeTerrainMaterial(const TexturePtr& cover);
/** Bridges, walls, rails: plain vertex colours. */
MaterialPtr makeStructMaterial();
/** Buildings: procedural windows, doors and roof tiles from wall-space coordinates. */
MaterialPtr makeBuildingMaterial(bool plain = false);

}  // namespace fs

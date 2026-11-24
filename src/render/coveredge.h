// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Smooth edges of the ground cover: how the edge byte of the map becomes the
 * cover texture's alpha (the terrain's shader reads it back – its GLSL is part of
 * the terrain material, src/render/glsl_gen.cpp).
 *
 * Origin: src/render/coveredge.ts @ v1.7.7
 */
#pragma once

#include <algorithm>

namespace fs {

/** the cover texture's alpha for a stored edge byte (1 + tenths of a metre, 1 … 31; 0: none) */
inline int coverEdgeAlpha(int b) { return b ? std::min(255, b * 8) : 0; }

/** The distance (m) an alpha of the cover texture stands for (as the shader reads it: alpha / 255 = a). */
inline double coverEdgeMetres(double a) { return ((a * 255) / 8 - 1) / 10; }

}  // namespace fs

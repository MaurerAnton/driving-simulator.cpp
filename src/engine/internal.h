// SPDX-License-Identifier: AGPL-3.0-only
/** What the engine's own sources share (not part of the interface in engine.h). */
#pragma once

#include <cstdint>

#include "engine/engine.h"

namespace fs::enginedetail {

/** a number that names no vertex of any attribute (and stays none when it is multiplied by an item size) */
inline constexpr size_t NO_VERTEX = SIZE_MAX / 64;

/**
 * A number read from an index as a vertex number.  What is none – read beyond the index's end, negative,
 * not a number – names no vertex: reading that vertex gives NaN and writing it does nothing, which is
 * what JavaScript makes of `array[undefined]`.
 */
inline size_t vertexNumber(double v) { return v >= 0 && v < 4503599627370496.0 ? static_cast<size_t>(v) : NO_VERTEX; }
inline size_t vertexAt(const BufferAttribute& index, size_t i) { return vertexNumber(index.getX(i)); }

}  // namespace fs::enginedetail

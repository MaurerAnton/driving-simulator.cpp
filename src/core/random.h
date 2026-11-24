// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Math.random() of the original: one stream for the whole program.  It can be
 * seeded, so that a test – and a comparison with the original, whose Math.random
 * is replaced by the same generator – sees the same draws.
 */
#pragma once

#include <cstdint>

namespace fs {

/** the next draw, 0 ≤ x < 1 (mulberry32, as core/geom.h's Rng) */
double rnd();
/** start the stream anew */
void rndSeed(uint32_t seed);

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
#include "core/random.h"

#include "core/geom.h"

namespace fs {

static Rng& stream() {
  static Rng r(0x9e3779b9u);
  return r;
}

double rnd() { return stream()(); }
void rndSeed(uint32_t seed) { stream() = Rng(seed); }

}  // namespace fs

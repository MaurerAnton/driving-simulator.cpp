// SPDX-License-Identifier: AGPL-3.0-only
// LINK: world core platform geo
// Origin: tests/world/far.test.ts @ v1.7.7
/**
 * The far view's grid (far.bin, tools/build-far.ts): read back, the height
 * between its points, and what kind of place a point lies in by the
 * buildings around it.
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "world/far.h"

using namespace fs;

namespace {

void setInt32(std::vector<uint8_t>& b, size_t o, int32_t v) {
  for (int i = 0; i < 4; i++) b[o + static_cast<size_t>(i)] = static_cast<uint8_t>((static_cast<uint32_t>(v) >> (8 * i)) & 255);
}
void setFloat32(std::vector<uint8_t>& b, size_t o, float v) {
  uint32_t bits;
  std::memcpy(&bits, &v, 4);
  setInt32(b, o, static_cast<int32_t>(bits));
}
void setUint16(std::vector<uint8_t>& b, size_t o, int v) {
  b[o] = static_cast<uint8_t>(v & 255);
  b[o + 1] = static_cast<uint8_t>((v >> 8) & 255);
}

/** A grid of 4 × 3 points, 100 m apart, rising 1 m to the east; buildings of a given height in its middle. */
std::vector<uint8_t> grid(int tall, char version = '2') {
  const size_t w = 4;
  const size_t h = 3;
  std::vector<uint8_t> b(32 + w * h * 4, 0);
  b[0] = 0x46;
  b[1] = 0x41;
  b[2] = 0x52;
  b[3] = static_cast<uint8_t>(version);
  setInt32(b, 4, 0);
  setInt32(b, 8, 0);
  setInt32(b, 12, 100);
  setInt32(b, 16, static_cast<int32_t>(w));
  setInt32(b, 20, static_cast<int32_t>(h));
  setFloat32(b, 24, 200);
  setFloat32(b, 28, 0.5f);
  for (size_t j = 0; j < h; j++)
    for (size_t i = 0; i < w; i++) setUint16(b, 32 + (j * w + i) * 2, static_cast<int>(i * 2));
  // a point with no ground known
  setUint16(b, 32 + (2 * w + 3) * 2, FAR_NONE);
  for (size_t k = 0; k < w * h; k++) b[32 + w * h * 2 + k] = 3;
  for (size_t k = 0; k < w * h; k++) b[32 + w * h * 3 + k] = static_cast<uint8_t>(tall * 2);
  return b;
}

}  // namespace

int main() {
  // the far grid: read back, heights between the points, nothing where none is known
  {
    const std::optional<FarGround> f = parseFar(grid(4));
    assert(f);
    assert(f->w == 4);
    assert(f->h == 3);
    assert(f->cover[5] == 3);
    assert(std::fabs(farHeight(*f, 0, 0) - 200) < 1e-6);
    assert(std::fabs(farHeight(*f, 150, 50) - 201.5) < 1e-6);  // between the points
    assert(std::isnan(farHeight(*f, 250, 150)));               // beside a point without ground
    assert(std::isnan(farHeight(*f, -10, 0)));                 // outside the grid
    assert(f->minX == 0 && f->minZ == 0 && f->cell == 100 && f->base == 200 && f->step == 0.5 && f->heights.size() == 12 && f->heights[3] == 6 && f->heights[11] == FAR_NONE && f->buildings && f->buildings->size() == 12);
  }
  // the kind of place by its buildings: small houses, rows and blocks, tall ones
  {
    assert(farBuilt(*parseFar(grid(4)), 100, 100) == 1);
    assert(farBuilt(*parseFar(grid(7)), 100, 100) == 2);
    assert(farBuilt(*parseFar(grid(14)), 100, 100) == 3);
    assert(farBuilt(*parseFar(grid(0)), 100, 100) == 0);  // no buildings: the open country
    assert(farBuilt(*parseFar(grid(4)), -80, 100) == 0 && farBuilt(*parseFar(grid(4)), 100, 1000) == 0);  // beyond the grid
  }
  // what is not a far grid; one of the first kind, without buildings
  {
    assert(!parseFar(std::vector<uint8_t>(10, 0)));
    std::vector<uint8_t> other = grid(4);
    other[0] = 'X';
    assert(!parseFar(other));
    std::vector<uint8_t> cut = grid(4);
    cut.resize(cut.size() - 1);
    assert(!parseFar(cut));
    std::vector<uint8_t> first = grid(4, '1');
    first.resize(32 + 4 * 3 * 3);
    const std::optional<FarGround> f = parseFar(first);
    assert(f && !f->buildings && farBuilt(*f, 100, 100) == -1 && std::fabs(farHeight(*f, 150, 50) - 201.5) < 1e-6);
  }
  std::printf("far: all checks passed\n");
  return 0;
}

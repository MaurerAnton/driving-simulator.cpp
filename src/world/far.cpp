// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/far.ts @ v1.7.7
#include "world/far.h"

#include <cmath>
#include <cstring>

#include "core/js.h"

namespace fs {

namespace {
int32_t int32At(const std::vector<uint8_t>& b, size_t at) {
  return static_cast<int32_t>(static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8) | (static_cast<uint32_t>(b[at + 2]) << 16) |
                              (static_cast<uint32_t>(b[at + 3]) << 24));
}
double float32At(const std::vector<uint8_t>& b, size_t at) {
  const uint32_t bits = static_cast<uint32_t>(int32At(b, at));
  float f;
  std::memcpy(&f, &bits, 4);
  return static_cast<double>(f);
}
}  // namespace

std::optional<FarGround> parseFar(const std::vector<uint8_t>& b) {
  if (b.size() < 32 || b[0] != 0x46 || b[1] != 0x41 || b[2] != 0x52 || (b[3] != 0x31 && b[3] != 0x32)) return std::nullopt;
  const bool withBuildings = b[3] == 0x32;
  FarGround f;
  f.minX = int32At(b, 4);
  f.minZ = int32At(b, 8);
  f.cell = int32At(b, 12);
  f.w = int32At(b, 16);
  f.h = int32At(b, 20);
  f.base = float32At(b, 24);
  f.step = float32At(b, 28);
  const double n = static_cast<double>(f.w) * static_cast<double>(f.h);
  if (static_cast<double>(b.size()) < 32 + n * (withBuildings ? 4 : 3)) return std::nullopt;
  // (a width or a height below zero: the original slices nothing out of the file then)
  const size_t count = n > 0 ? static_cast<size_t>(n) : 0;
  // (the heights start at an even offset: 32)
  f.heights.resize(count);
  for (size_t i = 0; i < count; i++) f.heights[i] = static_cast<uint16_t>(b[32 + i * 2] | (b[32 + i * 2 + 1] << 8));
  f.cover.assign(b.begin() + 32 + static_cast<long>(count * 2), b.begin() + 32 + static_cast<long>(count * 3));
  if (withBuildings) f.buildings = std::vector<uint8_t>(b.begin() + 32 + static_cast<long>(count * 3), b.begin() + 32 + static_cast<long>(count * 4));
  return f;
}

int farBuilt(const FarGround& f, double x, double z) {
  if (!f.buildings) return -1;
  const std::vector<uint8_t>& buildings = *f.buildings;
  const double i = jsRound((x - f.minX) / f.cell);
  const double j = jsRound((z - f.minZ) / f.cell);
  if (i < 0 || j < 0 || i >= f.w || j >= f.h) return 0;
  // (a place that is no number: the original reads nine cells that are not there, and none of them is 0)
  if (i != i || j != j) return 3;
  // (the cell and its neighbours: a street runs between the houses)
  int n = 0;
  double sum = 0;
  for (int dj = -1; dj <= 1; dj++) {
    for (int di = -1; di <= 1; di++) {
      const int ii = static_cast<int>(i) + di;
      const int jj = static_cast<int>(j) + dj;
      if (ii < 0 || jj < 0 || ii >= f.w || jj >= f.h) continue;
      const int v = buildings[static_cast<size_t>(jj) * static_cast<size_t>(f.w) + static_cast<size_t>(ii)];
      if (v == 0) continue;
      n++;
      sum += v / 2.0;
    }
  }
  if (n < 2) return 0;
  const double tall = sum / n;
  return tall < 5.5 ? 1 : tall < 9 ? 2 : 3;
}

double farHeight(const FarGround& f, double x, double z) {
  const double fx = (x - f.minX) / f.cell;
  const double fz = (z - f.minZ) / f.cell;
  const double i = std::floor(fx);
  const double j = std::floor(fz);
  if (!(i >= 0 && j >= 0 && i < f.w - 1 && j < f.h - 1)) return JS_NAN;
  const size_t k = static_cast<size_t>(j) * static_cast<size_t>(f.w) + static_cast<size_t>(i);
  const int a = f.heights[k];
  const int b = f.heights[k + 1];
  const int c = f.heights[k + static_cast<size_t>(f.w)];
  const int d = f.heights[k + static_cast<size_t>(f.w) + 1];
  if (a == FAR_NONE || b == FAR_NONE || c == FAR_NONE || d == FAR_NONE) return JS_NAN;
  const double tx = fx - i;
  const double tz = fz - j;
  const double v = (a * (1 - tx) + b * tx) * (1 - tz) + (c * (1 - tx) + d * tx) * tz;
  return f.base + v * f.step;
}

}  // namespace fs

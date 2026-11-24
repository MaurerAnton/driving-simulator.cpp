// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/far.ts @ v1.7.7
/**
 * The far view of the region (far.bin.gz, made by tools/build-far.ts): its
 * ground on a coarse grid – a point every 100 m with its height and what
 * covers it – for what lies beyond the tiles around the camera.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace fs {

/** metres between the points */
inline constexpr double FAR_CELL = 100;
/** the height of a point where no ground is known */
inline constexpr int FAR_NONE = 65535;

struct FarGround {
  int minX = 0;
  int minZ = 0;
  int cell = 0;
  int w = 0;
  int h = 0;
  /** metres: base + v · step, FAR_NONE where none */
  double base = 0;
  double step = 0;
  std::vector<uint16_t> heights;
  std::vector<uint8_t> cover;
  /** how tall the buildings around each point are (half metres; 0: none there) – null in a file without them */
  std::optional<std::vector<uint8_t>> buildings;
};

/** Read far.bin (already inflated); null if it is not one. */
std::optional<FarGround> parseFar(const std::vector<uint8_t>& buf);

/**
 * What kind of place a point lies in, by the buildings around it: 0 no
 * buildings (the open country), 1 small houses (up to two storeys), 2 rows
 * of houses and blocks of flats, 3 tall buildings (the middle of a town) –
 * -1 if the file does not say.
 */
int farBuilt(const FarGround& f, double x, double z);

/** The far ground's height at a place (bilinear between its points), NaN outside it. */
double farHeight(const FarGround& f, double x, double z);

}  // namespace fs

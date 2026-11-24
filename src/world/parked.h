// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/parked.ts @ v1.7.7
/**
 * Cars parked along the streets: where they stand, as the map build places
 * them (tools/mapgen/parking.ts) and the tiles carry them (sections
 * `parked` and `parkedK`), for the game to draw (render/parked.ts).  They do
 * not move; their outlines are in the tile's collision walls.
 */
#pragma once

#include <cstdint>
#include <vector>

namespace fs {

/** One kind of parked car: length, width, height (m). */
struct ParkedModel {
  const char* name;
  double len, wid, hgt;
};

/** The kinds of parked car. */
inline constexpr ParkedModel PARKED_MODELS[] = {
    {"small", 3.9, 1.72, 1.48},
    {"saloon", 4.55, 1.8, 1.45},
    {"estate", 4.75, 1.82, 1.5},
    {"van", 4.95, 1.95, 1.95},
};
inline constexpr int PARKED_MODEL_COUNT = 4;

/** Their colours (sRGB), white, black and silver the most. */
inline constexpr uint32_t PARKED_COLOURS[] = {0xe9e9e6, 0xeeeeea, 0x1c1d20, 0x26272b, 0x9ea2a6, 0xb6b9bc, 0x6c7075, 0x24375e, 0x7a1d1d, 0x2f4b37, 0x8c7b62, 0x3b5f8a};
inline constexpr int PARKED_COLOUR_COUNT = 12;

/** One parked car.  `yaw` as everywhere: 0 facing north, positive turning left. */
struct ParkedCar {
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  int model = 0;
  int colour = 0;
};

struct PackedParked {
  std::vector<int16_t> parked;
  std::vector<uint8_t> parkedK;
};

/** The cars of a tile as its two sections (positions relative to the tile centre and height base). */
PackedParked packParked(const std::vector<ParkedCar>& cars, double cx, double cz, double yBase);

/** The cars back from a tile's sections (none: an empty list). */
std::vector<ParkedCar> unpackParked(const std::vector<int16_t>* parked, const std::vector<uint8_t>* parkedK, double cx, double cz, double yBase);

/** The four corners of a car's outline on the ground (flat x, z), shrunk by `inset` all round. */
std::vector<double> parkedOutline(const ParkedCar& c, double inset = 0);

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Ground-cover classes of the terrain raster (shared by pipeline and renderer).
 *
 * Origin: src/world/cover.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#include "world/cover.h"

namespace fs {

const uint32_t COVER_COLORS[18] = {
    0x7d9a4e,  // meadow
    0xa9a35c,  // farmland
    0x6f9a48,  // mown grass / parks
    0x3f6534,  // forest floor
    0x5f7f3f,  // scrub
    0x7a9457,  // residential gardens
    0x9a9a92,  // commercial yards
    0x8f8d88,  // industrial yards
    0x9a8a6a,  // farmyard
    0x7c9550,  // allotments
    0x86a052,  // orchard
    0x6f8f55,  // cemetery
    0x77706a,  // railway ballast
    0x9c8b70,  // bare earth
    0x5f9a55,  // sports pitch
    0x55565a,  // asphalt (car parks)
    0xa39f97,  // paved squares
    0x4d7089,  // water
};

const char* const COVER_NAMES[18] = {
    "meadow", "farmland", "grass", "forest", "scrub", "residential",
    "commercial", "industrial", "farmyard", "allotment", "orchard",
    "cemetery", "railway", "bare", "pitch", "asphalt", "paved", "water",
};

}  // namespace fs

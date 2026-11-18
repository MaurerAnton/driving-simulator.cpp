// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Ground-cover classes of the terrain raster (shared by pipeline and renderer).
 *
 * Origin: src/world/cover.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fs {

enum Cover : int {
  COVER_MEADOW = 0,
  COVER_FARMLAND = 1,
  COVER_GRASS = 2,
  COVER_FOREST = 3,
  COVER_SCRUB = 4,
  COVER_RESIDENTIAL = 5,
  COVER_COMMERCIAL = 6,
  COVER_INDUSTRIAL = 7,
  COVER_FARMYARD = 8,
  COVER_ALLOTMENT = 9,
  COVER_ORCHARD = 10,
  COVER_CEMETERY = 11,
  COVER_RAILWAY = 12,
  COVER_BARE = 13,
  COVER_PITCH = 14,
  COVER_ASPHALT = 15,
  COVER_PAVED = 16,
  COVER_WATER = 17,
};

/** Base colours (sRGB hex) per cover class – the renderer adds detail noise. */
extern const uint32_t COVER_COLORS[18];
extern const char* const COVER_NAMES[18];

}  // namespace fs

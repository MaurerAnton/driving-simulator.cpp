// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/geolocate.ts @ v1.7.7
/**
 * "My location": where the player is now, asked of the browser (which asks
 * the player first).  The position stays in this browser – nothing is sent
 * anywhere; it only says where on the map to start, to go, or to drive to.
 */
#pragma once

#include <string>

#include "core/task.h"
#include "platform/geolocation.h"

namespace fs {

/** `{ ok: true, x, z, accuracy } | { ok: false, why }` */
struct LocateResult {
  bool ok = false;
  double x = 0;
  double z = 0;
  double accuracy = 0;
  /** 'unsupported' | 'denied' | 'unavailable' | 'outside' ('' when ok) */
  std::string why;
};

/** The bounds of the map (world.json meta). */
struct MapBounds {
  double minX = 0;
  double maxX = 0;
  double minZ = 0;
  double maxZ = 0;
};

/** Is a world point on the map (with a margin)? */
bool onMap(double x, double z, const MapBounds& b, double margin = 0);

/** Where the player is, on the map – or why not.  (`geo`: navigator.geolocation; null: the browser has none) */
Task<LocateResult> locate(MapBounds bounds, const plat::Geolocation* geo = plat::geolocation());

}  // namespace fs

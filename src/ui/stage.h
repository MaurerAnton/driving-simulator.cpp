// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/stage.ts @ v1.7.7 – of it only what the menu's map needs: tramKey, runwayKey, startFromSpot.
// (The menu as a stage itself is not in the port yet: docs/STATUS.md.)
#pragma once

#include <string>
#include <vector>

#include "game/settings.h"
#include "game/vehicles.h"
#include "ui/hud.h"

namespace fs {

inline std::string tramKey(const TramStopChoice& t) { return t.name + "|" + t.town; }
inline std::string runwayKey(const RunwayChoice& r) { return r.field + "|" + r.end; }

/**
 * A place tapped on the map opened from the menu becomes the start of the next drive: the nearest tram stop for
 * the tram, the nearest end of a runway for the aeroplane, else the place itself as an address (on the road
 * nearest to it).  Changes `last`; false: nothing fits.
 */
bool startFromSpot(const VehicleKind& kind, Settings::LastStart& last, const SpawnJson& spot, const std::vector<TramStopChoice>* tramStops,
                   const std::vector<RunwayChoice>* runways, const std::string& unnamed);

}  // namespace fs

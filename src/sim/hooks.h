// SPDX-License-Identifier: AGPL-3.0-only
/**
 * HOOKS – what the simulation asks of the parts of the original that are not translated yet.
 *
 * The traffic, the rule monitor and the driver of the autopilot call into the people on foot,
 * the cyclists, the emergency vehicles, the roadworks, the kerb build-outs, the level
 * crossings, the wild animals and the signs with a time on them.  None of
 * those is here yet.  Each stands in this file as a class of the original's name with just
 * the members that are called – all of them for the missing part to fill in – and whoever holds
 * one holds a pointer that is null until the part exists:
 *
 *     RoadEvents* roadEvents = nullptr;   …   if (roadEvents && roadEvents->blocked(lane, s)) …
 *
 * When a part is translated, its declaration here goes and the real class takes its place
 * (the same name, the same members): nothing that calls it changes.  A test can hang a small
 * class of its own on a hook in the meantime.
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/geom.h"
#include "core/js.h"
#include "sim/insertion_map.h"
#include "sim/pedestrians.h"
#include "sim/playerstate.h"
#include "world/buslines.h"
#include "world/lanes.h"
#include "world/stops.h"
#include "world/types.h"
#include "world/world.h"
#include "world/extrasigns.h"
#include "sim/narrowings.h"
#include "sim/roadevents.h"
#include "sim/levelcrossing.h"
#include "sim/cyclists.h"
#include "sim/emergency.h"
#include "sim/wildlife.h"

namespace fs {

struct TrafficCar;
struct TrafficEvent;
struct PlayerSnapshot;
class RuleMonitor;

// ---- src/sim/pedestrians.ts: translated (sim/pedestrians.h) ------------------------------

// ---- src/world/stops.ts: translated (world/stops.h) ------------------------------------

// ---- src/sim/cyclists.ts: translated (sim/cyclists.h) -------------------------------------

// ---- src/sim/emergency.ts: translated (sim/emergency.h) -----------------------------------

// ---- src/sim/wildlife.ts: translated (sim/wildlife.h) -------------------------------------

// ---- src/world/buslines.ts: translated (world/buslines.h) --------------------------------

// ---- src/game/weather.ts: translated (game/weather.h) ----------------------------------------

}  // namespace fs

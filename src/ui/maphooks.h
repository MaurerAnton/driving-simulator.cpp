// SPDX-License-Identifier: AGPL-3.0-only
/**
 * HOOKS – what the maps ask of the parts of the original that are not translated yet
 * (docs/STATUS.md "Later"): the roadworks and demonstrations of the day
 * (sim/roadevents.ts) and the fuels of the stations (world/services.ts).
 *
 * Each stands here as a class of the original's name with just the members the maps call;
 * a class that is made by the maps themselves has a `create` that is null until the part
 * exists (`new TransitLayer(host)` is `TransitLayer::create ? TransitLayer::create(host) : nullptr`),
 * and whoever holds one holds a pointer that may be null:
 *
 *     if (transit) transit->closePopup();
 *
 * When a part is translated, its declaration here goes and the real class takes its place
 * (the same name, the same members): nothing that calls it changes.  A test can hang a small
 * class of its own on a hook in the meantime.
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "platform/canvas.h"
#include "platform/dom.h"
// (the aerial photographs are translated: reality/aerial.h – the maps take them from here as before)
#include "reality/aerial.h"
#include "sim/roadevents.h"
#include "ui/transitmap.h"
#include "world/services.h"

namespace fs {

// ---- src/ui/transitmap.ts: translated (ui/transitmap.h) ------------------------------------------

// ---- src/sim/roadevents.ts: translated (RoadEventInfo: sim/roadevents.h) ----------------------

// ---- src/world/services.ts: translated (FUELS: world/services.h) ---------------------------------

}  // namespace fs

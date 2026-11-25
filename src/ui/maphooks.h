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
#include "ui/transitmap.h"

namespace fs {

// ---- src/ui/transitmap.ts: translated (ui/transitmap.h) ------------------------------------------

// ---- src/sim/roadevents.ts ----------------------------------------------------------------------

/** Roadworks or a demonstration of the day, for the map (interface RoadEventInfo). */
struct RoadEventInfo {
  /** 'works' | 'demo' */
  std::string kind;
  /** where it is (world) */
  double x = 0;
  double z = 0;
  /** the street it is in */
  std::string street;
  /** when it holds: from and until (ms since 1970, by the game's clock) */
  double from = 0;
  double until = 0;
  /** happening now (a demonstration of the day may still lie ahead, or be over) */
  bool active = false;
  /** roadworks: how the traffic passes ('lane', 'light', 'signs'); a demonstration: the place it gathers at */
  std::string detail;
  /** a demonstration: the course of its march (x, z, …; empty while its roads are not loaded) */
  std::vector<double> line;
};

// ---- src/world/services.ts ----------------------------------------------------------------------

/** A fuel as the stations sell it (an entry of FUELS). */
struct FuelKind {
  std::string label;
  bool diesel = false;
  double price = 0;
};

/** `FUELS`: the fuels by their key ('e10', 'diesel' …) – null until world/services.ts is translated */
inline const std::map<std::string, FuelKind>* FUELS = nullptr;

}  // namespace fs

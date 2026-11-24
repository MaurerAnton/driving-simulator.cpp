// SPDX-License-Identifier: AGPL-3.0-only
/**
 * HOOKS – what the maps ask of the parts of the original that are not translated yet
 * (docs/STATUS.md "Later"): the trams and buses on the map (ui/transitmap.ts), the roadworks and demonstrations of the day
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

namespace fs {

// ---- src/world/timetables.ts, src/ui/transitmap.ts ---------------------------------------------

/** One run of a line from stop to stop (TimetablePattern): what the maps read of it. */
struct TimetablePattern {
  std::vector<std::string> stops;
};

/** A tram or bus line of the timetable: what the map's search reads of it. */
struct TimetableLine {
  int index = 0;
  /** its number as it is shown ("5", "52") */
  std::string ref;
  std::vector<TimetablePattern> patterns;
};

/** The timetable (Timetable): the trams' lines and the buses', by their index (a gap: no such line). */
struct Timetable {
  std::vector<std::optional<TimetableLine>> lines;
  std::vector<std::optional<TimetableLine>> buses;
};

/** What the map needs of the game to show the trams and buses (interface TransitSource). */
class TransitSource {
 public:
  virtual ~TransitSource() = default;
  virtual const Timetable* timetable() = 0;
};

/** Watching a vehicle: what game/spectate.ts needs to find it and follow it (interface WatchRequest). */
struct WatchRequest {
  /** 'tram' | 'bus' */
  std::string kind;
  int line = 0;
  std::string ref;
  std::string colour;
  std::string dest;
  TimetablePattern pattern;
  double t0 = 0;
  std::string trip;
};

/** interface TransitMapHost */
struct TransitMapHost {
  std::function<std::string()> lang;
  std::function<TransitSource*()> source;
  /** may a vehicle be watched now (a free drive)? */
  std::function<bool()> canWatch;
  std::function<void(const WatchRequest& v)> watch;
  /** draw the map again */
  std::function<void()> redraw;
};

/** The trams and buses and the stops on the big map (class TransitLayer). */
class TransitLayer {
 public:
  /** `new TransitLayer(host)` */
  static inline std::function<std::unique_ptr<TransitLayer>(TransitMapHost host)> create = nullptr;

  virtual ~TransitLayer() = default;
  virtual void attach(const dom::El& ui, const dom::El& before) = 0;
  virtual void detach() = 0;
  /** the switch for the map's lower bar */
  virtual dom::El toggle() = 0;
  virtual void closePopup() = 0;
  /** show the whole way of a line (`kind`: 'tram' | 'bus') */
  virtual void showLine(const std::string& kind, int index, const std::string& colour) = 0;
  virtual void draw(const Ctx2D& g, double W, double H, double s, double cx, double cz) = 0;
  /** a tap at a point of the picture: true when a tram, a bus or a stop was there (its small window opens) */
  virtual bool tap(double px, double py) = 0;
};

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

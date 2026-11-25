// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Public transport on the big map: every tram and bus that runs now by the
 * timetables (src/sim/timetable.ts, at the game's clock) as a small round
 * sign in its line's colour with the line's number, moving along its line;
 * the stops, once the map is close enough, with their next departures at a
 * tap; and at a tap on a vehicle a small menu beside it – show its line's
 * whole way, or attach the camera to it (it goes with the vehicle while it
 * runs on, src/game/spectate.ts).  The menu follows the vehicle on the map.
 *
 * Where a vehicle is comes from the timetable alone: between which two stops
 * of its line it is and how far, laid onto the line's way – a tram's along
 * its tracks, a bus's along the roads its line takes (src/world/buslines.ts).
 *
 * A line's colour is its own where the data has one (OpenStreetMap's colour
 * of the route, or the GTFS feed's route_color), else the KVG's colour of a
 * Kassel tram line, else one of a fixed palette chosen by the line's number –
 * the same line always in the same colour, neighbouring numbers in different
 * ones.  The colour choice and the way along a bus line are plain functions
 * (tested in tests/transit_test.cpp); the rest is drawing and the DOM.
 *
 * Origin: src/ui/transitmap.ts @ v1.7.7
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "i18n/index.h"
#include "platform/canvas.h"
#include "platform/dom.h"
#include "sim/timetable.h"
#include "world/buslines.h"
#include "world/stops.h"
#include "world/types.h"

namespace fs {

// ---------------------------------------------------------------- colours

/** The colours of the KVG's tram lines in Kassel (its network map; OpenStreetMap tags the routes so) – for data without them. */
const std::map<std::string, std::string>& KVG_TRAM_COLOURS();

/** Colours for lines that have none: clearly different from each other, readable on the dark map and on photographs. */
const std::vector<std::string>& LINE_PALETTE();

/**
 * A colour as the data gives it – "#00954B", "00954B" (GTFS route_color has
 * no #), "#0a5", or a name ("red") – as #rrggbb; null where it is none.
 */
std::optional<std::string> parseColour(const std::string& c);

/** The palette's colour of a line without one of its own: by its number (1, 2, 3 … one after the other), else by its name. */
std::string paletteColour(const TransitKind& kind, const std::string& ref);

/**
 * The colour a line is shown in: the first colour of its own that the data
 * gives (`given`, in order of preference – the timetable's, then the line
 * file's), else for a Kassel tram line the KVG's, else the palette's.
 */
std::string lineColour(const TransitKind& kind, const std::string& ref, const std::vector<std::string>& given = {});

/** Dark or white writing on a colour, whichever reads better (by its relative luminance). */
std::string textOn(const std::string& colour);

// ---------------------------------------------------------------- where the vehicles are

/** A point on a line and the way a vehicle there faces (yaw: 0 north, positive to the left). */
struct Spot {
  double x = 0;
  double z = 0;
  double yaw = 0;
  /** a bus: the way of its line it is on (index into the line's ways) */
  std::optional<int> k;
};

/** a course: x, z, … (null: not known) */
using TransitCourse = std::shared_ptr<const std::vector<float>>;

/** What the map needs of the game to show the trams and buses (made in src/game/spectate.ts: transitSource). */
class TransitSource {
 public:
  virtual ~TransitSource() = default;
  virtual const Timetable* timetable() = 0;
  /** the moment the world is at (the game's clock), as the timetable sees it */
  virtual ServiceTime now() = 0;
  /** a tram line of lines.json.gz: its colour, and its stops as (piece of track, mark) */
  virtual const LineJson* tramLine(int index) = 0;
  /** where a tram of line `index` is that left stop a for stop b and did the share `frac` of the way (null: not on its tracks) */
  virtual std::optional<Spot> tramSpot(int index, const std::pair<int, double>& a, const std::pair<int, double>& b, double frac) = 0;
  /** the course of a tram line along its tracks: x, z, … (null: not known) */
  virtual TransitCourse tramCourse(int index) = 0;
  virtual BusLines* busLines() = 0;
  /** the course of a way (a road in one direction, src/world/routegraph.ts) as it is driven: x, z, … (null: not known yet) */
  virtual TransitCourse wayCourse(int way) = 0;
  virtual Stops* stops() = 0;
};

/** A tram or bus under way, with where it is now. */
struct MapVehicle {
  Running run;
  /** the trip by name: for a tram as the trams of the traffic know it ("line:pattern:t0"), a bus's with "b:" before */
  std::string trip;
  double x = 0;
  double z = 0;
  double yaw = 0;
  std::string colour;
};

/** Metres along a course (x, z, …) to a point: the point and the heading there – null for a course without length. */
std::optional<Spot> pointAlong(const std::vector<float>& course, double m);

/** The length of a course (x, z, …). */
double courseLength(const std::vector<float>& course);

/**
 * Where a bus is that left stop `a` of its line (index into `line.stops`)
 * for stop `b` and did the share `frac` of the way: along the line's ways,
 * whose courses `course` gives.  Null where a way's course is not known.
 */
std::optional<Spot> alongBusLine(const BusLine& line, int a, int b, double frac, const std::function<TransitCourse(int way)>& course);

/** The name of a trip: a tram's as the trams of the traffic (game.ts railSchedule) know it. */
std::string tripName(const TransitKind& kind, int line, const TimetablePattern& pattern, double t0);

/** What spotOf needs of a trip under way (a Running has it all). */
struct TripSpot {
  TransitKind kind;
  int line = 0;
  int fromStop = -1;
  int toStop = -1;
  int fromPlace = -1;
  int toPlace = -1;
  bool standing = false;
  double frac = 0;
};

/** Where a trip under way is now: along its line's way where that is known, else between its stops. */
std::optional<Spot> spotOf(TransitSource& src, const TripSpot& run);

/** Every trip under way at a moment, where it is (those whose place is not known are left out). */
std::vector<MapVehicle> vehiclesAt(TransitSource& src, const Timetable& tt, const ServiceTime& at, const std::function<std::string(const TransitKind& kind, int line, const std::string& ref, const std::string& own)>& colour);

/**
 * The next departures from a stop place: the buses and trams that call
 * there – and at a tram stop of a timetable that knows the trams' stops only
 * by their names, the trams of that name.
 */
std::vector<Departure> stopDepartures(const Timetable& tt, const StopPlace& place, const ServiceTime& at, int n = 6);

// ---------------------------------------------------------------- the layer on the big map

/** Watching a vehicle: what game/spectate.ts needs to find it and follow it. */
struct WatchRequest {
  TransitKind kind;
  int line = 0;
  std::string ref;
  std::string colour;
  std::string dest;
  const TimetablePattern* pattern = nullptr;
  double t0 = 0;
  std::string trip;
};

struct TransitMapHost {
  std::function<std::string()> lang;
  std::function<TransitSource*()> source;
  /** may a vehicle be watched now (a free drive)? */
  std::function<bool()> canWatch;
  std::function<void(const WatchRequest& v)> watch;
  /** draw the map again */
  std::function<void()> redraw;
};

class TransitLayer {
 public:
  explicit TransitLayer(TransitMapHost host);
  ~TransitLayer();
  TransitLayer(const TransitLayer&) = delete;
  TransitLayer& operator=(const TransitLayer&) = delete;

  /** the trams and buses are shown (the switch at the foot of the map) */
  bool shown = true;

  /** The map opened: the small window goes into its column (`before`: in front of this element), and the vehicles move on. */
  void attach(const dom::El& ui, const dom::El& before);
  void detach();
  /** The switch at the foot of the map: trams and buses on or off. */
  dom::El toggle();
  void closePopup();
  /**
   * Show a line's whole way on the map: what a tap on one of its vehicles
   * offers, and what the search for a line number asks for (bigmap.ts).
   * `colour` is the line's own; `index` its number in its own list (tram
   * lines or bus lines).
   */
  void showLine(const TransitKind& kind, int index, const std::string& colour);
  /** Is this line's way shown now? */
  bool lineShown(const TransitKind& kind, int index) const;
  /** The colour of a line (worked out once). */
  std::string colourOf(const TransitKind& kind, int line, const std::string& ref, const std::string& own);
  /** Draw the trams, the buses and the stops (in screen pixels: x east to the right, z south downwards). */
  void draw(const Ctx2D& g, double W, double H, double s, double cx, double cz);
  /** A tap at a point of the map (px from its upper left corner): a vehicle or a stop there? Then its small window opens. */
  bool tap(double px, double py);

 private:
  struct View {
    double W = 0, H = 0, s = 1, cx = 0, cz = 0;
  };
  struct Sel {
    /** 'vehicle' | 'stop' */
    std::string kind;
    std::string trip;
    StopPlace* place = nullptr;
  };
  struct LineShown {
    TransitKind kind;
    int index = 0;
    std::string colour;
  };
  struct MenuAt {
    double x = 0, y = 0;
  };

  TransitMapHost host_;
  std::vector<MapVehicle> list_;
  double listAt_ = -1e9;
  std::unordered_map<std::string, std::string> colours_;
  View view_;
  dom::El pop_;
  /** the interval's: while it lives (and is this one), the vehicles move on */
  std::shared_ptr<int> timer_;
  /** what the small window shows: a vehicle (by its trip) or a stop */
  std::optional<Sel> sel_;
  /** the line whose way is shown */
  std::optional<LineShown> line_;
  double popAt_ = 0;
  /** where the menu of a vehicle was put last (px of the map), so that it is moved only when its vehicle has */
  std::optional<MenuAt> menuAt_;

  std::string t(const std::string& key, const TrParams& p = {}) const;
  void placeMenu(double x, double y);
  bool anyInView();
  const std::vector<MapVehicle>& vehicles(TransitSource& src, const Timetable& tt);
  void drawVehicle(const Ctx2D& g, const MapVehicle& v, double x, double y, bool small, bool chosen);
  std::vector<TransitCourse> lineCourses(TransitSource& src, const TransitKind& kind, int index);
  void render();
  dom::El badge(const TransitKind& kind, const std::string& ref, const std::string& colour);
};

}  // namespace fs

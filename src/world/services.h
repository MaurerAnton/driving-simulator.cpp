// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/services.ts @ v1.7.7
/**
 * The fuel stations and car washes of the map (services.json.gz, made by
 * tools/build-services.ts of the original from OpenStreetMap and the official
 * building models) – and how each is laid out in the game.
 *
 * A fuel station of the file is a place with a name and a brand, the fuels
 * it sells, its area as drawn in OpenStreetMap, the roofs over its pumps
 * and the place of its price sign.  The roofs are those of the building
 * models where there are any: a canopy there is a solid block from the
 * ground to its roof, so the game takes that block away (the walls, the
 * roof, its outline in the collisions) and builds an open canopy of the same
 * outline and height in its place, with pump islands under it.  A car wash
 * has its hall (or none: then it stands in the open) with a gantry that
 * travels over the car.
 *
 * Everything here is plain geometry: the game, the renderer
 * (render/services.h) and the tests share it.  The frame is the world's
 * (x east, z south, metres); along a site's roof the long side is u and the
 * short side v.
 *
 * (In fs::svc: game/ce.h has a Pose and a Rect of its own in fs.)
 */
#pragma once

#include <array>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "platform/json.h"

namespace fs::svc {

/** The newest version of services.json.gz this reads. */
inline constexpr int SERVICES_VERSION = 1;

/** A roof that is taken over by the game: the canopy over the pumps, or a car wash's hall. */
struct RoofJson {
  /** its outline: x, z pairs (the ring is closed implicitly) */
  std::vector<double> ring;
  /** the ground under it and its top, metres above sea level – null where not known (a roof drawn in OpenStreetMap only, or made up by the build) */
  std::optional<double> y0;
  std::optional<double> y1;
  /** where it comes from: the building models ('lod2'), OpenStreetMap ('osm'), or laid out by the build where neither has one ('gen') */
  std::string src;
  /** its height over the ground where OpenStreetMap tags it (and the models have none) */
  std::optional<double> h;
  /** a roof without walls (a car wash under a canopy): no walls are built under it */
  bool open = false;
};

/** A fuel station or a car wash of the file. */
struct ServiceSiteJson {
  /** 'fuel' | 'wash' */
  std::string k;
  /** its object in OpenStreetMap: n123, w123 or r123 */
  std::string osm;
  std::string name;
  std::string brand;
  /** its middle (world x, z) */
  double x = 0;
  double z = 0;
  /** its area as OpenStreetMap draws it (x, z pairs), where it is drawn as one */
  std::optional<std::vector<double>> area;
  /** the fuels it sells (FUELS); none: what nearly every station sells */
  std::vector<std::string> fuels;
  /** the canopies over the pumps (a fuel station), the hall (a car wash) */
  std::vector<RoofJson> roofs;
  /** the price sign: x, z and the way it faces (yaw: 0 north, + to the left) */
  std::optional<std::array<double, 3>> sign;
  /** a car wash: it washes by itself (a gantry), or one washes oneself */
  bool auto_ = false;
  bool self = false;
  /** a fuel station with pumps for lorries */
  bool hgv = false;
};

struct ServicesJson {
  int v = 0;
  /** the build of the map it was made for (map.json meta.generated) */
  std::string map;
  std::vector<std::string> attribution;
  std::vector<ServiceSiteJson> sites;
};

/** services.json.gz as read (a site whose numbers are not numbers keeps NaN: parseServices leaves it out). */
ServicesJson servicesFromJson(const Json& j);

// ------------------------------------------------------------------ fuels

/** A fuel: what the pump calls it, whether a diesel engine takes it, what a litre (CNG: a kilogram) costs, generic. */
struct FuelInfo {
  std::string label;
  bool diesel = false;
  double price = 0;
};

/**
 * The fuels: their code in the file, what the pump calls them (the German
 * names, as on every pump in the country) and whether a diesel engine takes
 * it.  `price`: what a litre (CNG: a kilogram) costs, generic.
 */
extern const std::map<std::string, FuelInfo>* const FUELS;
/** the codes of FUELS in the original's order */
const std::vector<std::string>& FUEL_ORDER();

/** What a station sells where the file does not say. */
const std::vector<std::string>& DEFAULT_FUELS();

/** The fuels of a station from the tags of OpenStreetMap (fuel:diesel=yes …), in the order of FUELS. */
std::vector<std::string> fuelsOf(const std::map<std::string, std::string>& tags);

/** A station's price of a fuel: the generic price, a few cents up or down from station to station (the same every visit) – today's where there is one (x, z given). */
double priceAt(const std::string& osm, std::optional<double> x, std::optional<double> z, const std::string& fuel);
inline double priceAt(const ServiceSiteJson& site, const std::string& fuel) { return priceAt(site.osm, site.x, site.z, fuel); }

/** What a wash costs (generic). */
inline constexpr double WASH_PRICE = 9.9;

// ------------------------------------------------------------------ geometry

/** A rectangle turned in the plane: its middle, the unit vector along its long side (u), its length (along u) and width. */
struct Rect {
  double cx = 0;
  double cz = 0;
  double ux = 1;
  double uz = 0;
  double len = 0;
  double wid = 0;
};

/** A point of the plane in a rectangle's frame: along it (u) and across (v; v = u turned a quarter towards +z from +x). */
std::array<double, 2> toRect(const Rect& r, double x, double z);
/** … and back. */
std::array<double, 2> fromRect(const Rect& r, double u, double v);
/** The convex hull of a flat x, z list (counter-clockwise in x, z), as x, z pairs. */
std::vector<double> convexHull(const std::vector<double>& pts);
/** The smallest rectangle round a ring (x, z pairs), its long side along u. */
Rect minRect(const std::vector<double>& ring);
/** The corners of a rectangle grown by `pad` on every side, as a ring of x, z pairs. */
std::vector<double> rectRing(const Rect& r, double pad = 0);
/** Even-odd point-in-ring test for a flat x, z ring. */
bool inRing(double x, double z, const std::vector<double>& ring);
/** How far a point is from a ring's outline (0 on it). */
double ringDistance(double x, double z, const std::vector<double>& ring);
/** The area of a flat x, z ring (always positive). */
double ringAreaAbs(const std::vector<double>& ring);

// ------------------------------------------------------------------ the layout of a site

/** A pump island: a raised kerb along u with a pump in its middle. */
struct Island {
  double x, z, ux, uz, len, wid;
};

/** A canopy over pumps as the game builds it. */
struct CanopyPlan {
  std::vector<double> ring;
  Rect rect;
  /** its ground and its top (m above sea level; NaN where the game has to take the ground under it) */
  double y0 = 0;
  double y1 = 0;
  /** its height over the ground where only that is known (OpenStreetMap's height tag; NaN: none) */
  double h = 0;
  std::vector<Island> islands;
  /** where the pumps stand (one in the middle of each island), x, z pairs */
  std::vector<double> pumps;
  /** where the columns that carry the roof stand, x, z pairs (on the ends of the islands) */
  std::vector<double> columns;
};

/** How high over the ground a canopy's top is where nothing says so (m). */
inline constexpr double CANOPY_HEIGHT = 5.2;
/** Island and pump measures (m). */
inline constexpr double ISLAND_WIDTH = 1.3;

/**
 * The islands, pumps and columns under a canopy.  The cars drive through
 * along its long side: the islands run that way, in rows across it (one
 * row for every 7 m of width, a lane on both sides of each) and one after
 * the other along it (one for every 10 m of length).  An island that does
 * not lie under the roof (a canopy shaped like an L) is left out.
 */
CanopyPlan planCanopy(const std::vector<double>& ring, double y0, double y1, double h = std::numeric_limits<double>::quiet_NaN());

/** A car wash as the game builds it: the bay along u, the gantry travelling along it. */
struct WashPlan {
  std::vector<double> ring;
  Rect rect;
  double y0 = 0;
  double y1 = 0;
  /** a hall (walls along both long sides, open at the ends) or a bay in the open */
  bool hall = false;
};

/** The narrowest and shortest hall a gantry fits in (m). */
struct WashMin {
  double wid, len;
};
inline constexpr WashMin WASH_MIN = {3.6, 7.5};
/** What the gantry takes (m): the width and height of its opening. */
struct GantrySize {
  double width, height;
};
inline constexpr GantrySize GANTRY = {2.5, 2.3};

/** The bay of a car wash: its hall, its roof, or the bay the build laid out in the open – null where there is none, or none a gantry fits in. */
std::optional<WashPlan> planWash(const ServiceSiteJson& site);

// ------------------------------------------------------------------ a vehicle at a pump, in a bay

/** Where a vehicle stands: the middle of its length on the ground, its heading, length and width. */
struct Pose {
  double x, z, yaw, length, width;
};

/** The pump a vehicle stands beside, the side of the vehicle it is on (−1 left, 1 right), and where along the vehicle it is (m from the middle, + towards the rear). */
struct AtPump {
  int pump;
  int side;
  double along;
  double gap;
};

/** How far a hose reaches from the pump to the side of a vehicle (m). */
inline constexpr double HOSE_REACH = 2.6;

/**
 * The pump a vehicle stands beside, if any: one alongside it (its heading
 * along the island, either way, within 35°), not further than a hose
 * reaches from its side, and not beyond its ends.
 */
std::optional<AtPump> pumpBeside(const CanopyPlan& plan, const Pose& p);

/** How a vehicle stands in a car wash's bay: how far from the stop mark along the bay (+ too far in), how far off its middle, and whether it is straight. */
struct InBay {
  double along;
  double across;
  bool straight;
  /** inside the bay at all (between its ends, not beside it) */
  bool inside;
  /** which way it drove in (1: along u, −1: against it) */
  int dir;
};

/** Where a vehicle stands in a bay (the stop mark is its middle). */
InBay inBay(const WashPlan& plan, const Pose& p);

// ------------------------------------------------------------------ the sites, to be found by place

/** A site of the file, with its layout. */
struct ServiceSite : ServiceSiteJson {
  int index = 0;
  /** what it sells (the file's list, or DEFAULT_FUELS) */
  std::vector<std::string> sells;
  std::vector<CanopyPlan> canopies;
  std::optional<WashPlan> wash;
};

/**
 * Reads services.json.gz: each site with its layout.  Throws on a file it
 * cannot read; a site it cannot lay out keeps no canopy (no wash).
 */
std::vector<ServiceSite> parseServices(const ServicesJson& json);

/** The sites by the squares of 250 m they reach into. */
class ServiceIndex {
 public:
  const std::vector<ServiceSite> sites;

  explicit ServiceIndex(std::vector<ServiceSite> sites);
  ServiceIndex(const ServiceIndex&) = delete;
  ServiceIndex& operator=(const ServiceIndex&) = delete;

  /** The sites that reach within `r` of a point. */
  std::vector<const ServiceSite*> near(double x, double z, double r) const;

  struct Nearest {
    const ServiceSite* site;
    double dist;
  };
  /** The nearest site of a kind that can be used (a station with pumps, a wash with a bay) – and how far it is. */
  std::optional<Nearest> nearest(double x, double z, const std::string& kind, const std::function<bool(const ServiceSite&)>& accept = nullptr) const;

 private:
  std::map<double, std::vector<const ServiceSite*>> grid_;
  static constexpr double CELL = 250;
  static double key(double i, double j) { return (i + 32768) * 65536 + (j + 32768); }
};

/** The box round everything of a site (its area, roofs and sign), grown by `pad`: minX, minZ, maxX, maxZ. */
std::array<double, 4> siteBox(const ServiceSiteJson& s, double pad = 0);

/** Is a point on a site (in its area or under one of its roofs, `margin` metres round them – or that near its middle, if it has neither)? */
bool onSite(const ServiceSiteJson& s, double x, double z, double margin);

}  // namespace fs::svc

namespace fs {
// (what the maps take from here: ui/maphooks.h)
using svc::FuelInfo;
using svc::FUELS;
}  // namespace fs

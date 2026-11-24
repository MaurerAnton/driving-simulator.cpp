// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/xsection.ts @ v1.7.7
/**
 * The cross-section of a road beyond its driving lanes: what lies on each
 * side between the outermost lane and the far edge of the pavement, and
 * what lies between the two carriageways of a dual road.
 *
 * From the middle of the road outwards, one side is laid out as
 *
 *   lanes | hard strip | cycle lane | parking (on the road) | KERB |
 *   parking (on the pavement) | verge | cycle track | pavement
 *
 * (The kinds – PARK, ORIENT, CYCLE, KERB, MEDIAN, FRONT of the original – are whole numbers here:
 * PARK.LANE is PARK_LANE.)
 *
 * The map build reads it from OpenStreetMap's tags (tools/mapgen/xsection.ts)
 * and stores it with each road (EdgeJson.xl / xr, md).  Everything from the
 * lanes to the kerb is carriageway – the road's `w` – and the lanes keep
 * clear of what is not for driving (World: marginL / marginR); everything
 * beyond the kerb is the road's `sl` / `sr`, at kerb height.
 */
#pragma once

#include <array>
#include <optional>
#include <string>

namespace fs {

/** where the cars stand (parking:<side>=…) */
inline constexpr int PARK_NONE = 0, PARK_LANE = 1, PARK_STREET_SIDE = 2, PARK_HALF_ON_KERB = 3, PARK_ON_KERB = 4, PARK_SHOULDER = 5;
/** how they stand (parking:<side>:orientation=…) */
inline constexpr int ORIENT_PARALLEL = 0, ORIENT_DIAGONAL = 1, ORIENT_PERPENDICULAR = 2;
/** a cycle lane on the carriageway, or a track of its own beyond the kerb (cycleway:<side>=…) */
inline constexpr int CYCLE_NONE = 0, CYCLE_LANE = 1, CYCLE_TRACK = 2;
/** the kerb between carriageway and pavement (kerb:<side>=…) */
inline constexpr int KERB_RAISED = 0, KERB_LOWERED = 1, KERB_FLUSH = 2;
/** what lies between the carriageways of a dual road (median:contents=…, bits); PAVED: paving stones */
inline constexpr int MEDIAN_GRASS = 1, MEDIAN_PARKING = 2, MEDIAN_HEDGE = 4, MEDIAN_TREES = 8, MEDIAN_PAVED = 16;
/** what stands right behind the pavement (frontage:<side>=…) */
inline constexpr int FRONT_NONE = 0, FRONT_HEDGE = 1, FRONT_FENCE = 2, FRONT_WALL = 3, FRONT_LAWN = 4;
/** usual heights by FRONT (m): a garden hedge grows to about 1.8 m */
inline constexpr double FRONT_HEIGHTS[5] = {0, 1.8, 1.2, 1.0, 0};
/**
 * how far behind the pavement a hedge, fence or wall stands when nothing is tagged (m): a little behind a
 * pavement; where there is none, behind a strip of lawn or gravel – front gardens do not end at the asphalt
 */
inline constexpr struct {
  double behindWalk = 0.3;
  double noWalk = 1.0;
} FRONT_OFFSETS;
/** width of the paved gutter (Rinne) between asphalt and kerb in towns, when nothing is tagged (m) */
inline constexpr double GUTTER_DEFAULT = 0.4;

/** One side of a road as stored with it (all fields optional: absent = none / the default). */
struct RoadSideJson {
  /** PARK */
  std::optional<int> p;
  /** ORIENT */
  std::optional<int> po;
  /** depth of the parking strip across the road (m) */
  std::optional<double> pw;
  /** 1: the bays are marked with lines */
  std::optional<int> pm;
  /** share of the bays a car stands in (0…1), absent: 60–80 % (the map build's parked cars only) */
  std::optional<double> pq;
  /** CYCLE */
  std::optional<int> c;
  /** width of the cycle lane or track (m) */
  std::optional<double> cw;
  /** 1: red surface */
  std::optional<int> cr;
  /** 1: an advisory lane (Schutzstreifen, dashed line); else a lane of its own (Radfahrstreifen, solid line) */
  std::optional<int> ca;
  /** 1: the track is paved with stones (else asphalt) */
  std::optional<int> cs;
  /** width of the grass verge between kerb and pavement (m) */
  std::optional<double> v;
  /** KERB */
  std::optional<int> k;
  /** width of the paved gutter along the kerb (m); 0: none; absent: the default (gutterWidth) */
  std::optional<double> g;
  /** painted edge line: 1 yes, 0 no; absent: as the road's class says (EF.EDGE_LINES) */
  std::optional<int> el;
  /** 1: the verge is paved (paving stones) rather than grass */
  std::optional<int> vp;
  /** 1: young trees stand in the verge */
  std::optional<int> vt;
  /** a kerbed tree bulb in place of a parking bay every this many metres (0 / absent: none) */
  std::optional<double> pt;
  /** FRONT: a hedge, fence, wall or lawn right behind the pavement */
  std::optional<int> ft;
  /** its height (m), absent: FRONT_HEIGHTS */
  std::optional<double> fh;
  /** how far behind the pavement (or the kerb, where there is none) it stands (m), absent: FRONT_OFFSETS */
  std::optional<double> fo;
  /** what the pavement is made of: a ground material (src/world/tilefmt.h GM); absent: grey slabs (GM.SIDEWALK) */
  std::optional<int> wm;
};

/** kerb heights by KERB (m) */
inline constexpr double KERB_HEIGHTS[3] = {0.12, 0.03, 0};

/** How deep a parking strip is across the road, by ORIENT: cars along the kerb, at an angle, side by side. */
double parkDepth(int orient);

/** The depth of the side's parking strip (0: none). */
double parkWidth(const RoadSideJson* s);

/** How much of the side lies on the carriageway beyond the lanes: cycle lane and the cars' part of it. */
double sideInner(const RoadSideJson* s);

/** How much of the side lies beyond the kerb before the pavement proper: cars on it, verge, cycle track. */
double sideOuter(const RoadSideJson* s);

/** Height of the kerb of a side. */
double kerbHeight(const RoadSideJson* s);

/** Is there anything to store about a side? */
bool sideIsPlain(const RoadSideJson* s);

/** Where everything of one side lies: distances from the centreline outwards (m). */
struct SideLayout {
  /** the kerb line (= half the carriageway) */
  double kerb = 0;
  double kerbH = 0;
  /** the outer edge of the lanes and their hard strip: from here on nothing is driven along */
  double laneEdge = 0;
  std::optional<std::array<double, 2>> cycleLane;
  /** the strip the parked cars stand in (it straddles the kerb for half_on_kerb) */
  std::optional<std::array<double, 2>> park;
  std::optional<std::array<double, 2>> verge;
  std::optional<std::array<double, 2>> track;
  /** the pavement proper (null: none) */
  std::optional<std::array<double, 2>> walk;
  /** the far edge of it all (= kerb + sl / sr) */
  double outer = 0;
};

/**
 * The layout of one side of a road whose carriageway is `half` wide on each
 * side of the centreline and has `beyond` metres beyond its kerb on this side
 * (the road's sl or sr).
 */
SideLayout sideLayout(double half, double beyond, const RoadSideJson* s);

/**
 * The width of the paved gutter (Rinne) between the asphalt and the kerb of
 * one side: as tagged, else GUTTER_DEFAULT on a road in town with a raised
 * kerb on that side (`urban`, `kerbH` the side's kerb height, 0 without a
 * pavement), else none.  Not on bays with their own paving (street_side).
 */
double gutterWidth(const RoadSideJson* s, bool urban, double kerbH);

/** Is there a painted edge line on this side?  `byClass`: the road's default (EF.EDGE_LINES). */
bool edgeLineOn(const RoadSideJson* s, bool byClass);

/** The bits of a median:contents value ("grass;trees" …); '' for a tag that is not there. */
int medianBits(const std::string& v);

}  // namespace fs

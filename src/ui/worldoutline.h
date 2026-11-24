// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The outlines of the countries, the coasts and the larger lakes around the game's
 * regions: the background of the map that shows every region there is.
 *
 * Every region lies in one shared world frame (geo/utm, world/regions.h).  The site
 * carries `data/outline.bin`: Natural Earth's countries and lakes (1:10m, public
 * domain) in that frame, cut to a box around the regions and thinned out –
 * tools/build-outline.mjs writes the file and describes it.  This reads it into plain
 * data: rings to fill, lines to stroke, names.  Nothing is drawn here.
 *
 * Everything comes in two forms: as it is (`xz`, within about 300 m of Natural
 * Earth's lines – for the views down to one region on the screen, where the
 * region's own roads take over) and with about a quarter of the points (`far`,
 * within 2 km – for all regions on one screen).  The far points are some of the
 * near ones, so a border is the same line in both its countries in either form.
 * (Natural Earth's lines themselves lie up to 2 km off the ground: this is a
 * background to find one's way on, not a survey of the borders.)
 *
 * To draw it (the map does that), in world metres:
 *   the land    each country one path of its rings – polyline(ring.xz, true) – filled;
 *   the water   each lake the same, in the colour of the sea;
 *   the lines   polyline(line.xz) of every line, stroked: coasts, borders, shores, each once;
 * with `far` in place of `xz` (and without what has no `far`) while a pixel is about
 * `farTolerance` metres or more, and with the boxes to leave out what is not in view.
 *
 * (New in 2.0: there is no file of the original that this translates.)
 */
#pragma once

#include <array>
#include <string>
#include <vector>

#include "platform/plat.h"

namespace fs {

/** One closed line: the edge of a piece of land, of a hole in it, of a lake, of an island in a lake. */
struct OutlineRing {
  /**
   * x0, z0, x1, z1 … in world metres, ready for Ctx2D::polyline(xz, true): the ring
   * is closed, its first point is not repeated at the end.
   */
  std::vector<double> xz;
  /** the same ring for the far view; empty: too small to be seen from there */
  std::vector<double> far;
  /** [minX, minZ, maxX, maxZ] of `xz` (and so of `far`) */
  std::array<double, 4> box{0, 0, 0, 0};
  /**
   * A hole in its area (a country inside the country; an island in the lake).  Holes
   * run the other way round than the rings they lie in: one path of all rings of an
   * area, filled (non-zero, the canvas' default), leaves them out.
   */
  bool hole = false;
};

/** A country: what lies of it inside the outline's frame. */
struct OutlineCountry {
  /** ISO 3166-1 alpha-2: "DE" */
  std::string iso;
  /** "Germany" */
  std::string nameEn;
  /** "Deutschland" */
  std::string nameDe;
  std::vector<OutlineRing> rings;
  /** [minX, minZ, maxX, maxZ] of all its rings */
  std::array<double, 4> box{0, 0, 0, 0};
  /** a point well inside its largest part: where its name can stand */
  double labelX = 0;
  double labelZ = 0;
};

/** A lake (those of 30 km² and more). */
struct OutlineLake {
  std::vector<OutlineRing> rings;
  std::array<double, 4> box{0, 0, 0, 0};
};

/**
 * A line to stroke.  The rings of two neighbours both run along their border, and
 * the rings of what the frame cuts through run along the frame: stroking rings
 * draws borders twice and the frame's edge as if it were a coast.  The lines are
 * every coast, border and shore once, and nothing else.
 */
struct OutlineLine {
  enum Kind { Coast = 0, Border = 1, Shore = 2 };
  Kind kind = Coast;
  /** x0, z0, x1, z1 … for Ctx2D::polyline(xz): open (an island's coast repeats its first point at the end) */
  std::vector<double> xz;
  /** the same line for the far view; empty: not seen from there */
  std::vector<double> far;
  std::array<double, 4> box{0, 0, 0, 0};
};

struct WorldOutline {
  /** in the order of their codes */
  std::vector<OutlineCountry> countries;
  std::vector<OutlineLake> lakes;
  std::vector<OutlineLine> lines;
  /** the frame everything was cut to [minX, minZ, maxX, maxZ]: land ends at it in a straight line, beyond it nothing is known */
  std::array<double, 4> box{0, 0, 0, 0};
  /** how far (metres) the lines may lie off Natural Earth's: `xz`, and `far` – which is good while a pixel is about that much or more */
  double tolerance = 0;
  double farTolerance = 0;
  /** nothing could be read (no file, a cut-off one, something else): the map then has no background */
  bool empty() const { return countries.empty(); }
};

/** The outline a file holds; an empty one when the bytes are not such a file, or not all of it. */
WorldOutline parseWorldOutline(const Bytes& bytes);

/** The country a point lies in, or null (the sea; beyond the frame). */
const OutlineCountry* countryAt(const WorldOutline& outline, double x, double z);

/**
 * The country a box [minX, minZ, maxX, maxZ] belongs to – for the label of a region:
 * the country its middle lies in; and when that is water (a town by a fjord is, on
 * a map this coarse), the country most of the box lies in.  Null: no land in it.
 */
const OutlineCountry* countryOfBox(const WorldOutline& outline, const std::array<double, 4>& box);

}  // namespace fs

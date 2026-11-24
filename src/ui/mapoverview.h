// SPDX-License-Identifier: AGPL-3.0-only
/**
 * 2.0: the schema of a whole map, as a map app shows it at every zoom: every
 * road of its network (world/netfile.h – the whole map, not only the pieces
 * loaded around the car) by its kind – the motorways and main roads first,
 * the smaller ones appearing as one zooms in – and the names of all its
 * places, the bigger the larger, none lying on another.
 *
 * The roads are kept per zoom band: for each band the roads that are drawn
 * there, joined into long lines where one road goes on into the next and
 * thinned to what a pixel shows – made once when the band is first needed,
 * so that a phone draws even a region of a thousand kilometres of roads at once.
 *
 * Used for the loaded map (when it is zoomed out) and for regions that are not
 * loaded (their net.bin.gz and map.json, fetched when one zooms into them).
 * (New in 2.0: 1.7.7 draws the roads in view one by one, and nothing beyond its map.)
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "platform/canvas.h"
#include "ui/mapstyle.h"
#include "ui/worldlayer.h"
#include "world/netfile.h"
#include "world/types.h"

namespace fs {

/** A name written on the map: the box it takes (screen pixels), for keeping the next one off it. */
struct LabelBox {
  double x = 0;
  double y = 0;
  double half = 0;
  double size = 0;
};

/** A point on a road of an overview: where, and the direction the road runs there (yaw: 0 = north, positive = to the left). */
struct OverviewSpot {
  int edge = 0;
  double x = 0;
  double z = 0;
  double yaw = 0;
  double d = 0;
};

class MapOverview {
 public:
  /** `net`: the roads (null: none, only the names); `places`: map.json's places */
  MapOverview(std::shared_ptr<const NetData> net, std::vector<PlaceJson> places);

  const NetData* net() const { return net_.get(); }
  const std::vector<PlaceJson>& places() const { return places_; }

  /** from which scale (px per metre) a kind of road is drawn */
  static double roadFrom(RoadKind k);
  /** how prominent a kind of place is (city 5 … isolated dwelling 0) */
  static int placeRank(const std::string& kind);
  /** from which scale a place of that rank is named */
  static double placeFrom(int rank);
  /** below this scale the roads come from the bands; above it the big map draws the roads in view one by one */
  static constexpr double BAND_SCALE = 0.175;

  /**
   * Draw the roads in view (`v`, the picture's size in CSS pixels; `dpr`: device pixels per CSS pixel).
   * The transform is set here and put back afterwards.  `alpha`: how strongly (over a photograph: faintly).
   */
  void drawRoads(const Ctx2D& g, const MapView& v, double dpr, const MapStyle& st, double alpha = 1);
  /**
   * The names of the places in view, the bigger first, each only where it lies on no name of
   * `taken` (which it is added to).  The picture is in CSS pixels (the caller's transform).
   * `avoidX/Y`: a point no name is to cover (the car's arrow – a name there is written below it).
   */
  void drawNames(const Ctx2D& g, const MapView& v, const MapStyle& st, std::vector<LabelBox>& taken, double avoidX = -1e9, double avoidY = -1e9) const;

  /** The point on a road nearest to (x, z), not further than `maxDist` metres (service ways and tracks left out). */
  std::optional<OverviewSpot> nearest(double x, double z, double maxDist) const;
  /** the town or village nearest to (x, z) – a city, town, village or suburb – not further than `maxDist` (null: none) */
  const PlaceJson* nearestPlace(double x, double z, double maxDist) const;

  /** how many bands are made (for tests) */
  size_t bandsMade() const { return bands_.size(); }
  /** the band a scale draws from (for tests) */
  static int bandOf(double scale);

 private:
  struct Line {
    float minX, minZ, maxX, maxZ;
    uint32_t at, n;
  };
  struct Band {
    int level = 0;
    std::vector<float> pts[ROAD_KINDS];
    std::vector<Line> lines[ROAD_KINDS];
  };
  std::shared_ptr<const NetData> net_;
  /** the places, the most prominent first */
  std::vector<PlaceJson> places_;
  std::vector<int> rank_;
  /** the roads of each kind (their numbers in the network) */
  std::vector<int> edgesOf_[ROAD_KINDS];
  /** the bands made, the one used last at the end (at most a few are kept) */
  std::vector<std::unique_ptr<Band>> bands_;
  /** (a line's points as the canvas takes them) */
  std::vector<double> scratch_;

  Band& band(int level);
  void buildKind(Band& b, int kind, double tol) const;
  /** the course of road `e` from junction a to b: x, z, … appended to `out` */
  void edgePoints(int e, std::vector<double>& out) const;
};

}  // namespace fs

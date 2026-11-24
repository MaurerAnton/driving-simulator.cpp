// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The world behind the big map: the outlines of the countries, and on them every
 * region the game has – so that one map shows them all, and a tap picks one.
 *
 * Zoomed in, the big map draws the roads of the region that is loaded; zoomed out
 * beyond it, this layer is what there is to see: land and sea, borders, and each
 * region as a box with its name (a dot where a box would be too small).  The big
 * map owns the picture and the fingers; it asks this layer to draw into its
 * canvas with its own view, and which region lies under a point.
 *
 * (New in 2.0: 1.7.7 lists the regions in the settings and shows one at a time.)
 */
#pragma once

#include <memory>
#include <optional>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "core/task.h"
#include "platform/canvas.h"
#include "ui/mapstyle.h"
#include "ui/worldoutline.h"
#include "world/regions.h"

namespace fs {

/** What the map shows: its middle in world metres, pixels per metre, the picture's size in CSS pixels. */
struct MapView {
  double cx = 0, cz = 0;
  double scale = 1;
  double width = 0, height = 0;
  double sx(double x) const { return width / 2 + (x - cx) * scale; }
  double sy(double z) const { return height / 2 + (z - cz) * scale; }
  double wx(double px) const { return cx + (px - width / 2) / scale; }
  double wz(double py) const { return cz + (py - height / 2) / scale; }
};

class WorldLayer {
 public:
  /** the id the site's own map goes by in this layer and in the settings ("": no region chosen) */
  static constexpr const char* SITE = "";
  /** below this many pixels per metre the world is what the map shows (a screen is then some 150 km wide) */
  static constexpr double WORLD_SCALE = 0.008;
  /** the farthest out the map goes when the world is there: Europe on a phone */
  static constexpr double MIN_SCALE = 0.00022;

  /**
   * Fetch the outline (`siteBase` + "data/outline.bin") and the regions' index, once.
   * `siteMap`: the extent of the site's own map [minX, minZ, maxX, maxZ] and its name,
   * shown as a region among the others.
   */
  Task<void> load(std::string siteBase, std::array<double, 4> siteMap, std::string siteName);
  /**
   * The same from what is already at hand (the game has read the index itself; a test
   * brings its own): the outline (an empty one: no background), the regions of the index
   * (null: none), the site's own map.  The layer is ready afterwards.
   */
  void set(WorldOutline outline, const RegionIndex* index, std::array<double, 4> siteMap, std::string siteName);
  /** has load() finished (with or without an outline – without regions there is nothing to show)? */
  bool ready() const { return ready_; }
  /** finished when the layer is ready (the big map waits for it and draws itself again) */
  Task<void> whenReady() const { return readied_.task(); }
  bool hasRegions() const { return !regions_.empty(); }
  /** every region, the site's own map first */
  const std::vector<Region>& regions() const { return regions_; }
  const Region* byId(const std::string& id) const;
  /** the country a region lies in, in the language given ("de": German, else English; "": unknown) */
  std::string countryOf(const Region& r, const std::string& lang) const;
  /**
   * How much there is to download for a region, in bytes – where that is known: the
   * index (world/regions.h) does not say yet, so whoever knows tells the layer.
   */
  void setDownloadBytes(const std::string& id, double bytes);
  std::optional<double> downloadBytes(const Region& r) const;

  /**
   * Draw the layer: sea, land, lakes, borders and coasts, the countries' names
   * when far out, and the regions.  `current`: the region that is loaded;
   * `chosen`: the one the player has tapped (drawn stronger); `roads`: the loaded
   * region's roads are drawn by the map itself, so its box is only outlined;
   * `style`: the map's colours (ui/mapstyle.h).
   */
  void draw(const Ctx2D& g, const MapView& v, const std::string& current, const std::string& chosen, const std::string& lang, const MapStyle& style = mapStyleOf("dark")) const;
  /** The region under a point of the picture: the smallest box that holds it, a dot within reach of a finger. */
  const Region* regionAt(const MapView& v, double px, double py) const;
  /** the view in which every region is seen / one region fills the picture */
  MapView fitAll(double width, double height) const;
  MapView fit(const Region& r, double width, double height) const;
  /** the outlines of the countries the layer draws (empty: none were read) */
  const WorldOutline& outline() const { return outline_; }

 private:
  bool ready_ = false;
  Resolver<void> readied_;
  WorldOutline outline_;
  /** what there is to download for a region (by its id), where known */
  std::vector<std::pair<std::string, double>> bytes_;
  std::vector<Region> regions_;
  /** the country of each region (index into outline_.countries, -1: none) */
  std::vector<int> country_;
  struct Placed {
    double x0, y0, x1, y1;
  };
  static bool overlaps(const std::vector<Placed>& placed, const Placed& p);
};

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Aerial photographs under the map: the official digital orthophotos of the
 * State of Hesse (DOP20, 20 cm per pixel), served by the land survey (HVBG)
 * as a WMS.  "Any use … is permitted without restriction or condition"
 * (§ 24 HVGG) – free data like the terrain and the buildings of the map.
 *
 * The service speaks the map's own coordinate system (ETRS89 / UTM 32N), so
 * a picture asked for by its corner coordinates lies exactly on the roads.
 * Pictures are fetched by the platform only after the player has switched
 * them on: the server learns the IP address and the area looked at.
 *
 * Origin: src/reality/aerial.ts (MaurerAnton/driving-simulator @ ff37c5d,
 * v0.7.0).
 */
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "platform/canvas2d.h"
#include "reality/providers.h"

namespace fs {

/** What the pictures are called (a name, not translated) and where they come from. */
struct AerialSourceInfo {
  const char* name = "Luftbilder DOP20 \xe2\x80\x93 HVBG";
  const char* attribution =
      "Luftbilder: \xc2\xa9 Hessische Verwaltung f\xc3\xbcr Bodenmanagement und Geoinformation";
  const char* info =
      "https://hvbg.hessen.de/landesvermessung/geotopographie/luftbilder/digitale-orthophotos";
  const char* wms = "https://www.gds-srv.hessen.de/cgi-bin/lika-services/ogc-free-images.ows";
  /** all orthophoto layers, the service picks the one for the scale */
  const char* layer = "he_dop_rgb";
};

extern const AerialSourceInfo AERIAL_SOURCE;

/** edge of a tile in pixels */
constexpr int AERIAL_TILE_PX = 512;
/** ground resolution of the finest level in metres per pixel (that of the photographs) */
constexpr double AERIAL_FINEST = 0.2;
/** coarsest level: one pixel is 0.2 m · 2^8 = 51.2 m, a tile is 26 km wide */
constexpr int AERIAL_MAX_LEVEL = 8;

struct AerialTile {
  int level = 0;
  /** column and row of the tile in the UTM grid of its level (east, north) */
  int col = 0, row = 0;
  /** its square in UTM metres */
  double minE = 0, minN = 0, size = 0;
};

/** Ground size of a tile of a level in metres. */
double aerialTileSize(int level);

/** The level whose pictures are just fine enough for a map drawn with `pxPerMetre` device pixels per metre. */
int aerialLevel(double pxPerMetre);

/** The tiles of a level that cover a rectangle (UTM metres), row by row. */
std::vector<AerialTile> aerialTiles(int level, double minE, double minN, double maxE, double maxN);

/** The address of a tile's picture (WMS 1.3.0 GetMap; EPSG:25832 counts east, north). */
std::string aerialUrl(const AerialTile& t);

/** "level/col/row" */
std::string aerialKey(const AerialTile& t);

/**
 * Fetches tiles when they are asked for and keeps the most recently used
 * ones.  `onLoad` is called whenever a picture has arrived (draw again).
 *
 * The backend answers synchronously (the page's fetch is performed by the
 * platform); null means "no network here" and every picture stays missing.
 */
class AerialCache {
 public:
  AerialCache(const std::function<void()>& onLoad, HttpBackend* http = nullptr, int max = 90);

  /** The picture of a tile if it is there; otherwise it is ordered. */
  const Canvas2D* get(const AerialTile& t);

  /** Forget what was ordered but is no longer in view (called before a new picture of the map is drawn). */
  void beginFrame();

  /** Finish what is in flight (the platform's fetch answers in the background). */
  void poll();

 private:
  struct Entry {
    std::unique_ptr<Canvas2D> img;
    /** the picture's bytes while they are not decoded yet */
    std::vector<uint8_t> bytes;
    bool ready = false, failed = false;
    int used = 0;
  };
  std::map<std::string, Entry> tiles_;
  int tick_ = 0;
  std::vector<AerialTile> queue_;
  std::function<void()> onLoad_;
  int max_;
  HttpBackend* http_;

  void pump();
  void trim();
};

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/reality/aerial.ts @ v1.7.7
/**
 * Aerial photographs under the map: the official digital orthophotos of the
 * State of Hesse (DOP20, 20 cm per pixel), served by the land survey (HVBG)
 * as a WMS.  "Any use … is permitted without restriction or condition"
 * (§ 24 HVGG) – free data like the terrain and the buildings of the map.
 *
 * The service speaks the map's own coordinate system (ETRS89 / UTM 32N), so
 * a picture asked for by its corner coordinates lies exactly on the roads.
 * Pictures are fetched from the survey's server by the player's browser and
 * only after the player has switched them on: the server learns the IP
 * address and the area looked at.
 *
 * 2.0: the same layer for all of Germany and beyond, from the whole country down
 * to the street (docs/AERIAL.md has every source, its licence and its credit):
 *  - close up, the open orthophotos of the German states (AERIAL_STATES): a tile
 *    of the UTM grid is asked for from the state it lies in (a coarse outline per
 *    state, Natural Earth); a tile on a border from each state it touches, as PNG
 *    with the outside transparent.  Every state's service speaks EPSG:25832, so
 *    its pictures lie on the roads as Hesse's do.
 *  - farther out, and wherever no state has pictures, Sentinel-2 cloudless 2016 by
 *    EOX (S2_SOURCE, CC BY 4.0) – Web Mercator tiles, each laid onto the map's UTM
 *    frame by the affine map of its corners (s2Tiles, s2Corners).
 *  - drawAerial() draws both for a view: Sentinel under, the states' pictures fading
 *    in over it as the map zooms in; it answers whose pictures are on the screen.
 * Everything of 2.0 is marked `// 2.0:`; the original's functions keep their
 * meaning (aerialTiles and aerialUrl of a tile without a source: Hesse).
 */
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/task.h"
#include "platform/canvas.h"

namespace fs {

/** Where the pictures come from (AERIAL_SOURCE; 2.0: one per state and Sentinel-2 – AerialSource). */
struct AerialSource {
  /** 2.0: a short key ("he", "nw" … "s2"): part of a tile's key */
  std::string id;
  /** what to call it (a name, not translated) */
  std::string name;
  std::string attribution;
  std::string info;
  std::string wms;
  /** all orthophoto layers, the service picks the one for the scale */
  std::string layer;
  /** 2.0: the WMS version ("1.3.0": CRS=; "1.1.1": SRS= – EPSG:25832 counts east, north in both) */
  std::string version = "1.3.0";
  /** 2.0: the licence (docs/AERIAL.md) */
  std::string licence;
};

extern const AerialSource AERIAL_SOURCE;

/** edge of a tile in pixels */
constexpr int AERIAL_TILE_PX = 512;
/** coarsest level: one pixel is 0.2 m · 2^8 = 51.2 m, a tile is 26 km wide */
constexpr int AERIAL_MAX_LEVEL = 8;

struct AerialTile {
  int level = 0;
  /** column and row of the tile in the UTM grid of its level (east, north) */
  int col = 0;
  int row = 0;
  /** its square in UTM metres */
  double minE = 0;
  double minN = 0;
  double size = 0;
  /**
   * 2.0: whose picture it is (null: AERIAL_SOURCE, as in 1.7.7) – and, for a tile that
   * lies on a state's border, `png`: asked for as PNG with the outside transparent.
   * For a Sentinel-2 tile (source S2_SOURCE) level, col and row are the Web Mercator
   * zoom, x and y; minE, minN and size are not used.
   */
  const AerialSource* src = nullptr;
  bool png = false;
};

/** Ground size of a tile of a level in metres. */
double aerialTileSize(int level);

/**
 * The level whose pictures are just fine enough for a map drawn with
 * `pxPerMetre` device pixels per metre.
 */
int aerialLevel(double pxPerMetre);

/** The tiles of a level that cover a rectangle (UTM metres), row by row. */
std::vector<AerialTile> aerialTiles(int level, double minE, double minN, double maxE, double maxN);

/** The address of a tile's picture (WMS 1.3.0 GetMap; EPSG:25832 counts east, north). */
std::string aerialUrl(const AerialTile& t);

std::string aerialKey(const AerialTile& t);

/**
 * Fetches tiles when they are asked for and keeps the most recently used
 * ones.  `onLoad` is called whenever a picture has arrived (draw again).
 */
class AerialCache {
 public:
  explicit AerialCache(std::function<void()> onLoad, int max = 90);
  ~AerialCache();
  AerialCache(const AerialCache&) = delete;
  AerialCache& operator=(const AerialCache&) = delete;

  /** The picture of a tile if it is there; otherwise it is ordered. */
  const Image* get(const AerialTile& t);

  /** Forget what was ordered but is no longer in view (called before a new picture of the map is drawn). */
  void beginFrame();

  /** how a picture is fetched: `new Image()` with `src = url` (a test may hand its own) */
  std::function<Task<Image>(std::string url)> load = [](std::string url) { return loadImage(std::move(url)); };

  /** (for tests) how many pictures are kept or on their way, how many are on their way */
  size_t size() const;
  int loadingNow() const;

 private:
  struct Entry {
    Image img;
    bool ready = false;
    bool failed = false;
    double used = 0;
  };
  /** (what a picture on its way needs when it arrives – it may arrive after the cache is gone) */
  struct Inner {
    bool alive = true;
    /** (the tiles in the order they were put in, as the original's Map iterates) */
    std::unordered_map<std::string, Entry> tiles;
    std::vector<std::string> order;
    int loading = 0;
    double tick = 0;
    /** 2.0: the tick a picture of the map began at – the tiles asked for since are on the screen and are not let go */
    double frameStart = 1e300;   // (no picture begun yet: nothing is kept for one)
    std::vector<AerialTile> queue;
    std::function<void()> onLoad;
    int max = 90;
  };
  std::shared_ptr<Inner> in;
  static void pump(const std::shared_ptr<Inner>& in, const std::function<Task<Image>(std::string)>& load);
  static Task<void> fetchOne(std::shared_ptr<Inner> in, std::string key, Task<Image> picture, std::function<Task<Image>(std::string)> load);
  static void trim(Inner& in);

 public:
  /** 2.0: a new picture of the map begins (drawAerial says so): what it asks for is kept, however many tiles that is */
  void newFrame() { in->frameStart = in->tick + 1; }

 private:
};

// ---- 2.0: Germany and beyond ------------------------------------------------------------------

/** 2.0: a German state whose orthophotos are used: its service and a coarse outline (lon, lat; Natural Earth) */
struct AerialState {
  /** ISO 3166-2 ("DE-HE") */
  std::string iso;
  AerialSource source;
  /** its rings (lon, lat, lon, lat …), the islands among them; holes are left out (a city state is asked first) */
  std::vector<std::vector<double>> rings;
  /** (the box of all rings: west, south, east, north) */
  std::array<double, 4> box{};
};

/** 2.0: the states whose open orthophotos are used, the city states first (docs/AERIAL.md; AERIAL_NOT_FREE: the others) */
const std::vector<AerialState>& AERIAL_STATES();

/** 2.0: the states whose orthophotos are not used – not clearly free, or not to be drawn in a browser: why, and where to look */
struct AerialNotFree {
  std::string iso;
  std::string why;
  std::string info;
};
const std::vector<AerialNotFree>& AERIAL_NOT_FREE();

/** 2.0: does a ring (lon, lat …) hold a point? */
bool ringHolds(const std::vector<double>& ring, double lon, double lat);
/** 2.0: the state a point lies in (null: none of AERIAL_STATES – abroad, at sea, or a state without free pictures) */
const AerialState* aerialStateAt(double lat, double lon);
/**
 * 2.0: the tiles of a level that cover a rectangle (UTM metres), each with the state whose
 * pictures it shows – a tile on a border once for each state it touches (as PNG), a tile
 * in no state not at all.
 */
std::vector<AerialTile> aerialStateTiles(int level, double minE, double minN, double maxE, double maxN);

/** 2.0: Sentinel-2 cloudless 2016 by EOX (CC BY 4.0) – Web Mercator tiles of 256 pixels */
extern const AerialSource S2_SOURCE;
constexpr int S2_TILE_PX = 256;
/** 2.0: its finest zoom (9.55 m per pixel at the equator; the pictures are of 10 m) */
constexpr int S2_MAX_ZOOM = 14;
/** 2.0: the zoom whose pictures are just fine enough for `pxPerMetre` device pixels per metre at a latitude */
int s2Zoom(double pxPerMetre, double lat);
/** 2.0: the Web Mercator tiles of a zoom that cover a box of latitudes and longitudes, row by row */
std::vector<AerialTile> s2Tiles(int zoom, double south, double west, double north, double east);
/** 2.0: a tile's north-west corner (lat, lon) – of (x + 1, y + 1): its south-east one */
std::array<double, 2> s2TileLatLon(int zoom, double x, double y);
/** 2.0: a tile's picture (WMTS, GoogleMapsCompatible) */
std::string s2Url(const AerialTile& t);

/** 2.0: what the map is looking at: its middle in world metres, CSS pixels per metre, its size, the device's pixel ratio */
struct AerialView {
  double cx = 0, cz = 0;
  double scale = 1;
  double width = 0, height = 0;
  double dpr = 1;
  /** the world frame's origin in UTM metres (the map's meta) */
  double originE = 0, originN = 0;
};

/** 2.0: what the layer is called (the title of its switch): the states' orthophotos and Sentinel-2 – names, not translated */
std::string aerialLayerName();

/** 2.0: how strong the states' pictures are over Sentinel's at a level (1 close up, fading out from level 5 to 7) */
double aerialStatesAlpha(int level);

/**
 * 2.0: draw the aerial layer under the roads for a view (in CSS pixels, from the canvas'
 * plain transform `dpr`): Sentinel-2 everywhere, the states' orthophotos over it as the
 * map zooms in.  Answers the credits of the sources drawn (for the map's corner), one
 * line each.
 */
std::vector<std::string> drawAerial(const Ctx2D& g, AerialCache& cache, const AerialView& v);

}  // namespace fs

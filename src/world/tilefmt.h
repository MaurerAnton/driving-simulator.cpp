// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Binary container for map tiles (public/map/tiles/<ix>_<iz>.fsb).
 *
 *   "FSB1" | u32 jsonLength | JSON header (space padded to 4 bytes) | sections …
 *
 * The header lists typed-array sections by name with byte offsets relative to
 * the start of the payload.  Everything is little-endian (all supported
 * platforms are), so sections can be viewed in place without copying.
 *
 * Origin: src/world/tilefmt.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).
 */
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace fs {

/** Quantisation of mesh positions inside a tile. */
constexpr double POS_XZ_SCALE = 0.02;  // metres per unit, relative to the tile centre
constexpr double POS_Y_SCALE = 0.01;   // metres per unit, relative to header.yBase
constexpr double UV_SCALE = 0.02;

/** Ground surface material ids (per-vertex attribute of the `ground` mesh). */
enum GroundMat : int {
  GM_ASPHALT = 0,
  GM_CONCRETE = 1,
  GM_PAVING = 2,
  GM_SETT = 3,
  GM_GRAVEL = 4,
  GM_DIRT = 5,
  GM_GRASS = 6,
  GM_SIDEWALK = 7,
  GM_KERB = 8,
  GM_VERGE = 9,
  GM_ASPHALT_OLD = 10,
  GM_RAIL_BALLAST = 11,
  GM_PATH = 12,
  GM_CYCLE = 13,
  /** paint (`mark` mesh) */
  GM_PAINT_WHITE = 20,
  GM_PAINT_YELLOW = 21,
  GM_PAINT_RED = 22,
  GM_PAINT_BLUE = 23,
};

/** Building face styles (per-vertex attribute of the `bldg` mesh). */
enum BuildingStyle : int {
  BS_WALL_HOUSE = 0,
  BS_WALL_GARAGE = 1,
  BS_WALL_COMMERCIAL = 2,
  BS_WALL_PUBLIC = 3,
  BS_WALL_CHURCH = 4,
  BS_WALL_PLAIN = 5,
  BS_ROOF_TILES = 8,
  BS_ROOF_FLAT = 9,
  BS_ROOF_METAL = 10,
};

/** One sign post with its plates, top to bottom. */
struct SignPost {
  double x = 0, y = 0, z = 0;
  /** direction the plates FACE (radians, same convention as vehicle yaw: 0 = facing north) */
  double yaw = 0;
  /** sign codes, e.g. ["205", "1002-12"]; "310:Name" style codes carry text */
  std::vector<std::string> s;
  /** mounting height of the lowest plate's lower edge above ground */
  double h = 0;
  /** 0 = normal size, 1 = large (motorway) */
  int big = 0;
};

struct TileObject {
  /** kind: lamp, bus_stop, bollard, bench, … */
  std::string k;
  double x = 0, y = 0, z = 0;
  double yaw = 0;
  bool hasYaw = false;
  /** free-form parameter (e.g. name of the bus stop) */
  std::string p;
};

enum class SectionType { U8, I8, U16, I16, U32, I32, F32 };

struct SectionInfo {
  size_t off = 0;
  size_t n = 0;
  SectionType t = SectionType::U8;
};

struct TileHeader {
  int v = 0;
  int ix = 0, iz = 0;
  /** world coordinates of the tile centre and the height reference */
  double cx = 0, cz = 0, yBase = 0, size = 0;
  /** terrain grid: (n × n) samples covering the tile, spacing = size / (n - 1) */
  int terrainN = 0;
  double terrainMin = 0, terrainScale = 1;
  int coverN = 0;
  std::vector<SignPost> signs;
  std::vector<TileObject> objects;
  std::map<std::string, SectionInfo> sections;
};

/**
 * An unpacked tile: the header plus a typed view of every section.
 * Sections are typed vectors so the renderer can read them directly.
 */
struct UnpackedTile {
  TileHeader header;
  std::map<std::string, std::vector<double>> sections;  // numeric view of each section

  /** Numeric section, or nullptr when the tile has no such section. */
  const std::vector<double>* get(const std::string& name) const {
    auto it = sections.find(name);
    return it == sections.end() ? nullptr : &it->second;
  }
  /** Convenience: number of elements in a section (0 when absent). */
  size_t count(const std::string& name) const {
    auto it = sections.find(name);
    return it == sections.end() ? 0 : it->second.size();
  }
};

/**
 * Parse an FSB1 tile (gzip already inflated by the fetch layer).
 * Throws std::runtime_error on a bad magic, as the JS unpackTile does.
 */
UnpackedTile unpackTile(const std::vector<uint8_t>& buf);

/** Tile index of a world coordinate. */
int tileIndex(double v, double min, double size);

/** Tile key of a tile index pair ("ix_iz"). */
std::string tileKey(int ix, int iz);

/** Build an FSB1 tile (used by the map pipeline and by tests). */
std::vector<uint8_t> packTile(const TileHeader& headerWithoutSections,
                              const std::map<std::string, std::vector<double>>& sections);

}  // namespace fs

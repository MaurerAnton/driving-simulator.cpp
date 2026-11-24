// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/tilefmt.ts @ v1.7.7
/**
 * Binary container for map tiles (public/map/tiles/<ix>_<iz>.fsb).
 *
 *   "FSB1" | u32 jsonLength | JSON header (space padded to 4 bytes) | sections …
 *
 * The header lists typed-array sections by name with byte offsets relative to
 * the start of the payload.  Everything is little-endian (all supported
 * platforms are).
 *
 * A section is a typed array, as in the original: a `SectionArray` holds the
 * numbers in a vector of their own type (16-bit positions stay 16 bits wide),
 * so that the renderer can hand the vector to the graphics card as it is –
 * and move it out of the tile rather than copy it.
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/js.h"
#include "platform/json.h"

namespace fs {

/** 'u8' | 'i8' | 'u16' | 'i16' | 'u32' | 'i32' | 'f32' */
enum class SectionType { U8, I8, U16, I16, U32, I32, F32 };

/** how many bytes one number of that type takes (BYTES) */
size_t sectionBytes(SectionType t);
/** the type as the header names it ("u16"), and back (what is none of them: f32, as the original reads it) */
const char* sectionTypeName(SectionType t);
SectionType sectionTypeOf(std::string_view name);

/**
 * A typed array (Uint8Array | Int8Array | Uint16Array | Int16Array | Uint32Array |
 * Int32Array | Float32Array): the vector of its `type` holds the numbers, the others are empty.
 */
struct SectionArray {
  SectionType type = SectionType::U8;
  std::vector<uint8_t> u8;
  std::vector<int8_t> i8;
  std::vector<uint16_t> u16;
  std::vector<int16_t> i16;
  std::vector<uint32_t> u32;
  std::vector<int32_t> i32;
  std::vector<float> f32;

  SectionArray() = default;
  SectionArray(std::vector<uint8_t> v) : type(SectionType::U8), u8(std::move(v)) {}
  SectionArray(std::vector<int8_t> v) : type(SectionType::I8), i8(std::move(v)) {}
  SectionArray(std::vector<uint16_t> v) : type(SectionType::U16), u16(std::move(v)) {}
  SectionArray(std::vector<int16_t> v) : type(SectionType::I16), i16(std::move(v)) {}
  SectionArray(std::vector<uint32_t> v) : type(SectionType::U32), u32(std::move(v)) {}
  SectionArray(std::vector<int32_t> v) : type(SectionType::I32), i32(std::move(v)) {}
  SectionArray(std::vector<float> v) : type(SectionType::F32), f32(std::move(v)) {}
  /** an array of `n` zeros of a type */
  SectionArray(SectionType t, size_t n);

  /** how many numbers (`length`) */
  size_t size() const;
  /** number `i` (`a[i]`) */
  double at(size_t i) const;
  /** where its numbers lie in memory, and how many bytes they take */
  const uint8_t* bytes() const;
  size_t byteLength() const { return size() * sectionBytes(type); }
};

struct SectionInfo {
  size_t off = 0;
  size_t n = 0;
  SectionType t = SectionType::U8;
  /** how the numbers are stored (CODING; 0: as they are) and, for that, how many belong together */
  int c = 0;
  int s = 1;
};

/**
 * How a section of whole numbers is stored.  Vertices, indices and heights
 * change little from one to the next: stored as the steps between them, with
 * the low and the high bytes apart, they take half the room once deflated.
 * Which way is smallest depends on the data – the map build tries them.
 */
enum Coding : int {
  /** as they are */
  CODING_RAW = 0,
  /** component by component (all x, then all y, …), low bytes before high bytes */
  CODING_PLANES = 1,
  /** the same, of the step from the number of the same component before */
  CODING_DELTA = 2,
  /** a grid with rows of `s` numbers: what the neighbours to the left and above do not foretell */
  CODING_GRID = 3,
};

/** A section in one of the codings: its bytes, and what it decodes to. */
struct CodedSection {
  std::vector<uint8_t> bytes;
  SectionType t = SectionType::U8;
  size_t n = 0;
  int c = CODING_RAW;
  int s = 1;
};

/** Store a section of whole numbers in a coding (`stride`: how many numbers belong together; for GRID the width of a row). */
CodedSection encodeSection(const SectionArray& a, int coding, int stride);

/** What the bytes of a section stored in a coding decode to. */
SectionArray decodeSection(const uint8_t* bytes, const SectionInfo& info);

/** Quantisation of mesh positions inside a tile. */
constexpr double POS_XZ_SCALE = 0.02;  // metres per unit, relative to the tile centre
constexpr double POS_Y_SCALE = 0.01;   // metres per unit, relative to header.yBase
constexpr double UV_SCALE = 0.02;

/** Ground surface material ids (per-vertex attribute of the `ground` mesh) (GM). */
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
  /** parking bays beside the carriageway (lighter paving) */
  /** (not written by the map build: versions before 1.7.0 read the map too and draw 14 and above as paint – bays use PAVING) */
  GM_BAY = 14,
  /** paint (`mark` mesh) */
  GM_PAINT_WHITE = 20,
  GM_PAINT_YELLOW = 21,
  GM_PAINT_RED = 22,
  GM_PAINT_BLUE = 23,
};

/** Building face styles (per-vertex attribute of the `bldg` mesh) (BS). */
enum BuildingStyle : int {
  BS_WALL_HOUSE = 0,
  BS_WALL_GARAGE = 1,
  BS_WALL_COMMERCIAL = 2,
  BS_WALL_PUBLIC = 3,
  BS_WALL_CHURCH = 4,
  BS_WALL_PLAIN = 5,
  /** facing brick (with windows like a house) */
  BS_WALL_BRICK = 6,
  BS_ROOF_TILES = 8,
  BS_ROOF_FLAT = 9,
  BS_ROOF_METAL = 10,
};

/** One sign post with its plates, top to bottom. */
struct SignPost {
  double x = 0;
  double y = 0;
  double z = 0;
  /** direction the plates FACE (radians, same convention as vehicle yaw: 0 = facing north) */
  double yaw = 0;
  /** sign codes, e.g. ["205", "1002-12"]; "310:Name" style codes carry text */
  std::vector<std::string> s;
  /** mounting height of the lowest plate's lower edge above ground */
  double h = 0;
  /** 0 = normal size, 1 = large (motorway) */
  int big = 0;
};
FS_JSON_DECLARE(SignPost)

struct TileObject {
  /** kind: lamp, bus_stop, bollard, bench, … */
  std::string k;
  double x = 0;
  double y = 0;
  double z = 0;
  std::optional<double> yaw;
  /** free-form parameter (e.g. name of the bus stop); '': none */
  std::string p;
};
FS_JSON_DECLARE(TileObject)

/** `Record<string, SectionInfo>`: the sections of a container by their names, in the order of the file. */
class SectionMap {
 public:
  using value_type = std::pair<std::string, SectionInfo>;
  /** the section of that name, or null */
  const SectionInfo* find(std::string_view name) const;
  bool has(std::string_view name) const { return find(name) != nullptr; }
  /** `sections[name] = info` */
  void set(std::string_view name, const SectionInfo& info);
  size_t size() const { return items_.size(); }
  std::vector<value_type>::const_iterator begin() const { return items_.begin(); }
  std::vector<value_type>::const_iterator end() const { return items_.end(); }

 private:
  std::vector<value_type> items_;
};
void fromJson(JsonReader& r, SectionMap& v);
void toJson(JsonWriter& w, const SectionMap& v);

struct TileHeader {
  int v = 0;
  int ix = 0;
  int iz = 0;
  /** world coordinates of the tile centre and the height reference */
  double cx = 0;
  double cz = 0;
  double yBase = 0;
  double size = 0;
  /** terrain grid: (n × n) samples covering the tile, spacing = size / (n - 1) */
  int terrainN = 0;
  double terrainMin = 0;
  double terrainScale = 0;
  int coverN = 0;
  std::vector<SignPost> signs;
  std::vector<TileObject> objects;
  /** 1: nothing of this tile stands in a lane – the map build saw to it (src/sim/clearlanes.ts); missing: not said */
  std::optional<int> clr;
  SectionMap sections;
};
FS_JSON_DECLARE(TileHeader)

/** What goes into a container under a name: a typed array as it is, or a section in a coding. */
struct SectionData {
  CodedSection coded;
  SectionData(const SectionArray& a);
  SectionData(CodedSection c) : coded(std::move(c)) {}
};
using SectionList = std::vector<std::pair<std::string, SectionData>>;

/** (the header without its `sections`: they are worked out here) */
std::vector<uint8_t> packTile(const TileHeader& header, const SectionList& sections);

/** The container with any header: named typed arrays behind a JSON object (which gets their places as `sections`). */
std::vector<uint8_t> packSections(const Json& header, const SectionList& sections);

/**
 * A container that was read: its header and its sections.  A section is made
 * into a typed array when it is first asked for (one copy of its bytes, or
 * decoded from its coding) and then stays – whoever asks again gets the same
 * array, also after somebody has moved the numbers out of it.
 */
class UnpackedSections {
 public:
  /** the header as it stands in the file (JSON) – `header<H>()` reads it into a struct */
  std::string_view headerText() const { return std::string_view(reinterpret_cast<const char*>(buf_.data()) + 8, jsonLen_); }
  /** header.sections */
  SectionMap sections;

  /** The section of that name, or null if the container has none. */
  SectionArray* get(std::string_view name);

  /** The header read into a struct that fromJson knows (false: it does not fit). */
  template <class H>
  bool header(H& out) const {
    return jsonParse(headerText(), out);
  }

 private:
  friend UnpackedSections unpackSections(std::vector<uint8_t> buf);
  std::vector<uint8_t> buf_;
  size_t jsonLen_ = 0;
  size_t base_ = 0;
  /** the sections that were asked for */
  std::vector<std::pair<std::string, std::unique_ptr<SectionArray>>> made_;
};

/** Read a container (throws std::runtime_error if it is not one). */
UnpackedSections unpackSections(std::vector<uint8_t> buf);

/**
 * Trees and the outlines of buildings are stored as whole numbers around the
 * tile's middle.  A tree: where it stands to a hand's breadth (section
 * treeXZ – it stands on the tile's ground, so its height above sea level is
 * not stored), how tall it is and of which kind (treeHK: all the heights,
 * then all the kinds).
 */
constexpr double TREE_SCALE = 0.1;
constexpr double TREE_HEIGHT_SCALE = 0.2;
constexpr double RING_SCALE = 0.02;

/**
 * The height of a tile's ground at a place in it, from its grid of heights
 * (section hgt) – the surface the game draws: two triangles per cell, split
 * along the diagonal from its second to its third corner.
 *
 * `h`: anything with cx, cz, size, terrainN, terrainMin, terrainScale (a TileHeader);
 * `hgt`: anything read with [] (the u16 vector of the section, a list of numbers).
 */
template <class Header, class Heights>
double tileGround(const Header& h, const Heights& hgt, double x, double z) {
  const double n = h.terrainN;
  const double step = h.size / (n - 1);
  const double fx = jsMax(0, jsMin(n - 1.0001, (x - (h.cx - h.size / 2)) / step));
  const double fz = jsMax(0, jsMin(n - 1.0001, (z - (h.cz - h.size / 2)) / step));
  const double i = std::floor(fx);
  const double j = std::floor(fz);
  const double tx = fx - i;
  const double tz = fz - j;
  // (a place that is no number has no height)
  if (!(j * n + i >= 0)) return JS_NAN;
  const size_t k = static_cast<size_t>(j * n + i);
  const double h00 = hgt[k];
  const double h10 = hgt[k + 1];
  const double h01 = hgt[k + static_cast<size_t>(n)];
  const double h11 = hgt[k + static_cast<size_t>(n) + 1];
  const double v = tx + tz <= 1 ? h00 + (h10 - h00) * tx + (h01 - h00) * tz : h11 + (h01 - h11) * (1 - tx) + (h10 - h11) * (1 - tz);
  return h.terrainMin + v * h.terrainScale;
}

/**
 * A tile that was read.  Besides what the file holds, `get` answers for what
 * follows from it: `bldg_a16` (the facades' numbers, from the faces' runs –
 * world/facade.h), `trees` (x, y, z, height, kind per tree, from treeXZ or
 * treeQ and treeHK) and `collPts` (x, z per corner, from collQ).
 */
class UnpackedTile {
 public:
  TileHeader header;
  SectionArray* get(std::string_view name);

 private:
  friend UnpackedTile unpackTile(std::vector<uint8_t> buf);
  UnpackedSections u_;
  /** what follows from other sections: worked out (or found missing) once */
  std::unique_ptr<SectionArray> trees_;
  std::unique_ptr<SectionArray> rings_;
  std::unique_ptr<SectionArray> faces_;
  bool treesDone_ = false;
  bool ringsDone_ = false;
  bool facesDone_ = false;
};

UnpackedTile unpackTile(std::vector<uint8_t> buf);

/** Tile index of a world coordinate. */
inline int tileIndex(double v, double min, double size) {
  const double i = std::floor((v - min) / size);
  // (a place that is no number lies in no tile: an index no map has)
  if (!(i > -2e9 && i < 2e9)) return std::numeric_limits<int>::min();
  return static_cast<int>(i);
}

std::string tileKey(int ix, int iz);

}  // namespace fs

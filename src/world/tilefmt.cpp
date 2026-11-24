// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/tilefmt.ts @ v1.7.7
/** Binary container for map tiles (see tilefmt.h). */
#include "world/tilefmt.h"

#include <bit>
#include <cstring>
#include <stdexcept>

#include "world/facade.h"

namespace fs {

// (the sections are copied as they lie in the file: low byte first, as on every platform the game runs on)
static_assert(std::endian::native == std::endian::little, "the tile format is read on little-endian machines");

namespace {

/** (the first tiles of this kind: x, y, z per tree in steps of 2 cm, section treeQ) */
const double TREE_Q_SCALE = 0.02;

int32_t wrap(int64_t d, size_t bytes) { return bytes == 4 ? static_cast<int32_t>(d) : bytes == 2 ? static_cast<int16_t>(d) : static_cast<int8_t>(d); }
uint32_t zig(int32_t d, size_t bytes) {
  const uint32_t z = (static_cast<uint32_t>(d) << 1) ^ static_cast<uint32_t>(d >> 31);
  return z & (bytes == 4 ? 0xffffffffu : bytes == 2 ? 0xffffu : 0xffu);
}
int32_t unzig(uint32_t u) { return static_cast<int32_t>((u >> 1) ^ (0u - (u & 1u))); }

/** the numbers of a section as they are in the file */
template <class T>
void rawInto(const uint8_t* bytes, size_t n, std::vector<T>& out) {
  out.resize(n);
  if (n > 0) std::memcpy(out.data(), bytes, n * sizeof(T));
}

/**
 * decodeSection for one type of number.  The bytes of the numbers lie apart (all the low
 * bytes of a group, then its higher ones); `T` wraps a sum the way the steps were wrapped.
 */
template <class T>
void decodeInto(const uint8_t* bytes, size_t n, int coding, size_t stride, std::vector<T>& out) {
  const size_t B = sizeof(T);
  out.assign(n, 0);
  if (stride == 0) return;
  const size_t groups = coding == CODING_GRID ? 1 : stride;
  const size_t per = n / groups;
  for (size_t c = 0; c < groups; c++) {
    const uint8_t* p0 = bytes + c * B * per;
    const uint8_t* p1 = p0 + per;
    const uint8_t* p2 = p0 + 2 * per;
    const uint8_t* p3 = p0 + 3 * per;
    auto value = [&](size_t i) -> uint32_t {
      uint32_t v = p0[i];
      if (B > 1) v |= static_cast<uint32_t>(p1[i]) << 8;
      if (B > 2) v |= (static_cast<uint32_t>(p2[i]) << 16) | (static_cast<uint32_t>(p3[i]) << 24);
      return v;
    };
    if (coding == CODING_GRID) {
      T* o = out.data();
      for (size_t i = 0; i < per; i++) {
        const size_t col = i % stride;
        const int64_t left = col > 0 ? static_cast<int64_t>(o[i - 1]) : 0;
        const int64_t up = i >= stride ? static_cast<int64_t>(o[i - stride]) : 0;
        const int64_t ul = col > 0 && i >= stride ? static_cast<int64_t>(o[i - stride - 1]) : 0;
        o[i] = static_cast<T>(static_cast<int64_t>(unzig(value(i))) + left + up - ul);
      }
    } else if (coding == CODING_DELTA) {
      T* o = out.data() + c;
      // (the typed array wraps the sum the way the steps were wrapped)
      T prev = 0;
      for (size_t i = 0; i < per; i++) {
        prev = static_cast<T>(static_cast<int64_t>(prev) + static_cast<int64_t>(unzig(value(i))));
        o[i * stride] = prev;
      }
    } else {
      T* o = out.data() + c;
      for (size_t i = 0; i < per; i++) o[i * stride] = static_cast<T>(value(i));
    }
  }
}

/** number `i` of a section of whole numbers, as JavaScript reads it from its typed array */
int64_t wholeAt(const SectionArray& a, size_t i) {
  switch (a.type) {
    case SectionType::U8: return a.u8[i];
    case SectionType::I8: return a.i8[i];
    case SectionType::U16: return a.u16[i];
    case SectionType::I16: return a.i16[i];
    case SectionType::U32: return a.u32[i];
    case SectionType::I32: return a.i32[i];
    default: return static_cast<int64_t>(a.f32[i]);
  }
}

}  // namespace

size_t sectionBytes(SectionType t) {
  switch (t) {
    case SectionType::U8:
    case SectionType::I8: return 1;
    case SectionType::U16:
    case SectionType::I16: return 2;
    default: return 4;
  }
}

const char* sectionTypeName(SectionType t) {
  switch (t) {
    case SectionType::U8: return "u8";
    case SectionType::I8: return "i8";
    case SectionType::U16: return "u16";
    case SectionType::I16: return "i16";
    case SectionType::U32: return "u32";
    case SectionType::I32: return "i32";
    default: return "f32";
  }
}

SectionType sectionTypeOf(std::string_view name) {
  if (name == "u8") return SectionType::U8;
  if (name == "i8") return SectionType::I8;
  if (name == "u16") return SectionType::U16;
  if (name == "i16") return SectionType::I16;
  if (name == "u32") return SectionType::U32;
  if (name == "i32") return SectionType::I32;
  return SectionType::F32;
}

SectionArray::SectionArray(SectionType t, size_t n) : type(t) {
  switch (t) {
    case SectionType::U8: u8.assign(n, 0); break;
    case SectionType::I8: i8.assign(n, 0); break;
    case SectionType::U16: u16.assign(n, 0); break;
    case SectionType::I16: i16.assign(n, 0); break;
    case SectionType::U32: u32.assign(n, 0); break;
    case SectionType::I32: i32.assign(n, 0); break;
    default: f32.assign(n, 0); break;
  }
}

size_t SectionArray::size() const {
  switch (type) {
    case SectionType::U8: return u8.size();
    case SectionType::I8: return i8.size();
    case SectionType::U16: return u16.size();
    case SectionType::I16: return i16.size();
    case SectionType::U32: return u32.size();
    case SectionType::I32: return i32.size();
    default: return f32.size();
  }
}

double SectionArray::at(size_t i) const {
  switch (type) {
    case SectionType::U8: return u8[i];
    case SectionType::I8: return i8[i];
    case SectionType::U16: return u16[i];
    case SectionType::I16: return i16[i];
    case SectionType::U32: return u32[i];
    case SectionType::I32: return i32[i];
    default: return f32[i];
  }
}

const uint8_t* SectionArray::bytes() const {
  switch (type) {
    case SectionType::U8: return u8.data();
    case SectionType::I8: return reinterpret_cast<const uint8_t*>(i8.data());
    case SectionType::U16: return reinterpret_cast<const uint8_t*>(u16.data());
    case SectionType::I16: return reinterpret_cast<const uint8_t*>(i16.data());
    case SectionType::U32: return reinterpret_cast<const uint8_t*>(u32.data());
    case SectionType::I32: return reinterpret_cast<const uint8_t*>(i32.data());
    default: return reinterpret_cast<const uint8_t*>(f32.data());
  }
}

CodedSection encodeSection(const SectionArray& a, int coding, int stride_) {
  const size_t B = sectionBytes(a.type);
  const size_t n = a.size();
  CodedSection out;
  out.t = a.type;
  out.n = n;
  out.c = coding;
  out.s = stride_;
  if (coding == CODING_RAW) {
    out.bytes.assign(a.bytes(), a.bytes() + a.byteLength());
    return out;
  }
  const size_t stride = stride_ > 0 ? static_cast<size_t>(stride_) : 1;
  const size_t groups = coding == CODING_GRID ? 1 : stride;
  const size_t per = n / groups;
  std::vector<uint32_t> vals(n, 0);
  if (coding == CODING_GRID) {
    for (size_t i = 0; i < n; i++) {
      const size_t col = i % stride;
      const int64_t left = col > 0 ? wholeAt(a, i - 1) : 0;
      const int64_t up = i >= stride ? wholeAt(a, i - stride) : 0;
      const int64_t ul = col > 0 && i >= stride ? wholeAt(a, i - stride - 1) : 0;
      vals[i] = zig(wrap(wholeAt(a, i) - (left + up - ul), B), B);
    }
  } else {
    const uint32_t mask = B == 4 ? 0xffffffffu : B == 2 ? 0xffffu : 0xffu;
    for (size_t c = 0; c < stride; c++) {
      int64_t prev = 0;
      for (size_t i = 0; i < per; i++) {
        const int64_t v = wholeAt(a, i * stride + c);
        vals[c * per + i] = coding == CODING_DELTA ? zig(wrap(v - prev, B), B) : static_cast<uint32_t>(v) & mask;
        prev = v;
      }
    }
  }
  out.bytes.assign(n * B, 0);
  size_t o = 0;
  for (size_t c = 0; c < groups; c++)
    for (size_t b = 0; b < B; b++)
      for (size_t i = 0; i < per; i++) out.bytes[o++] = static_cast<uint8_t>((vals[c * per + i] >> (8 * b)) & 255);
  return out;
}

SectionArray decodeSection(const uint8_t* bytes, const SectionInfo& info) {
  SectionArray out;
  // (a section of fractions is never stored in a coding; the original reads such a thing as whole numbers of 32 bits)
  out.type = info.t == SectionType::F32 ? SectionType::I32 : info.t;
  const size_t stride = info.s > 0 ? static_cast<size_t>(info.s) : 1;
  switch (out.type) {
    case SectionType::U8: decodeInto(bytes, info.n, info.c, stride, out.u8); break;
    case SectionType::I8: decodeInto(bytes, info.n, info.c, stride, out.i8); break;
    case SectionType::U16: decodeInto(bytes, info.n, info.c, stride, out.u16); break;
    case SectionType::I16: decodeInto(bytes, info.n, info.c, stride, out.i16); break;
    case SectionType::U32: decodeInto(bytes, info.n, info.c, stride, out.u32); break;
    default: decodeInto(bytes, info.n, info.c, stride, out.i32); break;
  }
  return out;
}

// ---------------------------------------------------------------------------
// the header
// ---------------------------------------------------------------------------

FS_JSON_DEFINE(SignPost, x, y, z, yaw, s, h, big)
FS_JSON_DEFINE(TileObject, k, x, y, z, yaw, p)
FS_JSON_DEFINE(TileHeader, v, ix, iz, cx, cz, yBase, size, terrainN, terrainMin, terrainScale, coverN, signs, objects, clr, sections)

const SectionInfo* SectionMap::find(std::string_view name) const {
  for (const value_type& item : items_)
    if (item.first == name) return &item.second;
  return nullptr;
}

void SectionMap::set(std::string_view name, const SectionInfo& info) {
  for (value_type& item : items_) {
    if (item.first == name) {
      item.second = info;
      return;
    }
  }
  items_.emplace_back(std::string(name), info);
}

void fromJson(JsonReader& r, SectionMap& v) {
  r.object([&](std::string_view name) {
    const std::string key(name);
    SectionInfo info;
    r.object([&](std::string_view k) {
      if (k == "off") fromJson(r, info.off);
      else if (k == "n") fromJson(r, info.n);
      else if (k == "t") info.t = sectionTypeOf(r.string());
      else if (k == "c") fromJson(r, info.c);
      else if (k == "s") fromJson(r, info.s);
    });
    v.set(key, info);
  });
}

void toJson(JsonWriter& w, const SectionMap& v) {
  w.beginObject();
  for (const auto& [name, info] : v) {
    w.key(name);
    w.beginObject();
    w.key("off");
    w.number(static_cast<double>(info.off));
    w.key("n");
    w.number(static_cast<double>(info.n));
    w.key("t");
    w.string(sectionTypeName(info.t));
    if (info.c != CODING_RAW) {
      w.key("c");
      w.number(info.c);
      w.key("s");
      w.number(info.s);
    }
    w.endObject();
  }
  w.endObject();
}

// ---------------------------------------------------------------------------
// packing
// ---------------------------------------------------------------------------

SectionData::SectionData(const SectionArray& a) : coded(encodeSection(a, CODING_RAW, 1)) {}

std::vector<uint8_t> packTile(const TileHeader& header, const SectionList& sections) {
  // (the header as an object, in the order the map build writes it)
  Json h;
  Json::parse(jsonText(header), h);
  return packSections(h, sections);
}

std::vector<uint8_t> packSections(const Json& header, const SectionList& sections) {
  SectionMap infos;
  std::vector<const CodedSection*> data;
  size_t off = 0;
  for (const auto& [name, arr] : sections) {
    const CodedSection& coded = arr.coded;
    const size_t n = coded.n;
    if (n == 0) continue;
    off = (off + 3) & ~static_cast<size_t>(3);
    SectionInfo info;
    info.off = off;
    info.n = n;
    info.t = coded.t;
    if (coded.c != CODING_RAW) {
      info.c = coded.c;
      info.s = coded.s;
    }
    // (a name given twice: the later one takes the place of the earlier, as in an object)
    const bool known = infos.has(name);
    infos.set(name, info);
    if (known) {
      size_t at = 0;
      for (const auto& item : infos) {
        if (item.first == name) break;
        at++;
      }
      data[at] = &coded;
    } else {
      data.push_back(&coded);
    }
    off += n * sectionBytes(coded.t);
  }
  const size_t payloadLen = (off + 3) & ~static_cast<size_t>(3);
  Json withSections = header.isObject() ? header : Json::makeObject();
  Json sectionsJson;
  Json::parse(jsonText(infos), sectionsJson);
  withSections.set("sections", std::move(sectionsJson));
  std::string json = withSections.stringify();
  const size_t pad = (4 - (json.size() & 3)) & 3;
  json.append(pad, ' ');
  std::vector<uint8_t> out(8 + json.size() + payloadLen, 0);
  out[0] = 0x46;  // "FSB1"
  out[1] = 0x53;
  out[2] = 0x42;
  out[3] = 0x31;
  const uint32_t jsonLen = static_cast<uint32_t>(json.size());
  out[4] = static_cast<uint8_t>(jsonLen & 255);
  out[5] = static_cast<uint8_t>((jsonLen >> 8) & 255);
  out[6] = static_cast<uint8_t>((jsonLen >> 16) & 255);
  out[7] = static_cast<uint8_t>((jsonLen >> 24) & 255);
  std::memcpy(out.data() + 8, json.data(), json.size());
  const size_t base = 8 + json.size();
  size_t at = 0;
  for (const auto& [name, info] : infos) {
    const CodedSection& coded = *data[at++];
    if (!coded.bytes.empty()) std::memcpy(out.data() + base + info.off, coded.bytes.data(), std::min(coded.bytes.size(), info.n * sectionBytes(info.t)));
  }
  return out;
}

// ---------------------------------------------------------------------------
// reading
// ---------------------------------------------------------------------------

UnpackedSections unpackSections(std::vector<uint8_t> buf) {
  if (buf.size() < 8 || buf[0] != 0x46 || buf[1] != 0x53 || buf[2] != 0x42 || buf[3] != 0x31) {
    throw std::runtime_error("not a map tile (bad magic)");
  }
  UnpackedSections u;
  u.buf_ = std::move(buf);
  const std::vector<uint8_t>& b = u.buf_;
  const size_t jsonLen = static_cast<size_t>(b[4]) | (static_cast<size_t>(b[5]) << 8) | (static_cast<size_t>(b[6]) << 16) | (static_cast<size_t>(b[7]) << 24);
  if (8 + jsonLen > b.size()) throw std::runtime_error("not a map tile (its header is cut off)");
  u.jsonLen_ = jsonLen;
  u.base_ = 8 + jsonLen;
  // (of the header only where the sections are is needed here; what else it says, the caller reads)
  JsonReader r(u.headerText());
  const bool isObject = r.object([&](std::string_view k) {
    if (k == "sections") fromJson(r, u.sections);
  });
  if (!r.ok() || !r.atEnd()) throw std::runtime_error("not a map tile (its header is not JSON)");
  if (!isObject) throw std::runtime_error("not a map tile (its header is not an object)");
  return u;
}

SectionArray* UnpackedSections::get(std::string_view name) {
  const SectionInfo* info = sections.find(name);
  if (!info) return nullptr;
  for (auto& made : made_)
    if (made.first == name) return made.second.get();
  const size_t o = base_ + info->off;
  const size_t length = info->n * sectionBytes(info->t);
  if (o > buf_.size() || length > buf_.size() - o) throw std::runtime_error("map tile: section \"" + std::string(name) + "\" reaches beyond the end of the file");
  auto a = std::make_unique<SectionArray>();
  const uint8_t* bytes = buf_.data() + o;
  if (info->c) {
    // (stored in a coding: decoded once, when first asked for)
    *a = decodeSection(bytes, *info);
  } else {
    a->type = info->t;
    switch (info->t) {
      case SectionType::U8: rawInto(bytes, info->n, a->u8); break;
      case SectionType::I8: rawInto(bytes, info->n, a->i8); break;
      case SectionType::U16: rawInto(bytes, info->n, a->u16); break;
      case SectionType::I16: rawInto(bytes, info->n, a->i16); break;
      case SectionType::U32: rawInto(bytes, info->n, a->u32); break;
      case SectionType::I32: rawInto(bytes, info->n, a->i32); break;
      default: rawInto(bytes, info->n, a->f32); break;
    }
  }
  made_.emplace_back(std::string(name), std::move(a));
  return made_.back().second.get();
}

UnpackedTile unpackTile(std::vector<uint8_t> buf) {
  UnpackedTile tile;
  tile.u_ = unpackSections(std::move(buf));
  if (!tile.u_.header(tile.header)) throw std::runtime_error("not a map tile (its header is not JSON)");
  return tile;
}

SectionArray* UnpackedTile::get(std::string_view name) {
  const TileHeader& h = header;
  // (what the faces of the buildings need for their facades follows from their corners: world/facade.h)
  if (name == "bldg_a16" && !h.sections.has("bldg_a16")) {
    if (!facesDone_) {
      facesDone_ = true;
      const SectionArray* pos = u_.get("bldg_pos");
      const SectionArray* run = u_.get("bldg_run");
      const SectionArray* uvx = u_.get("bldg_uvx");
      if (pos && run && pos->type == SectionType::I16 && run->type == SectionType::U16) {
        faces_ = std::make_unique<SectionArray>(unpackFaces(pos->i16, run->u16, uvx && uvx->type == SectionType::I16 ? &uvx->i16 : nullptr));
      }
    }
    return faces_.get();
  }
  // (what older tiles hold as plain numbers: x, y, z, height, kind per tree; x, z per corner)
  if (name == "trees" && !h.sections.has("trees")) {
    if (!treesDone_) {
      treesDone_ = true;
      const SectionArray* xz = u_.get("treeXZ");
      const SectionArray* q = u_.get("treeQ");
      const SectionArray* hk = u_.get("treeHK");
      const SectionArray* hgt = u_.get("hgt");
      if (xz && hk && hgt) {
        const size_t n = xz->size() / 2;
        std::vector<float> trees(n * 5);
        // (the heights are read as numbers whatever they are stored as; a grid that is cut short has none beyond its end)
        struct Heights {
          const SectionArray* a;
          double operator[](size_t i) const { return i < a->size() ? a->at(i) : JS_NAN; }
        };
        const Heights heights{hgt};
        const bool plain = hgt->type == SectionType::U16 && h.terrainN > 1 && hgt->u16.size() >= static_cast<size_t>(h.terrainN) * static_cast<size_t>(h.terrainN);
        for (size_t i = 0; i < n; i++) {
          const double x = h.cx + xz->at(i * 2) * TREE_SCALE;
          const double z = h.cz + xz->at(i * 2 + 1) * TREE_SCALE;
          trees[i * 5] = static_cast<float>(x);
          trees[i * 5 + 1] = static_cast<float>(plain ? tileGround(h, hgt->u16, x, z) : tileGround(h, heights, x, z));
          trees[i * 5 + 2] = static_cast<float>(z);
          trees[i * 5 + 3] = static_cast<float>((i < hk->size() ? hk->at(i) : JS_NAN) * TREE_HEIGHT_SCALE);
          trees[i * 5 + 4] = static_cast<float>(n + i < hk->size() ? hk->at(n + i) : JS_NAN);
        }
        trees_ = std::make_unique<SectionArray>(std::move(trees));
      } else if (q && hk) {
        const size_t n = q->size() / 3;
        std::vector<float> trees(n * 5);
        for (size_t i = 0; i < n; i++) {
          trees[i * 5] = static_cast<float>(h.cx + q->at(i * 3) * TREE_Q_SCALE);
          trees[i * 5 + 1] = static_cast<float>(h.yBase + q->at(i * 3 + 1) * TREE_Q_SCALE);
          trees[i * 5 + 2] = static_cast<float>(h.cz + q->at(i * 3 + 2) * TREE_Q_SCALE);
          trees[i * 5 + 3] = static_cast<float>((i < hk->size() ? hk->at(i) : JS_NAN) * TREE_HEIGHT_SCALE);
          trees[i * 5 + 4] = static_cast<float>(n + i < hk->size() ? hk->at(n + i) : JS_NAN);
        }
        trees_ = std::make_unique<SectionArray>(std::move(trees));
      }
    }
    return trees_.get();
  }
  if (name == "collPts" && !h.sections.has("collPts")) {
    if (!ringsDone_) {
      ringsDone_ = true;
      const SectionArray* q = u_.get("collQ");
      if (q) {
        std::vector<float> rings(q->size());
        for (size_t i = 0; i < q->size(); i += 2) {
          rings[i] = static_cast<float>(h.cx + q->at(i) * RING_SCALE);
          if (i + 1 < q->size()) rings[i + 1] = static_cast<float>(h.cz + q->at(i + 1) * RING_SCALE);
        }
        rings_ = std::make_unique<SectionArray>(std::move(rings));
      }
    }
    return rings_.get();
  }
  return u_.get(name);
}

std::string tileKey(int ix, int iz) { return std::to_string(ix) + "_" + std::to_string(iz); }

}  // namespace fs

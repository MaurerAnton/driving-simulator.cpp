// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Binary container for map tiles (public/map/tiles/<ix>_<iz>.fsb).
 *
 * Origin: src/world/tilefmt.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).  The JSON header is parsed with a small dedicated reader: the
 * game only needs the numbers and the section table, and keeping the reader
 * local avoids pulling a JSON library into the WASM build.
 */
#include "world/tilefmt.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "world/facade.h"

namespace fs {

namespace {

// ---------------------------------------------------------------------------
// Minimal JSON reader for the tile header
// ---------------------------------------------------------------------------

class JsonReader {
 public:
  explicit JsonReader(const std::string& text) : s_(text) {}

  void skipWs() {
    while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' || s_[i_] == '\r' || s_[i_] == '\t')) i_++;
  }

  char peek() {
    skipWs();
    return i_ < s_.size() ? s_[i_] : '\0';
  }

  void expect(char c) {
    skipWs();
    if (i_ >= s_.size() || s_[i_] != c)
      throw std::runtime_error(std::string("tile header: expected '") + c + "'");
    i_++;
  }

  bool tryConsume(char c) {
    skipWs();
    if (i_ < s_.size() && s_[i_] == c) {
      i_++;
      return true;
    }
    return false;
  }

  std::string parseString() {
    expect('"');
    std::string out;
    while (i_ < s_.size() && s_[i_] != '"') {
      if (s_[i_] == '\\' && i_ + 1 < s_.size()) {
        i_++;
        char c = s_[i_];
        switch (c) {
          case 'n': out += '\n'; break;
          case 't': out += '\t'; break;
          case 'r': out += '\r'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'u': {
            // \uXXXX — decode into UTF-8 (the header only holds sign codes
            // and names, so a simple BMP decoder is enough)
            if (i_ + 4 < s_.size()) {
              unsigned code = 0;
              for (int k = 1; k <= 4; k++) {
                char h = s_[i_ + static_cast<size_t>(k)];
                code = code * 16 + static_cast<unsigned>(h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
              }
              i_ += 4;
              if (code < 0x80) out += static_cast<char>(code);
              else if (code < 0x800) {
                out += static_cast<char>(0xC0 | (code >> 6));
                out += static_cast<char>(0x80 | (code & 0x3F));
              } else {
                out += static_cast<char>(0xE0 | (code >> 12));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
              }
            }
            break;
          }
          default: out += c; break;
        }
      } else {
        out += s_[i_];
      }
      i_++;
    }
    expect('"');
    return out;
  }

  double parseNumber() {
    skipWs();
    size_t start = i_;
    if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) i_++;
    while (i_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[i_])) || s_[i_] == '.' ||
                              s_[i_] == 'e' || s_[i_] == 'E' || s_[i_] == '-' || s_[i_] == '+'))
      i_++;
    return std::strtod(s_.substr(start, i_ - start).c_str(), nullptr);
  }

  bool parseBool() {
    skipWs();
    if (s_.compare(i_, 4, "true") == 0) {
      i_ += 4;
      return true;
    }
    if (s_.compare(i_, 5, "false") == 0) {
      i_ += 5;
      return false;
    }
    return false;
  }

  void skipValue() {
    char c = peek();
    if (c == '{') {
      expect('{');
      if (tryConsume('}')) return;
      for (;;) {
        parseString();
        expect(':');
        skipValue();
        if (tryConsume(',')) continue;
        expect('}');
        return;
      }
    }
    if (c == '[') {
      expect('[');
      if (tryConsume(']')) return;
      for (;;) {
        skipValue();
        if (tryConsume(',')) continue;
        expect(']');
        return;
      }
    }
    if (c == '"') parseString();
    else if (c == 't' || c == 'f') parseBool();
    else if (c == 'n') i_ += 4;  // null
    else parseNumber();
  }

  std::string parseKey() { return parseString(); }

 private:
  std::string s_;
  size_t i_ = 0;
};

SectionType typeOf(const std::string& t) {
  if (t == "u8") return SectionType::U8;
  if (t == "i8") return SectionType::I8;
  if (t == "u16") return SectionType::U16;
  if (t == "i16") return SectionType::I16;
  if (t == "u32") return SectionType::U32;
  if (t == "i32") return SectionType::I32;
  return SectionType::F32;
}

/** The bytes one element of `t` takes. */
size_t typeBytes(SectionType t);

/** Wrap a step to the width of the element (the typed arrays wrap their sums). */
int64_t wrapTo(int64_t d, size_t bytes) {
  if (bytes == 4) return static_cast<int32_t>(static_cast<uint32_t>(d));
  if (bytes == 2) return static_cast<int16_t>(static_cast<uint16_t>(d));
  return static_cast<int8_t>(static_cast<uint8_t>(d));
}

/** Zigzag: the sign in the low bit (as the map build writes it). */
uint64_t zigTo(int64_t d, size_t bytes) {
  uint64_t u = static_cast<uint64_t>(d);
  if (bytes == 4) return (static_cast<uint32_t>(d << 1) ^ static_cast<uint32_t>(d >> 31));
  if (bytes == 2) return (static_cast<uint16_t>(d << 1) ^ static_cast<uint16_t>(d >> 15));
  return (static_cast<uint8_t>(d << 1) ^ static_cast<uint8_t>(d >> 7));
}

int64_t unzig(uint64_t u) { return static_cast<int64_t>((u >> 1) ^ (~(u & 1) + 1)); }

/**
 * Decode a section stored in a coding into the plain numbers (the JS
 * decodeSection): PLANES/DELTA component by component, GRID as the
 * differences from what the neighbours say.
 */
void decodeSectionImpl(const uint8_t* bytes, const SectionInfo& info, std::vector<double>& out) {
  const size_t B = typeBytes(info.t);
  const size_t n = info.n;
  const int coding = info.c;
  const size_t stride = static_cast<size_t>(std::max(1, info.s));
  out.assign(n, 0);
  const size_t groups = coding == CODING_GRID ? 1 : stride;
  if (groups == 0) return;
  const size_t per = n / groups;
  auto word = [&](size_t base, size_t i) -> uint64_t {
    uint64_t v = bytes[base + i];
    if (B > 1) v |= static_cast<uint64_t>(bytes[base + per + i]) << 8;
    if (B > 2) v |= static_cast<uint64_t>(bytes[base + 2 * per + i]) << 16;
    if (B > 3) v |= static_cast<uint64_t>(bytes[base + 3 * per + i]) << 24;
    return v;
  };
  for (size_t c = 0; c < groups; c++) {
    const size_t base = c * B * per;
    int64_t prev = 0;
    for (size_t i = 0; i < per; i++) {
      uint64_t v = word(base, i);
      if (coding == CODING_GRID) {
        const size_t col = i % stride;
        const double left = col > 0 ? out[i - 1] : 0;
        const double up = i >= stride ? out[i - stride] : 0;
        const double ul = col > 0 && i >= stride ? out[i - stride - 1] : 0;
        out[i] = static_cast<double>(unzig(v)) + left + up - ul;
      } else if (coding == CODING_DELTA) {
        // (the element wraps the sum the way the steps were wrapped)
        int64_t sum = wrapTo(prev + unzig(v), B);
        out[c * per + i] = static_cast<double>(sum);
        prev = sum;
      } else {
        out[c * per + i] = static_cast<double>(v);
      }
    }
  }
}

/** The bytes one element of `t` takes. */
size_t typeBytes(SectionType t) {
  switch (t) {
    case SectionType::U8:
    case SectionType::I8: return 1;
    case SectionType::U16:
    case SectionType::I16: return 2;
    default: return 4;
  }
}

const char* typeName(SectionType t) {
  switch (t) {
    case SectionType::U8: return "u8";
    case SectionType::I8: return "i8";
    case SectionType::U16: return "u16";
    case SectionType::I16: return "i16";
    case SectionType::U32: return "u32";
    case SectionType::I32: return "i32";
    case SectionType::F32: return "f32";
  }
  return "u8";
}

}  // namespace

void decodeSectionBytes(const uint8_t* bytes, const SectionInfo& info, std::vector<double>& out) {
  decodeSectionImpl(bytes, info, out);
}

UnpackedTile unpackTile(const std::vector<uint8_t>& buf) {
  if (buf.size() < 8 || buf[0] != 0x46 || buf[1] != 0x53 || buf[2] != 0x42 || buf[3] != 0x31)
    throw std::runtime_error("not a map tile (bad magic)");
  uint32_t jsonLen = 0;
  std::memcpy(&jsonLen, buf.data() + 4, 4);
  if (8u + jsonLen > buf.size()) throw std::runtime_error("tile header truncated");
  std::string json(reinterpret_cast<const char*>(buf.data() + 8), jsonLen);
  // the header is padded to 4 bytes with spaces
  while (!json.empty() && json.back() == ' ') json.pop_back();

  UnpackedTile tile;
  TileHeader& h = tile.header;
  JsonReader r(json);
  r.expect('{');
  if (r.tryConsume('}')) return tile;
  for (;;) {
    std::string key = r.parseKey();
    r.expect(':');
    if (key == "v") h.v = static_cast<int>(r.parseNumber());
    else if (key == "ix") h.ix = static_cast<int>(r.parseNumber());
    else if (key == "iz") h.iz = static_cast<int>(r.parseNumber());
    else if (key == "cx") h.cx = r.parseNumber();
    else if (key == "cz") h.cz = r.parseNumber();
    else if (key == "yBase") h.yBase = r.parseNumber();
    else if (key == "size") h.size = r.parseNumber();
    else if (key == "terrainN") h.terrainN = static_cast<int>(r.parseNumber());
    else if (key == "terrainMin") h.terrainMin = r.parseNumber();
    else if (key == "terrainScale") h.terrainScale = r.parseNumber();
    else if (key == "coverN") h.coverN = static_cast<int>(r.parseNumber());
    else if (key == "clr") h.clr = static_cast<int>(r.parseNumber());
    else if (key == "signs") {
      r.expect('[');
      if (!r.tryConsume(']')) {
        for (;;) {
          SignPost sp;
          r.expect('{');
          if (!r.tryConsume('}')) {
            for (;;) {
              std::string k = r.parseKey();
              r.expect(':');
              if (k == "x") sp.x = r.parseNumber();
              else if (k == "y") sp.y = r.parseNumber();
              else if (k == "z") sp.z = r.parseNumber();
              else if (k == "yaw") sp.yaw = r.parseNumber();
              else if (k == "h") sp.h = r.parseNumber();
              else if (k == "big") sp.big = static_cast<int>(r.parseNumber());
              else if (k == "s") {
                r.expect('[');
                if (!r.tryConsume(']')) {
                  for (;;) {
                    sp.s.push_back(r.parseString());
                    if (r.tryConsume(',')) continue;
                    r.expect(']');
                    break;
                  }
                }
              } else r.skipValue();
              if (r.tryConsume(',')) continue;
              r.expect('}');
              break;
            }
          }
          h.signs.push_back(sp);
          if (r.tryConsume(',')) continue;
          r.expect(']');
          break;
        }
      }
    } else if (key == "objects") {
      r.expect('[');
      if (!r.tryConsume(']')) {
        for (;;) {
          TileObject o;
          r.expect('{');
          if (!r.tryConsume('}')) {
            for (;;) {
              std::string k = r.parseKey();
              r.expect(':');
              if (k == "k") o.k = r.parseString();
              else if (k == "x") o.x = r.parseNumber();
              else if (k == "y") o.y = r.parseNumber();
              else if (k == "z") o.z = r.parseNumber();
              else if (k == "yaw") {
                o.yaw = r.parseNumber();
                o.hasYaw = true;
              } else if (k == "p") o.p = r.parseString();
              else r.skipValue();
              if (r.tryConsume(',')) continue;
              r.expect('}');
              break;
            }
          }
          h.objects.push_back(o);
          if (r.tryConsume(',')) continue;
          r.expect(']');
          break;
        }
      }
    } else if (key == "sections") {
      r.expect('{');
      if (!r.tryConsume('}')) {
        for (;;) {
          std::string name = r.parseKey();
          r.expect(':');
          SectionInfo info;
          r.expect('{');
          if (!r.tryConsume('}')) {
            for (;;) {
              std::string k = r.parseKey();
              r.expect(':');
              if (k == "off") info.off = static_cast<size_t>(r.parseNumber());
              else if (k == "n") info.n = static_cast<size_t>(r.parseNumber());
              else if (k == "t") info.t = typeOf(r.parseString());
              else if (k == "c") info.c = static_cast<int>(r.parseNumber());
              else if (k == "s") info.s = static_cast<int>(r.parseNumber());
              else r.skipValue();
              if (r.tryConsume(',')) continue;
              r.expect('}');
              break;
            }
          }
          h.sections[name] = info;
          if (r.tryConsume(',')) continue;
          r.expect('}');
          break;
        }
      }
    } else {
      r.skipValue();
    }
    if (r.tryConsume(',')) continue;
    r.expect('}');
    break;
  }

  const size_t base = 8 + jsonLen;
  for (const auto& kv : h.sections) {
    const SectionInfo& info = kv.second;
    size_t bytes = info.n * typeBytes(info.t);
    if (base + info.off + bytes > buf.size()) continue;
    std::vector<double> out;
    const uint8_t* p = buf.data() + base + info.off;
    if (info.c != CODING_RAW) {
      // stored in a coding: decoded here (the JS does it when first asked for)
      decodeSectionBytes(p, info, out);
      tile.sections[kv.first] = std::move(out);
      continue;
    }
    out.reserve(info.n);
    switch (info.t) {
      case SectionType::U8:
        for (size_t i = 0; i < info.n; i++) out.push_back(p[i]);
        break;
      case SectionType::I8:
        for (size_t i = 0; i < info.n; i++) out.push_back(static_cast<int8_t>(p[i]));
        break;
      case SectionType::U16:
        for (size_t i = 0; i < info.n; i++) {
          uint16_t v;
          std::memcpy(&v, p + i * 2, 2);
          out.push_back(v);
        }
        break;
      case SectionType::I16:
        for (size_t i = 0; i < info.n; i++) {
          int16_t v;
          std::memcpy(&v, p + i * 2, 2);
          out.push_back(v);
        }
        break;
      case SectionType::U32:
        for (size_t i = 0; i < info.n; i++) {
          uint32_t v;
          std::memcpy(&v, p + i * 4, 4);
          out.push_back(v);
        }
        break;
      case SectionType::I32:
        for (size_t i = 0; i < info.n; i++) {
          int32_t v;
          std::memcpy(&v, p + i * 4, 4);
          out.push_back(v);
        }
        break;
      case SectionType::F32:
        for (size_t i = 0; i < info.n; i++) {
          float v;
          std::memcpy(&v, p + i * 4, 4);
          out.push_back(v);
        }
        break;
    }
    tile.sections[kv.first] = std::move(out);
  }

  // What older tiles hold as plain numbers is worked out from what the new
  // ones store instead (the JS does it in get(), when first asked for):
  // the faces of the buildings, the trees, and the outlines of buildings.
  if (!tile.sections.count("bldg_a16")) {
    const std::vector<double>* pos = tile.get("bldg_pos");
    const std::vector<double>* run = tile.get("bldg_run");
    const std::vector<double>* uvx = tile.get("bldg_uvx");
    if (pos && run) {
      std::vector<int16_t> pos16(pos->size());
      for (size_t i = 0; i < pos->size(); i++)
        pos16[i] = static_cast<int16_t>((*pos)[i]);
      std::vector<uint16_t> run16(run->size());
      for (size_t i = 0; i < run->size(); i++)
        run16[i] = static_cast<uint16_t>((*run)[i]);
      std::vector<int16_t> uvx16;
      if (uvx) {
        uvx16.resize(uvx->size());
        for (size_t i = 0; i < uvx->size(); i++)
          uvx16[i] = static_cast<int16_t>((*uvx)[i]);
      }
      std::vector<double> posd(pos->size());
      for (size_t i = 0; i < pos->size(); i++) posd[i] = (*pos)[i];
      std::vector<int16_t> faces = unpackFaces(posd, run16, uvx ? &uvx16 : nullptr);
      std::vector<double> out(faces.size());
      for (size_t i = 0; i < faces.size(); i++) out[i] = faces[i];
      tile.sections["bldg_a16"] = std::move(out);
    }
  }
  if (!tile.sections.count("trees")) {
    const std::vector<double>* xz = tile.get("treeXZ");
    const std::vector<double>* q = tile.get("treeQ");
    const std::vector<double>* hk = tile.get("treeHK");
    const std::vector<double>* hgt = tile.get("hgt");
    std::vector<double> trees;
    if (xz && hk && hgt) {
      const size_t n = xz->size() / 2;
      trees.resize(n * 5);
      for (size_t i = 0; i < n; i++) {
        const double x = h.cx + (*xz)[i * 2] * TREE_SCALE;
        const double z = h.cz + (*xz)[i * 2 + 1] * TREE_SCALE;
        trees[i * 5] = x;
        trees[i * 5 + 1] = tileGround(h, *hgt, x, z);
        trees[i * 5 + 2] = z;
        trees[i * 5 + 3] = (*hk)[i] * TREE_HEIGHT_SCALE;
        trees[i * 5 + 4] = (*hk)[n + i];
      }
    } else if (q && hk) {
      const size_t n = q->size() / 3;
      trees.resize(n * 5);
      for (size_t i = 0; i < n; i++) {
        trees[i * 5] = h.cx + (*q)[i * 3] * TREE_Q_SCALE;
        trees[i * 5 + 1] = h.yBase + (*q)[i * 3 + 1] * TREE_Q_SCALE;
        trees[i * 5 + 2] = h.cz + (*q)[i * 3 + 2] * TREE_Q_SCALE;
        trees[i * 5 + 3] = (*hk)[i] * TREE_HEIGHT_SCALE;
        trees[i * 5 + 4] = (*hk)[n + i];
      }
    }
    if (!trees.empty()) tile.sections["trees"] = std::move(trees);
  }
  if (!tile.sections.count("collPts")) {
    const std::vector<double>* q = tile.get("collQ");
    if (q) {
      std::vector<double> rings(q->size());
      for (size_t i = 0; i < q->size(); i += 2) {
        rings[i] = h.cx + (*q)[i] * RING_SCALE;
        rings[i + 1] = h.cz + (*q)[i + 1] * RING_SCALE;
      }
      tile.sections["collPts"] = std::move(rings);
    }
  }
  return tile;
}

double tileGround(const TileHeader& h, const std::vector<double>& hgt, double x, double z) {
  const int n = h.terrainN;
  if (n < 2 || hgt.size() < static_cast<size_t>(n) * n) return 0;
  const double step = h.size / (n - 1);
  const double fx = std::max(0.0, std::min(n - 1.0001, (x - (h.cx - h.size / 2)) / step));
  const double fz = std::max(0.0, std::min(n - 1.0001, (z - (h.cz - h.size / 2)) / step));
  const int i = static_cast<int>(std::floor(fx));
  const int j = static_cast<int>(std::floor(fz));
  const double tx = fx - i;
  const double tz = fz - j;
  const size_t k = static_cast<size_t>(j) * n + i;
  const double h00 = hgt[k];
  const double h10 = hgt[k + 1];
  const double h01 = hgt[k + static_cast<size_t>(n)];
  const double h11 = hgt[k + static_cast<size_t>(n) + 1];
  const double v = tx + tz <= 1
                       ? h00 + (h10 - h00) * tx + (h01 - h00) * tz
                       : h11 + (h01 - h11) * (1 - tx) + (h10 - h11) * (1 - tz);
  return h.terrainMin + v * h.terrainScale;
}

void encodeSectionBytes(const std::vector<double>& values, SectionType t, int coding, int stride,
                        std::vector<uint8_t>& out) {
  const size_t B = typeBytes(t);
  const size_t n = values.size();
  const size_t st = static_cast<size_t>(std::max(1, stride));
  out.assign(n * B, 0);
  if (coding == CODING_RAW) {
    for (size_t i = 0; i < n; i++) {
      double v = values[i];
      if (B == 1) {
        out[i] = static_cast<uint8_t>(v);
      } else if (B == 2) {
        uint16_t u = static_cast<uint16_t>(static_cast<int64_t>(v));
        std::memcpy(out.data() + i * 2, &u, 2);
      } else if (t == SectionType::F32) {
        float f = static_cast<float>(v);
        std::memcpy(out.data() + i * 4, &f, 4);
      } else {
        uint32_t u = static_cast<uint32_t>(static_cast<int64_t>(v));
        std::memcpy(out.data() + i * 4, &u, 4);
      }
    }
    return;
  }
  // the steps (or the differences from the neighbours), low bytes first
  std::vector<uint64_t> vals(n, 0);
  if (coding == CODING_GRID) {
    for (size_t i = 0; i < n; i++) {
      const size_t col = i % st;
      const int64_t left = col > 0 ? static_cast<int64_t>(values[i - 1]) : 0;
      const int64_t up = i >= st ? static_cast<int64_t>(values[i - st]) : 0;
      const int64_t ul = col > 0 && i >= st ? static_cast<int64_t>(values[i - st - 1]) : 0;
      vals[i] = zigTo(static_cast<int64_t>(values[i]) - (left + up - ul), B);
    }
  } else {
    const size_t groups = st;
    const size_t per = n / groups;
    for (size_t c = 0; c < groups; c++) {
      int64_t prev = 0;
      for (size_t i = 0; i < per; i++) {
        const int64_t v = static_cast<int64_t>(values[i * st + c]);
        vals[c * per + i] =
            coding == CODING_DELTA ? zigTo(wrapTo(v - prev, B), B)
                                   : static_cast<uint64_t>(v) &
                                         (B == 4 ? 0xffffffffu : B == 2 ? 0xffffu : 0xffu);
        prev = v;
      }
    }
  }
  size_t o = 0;
  const size_t groups = coding == CODING_GRID ? 1 : st;
  const size_t per = n / groups;
  for (size_t c = 0; c < groups; c++)
    for (size_t b = 0; b < B; b++)
      for (size_t i = 0; i < per; i++)
        out[o++] = static_cast<uint8_t>((vals[c * per + i] >> (8 * b)) & 255);
}

int tileIndex(double v, double min, double size) {
  return static_cast<int>(std::floor((v - min) / size));
}

std::string tileKey(int ix, int iz) {
  return std::to_string(ix) + "_" + std::to_string(iz);
}

std::vector<uint8_t> packTile(const TileHeader& headerWithoutSections,
                              const std::map<std::string, std::vector<double>>& sections) {
  // layout: sections are 4-byte aligned, payload follows the padded JSON
  std::map<std::string, SectionInfo> infos;
  size_t off = 0;
  for (const auto& kv : sections) {
    if (kv.second.empty()) continue;
    SectionType t = SectionType::F32;  // the packer writes doubles as f32
    off = (off + 3) & ~size_t(3);
    SectionInfo info;
    info.off = off;
    info.n = kv.second.size();
    info.t = t;
    infos[kv.first] = info;
    off += info.n * typeBytes(t);
  }
  size_t payloadLen = (off + 3) & ~size_t(3);

  // build the JSON header
  char buf[128];
  std::string json = "{";
  json += "\"v\":" + std::to_string(headerWithoutSections.v);
  json += ",\"ix\":" + std::to_string(headerWithoutSections.ix);
  json += ",\"iz\":" + std::to_string(headerWithoutSections.iz);
  auto num = [&](const char* k, double v) {
    std::snprintf(buf, sizeof buf, ",\"%s\":%.10g", k, v);
    json += buf;
  };
  num("cx", headerWithoutSections.cx);
  num("cz", headerWithoutSections.cz);
  num("yBase", headerWithoutSections.yBase);
  num("size", headerWithoutSections.size);
  num("terrainN", headerWithoutSections.terrainN);
  num("terrainMin", headerWithoutSections.terrainMin);
  num("terrainScale", headerWithoutSections.terrainScale);
  num("coverN", headerWithoutSections.coverN);
  if (headerWithoutSections.clr) json += ",\"clr\":1";
  json += ",\"signs\":[";
  for (size_t i = 0; i < headerWithoutSections.signs.size(); i++) {
    const SignPost& sp = headerWithoutSections.signs[i];
    if (i) json += ",";
    json += "{\"x\":" + std::to_string(sp.x) + ",\"y\":" + std::to_string(sp.y) +
            ",\"z\":" + std::to_string(sp.z) + ",\"yaw\":" + std::to_string(sp.yaw) +
            ",\"h\":" + std::to_string(sp.h) + ",\"big\":" + std::to_string(sp.big) + ",\"s\":[";
    for (size_t k = 0; k < sp.s.size(); k++) {
      if (k) json += ",";
      json += "\"" + sp.s[k] + "\"";
    }
    json += "]}";
  }
  json += "],\"objects\":[";
  for (size_t i = 0; i < headerWithoutSections.objects.size(); i++) {
    const TileObject& o = headerWithoutSections.objects[i];
    if (i) json += ",";
    json += "{\"k\":\"" + o.k + "\",\"x\":" + std::to_string(o.x) + ",\"y\":" + std::to_string(o.y) +
            ",\"z\":" + std::to_string(o.z);
    if (o.hasYaw) json += ",\"yaw\":" + std::to_string(o.yaw);
    if (!o.p.empty()) json += ",\"p\":\"" + o.p + "\"";
    json += "}";
  }
  json += "],\"sections\":{";
  bool first = true;
  for (const auto& kv : infos) {
    if (!first) json += ",";
    first = false;
    json += "\"" + kv.first + "\":{\"off\":" + std::to_string(kv.second.off) +
            ",\"n\":" + std::to_string(kv.second.n) + ",\"t\":\"" + typeName(kv.second.t) + "\"}";
  }
  json += "}}";
  size_t pad = (4 - (json.size() & 3)) & 3;
  json.append(pad, ' ');

  std::vector<uint8_t> out(8 + json.size() + payloadLen, 0);
  out[0] = 0x46;
  out[1] = 0x53;
  out[2] = 0x42;
  out[3] = 0x31;
  uint32_t jsonLen = static_cast<uint32_t>(json.size());
  std::memcpy(out.data() + 4, &jsonLen, 4);
  std::memcpy(out.data() + 8, json.data(), json.size());
  size_t base = 8 + json.size();
  for (const auto& kv : sections) {
    auto it = infos.find(kv.first);
    if (it == infos.end()) continue;
    const SectionInfo& info = it->second;
    uint8_t* p = out.data() + base + info.off;
    for (size_t i = 0; i < info.n; i++) {
      float v = static_cast<float>(kv.second[i]);
      std::memcpy(p + i * 4, &v, 4);
    }
  }
  return out;
}

std::vector<uint8_t> packSections(const std::string& json,
                                  const std::map<std::string, std::vector<double>>& sections) {
  std::map<std::string, SectionInfo> infos;
  size_t off = 0;
  for (const auto& kv : sections) {
    if (kv.second.empty()) continue;
    SectionType t = SectionType::F32;  // the packer writes doubles as f32
    off = (off + 3) & ~size_t(3);
    SectionInfo info;
    info.off = off;
    info.n = kv.second.size();
    info.t = t;
    infos[kv.first] = info;
    off += info.n * typeBytes(t);
  }
  const size_t payloadLen = (off + 3) & ~size_t(3);
  std::string head = json;
  // the section table joins the header JSON as `sections`
  if (!head.empty() && head.back() == '}') head.pop_back();
  if (!head.empty() && head.back() != ',') head += ',';
  head += "\"sections\":{";
  bool first = true;
  for (const auto& kv : infos) {
    if (!first) head += ",";
    first = false;
    head += "\"" + kv.first + "\":{\"off\":" + std::to_string(kv.second.off) +
            ",\"n\":" + std::to_string(kv.second.n) + ",\"t\":\"" +
            typeName(kv.second.t) + "\"}";
  }
  head += "}}";
  // pad the JSON to 4 bytes with spaces (as the map build does)
  while (head.size() % 4) head += ' ';

  std::vector<uint8_t> out(8 + head.size() + payloadLen, 0);
  out[0] = 0x46;
  out[1] = 0x53;
  out[2] = 0x42;
  out[3] = 0x31;
  const uint32_t jsonLen = static_cast<uint32_t>(head.size());
  std::memcpy(out.data() + 4, &jsonLen, 4);
  std::memcpy(out.data() + 8, head.data(), head.size());
  const size_t base = 8 + head.size();
  for (const auto& kv : infos) {
    const SectionInfo& info = kv.second;
    uint8_t* p = out.data() + base + info.off;
    const std::vector<double>& v = sections.at(kv.first);
    for (size_t i = 0; i < info.n; i++) {
      float f = static_cast<float>(v[i]);
      std::memcpy(p + i * 4, &f, 4);
    }
  }
  return out;
}

UnpackedSections unpackSections(const std::vector<uint8_t>& buf) {
  if (buf.size() < 8 || buf[0] != 0x46 || buf[1] != 0x53 || buf[2] != 0x42 || buf[3] != 0x31)
    throw std::runtime_error("not a map tile (bad magic)");
  uint32_t jsonLen = 0;
  std::memcpy(&jsonLen, buf.data() + 4, 4);
  if (8u + jsonLen > buf.size()) throw std::runtime_error("tile header truncated");
  std::string json(reinterpret_cast<const char*>(buf.data() + 8), jsonLen);
  while (!json.empty() && json.back() == ' ') json.pop_back();

  UnpackedSections u;
  u.json = json;
  JsonReader r(json);
  r.expect('{');
  std::map<std::string, SectionInfo> table;
  if (r.tryConsume('}')) return u;
  for (;;) {
    std::string key = r.parseKey();
    r.expect(':');
    if (key == "sections") {
      r.expect('{');
      if (!r.tryConsume('}')) {
        for (;;) {
          std::string name = r.parseKey();
          r.expect(':');
          SectionInfo info;
          r.expect('{');
          if (!r.tryConsume('}')) {
            for (;;) {
              std::string k = r.parseKey();
              r.expect(':');
              if (k == "off") info.off = static_cast<size_t>(r.parseNumber());
              else if (k == "n") info.n = static_cast<size_t>(r.parseNumber());
              else if (k == "t") info.t = typeOf(r.parseString());
              else if (k == "c") info.c = static_cast<int>(r.parseNumber());
              else if (k == "s") info.s = static_cast<int>(r.parseNumber());
              else r.skipValue();
              if (r.tryConsume(',')) continue;
              r.expect('}');
              break;
            }
          }
          table[name] = info;
          if (r.tryConsume(',')) continue;
          r.expect('}');
          break;
        }
      }
    } else {
      r.skipValue();
    }
    if (r.tryConsume(',')) continue;
    r.expect('}');
    break;
  }

  const size_t base = 8 + jsonLen;
  for (const auto& kv : table) {
    const SectionInfo& info = kv.second;
    const size_t bytes = info.n * typeBytes(info.t);
    if (base + info.off + bytes > buf.size()) continue;
    const uint8_t* p = buf.data() + base + info.off;
    std::vector<double> out;
    if (info.c != CODING_RAW) {
      decodeSectionBytes(p, info, out);
      u.sections[kv.first] = std::move(out);
      continue;
    }
    out.reserve(info.n);
    switch (info.t) {
      case SectionType::U8:
        for (size_t i = 0; i < info.n; i++) out.push_back(p[i]);
        break;
      case SectionType::I8:
        for (size_t i = 0; i < info.n; i++) out.push_back(static_cast<int8_t>(p[i]));
        break;
      case SectionType::U16:
        for (size_t i = 0; i < info.n; i++) {
          uint16_t v;
          std::memcpy(&v, p + i * 2, 2);
          out.push_back(v);
        }
        break;
      case SectionType::I16:
        for (size_t i = 0; i < info.n; i++) {
          int16_t v;
          std::memcpy(&v, p + i * 2, 2);
          out.push_back(v);
        }
        break;
      case SectionType::U32:
        for (size_t i = 0; i < info.n; i++) {
          uint32_t v;
          std::memcpy(&v, p + i * 4, 4);
          out.push_back(v);
        }
        break;
      case SectionType::I32:
        for (size_t i = 0; i < info.n; i++) {
          int32_t v;
          std::memcpy(&v, p + i * 4, 4);
          out.push_back(v);
        }
        break;
      case SectionType::F32:
        for (size_t i = 0; i < info.n; i++) {
          float v;
          std::memcpy(&v, p + i * 4, 4);
          out.push_back(v);
        }
        break;
    }
    u.sections[kv.first] = std::move(out);
  }
  return u;
}

}  // namespace fs

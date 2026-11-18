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

size_t typeBytes(SectionType t) {
  switch (t) {
    case SectionType::U8:
    case SectionType::I8: return 1;
    case SectionType::U16:
    case SectionType::I16: return 2;
    default: return 4;
  }
}

}  // namespace

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
    out.reserve(info.n);
    const uint8_t* p = buf.data() + base + info.off;
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
  return tile;
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

}  // namespace fs

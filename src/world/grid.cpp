// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/grid.ts @ v1.7.7
/**
 * The grids the map is cut into: the square numbers, and the sets of squares
 * that exist (base64 bit fields, as tools/build-map.ts writes them).
 */
#include "world/grid.h"

#include <algorithm>
#include <limits>

namespace fs {

namespace {
const char* const kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string base64Encode(const std::vector<uint8_t>& bytes) {
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  size_t i = 0;
  while (i + 2 < bytes.size()) {
    unsigned v = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
    out += kAlphabet[(v >> 18) & 63];
    out += kAlphabet[(v >> 12) & 63];
    out += kAlphabet[(v >> 6) & 63];
    out += kAlphabet[v & 63];
    i += 3;
  }
  if (i + 1 == bytes.size()) {
    unsigned v = bytes[i] << 16;
    out += kAlphabet[(v >> 18) & 63];
    out += kAlphabet[(v >> 12) & 63];
    out += "==";
  } else if (i + 2 == bytes.size()) {
    unsigned v = (bytes[i] << 16) | (bytes[i + 1] << 8);
    out += kAlphabet[(v >> 18) & 63];
    out += kAlphabet[(v >> 12) & 63];
    out += kAlphabet[(v >> 6) & 63];
    out += '=';
  }
  return out;
}

std::vector<uint8_t> base64Decode(const std::string& text) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::vector<uint8_t> out;
  if (text.size() % 4 != 0) return out;
  unsigned acc = 0;
  int bits = 0;
  for (char c : text) {
    if (c == '=') break;
    int v = val(c);
    if (v < 0) return {};
    acc = ((acc << 6) | static_cast<unsigned>(v)) & 0xffffffu;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8_t>((acc >> bits) & 0xff));
    }
  }
  return out;
}

GridSet::GridSet(int x0_, int z0_, int w_, int h_)
    : x0(x0_), z0(z0_), w(w_), h(h_), bytes_((static_cast<size_t>(w_) * static_cast<size_t>(h_) + 7) / 8, 0) {}

GridSet::GridSet(int x0_, int z0_, int w_, int h_, std::vector<uint8_t> bytes) : x0(x0_), z0(z0_), w(w_), h(h_), bytes_(std::move(bytes)) {}

GridSet GridSet::from(const GridBits& json) { return GridSet(json.x0, json.z0, json.w, json.h, base64Decode(json.bits)); }

GridSet GridSet::of(const std::vector<std::pair<int, int>>& squares) {
  int x0 = std::numeric_limits<int>::max();
  int z0 = std::numeric_limits<int>::max();
  int x1 = std::numeric_limits<int>::min();
  int z1 = std::numeric_limits<int>::min();
  for (const auto& [x, z] : squares) {
    x0 = std::min(x0, x);
    x1 = std::max(x1, x);
    z0 = std::min(z0, z);
    z1 = std::max(z1, z);
  }
  GridSet set = squares.empty() ? GridSet(0, 0, 0, 0) : GridSet(x0, z0, x1 - x0 + 1, z1 - z0 + 1);
  for (const auto& [x, z] : squares) set.add(x, z);
  return set;
}

void GridSet::add(int ix, int iz) {
  const int64_t k = static_cast<int64_t>(iz - z0) * w + (ix - x0);
  // (a square outside the rectangle has no bit: as a write beyond the end of the original's byte array, nothing happens)
  if (k < 0 || static_cast<size_t>(k >> 3) >= bytes_.size()) return;
  bytes_[static_cast<size_t>(k >> 3)] |= static_cast<uint8_t>(1 << (k & 7));
}

bool GridSet::has(int ix, int iz) const {
  if (ix < x0 || iz < z0 || ix >= x0 + w || iz >= z0 + h) return false;
  const int64_t k = static_cast<int64_t>(iz - z0) * w + (ix - x0);
  if (static_cast<size_t>(k >> 3) >= bytes_.size()) return false;
  return (bytes_[static_cast<size_t>(k >> 3)] & (1 << (k & 7))) != 0;
}

int GridSet::count() const {
  int n = 0;
  for (uint8_t b : bytes_)
    for (int v = b; v; v &= v - 1) n++;
  return n;
}

GridBits GridSet::toJSON() const { return {x0, z0, w, h, base64Encode(bytes_)}; }

}  // namespace fs

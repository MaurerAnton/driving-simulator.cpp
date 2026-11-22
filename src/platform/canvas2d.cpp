// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Software 2D canvas: fill, stroke, arcs and bitmap text into an RGBA buffer.
 *
 * Origin: the canvas 2D calls of src/render/instruments.ts (the browser's
 * own context in the original).
 */
#include "platform/canvas2d.h"

#include <algorithm>
#include <cmath>

namespace fs {

namespace {
/** 5×7 glyphs for the characters the cluster draws (digits, A–Z, a few marks). */
const char* const* glyph(char c) {
  static const char* const digits[10][7] = {
      {"01110", "10001", "10011", "10101", "11001", "10001", "01110"},
      {"00100", "01100", "00100", "00100", "00100", "00100", "01110"},
      {"01110", "10001", "00001", "00010", "00100", "01000", "11111"},
      {"11111", "00010", "00100", "00010", "00001", "10001", "01110"},
      {"00010", "00110", "01010", "10010", "11111", "00010", "00010"},
      {"11111", "10000", "11110", "00001", "00001", "10001", "01110"},
      {"00110", "01000", "10000", "11110", "10001", "10001", "01110"},
      {"11111", "00001", "00010", "00100", "01000", "01000", "01000"},
      {"01110", "10001", "10001", "01110", "10001", "10001", "01110"},
      {"01110", "10001", "10001", "01111", "00001", "00010", "01100"},
  };
  static const char* const letters[26][7] = {
      {"01110", "10001", "10001", "11111", "10001", "10001", "10001"},  // A
      {"11110", "10001", "10001", "11110", "10001", "10001", "11110"},  // B
      {"01110", "10001", "10000", "10000", "10000", "10001", "01110"},  // C
      {"11110", "10001", "10001", "10001", "10001", "10001", "11110"},  // D
      {"11111", "10000", "10000", "11110", "10000", "10000", "11111"},  // E
      {"11111", "10000", "10000", "11110", "10000", "10000", "10000"},  // F
      {"01110", "10001", "10000", "10111", "10001", "10001", "01111"},  // G
      {"10001", "10001", "10001", "11111", "10001", "10001", "10001"},  // H
      {"01110", "00100", "00100", "00100", "00100", "00100", "01110"},  // I
      {"00111", "00010", "00010", "00010", "00010", "10010", "01100"},  // J
      {"10001", "10010", "10100", "11000", "10100", "10010", "10001"},  // K
      {"10000", "10000", "10000", "10000", "10000", "10000", "11111"},  // L
      {"10001", "11011", "10101", "10101", "10001", "10001", "10001"},  // M
      {"10001", "11001", "10101", "10011", "10001", "10001", "10001"},  // N
      {"01110", "10001", "10001", "10001", "10001", "10001", "01110"},  // O
      {"11110", "10001", "10001", "11110", "10000", "10000", "10000"},  // P
      {"01110", "10001", "10001", "10001", "10101", "10010", "01101"},  // Q
      {"11110", "10001", "10001", "11110", "10100", "10010", "10001"},  // R
      {"01111", "10000", "10000", "01110", "00001", "00001", "11110"},  // S
      {"11111", "00100", "00100", "00100", "00100", "00100", "00100"},  // T
      {"10001", "10001", "10001", "10001", "10001", "10001", "01110"},  // U
      {"10001", "10001", "10001", "10001", "01010", "01010", "00100"},  // V
      {"10001", "10001", "10001", "10101", "10101", "11011", "10001"},  // W
      {"10001", "01010", "00100", "00100", "00100", "01010", "10001"},  // X
      {"10001", "01010", "00100", "00100", "00100", "00100", "00100"},  // Y
      {"11111", "00001", "00010", "00100", "01000", "10000", "11111"},  // Z
  };
  static const char* const slash[7] = {"00001", "00010", "00100", "01000",
                                       "10000", "00000", "00000"};
  static const char* const dash[7] = {"00000", "00000", "00000", "11111",
                                      "00000", "00000", "00000"};
  static const char* const dot[7] = {"00000", "00000", "00000", "00000",
                                     "00000", "01100", "01100"};
  static const char* const colon[7] = {"00000", "01100", "01100", "00000",
                                       "01100", "01100", "00000"};
  if (c >= '0' && c <= '9') return digits[c - '0'];
  if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
  if (c >= 'a' && c <= 'z') return letters[c - 'a'];
  if (c == '/') return slash;
  if (c == '-') return dash;
  if (c == '.') return dot;
  if (c == ':') return colon;
  return nullptr;
}
}  // namespace

uint32_t parseColor(const std::string& s) {
  if (s.empty() || s[0] != '#') return 0xff000000u;
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  auto byte = [&](size_t i) -> uint32_t {
    return i + 1 < s.size() ? static_cast<uint32_t>(hex(s[i]) * 16 + hex(s[i + 1])) : 0;
  };
  uint32_t r = byte(1), g = byte(3), b = byte(5);
  uint32_t a = s.size() >= 9 ? byte(7) : 0xff;
  return (a << 24) | (r << 16) | (g << 8) | b;
}

Canvas2D::Canvas2D(int width, int height) : w_(width), h_(height), px_(static_cast<size_t>(width) * height * 4, 0) {}

void Canvas2D::blend(int x, int y, uint32_t rgba) {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
  uint32_t a = (rgba >> 24) & 0xff;
  size_t o = (static_cast<size_t>(y) * w_ + x) * 4;
  if (a == 0xff) {
    px_[o] = (rgba >> 16) & 0xff;
    px_[o + 1] = (rgba >> 8) & 0xff;
    px_[o + 2] = rgba & 0xff;
    px_[o + 3] = 0xff;
    return;
  }
  double na = a / 255.0;
  double ia = 1 - na;
  px_[o] = static_cast<uint8_t>(((rgba >> 16) & 0xff) * na + px_[o] * ia);
  px_[o + 1] = static_cast<uint8_t>(((rgba >> 8) & 0xff) * na + px_[o + 1] * ia);
  px_[o + 2] = static_cast<uint8_t>((rgba & 0xff) * na + px_[o + 2] * ia);
  px_[o + 3] = 0xff;
}

void Canvas2D::clear(uint32_t rgba) {
  uint8_t r = (rgba >> 16) & 0xff, g = (rgba >> 8) & 0xff, b = rgba & 0xff;
  uint8_t a = (rgba >> 24) & 0xff;
  for (size_t i = 0; i < px_.size(); i += 4) {
    px_[i] = r;
    px_[i + 1] = g;
    px_[i + 2] = b;
    px_[i + 3] = a;
  }
}

void Canvas2D::setTransform(double a, double b, double c, double d, double e, double f) {
  a_ = a; b_ = b; c_ = c; d_ = d; e_ = e; f_ = f;
}

void Canvas2D::translate(double tx, double ty) {
  // the new origin lands where (tx, ty) lies now: M · T
  e_ += a_ * tx + c_ * ty;
  f_ += b_ * tx + d_ * ty;
}

void Canvas2D::scale(double sx, double sy) {
  a_ *= sx; b_ *= sx;
  c_ *= sy; d_ *= sy;
}

uint32_t Canvas2D::withAlpha(uint32_t rgba) const {
  if (alpha_ >= 0.999) return rgba;
  uint32_t a = static_cast<uint32_t>(((rgba >> 24) & 0xff) * alpha_);
  return (rgba & 0x00ffffffu) | (a << 24);
}

void Canvas2D::fillRect(double x, double y, double w, double h, uint32_t rgba) {
  // (the map draws in world coordinates: through the transform, as a quad)
  if (a_ != 1 || b_ != 0 || c_ != 0 || d_ != 1) {
    double x1 = x + w, y1 = y + h;
    std::vector<std::pair<double, double>> quad = {{x, y}, {x1, y}, {x1, y1}, {x, y1}};
    for (auto& q : quad) {
      double ox, oy;
      map(q.first, q.second, ox, oy);
      q = {ox, oy};
    }
    fillPoly(quad, withAlpha(rgba));
    return;
  }
  rgba = withAlpha(rgba);
  int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
  int x1 = static_cast<int>(std::ceil(x + w)), y1 = static_cast<int>(std::ceil(y + h));
  for (int yy = y0; yy < y1; yy++)
    for (int xx = x0; xx < x1; xx++) blend(xx, yy, rgba);
}

void Canvas2D::strokeRect(double x, double y, double w, double h, uint32_t rgba, double lineWidth) {
  double half = lineWidth / 2;
  fillRect(x - half, y - half, w + lineWidth, lineWidth, rgba);
  fillRect(x - half, y + h - half, w + lineWidth, lineWidth, rgba);
  fillRect(x - half, y - half, lineWidth, h + lineWidth, rgba);
  fillRect(x + w - half, y - half, lineWidth, h + lineWidth, rgba);
}

void Canvas2D::fillCircle(double cx, double cy, double r, uint32_t rgba) {
  rgba = withAlpha(rgba);
  double tx, ty;
  map(cx, cy, tx, ty);
  // (under a transform the radius grows with it – the map's pins are round
  // in pixels, and this is only used with uniform scales)
  r *= std::sqrt(std::fabs(a_ * d_ - b_ * c_));
  cx = tx;
  cy = ty;
  int y0 = static_cast<int>(std::floor(cy - r)), y1 = static_cast<int>(std::ceil(cy + r));
  for (int yy = y0; yy <= y1; yy++) {
    double dy = yy + 0.5 - cy;
    double dx = std::sqrt(std::max(0.0, r * r - dy * dy));
    int xa = static_cast<int>(std::floor(cx - dx));
    int xb = static_cast<int>(std::ceil(cx + dx));
    for (int xx = xa; xx < xb; xx++) blend(xx, yy, rgba);
  }
}

void Canvas2D::strokeCircle(double cx, double cy, double r, uint32_t rgba, double lineWidth) {
  rgba = withAlpha(rgba);
  double tx, ty;
  map(cx, cy, tx, ty);
  r *= std::sqrt(std::fabs(a_ * d_ - b_ * c_));
  cx = tx;
  cy = ty;
  int steps = std::max(16, static_cast<int>(r * 4));
  for (int i = 0; i < steps; i++) {
    double a0 = (static_cast<double>(i) / steps) * 6.283185307179586;
    double a1 = (static_cast<double>(i + 1) / steps) * 6.283185307179586;
    line(cx + std::cos(a0) * r, cy + std::sin(a0) * r, cx + std::cos(a1) * r,
         cy + std::sin(a1) * r, rgba, lineWidth);
  }
}

void Canvas2D::line(double x0, double y0, double x1, double y1, uint32_t rgba, double lineWidth) {
  double dx = x1 - x0, dy = y1 - y0;
  int steps = std::max(1, static_cast<int>(std::max(std::fabs(dx), std::fabs(dy))) + 1);
  double half = std::max(0.5, lineWidth / 2);
  for (int i = 0; i <= steps; i++) {
    double t = static_cast<double>(i) / steps;
    double x = x0 + dx * t, y = y0 + dy * t;
    for (int oy = static_cast<int>(-half); oy <= static_cast<int>(half); oy++)
      for (int ox = static_cast<int>(-half); ox <= static_cast<int>(half); ox++)
        blend(static_cast<int>(x) + ox, static_cast<int>(y) + oy, rgba);
  }
}

void Canvas2D::strokePolyline(const std::vector<std::pair<double, double>>& pts, uint32_t rgba,
                              double lineWidth, bool roundCap) {
  if (pts.size() < 2) return;
  rgba = withAlpha(rgba);
  double half = std::max(0.5, lineWidth / 2);
  for (size_t i = 1; i < pts.size(); i++) {
    // the segment through the transform (line() maps its ends)
    line(pts[i - 1].first, pts[i - 1].second, pts[i].first, pts[i].second, rgba, lineWidth);
  }
  if (roundCap) {
    for (const auto& p : {pts.front(), pts.back()}) {
      double x, y;
      map(p.first, p.second, x, y);
      fillCircle(x, y, half, rgba);
    }
  }
}

void Canvas2D::fillPoly(const std::vector<std::pair<double, double>>& pts, uint32_t rgba) {
  if (pts.size() < 3) return;
  double minY = 1e300, maxY = -1e300;
  for (const auto& p : pts) {
    minY = std::min(minY, p.second);
    maxY = std::max(maxY, p.second);
  }
  for (int y = static_cast<int>(std::floor(minY)); y <= static_cast<int>(std::ceil(maxY)); y++) {
    std::vector<double> xs;
    double yc = y + 0.5;
    for (size_t i = 0; i < pts.size(); i++) {
      const auto& a = pts[i];
      const auto& b = pts[(i + 1) % pts.size()];
      if ((a.second <= yc && b.second > yc) || (b.second <= yc && a.second > yc)) {
        double t = (yc - a.second) / (b.second - a.second);
        xs.push_back(a.first + (b.first - a.first) * t);
      }
    }
    std::sort(xs.begin(), xs.end());
    for (size_t i = 0; i + 1 < xs.size(); i += 2)
      for (int x = static_cast<int>(std::ceil(xs[i])); x < static_cast<int>(xs[i + 1]); x++)
        blend(x, y, rgba);
  }
}

void Canvas2D::fillText(const std::string& text, double x, double y, double size, uint32_t rgba) {
  rgba = withAlpha(rgba);
  double tx, ty;
  map(x, y, tx, ty);
  x = tx;
  y = ty;
  size *= std::sqrt(std::fabs(a_ * d_ - b_ * c_));
  double scale = std::max(1.0, size / 7.0);
  double advance = 6 * scale;
  double total = text.empty() ? 0 : text.size() * advance - scale;
  double cx = x - total / 2;
  for (char c : text) {
    const char* const* g = glyph(c);
    if (g) {
      for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
          if (g[row][col] != '1') continue;
          double px = cx + col * scale;
          double py = y - size / 2 + row * scale;
          fillRect(px, py, scale, scale, rgba);
        }
      }
    }
    cx += advance;
  }
}

void Canvas2D::strokeText(const std::string& text, double x, double y, double size, uint32_t rgba) {
  // the outline is the same text drawn a pixel to every side
  double dx = std::max(1.0, size / 10.0);
  for (int oy = -1; oy <= 1; oy++)
    for (int ox = -1; ox <= 1; ox++)
      if (ox || oy) fillText(text, x + ox * dx, y + oy * dx, size, rgba);
}

void Canvas2D::fillGradientV(uint32_t top, uint32_t bottom) {
  uint8_t tr = (top >> 16) & 0xff, tg = (top >> 8) & 0xff, tb = top & 0xff;
  uint8_t br = (bottom >> 16) & 0xff, bg = (bottom >> 8) & 0xff, bb = bottom & 0xff;
  for (int y = 0; y < h_; y++) {
    double t = h_ > 1 ? static_cast<double>(y) / (h_ - 1) : 0;
    uint8_t r = static_cast<uint8_t>(tr + (br - tr) * t);
    uint8_t g = static_cast<uint8_t>(tg + (bg - tg) * t);
    uint8_t b = static_cast<uint8_t>(tb + (bb - tb) * t);
    for (int x = 0; x < w_; x++) {
      size_t o = (static_cast<size_t>(y) * w_ + x) * 4;
      px_[o] = r;
      px_[o + 1] = g;
      px_[o + 2] = b;
      px_[o + 3] = 0xff;
    }
  }
}



namespace {
/** JPEG Annex K tables (zigzag-ordered values, and the standard Huffman codes). */
const int kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                         12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                         35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                         58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
const int kLumQ[64] = {16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13,
                       16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62, 18, 22, 37, 56,
                       68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103,
                       121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
const int kChromQ[64] = {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
                         24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
                         99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                         99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};
const int kLumVal[64] = {16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13,
                         16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62, 18, 22, 37, 56,
                         68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103,
                         121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
const int kChromVal[64] = {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
                          24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
                          99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                          99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};
/** Huffman code-length counts (16 bytes each) for the DHT segment. */
const uint8_t kLumDC[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
const uint8_t kLumAC[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
const uint8_t kChromDC[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
const uint8_t kChromAC[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
/** Huffman symbol tables: the 12 DC values in code order. */
const uint8_t kLumDCVal[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
const uint8_t kChromDCVal[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
const uint8_t kLumACVal[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61,
    0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52,
    0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25,
    0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45,
    0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64,
    0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
    0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
    0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3,
    0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8,
    0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};
const uint8_t kChromACVal[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61,
    0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33,
    0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18,
    0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44,
    0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63,
    0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
    0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
    0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca,
    0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};
}  // namespace

std::vector<uint8_t> Canvas2D::toJpeg(int quality) const {
  // A minimal baseline JPEG encoder: 4:4:4, 8×8 blocks, standard Huffman
  // tables.  Enough for the drawn test panorama and the instrument panel;
  // the street pictures themselves are never re-encoded.
  quality = std::max(1, std::min(100, quality));
  int sf = quality < 50 ? 5000 / quality : 200 - quality * 2;
  auto qtbl = [&](int v) { return std::min(255, (v * sf + 50) / 100); };
  // the coefficients are divided by the SAME table the DQT carries, or the
  // decoder would undo a different scale than was applied
  int lumQ[64], chromaQ[64];
  for (int i = 0; i < 64; i++) {
    lumQ[i] = qtbl(kLumVal[i]);
    chromaQ[i] = qtbl(kChromVal[i]);
  }

  struct BitWriter {
    std::vector<uint8_t>& out;
    uint32_t buf = 0;
    int bits = 0;
    void put(uint32_t code, int len) {
      buf = (buf << len) | (code & ((1u << len) - 1));
      bits += len;
      while (bits >= 8) {
        bits -= 8;
        uint8_t byte = static_cast<uint8_t>((buf >> bits) & 0xff);
        out.push_back(byte);
        if (byte == 0xff) out.push_back(0x00);  // byte stuffing
      }
    }
    void flush() {
      while (bits > 0) {
        put(1, 1);
      }
    }
  };

  std::vector<uint8_t> out;
  auto marker = [&](int m, const std::vector<uint8_t>& payload) {
    out.push_back(0xff);
    out.push_back(static_cast<uint8_t>(m));
    if (!payload.empty()) {
      size_t n = payload.size() + 2;
      out.push_back(static_cast<uint8_t>(n >> 8));
      out.push_back(static_cast<uint8_t>(n & 0xff));
      out.insert(out.end(), payload.begin(), payload.end());
    }
  };
  auto marker0 = [&](int m) {
    out.push_back(0xff);
    out.push_back(static_cast<uint8_t>(m));
  };

  marker0(0xd8);  // SOI
  // APP0 JFIF
  {
    std::vector<uint8_t> p = {'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0};
    marker(0xe0, p);
  }
  // DQT
  {
    std::vector<uint8_t> p;
    p.push_back(0);
    for (int i = 0; i < 64; i++) p.push_back(static_cast<uint8_t>(qtbl(kLumVal[i])));
    p.push_back(1);
    for (int i = 0; i < 64; i++) p.push_back(static_cast<uint8_t>(qtbl(kChromVal[i])));
    marker(0xdb, p);
  }
  // SOF0
  {
    std::vector<uint8_t> p = {8};
    p.push_back(static_cast<uint8_t>(h_ >> 8));
    p.push_back(static_cast<uint8_t>(h_ & 0xff));
    p.push_back(static_cast<uint8_t>(w_ >> 8));
    p.push_back(static_cast<uint8_t>(w_ & 0xff));
    p.push_back(3);
    for (int c = 0; c < 3; c++) {
      p.push_back(static_cast<uint8_t>(c + 1));  // component id
      p.push_back(0x11);                        // sampling 1×1 (4:4:4)
      p.push_back(static_cast<uint8_t>(c == 0 ? 0 : 1));  // quant table
    }
    marker(0xc0, p);
  }
  // DHT
  {
    std::vector<uint8_t> p;
    p.push_back(0x00);
    for (int i = 0; i < 16; i++) p.push_back(kLumDC[i]);
    for (int i = 0; i < 12; i++) p.push_back(kLumDCVal[i]);
    p.push_back(0x10);
    for (int i = 0; i < 16; i++) p.push_back(kLumAC[i]);
    for (int i = 0; i < 162; i++) p.push_back(kLumACVal[i]);
    p.push_back(0x01);
    for (int i = 0; i < 16; i++) p.push_back(kChromDC[i]);
    for (int i = 0; i < 12; i++) p.push_back(kChromDCVal[i]);
    p.push_back(0x11);
    for (int i = 0; i < 16; i++) p.push_back(kChromAC[i]);
    for (int i = 0; i < 162; i++) p.push_back(kChromACVal[i]);
    marker(0xc4, p);
  }
  // SOS
  {
    std::vector<uint8_t> p = {3, 1, 0x00, 2, 0x11, 3, 0x11, 0, 63, 0};
    marker(0xda, p);
  }

  // entropy-coded scan: 8×8 blocks, level-shifted, FDCT, quantised
  BitWriter bw{out};
  int prev[3] = {0, 0, 0};
  const double kCos[8][8] = {
      {0.35355339, 0.35355339, 0.35355339, 0.35355339, 0.35355339, 0.35355339, 0.35355339, 0.35355339},
      {0.49039264, 0.41573481, 0.27778512, 0.09754516, -0.09754516, -0.27778512, -0.41573481, -0.49039264},
      {0.46193977, 0.19134171, -0.19134171, -0.46193977, -0.46193977, -0.19134171, 0.19134171, 0.46193977},
      {0.41573481, -0.09754516, -0.49039264, -0.27778512, 0.27778512, 0.49039264, 0.09754516, -0.41573481},
      {0.35355339, -0.35355339, -0.35355339, 0.35355339, 0.35355339, -0.35355339, -0.35355339, 0.35355339},
      {0.27778512, -0.49039264, 0.09754516, 0.41573481, -0.41573481, -0.09754516, 0.49039264, -0.27778512},
      {0.19134171, -0.46193977, 0.46193977, -0.19134171, -0.19134171, 0.46193977, -0.46193977, 0.19134171},
      {0.09754516, -0.41573481, 0.49039264, -0.27778512, -0.27778512, 0.49039264, -0.41573481, 0.09754516},
  };
  auto huff = [](int symbol, const uint8_t* counts, const uint8_t* values, int nvalues,
                 uint32_t& code, int& len) {
    // the symbol names a VALUE; the codes are assigned to the symbols in the
    // order their values are listed (DC values happen to be their index too,
    // AC values are not: 0x00 EOB sits at position 3 in the standard table)
    int want = -1;
    for (int m = 0; m < nvalues; m++)
      if (values[m] == symbol) {
        want = m;
        break;
      }
    if (want < 0) {
      code = 0;
      len = 0;
      return;
    }
    int codev = 0;
    int k = 0;
    for (int i = 0; i < 16; i++) {
      for (int j = 0; j < counts[i]; j++) {
        if (k == want) {
          code = static_cast<uint32_t>(codev);
          len = i + 1;
          return;
        }
        codev++;
        k++;
      }
      codev <<= 1;
    }
    code = 0;
    len = 0;
  };
  int bwBlocks = (w_ + 7) / 8, bhBlocks = (h_ + 7) / 8;
  for (int by = 0; by < bhBlocks; by++) {
    for (int bx = 0; bx < bwBlocks; bx++) {
      for (int comp = 0; comp < 3; comp++) {
        // gather the 8×8 block (level-shifted to -128…127)
        double blk[64];
        for (int y = 0; y < 8; y++) {
          for (int x = 0; x < 8; x++) {
            int sx = std::min(bx * 8 + x, w_ - 1);
            int sy = std::min(by * 8 + y, h_ - 1);
            size_t o = (static_cast<size_t>(sy) * w_ + sx) * 4;
            double v;
            if (comp == 0) {
              // luma: level-shifted to -128…127, as the standard says
              v = 0.299 * px_[o] + 0.587 * px_[o + 1] + 0.114 * px_[o + 2] - 128;
            } else if (comp == 1) {
              // chroma already straddles zero around 128: no further shift
              v = -0.168736 * px_[o] - 0.331264 * px_[o + 1] + 0.5 * px_[o + 2];
            } else {
              v = 0.5 * px_[o] - 0.418688 * px_[o + 1] - 0.081312 * px_[o + 2];
            }
            blk[y * 8 + x] = v;
          }
        }
        // FDCT (separable, the 8×8 cosine basis): kCos is laid out as
        // [frequency][position] – the basis of frequency u at position x is
        // kCos[u][x].  The coefficient at (v, u) is the transform of the row
        // y=v along x → u and of the column x=u along y → v.
        double coef[64];
        for (int v = 0; v < 8; v++) {
          for (int u = 0; u < 8; u++) {
            double sum = 0;
            for (int x = 0; x < 8; x++)
              for (int y = 0; y < 8; y++)
                sum += blk[y * 8 + x] * kCos[u][x] * kCos[v][y];
            coef[v * 8 + u] = sum;
          }
        }
        const int* qt = comp == 0 ? lumQ : chromaQ;
        int zz[64];
        // kZigzag is the scan's own order (position → natural index), as the
        // decoder reads it: the encoder must fill the scan in that order too
        for (int pos = 0; pos < 64; pos++) {
          int nat = kZigzag[pos];
          int v = static_cast<int>(std::lround(coef[nat] / qt[nat]));
          // the standard tables carry AC categories 1…10 only: keep the
          // coefficient within the 10 bits the symbol can name
          if (pos > 0) v = std::max(-1023, std::min(1023, v));
          else v = std::max(-2047, std::min(2047, v));
          zz[pos] = v;
        }
        // DC difference + AC run-length coding
        int dc = zz[0] - prev[comp];
        prev[comp] = zz[0];
        int mag = std::abs(dc);
        int cat = 0;
        while ((1 << cat) <= mag) cat++;
        uint32_t code;
        int len;
        huff(cat, comp == 0 ? kLumDC : kChromDC, comp == 0 ? kLumDCVal : kChromDCVal, 12, code, len);
        bw.put(code, len);
        if (cat > 0) bw.put(static_cast<uint32_t>(dc < 0 ? dc + (1 << cat) - 1 : dc), cat);
        int run = 0;
        for (int k = 1; k < 64; k++) {
          if (zz[k] == 0) {
            run++;
            continue;
          }
          while (run > 15) {
            huff(0xf0, comp == 0 ? kLumAC : kChromAC, comp == 0 ? kLumACVal : kChromACVal, 162, code, len);
            bw.put(code, len);
            run -= 16;
          }
          mag = std::abs(zz[k]);
          int acat = 0;
          while ((1 << acat) <= mag) acat++;
          int symbol = (run << 4) | acat;
          huff(symbol, comp == 0 ? kLumAC : kChromAC, comp == 0 ? kLumACVal : kChromACVal, 162, code, len);
          bw.put(code, len);
          bw.put(static_cast<uint32_t>(zz[k] < 0 ? zz[k] + (1 << acat) - 1 : zz[k]), acat);
          run = 0;
        }
        if (run > 0) {
          huff(0x00, comp == 0 ? kLumAC : kChromAC, comp == 0 ? kLumACVal : kChromACVal, 162, code, len);
          bw.put(code, len);
        }
      }
    }
  }
  bw.flush();
  marker0(0xd9);  // EOI
  return out;
}

void Canvas2D::drawImage(const Canvas2D& src, double x, double y) {
  rgbaDrawImage(src, x, y, src.w_, src.h_, src.w_, src.h_);
}

void Canvas2D::drawImage(const Canvas2D& src, double dx, double dy, int sw, int sh) {
  rgbaDrawImage(src, dx, dy, sw, sh, sw, sh);
}

void Canvas2D::rgbaDrawImage(const Canvas2D& src, double dx, double dy, double dw, double dh,
                             int sw, int sh) {
  (void)dw;
  (void)dh;
  int x0 = static_cast<int>(dx), y0 = static_cast<int>(dy);
  if (a_ == 1 && b_ == 0 && c_ == 0 && d_ == 1) {
    for (int yy = 0; yy < sh; yy++) {
      for (int xx = 0; xx < sw; xx++) {
        size_t si = (static_cast<size_t>(yy) * src.w_ + xx) * 4;
        uint32_t rgba = (static_cast<uint32_t>(src.px_[si]) << 16) |
                        (static_cast<uint32_t>(src.px_[si + 1]) << 8) | src.px_[si + 2];
        if (src.px_[si + 3] == 0) continue;
        blend(x0 + xx, y0 + yy,
              withAlpha(rgba | (static_cast<uint32_t>(src.px_[si + 3]) << 24)));
      }
    }
    return;
  }
  // through the transform: nearest corner of each destination pixel
  for (int yy = 0; yy < sh; yy++) {
    for (int xx = 0; xx < sw; xx++) {
      double ox, oy;
      map(x0 + xx + 0.5, y0 + yy + 0.5, ox, oy);
      blend(static_cast<int>(ox), static_cast<int>(oy),
            withAlpha((static_cast<uint32_t>(src.px_[(static_cast<size_t>(yy) * src.w_ + xx) * 4]) << 16) |
                      (static_cast<uint32_t>(src.px_[(static_cast<size_t>(yy) * src.w_ + xx) * 4 + 1]) << 8) |
                      src.px_[(static_cast<size_t>(yy) * src.w_ + xx) * 4 + 2] |
                      (static_cast<uint32_t>(src.px_[(static_cast<size_t>(yy) * src.w_ + xx) * 4 + 3]) << 24)));
    }
  }
}

}  // namespace fs

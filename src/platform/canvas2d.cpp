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

void Canvas2D::fillRect(double x, double y, double w, double h, uint32_t rgba) {
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

void Canvas2D::drawImage(const Canvas2D& src, double x, double y) {
  int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
  for (int yy = 0; yy < src.h_; yy++) {
    for (int xx = 0; xx < src.w_; xx++) {
      size_t si = (static_cast<size_t>(yy) * src.w_ + xx) * 4;
      uint32_t rgba = (static_cast<uint32_t>(src.px_[si]) << 16) |
                      (static_cast<uint32_t>(src.px_[si + 1]) << 8) | src.px_[si + 2];
      if (src.px_[si + 3] == 0) continue;
      blend(x0 + xx, y0 + yy, rgba | (static_cast<uint32_t>(src.px_[si + 3]) << 24));
    }
  }
}

}  // namespace fs

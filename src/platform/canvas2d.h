// SPDX-License-Identifier: AGPL-3.0-only
/**
 * A small software 2D canvas: the subset of the canvas API the instrument
 * cluster and the sign textures need (fill, stroke, arcs, text).
 *
 * The original draws with the browser's 2D context; running the game as
 * WebAssembly means the same drawing has to happen somewhere, so this
 * rasteriser provides it.  Text uses a built-in 5×7 bitmap font, which is
 * enough for the digits and capitals the cluster shows.
 *
 * Origin: the canvas calls of src/render/instruments.ts (MaurerAnton/
 * driving-simulator @ 5b6ddc7, v0.1.0).
 */
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fs {

class Canvas2D {
 public:
  Canvas2D(int width, int height);

  int width() const { return w_; }
  int height() const { return h_; }
  /** RGBA pixels, row-major. */
  std::vector<uint8_t>& pixels() { return px_; }
  const std::vector<uint8_t>& pixels() const { return px_; }

  void clear(uint32_t rgba);
  void fillRect(double x, double y, double w, double h, uint32_t rgba);
  void strokeRect(double x, double y, double w, double h, uint32_t rgba, double lineWidth);
  void fillCircle(double cx, double cy, double r, uint32_t rgba);
  void strokeCircle(double cx, double cy, double r, uint32_t rgba, double lineWidth);
  /** Line with square ends (the cluster only needs straight strokes). */
  void line(double x0, double y0, double x1, double y1, uint32_t rgba, double lineWidth);
  /** Polygon fill (the needles are quads). */
  void fillPoly(const std::vector<std::pair<double, double>>& pts, uint32_t rgba);
  /** Text centred on (x, y), scaled to `size` pixels. */
  void fillText(const std::string& text, double x, double y, double size, uint32_t rgba);
  /** Copy another canvas onto this one (the cluster composes face + state). */
  void drawImage(const Canvas2D& src, double x, double y);

 private:
  int w_, h_;
  std::vector<uint8_t> px_;

  void blend(int x, int y, uint32_t rgba);
};

/** Parse "#rrggbb" or "#rrggbbaa" (the colours the original writes inline). */
uint32_t parseColor(const std::string& s);

}  // namespace fs

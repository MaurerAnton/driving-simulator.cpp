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

  // -- the transform of the map (the original's ctx.translate/scale) --------
  /**
   * The current transformation, as canvas keeps it: x' = a·x + c·y + e,
   * y' = b·x + d·y + f.  Identity at the start; `setTransform` replaces it,
   * `translate`/`scale` move the origin or the unit (the full-screen map).
   */
  void setTransform(double a, double b, double c, double d, double e, double f);
  void translate(double tx, double ty);
  void scale(double sx, double sy);

  /** Alpha of what is drawn next (1 = opaque), as the original's globalAlpha. */
  void setAlpha(double a) { alpha_ = a; }

  void fillRect(double x, double y, double w, double h, uint32_t rgba);
  void strokeRect(double x, double y, double w, double h, uint32_t rgba, double lineWidth);
  void fillCircle(double cx, double cy, double r, uint32_t rgba);
  void strokeCircle(double cx, double cy, double r, uint32_t rgba, double lineWidth);
  /** Line with square ends (the cluster only needs straight strokes). */
  void line(double x0, double y0, double x1, double y1, uint32_t rgba, double lineWidth);
  /** Stroke a polyline (the roads of the map): square joins, round caps. */
  void strokePolyline(const std::vector<std::pair<double, double>>& pts, uint32_t rgba,
                      double lineWidth, bool roundCap = false);
  /** Polygon fill (the needles are quads). */
  void fillPoly(const std::vector<std::pair<double, double>>& pts, uint32_t rgba);
  /** Text centred on (x, y), scaled to `size` pixels. */
  void fillText(const std::string& text, double x, double y, double size, uint32_t rgba);
  /** The same text with an outline around it (the names on the map). */
  void strokeText(const std::string& text, double x, double y, double size, uint32_t rgba);
  /** Copy another canvas onto this one (the cluster composes face + state). */
  void drawImage(const Canvas2D& src, double x, double y);
  /** The same, drawn from an area `sw × sh` of `src` at `dx, dy` (the map's tiles). */
  void drawImage(const Canvas2D& src, double dx, double dy, int sw, int sh);
  /** A vertical gradient from `top` to `bottom` over the whole canvas. */
  void fillGradientV(uint32_t top, uint32_t bottom);
  /** Encode as a baseline JPEG (quality 1–100). */
  std::vector<uint8_t> toJpeg(int quality) const;

 private:
  int w_, h_;
  std::vector<uint8_t> px_;
  double a_ = 1, b_ = 0, c_ = 0, d_ = 1, e_ = 0, f_ = 0;
  double alpha_ = 1;

  void blend(int x, int y, uint32_t rgba);
  /** One point through the transform. */
  void map(double x, double y, double& outX, double& outY) const {
    outX = a_ * x + c_ * y + e_;
    outY = b_ * x + d_ * y + f_;
  }
  /** The same, with the alpha multiplied in (globalAlpha). */
  uint32_t withAlpha(uint32_t rgba) const;
  void rgbaDrawImage(const Canvas2D& src, double dx, double dy, double dw, double dh, int sw,
                     int sh);
};

/** Parse "#rrggbb" or "#rrggbbaa" (the colours the original writes inline). */
uint32_t parseColor(const std::string& s);

}  // namespace fs

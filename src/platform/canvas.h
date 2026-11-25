// SPDX-License-Identifier: AGPL-3.0-only
/**
 * A 2D canvas: the browser's CanvasRenderingContext2D by the same names.  The
 * mini map, the big map, the sign faces, the instrument faces and the loading
 * picture are drawn with it in the original, and so they are here:
 *
 *     const g = canvas.getContext('2d')!;        Ctx2D g = canvas.getContext2D();
 *     g.fillStyle = '#1c2630';                    g.fillStyle("#1c2630");
 *     g.fillRect(0, 0, W, H);                     g.fillRect(0, 0, W, H);
 *     g.font = '600 13px system-ui';              g.font("600 13px system-ui");
 *     const w = g.measureText(name).width;        double w = g.measureText(name).width;
 *
 * Properties are set with a call of their name (C++ has no setters).  In the
 * browser this is the real canvas, with the browser's fonts and text shaping; on
 * the native host it is painted in memory (platform/native/canvas_native.cpp:
 * plutovg, the fonts of third_party/fonts), and the calls are counted besides –
 * the tests look at what is asked for, and at pixels.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/task.h"
#include "platform/dom.h"

namespace fs {

using Bytes = std::vector<uint8_t>;

class Ctx2D;

/** `ctx.createLinearGradient(…)` / createRadialGradient: a paint for fillStyle/strokeStyle */
class CanvasGradient {
 public:
  CanvasGradient() = default;
  void addColorStop(double offset, const std::string& color) const;
  int handle() const { return h_ ? *h_ : 0; }

 private:
  friend class Ctx2D;
  /** the host's gradient, given back when the last copy goes (a gradient made every frame would pile up otherwise) */
  std::shared_ptr<const int> h_;
};

/** what measureText answers */
struct TextMetrics {
  double width = 0;
  double actualBoundingBoxAscent = 0, actualBoundingBoxDescent = 0;
  double actualBoundingBoxLeft = 0, actualBoundingBoxRight = 0;
};

/** `ctx.getImageData(…)`: RGBA bytes, row by row from the top */
struct ImageData {
  int width = 0, height = 0;
  std::vector<uint8_t> data;
};

/** A picture the host has decoded (an HTMLImageElement / ImageBitmap of the original). */
struct Image {
  int handle = 0;
  int width = 0, height = 0;
  explicit operator bool() const { return handle != 0; }
};

/**
 * `const img = new Image(); img.src = url; await img.decode()`: a picture from an
 * address (a failed one has handle 0).  Natively: PNG, JPEG and SVG (from data: addresses and fetch).
 */
Task<Image> loadImage(std::string url);
/** a picture from bytes that are a JPEG, PNG or WebP file (createImageBitmap of a Blob) */
Task<Image> decodeImage(Bytes bytes, std::string mimeType = "image/jpeg");
/** let the host forget a picture (its handle is dead afterwards) */
void freeImage(Image& image);

class Canvas {
 public:
  /** no canvas */
  Canvas() = default;
  /** document.createElement('canvas') of that size (not in the page until its element is appended) */
  Canvas(int width, int height);
  /** the canvas element that is already there */
  explicit Canvas(const dom::El& element) : el_(element) {}
  explicit operator bool() const { return static_cast<bool>(el_); }
  bool operator==(const Canvas& o) const { return el_ == o.el_; }
  /** the element, for the page */
  const dom::El& el() const { return el_; }
  int width() const { return static_cast<int>(el_.width()); }
  int height() const { return static_cast<int>(el_.height()); }
  /** `canvas.width = w` (which also wipes it) */
  void width(int w) const { el_.width(w); }
  void height(int h) const { el_.height(h); }
  /** getContext('2d') */
  Ctx2D getContext2D() const;
  /** toDataURL(type, quality) */
  std::string toDataURL(const std::string& type = "image/png", double quality = 0.92) const;

 private:
  dom::El el_;
};

class Ctx2D {
 public:
  Ctx2D() = default;
  explicit operator bool() const { return h_ != 0; }
  int handle() const { return h_; }

  // --- what is drawn with -----------------------------------------------------
  void fillStyle(const std::string& css) const;
  void fillStyle(const CanvasGradient& g) const;
  void strokeStyle(const std::string& css) const;
  void strokeStyle(const CanvasGradient& g) const;
  void lineWidth(double w) const;
  void lineCap(const std::string& v) const;
  void lineJoin(const std::string& v) const;
  void miterLimit(double v) const;
  void setLineDash(const std::vector<double>& segments) const;
  void lineDashOffset(double v) const;
  void font(const std::string& css) const;
  void textAlign(const std::string& v) const;
  void textBaseline(const std::string& v) const;
  void direction(const std::string& v) const;
  void letterSpacing(const std::string& v) const;
  void globalAlpha(double a) const;
  void globalCompositeOperation(const std::string& v) const;
  void shadowColor(const std::string& css) const;
  void shadowBlur(double v) const;
  void shadowOffsetX(double v) const;
  void shadowOffsetY(double v) const;
  void imageSmoothingEnabled(bool on) const;
  void filter(const std::string& v) const;

  // --- the state ----------------------------------------------------------------
  void save() const;
  void restore() const;
  void translate(double x, double y) const;
  void rotate(double angle) const;
  void scale(double x, double y) const;
  void transform(double a, double b, double c, double d, double e, double f) const;
  void setTransform(double a, double b, double c, double d, double e, double f) const;
  void resetTransform() const;

  // --- paths ----------------------------------------------------------------------
  void beginPath() const;
  void closePath() const;
  void moveTo(double x, double y) const;
  void lineTo(double x, double y) const;
  void arc(double x, double y, double r, double a0, double a1, bool counterclockwise = false) const;
  void arcTo(double x1, double y1, double x2, double y2, double r) const;
  void ellipse(double x, double y, double rx, double ry, double rotation, double a0, double a1, bool counterclockwise = false) const;
  void rect(double x, double y, double w, double h) const;
  void roundRect(double x, double y, double w, double h, double r) const;
  void quadraticCurveTo(double cx, double cy, double x, double y) const;
  void bezierCurveTo(double c1x, double c1y, double c2x, double c2y, double x, double y) const;
  void fill() const;
  /** fill('evenodd') */
  void fillEvenOdd() const;
  void stroke() const;
  void clip() const;
  /**
   * moveTo(first point), lineTo(the others) in one call – for the long lines of a
   * map (`xy`: x0, y0, x1, y1 …; `n`: how many points); `close`: closePath() after
   */
  void polyline(const double* xy, size_t n, bool close = false) const;
  void polyline(const std::vector<double>& xy, bool close = false) const { polyline(xy.data(), xy.size() / 2, close); }

  // --- rectangles, text, pictures ---------------------------------------------------
  void fillRect(double x, double y, double w, double h) const;
  void strokeRect(double x, double y, double w, double h) const;
  void clearRect(double x, double y, double w, double h) const;
  void fillText(const std::string& text, double x, double y) const;
  void fillText(const std::string& text, double x, double y, double maxWidth) const;
  void strokeText(const std::string& text, double x, double y) const;
  TextMetrics measureText(const std::string& text) const;
  void drawImage(const Canvas& c, double dx, double dy) const;
  void drawImage(const Canvas& c, double dx, double dy, double dw, double dh) const;
  void drawImage(const Canvas& c, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh) const;
  void drawImage(const Image& i, double dx, double dy) const;
  void drawImage(const Image& i, double dx, double dy, double dw, double dh) const;
  void drawImage(const Image& i, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh) const;
  ImageData getImageData(int x, int y, int w, int h) const;
  void putImageData(const ImageData& d, int x, int y) const;

  // --- paints -----------------------------------------------------------------------
  CanvasGradient createLinearGradient(double x0, double y0, double x1, double y1) const;
  CanvasGradient createRadialGradient(double x0, double y0, double r0, double x1, double y1, double r1) const;

 private:
  friend class Canvas;
  int h_ = 0;
};

namespace canvasBackend {
/**
 * The operations by number (the same numbers in platform/web/library_fahrsim.js – keep
 * the two lists alike).  `num` takes up to eight numbers, `str` a text and three numbers.
 */
enum Op {
  // num
  LineWidth = 1, MiterLimit, LineDashOffset, GlobalAlpha, ShadowBlur, ShadowOffsetX, ShadowOffsetY, ImageSmoothing,
  Save, Restore, Translate, Rotate, Scale, Transform, SetTransform, ResetTransform,
  BeginPath, ClosePath, MoveTo, LineTo, Arc, ArcTo, Ellipse, Rect, RoundRect, QuadTo, BezierTo, Fill, FillEvenOdd, Stroke, Clip,
  FillRect, StrokeRect, ClearRect,
  FillGradient, StrokeGradient,
  // str
  FillStyle = 100, StrokeStyle, LineCap, LineJoin, Font, TextAlign, TextBaseline, Direction, LetterSpacing, Composite, ShadowColor, Filter,
  FillText, FillTextMax, StrokeText, AddColorStop,
};
/** getContext('2d') of a canvas element (0: there is none) */
int context(int canvasElement);
void num(int ctx, int op, double a = 0, double b = 0, double c = 0, double d = 0, double e = 0, double f = 0, double g = 0, double h = 0);
void str(int ctx, int op, const char* s, double a = 0, double b = 0, double c = 0);
void lineDash(int ctx, const double* segments, int n);
void polyline(int ctx, const double* xy, int n, bool close);
/** drawImage of a canvas (its element) or of a decoded picture: `n` numbers follow (2: dx dy; 4: + dw dh; 8: sx sy sw sh dx dy dw dh) */
void drawImage(int ctx, bool fromCanvas, int source, const double* v, int n);
void measure(int ctx, const char* text, double out[5]);
int gradient(int ctx, bool radial, double a, double b, double c, double d, double e, double f);
/** the gradient is no longer used */
void gradientFree(int g);
void getImageData(int ctx, int x, int y, int w, int h, uint8_t* out);
void putImageData(int ctx, const uint8_t* data, int w, int h, int x, int y);
std::string toDataURL(int canvasElement, const char* type, double quality);
}  // namespace canvasBackend

}  // namespace fs

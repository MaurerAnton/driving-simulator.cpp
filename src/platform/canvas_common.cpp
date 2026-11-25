// SPDX-License-Identifier: AGPL-3.0-only
/** canvas.h: the calls of the 2D context, handed to the host by number (see canvasBackend::Op). */
#include "platform/canvas.h"

namespace fs {

using namespace canvasBackend;

Canvas::Canvas(int width, int height) : el_(dom::el("canvas")) {
  el_.width(width);
  el_.height(height);
}

Ctx2D Canvas::getContext2D() const {
  Ctx2D c;
  c.h_ = el_ ? context(el_.handle()) : 0;
  return c;
}

std::string Canvas::toDataURL(const std::string& type, double quality) const { return el_ ? canvasBackend::toDataURL(el_.handle(), type.c_str(), quality) : std::string(); }

void CanvasGradient::addColorStop(double offset, const std::string& color) const { str(handle(), AddColorStop, color.c_str(), offset); }

void Ctx2D::fillStyle(const std::string& css) const { str(h_, FillStyle, css.c_str()); }
void Ctx2D::fillStyle(const CanvasGradient& g) const { num(h_, FillGradient, g.handle()); }
void Ctx2D::strokeStyle(const std::string& css) const { str(h_, StrokeStyle, css.c_str()); }
void Ctx2D::strokeStyle(const CanvasGradient& g) const { num(h_, StrokeGradient, g.handle()); }
void Ctx2D::lineWidth(double w) const { num(h_, LineWidth, w); }
void Ctx2D::lineCap(const std::string& v) const { str(h_, LineCap, v.c_str()); }
void Ctx2D::lineJoin(const std::string& v) const { str(h_, LineJoin, v.c_str()); }
void Ctx2D::miterLimit(double v) const { num(h_, MiterLimit, v); }
void Ctx2D::setLineDash(const std::vector<double>& segments) const { lineDash(h_, segments.data(), static_cast<int>(segments.size())); }
void Ctx2D::lineDashOffset(double v) const { num(h_, LineDashOffset, v); }
void Ctx2D::font(const std::string& css) const { str(h_, Font, css.c_str()); }
void Ctx2D::textAlign(const std::string& v) const { str(h_, TextAlign, v.c_str()); }
void Ctx2D::textBaseline(const std::string& v) const { str(h_, TextBaseline, v.c_str()); }
void Ctx2D::direction(const std::string& v) const { str(h_, Direction, v.c_str()); }
void Ctx2D::letterSpacing(const std::string& v) const { str(h_, LetterSpacing, v.c_str()); }
void Ctx2D::globalAlpha(double a) const { num(h_, GlobalAlpha, a); }
void Ctx2D::globalCompositeOperation(const std::string& v) const { str(h_, Composite, v.c_str()); }
void Ctx2D::shadowColor(const std::string& css) const { str(h_, ShadowColor, css.c_str()); }
void Ctx2D::shadowBlur(double v) const { num(h_, ShadowBlur, v); }
void Ctx2D::shadowOffsetX(double v) const { num(h_, ShadowOffsetX, v); }
void Ctx2D::shadowOffsetY(double v) const { num(h_, ShadowOffsetY, v); }
void Ctx2D::imageSmoothingEnabled(bool on) const { num(h_, ImageSmoothing, on ? 1 : 0); }
void Ctx2D::filter(const std::string& v) const { str(h_, Filter, v.c_str()); }

void Ctx2D::save() const { num(h_, Save); }
void Ctx2D::restore() const { num(h_, Restore); }
void Ctx2D::translate(double x, double y) const { num(h_, Translate, x, y); }
void Ctx2D::rotate(double angle) const { num(h_, Rotate, angle); }
void Ctx2D::scale(double x, double y) const { num(h_, Scale, x, y); }
void Ctx2D::transform(double a, double b, double c, double d, double e, double f) const { num(h_, Transform, a, b, c, d, e, f); }
void Ctx2D::setTransform(double a, double b, double c, double d, double e, double f) const { num(h_, SetTransform, a, b, c, d, e, f); }
void Ctx2D::resetTransform() const { num(h_, ResetTransform); }

void Ctx2D::beginPath() const { num(h_, BeginPath); }
void Ctx2D::closePath() const { num(h_, ClosePath); }
void Ctx2D::moveTo(double x, double y) const { num(h_, MoveTo, x, y); }
void Ctx2D::lineTo(double x, double y) const { num(h_, LineTo, x, y); }
void Ctx2D::arc(double x, double y, double r, double a0, double a1, bool ccw) const { num(h_, Arc, x, y, r, a0, a1, ccw ? 1 : 0); }
void Ctx2D::arcTo(double x1, double y1, double x2, double y2, double r) const { num(h_, ArcTo, x1, y1, x2, y2, r); }
void Ctx2D::ellipse(double x, double y, double rx, double ry, double rotation, double a0, double a1, bool ccw) const {
  num(h_, Ellipse, x, y, rx, ry, rotation, a0, a1, ccw ? 1 : 0);
}
void Ctx2D::rect(double x, double y, double w, double h) const { num(h_, Rect, x, y, w, h); }
void Ctx2D::roundRect(double x, double y, double w, double h, double r) const { num(h_, RoundRect, x, y, w, h, r); }
void Ctx2D::quadraticCurveTo(double cx, double cy, double x, double y) const { num(h_, QuadTo, cx, cy, x, y); }
void Ctx2D::bezierCurveTo(double c1x, double c1y, double c2x, double c2y, double x, double y) const { num(h_, BezierTo, c1x, c1y, c2x, c2y, x, y); }
void Ctx2D::fill() const { num(h_, Fill); }
void Ctx2D::fillEvenOdd() const { num(h_, FillEvenOdd); }
void Ctx2D::stroke() const { num(h_, Stroke); }
void Ctx2D::clip() const { num(h_, Clip); }
void Ctx2D::polyline(const double* xy, size_t n, bool close) const {
  if (n > 0) canvasBackend::polyline(h_, xy, static_cast<int>(n), close);
}

void Ctx2D::fillRect(double x, double y, double w, double h) const { num(h_, FillRect, x, y, w, h); }
void Ctx2D::strokeRect(double x, double y, double w, double h) const { num(h_, StrokeRect, x, y, w, h); }
void Ctx2D::clearRect(double x, double y, double w, double h) const { num(h_, ClearRect, x, y, w, h); }
void Ctx2D::fillText(const std::string& text, double x, double y) const { str(h_, FillText, text.c_str(), x, y); }
void Ctx2D::fillText(const std::string& text, double x, double y, double maxWidth) const { str(h_, FillTextMax, text.c_str(), x, y, maxWidth); }
void Ctx2D::strokeText(const std::string& text, double x, double y) const { str(h_, StrokeText, text.c_str(), x, y); }
TextMetrics Ctx2D::measureText(const std::string& text) const {
  double m[5] = {0, 0, 0, 0, 0};
  measure(h_, text.c_str(), m);
  TextMetrics t;
  t.width = m[0];
  t.actualBoundingBoxAscent = m[1];
  t.actualBoundingBoxDescent = m[2];
  t.actualBoundingBoxLeft = m[3];
  t.actualBoundingBoxRight = m[4];
  return t;
}
void Ctx2D::drawImage(const Canvas& c, double dx, double dy) const {
  const double v[2] = {dx, dy};
  canvasBackend::drawImage(h_, true, c.el().handle(), v, 2);
}
void Ctx2D::drawImage(const Canvas& c, double dx, double dy, double dw, double dh) const {
  const double v[4] = {dx, dy, dw, dh};
  canvasBackend::drawImage(h_, true, c.el().handle(), v, 4);
}
void Ctx2D::drawImage(const Canvas& c, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh) const {
  const double v[8] = {sx, sy, sw, sh, dx, dy, dw, dh};
  canvasBackend::drawImage(h_, true, c.el().handle(), v, 8);
}
void Ctx2D::drawImage(const Image& i, double dx, double dy) const {
  const double v[2] = {dx, dy};
  canvasBackend::drawImage(h_, false, i.handle, v, 2);
}
void Ctx2D::drawImage(const Image& i, double dx, double dy, double dw, double dh) const {
  const double v[4] = {dx, dy, dw, dh};
  canvasBackend::drawImage(h_, false, i.handle, v, 4);
}
void Ctx2D::drawImage(const Image& i, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh) const {
  const double v[8] = {sx, sy, sw, sh, dx, dy, dw, dh};
  canvasBackend::drawImage(h_, false, i.handle, v, 8);
}
ImageData Ctx2D::getImageData(int x, int y, int w, int h) const {
  ImageData d;
  d.width = w;
  d.height = h;
  d.data.assign(static_cast<size_t>(w > 0 ? w : 0) * static_cast<size_t>(h > 0 ? h : 0) * 4, 0);
  if (!d.data.empty()) canvasBackend::getImageData(h_, x, y, w, h, d.data.data());
  return d;
}
void Ctx2D::putImageData(const ImageData& d, int x, int y) const {
  if (!d.data.empty()) canvasBackend::putImageData(h_, d.data.data(), d.width, d.height, x, y);
}

namespace {
std::shared_ptr<const int> ownGradient(int id) {
  return std::shared_ptr<const int>(new int(id), [](const int* p) {
    canvasBackend::gradientFree(*p);
    delete p;
  });
}
}  // namespace

CanvasGradient Ctx2D::createLinearGradient(double x0, double y0, double x1, double y1) const {
  CanvasGradient g;
  g.h_ = ownGradient(gradient(h_, false, x0, y0, x1, y1, 0, 0));
  return g;
}
CanvasGradient Ctx2D::createRadialGradient(double x0, double y0, double r0, double x1, double y1, double r1) const {
  CanvasGradient g;
  g.h_ = ownGradient(gradient(h_, true, x0, y0, r0, x1, y1, r1));
  return g;
}

}  // namespace fs

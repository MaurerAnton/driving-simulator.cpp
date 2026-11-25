// SPDX-License-Identifier: AGPL-3.0-only
/**
 * canvas.h on this machine: a real 2D canvas, painted in memory with plutovg
 * (third_party/plutovg, MIT) – paths, strokes with their caps, joins and dashes,
 * gradients, clipping, transforms, pictures (PNG and JPEG through stb_image, SVG
 * through plutosvg) and text in real fonts (canvas_fonts.cpp).  What a canvas holds
 * reaches the textures (gl_native.cpp: texImageCanvas) and the tests (getImageData,
 * canvasNative::savePng).
 *
 * As before, the calls are also counted (a test asks whether something was drawn
 * at all) and every context remembers the last texts it was asked to write.
 *
 * How the browser's canvas is followed: the path is kept in device pixels (each
 * point is transformed when it is added, as the canvas specification says);
 * a fill paints that path, a stroke takes it back to the user space of the moment
 * and strokes it there under the current transform (so the line width is
 * transformed with it).  The pixels are premultiplied ARGB, as plutovg keeps them.
 *
 * Not painted (only counted): shadows, `filter`, the blend modes that are not
 * Porter-Duff operators ("multiply", "lighter" … are drawn as "source-over"), WebP
 * pictures.
 */
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "platform/canvas.h"
#include "platform/native/canvas_fonts.h"
#include "platform/native/canvas_native.h"
#include "platform/plat.h"

#include "plutosvg.h"
#include "plutovg.h"

namespace fs {

using namespace canvasBackend;

namespace {

constexpr double PI = 3.14159265358979323846;

// --- transforms -----------------------------------------------------------------------

/** x' = a·x + c·y + e, y' = b·x + d·y + f */
struct M {
  double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
};
/** first `n`, then `m` (m ∘ n) */
M operator*(const M& m, const M& n) {
  return {m.a * n.a + m.c * n.b, m.b * n.a + m.d * n.b, m.a * n.c + m.c * n.d, m.b * n.c + m.d * n.d, m.a * n.e + m.c * n.f + m.e, m.b * n.e + m.d * n.f + m.f};
}
void apply(const M& m, double x, double y, double& ox, double& oy) {
  ox = m.a * x + m.c * y + m.e;
  oy = m.b * x + m.d * y + m.f;
}
bool invert(const M& m, M& out) {
  const double det = m.a * m.d - m.b * m.c;
  if (!(std::fabs(det) > 1e-300) || !std::isfinite(det)) return false;
  out = {m.d / det, -m.b / det, -m.c / det, m.a / det, (m.c * m.f - m.d * m.e) / det, (m.b * m.e - m.a * m.f) / det};
  return true;
}
plutovg_matrix_t pm(const M& m) {
  return {static_cast<float>(m.a), static_cast<float>(m.b), static_cast<float>(m.c), static_cast<float>(m.d), static_cast<float>(m.e), static_cast<float>(m.f)};
}
bool finite(std::initializer_list<double> v) {
  for (double x : v)
    if (!std::isfinite(x)) return false;
  return true;
}

// --- paints -----------------------------------------------------------------------------

struct Gradient {
  bool radial = false;
  double v[6] = {0, 0, 0, 0, 0, 0};
  std::vector<plutovg_gradient_stop_t> stops;
};

std::unordered_map<int, Gradient>& gradients() {
  static auto* g = new std::unordered_map<int, Gradient>();
  return *g;
}
int lastGradient = 0;

struct Paint {
  plutovg_color_t color{0, 0, 0, 1};
  /** a gradient (0: the colour) */
  int gradient = 0;
};

bool parseColor(const std::string& css, plutovg_color_t& out) {
  plutovg_color_t c;
  if (plutovg_color_parse(&c, css.c_str(), static_cast<int>(css.size())) <= 0) {
    // the newer form plutovg does not read: "rgb(1 2 3 / 0.5)"
    std::string s = css;
    for (char& ch : s)
      if (ch == '/') ch = ',';
    std::string t;
    bool prevSpace = false;
    for (char ch : s) {
      if (std::isspace(static_cast<unsigned char>(ch))) {
        prevSpace = true;
        continue;
      }
      if (prevSpace && !t.empty() && t.back() != '(' && t.back() != ',' && ch != ',' && ch != ')') t += ',';
      prevSpace = false;
      t += ch;
    }
    if (plutovg_color_parse(&c, t.c_str(), static_cast<int>(t.size())) <= 0) return false;
  }
  out = c;
  return true;
}

// --- pictures ---------------------------------------------------------------------------

struct Picture {
  plutovg_surface_t* raster = nullptr;
  plutosvg_document_t* svg = nullptr;
  int width = 0, height = 0;
};

std::unordered_map<int, Picture>& pictures() {
  static auto* p = new std::unordered_map<int, Picture>();
  return *p;
}
int lastPicture = 0;

void dropPicture(Picture& p) {
  if (p.raster) plutovg_surface_destroy(p.raster);
  if (p.svg) plutosvg_document_destroy(p.svg);
  p = Picture();
}

// --- a context --------------------------------------------------------------------------

using PathPtr = std::shared_ptr<plutovg_path_t>;
PathPtr newPath() { return PathPtr(plutovg_path_create(), plutovg_path_destroy); }

struct State {
  Paint fill, stroke;
  double lineWidth = 1, miterLimit = 10, dashOffset = 0;
  plutovg_line_cap_t cap = PLUTOVG_LINE_CAP_BUTT;
  plutovg_line_join_t join = PLUTOVG_LINE_JOIN_MITER;
  std::vector<double> dash;
  canvasFonts::FontSpec font;
  std::string align = "start", baseline = "alphabetic", direction = "ltr";
  /** letterSpacing: the number and whether it is in em */
  double spacing = 0;
  bool spacingEm = false;
  double alpha = 1;
  plutovg_operator_t op = PLUTOVG_OPERATOR_SRC_OVER;
  bool smoothing = true;
  M ctm;
  /** the clip: device-space paths, all of which a pixel must be in */
  std::vector<PathPtr> clips;
};

struct Ctx {
  int canvas = 0;
  long calls = 0;
  std::vector<std::string> texts;
  State s;
  std::vector<State> stack;
  PathPtr path = newPath();
  /** the current point and the start of the subpath, in device pixels */
  bool hasPoint = false;
  double px = 0, py = 0, sx = 0, sy = 0;
  plutovg_surface_t* surface = nullptr;
  plutovg_canvas_t* vg = nullptr;
  uint64_t version = 0;

  void dropSurface() {
    if (vg) plutovg_canvas_destroy(vg);
    if (surface) plutovg_surface_destroy(surface);
    vg = nullptr;
    surface = nullptr;
  }
  /** the canvas was given a size again: blank, and the context as new */
  void reset() {
    dropSurface();
    s = State();
    stack.clear();
    plutovg_path_reset(path.get());
    hasPoint = false;
    version++;
  }
};

std::vector<std::unique_ptr<Ctx>>& contexts() {
  // (never destroyed: canvases of static objects go while the program ends)
  static auto* c = new std::vector<std::unique_ptr<Ctx>>();
  if (c->empty()) c->push_back(nullptr);
  return *c;
}
std::map<int, int>& byCanvas() {
  static auto* m = new std::map<int, int>();
  return *m;
}

Ctx* ctxOf(int h) { return h > 0 && h < static_cast<int>(contexts().size()) ? contexts()[static_cast<size_t>(h)].get() : nullptr; }
Ctx* ctxOfCanvas(int canvasElement) {
  const auto it = byCanvas().find(canvasElement);
  return it != byCanvas().end() ? ctxOf(it->second) : nullptr;
}

/** the size the canvas element has */
void sizeOf(int canvasElement, int& w, int& h) {
  w = static_cast<int>(dom::backend::getNum(canvasElement, "width"));
  h = static_cast<int>(dom::backend::getNum(canvasElement, "height"));
}

constexpr int MAX_SIDE = 16384;

/** the pixels of a context if it has the canvas's size (null: never painted – transparent) */
const plutovg_surface_t* surfaceOf(const Ctx* c) {
  if (!c || !c->surface) return nullptr;
  int w = 0, h = 0;
  sizeOf(c->canvas, w, h);
  return plutovg_surface_get_width(c->surface) == w && plutovg_surface_get_height(c->surface) == h ? c->surface : nullptr;
}

/** the pixels, made when first painted (false: the canvas has no pixels – a size of 0) */
bool ensure(Ctx& c) {
  int w = 0, h = 0;
  sizeOf(c.canvas, w, h);
  if (c.surface && (plutovg_surface_get_width(c.surface) != w || plutovg_surface_get_height(c.surface) != h)) c.dropSurface();
  if (c.surface) return true;
  if (w <= 0 || h <= 0 || w > MAX_SIDE || h > MAX_SIDE) return false;
  c.surface = plutovg_surface_create(w, h);
  if (!c.surface) return false;
  c.vg = plutovg_canvas_create(c.surface);
  return c.vg != nullptr;
}

/** before painting: the clip, the operator and the alpha – false: nothing to paint on */
bool begin(Ctx& c) {
  if (!ensure(c)) return false;
  plutovg_canvas_t* vg = c.vg;
  if (!c.s.clips.empty()) {
    plutovg_canvas_save(vg);
    plutovg_canvas_reset_matrix(vg);
    plutovg_canvas_set_fill_rule(vg, PLUTOVG_FILL_RULE_NON_ZERO);
    for (const PathPtr& p : c.s.clips) plutovg_canvas_clip_path(vg, p.get());
  }
  plutovg_canvas_set_operator(vg, c.s.op);
  plutovg_canvas_set_opacity(vg, static_cast<float>(c.s.alpha));
  return true;
}
void end(Ctx& c) {
  if (!c.s.clips.empty()) plutovg_canvas_restore(c.vg);
  plutovg_canvas_set_operator(c.vg, PLUTOVG_OPERATOR_SRC_OVER);
  plutovg_canvas_set_opacity(c.vg, 1);
  c.version++;
}

/** the paint, its coordinates mapped to device pixels by `toDevice` (relative to the canvas's own matrix); false: it paints nothing */
bool setPaint(Ctx& c, const Paint& p, const M& toDevice) {
  if (!p.gradient) {
    plutovg_canvas_set_rgba(c.vg, p.color.r, p.color.g, p.color.b, p.color.a);
    return true;
  }
  const auto it = gradients().find(p.gradient);
  if (it == gradients().end() || it->second.stops.empty()) return false;
  const Gradient& g = it->second;
  const plutovg_matrix_t m = pm(toDevice);
  const auto* stops = g.stops.data();
  const int n = static_cast<int>(g.stops.size());
  const auto F = [](double v) { return static_cast<float>(v); };
  if (g.radial) {
    // (canvas: from the circle x0 y0 r0 to the circle x1 y1 r1; plutovg: the end circle c, the focal circle f)
    if (g.v[0] == g.v[3] && g.v[1] == g.v[4] && g.v[2] == g.v[5]) return false;
    plutovg_canvas_set_radial_gradient(c.vg, F(g.v[3]), F(g.v[4]), F(g.v[5]), F(g.v[0]), F(g.v[1]), F(g.v[2]), PLUTOVG_SPREAD_METHOD_PAD, stops, n, &m);
  } else {
    if (g.v[0] == g.v[2] && g.v[1] == g.v[3]) return false;
    plutovg_canvas_set_linear_gradient(c.vg, F(g.v[0]), F(g.v[1]), F(g.v[2]), F(g.v[3]), PLUTOVG_SPREAD_METHOD_PAD, stops, n, &m);
  }
  return true;
}

/** fill a path given in device pixels */
void fillDevice(Ctx& c, const plutovg_path_t* path, plutovg_fill_rule_t rule) {
  if (!begin(c)) return;
  plutovg_canvas_reset_matrix(c.vg);
  if (setPaint(c, c.s.fill, c.s.ctm)) {
    plutovg_canvas_set_fill_rule(c.vg, rule);
    plutovg_canvas_fill_path(c.vg, path);
  }
  end(c);
}

/** stroke a path given in device pixels (taken back to user space to be stroked under the transform) */
void strokeDevice(Ctx& c, const plutovg_path_t* path) {
  M inv;
  if (!(c.s.lineWidth > 0) || !invert(c.s.ctm, inv)) return;
  if (!begin(c)) return;
  plutovg_path_t* user = plutovg_path_clone(path);
  const plutovg_matrix_t im = pm(inv);
  plutovg_path_transform(user, &im);
  const plutovg_matrix_t cm = pm(c.s.ctm);
  plutovg_canvas_set_matrix(c.vg, &cm);
  if (setPaint(c, c.s.stroke, M())) {
    plutovg_canvas_set_line_width(c.vg, static_cast<float>(c.s.lineWidth));
    plutovg_canvas_set_line_cap(c.vg, c.s.cap);
    plutovg_canvas_set_line_join(c.vg, c.s.join);
    plutovg_canvas_set_miter_limit(c.vg, static_cast<float>(c.s.miterLimit));
    if (!c.s.dash.empty()) {
      // (an odd list is taken twice, as the canvas does)
      std::vector<float> d;
      for (size_t k = 0; k < (c.s.dash.size() % 2 ? 2u : 1u); k++)
        for (double v : c.s.dash) d.push_back(static_cast<float>(v));
      plutovg_canvas_set_dash(c.vg, static_cast<float>(c.s.dashOffset), d.data(), static_cast<int>(d.size()));
    } else plutovg_canvas_set_dash(c.vg, 0, nullptr, 0);
    plutovg_canvas_stroke_path(c.vg, user);
    plutovg_canvas_set_dash(c.vg, 0, nullptr, 0);
  }
  plutovg_path_destroy(user);
  plutovg_canvas_reset_matrix(c.vg);
  end(c);
}

// --- building the path (device pixels) ---------------------------------------------------

void devMove(Ctx& c, double x, double y) {
  double dx, dy;
  apply(c.s.ctm, x, y, dx, dy);
  plutovg_path_move_to(c.path.get(), static_cast<float>(dx), static_cast<float>(dy));
  c.hasPoint = true;
  c.px = c.sx = dx;
  c.py = c.sy = dy;
}
void devLine(Ctx& c, double x, double y) {
  if (!c.hasPoint) return devMove(c, x, y);
  double dx, dy;
  apply(c.s.ctm, x, y, dx, dy);
  plutovg_path_line_to(c.path.get(), static_cast<float>(dx), static_cast<float>(dy));
  c.px = dx;
  c.py = dy;
}
void devCubic(Ctx& c, double x1, double y1, double x2, double y2, double x, double y) {
  if (!c.hasPoint) devMove(c, x1, y1);
  double a, b, cc, d, e, f;
  apply(c.s.ctm, x1, y1, a, b);
  apply(c.s.ctm, x2, y2, cc, d);
  apply(c.s.ctm, x, y, e, f);
  plutovg_path_cubic_to(c.path.get(), static_cast<float>(a), static_cast<float>(b), static_cast<float>(cc), static_cast<float>(d), static_cast<float>(e), static_cast<float>(f));
  c.px = e;
  c.py = f;
}
void devQuad(Ctx& c, double x1, double y1, double x, double y) {
  if (!c.hasPoint) devMove(c, x1, y1);
  double a, b, e, f;
  apply(c.s.ctm, x1, y1, a, b);
  apply(c.s.ctm, x, y, e, f);
  plutovg_path_quad_to(c.path.get(), static_cast<float>(a), static_cast<float>(b), static_cast<float>(e), static_cast<float>(f));
  c.px = e;
  c.py = f;
}
void devClose(Ctx& c) {
  if (!c.hasPoint) return;
  plutovg_path_close(c.path.get());
  c.px = c.sx;
  c.py = c.sy;
}

/** an elliptic arc (user space): centre, radii, rotation, start angle and the angle swept (signed) */
void addArc(Ctx& c, double cx, double cy, double rx, double ry, double rot, double a0, double sweep) {
  const double cr = std::cos(rot), sr = std::sin(rot);
  const auto at = [&](double t, double& x, double& y) {
    const double ex = rx * std::cos(t), ey = ry * std::sin(t);
    x = cx + ex * cr - ey * sr;
    y = cy + ex * sr + ey * cr;
  };
  const auto tangent = [&](double t, double& x, double& y) {
    const double ex = -rx * std::sin(t), ey = ry * std::cos(t);
    x = ex * cr - ey * sr;
    y = ex * sr + ey * cr;
  };
  double x, y;
  at(a0, x, y);
  if (c.hasPoint) devLine(c, x, y);
  else devMove(c, x, y);
  if (sweep == 0) return;
  const int n = std::max(1, static_cast<int>(std::ceil(std::fabs(sweep) / (PI / 2) - 1e-9)));
  const double step = sweep / n;
  const double k = 4.0 / 3.0 * std::tan(step / 4);
  double t = a0;
  for (int i = 0; i < n; i++) {
    const double t1 = (i == n - 1) ? a0 + sweep : t + step;
    double x0, y0, x1, y1, tx0, ty0, tx1, ty1;
    at(t, x0, y0);
    at(t1, x1, y1);
    tangent(t, tx0, ty0);
    tangent(t1, tx1, ty1);
    devCubic(c, x0 + k * tx0, y0 + k * ty0, x1 - k * tx1, y1 - k * ty1, x1, y1);
    t = t1;
  }
}

/** the canvas's rules for the angles of arc() and ellipse(): the angle swept */
double sweepOf(double a0, double a1, bool ccw) {
  const double TAU = 2 * PI;
  double sweep = a1 - a0;
  if (!ccw) {
    if (sweep >= TAU) return TAU;
    sweep = std::fmod(sweep, TAU);
    if (sweep < 0) sweep += TAU;
  } else {
    if (sweep <= -TAU) return -TAU;
    sweep = std::fmod(sweep, TAU);
    if (sweep > 0) sweep -= TAU;
  }
  return sweep;
}

void arcTo(Ctx& c, double x1, double y1, double x2, double y2, double r) {
  if (!finite({x1, y1, x2, y2, r}) || r < 0) return;
  if (!c.hasPoint) return devMove(c, x1, y1);
  M inv;
  if (!invert(c.s.ctm, inv)) return;
  double x0, y0;
  apply(inv, c.px, c.py, x0, y0);
  const double v1x = x0 - x1, v1y = y0 - y1, v2x = x2 - x1, v2y = y2 - y1;
  const double l1 = std::hypot(v1x, v1y), l2 = std::hypot(v2x, v2y);
  const double cross = v1x * v2y - v1y * v2x;
  if (l1 < 1e-12 || l2 < 1e-12 || r == 0 || std::fabs(cross) < 1e-12 * l1 * l2) return devLine(c, x1, y1);
  const double cosA = std::clamp((v1x * v2x + v1y * v2y) / (l1 * l2), -1.0, 1.0);
  const double angle = std::acos(cosA);
  const double dist = r / std::tan(angle / 2);
  const double t1x = x1 + v1x / l1 * dist, t1y = y1 + v1y / l1 * dist;
  const double t2x = x1 + v2x / l2 * dist, t2y = y1 + v2y / l2 * dist;
  double bx = v1x / l1 + v2x / l2, by = v1y / l1 + v2y / l2;
  const double bl = std::hypot(bx, by);
  bx /= bl;
  by /= bl;
  const double cd = r / std::sin(angle / 2);
  const double cx = x1 + bx * cd, cy = y1 + by * cd;
  const double a0 = std::atan2(t1y - cy, t1x - cx);
  double sweep = std::atan2(t2y - cy, t2x - cx) - a0;
  while (sweep > PI) sweep -= 2 * PI;
  while (sweep < -PI) sweep += 2 * PI;
  devLine(c, t1x, t1y);
  addArc(c, cx, cy, r, r, 0, a0, sweep);
}

void rectPath(Ctx& c, double x, double y, double w, double h) {
  devMove(c, x, y);
  devLine(c, x + w, y);
  devLine(c, x + w, y + h);
  devLine(c, x, y + h);
  devClose(c);
  devMove(c, x, y);
}

void roundRectPath(Ctx& c, double x, double y, double w, double h, double r) {
  if (w < 0) {
    x += w;
    w = -w;
  }
  if (h < 0) {
    y += h;
    h = -h;
  }
  r = std::max(0.0, std::min(r, std::min(w, h) / 2));
  if (r == 0) return rectPath(c, x, y, w, h);
  devMove(c, x + r, y);
  devLine(c, x + w - r, y);
  addArc(c, x + w - r, y + r, r, r, 0, -PI / 2, PI / 2);
  devLine(c, x + w, y + h - r);
  addArc(c, x + w - r, y + h - r, r, r, 0, 0, PI / 2);
  devLine(c, x + r, y + h);
  addArc(c, x + r, y + h - r, r, r, 0, PI / 2, PI / 2);
  devLine(c, x, y + r);
  addArc(c, x + r, y + r, r, r, 0, PI, PI / 2);
  devClose(c);
  devMove(c, x, y);
}

/** a rectangle as a path of its own (fillRect, strokeRect, clearRect) */
PathPtr rectDevice(const M& m, double x, double y, double w, double h) {
  PathPtr p = newPath();
  double ax, ay;
  apply(m, x, y, ax, ay);
  plutovg_path_move_to(p.get(), static_cast<float>(ax), static_cast<float>(ay));
  apply(m, x + w, y, ax, ay);
  plutovg_path_line_to(p.get(), static_cast<float>(ax), static_cast<float>(ay));
  apply(m, x + w, y + h, ax, ay);
  plutovg_path_line_to(p.get(), static_cast<float>(ax), static_cast<float>(ay));
  apply(m, x, y + h, ax, ay);
  plutovg_path_line_to(p.get(), static_cast<float>(ax), static_cast<float>(ay));
  plutovg_path_close(p.get());
  return p;
}

// --- text ---------------------------------------------------------------------------------

/** glyph outlines into a plutovg path, through a matrix */
struct PathSink : canvasFonts::OutlineSink {
  plutovg_path_t* path;
  M m;
  double ox = 0;
  PathSink(plutovg_path_t* p, const M& matrix) : path(p), m(matrix) {}
  void map(double x, double y, float& rx, float& ry) const {
    double a, b;
    apply(m, x + ox, y, a, b);
    rx = static_cast<float>(a);
    ry = static_cast<float>(b);
  }
  void moveTo(double x, double y) override {
    float a, b;
    map(x, y, a, b);
    plutovg_path_move_to(path, a, b);
  }
  void lineTo(double x, double y) override {
    float a, b;
    map(x, y, a, b);
    plutovg_path_line_to(path, a, b);
  }
  void quadTo(double cx, double cy, double x, double y) override {
    float a, b, e, f;
    map(cx, cy, a, b);
    map(x, y, e, f);
    plutovg_path_quad_to(path, a, b, e, f);
  }
  void cubicTo(double c1x, double c1y, double c2x, double c2y, double x, double y) override {
    float a, b, cc, d, e, f;
    map(c1x, c1y, a, b);
    map(c2x, c2y, cc, d);
    map(x, y, e, f);
    plutovg_path_cubic_to(path, a, b, cc, d, e, f);
  }
  void close() override { plutovg_path_close(path); }
};

double spacingPx(const State& s) { return s.spacingEm ? s.spacing * s.font.px : s.spacing; }

/** where the text starts left of the point given (textAlign, direction) – a factor of its width (0, -0.5, -1) */
double alignFactor(const State& s) {
  const bool rtl = s.direction == "rtl";
  if (s.align == "center") return -0.5;
  if (s.align == "right" || (s.align == "end" && !rtl) || (s.align == "start" && rtl)) return -1;
  return 0;
}
/** how far the alphabetic baseline lies below the point given (textBaseline) */
double baselineShift(const State& s, const canvasFonts::Layout& l) {
  if (s.baseline == "top") return l.ascent;
  if (s.baseline == "hanging") return l.ascent * 0.8;
  if (s.baseline == "middle") return (l.ascent - l.descent) / 2;
  if (s.baseline == "bottom" || s.baseline == "ideographic") return -l.descent;
  return 0;
}

void drawText(Ctx& c, const std::string& text, double x, double y, double maxWidth, bool stroke) {
  if (!finite({x, y}) || std::isnan(maxWidth) || maxWidth <= 0) return;
  canvasFonts::Layout l;
  if (!canvasFonts::layout(c.s.font, text, spacingPx(c.s), l) || l.glyphs.empty()) return;
  const double squeeze = std::isfinite(maxWidth) && l.width > maxWidth ? maxWidth / l.width : 1;
  // text space: from the start of the text on its baseline (x right, y down), squeezed, slanted where the face has no italic
  M t;
  t.e = x + alignFactor(c.s) * l.width * squeeze;
  t.f = y + baselineShift(c.s, l);
  t.a = squeeze;
  if (l.slant) t.c = -0.2;
  PathPtr p = newPath();
  PathSink sink(p.get(), c.s.ctm * t);
  for (const canvasFonts::Glyph& g : l.glyphs) {
    sink.ox = g.x;
    canvasFonts::outline(g, l.px, sink);
  }
  if (stroke) strokeDevice(c, p.get());
  else fillDevice(c, p.get(), PLUTOVG_FILL_RULE_NON_ZERO);
}

// --- pictures on the canvas ----------------------------------------------------------------

struct Tap {
  int i;
  float w;
};
/** which source pixels make each of `out` pixels of the span from `s0`, `len` long (area average when shrinking, linear when growing) */
std::vector<std::vector<Tap>> taps(double s0, double len, int out, int limit) {
  std::vector<std::vector<Tap>> t(static_cast<size_t>(out));
  const double scale = len / out;
  const int lo = std::clamp(static_cast<int>(std::floor(s0)), 0, limit - 1);
  const int hi = std::clamp(static_cast<int>(std::ceil(s0 + len)) - 1, lo, limit - 1);
  for (int o = 0; o < out; o++) {
    std::vector<Tap>& v = t[static_cast<size_t>(o)];
    if (scale > 1) {
      const double a = s0 + o * scale, b = a + scale;
      for (int k = static_cast<int>(std::floor(a)); k < static_cast<int>(std::ceil(b)); k++) {
        const double w = std::min(b, k + 1.0) - std::max(a, static_cast<double>(k));
        if (w > 0) v.push_back({std::clamp(k, lo, hi), static_cast<float>(w)});
      }
    } else {
      const double cpos = s0 + (o + 0.5) * scale - 0.5;
      const int k = static_cast<int>(std::floor(cpos));
      const double f = cpos - k;
      v.push_back({std::clamp(k, lo, hi), static_cast<float>(1 - f)});
      v.push_back({std::clamp(k + 1, lo, hi), static_cast<float>(f)});
    }
    float sum = 0;
    for (const Tap& tp : v) sum += tp.w;
    if (sum > 0)
      for (Tap& tp : v) tp.w /= sum;
  }
  return t;
}

/** a part of a picture resampled to `tw` × `th` pixels (premultiplied ARGB, as plutovg keeps it) */
plutovg_surface_t* resample(const plutovg_surface_t* src, double sx, double sy, double sw, double sh, int tw, int th) {
  const int w = plutovg_surface_get_width(src), h = plutovg_surface_get_height(src), stride = plutovg_surface_get_stride(src);
  const unsigned char* data = plutovg_surface_get_data(src);
  const auto tx = taps(sx, sw, tw, w);
  const auto ty = taps(sy, sh, th, h);
  // the rows the columns need, across first
  int r0 = h, r1 = -1;
  for (const auto& v : ty)
    for (const Tap& t : v) r0 = std::min(r0, t.i), r1 = std::max(r1, t.i);
  if (r1 < r0) return nullptr;
  const int rows = r1 - r0 + 1;
  std::vector<float> across(static_cast<size_t>(rows) * static_cast<size_t>(tw) * 4);
  for (int r = 0; r < rows; r++) {
    const uint32_t* row = reinterpret_cast<const uint32_t*>(data + static_cast<size_t>(r0 + r) * static_cast<size_t>(stride));
    float* o = across.data() + static_cast<size_t>(r) * static_cast<size_t>(tw) * 4;
    for (int x = 0; x < tw; x++) {
      float a = 0, rr = 0, g = 0, b = 0;
      for (const Tap& t : tx[static_cast<size_t>(x)]) {
        const uint32_t p = row[t.i];
        a += t.w * static_cast<float>(p >> 24);
        rr += t.w * static_cast<float>((p >> 16) & 0xff);
        g += t.w * static_cast<float>((p >> 8) & 0xff);
        b += t.w * static_cast<float>(p & 0xff);
      }
      o[x * 4] = a, o[x * 4 + 1] = rr, o[x * 4 + 2] = g, o[x * 4 + 3] = b;
    }
  }
  plutovg_surface_t* out = plutovg_surface_create(tw, th);
  if (!out) return nullptr;
  unsigned char* od = plutovg_surface_get_data(out);
  const int ostride = plutovg_surface_get_stride(out);
  const auto byte = [](float v) { return static_cast<uint32_t>(std::clamp(std::lround(v), 0L, 255L)); };
  for (int y = 0; y < th; y++) {
    uint32_t* orow = reinterpret_cast<uint32_t*>(od + static_cast<size_t>(y) * static_cast<size_t>(ostride));
    for (int x = 0; x < tw; x++) {
      float a = 0, rr = 0, g = 0, b = 0;
      for (const Tap& t : ty[static_cast<size_t>(y)]) {
        const float* p = across.data() + (static_cast<size_t>(t.i - r0) * static_cast<size_t>(tw) + static_cast<size_t>(x)) * 4;
        a += t.w * p[0], rr += t.w * p[1], g += t.w * p[2], b += t.w * p[3];
      }
      const uint32_t A = byte(a);
      orow[x] = (A << 24) | (std::min(byte(rr), A) << 16) | (std::min(byte(g), A) << 8) | std::min(byte(b), A);
    }
  }
  return out;
}

/** drawImage of pixels: the part sx sy sw sh of `src` into the rectangle dx dy dw dh (user space) */
void drawSurface(Ctx& c, const plutovg_surface_t* src, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh) {
  if (!finite({sx, sy, sw, sh, dx, dy, dw, dh})) return;
  // (negative sizes: the rectangle the other way round, as browsers take it)
  if (sw < 0) sx += sw, sw = -sw;
  if (sh < 0) sy += sh, sh = -sh;
  if (dw < 0) dx += dw, dw = -dw;
  if (dh < 0) dy += dh, dh = -dh;
  const int W = plutovg_surface_get_width(src), H = plutovg_surface_get_height(src);
  // the source cut to the picture, the destination with it
  const double kx = sw > 0 ? dw / sw : 0, ky = sh > 0 ? dh / sh : 0;
  if (sx < 0) dx -= sx * kx, dw += sx * kx, sw += sx, sx = 0;
  if (sy < 0) dy -= sy * ky, dh += sy * ky, sh += sy, sy = 0;
  if (sx + sw > W) dw -= (sx + sw - W) * kx, sw = W - sx;
  if (sy + sh > H) dh -= (sy + sh - H) * ky, sh = H - sy;
  if (!(sw > 0 && sh > 0 && dw > 0 && dh > 0)) return;
  if (!begin(c)) return;
  const M m = c.s.ctm;
  // how many device pixels a source pixel becomes
  const double ex = std::hypot(m.a, m.b) * dw / sw, ey = std::hypot(m.c, m.d) * dh / sh;
  plutovg_surface_t* own = nullptr;
  const plutovg_surface_t* tex = src;
  M texM;   // texture pixels → user space
  const bool exact = std::fabs(ex - 1) < 1e-6 && std::fabs(ey - 1) < 1e-6 && sx == std::floor(sx) && sy == std::floor(sy);
  if (c.s.smoothing && !exact) {
    const int tw = std::clamp(static_cast<int>(std::lround(sw * ex)), 1, MAX_SIDE), th = std::clamp(static_cast<int>(std::lround(sh * ey)), 1, MAX_SIDE);
    own = resample(src, sx, sy, sw, sh, tw, th);
    if (own) {
      tex = own;
      texM = {dw / tw, 0, 0, dh / th, dx, dy};
    }
  }
  if (!own) texM = M{1, 0, 0, 1, dx, dy} * M{dw / sw, 0, 0, dh / sh, 0, 0} * M{1, 0, 0, 1, -sx, -sy};
  plutovg_canvas_reset_matrix(c.vg);
  const plutovg_matrix_t tm = pm(m * texM);
  plutovg_canvas_set_texture(c.vg, const_cast<plutovg_surface_t*>(tex), PLUTOVG_TEXTURE_TYPE_PLAIN, 1, &tm);
  plutovg_canvas_set_fill_rule(c.vg, PLUTOVG_FILL_RULE_NON_ZERO);
  const PathPtr r = rectDevice(m, dx, dy, dw, dh);
  plutovg_canvas_fill_path(c.vg, r.get());
  // (the canvas lets go of the texture)
  plutovg_canvas_set_rgba(c.vg, 0, 0, 0, 1);
  if (own) plutovg_surface_destroy(own);
  end(c);
}

/** an SVG picture, rasterised at the size it is drawn at (as browsers draw SVG images) */
void drawSvg(Ctx& c, const Picture& pic, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh) {
  if (!finite({sx, sy, sw, sh, dx, dy, dw, dh}) || sw == 0 || sh == 0) return;
  const M& m = c.s.ctm;
  const double ex = std::hypot(m.a, m.b) * std::fabs(dw / sw), ey = std::hypot(m.c, m.d) * std::fabs(dh / sh);
  const int rw = std::clamp(static_cast<int>(std::ceil(pic.width * ex)), 1, 8192), rh = std::clamp(static_cast<int>(std::ceil(pic.height * ey)), 1, 8192);
  plutovg_surface_t* r = plutosvg_document_render_to_surface(pic.svg, nullptr, rw, rh, nullptr, nullptr, nullptr);
  if (!r) return;
  const double fx = static_cast<double>(rw) / pic.width, fy = static_cast<double>(rh) / pic.height;
  drawSurface(c, r, sx * fx, sy * fy, sw * fx, sh * fy, dx, dy, dw, dh);
  plutovg_surface_destroy(r);
}

// --- PNG and base64 ---------------------------------------------------------------------------

std::string base64(const std::vector<unsigned char>& in) {
  static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((in.size() + 2) / 3 * 4);
  for (size_t i = 0; i < in.size(); i += 3) {
    const uint32_t v = (static_cast<uint32_t>(in[i]) << 16) | (i + 1 < in.size() ? static_cast<uint32_t>(in[i + 1]) << 8 : 0) | (i + 2 < in.size() ? in[i + 2] : 0);
    out += k[(v >> 18) & 63];
    out += k[(v >> 12) & 63];
    out += i + 1 < in.size() ? k[(v >> 6) & 63] : '=';
    out += i + 2 < in.size() ? k[v & 63] : '=';
  }
  return out;
}

Bytes unbase64(const std::string& s) {
  Bytes out;
  uint32_t v = 0;
  int bits = 0;
  for (char ch : s) {
    int d;
    if (ch >= 'A' && ch <= 'Z') d = ch - 'A';
    else if (ch >= 'a' && ch <= 'z') d = ch - 'a' + 26;
    else if (ch >= '0' && ch <= '9') d = ch - '0' + 52;
    else if (ch == '+' || ch == '-') d = 62;
    else if (ch == '/' || ch == '_') d = 63;
    else continue;
    v = (v << 6) | static_cast<uint32_t>(d);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8_t>((v >> bits) & 0xff));
    }
  }
  return out;
}

Bytes percentDecode(const std::string& s) {
  Bytes out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      out.push_back(static_cast<uint8_t>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16)));
      i += 2;
    } else out.push_back(static_cast<uint8_t>(s[i]));
  }
  return out;
}

void appendBytes(void* closure, void* data, int size) {
  auto* v = static_cast<std::vector<unsigned char>*>(closure);
  const auto* p = static_cast<const unsigned char*>(data);
  v->insert(v->end(), p, p + size);
}

/** a picture from the bytes of a file: SVG (text that starts with markup), else what stb_image reads (PNG, JPEG, GIF, BMP) */
Image decodeBytes(const Bytes& bytes) {
  Image img;
  if (bytes.empty()) return img;
  size_t i = 0;
  if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) i = 3;
  while (i < bytes.size() && std::isspace(bytes[i])) i++;
  Picture p;
  if (i < bytes.size() && bytes[i] == '<') {
    char* copy = static_cast<char*>(std::malloc(bytes.size()));
    if (!copy) return img;
    std::memcpy(copy, bytes.data(), bytes.size());
    p.svg = plutosvg_document_load_from_data(copy, static_cast<int>(bytes.size()), -1, -1, std::free, copy);
    if (!p.svg) return img;
    p.width = static_cast<int>(std::ceil(plutosvg_document_get_width(p.svg)));
    p.height = static_cast<int>(std::ceil(plutosvg_document_get_height(p.svg)));
    if (p.width <= 0 || p.height <= 0) {
      dropPicture(p);
      return img;
    }
  } else {
    p.raster = plutovg_surface_load_from_image_data(bytes.data(), static_cast<int>(bytes.size()));
    if (!p.raster) return img;
    p.width = plutovg_surface_get_width(p.raster);
    p.height = plutovg_surface_get_height(p.raster);
  }
  img.handle = ++lastPicture;
  img.width = p.width;
  img.height = p.height;
  pictures()[img.handle] = p;
  return img;
}

Task<Image> fetchPicture(std::string url) {
  const plat::Response r = co_await plat::fetch(url);
  co_return r.ok() ? decodeBytes(r.body) : Image();
}

/** pixels as RGBA bytes, top row first (premultiplied or not); what lies outside stays as it is */
void readPixels(const plutovg_surface_t* s, int x, int y, int w, int h, uint8_t* out, bool premultiplied) {
  const int W = plutovg_surface_get_width(s), H = plutovg_surface_get_height(s), stride = plutovg_surface_get_stride(s);
  const unsigned char* d = plutovg_surface_get_data(s);
  for (int r = 0; r < h; r++) {
    const int yy = y + r;
    if (yy < 0 || yy >= H) continue;
    const uint32_t* row = reinterpret_cast<const uint32_t*>(d + static_cast<size_t>(yy) * static_cast<size_t>(stride));
    for (int k = 0; k < w; k++) {
      const int xx = x + k;
      if (xx < 0 || xx >= W) continue;
      const uint32_t p = row[xx];
      uint32_t a = p >> 24, rr = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff;
      if (!premultiplied && a != 0 && a != 255) {
        rr = std::min<uint32_t>(255, (rr * 255 + a / 2) / a);
        g = std::min<uint32_t>(255, (g * 255 + a / 2) / a);
        b = std::min<uint32_t>(255, (b * 255 + a / 2) / a);
      }
      uint8_t* o = out + (static_cast<size_t>(r) * static_cast<size_t>(w) + static_cast<size_t>(k)) * 4;
      o[0] = static_cast<uint8_t>(rr), o[1] = static_cast<uint8_t>(g), o[2] = static_cast<uint8_t>(b), o[3] = static_cast<uint8_t>(a);
    }
  }
}

void flipRows(std::vector<uint8_t>& rgba, int width, int height) {
  const size_t row = static_cast<size_t>(width) * 4;
  for (int y = 0; y < height / 2; y++)
    std::swap_ranges(rgba.begin() + static_cast<long>(static_cast<size_t>(y) * row), rgba.begin() + static_cast<long>(static_cast<size_t>(y + 1) * row),
                     rgba.begin() + static_cast<long>(static_cast<size_t>(height - 1 - y) * row));
}

plutovg_operator_t operatorOf(const std::string& v, plutovg_operator_t keep) {
  static const std::map<std::string, plutovg_operator_t> ops = {
      {"source-over", PLUTOVG_OPERATOR_SRC_OVER},      {"copy", PLUTOVG_OPERATOR_SRC},           {"destination-over", PLUTOVG_OPERATOR_DST_OVER}, {"source-in", PLUTOVG_OPERATOR_SRC_IN},
      {"destination-in", PLUTOVG_OPERATOR_DST_IN},     {"source-out", PLUTOVG_OPERATOR_SRC_OUT}, {"destination-out", PLUTOVG_OPERATOR_DST_OUT},  {"source-atop", PLUTOVG_OPERATOR_SRC_ATOP},
      {"destination-atop", PLUTOVG_OPERATOR_DST_ATOP}, {"xor", PLUTOVG_OPERATOR_XOR},
  };
  static const char* const blends[] = {"lighter",    "multiply",   "screen",     "overlay",    "darken", "lighten",    "color-dodge", "color-burn",
                                       "hard-light", "soft-light", "difference", "exclusion", "hue",    "saturation", "color",       "luminosity"};
  const auto it = ops.find(v);
  if (it != ops.end()) return it->second;
  for (const char* b : blends)
    if (v == b) return PLUTOVG_OPERATOR_SRC_OVER;
  return keep;
}

}  // namespace

namespace canvasBackend {

int context(int canvasElement) {
  auto it = byCanvas().find(canvasElement);
  if (it != byCanvas().end()) return it->second;
  auto c = std::make_unique<Ctx>();
  c->canvas = canvasElement;
  contexts().push_back(std::move(c));
  int h = static_cast<int>(contexts().size()) - 1;
  byCanvas()[canvasElement] = h;
  return h;
}

void num(int ctx, int op, double a, double b, double c_, double d, double e, double f, double g, double h) {
  Ctx* cp = ctxOf(ctx);
  if (!cp) return;
  Ctx& c = *cp;
  c.calls++;
  State& s = c.s;
  switch (op) {
    case LineWidth:
      if (std::isfinite(a) && a > 0) s.lineWidth = a;
      break;
    case MiterLimit:
      if (std::isfinite(a) && a > 0) s.miterLimit = a;
      break;
    case LineDashOffset:
      if (std::isfinite(a)) s.dashOffset = a;
      break;
    case GlobalAlpha:
      if (std::isfinite(a) && a >= 0 && a <= 1) s.alpha = a;
      break;
    case ImageSmoothing:
      s.smoothing = a != 0;
      break;
    case Save:
      c.stack.push_back(s);
      break;
    case Restore:
      if (!c.stack.empty()) {
        s = c.stack.back();
        c.stack.pop_back();
      }
      break;
    case Translate:
      if (finite({a, b})) s.ctm = s.ctm * M{1, 0, 0, 1, a, b};
      break;
    case Rotate:
      if (finite({a})) s.ctm = s.ctm * M{std::cos(a), std::sin(a), -std::sin(a), std::cos(a), 0, 0};
      break;
    case Scale:
      if (finite({a, b})) s.ctm = s.ctm * M{a, 0, 0, b, 0, 0};
      break;
    case Transform:
      if (finite({a, b, c_, d, e, f})) s.ctm = s.ctm * M{a, b, c_, d, e, f};
      break;
    case SetTransform:
      if (finite({a, b, c_, d, e, f})) s.ctm = M{a, b, c_, d, e, f};
      break;
    case ResetTransform:
      s.ctm = M();
      break;
    case BeginPath:
      plutovg_path_reset(c.path.get());
      c.hasPoint = false;
      break;
    case ClosePath:
      devClose(c);
      break;
    case MoveTo:
      if (finite({a, b})) devMove(c, a, b);
      break;
    case LineTo:
      if (finite({a, b})) devLine(c, a, b);
      break;
    case Arc:
      if (finite({a, b, c_, d, e}) && c_ >= 0) addArc(c, a, b, c_, c_, 0, d, sweepOf(d, e, f != 0));
      break;
    case ArcTo:
      arcTo(c, a, b, c_, d, e);
      break;
    case Ellipse:
      if (finite({a, b, c_, d, e, f, g}) && c_ >= 0 && d >= 0) addArc(c, a, b, c_, d, e, f, sweepOf(f, g, h != 0));
      break;
    case Rect:
      if (finite({a, b, c_, d})) rectPath(c, a, b, c_, d);
      break;
    case RoundRect:
      if (finite({a, b, c_, d, e})) roundRectPath(c, a, b, c_, d, e);
      break;
    case QuadTo:
      if (finite({a, b, c_, d})) devQuad(c, a, b, c_, d);
      break;
    case BezierTo:
      if (finite({a, b, c_, d, e, f})) devCubic(c, a, b, c_, d, e, f);
      break;
    case Fill:
      fillDevice(c, c.path.get(), PLUTOVG_FILL_RULE_NON_ZERO);
      break;
    case FillEvenOdd:
      fillDevice(c, c.path.get(), PLUTOVG_FILL_RULE_EVEN_ODD);
      break;
    case Stroke:
      strokeDevice(c, c.path.get());
      break;
    case Clip:
      s.clips.push_back(PathPtr(plutovg_path_clone(c.path.get()), plutovg_path_destroy));
      break;
    case FillRect:
      if (finite({a, b, c_, d}) && c_ != 0 && d != 0) fillDevice(c, rectDevice(s.ctm, a, b, c_, d).get(), PLUTOVG_FILL_RULE_NON_ZERO);
      break;
    case StrokeRect:
      if (finite({a, b, c_, d})) strokeDevice(c, rectDevice(s.ctm, a, b, c_, d).get());
      break;
    case ClearRect:
      if (finite({a, b, c_, d}) && c_ != 0 && d != 0 && begin(c)) {
        plutovg_canvas_set_operator(c.vg, PLUTOVG_OPERATOR_CLEAR);
        plutovg_canvas_set_opacity(c.vg, 1);
        plutovg_canvas_reset_matrix(c.vg);
        plutovg_canvas_set_rgba(c.vg, 0, 0, 0, 1);
        plutovg_canvas_set_fill_rule(c.vg, PLUTOVG_FILL_RULE_NON_ZERO);
        plutovg_canvas_fill_path(c.vg, rectDevice(s.ctm, a, b, c_, d).get());
        end(c);
      }
      break;
    case FillGradient:
      s.fill.gradient = static_cast<int>(a);
      break;
    case StrokeGradient:
      s.stroke.gradient = static_cast<int>(a);
      break;
    default:
      // (ShadowBlur, ShadowOffsetX, ShadowOffsetY: shadows are not drawn here)
      break;
  }
}

void str(int ctx, int op, const char* text, double a, double b, double c_) {
  if (op == AddColorStop) {
    // (here `ctx` is the gradient)
    auto it = gradients().find(ctx);
    plutovg_color_t col;
    if (it != gradients().end() && std::isfinite(a) && a >= 0 && a <= 1 && parseColor(text, col)) {
      std::vector<plutovg_gradient_stop_t>& stops = it->second.stops;
      plutovg_gradient_stop_t st{static_cast<float>(a), col};
      // (in the order of their offsets; one at the same offset goes after those there)
      auto at = std::upper_bound(stops.begin(), stops.end(), st, [](const plutovg_gradient_stop_t& x, const plutovg_gradient_stop_t& y) { return x.offset < y.offset; });
      stops.insert(at, st);
    }
    return;
  }
  Ctx* cp = ctxOf(ctx);
  if (!cp) return;
  Ctx& c = *cp;
  State& s = c.s;
  c.calls++;
  const std::string v = text;
  switch (op) {
    case FillStyle:
    case StrokeStyle: {
      plutovg_color_t col;
      if (parseColor(v, col)) {
        Paint& p = op == FillStyle ? s.fill : s.stroke;
        p.color = col;
        p.gradient = 0;
      }
      break;
    }
    case LineCap:
      if (v == "butt") s.cap = PLUTOVG_LINE_CAP_BUTT;
      else if (v == "round") s.cap = PLUTOVG_LINE_CAP_ROUND;
      else if (v == "square") s.cap = PLUTOVG_LINE_CAP_SQUARE;
      break;
    case LineJoin:
      if (v == "miter") s.join = PLUTOVG_LINE_JOIN_MITER;
      else if (v == "round") s.join = PLUTOVG_LINE_JOIN_ROUND;
      else if (v == "bevel") s.join = PLUTOVG_LINE_JOIN_BEVEL;
      break;
    case Font: {
      canvasFonts::FontSpec f;
      if (canvasFonts::parseFont(v, f)) s.font = f;
      break;
    }
    case TextAlign:
      if (v == "start" || v == "end" || v == "left" || v == "right" || v == "center") s.align = v;
      break;
    case TextBaseline:
      if (v == "top" || v == "hanging" || v == "middle" || v == "alphabetic" || v == "ideographic" || v == "bottom") s.baseline = v;
      break;
    case Direction:
      if (v == "ltr" || v == "rtl") s.direction = v;
      else if (v == "inherit") s.direction = "ltr";
      break;
    case LetterSpacing: {
      char* end = nullptr;
      const double n = std::strtod(v.c_str(), &end);
      const std::string unit = end ? std::string(end) : std::string();
      if (end != v.c_str() && std::isfinite(n) && (unit == "px" || unit == "em" || (unit.empty() && n == 0))) {
        s.spacing = n;
        s.spacingEm = unit == "em";
      } else if (v == "normal") s.spacing = 0;
      break;
    }
    case Composite:
      s.op = operatorOf(v, s.op);
      break;
    case FillText:
    case FillTextMax:
    case StrokeText:
      c.texts.push_back(v);
      if (c.texts.size() > 400) c.texts.erase(c.texts.begin(), c.texts.begin() + 200);
      drawText(c, v, a, b, op == FillTextMax ? c_ : std::numeric_limits<double>::infinity(), op == StrokeText);
      break;
    default:
      // (ShadowColor, Filter: not drawn here)
      break;
  }
}

void lineDash(int ctx, const double* segments, int n) {
  Ctx* c = ctxOf(ctx);
  if (!c) return;
  c->calls++;
  std::vector<double> d(segments, segments + std::max(0, n));
  for (double v : d)
    if (!std::isfinite(v) || v < 0) return;
  // (all zeros: a solid line)
  if (std::all_of(d.begin(), d.end(), [](double v) { return v == 0; })) d.clear();
  c->s.dash = d;
}

void polyline(int ctx, const double* xy, int n, bool close) {
  Ctx* c = ctxOf(ctx);
  if (!c) return;
  c->calls++;
  for (int i = 0; i < n; i++) {
    const double x = xy[i * 2], y = xy[i * 2 + 1];
    if (!finite({x, y})) continue;
    if (i == 0) devMove(*c, x, y);
    else devLine(*c, x, y);
  }
  if (close) devClose(*c);
}

void drawImage(int ctx, bool fromCanvas, int source, const double* v, int n) {
  Ctx* c = ctxOf(ctx);
  if (!c) return;
  c->calls++;
  double sw = 0, sh = 0;
  const plutovg_surface_t* surface = nullptr;
  const Picture* pic = nullptr;
  plutovg_surface_t* copy = nullptr;
  if (fromCanvas) {
    int w = 0, h = 0;
    sizeOf(source, w, h);
    sw = w, sh = h;
    Ctx* from = ctxOfCanvas(source);
    surface = surfaceOf(from);
    // (a canvas never painted on is transparent: nothing to draw)
    if (!surface) return;
    if (from == c) {
      copy = plutovg_surface_create(w, h);
      if (!copy) return;
      std::memcpy(plutovg_surface_get_data(copy), plutovg_surface_get_data(surface), static_cast<size_t>(plutovg_surface_get_stride(surface)) * static_cast<size_t>(h));
      surface = copy;
    }
  } else {
    const auto it = pictures().find(source);
    if (it == pictures().end()) return;
    pic = &it->second;
    sw = pic->width, sh = pic->height;
    surface = pic->raster;
  }
  double r[8];
  if (n == 2) r[0] = 0, r[1] = 0, r[2] = sw, r[3] = sh, r[4] = v[0], r[5] = v[1], r[6] = sw, r[7] = sh;
  else if (n == 4) r[0] = 0, r[1] = 0, r[2] = sw, r[3] = sh, r[4] = v[0], r[5] = v[1], r[6] = v[2], r[7] = v[3];
  else if (n == 8) std::copy(v, v + 8, r);
  else return;
  if (pic && pic->svg) drawSvg(*c, *pic, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
  else if (surface) drawSurface(*c, surface, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
  if (copy) plutovg_surface_destroy(copy);
}

void measure(int ctx, const char* text, double out[5]) {
  Ctx* c = ctxOf(ctx);
  const State s = c ? c->s : State();
  canvasFonts::Layout l;
  if (!canvasFonts::layout(s.font, text, spacingPx(s), l)) {
    // no font on this machine: roughly, from the size
    size_t chars = 0;
    for (const char* p = text; *p; p++)
      if ((static_cast<unsigned char>(*p) & 0xc0) != 0x80) chars++;
    out[0] = 0.55 * s.font.px * static_cast<double>(chars);
    out[1] = 0.74 * s.font.px;
    out[2] = 0.2 * s.font.px;
    out[3] = 0;
    out[4] = out[0];
    return;
  }
  // (the inked box from the point the text is drawn at, as textAlign and textBaseline place it)
  const double dx = alignFactor(s) * l.width;
  const double by = baselineShift(s, l);
  out[0] = l.width;
  out[1] = l.inkUp - by;
  out[2] = l.inkDown + by;
  out[3] = l.inkLeft - dx;
  out[4] = l.inkRight + dx;
}

int gradient(int, bool radial, double a, double b, double c, double d, double e, double f) {
  Gradient g;
  g.radial = radial;
  const double v[6] = {a, b, c, d, e, f};
  std::copy(v, v + 6, g.v);
  const int id = ++lastGradient;
  gradients()[id] = g;
  return id;
}
void gradientFree(int g) { gradients().erase(g); }

void getImageData(int ctx, int x, int y, int w, int h, uint8_t* out) {
  Ctx* c = ctxOf(ctx);
  if (!c || w <= 0 || h <= 0) return;
  std::memset(out, 0, static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
  if (const plutovg_surface_t* s = surfaceOf(c)) readPixels(s, x, y, w, h, out, false);
}

void putImageData(int ctx, const uint8_t* data, int w, int h, int x, int y) {
  Ctx* c = ctxOf(ctx);
  if (!c) return;
  c->calls++;
  if (!ensure(*c)) return;
  const int W = plutovg_surface_get_width(c->surface), H = plutovg_surface_get_height(c->surface), stride = plutovg_surface_get_stride(c->surface);
  unsigned char* d = plutovg_surface_get_data(c->surface);
  for (int r = 0; r < h; r++) {
    const int yy = y + r;
    if (yy < 0 || yy >= H) continue;
    uint32_t* row = reinterpret_cast<uint32_t*>(d + static_cast<size_t>(yy) * static_cast<size_t>(stride));
    for (int k = 0; k < w; k++) {
      const int xx = x + k;
      if (xx < 0 || xx >= W) continue;
      const uint8_t* p = data + (static_cast<size_t>(r) * static_cast<size_t>(w) + static_cast<size_t>(k)) * 4;
      const uint32_t a = p[3];
      const auto pre = [a](uint32_t v) { return (v * a + 127) / 255; };
      row[xx] = (a << 24) | (pre(p[0]) << 16) | (pre(p[1]) << 8) | pre(p[2]);
    }
  }
  c->version++;
}

std::string toDataURL(int canvasElement, const char* type, double quality) {
  int w = 0, h = 0;
  sizeOf(canvasElement, w, h);
  if (w <= 0 || h <= 0) return "data:,";
  plutovg_surface_t* blank = nullptr;
  const plutovg_surface_t* s = surfaceOf(ctxOfCanvas(canvasElement));
  if (!s) s = blank = plutovg_surface_create(w, h);
  if (!s) return "data:,";
  std::vector<unsigned char> bytes;
  const bool jpeg = std::string(type) == "image/jpeg";
  const bool ok = jpeg ? plutovg_surface_write_to_jpg_stream(s, appendBytes, &bytes, static_cast<int>(std::lround(std::clamp(quality, 0.0, 1.0) * 100)))
                       : plutovg_surface_write_to_png_stream(s, appendBytes, &bytes);
  if (blank) plutovg_surface_destroy(blank);
  if (!ok) return "data:,";
  return std::string(jpeg ? "data:image/jpeg;base64," : "data:image/png;base64,") + base64(bytes);
}

}  // namespace canvasBackend

Task<Image> loadImage(std::string url) {
  if (url.compare(0, 5, "data:") == 0) {
    const size_t comma = url.find(',');
    if (comma == std::string::npos) return resolved(Image());
    const std::string head = url.substr(5, comma - 5);
    const std::string body = url.substr(comma + 1);
    const bool b64 = head.size() >= 7 && head.compare(head.size() - 7, 7, ";base64") == 0;
    // (the answer at once: the picture is in the address)
    return resolved(decodeBytes(b64 ? unbase64(body) : percentDecode(body)));
  }
  return fetchPicture(std::move(url));
}
Task<Image> decodeImage(Bytes bytes, std::string) { return resolved(decodeBytes(bytes)); }
void freeImage(Image& image) {
  const auto it = pictures().find(image.handle);
  if (it != pictures().end()) {
    dropPicture(it->second);
    pictures().erase(it);
  }
  image = Image();
}

namespace canvasNative {

long calls(const Ctx2D& g) {
  Ctx* c = ctxOf(g.handle());
  return c ? c->calls : 0;
}
std::vector<std::string> texts(const Ctx2D& g) {
  Ctx* c = ctxOf(g.handle());
  return c ? c->texts : std::vector<std::string>();
}
void forget(const Ctx2D& g) {
  if (Ctx* c = ctxOf(g.handle())) {
    c->calls = 0;
    c->texts.clear();
  }
}

uint64_t version(const Ctx2D& g) {
  Ctx* c = ctxOf(g.handle());
  return c ? c->version : 0;
}

bool pixels(int canvasElement, std::vector<uint8_t>& rgba, int& width, int& height, bool premultiplied, bool flipY) {
  sizeOf(canvasElement, width, height);
  if (width <= 0 || height <= 0) return false;
  rgba.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);
  if (const plutovg_surface_t* s = surfaceOf(ctxOfCanvas(canvasElement))) readPixels(s, 0, 0, width, height, rgba.data(), premultiplied);
  if (flipY) flipRows(rgba, width, height);
  return true;
}

bool imagePixels(int image, std::vector<uint8_t>& rgba, int& width, int& height, bool premultiplied, bool flipY) {
  const auto it = pictures().find(image);
  if (it == pictures().end()) return false;
  const Picture& p = it->second;
  plutovg_surface_t* s = p.raster;
  plutovg_surface_t* own = nullptr;
  if (!s && p.svg) s = own = plutosvg_document_render_to_surface(p.svg, nullptr, p.width, p.height, nullptr, nullptr, nullptr);
  if (!s) return false;
  width = plutovg_surface_get_width(s);
  height = plutovg_surface_get_height(s);
  rgba.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);
  readPixels(s, 0, 0, width, height, rgba.data(), premultiplied);
  if (own) plutovg_surface_destroy(own);
  if (flipY) flipRows(rgba, width, height);
  return true;
}

bool savePng(const Canvas& canvas, const std::string& path) {
  if (!canvas) return false;
  const int h = canvas.el().handle();
  int w = 0, hh = 0;
  sizeOf(h, w, hh);
  if (w <= 0 || hh <= 0) return false;
  plutovg_surface_t* blank = nullptr;
  const plutovg_surface_t* s = surfaceOf(ctxOfCanvas(h));
  if (!s) s = blank = plutovg_surface_create(w, hh);
  const bool ok = s && plutovg_surface_write_to_png(s, path.c_str());
  if (blank) plutovg_surface_destroy(blank);
  return ok;
}

void resized(int canvasElement) {
  if (Ctx* c = ctxOfCanvas(canvasElement)) c->reset();
}

void released(int canvasElement) {
  if (Ctx* c = ctxOfCanvas(canvasElement)) c->dropSurface();
}

}  // namespace canvasNative

}  // namespace fs

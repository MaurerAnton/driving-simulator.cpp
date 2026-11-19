// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Traffic signs: the atlas, the plate geometry and the procedural fallbacks.
 *
 * Origin: src/render/signs.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).  The atlas cells, the slot allocator and every fallback drawing
 * are carried over exactly; the SVG rasterisation goes through SignSource.
 */
#include "render/signs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fs {

namespace {
constexpr int CELL = 256;
constexpr int COLS = 8;
constexpr int ROWS = 8;
constexpr int PAD = 6;
constexpr const char* FONT = "DIN 1451 Std, DIN Alternate, Bahnschrift, Arial Narrow, sans-serif";

uint32_t hex(const char* s) { return parseColor(s); }
}  // namespace

std::pair<double, double> signSize(const std::string& code, double aspect, bool big) {
  double k = big ? 1.45 : 1;
  std::string base = code.substr(0, code.find(':'));
  double n = std::atof(base.c_str());
  double w;
  if (base == "310" || base == "311") return {1.2 * k, 0.8 * k};
  if (n >= 1000) w = 0.6;
  else if (n < 200 || base == "205" || base == "301") w = 0.9;
  else if (base == "206") w = 0.9;
  else if (base == "306" || base == "307") w = 0.84;
  else if (base.rfind("325", 0) == 0 || base.rfind("330", 0) == 0 || base.rfind("331", 0) == 0)
    w = 0.9;
  else if (base.rfind("201", 0) == 0) w = 1.0;
  else w = 0.6;
  return {w * k, (w * k) / (aspect > 0 ? aspect : 1)};
}

SignRenderer::SignRenderer(const std::string& baseUrl, SignSource& source)
    : baseUrl_(baseUrl), source_(source), canvas_(CELL * COLS, CELL * ROWS) {
  // slot 0 is the plain grey back side
  canvas_.fillRect(0, 0, CELL, CELL, hex("#9a9fa3"));
}

void SignRenderer::init() { source_.load(baseUrl_); }

void SignRenderer::flush() {
  if (dirty_) dirty_ = false;  // the atlas lives on the canvas; nothing to upload
}

double SignRenderer::aspectOf(const std::string& code) const {
  std::string base = code.substr(0, code.find(':'));
  if (base == "310" || base == "311") return 1.5;
  auto sz = source_.size(base);
  if (sz.first > 0 && sz.second > 0) return static_cast<double>(sz.first) / sz.second;
  double n = std::atof(base.c_str());
  if (n >= 1000) return 1.82;
  if (n < 200 || base == "205" || base == "301") return 1.14;
  if (base.rfind("325", 0) == 0 || base.rfind("330", 0) == 0) return 1.5;
  return 1;
}

SignRenderer::Slot& SignRenderer::slot(const std::string& code) {
  auto it = slots_.find(code);
  if (it != slots_.end()) return it->second;
  double aspect = aspectOf(code);
  bool wide = aspect > 1.3;
  // wide plates take two cells so that text stays legible
  if (wide && next_ % COLS == COLS - 1) next_++;
  int index = next_;
  next_ += wide ? 2 : 1;
  if (next_ > COLS * ROWS) {
    // atlas full: reuse the back side rather than crash
    Slot s;
    s.u0 = 0.01;
    s.v0 = 0.01;
    s.u1 = 0.02;
    s.v1 = 0.02;
    s.aspect = aspect;
    slots_[code] = s;
    return slots_[code];
  }
  int col = index % COLS;
  int row = index / COLS;
  double cw = CELL * (wide ? 2 : 1) - PAD * 2;
  double ch = CELL - PAD * 2;
  // fit the plate into the cell keeping its aspect
  double w = cw;
  double h = cw / aspect;
  if (h > ch) {
    h = ch;
    w = ch * aspect;
  }
  double x = col * CELL + PAD + (cw - w) / 2;
  double y = row * CELL + PAD + (ch - h) / 2;
  double W = canvas_.width();
  double H = canvas_.height();
  Slot s;
  s.u0 = x / W;
  s.v0 = 1 - (y + h) / H;
  s.u1 = (x + w) / W;
  s.v1 = 1 - y / H;
  s.aspect = aspect;
  slots_[code] = s;
  draw(code, x, y, w, h);
  return slots_[code];
}

void SignRenderer::draw(const std::string& code, double x, double y, double w, double h) {
  std::string base = code.substr(0, code.find(':'));
  std::string svg = source_.svg(base);
  if (!svg.empty() && base != "310" && base != "311") {
    // the platform decodes the SVG; until then the fallback is drawn first
    fallback(code, x, y, w, h);
    source_.rasterise(svg, canvas_, x, y, w, h);
  } else {
    if (base != "310" && base != "311") missing.insert(base);
    fallback(code, x, y, w, h);
  }
  dirty_ = true;
}

void SignRenderer::fallback(const std::string& code, double x, double y, double w, double h) {
  Canvas2D& c = canvas_;
  size_t colon = code.find(':');
  std::string base = code.substr(0, colon);
  std::string arg = colon == std::string::npos ? "" : code.substr(colon + 1);
  double cx = x + w / 2;
  double cy = y + h / 2;
  double r = std::min(w, h) / 2;

  auto ring = [&](const char* fill, const char* rim, double rimW) {
    c.fillCircle(cx, cy, r - 1, hex(rim));
    c.fillCircle(cx, cy, r * (1 - rimW), hex(fill));
  };
  auto poly = [&](const std::vector<double>& pts, const char* fill) {
    std::vector<std::pair<double, double>> q;
    for (size_t i = 0; i < pts.size(); i += 2)
      q.push_back({x + pts[i] * w, y + pts[i + 1] * h});
    c.fillPoly(q, hex(fill));
  };
  auto label = [&](const std::string& t, double size, const char* color = "#111", double dy = 0) {
    c.fillText(t, cx, cy + dy, size, hex(color));
  };

  if (base == "310" || base == "311") {
    std::string l1, l2;
    size_t bar = arg.find('|');
    if (bar == std::string::npos) {
      l1 = arg;
    } else {
      l1 = arg.substr(0, bar);
      l2 = arg.substr(bar + 1);
    }
    c.fillRect(x, y, w, h, hex("#111"));
    c.fillRect(x + 5, y + 5, w - 10, h - 10, hex("#f6d417"));
    if (base == "310") {
      label(l1, h * 0.3, "#111", l2.empty() ? 0 : -h * 0.12);
      if (!l2.empty()) label(l2, h * 0.15, "#111", h * 0.22);
    } else {
      c.fillRect(x + 5, cy - 2, w - 10, 4, hex("#111"));
      label(l1, h * 0.26, "#111", h * 0.22);
      c.line(x + w * 0.06, y + h * 0.94, x + w * 0.94, y + h * 0.56, hex("#d0141e"), h * 0.07);
    }
  } else if (base.rfind("274.", 0) == 0 || base.rfind("290", 0) == 0) {
    c.fillRect(x, y, w, h, hex("#111"));
    c.fillRect(x + 4, y + 4, w - 8, h - 8, hex("#fff"));
    std::string speed = base.rfind("-20", base.size() - 3) == base.size() - 3 ? "20" : "30";
    bool grey = base.rfind("274.2", 0) == 0;
    c.fillCircle(cx, cy - h * 0.1, r * 0.6, hex(grey ? "#888" : "#d0141e"));
    c.fillCircle(cx, cy - h * 0.1, r * 0.46, hex("#fff"));
    label(speed, r * 0.5, grey ? "#888" : "#111", -h * 0.1);
    label("ZONE", r * 0.3, grey ? "#888" : "#111", h * 0.33);
  } else if (base.rfind("274-", 0) == 0) {
    ring("#fff", "#d0141e", 0.2);
    label(base.substr(4), r * (base.size() > 6 ? 0.78 : 0.95));
  } else if (base.rfind("278-", 0) == 0 || base == "282") {
    ring("#fff", "#444", 0.06);
    if (base != "282") label(base.substr(4), r * 0.8, "#666");
    for (int i = -2; i <= 2; i++)
      c.line(cx - r * 0.62 + i * r * 0.09, cy + r * 0.62 + i * r * 0.09,
             cx + r * 0.62 + i * r * 0.09, cy - r * 0.62 + i * r * 0.09, hex("#333"), r * 0.05);
  } else if (base == "205") {
    poly({0, 0, 1, 0, 0.5, 1}, "#d0141e");
    poly({0.17, 0.1, 0.83, 0.1, 0.5, 0.8}, "#fff");
  } else if (base == "206") {
    double o = 0.293;
    poly({o, 0, 1 - o, 0, 1, o, 1, 1 - o, 1 - o, 1, o, 1, 0, 1 - o, 0, o}, "#fff");
    double i = 0.03;
    poly({o + i * 0.4, i, 1 - o - i * 0.4, i, 1 - i, o + i * 0.4, 1 - i, 1 - o - i * 0.4,
          1 - o - i * 0.4, 1 - i, o + i * 0.4, 1 - i, i, 1 - o - i * 0.4, i, o + i * 0.4},
         "#d0141e");
    label("STOP", r * 0.62, "#fff");
  } else if (base == "306" || base == "307") {
    poly({0.5, 0, 1, 0.5, 0.5, 1, 0, 0.5}, "#fff");
    poly({0.5, 0.12, 0.88, 0.5, 0.5, 0.88, 0.12, 0.5}, "#f6c817");
  } else if (base == "301" || std::atof(base.c_str()) < 200) {
    poly({0.5, 0, 1, 1, 0, 1}, "#d0141e");
    poly({0.5, 0.2, 0.83, 0.9, 0.17, 0.9}, "#fff");
    label(base == "301" ? "^" : "!", r * 0.7, "#111", h * 0.18);
  } else if (base == "267") {
    ring("#d0141e", "#fff", 0.05);
    c.fillRect(cx - r * 0.66, cy - r * 0.13, r * 1.32, r * 0.26, hex("#fff"));
  } else if (base == "250" || base == "260" || base == "253" || base == "251") {
    ring("#fff", "#d0141e", 0.18);
  } else if (base == "215" || base.rfind("209", 0) == 0 || base.rfind("211", 0) == 0 ||
             base.rfind("214", 0) == 0 || base.rfind("222", 0) == 0 || base == "237" ||
             base == "240") {
    ring("#1b5fa8", "#fff", 0.04);
    label(base == "215" ? "o" : "^", r * 1.1, "#fff");
  } else if (base == "224") {
    ring("#f6d417", "#1d7a34", 0.14);
    label("H", r * 1.0, "#1d7a34");
  } else if (std::atof(base.c_str()) >= 1000) {
    c.fillRect(x, y, w, h, hex("#111"));
    c.fillRect(x + 4, y + 4, w - 8, h - 8, hex("#fff"));
    static const std::pair<const char*, const char*> texts[] = {
        {"1020-30", "Anlieger frei"},
        {"1026-38", "Land u forstw Verkehr frei"},
        {"1026-36", "Landw Verkehr frei"},
        {"1022-10", "Rad frei"},
        {"1012-31", "Ende"},
        {"1012-30", "Anfang"},
    };
    std::string text = base;
    for (const auto& t : texts)
      if (base == t.first) text = t.second;
    label(text, h * 0.3);
  } else {
    c.fillRect(x, y, w, h, hex("#fff"));
    c.fillRect(x + 4, y + 4, w - 8, h - 8, hex("#1b5fa8"));
    label(base, h * 0.3, "#fff");
  }
  (void)FONT;
}

void SignRenderer::add(LoadedTile& tile) {
  const auto& posts = tile.header.signs;
  if (posts.empty()) return;
  std::vector<double> pos, uv;
  std::vector<double> idx, idxBack;
  auto group = std::make_unique<Group>();

  for (const auto& p : posts) {
    double s = std::sin(p.yaw);
    double c = std::cos(p.yaw);
    // facing vector f = (-s, 0, -c); the viewer's right is (-c, 0, s)
    double rx = -c;
    double rz = s;
    double y = p.y + p.h;
    for (int k = static_cast<int>(p.s.size()) - 1; k >= 0; k--) {
      const std::string& code = p.s[static_cast<size_t>(k)];
      Slot& sl = slot(code);
      auto sz = signSize(code, sl.aspect, p.big == 1);
      double w = sz.first, hh = sz.second;
      double y0 = y;
      double y1 = y + hh;
      double hw = w / 2;
      double fx = -s * 0.035;
      double fz = -c * 0.035;
      size_t base = pos.size() / 3;
      // front
      pos.insert(pos.end(), {p.x - rx * hw + fx, y0, p.z - rz * hw + fz,
                             p.x + rx * hw + fx, y0, p.z + rz * hw + fz,
                             p.x + rx * hw + fx, y1, p.z + rz * hw + fz,
                             p.x - rx * hw + fx, y1, p.z - rz * hw + fz});
      uv.insert(uv.end(), {sl.u0, sl.v0, sl.u1, sl.v0, sl.u1, sl.v1, sl.u0, sl.v1});
      for (double v : {static_cast<double>(base), static_cast<double>(base + 1),
                       static_cast<double>(base + 2), static_cast<double>(base),
                       static_cast<double>(base + 2), static_cast<double>(base + 3)})
        idx.push_back(v);
      // back: same outline, drawn with the bare-metal material
      pos.insert(pos.end(), {p.x - rx * hw - fx * 0.2, y0, p.z - rz * hw - fz * 0.2,
                             p.x + rx * hw - fx * 0.2, y0, p.z + rz * hw - fz * 0.2,
                             p.x + rx * hw - fx * 0.2, y1, p.z + rz * hw - fz * 0.2,
                             p.x - rx * hw - fx * 0.2, y1, p.z - rz * hw - fz * 0.2});
      uv.insert(uv.end(), {sl.u0, sl.v0, sl.u1, sl.v0, sl.u1, sl.v1, sl.u0, sl.v1});
      for (double v : {static_cast<double>(base + 4), static_cast<double>(base + 6),
                       static_cast<double>(base + 5), static_cast<double>(base + 4),
                       static_cast<double>(base + 7), static_cast<double>(base + 6)})
        idxBack.push_back(v);
      y = y1 + 0.03;
    }
    Matrix4 m4;
    m4.compose(Vector3(p.x, p.y - 0.3, p.z), Quaternion(), Vector3(1, y - p.y + 0.3, 1));
    (void)m4;
  }

  // tile-relative positions keep float precision
  double cx = tile.header.cx;
  double cz = tile.header.cz;
  std::vector<double> rel(pos.size());
  for (size_t i = 0; i < pos.size(); i += 3) {
    rel[i] = pos[i] - cx;
    rel[i + 1] = pos[i + 1] - tile.header.yBase;
    rel[i + 2] = pos[i + 2] - cz;
  }
  auto g = std::make_shared<BufferGeometry>();
  g->position = std::make_shared<BufferAttribute>(rel, 3);
  g->uv = std::make_shared<BufferAttribute>(uv, 2);
  std::vector<double> all = idx;
  all.insert(all.end(), idxBack.begin(), idxBack.end());
  g->index = std::make_shared<BufferAttribute>(all, 1);
  g->computeVertexNormals();

  auto plates = std::make_unique<Mesh>(g, MaterialKind::Struct);
  plates->kind2 = MaterialKind::Struct;  // the back uses the bare-metal material
  plates->position.set(cx, tile.header.yBase, cz);
  plates->updateMatrix();
  plates->matrixAutoUpdate = false;
  plates->name = "signs";
  group->add(std::unique_ptr<Object3D>(plates.release()));
  tile.group->add(std::unique_ptr<Object3D>(group.release()));
  perTile_[tile.key] = std::move(group);
}

void SignRenderer::remove(const std::string& key) { perTile_.erase(key); }

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/signs.ts @ v1.7.7
#include "render/signs.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

#include "core/js.h"
#include "engine/geometries.h"
#include "platform/json.h"
#include "platform/plat.h"
#include "render/glsl_gen.h"
#include "render/signart.h"
#include "render/tiles.h"

namespace fs {

namespace {

/**
 * Graphics of other numbers that show the same sign: the 1992 numbers of the
 * bundled files for their 2017 numbers, and those that hold for the variants
 * with an arrow beneath.
 */
const std::unordered_map<std::string, std::string> ALIAS = {
    {"1010-51", "1048-12"},
    {"1053-35", "1052-36"},
    {"286-10", "286"},
    {"286-20", "286"},
    {"286-30", "286"},
    {"315-50", "315"},
    {"315-55", "315"},
    {"350-10", "350"},
    {"350-20", "350"},
};

/** Graphics that show a sign the other way round (Aufstellung links: the figure looks the other way). */
const std::unordered_map<std::string, std::string> MIRROR = {
    {"133-20", "133-10"},
    {"136-20", "136-10"},
    {"138-20", "138-10"},
    {"142-20", "142-10"},
    {"156-20", "156-10"},
    {"159-20", "159-10"},
    {"162-20", "162-10"},
    {"241-31", "241-30"},
};

const char* const FONT =
    "\"DIN 1451 Std\", \"DIN Alternate\", \"Bahnschrift\", \"Roboto Condensed\", \"Arial Narrow\", \"Liberation Sans Narrow\", \"Helvetica Neue\", Arial, sans-serif";

// ---- the regular expressions of the original, by hand

/** /^\d+$/ */
bool isDigits(std::string_view s) {
  if (s.empty()) return false;
  for (const char c : s)
    if (c < '0' || c > '9') return false;
  return true;
}

/** \s (of the characters a file of ours has: the plain ones) */
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

/** /^(\d+(?:\.\d+)?)-\d+$/.exec(base)?.[1] – the number a variant belongs to ("101-12" → "101"; '': none) */
std::string parentOf(const std::string& base) {
  const size_t dash = base.find('-');
  if (dash == std::string::npos || !isDigits(std::string_view(base).substr(dash + 1))) return "";
  const std::string_view head = std::string_view(base).substr(0, dash);
  const size_t dot = head.find('.');
  if (dot == std::string_view::npos ? !isDigits(head) : !isDigits(head.substr(0, dot)) || !isDigits(head.substr(dot + 1))) return "";
  return std::string(head);
}

/** /<svg[^>]*\swidth=/.test(text) */
bool svgHasWidth(const std::string& text) {
  for (size_t at = text.find("<svg"); at != std::string::npos; at = text.find("<svg", at + 1)) {
    for (size_t i = at + 4; i < text.size() && text[i] != '>'; i++)
      if (isSpace(text[i]) && text.compare(i + 1, 6, "width=") == 0) return true;
  }
  return false;
}

/** /viewBox="([\d.\s,-]+)"/.exec(text)?.[1] (false: there is none) */
bool svgViewBox(const std::string& text, std::string& out) {
  static const std::string open = "viewBox=\"";
  for (size_t at = text.find(open); at != std::string::npos; at = text.find(open, at + 1)) {
    const size_t from = at + open.size();
    size_t to = from;
    while (to < text.size() && ((text[to] >= '0' && text[to] <= '9') || text[to] == '.' || isSpace(text[to]) || text[to] == ',' || text[to] == '-')) to++;
    if (to > from && to < text.size() && text[to] == '"') {
      out = text.substr(from, to - from);
      return true;
    }
  }
  return false;
}

/** text.split(/[\s,]+/) */
std::vector<std::string> splitSpacesCommas(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  size_t i = 0;
  while (i < text.size()) {
    if (isSpace(text[i]) || text[i] == ',') {
      while (i < text.size() && (isSpace(text[i]) || text[i] == ',')) i++;
      out.push_back(cur);
      cur.clear();
    } else cur += text[i++];
  }
  out.push_back(cur);
  return out;
}

}  // namespace

std::pair<double, double> signSize(const std::string& code, double aspect, bool big) {
  const double k = big ? 1.45 : 1;
  const std::string base = split(code, ":")[0];
  const double n = jsNumber(base, false);
  // (`aspect || 1`)
  const double a = aspect == 0 || aspect != aspect ? 1 : aspect;
  double w;
  if (base == "310" || base == "311") return {1.2 * k, 0.8 * k};
  // (the diversion that is announced at a closure: a wide yellow plate)
  if (base == "457.1") return {0.9 * k, 0.56 * k};
  // (a beacon stands tall and narrow, a guide plate or a barrier lies wide; the detour signs are a little larger)
  if (startsWith(base, "605-")) return {0.25 * k, 1.0 * k};
  // (the beacons before a level crossing: a narrow plate a metre tall – the one with three stripes carries the warning sign above them)
  // /^(159|162)-[12]0$/
  if (base == "159-10" || base == "159-20" || base == "162-10" || base == "162-20") return {0.3 * k, 1.0 * k};
  // /^156-[12]0$/
  if (base == "156-10" || base == "156-20") return {0.6 * k, (0.6 * k) / a};
  if (startsWith(base, "626-") || base == "600") w = 1.0;
  else if (startsWith(base, "454-") || startsWith(base, "455.") || base == "457.2") w = 0.9;
  else if (n >= 1000) w = 0.6;
  else if (n < 200 || base == "205" || base == "301") w = 0.9;
  else if (base == "206") w = 0.9;
  else if (base == "306" || base == "307") w = 0.84;
  else if (startsWith(base, "325") || startsWith(base, "330") || startsWith(base, "331")) w = 0.9;
  else if (startsWith(base, "201")) w = 1.0;
  else w = 0.6;
  return {w * k, (w * k) / a};
}

SignRenderer::SignRenderer(const std::string& baseUrl, double maxAnisotropy)
    : baseUrl_(baseUrl), canvas_(CELL * COLS, CELL * ROWS), postMat_(lambert({.color = Color(0x8b9094)})) {
  postGeo_ = cylinderGeometry(0.03, 0.03, 1, 6, 1, true)->translate(0, 0.5, 0).shared_from_this();
  ctx_ = canvas_.getContext2D();
  ctx_.fillStyle("#9a9fa3");
  ctx_.fillRect(0, 0, CELL, CELL);
  taken_[0] = 1;
  texture = canvasTexture(canvas_);
  texture->srgb = true;
  texture->anisotropy = static_cast<int>(jsMin(8, maxAnisotropy));
  texture->generateMipmaps = true;
  texture->minFilter = Filter::LinearMipmapLinear;
  material = basic({.map = texture, .transparent = false, .alphaTest = 0.45});
  // back sides keep the plate's silhouette (alpha) but show bare metal
  backMaterial = basic({.map = texture, .alphaTest = 0.45});
  backMaterial->patch = gamePatch("signBack");
}

SignRenderer::~SignRenderer() { *alive_ = false; }

Task<void> SignRenderer::init() {
  const std::shared_ptr<bool> alive = alive_;
  // (both asked for at once, as Promise.all does)
  Task<plat::Response> first = plat::fetch(baseUrl_ + "signs.json");
  Task<plat::Response> second = plat::fetch(baseUrl_ + "manifest.json");
  const plat::Response a = co_await std::move(first);
  const plat::Response b = co_await std::move(second);
  if (!*alive) co_return;
  try {
    // (no answer at all: the original's fetch rejects)
    if (a.status == 0 || b.status == 0) throw std::runtime_error("no answer");
    if (a.ok()) {
      Json doc;
      if (!Json::parse(a.text(), doc)) throw std::runtime_error("signs.json is not JSON");
      svgs_.clear();
      if (doc.isObject())
        for (auto& member : doc.object)
          if (member.second.isString()) svgs_[member.first] = std::move(member.second.str);
    }
    if (b.ok()) {
      Json doc;
      if (!Json::parse(b.text(), doc)) throw std::runtime_error("manifest.json is not JSON");
      manifest_.clear();
      if (doc.isObject()) {
        for (const auto& member : doc.object) {
          ManifestEntry m;
          m.width = member.second.at("width").asNum();
          m.height = member.second.at("height").asNum();
          m.shape = member.second.at("shape").asStr();
          manifest_[member.first] = std::move(m);
        }
      }
    }
  } catch (const std::exception& err) {
    plat::warn(std::string("sign graphics unavailable, using procedural fallbacks ") + err.what());
  }
}

void SignRenderer::flush() {
  if (dirty_) {
    texture->needsUpdate();
    dirty_ = false;
  }
}

const std::string* SignRenderer::svgOf(const std::string& name) const {
  const auto it = svgs_.find(name);
  return it != svgs_.end() && !it->second.empty() ? &it->second : nullptr;
}

void SignRenderer::addMissing(const std::string& base) {
  if (std::find(missing.begin(), missing.end(), base) == missing.end()) missing.push_back(base);
}

SignRenderer::Plan SignRenderer::plan(const std::string& code) const {
  const auto [base, arg] = splitCode(code);
  if (base == "310" || base == "311") return {.how = Plan::Town};
  if (arg.empty()) {
    if (svgOf(base)) return {.how = Plan::Svg, .svg = base};
    const auto alias = ALIAS.find(base);
    if (alias != ALIAS.end() && svgOf(alias->second)) return {.how = Plan::Svg, .svg = alias->second};
    const auto mirror = MIRROR.find(base);
    if (mirror != MIRROR.end() && svgOf(mirror->second)) return {.how = Plan::Svg, .svg = mirror->second, .mirror = true};
  }
  // (the diversion of a closure is drawn by the fallback: sim/roadevents)
  if (base != "457.1" && artAspect(code)) return {.how = Plan::Art};
  const std::string parent = parentOf(base);
  if (!arg.empty() && svgOf(base)) return {.how = Plan::Svg, .svg = base};
  if (!parent.empty() && svgOf(parent) && !startsWith(parent, "274") && !startsWith(parent, "278")) return {.how = Plan::Svg, .svg = parent};
  return {.how = Plan::Fallback};
}

double SignRenderer::aspectOf(const std::string& code) const {
  const std::string base = splitCode(code).first;
  if (base == "310" || base == "311") return 1.5;
  if (base == "457.1") return 1.6;
  const Plan plan = this->plan(code);
  if (!plan.svg.empty()) {
    const auto m = manifest_.find(plan.svg);
    if (m != manifest_.end() && m->second.width > 0 && m->second.height > 0) return m->second.width / m->second.height;
  }
  if (plan.how == Plan::Art) return artAspect(code).value_or(1);
  const double n = jsNumber(base, false);
  if (n >= 1000) return 1.82;
  if (n < 200 || base == "205" || base == "301") return 1.14;
  if (startsWith(base, "325") || startsWith(base, "330")) return 1.5;
  return 1;
}

int SignRenderer::freeCell(bool wide) {
  const auto find = [&]() -> int {
    for (int i = 1; i < COLS * ROWS; i++) {
      if (taken_[static_cast<size_t>(i)]) continue;
      if (!wide) return i;
      if (i % COLS != COLS - 1 && !taken_[static_cast<size_t>(i) + 1]) return i;
    }
    return -1;
  };
  int cell = find();
  while (cell < 0) {
    // (of several that went with the same group the one the original's Map comes to first: the one made first)
    auto oldest = slots_.end();
    for (auto e = slots_.begin(); e != slots_.end(); ++e) {
      const Slot& s = *e->second;
      if (s.users != 0 || s.cell < 0) continue;
      if (oldest == slots_.end() || s.last < oldest->second->last || (s.last == oldest->second->last && s.seq < oldest->second->seq)) oldest = e;
    }
    if (oldest == slots_.end()) return -1;
    const std::shared_ptr<Slot> old = oldest->second;
    slots_.erase(oldest);
    taken_[static_cast<size_t>(old->cell)] = 0;
    if (old->wide) taken_[static_cast<size_t>(old->cell) + 1] = 0;
    cell = find();
  }
  return cell;
}

SignRenderer::Slot& SignRenderer::slot(const std::string& code) {
  const auto have = slots_.find(code);
  // (one that found the atlas full is tried again once nothing shows it)
  if (have != slots_.end() && (have->second->cell >= 0 || have->second->users > 0)) return *have->second;
  if (have != slots_.end()) slots_.erase(have);
  const double aspect = aspectOf(code);
  // wide plates take two cells so that text stays legible
  const bool wide = aspect > 1.3;
  const int index = freeCell(wide);
  auto s = std::make_shared<Slot>();
  s->aspect = aspect;
  s->seq = slotSeq_++;
  if (index < 0) {
    // atlas full: reuse the back side rather than crash
    plat::warn("sign atlas full, dropping " + code);
    s->u0 = 0.01;
    s->v0 = 0.01;
    s->u1 = 0.02;
    s->v1 = 0.02;
    s->cell = -1;
    s->wide = false;
    slots_[code] = s;
    return *s;
  }
  taken_[static_cast<size_t>(index)] = 1;
  if (wide) taken_[static_cast<size_t>(index) + 1] = 1;
  const int col = index % COLS;
  const int row = index / COLS;
  const double cw = CELL * (wide ? 2 : 1) - PAD * 2;
  const double ch = CELL - PAD * 2;
  // fit the plate into the cell keeping its aspect
  double w = cw;
  double h = cw / aspect;
  if (h > ch) {
    h = ch;
    w = ch * aspect;
  }
  const double x = col * CELL + PAD + (cw - w) / 2;
  const double y = row * CELL + PAD + (ch - h) / 2;
  const double W = canvas_.width();
  const double H = canvas_.height();
  s->u0 = x / W;
  s->v0 = 1 - (y + h) / H;
  s->u1 = (x + w) / W;
  s->v1 = 1 - y / H;
  s->cell = index;
  s->wide = wide;
  slots_[code] = s;
  // (whatever was drawn in the cells before goes)
  ctx_.clearRect(col * CELL, row * CELL, CELL * (wide ? 2 : 1), CELL);
  draw(code, x, y, w, h);
  return *s;
}

void SignRenderer::draw(const std::string& code, double x, double y, double w, double h) {
  const std::string base = splitCode(code).first;
  const Plan plan = this->plan(code);
  const std::string* svg = plan.svg.empty() ? nullptr : svgOf(plan.svg);
  if (plan.how == Plan::Svg && svg) {
    std::string text = *svg;
    // browsers need an intrinsic size to rasterise an SVG image
    if (!svgHasWidth(text)) {
      std::string vb;
      if (svgViewBox(text, vb)) {
        const std::vector<std::string> p = splitSpacesCommas(trim(vb));
        const auto number = [&p](size_t i) { return i < p.size() ? jsStr(jsNumber(p[i])) : std::string("undefined"); };
        const size_t at = text.find("<svg");
        if (at != std::string::npos) text.replace(at, 4, "<svg width=\"" + number(2) + "\" height=\"" + number(3) + "\"");
      }
    }
    const auto mine = slots_.find(code);
    // draw something right away; the SVG replaces it once decoded
    if (!drawSignArt(ctx_, code, x, y, w, h)) fallback(code, x, y, w, h);
    // (the picture is asked for after that: where the host has none it says so at once)
    rasterise(code, base, std::move(text), plan.mirror, x, y, w, h, mine != slots_.end() ? mine->second : nullptr).detach();
  } else if (plan.how == Plan::Art && drawSignArt(ctx_, code, x, y, w, h)) {
    /* drawn by signart */
  } else {
    if (plan.how != Plan::Town && base != "457.1") addMissing(base);
    fallback(code, x, y, w, h);
  }
  dirty_ = true;
}

Task<void> SignRenderer::rasterise(std::string code, std::string base, std::string text, bool mirror, double x, double y, double w, double h, std::shared_ptr<Slot> mine) {
  const std::shared_ptr<bool> alive = alive_;
  Image img = co_await loadImage("data:image/svg+xml;charset=utf-8," + encodeURIComponent(text));
  if (!*alive) {
    freeImage(img);
    co_return;
  }
  // (the cell may have been given to another sign meanwhile)
  const auto now = slots_.find(code);
  if ((now != slots_.end() ? now->second : nullptr) != mine) {
    freeImage(img);
    co_return;
  }
  if (img) {
    ctx_.clearRect(x - 1, y - 1, w + 2, h + 2);
    if (mirror) {
      ctx_.save();
      ctx_.translate(2 * x + w, 0);
      ctx_.scale(-1, 1);
      ctx_.drawImage(img, x, y, w, h);
      ctx_.restore();
    } else ctx_.drawImage(img, x, y, w, h);
    dirty_ = true;
    // (the atlas has it now: the host may forget the picture)
    freeImage(img);
  } else {
    addMissing(base);
    ctx_.clearRect(x - 1, y - 1, w + 2, h + 2);
    if (!drawSignArt(ctx_, code, x, y, w, h)) fallback(code, x, y, w, h);
    dirty_ = true;
  }
}

void SignRenderer::fallback(const std::string& code, double x, double y, double w, double h) {
  const Ctx2D& c = ctx_;
  const auto [base, arg] = splitCode(code);
  const double cx = x + w / 2;
  const double cy = y + h / 2;
  const double r = jsMin(w, h) / 2;
  c.save();
  c.lineJoin("round");
  c.textAlign("center");
  c.textBaseline("middle");
  const auto ring = [&](const char* fill, const char* rim, double rimW) {
    c.beginPath();
    c.arc(cx, cy, r - 1, 0, PI * 2);
    c.fillStyle(rim);
    c.fill();
    c.beginPath();
    c.arc(cx, cy, r * (1 - rimW), 0, PI * 2);
    c.fillStyle(fill);
    c.fill();
  };
  const auto poly = [&](std::initializer_list<double> list, const char* fill) {
    const double* pts = list.begin();
    c.beginPath();
    for (size_t i = 0; i < list.size(); i += 2) {
      const double px = x + pts[i] * w;
      const double py = y + pts[i + 1] * h;
      if (i == 0) c.moveTo(px, py);
      else c.lineTo(px, py);
    }
    c.closePath();
    c.fillStyle(fill);
    c.fill();
  };
  const auto label = [&](const std::string& t, double size, const char* color = "#111", double dy = 0) {
    c.fillStyle(color);
    c.font("bold " + jsStr(size) + "px " + FONT);
    c.fillText(t, cx, cy + dy, w * 0.92);
  };
  const double n = jsNumber(base, false);
  if (base == "310" || base == "311") {
    const std::vector<std::string> lines = split(arg, "|");
    const std::string l1 = lines.size() > 0 ? lines[0] : "";
    const std::string l2 = lines.size() > 1 ? lines[1] : "";
    c.fillStyle("#111");
    c.fillRect(x, y, w, h);
    c.fillStyle("#f6d417");
    c.fillRect(x + 5, y + 5, w - 10, h - 10);
    if (base == "310") {
      label(l1, h * 0.3, "#111", !l2.empty() ? -h * 0.12 : 0);
      if (!l2.empty()) label(l2, h * 0.15, "#111", h * 0.22);
    } else {
      c.fillStyle("#111");
      c.fillRect(x + 5, cy - 2, w - 10, 4);
      label(l1, h * 0.26, "#111", h * 0.22);
      c.strokeStyle("#d0141e");
      c.lineWidth(h * 0.07);
      c.beginPath();
      c.moveTo(x + w * 0.06, y + h * 0.94);
      c.lineTo(x + w * 0.94, y + h * 0.56);
      c.stroke();
    }
  } else if (base == "457.1") {
    // Umleitung (a diversion ahead): black on yellow, as the temporary signs of a closure are
    c.fillStyle("#111");
    c.fillRect(x, y, w, h);
    c.fillStyle("#f6c21c");
    c.fillRect(x + 6, y + 6, w - 12, h - 12);
    label("Umleitung", h * 0.3, "#111", -h * 0.08);
    c.fillStyle("#111");
    c.fillRect(x + w * 0.2, y + h * 0.72, w * 0.6, h * 0.05);
  } else if (startsWith(base, "274.") || startsWith(base, "290")) {
    c.fillStyle("#111");
    c.fillRect(x, y, w, h);
    c.fillStyle("#fff");
    c.fillRect(x + 4, y + 4, w - 8, h - 8);
    const char* speed = endsWith(base, "-20") ? "20" : "30";
    c.beginPath();
    c.arc(cx, cy - h * 0.1, r * 0.6, 0, PI * 2);
    c.fillStyle(startsWith(base, "274.2") ? "#888" : "#d0141e");
    c.fill();
    c.beginPath();
    c.arc(cx, cy - h * 0.1, r * 0.46, 0, PI * 2);
    c.fillStyle("#fff");
    c.fill();
    label(speed, r * 0.5, startsWith(base, "274.2") ? "#888" : "#111", -h * 0.1);
    label("ZONE", r * 0.3, startsWith(base, "274.2") ? "#888" : "#111", h * 0.33);
  } else if (startsWith(base, "274-")) {
    ring("#fff", "#d0141e", 0.2);
    label(base.substr(4), r * (base.size() > 6 ? 0.78 : 0.95));
  } else if (startsWith(base, "278-") || base == "282") {
    ring("#fff", "#444", 0.06);
    if (base != "282") label(base.substr(4), r * 0.8, "#666");
    c.strokeStyle("#333");
    c.lineWidth(r * 0.05);
    for (int i = -2; i <= 2; i++) {
      c.beginPath();
      c.moveTo(cx - r * 0.62 + i * r * 0.09, cy + r * 0.62 + i * r * 0.09);
      c.lineTo(cx + r * 0.62 + i * r * 0.09, cy - r * 0.62 + i * r * 0.09);
      c.stroke();
    }
  } else if (base == "205") {
    poly({0, 0, 1, 0, 0.5, 1}, "#d0141e");
    poly({0.17, 0.1, 0.83, 0.1, 0.5, 0.8}, "#fff");
  } else if (base == "206") {
    const double o = 0.293;
    poly({o, 0, 1 - o, 0, 1, o, 1, 1 - o, 1 - o, 1, o, 1, 0, 1 - o, 0, o}, "#fff");
    const double i = 0.03;
    poly({o + i * 0.4, i, 1 - o - i * 0.4, i, 1 - i, o + i * 0.4, 1 - i, 1 - o - i * 0.4, 1 - o - i * 0.4, 1 - i, o + i * 0.4, 1 - i, i, 1 - o - i * 0.4, i, o + i * 0.4},
         "#d0141e");
    label("STOP", r * 0.62, "#fff");
  } else if (base == "306" || base == "307") {
    poly({0.5, 0, 1, 0.5, 0.5, 1, 0, 0.5}, "#fff");
    poly({0.5, 0.12, 0.88, 0.5, 0.5, 0.88, 0.12, 0.5}, "#f6c817");
  } else if (base == "301" || n < 200) {
    poly({0.5, 0, 1, 1, 0, 1}, "#d0141e");
    poly({0.5, 0.2, 0.83, 0.9, 0.17, 0.9}, "#fff");
    label(base == "301" ? "▲" : "!", r * 0.7, "#111", h * 0.18);
  } else if (base == "267") {
    ring("#d0141e", "#fff", 0.05);
    c.fillStyle("#fff");
    c.fillRect(cx - r * 0.66, cy - r * 0.13, r * 1.32, r * 0.26);
  } else if (base == "250" || base == "260" || base == "253" || base == "251") {
    ring("#fff", "#d0141e", 0.18);
  } else if (base == "215" || startsWith(base, "209") || startsWith(base, "211") || startsWith(base, "214") || startsWith(base, "222") || base == "237" || base == "240") {
    ring("#1b5fa8", "#fff", 0.04);
    label(base == "215" ? "↺" : "↑", r * 1.1, "#fff");
  } else if (base == "224") {
    ring("#f6d417", "#1d7a34", 0.14);
    label("H", r * 1.0, "#1d7a34");
  } else if (n >= 1000) {
    c.fillStyle("#111");
    c.fillRect(x, y, w, h);
    c.fillStyle("#fff");
    c.fillRect(x + 4, y + 4, w - 8, h - 8);
    static const std::unordered_map<std::string, std::string> texts = {
        {"1020-30", "Anlieger frei"},
        {"1026-38", "Land- u. forstw. Verkehr frei"},
        {"1026-36", "Landw. Verkehr frei"},
        {"1022-10", "Rad frei"},
        {"1012-31", "Ende"},
        {"1012-30", "Anfang"},
    };
    const auto t = texts.find(base);
    label(t != texts.end() ? t->second : base, h * 0.3);
  } else {
    c.fillStyle("#fff");
    c.fillRect(x, y, w, h);
    c.fillStyle("#1b5fa8");
    c.fillRect(x + 4, y + 4, w - 8, h - 8);
    label(base, h * 0.3, "#fff");
  }
  c.restore();
}

void SignRenderer::add(LoadedTile& tile) {
  const std::vector<SignPost>* posts = &tile.header.signs;
  std::vector<SignPost> edited;
  if (editPost) {
    for (const SignPost& p : tile.header.signs) {
      std::optional<SignPost> e = editPost(p);
      if (e) edited.push_back(std::move(*e));
    }
    posts = &edited;
  }
  if (posts->empty()) return;
  const std::shared_ptr<Group> group = build(*posts, tile.header.cx, tile.header.yBase, tile.header.cz);
  tile.group->add(group);
  perTile_[tile.key] = group;
}

std::shared_ptr<Group> SignRenderer::build(const std::vector<SignPost>& posts, double cx, double yBase, double cz) {
  std::vector<double> pos;
  std::vector<float> uv;
  std::vector<uint32_t> idx;
  std::vector<uint32_t> idxBack;
  auto group = make<Group>();
  auto postMesh = make<InstancedMesh>(postGeo_, postMat_, static_cast<int>(posts.size()));
  Matrix4 m4;
  Quaternion quat;
  // (each sign the group shows is counted at once: its cell is not drawn over while the group is there)
  std::vector<std::string> used;
  std::unordered_set<std::string> usedSet;
  for (size_t pi = 0; pi < posts.size(); pi++) {
    const SignPost& p = posts[pi];
    const double s = std::sin(p.yaw);
    const double c = std::cos(p.yaw);
    // facing vector f = (-s, 0, -c); the viewer's right is (-c, 0, s)
    const double rx = -c;
    const double rz = s;
    double y = p.y + p.h;
    for (size_t k = p.s.size(); k-- > 0;) {
      const std::string& code = p.s[k];
      Slot& slot = this->slot(code);
      if (usedSet.insert(code).second) {
        used.push_back(code);
        slot.users++;
      }
      const auto [w, h] = signSize(code, slot.aspect, p.big == 1);
      const double y0 = y;
      const double y1 = y + h;
      const double hw = w / 2;
      const double fx = -s * 0.035;
      const double fz = -c * 0.035;
      const uint32_t base = static_cast<uint32_t>(pos.size() / 3);
      // front
      pos.insert(pos.end(), {p.x - rx * hw + fx, y0, p.z - rz * hw + fz, p.x + rx * hw + fx, y0, p.z + rz * hw + fz, p.x + rx * hw + fx, y1, p.z + rz * hw + fz, p.x - rx * hw + fx, y1,
                             p.z - rz * hw + fz});
      for (const double v : {slot.u0, slot.v0, slot.u1, slot.v0, slot.u1, slot.v1, slot.u0, slot.v1}) uv.push_back(static_cast<float>(v));
      idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
      // back: same outline, drawn with the bare-metal material
      pos.insert(pos.end(), {p.x - rx * hw - fx * 0.2, y0, p.z - rz * hw - fz * 0.2, p.x + rx * hw - fx * 0.2, y0, p.z + rz * hw - fz * 0.2, p.x + rx * hw - fx * 0.2, y1,
                             p.z + rz * hw - fz * 0.2, p.x - rx * hw - fx * 0.2, y1, p.z - rz * hw - fz * 0.2});
      for (const double v : {slot.u0, slot.v0, slot.u1, slot.v0, slot.u1, slot.v1, slot.u0, slot.v1}) uv.push_back(static_cast<float>(v));
      idxBack.insert(idxBack.end(), {base + 4, base + 6, base + 5, base + 4, base + 7, base + 6});
      y = y1 + 0.03;
    }
    quat.identity();
    m4.compose(Vector3(p.x, p.y - 0.3, p.z), quat, Vector3(1, y - p.y + 0.3, 1));
    postMesh->setMatrixAt(static_cast<int>(pi), m4);
  }
  postMesh->instanceMatrix->needsUpdate();
  postMesh->computeBoundingSphere();
  group->add(postMesh);
  auto g = make<BufferGeometry>();
  // tile-relative positions keep float precision
  std::vector<float> rel(pos.size());
  for (size_t i = 0; i < pos.size(); i += 3) {
    rel[i] = static_cast<float>(pos[i] - cx);
    rel[i + 1] = static_cast<float>(pos[i + 1] - yBase);
    rel[i + 2] = static_cast<float>(pos[i + 2] - cz);
  }
  g->setAttribute("position", attr(std::move(rel), 3));
  g->setAttribute("uv", attr(std::move(uv), 2));
  const int front = static_cast<int>(idx.size());
  const int back = static_cast<int>(idxBack.size());
  idx.insert(idx.end(), idxBack.begin(), idxBack.end());
  g->setIndex(idx);
  g->addGroup(0, front, 0);
  g->addGroup(front, back, 1);
  g->computeBoundingSphere();
  auto plates = make<Mesh>(g, std::vector<MaterialPtr>{material, backMaterial});
  plates->position.set(cx, yBase, cz);
  plates->updateMatrix();
  plates->matrixAutoUpdate = false;
  plates->name = "signs";
  group->add(plates);
  groupCodes_[group.get()] = std::move(used);
  return group;
}

void SignRenderer::remove(const LoadedTile& tile) {
  const auto group = perTile_.find(tile.key);
  if (group == perTile_.end()) return;
  release(*group->second);
  perTile_.erase(group);
}

void SignRenderer::release(const Object3D& group) {
  const auto found = groupCodes_.find(&group);
  if (found == groupCodes_.end()) return;
  const std::vector<std::string> codes = std::move(found->second);
  groupCodes_.erase(found);
  gone_++;
  for (const std::string& c : codes) {
    const auto s = slots_.find(c);
    if (s != slots_.end() && s->second->users > 0) {
      s->second->users--;
      s->second->last = gone_;
    }
  }
}

}  // namespace fs

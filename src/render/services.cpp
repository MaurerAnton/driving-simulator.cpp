// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/services.ts @ v1.7.7
#include "render/services.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/js.h"
#include "core/random.h"
#include "engine/curves.h"
#include "engine/geometries.h"

namespace fs {

using svc::CanopyPlan;
using svc::FUELS;
using svc::GANTRY;
using svc::ServiceSite;
using svc::WashPlan;

namespace {

// ------------------------------------------------------------------ materials (shared by every site)

MaterialPtr lam(uint32_t color) { return lambert({.color = color}); }

/** (the original's module constants: made when first asked for, kept to the end) */
struct ServiceMaterials {
  const MaterialPtr concrete = lam(0xb4b1a9);
  const MaterialPtr underside = lam(0xe6e8ea);
  const MaterialPtr steel = lam(0x9aa2aa);
  const MaterialPtr dark = lam(0x24282c);
  const MaterialPtr white = lam(0xeef0f1);
  const MaterialPtr rubber = lam(0x141517);
  const MaterialPtr yellow = lam(0xf0c419);
  const MaterialPtr wall = lam(0xd3d6d6);
  const MaterialPtr roof = lam(0x8c9196);
  const MaterialPtr rail = lam(0x6e757c);
  const MaterialPtr light = basic({.color = 0xfffaea});
  const MaterialPtr red = basic({.color = 0xff2a1a});
  const MaterialPtr green = basic({.color = 0x23e05a});
  const MaterialPtr off = lam(0x2b2f33);
  std::map<uint32_t, MaterialPtr> brand;
  std::map<uint32_t, MaterialPtr> colour;
  MaterialPtr idleDisplay;
  MaterialPtr brush;
  TexturePtr dot;
};

ServiceMaterials& M() {
  static auto* m = new ServiceMaterials();
  return *m;
}

/** Colours of a station's own, from its name: none is a real brand's. */
constexpr std::array<uint32_t, 8> BRAND_COLOURS = {0x1f5fa8, 0xc8281e, 0x1d8a4c, 0xe8a90c, 0x5b3591, 0x0d8aa0, 0xd8601a, 0x2c3e50};

/** The UTF-16 units of a UTF-8 text (what charCodeAt walks through). */
std::vector<uint16_t> utf16Units(const std::string& s) {
  std::vector<uint16_t> out;
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    uint32_t cp = c;
    int n = 0;
    if (c >= 0xf0) cp = c & 0x07, n = 3;
    else if (c >= 0xe0) cp = c & 0x0f, n = 2;
    else if (c >= 0xc0) cp = c & 0x1f, n = 1;
    i++;
    for (int k = 0; k < n && i < s.size(); k++, i++) cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3f);
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back(static_cast<uint16_t>(0xd800 + (cp >> 10)));
      out.push_back(static_cast<uint16_t>(0xdc00 + (cp & 0x3ff)));
    } else out.push_back(static_cast<uint16_t>(cp));
  }
  return out;
}

uint32_t brandColour(const ServiceSite& site) {
  // (toLowerCase: the letters of ASCII – a brand's colour of a name with capital umlauts may come out another)
  const std::string key = toLowerAscii(!site.brand.empty() ? site.brand : !site.name.empty() ? site.name : site.osm);
  uint32_t h = 2166136261u;
  for (uint16_t c : utf16Units(key)) h = (h ^ c) * 16777619u;
  return BRAND_COLOURS[h % BRAND_COLOURS.size()];
}

const MaterialPtr& brandMaterial(uint32_t colour) {
  MaterialPtr& m = M().brand[colour];
  if (!m) m = lam(colour);
  return m;
}

/** The colours of the nozzles by fuel (as the grips on German pumps tend to be: diesel black, petrol green or blue). */
uint32_t nozzleColour(const std::string& fuel, uint32_t fallback) {
  static const auto* c = new std::map<std::string, uint32_t>{{"e10", 0x2e9b45}, {"e5", 0x2a5bd7},    {"plus", 0x7d3fb0}, {"diesel", 0x1b1c1e},
                                                             {"hgv", 0x1b1c1e}, {"lpg", 0xd8a514}, {"cng", 0x8a8f94},  {"adblue", 0x3a7fd0}};
  auto it = c->find(fuel);
  return it != c->end() ? it->second : fallback;
}

/** A canvas texture of w × h pixels drawn by `draw`. */
TexturePtr canvasTex(int w, int h, const std::function<void(const Ctx2D&)>& draw) {
  const Canvas canvas(w, h);
  draw(canvas.getContext2D());
  TexturePtr tex = canvasTexture(canvas);
  tex->srgb = true;
  tex->anisotropy = 4;
  return tex;
}

const std::string FONT = "\"DIN Alternate\", \"Bahnschrift\", \"Roboto Condensed\", \"Arial Narrow\", Arial, sans-serif";

std::string hex(uint32_t c) {
  static const char* digits = "0123456789abcdef";
  std::string s = "#000000";
  for (int i = 0; i < 6; i++) s[static_cast<size_t>(6 - i)] = digits[(c >> (i * 4)) & 0xf];
  return s;
}

/** A pump's display while nobody fills up: dark, with dashes. */
const MaterialPtr& idleDisplayMaterial() {
  MaterialPtr& m = M().idleDisplay;
  if (!m)
    m = basic({.map = canvasTex(128, 96,
                                [](const Ctx2D& c) {
                                  c.fillStyle("#10151a");
                                  c.fillRect(0, 0, 128, 96);
                                  c.fillStyle("#3f6a3a");
                                  c.font("600 26px " + FONT);
                                  c.textAlign("right");
                                  c.fillText("--,--", 118, 40);
                                  c.fillText("--,--", 118, 80);
                                }),
               .toneMapped = false});
  return m;
}

/** The bristles of a brush: stripes round it, so that its turning shows. */
const MaterialPtr& brushMaterial() {
  MaterialPtr& m = M().brush;
  if (!m) {
    TexturePtr tex = canvasTex(64, 8, [](const Ctx2D& c) {
      const char* cols[] = {"#2f6fd0", "#5b9cf0", "#e8eef5", "#3f7fe0"};
      for (int i = 0; i < 16; i++) {
        c.fillStyle(cols[i % 4]);
        c.fillRect(i * 4, 0, 4, 8);
      }
    });
    tex->wrapS = Wrapping::Repeat;
    tex->repeat.set(3, 1);
    m = lambert({.map = tex});
  }
  return m;
}

/** A round soft dot for the foam and the water. */
const TexturePtr& dotTexture() {
  TexturePtr& t = M().dot;
  if (!t)
    t = canvasTex(32, 32, [](const Ctx2D& c) {
      const CanvasGradient g = c.createRadialGradient(16, 16, 0, 16, 16, 16);
      g.addColorStop(0, "rgba(255,255,255,1)");
      g.addColorStop(0.6, "rgba(255,255,255,0.75)");
      g.addColorStop(1, "rgba(255,255,255,0)");
      c.fillStyle(g);
      c.fillRect(0, 0, 32, 32);
    });
  return t;
}

/** The yaw that turns a model's −z towards (ux, uz). */
double yawOf(double ux, double uz) { return std::atan2(-ux, -uz); }

/** Where a ray from (x, z) along (dx, dz) leaves a ring – the distance, or NaN. */
double rayOut(const std::vector<double>& ring, double x, double z, double dx, double dz) {
  double best = std::numeric_limits<double>::quiet_NaN();
  const size_t n = ring.size();
  if (n < 2) return best;
  for (size_t i = 0, j = n - 2; i < n; j = i, i += 2) {
    const double ax = ring[j];
    const double az = ring[j + 1];
    const double ex = ring[i] - ax;
    const double ez = ring[i + 1] - az;
    const double den = dx * ez - dz * ex;
    if (std::abs(den) < 1e-9) continue;
    const double t = ((ax - x) * ez - (az - z) * ex) / den;
    const double s = ((ax - x) * dz - (az - z) * dx) / den;
    if (t > 0 && s >= 0 && s <= 1 && !(t >= best)) best = t;
  }
  return best;
}

/** A hose: a tube along a sagging curve from a to b. */
GeometryPtr hoseGeometry(const Vector3& a, const Vector3& b, double sag) {
  Vector3 mid = a.clone().lerp(b, 0.5);
  mid.y = std::min(a.y, b.y) - sag;
  Vector3 q1 = a.clone().lerp(mid, 0.55);
  q1.y -= sag * 0.25;
  Vector3 q2 = b.clone().lerp(mid, 0.55);
  q2.y -= sag * 0.25;
  return tubeGeometry(CatmullRomCurve3({a, q1, mid, q2, b}), 16, 0.017, 5, false);
}

/** A plain material of a colour, made once. */
const MaterialPtr& colourMaterial(uint32_t colour) {
  MaterialPtr& m = M().colour[colour];
  if (!m) m = lam(colour);
  return m;
}

/**
 * The pieces of a nozzle in its own frame – the grip in its colour, the
 * spout pointing along −z and a little down –, each with its material.
 */
std::vector<std::pair<GeometryPtr, MaterialPtr>> nozzlePieces(uint32_t colour) {
  GeometryPtr grip = boxGeometry(0.05, 0.16, 0.09);
  grip->translate(0, 0, 0.08);
  GeometryPtr body = boxGeometry(0.06, 0.06, 0.16);
  body->translate(0, 0.07, 0.04);
  GeometryPtr spout = cylinderGeometry(0.012, 0.014, 0.2, 6);
  spout->rotateX(PI / 2 + 0.35).translate(0, 0.04, -0.08);
  return {{grip, colourMaterial(colour)}, {body, M().steel}, {spout, M().steel}};
}

/** A nozzle as a group of its own (the one that moves). */
std::shared_ptr<Group> nozzleGroup(uint32_t colour) {
  auto g = make<Group>();
  for (auto& [geo, mat] : nozzlePieces(colour)) g->add(make<Mesh>(geo, mat));
  return g;
}

struct PumpSize {
  double len, depth, height, base;
};
constexpr PumpSize PUMP = {0.92, 0.46, 2.05, 0.22};
/** where the nozzles hang on a face: along the pump, and how high */
struct Holster {
  std::array<double, 2> along;
  double y;
};
constexpr Holster HOLSTER = {{-0.27, 0.27}, 1.05};
/** where a hose leaves the pump: along it, at the top */
constexpr double OUTLET_Y = 1.98;

}  // namespace

// ------------------------------------------------------------------ building geometry

/** Collects pieces of geometry per material, to be merged into one mesh each. */
class SiteView::Parts {
 public:
  void add(const MaterialPtr& material, GeometryPtr g) {
    for (auto& [m, list] : lists_)
      if (m == material) {
        list.push_back(std::move(g));
        return;
      }
    lists_.push_back({material, {std::move(g)}});
  }

  /** A box of w × h × d with its foot's middle at (x, y, z), turned by yaw. */
  void box(const MaterialPtr& material, double w, double h, double d, double x, double y, double z, double yaw = 0) {
    GeometryPtr g = boxGeometry(w, h, d);
    g->translate(0, h / 2, 0);
    if (yaw) g->rotateY(yaw);
    g->translate(x, y, z);
    add(material, g);
  }

  /** Merge into meshes, added to `group`. */
  void into(Group& group, bool shadows) {
    for (auto& [material, list] : lists_) {
      std::vector<GeometryPtr> geos;
      for (const GeometryPtr& g : list) geos.push_back(g->index ? g->toNonIndexed() : g);
      for (const GeometryPtr& g : geos) {
        if (g->getAttribute("uv")) g->deleteAttribute("uv");
        std::vector<std::string> names;
        for (const auto& [name, a] : g->attributes)
          if (name != "position" && name != "normal") names.push_back(name);
        for (const std::string& name : names) g->deleteAttribute(name);
      }
      GeometryPtr merged = mergeGeometries(geos, false);
      if (!merged) continue;
      auto mesh = make<Mesh>(merged, material);
      mesh->castShadow = shadows;
      mesh->receiveShadow = true;
      group.add(mesh);
    }
    lists_.clear();
  }

 private:
  std::vector<std::pair<MaterialPtr, std::vector<GeometryPtr>>> lists_;
};

// ------------------------------------------------------------------ a site

SiteView::SiteView(const ServiceSite& siteIn, const std::function<double(double, double)>& ground, bool shadows) : site(siteIn), colour(brandColour(siteIn)) {
  group->name = std::string(site.k == "fuel" ? "fuel station" : "car wash") + " " + site.name;
  // (built round the site's middle: the numbers of the geometry stay small)
  group->position.set(site.x, 0, site.z);
  Parts parts;
  for (const CanopyPlan& c : site.canopies) canopy(parts, c, ground);
  if (site.k == "fuel" && site.sign) pylon(*site.sign, ground);
  if (site.wash) wash(parts, *site.wash, ground);
  parts.into(*group, shadows);
  group->updateMatrixWorld(true);
}

// ---------------------------------------------------------------- the canopy, the islands, the pumps
void SiteView::canopy(Parts& parts, const CanopyPlan& c, const std::function<double(double, double)>& ground) {
  const MaterialPtr& brand = brandMaterial(colour);
  const svc::Rect& r = c.rect;
  const double g0 = std::isfinite(c.y0) ? c.y0 : ground(r.cx, r.cz);
  // the top: the building model's, else what a canopy usually has (never so low that a van would not fit)
  const double top = std::isfinite(c.y1) ? std::max(c.y1, g0 + 4.2) : g0 + (std::isfinite(c.h) ? std::max(4.2, c.h) : 5.2);
  const double thick = 0.7;
  const double under = top - thick;
  // the slab: the outline drawn up from the underside, its edge in the station's colour
  Shape shape;
  for (size_t i = 0; i < c.ring.size(); i += 2) {
    const double x = lx(c.ring[i]);
    const double y = -lz(c.ring[i + 1]);
    if (i == 0) shape.moveTo(x, y);
    else shape.lineTo(x, y);
  }
  GeometryPtr slab = extrudeGeometry(shape, {.depth = thick, .bevelEnabled = false});
  slab->rotateX(-PI / 2);
  slab->translate(0, under, 0);
  auto slabMesh = make<Mesh>(slab, std::vector<MaterialPtr>{M().underside, brand});
  slabMesh->castShadow = true;
  slabMesh->receiveShadow = true;
  group->add(slabMesh);
  // lights in the underside, every 4 m
  const int nu = static_cast<int>(std::max(1.0, std::floor(r.len / 4)));
  const int nv = static_cast<int>(std::max(1.0, std::floor(r.wid / 4)));
  for (int i = 0; i < nu; i++) {
    for (int j = 0; j < nv; j++) {
      const auto [x, z] = svc::fromRect(r, (i + 0.5 - nu / 2.0) * (r.len / nu), (j + 0.5 - nv / 2.0) * (r.wid / nv));
      if (!svc::inRing(x, z, c.ring)) continue;
      parts.box(M().light, 0.7, 0.04, 0.7, lx(x), under - 0.04, lz(z));
    }
  }
  // the name on the edge, on both long sides
  if (!site.brand.empty() || !site.name.empty()) {
    const std::string label = toUpperAscii(!site.brand.empty() ? site.brand : site.name);
    const uint32_t col = colour;
    TexturePtr tex = canvasTex(512, 64, [&label, col](const Ctx2D& cx) {
      cx.fillStyle(hex(col));
      cx.fillRect(0, 0, 512, 64);
      cx.fillStyle("#ffffff");
      cx.textAlign("center");
      cx.textBaseline("middle");
      double size = 46;
      cx.font("800 " + jsStr(size) + "px " + FONT);
      const double w = cx.measureText(label).width;
      if (w > 480) {
        size *= 480 / w;
        cx.font("800 " + jsStr(size) + "px " + FONT);
      }
      cx.fillText(label, 256, 34);
    });
    MaterialPtr mat = basic({.map = tex});
    const double w = std::min(r.len * 0.5, 8.0);
    for (double side : {-1.0, 1.0}) {
      const double vx = -r.uz * side;
      const double vz = r.ux * side;
      const double d = rayOut(c.ring, r.cx, r.cz, vx, vz);
      if (!std::isfinite(d)) continue;
      auto panel = make<Mesh>(planeGeometry(w, w / 8), mat);
      panel->position.set(lx(r.cx + vx * (d + 0.03)), under + thick / 2, lz(r.cz + vz * (d + 0.03)));
      panel->rotation.y = yawOf(-vx, -vz);
      group->add(panel);
    }
  }
  // the islands with their kerbs and posts, and the columns standing on them
  for (const svc::Island& isl : c.islands) {
    const double y = ground(isl.x, isl.z);
    const double yaw = yawOf(isl.ux, isl.uz);
    // (the box's length along its local z, turned onto the island's axis)
    parts.box(M().concrete, isl.wid, 0.16, isl.len, lx(isl.x), y - 0.05, lz(isl.z), yaw);
    for (double e : {-1.0, 1.0}) {
      const double px = isl.x + isl.ux * e * (isl.len / 2 - 0.12);
      const double pz = isl.z + isl.uz * e * (isl.len / 2 - 0.12);
      parts.box(M().yellow, 0.16, 1.0, 0.16, lx(px), y + 0.1, lz(pz), yaw);
    }
    const svc::Rect isRect{isl.x, isl.z, isl.ux, isl.uz, isl.len, isl.wid};
    rings.push_back(svc::rectRing(isRect, 0.05));
    pump(parts, isl.x, y + 0.11, isl.z, isl.ux, isl.uz);
  }
  for (size_t k = 0; k + 1 < c.columns.size(); k += 2) {
    const double x = c.columns[k];
    const double z = c.columns[k + 1];
    const double y = ground(x, z);
    parts.box(brand, 0.42, under - y + 0.05, 0.42, lx(x), y, lz(z));
    discs.insert(discs.end(), {x, y, z, 0.28});
  }
}

void SiteView::pump(Parts& parts, double x, double y, double z, double ux, double uz) {
  const double yaw = yawOf(ux, uz);
  const double px = lx(x);
  const double pz = lz(z);
  // (the cabinet's length along the island: its local z)
  parts.box(M().dark, PUMP.depth + 0.06, PUMP.base, PUMP.len + 0.06, px, y, pz, yaw);
  parts.box(M().white, PUMP.depth, PUMP.height - PUMP.base - 0.32, PUMP.len, px, y + PUMP.base, pz, yaw);
  parts.box(brandMaterial(colour), PUMP.depth + 0.02, 0.32, PUMP.len + 0.02, px, y + PUMP.height - 0.32, pz, yaw);
  PumpView view{x, y, z, ux, uz, {}};
  const double vx = -uz;
  const double vz = ux;
  const std::vector<std::string> fuels = !site.sells.empty() ? site.sells : std::vector<std::string>{"e10", "diesel"};
  for (double side : {-1.0, 1.0}) {
    // the display, a little out from the face
    auto d = make<Mesh>(planeGeometry(0.42, 0.3), idleDisplayMaterial());
    const double fx = x + vx * side * (PUMP.depth / 2 + 0.006);
    const double fz = z + vz * side * (PUMP.depth / 2 + 0.006);
    d->position.set(lx(fx), y + 1.5, lz(fz));
    d->rotation.y = yawOf(-vx * side, -vz * side);
    group->add(d);
    view.displays.push_back(d);
    // two nozzles in their holsters, their hoses from the top of the pump
    for (size_t k = 0; k < HOLSTER.along.size(); k++) {
      const double along = HOLSTER.along[k];
      const std::string& fuel = fuels[(k + (side > 0 ? 0 : 1)) % fuels.size()];
      const double hx = x + ux * along + vx * side * (PUMP.depth / 2 + 0.07);
      const double hz = z + uz * along + vz * side * (PUMP.depth / 2 + 0.07);
      parts.box(M().dark, 0.1, 0.16, 0.14, lx(hx - vx * side * 0.04), y + HOLSTER.y - 0.12, lz(hz - vz * side * 0.04), yaw);
      // (hanging, the spout down into the holster, towards the pump)
      Quaternion q;
      q.setFromEuler(Euler(-1.2, yawOf(-vx * side, -vz * side), 0, Euler::YXZ));
      Matrix4 at;
      at.compose(Vector3(lx(hx), y + HOLSTER.y, lz(hz)), q, Vector3(1, 1, 1));
      for (auto& [geo, mat] : nozzlePieces(nozzleColour(fuel, 0x1b1c1e))) {
        geo->applyMatrix4(at);
        parts.add(mat, geo);
      }
      const Vector3 top(lx(x + ux * along + vx * side * (PUMP.depth / 2 - 0.05)), y + OUTLET_Y, lz(z + uz * along + vz * side * (PUMP.depth / 2 - 0.05)));
      const Vector3 end(lx(hx + vx * side * 0.05), y + HOLSTER.y + 0.1, lz(hz + vz * side * 0.05));
      parts.add(M().rubber, hoseGeometry(top, end, 0.55));
    }
  }
  pumps.push_back(std::move(view));
}

// ---------------------------------------------------------------- the price sign
void SiteView::pylon(const std::array<double, 3>& sign, const std::function<double(double, double)>& ground) {
  const auto [x, z, yaw] = sign;
  const double y = ground(x, z);
  const ServiceSite& s = site;
  const std::string label = toUpperAscii(!s.brand.empty() ? s.brand : !s.name.empty() ? s.name : "Tankstelle");
  std::vector<std::string> rows;
  for (const std::string& f : !s.sells.empty() ? s.sells : std::vector<std::string>{"e10", "e5", "diesel"})
    if (f != "adblue" && rows.size() < 4) rows.push_back(f);
  const uint32_t col = colour;
  TexturePtr tex = canvasTex(256, 1024, [&](const Ctx2D& c) {
    c.fillStyle("#f4f5f6");
    c.fillRect(0, 0, 256, 1024);
    c.fillStyle(hex(col));
    c.fillRect(0, 0, 256, 300);
    c.fillStyle("#ffffff");
    c.textAlign("center");
    c.textBaseline("middle");
    double size = 60;
    c.font("800 " + jsStr(size) + "px " + FONT);
    const double w = c.measureText(label).width;
    if (w > 230) {
      size *= 230 / w;
      c.font("800 " + jsStr(size) + "px " + FONT);
    }
    c.fillText(label, 128, 150);
    for (size_t i = 0; i < rows.size(); i++) {
      const std::string& f = rows[i];
      const double y0 = 330 + static_cast<double>(i) * 160;
      c.fillStyle("#1e2227");
      c.fillRect(10, y0, 236, 140);
      c.fillStyle("#dfe4e8");
      c.font("600 30px " + FONT);
      c.textAlign("left");
      auto it = FUELS->find(f);
      c.fillText(it != FUELS->end() ? it->second.label : f, 22, y0 + 30);
      const std::string p = toFixed(svc::priceAt(s, f), 3);
      std::string head = p.substr(0, p.size() - 1);
      const size_t dot = head.find('.');
      if (dot != std::string::npos) head[dot] = ',';
      c.fillStyle("#ffd23a");
      c.font("700 70px " + FONT);
      c.textAlign("right");
      c.fillText(head, 208, y0 + 95);
      c.font("700 38px " + FONT);
      c.fillText(p.substr(p.size() - 1), 236, y0 + 78);
    }
  });
  MaterialPtr face = lambert({.map = tex});
  const MaterialPtr& edge = brandMaterial(colour);
  auto board = make<Mesh>(boxGeometry(1.6, 5.4, 0.34), std::vector<MaterialPtr>{edge, edge, edge, edge, face, face});
  board->position.set(lx(x), y + 0.6 + 2.7, lz(z));
  board->rotation.y = yaw;
  board->castShadow = true;
  auto base = make<Mesh>(boxGeometry(1.9, 0.6, 0.55), M().concrete);
  base->position.set(lx(x), y + 0.3 - 0.05, lz(z));
  base->rotation.y = yaw;
  group->add(board, base);
  discs.insert(discs.end(), {x, y, z, 0.95});
}

// ---------------------------------------------------------------- a car wash
void SiteView::wash(Parts& parts, const WashPlan& w, const std::function<double(double, double)>& ground) {
  const svc::Rect& r = w.rect;
  const double y = std::isfinite(w.y0) ? std::max(w.y0, ground(r.cx, r.cz) - 0.3) : ground(r.cx, r.cz);
  const double top = std::isfinite(w.y1) ? std::max(w.y1, y + 3.4) : y + 3.8;
  const double yaw = yawOf(r.ux, r.uz);
  const double halfW = r.wid / 2;
  // the floor of the bay and the gantry's rails
  parts.box(M().concrete, r.wid, 0.06, r.len, lx(r.cx), y - 0.03, lz(r.cz), yaw);
  const double railV = std::min(halfW - 0.25, GANTRY.width / 2 + 0.75);
  for (double side : {-1.0, 1.0}) {
    const auto [x, z] = svc::fromRect(r, 0, side * railV);
    parts.box(M().rail, 0.12, 0.1, r.len - 0.4, lx(x), y, lz(z), yaw);
  }
  const svc::RoofJson* roof = site.roofs.empty() ? nullptr : &site.roofs[0];
  if (w.hall && !(roof && roof->open)) {
    // a hall: a wall along each long side, a roof, open at both ends
    for (double side : {-1.0, 1.0}) {
      const auto [x, z] = svc::fromRect(r, 0, side * (halfW - 0.12));
      parts.box(M().wall, 0.24, top - y - 0.3 + 0.4, r.len, lx(x), y - 0.4, lz(z), yaw);
      rings.push_back(svc::rectRing({x, z, r.ux, r.uz, r.len, 0.24}));
    }
    parts.box(M().roof, r.wid + 0.2, 0.3, r.len + 0.2, lx(r.cx), top - 0.3, lz(r.cz), yaw);
    // a light strip under the roof
    parts.box(M().light, 0.25, 0.04, r.len - 1, lx(r.cx), top - 0.34, lz(r.cz), yaw);
  } else if (w.hall) {
    // under a roof on columns
    for (const auto& [su, sv] : {std::pair{-1, -1}, std::pair{1, -1}, std::pair{1, 1}, std::pair{-1, 1}}) {
      const auto [x, z] = svc::fromRect(r, su * (r.len / 2 - 0.25), sv * (halfW - 0.25));
      parts.box(M().steel, 0.3, top - y, 0.3, lx(x), y - 0.1, lz(z));
      discs.insert(discs.end(), {x, y, z, 0.25});
    }
    parts.box(M().roof, r.wid, 0.3, r.len, lx(r.cx), top - 0.3, lz(r.cz), yaw);
  }
  // the gantry: a portal over the bay, with its brushes
  auto g = make<Group>();
  const double postV = railV;
  const double beamY = GANTRY.height + 0.55;
  for (double side : {-1.0, 1.0}) {
    auto post = make<Mesh>(boxGeometry(0.34, beamY, 0.7), brandMaterial(colour));
    post->position.set(side * postV, beamY / 2, 0);
    g->add(post);
  }
  auto beam = make<Mesh>(boxGeometry(postV * 2 + 0.34, 0.5, 0.7), brandMaterial(colour));
  beam->position.set(0, beamY + 0.25, 0);
  g->add(beam);
  const MaterialPtr& brush = brushMaterial();
  GeometryPtr sideGeo = cylinderGeometry(0.42, 0.42, 2.0, 18, 1, true);
  auto brushL = make<Mesh>(sideGeo, brush);
  auto brushR = make<Mesh>(sideGeo, brush);
  brushL->position.set(-postV + 0.6, 1.1, 0.05);
  brushR->position.set(postV - 0.6, 1.1, 0.05);
  GeometryPtr topGeo = cylinderGeometry(0.42, 0.42, postV * 2 - 0.5, 18, 1, true);
  topGeo->rotateZ(PI / 2);
  auto brushTop = make<Mesh>(topGeo, brush);
  brushTop->position.set(0, GANTRY.height, -0.3);
  g->add(brushL, brushR, brushTop);
  // the light at its front: green – drive in, red – stop
  auto lampRed = make<Mesh>(circleGeometry(0.11, 12), M().off);
  auto lampGreen = make<Mesh>(circleGeometry(0.11, 12), M().off);
  lampRed->position.set(-0.2, beamY + 0.25, -0.36);
  lampGreen->position.set(0.2, beamY + 0.25, -0.36);
  lampRed->rotation.y = PI;
  lampGreen->rotation.y = PI;
  g->add(lampRed, lampGreen);
  for (const ObjectPtr& o : g->children) o->castShadow = true;
  group->add(g);
  // (over the car round the mark: in a long hall it does not travel the whole length)
  gantry = GantryView{g, brushL, brushR, brushTop, lampRed, lampGreen, r, y, std::max(0.5, std::min(3.6, r.len / 2 - 1.2)), postV};
  placeGantry(1, 1, 0, 0, 0, 0);
}

void SiteView::placeGantry(double at, double face, double close, double topY, double spin, double light) {
  if (!gantry) return;
  GantryView& gv = *gantry;
  const auto [x, z] = svc::fromRect(gv.rect, at * gv.reach, 0);
  gv.group->position.set(lx(x), gv.y, lz(z));
  gv.group->rotation.y = yawOf(-face * gv.rect.ux, -face * gv.rect.uz);
  const double open = gv.postV - 0.6;
  // (in to the sides of a car of 1.8 m)
  const double inner = 1.32;
  const double v = open + (inner - open) * close;
  gv.brushL->position.x = -v;
  gv.brushR->position.x = v;
  gv.brushTop->position.y = topY > 0 ? topY : GANTRY.height;
  gv.brushL->rotation.y += spin;
  gv.brushR->rotation.y -= spin;
  gv.brushTop->rotation.x += spin;
  gv.lampRed->material = light < 0 ? M().red : M().off;
  gv.lampGreen->material = light > 0 ? M().green : M().off;
}

Vector3& SiteView::gantryAt(Vector3& out) const {
  if (!gantry) return out.set(site.x, 0, site.z);
  return out.copy(gantry->group->position).add(group->position);
}

void SiteView::dispose() {
  // (the geometries, pictures and materials of its own go with their meshes)
  group->removeFromParent();
}

// ------------------------------------------------------------------ what moves while one fills up or washes

FillView::FillView() : canvas_(128, 96) {
  group->name = "filling up";
  nozzle_ = nozzleGroup(0x2e9b45);
  group->add(nozzle_);
  tex_ = canvasTexture(canvas_);
  tex_->srgb = true;
  display_ = basic({.map = tex_, .toneMapped = false});
  group->visible = false;
}

void FillView::show(PumpView& pump, double side, const Vector3& filler, double t, const std::string& fuel) {
  if (pump_ && pump_ != &pump) hide();
  pump_ = &pump;
  for (const auto& d : pump.displays) d->material = display_;
  const double vx = -pump.uz * side;
  const double vz = pump.ux * side;
  const Vector3 holster(pump.x + vx * (PUMP.depth / 2 + 0.07) + pump.ux * HOLSTER.along[0], pump.y + HOLSTER.y,
                        pump.z + vz * (PUMP.depth / 2 + 0.07) + pump.uz * HOLSTER.along[0]);
  const double e = t * t * (3 - 2 * t);
  Vector3 at = holster.clone().lerp(filler, e);
  // (lifted over the gap on the way)
  at.y += std::sin(e * PI) * 0.35;
  nozzle_->position.copy(at);
  // the spout from the holster (towards the pump, down) round to the filler (into the vehicle's side)
  nozzle_->rotation.set(-0.25 - (1 - e) * 0.95, yawOf(vx, vz) + (1 - e) * PI, 0, Euler::YXZ);
  std::static_pointer_cast<Mesh>(nozzle_->children[0])->material = colourMaterial(nozzleColour(fuel, 0x2e9b45));
  const Vector3 outlet(pump.x + vx * (PUMP.depth / 2 - 0.05) + pump.ux * HOLSTER.along[0], pump.y + OUTLET_Y,
                       pump.z + vz * (PUMP.depth / 2 - 0.05) + pump.uz * HOLSTER.along[0]);
  Vector3 end = at.clone().add(Vector3(vx, 0, vz).multiplyScalar(0.12));
  end.y += 0.02;
  if (hose_) group->remove(hose_);
  hose_ = make<Mesh>(hoseGeometry(outlet, end, 0.35 + 0.25 * (1 - e)), M().rubber);
  group->add(hose_);
  group->visible = true;
}

void FillView::count(double litres, double cost) {
  const std::string text = toFixed(cost, 2) + "|" + toFixed(litres, 2);
  if (text == last_) return;
  last_ = text;
  const auto comma = [](std::string s) {
    const size_t d = s.find('.');
    if (d != std::string::npos) s[d] = ',';
    return s;
  };
  const Ctx2D c = canvas_.getContext2D();
  c.fillStyle("#0e1418");
  c.fillRect(0, 0, 128, 96);
  c.textAlign("right");
  c.fillStyle("#ff5b36");
  c.font("700 30px " + FONT);
  c.fillText(comma(toFixed(cost, 2)), 116, 38);
  c.fillStyle("#7cf06a");
  c.fillText(comma(toFixed(litres, 2)), 116, 80);
  c.fillStyle("#7d8a94");
  c.font("600 13px " + FONT);
  c.textAlign("left");
  c.fillText("EUR", 6, 30);
  c.fillText("L", 6, 72);
  tex_->needsUpdate();
}

void FillView::hide() {
  if (pump_)
    for (const auto& d : pump_->displays) d->material = idleDisplayMaterial();
  pump_ = nullptr;
  group->visible = false;
}

SprayView::SprayView() : vel_(N * 3, 0.0f), life_(N, 0.0f) {
  pos_ = attr(std::vector<float>(N * 3, 0.0f), 3);
  pos_->setDynamic();
  col_ = attr(std::vector<float>(N * 3, 0.0f), 3);
  col_->setDynamic();
  auto g = make<BufferGeometry>();
  g->setAttribute("position", pos_);
  g->setAttribute("color", col_);
  points = make<Points>(g, pointsMaterial({.map = dotTexture(), .vertexColors = true, .transparent = true, .opacity = 0.9, .depthWrite = false}));
  points->material->size = 0.16;
  points->frustumCulled = false;
  points->visible = false;
  points->name = "car wash: foam and water";
}

void SprayView::update(double dt, double x, double y, double z, double vx, double vz, double rate, bool foam) {
  std::vector<float>& pos = pos_->f32;
  std::vector<float>& col = col_->f32;
  int add = static_cast<int>(std::min(60.0, jsRound(rate * dt + rnd())));
  while (add-- > 0) {
    const size_t i = static_cast<size_t>(next_);
    next_ = (next_ + 1) % N;
    const double across = (rnd() * 2 - 1) * 1.3;
    pos[i * 3] = static_cast<float>(x + vx * across);
    pos[i * 3 + 1] = static_cast<float>(y + 2.2 + rnd() * 0.4);
    pos[i * 3 + 2] = static_cast<float>(z + vz * across);
    const double s = foam ? 0.5 : 2.2;
    vel_[i * 3] = static_cast<float>(vx * across * 0.6 + (rnd() - 0.5) * s);
    vel_[i * 3 + 1] = static_cast<float>(foam ? -0.4 - rnd() * 0.6 : -1.5 - rnd() * 2);
    vel_[i * 3 + 2] = static_cast<float>(vz * across * 0.6 + (rnd() - 0.5) * s);
    life_[i] = foam ? 1.8f : 0.9f;
    const std::array<float, 3> c = foam ? std::array<float, 3>{0.97f, 0.97f, 0.98f} : std::array<float, 3>{0.62f, 0.8f, 0.95f};
    for (int k = 0; k < 3; k++) col[i * 3 + static_cast<size_t>(k)] = c[static_cast<size_t>(k)];
  }
  int alive = 0;
  for (size_t i = 0; i < static_cast<size_t>(N); i++) {
    if (life_[i] <= 0) {
      pos[i * 3 + 1] = -1e4f;
      continue;
    }
    alive++;
    life_[i] -= static_cast<float>(dt);
    vel_[i * 3 + 1] -= static_cast<float>(9.81 * dt * 0.5);
    pos[i * 3] += static_cast<float>(vel_[i * 3] * dt);
    pos[i * 3 + 1] = static_cast<float>(std::max(y + 0.02, static_cast<double>(pos[i * 3 + 1]) + vel_[i * 3 + 1] * dt));
    pos[i * 3 + 2] += static_cast<float>(vel_[i * 3 + 2] * dt);
  }
  points->visible = alive > 0;
  pos_->needsUpdate();
  col_->needsUpdate();
}

void SprayView::stop() {
  std::fill(life_.begin(), life_.end(), 0.0f);
  points->visible = false;
}

}  // namespace fs

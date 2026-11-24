// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/cranesite.ts @ v1.7.7
#include "render/cranesite.h"

#include <array>
#include <cmath>
#include <optional>
#include <utility>

#include "core/js.h"
#include "engine/geometries.h"
#include "platform/canvas.h"
#include "render/truck.h"
#include "sim/crane.h"

namespace fs {

namespace {

/** (the original's module constants: made when first asked for; named as there, held together – fs::WHITE() … are the truck's) */
struct SiteMaterials {
  const MaterialPtr ASPHALT = lambert({.color = 0x3b3e42, .polygonOffset = true, .polygonOffsetFactor = -2, .polygonOffsetUnits = -6});
  const MaterialPtr GRAVEL = lambert({.color = 0x8d877c, .polygonOffset = true, .polygonOffsetFactor = -2, .polygonOffsetUnits = -6});
  const MaterialPtr SAND = lambert({.color = 0xa89474, .polygonOffset = true, .polygonOffsetFactor = -2, .polygonOffsetUnits = -6});
  const MaterialPtr MARK = basic({.color = 0xf2c500, .polygonOffset = true, .polygonOffsetFactor = -4, .polygonOffsetUnits = -12});
  const MaterialPtr ZONE = basic({.color = 0x38d06a, .transparent = true, .opacity = 0.38, .depthWrite = false, .polygonOffset = true, .polygonOffsetFactor = -6, .polygonOffsetUnits = -18});
  const MaterialPtr ZONE_EDGE = basic({.color = 0x2bff6a, .transparent = true, .opacity = 0.9, .depthWrite = false, .polygonOffset = true, .polygonOffsetFactor = -6, .polygonOffsetUnits = -18});
  const MaterialPtr PILLAR = basic({.color = 0x5dff8a, .transparent = true, .opacity = 0.16, .side = Side::Double, .depthWrite = false, .blending = Blending::Additive});
  const MaterialPtr AIM = basic({.color = 0xffd21a, .transparent = true, .opacity = 0.85, .depthWrite = false});
  const MaterialPtr CONCRETE = lambert({.color = 0xb9b7b1});
  const MaterialPtr MASONRY = lambert({.color = 0xe6e1d6});
  const MaterialPtr STEEL = lambert({.color = 0x8f969c});
  const MaterialPtr SCAFFOLD = lambert({.color = 0xaeb3b7});
  const MaterialPtr PLANK = lambert({.color = 0x8e6b45});
  const MaterialPtr RAIL = lambert({.color = 0xd8331f});
  const MaterialPtr WHITE = lambert({.color = 0xeceeea});
  const MaterialPtr DARK = lambert({.color = 0x24272b});
  const MaterialPtr TRUCK_CAB = lambert({.color = 0x2b6ca8});
  const MaterialPtr TYRE = lambert({.color = 0x151515});
  const MaterialPtr TIMBER = lambert({.color = 0xa0784c});
  const MaterialPtr SLING = lambert({.color = 0x3a3a3a});
  const MaterialPtr FENCE = lambert({.color = 0xc9cdd0, .transparent = true, .opacity = 0.55, .side = Side::Double, .depthWrite = false});
  const MaterialPtr PRIMER = lambert({.color = 0x8c3b2a});
};

const SiteMaterials& S() {
  static const SiteMaterials s;
  return s;
}

/** A canvas texture drawn once (bricks, corrugation, plywood …). */
TexturePtr siteCanvasTexture(int w, int h, const std::function<void(const Ctx2D&)>& draw, std::array<double, 2> repeat = {1, 1}) {
  const Canvas c(w, h);
  const Ctx2D g = c.getContext2D();
  draw(g);
  TexturePtr t = canvasTexture(c);
  t->srgb = true;
  t->wrapS = t->wrapT = Wrapping::Repeat;
  t->repeat.set(repeat[0], repeat[1]);
  t->anisotropy = 4;
  return t;
}

struct LoadTextures {
  MaterialPtr bricks, container, formwork, mesh, plywood;
};

/** The materials of the loads (their textures drawn the first time they are needed). */
const LoadTextures& loadMaterials() {
  static const LoadTextures textures = [] {
    const TexturePtr bricks = siteCanvasTexture(256, 256, [](const Ctx2D& g) {
      g.fillStyle("#b9b3a6");
      g.fillRect(0, 0, 256, 256);
      for (int r = 0; r < 8; r++) {
        for (int k = -1; k < 4; k++) {
          const int x = k * 80 + (r % 2 ? 40 : 0);
          g.fillStyle("hsl(40, 12%, " + std::to_string(80 + jsMod(r * 7 + k * 13, 9)) + "%)");
          g.fillRect(x + 3, r * 32 + 3, 74, 26);
        }
      }
    });
    const TexturePtr container = siteCanvasTexture(
        256, 128,
        [](const Ctx2D& g) {
          g.fillStyle("#245f86");
          g.fillRect(0, 0, 256, 128);
          for (int x = 0; x < 256; x += 16) {
            g.fillStyle("rgba(0,0,0,0.22)");
            g.fillRect(x, 0, 5, 128);
            g.fillStyle("rgba(255,255,255,0.12)");
            g.fillRect(x + 8, 0, 4, 128);
          }
        },
        {3, 1});
    const TexturePtr formwork = siteCanvasTexture(128, 128, [](const Ctx2D& g) {
      g.fillStyle("#d6a22a");
      g.fillRect(0, 0, 128, 128);
      for (int y = 0; y < 128; y += 21) {
        g.fillStyle("#7a5a2a");
        g.fillRect(0, y, 128, 3);
      }
    });
    const TexturePtr meshTex = siteCanvasTexture(
        128, 128,
        [](const Ctx2D& g) {
          g.clearRect(0, 0, 128, 128);
          g.strokeStyle("#6a4630");
          g.lineWidth(5);
          for (int k = 0; k <= 128; k += 21) {
            g.beginPath();
            g.moveTo(k, 0);
            g.lineTo(k, 128);
            g.moveTo(0, k);
            g.lineTo(128, k);
            g.stroke();
          }
        },
        {4, 2});
    const TexturePtr plywood = siteCanvasTexture(64, 64, [](const Ctx2D& g) {
      g.fillStyle("#c8a46c");
      g.fillRect(0, 0, 64, 64);
      g.fillStyle("rgba(120,80,40,0.25)");
      for (int y = 4; y < 64; y += 9) g.fillRect(0, y, 64, 2);
    });
    return LoadTextures{
        .bricks = lambert({.map = bricks}),
        .container = lambert({.map = container}),
        .formwork = lambert({.map = formwork}),
        .mesh = lambert({.map = meshTex, .transparent = true, .side = Side::Double, .alphaTest = 0.4}),
        .plywood = lambert({.map = plywood}),
    };
  }();
  return textures;
}

/** Boxes and their kin collected by material, merged into one mesh each (as render/crane.ts does). */
class Parts {
 public:
  void box(const MaterialPtr& mat, double x0, double x1, double y0, double y1, double z0, double z1) {
    GeometryPtr g = boxGeometry(std::abs(x1 - x0), std::abs(y1 - y0), std::abs(z1 - z0));
    g->translate((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
    push(mat, g);
  }

  void cyl(const MaterialPtr& mat, double x, double y, double z, double r, double len, char axis, int seg = 12) {
    GeometryPtr g = cylinderGeometry(r, r, len, seg, 1);
    if (axis == 'x') g->rotateZ(PI / 2);
    else if (axis == 'z') g->rotateX(PI / 2);
    g->translate(x, y, z);
    push(mat, g);
  }

  void build(Object3D& group, bool shadow = true) {
    for (const auto& [mat, list] : lists_) {
      GeometryPtr g = mergeGeometries(list, false);
      if (!g) continue;
      auto m = make<Mesh>(g, mat);
      m->castShadow = shadow && !mat->transparent;
      m->receiveShadow = true;
      group.add(m);
    }
    lists_.clear();
  }

 private:
  void push(const MaterialPtr& mat, const GeometryPtr& g) {
    // (position, normal and uv: the textured loads need their uv)
    GeometryPtr n = g->index ? g->toNonIndexed() : g;
    for (auto& [m, l] : lists_) {
      if (m == mat) {
        l.push_back(n);
        return;
      }
    }
    lists_.push_back({mat, {n}});
  }

  /** (a Map in the original: by material, in the order they came) */
  std::vector<std::pair<MaterialPtr, std::vector<GeometryPtr>>> lists_;
};

/** The mesh of a load, its bottom's middle at the origin, its length along x. */
std::shared_ptr<Group> loadMesh(const LoadDef& def, const CraneDetail& detail) {
  const SiteMaterials& k = S();
  auto g = make<Group>();
  const LoadTextures& m = loadMaterials();
  Parts p;
  const double hx = def.hx;
  const double hy = def.hy;
  const double hz = def.hz;
  const LoadKind& kind = def.kind;
  if (kind == "bricks") {
    // the pallet: boards and blocks; the bricks on it, strapped
    p.box(k.TIMBER, -hx, hx, 0, 0.03, -hz, hz);
    for (const double x : {-hx + 0.08, 0.0, hx - 0.08}) p.box(k.TIMBER, x - 0.07, x + 0.07, 0.03, 0.12, -hz, hz);
    p.box(k.TIMBER, -hx, hx, 0.12, 0.145, -hz, hz);
    p.box(m.bricks, -hx + 0.02, hx - 0.02, 0.145, 2 * hy, -hz + 0.02, hz - 0.02);
    if (detail == "full") {
      for (const double x : {-hx * 0.5, hx * 0.5}) p.box(k.DARK, x - 0.02, x + 0.02, 0.145, 2 * hy + 0.005, -hz + 0.015, hz - 0.015);
    }
  } else if (kind == "beam") {
    // an I-beam: two flanges and the web, in red primer
    const double fl = 0.019;
    p.box(k.PRIMER, -hx, hx, 0, fl, -hz, hz);
    p.box(k.PRIMER, -hx, hx, 2 * hy - fl, 2 * hy, -hz, hz);
    p.box(k.PRIMER, -hx, hx, fl, 2 * hy - fl, -0.0055, 0.0055);
  } else if (kind == "container") {
    p.box(m.container, -hx, hx, 0.1, 2 * hy - 0.1, -hz, hz);
    p.box(k.DARK, -hx, hx, 0, 0.12, -hz, hz);
    p.box(k.DARK, -hx, hx, 2 * hy - 0.1, 2 * hy, -hz, hz);
    for (const double x : {-hx, hx})
      for (const double z : {-hz, hz}) p.box(k.DARK, x - 0.08, x + 0.08, 0, 2 * hy, z - 0.08, z + 0.08);
    if (detail == "full") {
      // the doors at one end, their locking bars
      p.box(k.DARK, hx - 0.005, hx + 0.01, 0.15, 2 * hy - 0.15, -0.01, 0.01);
      for (const double z : {-0.6, -0.35, 0.35, 0.6}) p.box(k.STEEL, hx + 0.01, hx + 0.04, 0.2, 2 * hy - 0.2, z - 0.02, z + 0.02);
    }
  } else if (kind == "formwork") {
    p.box(m.formwork, -hx, hx, 0.1, 2 * hy, -hz, hz);
    p.box(m.plywood, -hx + 0.03, hx - 0.03, 2 * hy, 2 * hy + 0.005, -hz + 0.03, hz - 0.03);
    for (const double x : {-hx + 0.3, hx - 0.3}) p.box(k.TIMBER, x - 0.06, x + 0.06, 0, 0.1, -hz, hz);
  } else {
    // reinforcing mesh: a bundle of mats, seen through
    for (int i = 0; i < (detail == "full" ? 6 : 3); i++) {
      const double y = 0.04 + (i * (2 * hy - 0.08)) / 5;
      p.box(m.mesh, -hx, hx, y, y + 0.012, -hz, hz);
    }
    for (const double x : {-hx + 0.4, 0.0, hx - 0.4}) p.box(k.STEEL, x - 0.01, x + 0.01, 0, 2 * hy, -hz, hz);
  }
  p.build(*g);
  return g;
}

}  // namespace

namespace cranesite_detail {

/** A rope (a sling's leg): a unit cylinder stretched between two points each picture. */
class Leg {
 public:
  std::shared_ptr<Mesh> mesh;

  explicit Leg(Object3D& parent) {
    mesh = make<Mesh>(geo(), S().SLING);
    mesh->visible = false;
    parent.add(mesh);
  }

  void set(const Vector3& a, const Vector3& b) {
    d_.subVectors(b, a);
    const double len = d_.length();
    mesh->visible = len > 0.01;
    if (!mesh->visible) return;
    mesh->position.copy(a);
    mesh->quaternion.setFromUnitVectors(Vector3(0, 1, 0), d_.divideScalar(len));
    mesh->scale.set(0.018, len, 0.018);
  }

 private:
  static const GeometryPtr& geo() {
    static const GeometryPtr g = cylinderGeometry(1, 1, 1, 5, 1)->translate(0, 0.5, 0).shared_from_this();
    return g;
  }
  Vector3 d_;
};

}  // namespace cranesite_detail

using cranesite_detail::Leg;

CraneSiteScene::CraneSiteScene() : aim(make<Mesh>(coneGeometry(0.45, 0.9, 12)->rotateX(PI).shared_from_this(), S().AIM)) {
  group->name = "crane site";
  group->visible = false;
  aim->visible = false;
  group->add(aim);
}

CraneSiteScene::~CraneSiteScene() = default;

bool CraneSiteScene::show(const SitePlacement& p, const std::function<double(double, double)>& heightAt, double y0, bool towerToo, const CraneDetail& detail) {
  const SiteMaterials& k = S();
  clear();
  this->detail = detail;
  this->heightAt = [heightAt, y0](double x, double z) {
    const double h = heightAt(x, z);
    return std::isnan(h) ? y0 : h;
  };
  Group& g = *group;
  bool complete = true;
  const auto yAt = [&](double u, double w) {
    const auto [x, z] = p.toWorld(u, w);
    const double h = heightAt(x, z);
    if (std::isnan(h)) {
      complete = false;
      return y0;
    }
    return h;
  };
  const auto drape = [&](const SiteRect& r, double lift, const MaterialPtr& mat, double cell = 3) {
    const int nu = static_cast<int>(jsMax(1, std::ceil(std::abs(r.u1 - r.u0) / cell)));
    const int nw = static_cast<int>(jsMax(1, std::ceil(std::abs(r.w1 - r.w0) / cell)));
    std::vector<float> pos(static_cast<size_t>((nu + 1) * (nw + 1) * 3));
    for (int j = 0; j <= nw; j++) {
      for (int i = 0; i <= nu; i++) {
        const double u = r.u0 + ((r.u1 - r.u0) * i) / nu;
        const double w = r.w0 + ((r.w1 - r.w0) * j) / nw;
        const auto [x, z] = p.toWorld(u, w);
        const size_t kk = static_cast<size_t>((j * (nu + 1) + i) * 3);
        pos[kk] = static_cast<float>(x);
        pos[kk + 1] = static_cast<float>(yAt(u, w) + lift);
        pos[kk + 2] = static_cast<float>(z);
      }
    }
    std::vector<uint32_t> index;
    for (int j = 0; j < nw; j++) {
      for (int i = 0; i < nu; i++) {
        const uint32_t a = static_cast<uint32_t>(j * (nu + 1) + i);
        const uint32_t n1 = static_cast<uint32_t>(nu + 1);
        index.insert(index.end(), {a, a + n1, a + 1, a + 1, a + n1, a + n1 + 1});
      }
    }
    auto geo = make<BufferGeometry>();
    geo->setAttribute("position", attr(std::move(pos), 3));
    geo->setIndex(index);
    geo->computeVertexNormals();
    // (whichever way the yard is turned, the ground must face up)
    if (geo->getAttribute("normal")->getY(0) < 0) {
      std::vector<uint32_t> flipped(index.size());
      static const int order[3] = {0, 2, 1};
      for (size_t kk = 0; kk < index.size(); kk++) flipped[kk] = index[kk - (kk % 3) + order[kk % 3]];
      geo->setIndex(flipped);
      geo->computeVertexNormals();
    }
    made.push_back(geo);
    auto mesh = make<Mesh>(geo, mat);
    mesh->receiveShadow = true;
    g.add(mesh);
  };
  /** a painted line round a rectangle */
  const auto outline = [&](const SiteRect& r, double width, const MaterialPtr& mat) {
    drape({.u0 = r.u0, .u1 = r.u1, .w0 = r.w0, .w1 = r.w0 + width}, 0.08, mat, 4);
    drape({.u0 = r.u0, .u1 = r.u1, .w0 = r.w1 - width, .w1 = r.w1}, 0.08, mat, 4);
    drape({.u0 = r.u0, .u1 = r.u0 + width, .w0 = r.w0, .w1 = r.w1}, 0.08, mat, 4);
    drape({.u0 = r.u1 - width, .u1 = r.u1, .w0 = r.w0, .w1 = r.w1}, 0.08, mat, 4);
  };
  // ---- the ground: the depot, the site road, the storage areas, the site itself, the stands
  drape(AREAS.site, 0.04, k.SAND);
  drape(AREAS.storage, 0.05, k.GRAVEL);
  drape(AREAS.west, 0.05, k.GRAVEL);
  drape(AREAS.depot, 0.05, k.GRAVEL);
  drape(AREAS.road, 0.06, k.ASPHALT);
  // (Object.values(STANDS): in the order they were written)
  for (const char* key : {"mobile", "loader"}) {
    const Stand& s = STANDS.at(key);
    outline(s.area, 0.2, k.MARK);
    // (hatched at the front: where the vehicle's nose goes)
    for (int i = 0; i < 4; i++) {
      const double u = s.area.u1 - 0.6 - i * 0.9;
      drape({.u0 = u - 0.25, .u1 = u, .w0 = s.area.w0 + 0.3, .w1 = s.area.w1 - 0.3}, 0.08, k.MARK, 4);
    }
  }
  // ---- what stands on it, in the world's frame: one group, turned like the yard
  auto site = make<Group>();
  const auto [ox, oz] = p.toWorld(0, 0);
  site->position.set(ox, y0, oz);
  site->rotation.y = p.yawToWorld(0);
  g.add(site);
  // (the site's own frame in that group: x = u, z = w, y above the base height)
  Parts b;
  const Building& B = BUILDING;
  const bool full = detail == "full";
  // the plinth and the slabs, the columns of each storey
  b.box(k.CONCRETE, B.u0 - 0.2, B.u1 + 0.2, -0.8, B.floors[0], B.w0 - 0.2, B.w1 + 0.2);
  for (size_t f = 1; f < B.floors.size(); f++) b.box(k.CONCRETE, B.u0, B.u1, B.floors[f] - B.slab, B.floors[f], B.w0, B.w1);
  const std::array<double, 5> cols = {B.u0 + 0.2, B.u0 + 4.1, B.u0 + 8.0, B.u0 + 11.9, B.u1 - 0.2};
  const std::array<double, 4> rows = {B.w0 + 0.2, B.w0 + 4.1, B.w1 - 4.1, B.w1 - 0.2};
  for (size_t f = 0; f + 1 < B.floors.size(); f++) {
    for (const double u : cols)
      for (const double w : rows) b.box(k.CONCRETE, u - 0.15, u + 0.15, B.floors[f], B.floors[f + 1] - B.slab, w - 0.15, w + 0.15);
  }
  // the walls of the ground and first floor (masonry, with window openings), the second floor still open
  for (int f = 0; f < 2; f++) {
    const double y0w = B.floors[f];
    const double y1w = B.floors[f + 1] - B.slab;
    // north and south faces: wall pieces between the windows
    for (const double w : {B.w0 + 0.05, B.w1 - 0.05}) {
      for (double u = B.u0 + 0.3; u < B.u1 - 0.3; u += 3.9) {
        b.box(k.MASONRY, u, u + 1.2, y0w, y1w, w - 0.12, w + 0.12);
        b.box(k.MASONRY, u + 1.2, jsMin(u + 3.9, B.u1 - 0.3), y0w, y0w + 0.9, w - 0.12, w + 0.12);
        b.box(k.MASONRY, u + 1.2, jsMin(u + 3.9, B.u1 - 0.3), y1w - 0.45, y1w, w - 0.12, w + 0.12);
      }
    }
    if (f == 0) b.box(k.MASONRY, B.u0 - 0.07, B.u0 + 0.17, y0w, y1w, B.w0 + 0.3, B.w1 - 0.3);
  }
  // the stair core, a storey above the roof
  b.box(k.CONCRETE, B.core.u0, B.core.u1, B.floors[0], B.core.top, B.core.w0, B.core.w1);
  // the loading platform at the second floor: its deck, its girders, its side rails
  const SitePlatform& P = B.platform;
  b.box(k.STEEL, P.u0, P.u1, P.y - 0.18, P.y, P.w0, P.w1 + 0.5);
  b.box(k.PLANK, P.u0 + 0.05, P.u1 - 0.05, P.y, P.y + 0.03, P.w0 + 0.05, P.w1);
  for (const double u : {P.u0, P.u1}) {
    b.box(k.RAIL, u - 0.04, u + 0.04, P.y, P.y + 1.1, P.w0, P.w1);
    b.box(k.STEEL, u - 0.05, u + 0.05, P.y - 1.4, P.y - 0.18, P.w1 - 0.1, P.w1 + 0.1);
  }
  // the guard rails round the roof
  for (const SiteRect& r : B.rails) {
    b.box(k.RAIL, r.u0, r.u1, B.railTop - 0.06, B.railTop, r.w0, r.w1);
    b.box(k.RAIL, r.u0, r.u1, (B.floors[3] + B.railTop) / 2 - 0.03, (B.floors[3] + B.railTop) / 2 + 0.03, r.w0, r.w1);
    const bool along = r.u1 - r.u0 > r.w1 - r.w0;
    const double len = along ? r.u1 - r.u0 : r.w1 - r.w0;
    for (double kk = 0; kk <= len; kk += 2) {
      const double u = along ? r.u0 + kk : (r.u0 + r.u1) / 2;
      const double w = along ? (r.w0 + r.w1) / 2 : r.w0 + kk;
      b.box(k.RAIL, u - 0.03, u + 0.03, B.floors[3], B.railTop, w - 0.03, w + 0.03);
    }
  }
  // the scaffolding: standards, ledgers, decks, toe boards, the diagonals
  for (const SiteRect& s : B.scaffold) {
    const bool along = s.u1 - s.u0 > s.w1 - s.w0;
    const double len = along ? s.u1 - s.u0 : s.w1 - s.w0;
    const int bays = static_cast<int>(jsMax(1, jsRound(len / 2.5)));
    for (int kk = 0; kk <= bays; kk++) {
      const double t = static_cast<double>(kk) / bays;
      for (const int side : {0, 1}) {
        const double u = along ? s.u0 + t * (s.u1 - s.u0) : side ? s.u1 - 0.05 : s.u0 + 0.05;
        const double w = along ? (side ? s.w1 - 0.05 : s.w0 + 0.05) : s.w0 + t * (s.w1 - s.w0);
        b.box(k.SCAFFOLD, u - 0.025, u + 0.025, 0.3, B.scaffoldTop, w - 0.025, w + 0.025);
      }
    }
    for (double y = 2.3; y < B.scaffoldTop; y += 2) {
      b.box(k.PLANK, s.u0, s.u1, y - 0.04, y, s.w0, s.w1);
      b.box(k.SCAFFOLD, s.u0, s.u1, y + 0.45, y + 0.5, along ? s.w1 - 0.07 : s.w0, along ? s.w1 - 0.02 : s.w1);
      b.box(k.SCAFFOLD, s.u0, s.u1, y + 0.95, y + 1.0, along ? s.w1 - 0.07 : s.w0, along ? s.w1 - 0.02 : s.w1);
      if (full) b.box(k.PLANK, s.u0, s.u1, y, y + 0.15, along ? s.w1 - 0.05 : s.w0, along ? s.w1 - 0.03 : s.w1);
    }
    if (full) {
      // a diagonal brace in every other bay of the outer face
      for (int kk = 0; kk < bays; kk += 2) {
        const double t0 = static_cast<double>(kk) / bays;
        const double t1 = static_cast<double>(kk + 1) / bays;
        const int n = 8;
        for (int i = 0; i < n; i++) {
          const double a = t0 + ((t1 - t0) * i) / n;
          const double y = 0.3 + ((B.scaffoldTop - 0.3) * i) / n;
          const double u = along ? s.u0 + a * (s.u1 - s.u0) : s.u1 - 0.02;
          const double w = along ? s.w1 - 0.02 : s.w0 + a * (s.w1 - s.w0);
          b.box(k.SCAFFOLD, u - 0.03, u + 0.03, y, y + (B.scaffoldTop - 0.3) / n, w - 0.03, w + 0.03);
        }
      }
    }
  }
  // the delivery truck: cab, chassis, wheels, its flatbed
  const Delivery& D = DELIVERY;
  b.box(k.TRUCK_CAB, D.cab.u0, D.cab.u1, D.cab.y0 + 0.9, D.cab.y1, D.cab.w0, D.cab.w1);
  b.box(k.DARK, D.cab.u0, D.cab.u1, 0.45, 0.95, D.cab.w0 + 0.05, D.cab.w1 - 0.05);
  b.box(GLASS(), D.cab.u1 - 0.01, D.cab.u1 + 0.01, 2.0, 2.9, D.cab.w0 + 0.1, D.cab.w1 - 0.1);
  b.box(k.DARK, D.chassis.u0, D.chassis.u1, 0.75, D.bed.y - 0.15, D.chassis.w0, D.chassis.w1);
  b.box(k.PLANK, D.bed.u0, D.bed.u1, D.bed.y - 0.15, D.bed.y, D.bed.w0, D.bed.w1);
  for (const double u : {6.4, 7.8, 12.9})
    for (const double w : {D.bed.w0 + 0.3, D.bed.w1 - 0.3}) b.cyl(k.TYRE, u, 0.5, w, 0.5, 0.4, 'z', 16);
  // the office and the store at the depot, the site's sign
  for (const SiteSolid& o : OFFICES) {
    b.box(k.WHITE, o.u0, o.u1, o.y0, o.y1, o.w0, o.w1);
    b.box(k.DARK, o.u0 + 0.8, o.u0 + 1.7, 0.2, 2.2, o.w1, o.w1 + 0.02);
    b.box(GLASS(), o.u0 + 2.5, o.u1 - 0.6, 1.0, 2.0, o.w1 + 0.005, o.w1 + 0.015);
  }
  // the fence round the site (gaps where the road comes in)
  if (full) {
    const auto fence = [&](double u0, double w0, double u1, double w1) {
      const double len = std::hypot(u1 - u0, w1 - w0);
      const int n = static_cast<int>(jsMax(1, jsRound(len / 3.5)));
      for (int i = 0; i < n; i++) {
        const double a = static_cast<double>(i) / n;
        const double c = static_cast<double>(i + 1) / n;
        const double pu0 = u0 + (u1 - u0) * a;
        const double pw0 = w0 + (w1 - w0) * a;
        const double pu1 = u0 + (u1 - u0) * c;
        const double pw1 = w0 + (w1 - w0) * c;
        b.box(k.FENCE, jsMin(pu0, pu1), jsMax(pu0, pu1) + 0.02, 0.15, 2.0, jsMin(pw0, pw1), jsMax(pw0, pw1) + 0.02);
        b.box(k.CONCRETE, pu0 - 0.3, pu0 + 0.3, 0, 0.15, pw0 - 0.12, pw0 + 0.12);
      }
    };
    fence(-47, -27.5, 46, -27.5);
    fence(-47, 27, 46, 27);
    fence(46, -27.5, 46, -24.5);
    fence(46, -16.5, 46, 27);
    fence(-47, -16.5, -47, 27);
  }
  b.build(*site);
  // the site's sign at the depot
  auto sign = lettering("Baustelle · Kranhof", 4.2, 0.7);
  sign->position.set(-50.5, 2.3, -8.2);
  site->add(sign);
  auto post = make<Mesh>(boxGeometry(0.1, 2.4, 0.1), k.STEEL);
  post->position.set(-50.5, 1.2, -8.25);
  site->add(post);
  // ---- the tower crane, when it is not the player's own
  if (towerToo) {
    auto t = std::make_unique<TowerCraneModel>(detail);
    const auto [tx, tz] = p.toWorld(TOWER_SITE.u, TOWER_SITE.w);
    Crane sim("tower");
    sim.place(tx, y0, tz, p.yawToWorld(TOWER_SITE.yaw));
    // (out of service: turned with the wind, the hook up)
    sim.q.slew = 2.3;
    sim.q.trolley = 9;
    sim.q.rope = 3;
    sim.place(tx, y0, tz, p.yawToWorld(TOWER_SITE.yaw));
    t->root->position.set(tx, y0, tz);
    t->root->rotation.y = p.yawToWorld(TOWER_SITE.yaw);
    t->root->updateMatrixWorld(true);
    t->pose(sim, 0);
    g.add(t->root);
    tower = std::move(t);
  }
  g.visible = true;
  return complete;
}

void CraneSiteScene::tick(double now) {
  if (tower) tower->flash(now);
}

void CraneSiteScene::update(const std::vector<WorldLoad>& loads, const Vector3* hook, const CraneSiteTarget* target, double now) {
  for (const WorldLoad& l : loads) {
    auto found = this->loads.find(l.id);
    if (found == this->loads.end()) {
      DrawnLoad d;
      d.group = make<Group>();
      d.mesh = loadMesh(l.def, detail);
      d.bearers = make<Group>();
      Parts bp;
      const double x0 = l.def.hx > 1.5 ? -l.def.hx + 0.8 : -l.def.hx + 0.2;
      const double x1 = l.def.hx > 1.5 ? l.def.hx - 0.8 : l.def.hx - 0.2;
      for (const double x : {x0, x1}) bp.box(S().TIMBER, x - 0.06, x + 0.06, -0.12, 0, -l.def.hz - 0.15, l.def.hz + 0.15);
      bp.build(*d.bearers);
      d.group->add(d.mesh, d.bearers);
      group->add(d.group);
      for (int i = 0; i < 4; i++) d.legs.push_back(std::make_unique<Leg>(*group));
      found = this->loads.emplace(l.id, std::move(d)).first;
    }
    DrawnLoad& d = found->second;
    d.group->position.set(l.x, l.y, l.z);
    d.group->rotation.y = l.yaw;
    d.bearers->visible = l.bearers && !l.hooked;
    // the slings from the hook's bowl to the load's corners (two legs to a beam)
    auto& legs = d.legs;
    if (l.hooked && hook) {
      const LoadDef& def = l.def;
      const double c = std::cos(l.yaw);
      const double s = std::sin(l.yaw);
      const double top = l.y + 2 * def.hy;
      const std::vector<std::array<double, 2>> pts =
          def.kind == "beam" ? std::vector<std::array<double, 2>>{{-def.hx * 0.6, 0}, {def.hx * 0.6, 0}}
                             : std::vector<std::array<double, 2>>{{-def.hx * 0.85, -def.hz * 0.85}, {def.hx * 0.85, -def.hz * 0.85}, {def.hx * 0.85, def.hz * 0.85}, {-def.hx * 0.85, def.hz * 0.85}};
      for (size_t i = 0; i < legs.size(); i++) {
        Leg& leg = *legs[i];
        if (i >= pts.size()) {
          leg.mesh->visible = false;
          continue;
        }
        const auto& q = pts[i];
        // (local x along (cos, −sin) of the world, z along (sin, cos))
        tmpA.set(l.x + q[0] * c + q[1] * s, top, l.z - q[0] * s + q[1] * c);
        leg.set(*hook, tmpA);
      }
    } else
      for (const auto& leg : legs) leg->mesh->visible = false;
  }
  // the load of the task now: an arrow over it – the place it goes: lit up
  if (target) {
    const WorldLoad* l = nullptr;
    for (const WorldLoad& x : loads)
      if (x.id == target->load) {
        l = &x;
        break;
      }
    aim->visible = l && !l->hooked;
    if (l) aim->position.set(l->x, l->y + 2 * l->def.hy + 1.6 + 0.25 * std::sin(now * 3), l->z);
    showZone(&target->zone, l ? &l->def : nullptr);
  } else {
    aim->visible = false;
    showZone(nullptr, nullptr);
  }
}

void CraneSiteScene::showZone(const WorldZone* zone, const LoadDef* def) {
  const SiteMaterials& k = S();
  const std::string key = zone ? zone->id : "";
  if (key == zoneFor) {
    // (the same place – one on the loader crane's bed goes where the truck goes)
    if (zone && zoneMesh) {
      zoneMesh->position.set(zone->x, std::isnan(zone->y) ? heightAt(zone->x, zone->z) : zone->y, zone->z);
      zoneMesh->rotation.y = zone->yaw;
    }
    return;
  }
  zoneFor = key;
  if (zoneMesh) {
    group->remove(zoneMesh);
    zoneMesh->traverse([](Object3D& o) {
      if (o.type == ObjectType::Mesh) static_cast<Mesh&>(o).geometry->dispose();
    });
    zoneMesh = nullptr;
  }
  if (!zone || !def) return;
  auto g = make<Group>();
  // (the load's footprint, as large as the zone lets its middle wander)
  const auto [fx, fz] = loadFootprint(*def, 0);
  const double w = 2 * (fx + zone->hu);
  const double d = 2 * (fz + zone->hw);
  const double y = std::isnan(zone->y) ? heightAt(zone->x, zone->z) : zone->y;
  auto plate = make<Mesh>(boxGeometry(w, 0.03, d), k.ZONE);
  plate->position.y = 0.04;
  g->add(plate);
  const double e = 0.08;
  for (const auto& [px, pz, sx, sz] : std::array<std::array<double, 4>, 4>{{{0, -d / 2, w, e}, {0, d / 2, w, e}, {-w / 2, 0, e, d}, {w / 2, 0, e, d}}}) {
    auto edge = make<Mesh>(boxGeometry(sx, 0.05, sz), k.ZONE_EDGE);
    edge->position.set(px, 0.06, pz);
    g->add(edge);
  }
  auto pillar = make<Mesh>(cylinderGeometry(0.5, 0.5, 30, 12, 1, true), k.PILLAR);
  pillar->position.y = 15;
  g->add(pillar);
  g->position.set(zone->x, y, zone->z);
  g->rotation.y = zone->yaw;
  zoneMesh = g;
  group->add(g);
}

void CraneSiteScene::addBearers(const std::vector<CraneBoxY>& list) {
  Parts p;
  for (const CraneBoxY& b : list) p.box(S().TIMBER, b.x0, b.x1, b.y - 0.12, b.y, b.z0, b.z1);
  p.build(*group);
}

void CraneSiteScene::clear() {
  group->clear();
  group->add(aim);
  for (const GeometryPtr& geo : made) geo->dispose();
  made.clear();
  loads.clear();
  zoneMesh = nullptr;
  zoneFor = "";
  tower = nullptr;
}

void CraneSiteScene::hide() {
  clear();
  group->visible = false;
}

}  // namespace fs

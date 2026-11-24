// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/caropen.ts @ v1.7.7
#include "render/caropen.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

#include "core/js.h"
#include "engine/geometries.h"
#include "render/car.h"
#include "render/mirrors.h"

namespace fs {

namespace {

/** the parts, in the order of game/cardoors.ts CAR_OPENINGS */
constexpr int FL = 0;
constexpr int FR = 1;
constexpr int RL = 2;
constexpr int RR = 3;
constexpr int BONNET = 4;
constexpr int BOOT = 5;
constexpr int PARTS = 6;

/** how far each part opens (radians) */
const double DOOR_ANGLE = (65 * PI) / 180;
const double BONNET_ANGLE = (52 * PI) / 180;
const double BOOT_ANGLE = (78 * PI) / 180;

/** Linear through points [t, value] sorted by t (held at the ends). */
double along(const std::vector<P2>& pts, double t) {
  if (t <= pts[0][0]) return pts[0][1];
  for (size_t i = 1; i < pts.size(); i++) {
    const double t1 = pts[i][0], v1 = pts[i][1];
    if (t <= t1) {
      const double t0 = pts[i - 1][0], v0 = pts[i - 1][1];
      const double dt = t1 - t0;
      return v0 + ((v1 - v0) * (t - t0)) / (dt != 0 ? dt : 1);
    }
  }
  return pts.back()[1];
}

using Profile = std::vector<std::array<double, 2>>;

/** Distances from (|x|, y) to the six segments of the body's outline at z: 0 floor … 4 side window, 5 roof/bonnet/boot. */
std::vector<double>& segmentDistances(const Profile& profile, double ax, double y, std::vector<double>& out) {
  for (size_t k = 0; k < 6; k++) {
    const double x0 = profile[k][0], y0 = profile[k][1];
    const double x1 = profile[k + 1][0], y1 = profile[k + 1][1];
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    const double l2 = dx * dx + dy * dy;
    const double t = l2 > 0 ? jsMax(0, jsMin(1, ((ax - x0) * dx + (y - y0) * dy) / l2)) : 0;
    out[k] = std::hypot(ax - (x0 + dx * t), y - (y0 + dy * t));
  }
  return out;
}

/** How far (|x|, y) lies outside segment k of the outline (negative: inside). */
double outside(const Profile& profile, int k, double ax, double y) {
  const double x0 = profile[static_cast<size_t>(k)][0], y0 = profile[static_cast<size_t>(k)][1];
  const double x1 = profile[static_cast<size_t>(k) + 1][0], y1 = profile[static_cast<size_t>(k) + 1][1];
  // (the outline runs counter-clockwise: outwards is to the right of it)
  double l = std::hypot(x1 - x0, y1 - y0);
  if (l == 0) l = 1;
  return ((ax - x0) * (y1 - y0) - (y - y0) * (x1 - x0)) / l;
}

int nearest(const std::vector<double>& ds) {
  int k = 0;
  for (size_t i = 1; i < ds.size(); i++)
    if (ds[i] < ds[static_cast<size_t>(k)]) k = static_cast<int>(i);
  return k;
}

/** The planes the shell is cut along: n·p = d, applied to triangles that reach into `box`. */
struct Cut {
  std::array<double, 3> n;
  double d;
  bool hasBox;
  std::array<double, 6> box;
};

std::vector<Cut> shellCuts(const PanelLines& L) {
  std::vector<Cut> cuts;
  const auto zPlane = [&](double z) { cuts.push_back({{0, 0, 1}, z, false, {}}); };
  for (const double z : {L.zFD, L.zB, L.zRD, L.zRW, L.bz0, L.bz1, L.tz0}) zPlane(z);
  cuts.push_back({{0, 1, 0}, L.sill, true, {-9, 0, L.zFD - 0.3, 9, 1, L.zRD + 0.3}});
  cuts.push_back({{0, 1, 0}, L.tailSill, true, {-0.6, 0.6, L.zRear - 0.02, 0.6, 0.9, L.zRear + 0.02}});
  // round the wheel arch: planes across the car through each piece of the edge (in y and z)
  for (size_t i = 0; i + 1 < L.arch.size(); i++) {
    const double y0 = L.arch[i][0], z0 = L.arch[i][1];
    const double y1 = L.arch[i + 1][0], z1 = L.arch[i + 1][1];
    std::array<double, 3> n = {0, z1 - z0, -(y1 - y0)};
    const double l = std::hypot(n[1], n[2]);
    n[1] /= l;
    n[2] /= l;
    cuts.push_back({n, n[1] * y0 + n[2] * z0, true, {-9, y0 - 0.05, jsMin(z0, z1) - 0.05, 9, jsMax(y1, 0.66) + 0.05, jsMax(z0, z1) + 0.05}});
  }
  // the edges of the bonnet and of the tailgate: upright planes through each piece, on both sides
  const auto edge = [&](const std::vector<P2>& pts, double yLow) {
    for (size_t i = 0; i + 1 < pts.size(); i++) {
      const double za = pts[i][0], xa = pts[i][1];
      const double zb = pts[i + 1][0], xb = pts[i + 1][1];
      for (const double s : {-1.0, 1.0}) {
        std::array<double, 3> n = {zb - za, 0, -(s * xb - s * xa)};
        const double l = std::hypot(n[0], n[2]);
        n[0] /= l;
        n[2] /= l;
        const double x0 = jsMin(s * xa, s * xb);
        const double x1 = jsMax(s * xa, s * xb);
        cuts.push_back({n, n[0] * s * xa + n[2] * za, true, {x0 - 0.05, yLow, jsMin(za, zb) - 0.01, x1 + 0.05, 3, jsMax(za, zb) + 0.01}});
      }
    }
  };
  edge(L.bonnetEdge, L.belt - 0.35);
  edge(L.tailEdge, 0.6);
  for (const double s : {-1.0, 1.0}) cuts.push_back({{1, 0, 0}, s * 0.385, true, {s * 0.385 - 0.01, L.tailSill - 0.01, L.zRear - 0.02, s * 0.385 + 0.01, 0.9, L.zRear + 0.02}});
  return cuts;
}

}  // namespace

PanelLines panelLines(const CarShell& sh, const CarDims& d) {
  const auto at = bodyProfile(d);
  const double zRD = sh.zAxleR - 0.255;
  const double bz0 = sh.zFront + 0.105;
  const double bz1 = sh.zCowl - 0.09;
  // (the same points the gap is drawn through)
  std::vector<P2> bonnetEdge;
  for (double z = bz1; z > sh.zFront + 0.1; z -= 0.08) bonnetEdge.push_back({z, at(z)[5][0] - 0.05});
  bonnetEdge.push_back({bz0, at(bz0)[5][0] - 0.05});
  std::reverse(bonnetEdge.begin(), bonnetEdge.end());
  const double zt = sh.zTail;
  PanelLines L;
  L.zFD = sh.zCowl + 0.105;
  L.zB = sh.zPillarB;
  L.zRD = zRD;
  L.zRW = sh.zRoofRear - 0.17;
  L.sill = 0.37;
  L.arch = {{0.37, zRD - 0.16}, {0.42, zRD - 0.13}, {0.55, zRD - 0.07}, {0.63, zRD}};
  L.bz0 = bz0;
  L.bz1 = bz1;
  L.bonnetEdge = bonnetEdge;
  L.tz0 = sh.zRoofRear + 0.01;
  L.tailEdge = {{zt, at(zt)[5][0]}, {zt + 0.07, 0.4}, {sh.zRear - 0.06, 0.385}, {sh.zRear + 0.01, 0.385}};
  L.tailSill = 0.745;
  L.zTail = zt;
  L.zRear = sh.zRear;
  L.belt = sh.beltY;
  return L;
}

int shellPart(const PanelLines& L, double x, double y, double z, int k, bool cap) {
  const double ax = std::fabs(x);
  const int s = x < 0 ? 0 : 1;
  if (cap) return z > L.zRear - 0.006 && y >= L.tailSill && ax <= 0.385 ? BOOT : -1;
  if (k >= 1 && k <= 4 && y >= L.sill) {
    if (z >= L.zFD && z <= L.zB) return FL + s;
    if (z > L.zB && (k == 4 ? z <= L.zRW : z <= along(L.arch, y))) return RL + s;
  }
  if (k == 5) {
    if (z >= L.bz0 && z <= L.bz1 && ax <= along(L.bonnetEdge, z)) return BONNET;
    if (z >= L.tz0 && (z <= L.zTail || ax <= along(L.tailEdge, z))) return BOOT;
  }
  return -1;
}

// ----------------------------------------------------------------- triangle soup ----

/** The triangles of a geometry with every attribute, to be cut and sorted. */
class Soup {
 public:
  std::vector<std::string> names;
  std::vector<int> sizes;
  std::vector<std::vector<double>> data;
  std::vector<int> tris;
  /** vertices made where a triangle was cut (for mending the edges of its neighbours) */
  std::vector<int> made;

  explicit Soup(const BufferGeometry& g) {
    for (const auto& [name, a] : g.attributes) {
      if (!a || a->divisor != 0) continue;
      names.push_back(name);
      sizes.push_back(a->itemSize);
      std::vector<double> out;
      const size_t n = a->count();
      for (size_t i = 0; i < n; i++)
        for (int c = 0; c < a->itemSize; c++) out.push_back(a->at(i * static_cast<size_t>(a->itemSize) + static_cast<size_t>(c)));
      data.push_back(std::move(out));
    }
    const AttrPtr& idx = g.index;
    const size_t n = idx ? idx->count() : g.getAttribute("position")->count();
    for (size_t i = 0; i < n; i++) tris.push_back(idx ? static_cast<int>(idx->getX(i)) : static_cast<int>(i));
  }

  int posIndex() const { return static_cast<int>(std::find(names.begin(), names.end(), "position") - names.begin()); }

  std::array<double, 3> pos(int i) const {
    const std::vector<double>& d = data[static_cast<size_t>(posIndex())];
    const size_t k = static_cast<size_t>(i) * 3;
    return {d[k], d[k + 1], d[k + 2]};
  }

  /** A new vertex between i and j (every attribute interpolated: normals stay as the surface had them). */
  int lerp(int i, int j, double t) {
    const int id = static_cast<int>(data[0].size() / static_cast<size_t>(sizes[0]));
    for (size_t a = 0; a < data.size(); a++) {
      std::vector<double>& d = data[a];
      const size_t s = static_cast<size_t>(sizes[a]);
      for (size_t c = 0; c < s; c++) d.push_back(d[static_cast<size_t>(i) * s + c] + (d[static_cast<size_t>(j) * s + c] - d[static_cast<size_t>(i) * s + c]) * t);
    }
    made.push_back(id);
    return id;
  }

  /** The geometry of the triangles `list` (indices into tris / 3). */
  GeometryPtr build(const std::vector<int>& list) const {
    const GeometryPtr g = make<BufferGeometry>();
    std::unordered_map<int, uint32_t> remap;
    std::vector<uint32_t> index;
    std::vector<std::vector<float>> out(data.size());
    for (const int t : list) {
      for (int c = 0; c < 3; c++) {
        const int v = tris[static_cast<size_t>(t * 3 + c)];
        auto it = remap.find(v);
        uint32_t r;
        if (it == remap.end()) {
          r = static_cast<uint32_t>(remap.size());
          remap.emplace(v, r);
          for (size_t a = 0; a < data.size(); a++) {
            const size_t s = static_cast<size_t>(sizes[a]);
            for (size_t k = 0; k < s; k++) out[a].push_back(static_cast<float>(data[a][static_cast<size_t>(v) * s + k]));
          }
        } else r = it->second;
        index.push_back(r);
      }
    }
    for (size_t a = 0; a < names.size(); a++) g->setAttribute(names[a], attr(std::move(out[a]), sizes[a]));
    g->setIndex(index);
    g->computeBoundingSphere();
    return g;
  }
};

namespace {

/** Cut every triangle of the soup that `cut` passes through (and that reaches into its box) in two along it. */
void cutSoup(Soup& s, const Cut& cut) {
  const size_t pi = static_cast<size_t>(s.posIndex());
  const double nx = cut.n[0], ny = cut.n[1], nz = cut.n[2];
  std::vector<int> out;
  const std::vector<int> tris = s.tris;
  int v[3];
  double d[3];
  for (size_t t = 0; t < tris.size(); t += 3) {
    const int i0 = tris[t];
    const int i1 = tris[t + 1];
    const int i2 = tris[t + 2];
    // (the positions may move as vertices are added: read them afresh)
    const std::vector<double>& P = s.data[pi];
    if (cut.hasBox) {
      bool inBox = true;
      for (size_t c = 0; c < 3 && inBox; c++) {
        const double p0 = P[static_cast<size_t>(i0) * 3 + c];
        const double p1 = P[static_cast<size_t>(i1) * 3 + c];
        const double p2 = P[static_cast<size_t>(i2) * 3 + c];
        if (jsMax(jsMax(p0, p1), p2) < cut.box[c] || jsMin(jsMin(p0, p1), p2) > cut.box[c + 3]) inBox = false;
      }
      if (!inBox) {
        out.insert(out.end(), {i0, i1, i2});
        continue;
      }
    }
    v[0] = i0;
    v[1] = i1;
    v[2] = i2;
    bool pos = false;
    bool neg = false;
    for (int k = 0; k < 3; k++) {
      const size_t i = static_cast<size_t>(v[k]);
      const double x = P[i * 3] * nx + P[i * 3 + 1] * ny + P[i * 3 + 2] * nz - cut.d;
      d[k] = std::fabs(x) < 1e-7 ? 0 : x;
      if (d[k] > 0) pos = true;
      if (d[k] < 0) neg = true;
    }
    if (!pos || !neg) {
      out.insert(out.end(), {i0, i1, i2});
      continue;
    }
    // the two sides as polygons (in the triangle's order, so they keep its winding)
    std::vector<int> front;
    std::vector<int> back;
    for (int k = 0; k < 3; k++) {
      const int i = v[k];
      const int j = v[(k + 1) % 3];
      const double di = d[k];
      const double dj = d[(k + 1) % 3];
      if (di >= 0) front.push_back(i);
      if (di <= 0) back.push_back(i);
      if ((di > 0 && dj < 0) || (di < 0 && dj > 0)) {
        const int m = s.lerp(i, j, di / (di - dj));
        front.push_back(m);
        back.push_back(m);
      }
    }
    for (const std::vector<int>* poly : {&front, &back})
      for (size_t k = 1; k + 1 < poly->size(); k++) out.insert(out.end(), {(*poly)[0], (*poly)[k], (*poly)[k + 1]});
  }
  s.tris = std::move(out);
}

/**
 * Mend the edges where a neighbour was cut and this triangle was not (a
 * vertex of the cut lies on its edge): split it there too, so no hairline
 * crack can open between them.
 */
void mendSoup(Soup& s) {
  if (s.made.empty()) return;
  const size_t pi = static_cast<size_t>(s.posIndex());
  // the vertices made, in cells of 5 cm
  const double cell = 0.05;
  std::unordered_map<double, std::vector<int>> grid;
  // (the car is within ±25 m of its middle)
  const auto key = [&](double x, double y, double z) -> double {
    return ((std::floor(x / cell) + 512) * 1024 + std::floor(y / cell) + 512) * 1024 + std::floor(z / cell) + 512;
  };
  {
    const std::vector<double>& P = s.data[pi];
    for (const int m : s.made) {
      const size_t mi = static_cast<size_t>(m) * 3;
      grid[key(P[mi], P[mi + 1], P[mi + 2])].push_back(m);
    }
  }
  std::vector<int> queue = s.tris;
  std::vector<int> out;
  int guard = 0;
  while (!queue.empty()) {
    const int c = queue.back();
    queue.pop_back();
    const int b = queue.back();
    queue.pop_back();
    const int a = queue.back();
    queue.pop_back();
    bool split = false;
    const int v[3] = {a, b, c};
    for (int e = 0; e < 3 && !split && guard < 200000; e++) {
      const int i = v[e];
      const int j = v[(e + 1) % 3];
      const std::vector<double>& P = s.data[pi];
      const double ix = P[static_cast<size_t>(i) * 3];
      const double iy = P[static_cast<size_t>(i) * 3 + 1];
      const double iz = P[static_cast<size_t>(i) * 3 + 2];
      const double dx = P[static_cast<size_t>(j) * 3] - ix;
      const double dy = P[static_cast<size_t>(j) * 3 + 1] - iy;
      const double dz = P[static_cast<size_t>(j) * 3 + 2] - iz;
      const double l2 = dx * dx + dy * dy + dz * dz;
      if (l2 < 1e-10) continue;
      // the cells along the edge
      const int n = static_cast<int>(std::ceil(std::sqrt(l2) / cell)) + 1;
      double last = -1;
      for (int q = 0; q <= n && !split; q++) {
        const double t = static_cast<double>(q) / n;
        const double k = key(ix + dx * t, iy + dy * t, iz + dz * t);
        if (k == last) continue;
        last = k;
        const auto it = grid.find(k);
        if (it == grid.end()) continue;
        for (const int m : it->second) {
          if (m == i || m == j) continue;
          const std::vector<double>& Q = s.data[pi];
          const double mx = Q[static_cast<size_t>(m) * 3] - ix;
          const double my = Q[static_cast<size_t>(m) * 3 + 1] - iy;
          const double mz = Q[static_cast<size_t>(m) * 3 + 2] - iz;
          const double u = (mx * dx + my * dy + mz * dz) / l2;
          if (u <= 1e-6 || u >= 1 - 1e-6) continue;
          const double ex = mx - dx * u;
          const double ey = my - dy * u;
          const double ez = mz - dz * u;
          if (ex * ex + ey * ey + ez * ez > 1e-12) continue;
          // the point on this triangle's own edge, its attributes from this triangle's own corners
          const int w = s.lerp(i, j, u);
          s.made.pop_back();
          const int o = v[(e + 2) % 3];
          queue.insert(queue.end(), {i, w, o, w, j, o});
          split = true;
          guard++;
          break;
        }
      }
    }
    if (!split) out.insert(out.end(), {a, b, c});
  }
  s.tris = std::move(out);
}

const MaterialPtr& DARK() {
  static const MaterialPtr m = lambert({.color = Color(0x141518), .side = Side::Double});
  return m;
}
const MaterialPtr& BAY() {
  static const MaterialPtr m = lambert({.color = Color(0x2a2c30)});
  return m;
}
const MaterialPtr& BAY_METAL() {
  static const MaterialPtr m = lambert({.color = Color(0x6d7278)});
  return m;
}
const MaterialPtr& CAP() {
  static const MaterialPtr m = lambert({.color = Color(0xd8b21c)});
  return m;
}
const GeometryPtr& EMPTY() {
  static const GeometryPtr g = make<BufferGeometry>();
  return g;
}

/** Swap a mesh's geometry – and that of its twin in the see-through cabin, which draws the same. */
void setGeometry(Mesh& mesh, const GeometryPtr& from, const GeometryPtr& to) {
  if (mesh.geometry == to) return;
  mesh.geometry = to;
  for (const ObjectPtr& c : mesh.children)
    if (c->type == ObjectType::Mesh) {
      Mesh& m = static_cast<Mesh&>(*c);
      if (m.geometry == from) m.geometry = to;
    }
}

/** Whether a mesh is part of the see-through cabin (it has a twin that draws its depth). */
bool ghosted(const Mesh& mesh) {
  for (const ObjectPtr& c : mesh.children)
    if (c->type == ObjectType::Mesh && static_cast<const Mesh&>(*c).geometry == mesh.geometry) return true;
  return false;
}

}  // namespace

CarOpenings::~CarOpenings() = default;

CarOpenings::CarOpenings(const OpeningsHost& host) : host_(host) {
  const CarShell& sh = *host.exterior.shell;
  L_ = panelLines(sh, *host.exterior.dims);
  const PanelLines& L = L_;
  profile_ = bodyProfile(*host.exterior.dims);
  const auto& at = profile_;
  const double hw = sh.halfWidth;
  const std::array<double, 3> hinges[] = {
      {-hw, 0, L.zFD}, {hw, 0, L.zFD}, {-hw, 0, L.zB}, {hw, 0, L.zB}, {0, at(L.bz1)[6][1], L.bz1}, {0, at(L.tz0)[6][1], L.tz0},
  };
  for (const auto& [x, y, z] : hinges) {
    const auto pivot = make<Group>();
    pivot->name = "opening";
    pivot->position.set(x, y, z);
    const auto holder = make<Group>();
    holder->position.set(-x, -y, -z);
    pivot->add(holder);
    host.root->add(pivot);
    pivots_.push_back(pivot);
    holders_.push_back(holder);
  }

  // ---- the shell, cut along the panel lines ----
  const std::vector<Cut> cuts = shellCuts(L);
  const std::pair<GeometryPtr, bool> sources[] = {
      {sh.paint, true},
      {sh.glass, true},
      {sh.lining, false},
      {sh.headliner, false},
  };
  std::vector<std::shared_ptr<Mesh>> meshes;
  host.root->traverse([&](Object3D& o) {
    if (o.type != ObjectType::Mesh) return;
    const bool inHolder = o.parent && std::any_of(holders_.begin(), holders_.end(), [&](const std::shared_ptr<Group>& h) { return h.get() == o.parent; });
    if (!inHolder) meshes.push_back(o.as<Mesh>());
  });
  // edges round each part, and round the openings in the body, for the dark strips
  std::vector<std::vector<double>> partEdges(PARTS);
  std::vector<std::vector<double>> bodyEdges(PARTS);
  std::vector<std::unique_ptr<Soup>> soups;
  std::vector<Skin> skins;
  for (const auto& [geo, skin] : sources) {
    std::shared_ptr<Mesh> mesh;
    for (const std::shared_ptr<Mesh>& m : meshes)
      if (m->geometry == geo) {
        mesh = m;
        break;
      }
    if (!mesh) continue;
    soups.push_back(std::make_unique<Soup>(*geo));
    Soup& soup = *soups.back();
    for (const Cut& c : cuts) cutSoup(soup, c);
    mendSoup(soup);
    std::vector<int8_t> region = classifyShell(soup);
    addSwap(mesh, soup, region);
    if (skin) skins.push_back({&soup, std::move(region)});
  }
  collectEdges(skins, partEdges, bodyEdges);
  for (int p = 0; p < PARTS; p++) {
    if (const GeometryPtr own = strips(partEdges[static_cast<size_t>(p)])) {
      const auto m = make<Mesh>(own, DARK());
      m->name = "opening edge";
      holders_[static_cast<size_t>(p)]->add(m);
      shows_.push_back({m, p});
    }
    if (const GeometryPtr hole = strips(bodyEdges[static_cast<size_t>(p)])) {
      const auto m = make<Mesh>(hole, DARK());
      m->name = "opening frame";
      host.exterior.group->add(m);
      shows_.push_back({m, p});
    }
  }

  // ---- the door panels inside ----
  for (const DoorTrim& t : *host.doorTrim) {
    std::vector<std::shared_ptr<Mesh>> parts;
    for (size_t i = 0; i < t.doors.size(); i++) parts.push_back(t.doors[i] ? partMesh(t.mesh, t.doors[i], static_cast<int>(i)) : nullptr);
    parts.push_back(nullptr);
    parts.push_back(nullptr);
    swaps_.push_back({t.mesh, t.mesh->geometry, t.rest ? t.rest : EMPTY(), parts});
  }

  // ---- the door mirrors go with the front doors ----
  for (Mirror* m : host.mirrors) {
    const int side = m->spec.name == "left" ? FL : m->spec.name == "right" ? FR : -1;
    if (side < 0) continue;
    holders_[static_cast<size_t>(side)]->add(m->mesh);
    if (Object3D* housing = host.cockpit->getObjectByName("mirror housing " + m->spec.name)) holders_[static_cast<size_t>(side)]->add(housing->shared_from_this());
  }
  if (Object3D* feet = host.cockpit->getObjectByName("mirror feet"); feet && feet->type == ObjectType::Mesh) addVolume(feet->as<Mesh>());

  // ---- the engine bay under the bonnet ----
  const std::shared_ptr<Group> bay = engineBay();
  host.exterior.group->add(bay);
  shows_.push_back({bay, BONNET});
  poseNow();
}

void CarOpenings::addDetail(const std::vector<ObjectPtr>& objects) {
  for (const ObjectPtr& o : objects)
    if (o->type == ObjectType::Mesh) addVolume(std::static_pointer_cast<Mesh>(o));
  poseNow();
}

void CarOpenings::pose(const std::vector<double>& values) {
  for (int i = 0; i < PARTS; i++) values_[static_cast<size_t>(i)] = static_cast<size_t>(i) < values.size() ? values[static_cast<size_t>(i)] : 0;
  poseNow();
}

/** (pose(this.values): the numbers as they stand) */
void CarOpenings::poseNow() {
  bool any = false;
  for (int i = 0; i < PARTS; i++) {
    // (a Float32Array keeps them: as 32-bit numbers)
    const double v = static_cast<float>(jsMax(0, jsMin(1, values_[static_cast<size_t>(i)])));
    values_[static_cast<size_t>(i)] = v;
    if (v > 0) any = true;
  }
  if (any != split_) {
    split_ = any;
    for (Swap& s : swaps_) setGeometry(*s.mesh, any ? s.whole : s.body, any ? s.body : s.whole);
  }
  for (Swap& s : swaps_)
    for (const std::shared_ptr<Mesh>& p : s.parts)
      if (p) p->visible = any && s.mesh->visible;
  for (Show& s : shows_) s.obj->visible = values_[static_cast<size_t>(s.part)] > 0;
  for (int i = 0; i < PARTS; i++) {
    const double v = values_[static_cast<size_t>(i)];
    const double e = v * v * (3 - 2 * v);
    Group& pv = *pivots_[static_cast<size_t>(i)];
    if (i <= RR) pv.rotation.y = (i % 2 == 0 ? -1 : 1) * DOOR_ANGLE * e;
    else if (i == BONNET) pv.rotation.x = BONNET_ANGLE * e;
    else pv.rotation.x = -BOOT_ANGLE * e;
  }
}

// ---------------------------------------------------------------- building ----

std::vector<int8_t> CarOpenings::classifyShell(const Soup& soup) {
  const size_t n = soup.tris.size() / 3;
  std::vector<int8_t> out(n);
  for (size_t t = 0; t < n; t++) out[t] = static_cast<int8_t>(shellAt(soup.pos(soup.tris[t * 3]), soup.pos(soup.tris[t * 3 + 1]), soup.pos(soup.tris[t * 3 + 2])));
  return out;
}

int CarOpenings::shellAt(const std::array<double, 3>& a, const std::array<double, 3>& b, const std::array<double, 3>& c) {
  const double x = (a[0] + b[0] + c[0]) / 3;
  const double y = (a[1] + b[1] + c[1]) / 3;
  const double z = (a[2] + b[2] + c[2]) / 3;
  const double ux = b[0] - a[0];
  const double uy = b[1] - a[1];
  const double uz = b[2] - a[2];
  const double vx = c[0] - a[0];
  const double vy = c[1] - a[1];
  const double vz = c[2] - a[2];
  const double nx = uy * vz - uz * vy;
  const double ny = uz * vx - ux * vz;
  const double nz = ux * vy - uy * vx;
  double nl = std::sqrt(nx * nx + ny * ny + nz * nz);
  if (nl == 0) nl = 1;
  // (the flat face of the tail – and the panel just inside it, which stays)
  const bool cap = std::fabs(nz) / nl > 0.97 && z > L_.zRear - 0.03;
  return pointPart(x, y, z, cap);
}

int CarOpenings::pointPart(double x, double y, double z, bool cap) {
  const int k = nearest(segmentDistances(profile_(z), std::fabs(x), y, ds_));
  return shellPart(L_, x, y, z, k, cap);
}

std::shared_ptr<Mesh> CarOpenings::partMesh(const std::shared_ptr<Mesh>& src, const GeometryPtr& geo, int part) {
  const auto m = make<Mesh>(geo, src->material);
  m->materials = src->materials;
  m->name = (src->name.empty() ? std::string("part") : src->name) + " (opening)";
  m->castShadow = src->castShadow;
  m->receiveShadow = src->receiveShadow;
  m->renderOrder = src->renderOrder;
  m->visible = false;
  holders_[static_cast<size_t>(part)]->add(m);
  if (ghosted(*src)) host_.register_(m);
  return m;
}

void CarOpenings::addSwap(const std::shared_ptr<Mesh>& mesh, Soup& soup, const std::vector<int8_t>& region) {
  std::vector<std::vector<int>> lists(PARTS + 1);
  for (size_t t = 0; t < region.size(); t++) lists[static_cast<size_t>(region[t] + 1)].push_back(static_cast<int>(t));
  bool none = true;
  for (size_t p = 1; p < lists.size(); p++)
    if (!lists[p].empty()) none = false;
  if (none) return;
  std::vector<std::shared_ptr<Mesh>> parts;
  for (int p = 0; p < PARTS; p++) parts.push_back(!lists[static_cast<size_t>(p) + 1].empty() ? partMesh(mesh, soup.build(lists[static_cast<size_t>(p) + 1]), p) : nullptr);
  swaps_.push_back({mesh, mesh->geometry, soup.build(lists[0]), parts});
}

void CarOpenings::addVolume(const std::shared_ptr<Mesh>& mesh) {
  for (const Swap& s : swaps_)
    if (s.mesh == mesh) return;
  Soup soup(*mesh->geometry);
  const MaterialPtr& mat = mesh->material ? mesh->material : mesh->materials.at(0);
  // (a dark gap drawn along a panel line stays with the body: only what lies well inside a part goes with it)
  const bool gap = mat->name == "panel gap";
  const size_t n = soup.tris.size() / 3;
  std::vector<int8_t> region(n);
  bool any = false;
  for (size_t t = 0; t < n; t++) {
    const auto a = soup.pos(soup.tris[t * 3]);
    const auto b = soup.pos(soup.tris[t * 3 + 1]);
    const auto c = soup.pos(soup.tris[t * 3 + 2]);
    const double x = (a[0] + b[0] + c[0]) / 3;
    const double y = (a[1] + b[1] + c[1]) / 3;
    const double z = (a[2] + b[2] + c[2]) / 3;
    int r = volumeAt(x, y, z);
    if (gap && r >= 0) {
      const double m = 0.03;
      for (const auto& [dx, dy, dz] : {std::array<double, 3>{m, 0, 0}, std::array<double, 3>{-m, 0, 0}, std::array<double, 3>{0, m, 0}, std::array<double, 3>{0, -m, 0},
                                       std::array<double, 3>{0, 0, m}, std::array<double, 3>{0, 0, -m}})
        if (volumeAt(x + dx, y + dy, z + dz) != r) r = -1;
    }
    region[t] = static_cast<int8_t>(r);
    if (r >= 0) any = true;
  }
  if (any) addSwap(mesh, soup, region);
}

int CarOpenings::volumeAt(double x, double y, double z) {
  const PanelLines& L = L_;
  const double ax = std::fabs(x);
  const Profile prof = profile_(z);
  const std::vector<double>& ds = segmentDistances(prof, ax, y, ds_);
  const double hw = host_.exterior.shell->halfWidth;
  // (on the outside of the shell – not on the lining inside it)
  const int k = nearest(ds);
  if (ds[static_cast<size_t>(k)] < 0.04 && outside(prof, k, ax, y) > -0.012) {
    const int part = shellPart(L, x, y, z, k, false);
    if (part >= 0) return part;
  }
  // the tail's face above the tailgate's lower edge
  if (z > L.zRear - 0.05 && z < L.zRear + 0.08 && y >= L.tailSill && ax <= 0.385) return BOOT;
  // inside a door: its panel (below the window), as far in as the panel reaches
  if (ax > hw - 0.17 && y >= L.sill && y < L.belt + 0.01) {
    const int s = x < 0 ? 0 : 1;
    if (z >= L.zFD && z <= L.zB) return FL + s;
    if (z > L.zB && z <= jsMax(L.zRW, along(L.arch, y))) return RL + s;
  }
  return -1;
}

void CarOpenings::collectEdges(const std::vector<Skin>& skins, std::vector<std::vector<double>>& partEdges, std::vector<std::vector<double>>& bodyEdges) {
  using A3 = std::array<double, 3>;
  const auto q = [](double v) { return jsRound(v * 1e4); };
  const auto pkey = [&](const A3& p) { return jsStr(q(p[0])) + "," + jsStr(q(p[1])) + "," + jsStr(q(p[2])); };
  // per region: each edge with how often it is used, and one use of it (positions and normals of its ends, the triangle's middle)
  struct Use {
    int count;
    int region;
    A3 a, b, na, nb, mid;
    bool cap;
  };
  // (a Map: in the order the edges were first seen)
  std::unordered_map<std::string, size_t> at;
  std::vector<Use> uses;
  for (const Skin& sk : skins) {
    const Soup& soup = *sk.soup;
    const auto ni = std::find(soup.names.begin(), soup.names.end(), "normal");
    const std::vector<double>* N = ni != soup.names.end() ? &soup.data[static_cast<size_t>(ni - soup.names.begin())] : nullptr;
    const auto nrm = [&](int i) -> A3 {
      if (!N) return {0, 0, 0};
      const size_t k = static_cast<size_t>(i) * 3;
      return {(*N)[k], (*N)[k + 1], (*N)[k + 2]};
    };
    for (size_t t = 0; t < sk.region.size(); t++) {
      const int v[3] = {soup.tris[t * 3], soup.tris[t * 3 + 1], soup.tris[t * 3 + 2]};
      const A3 p[3] = {soup.pos(v[0]), soup.pos(v[1]), soup.pos(v[2])};
      const A3 mid = {(p[0][0] + p[1][0] + p[2][0]) / 3, (p[0][1] + p[1][1] + p[2][1]) / 3, (p[0][2] + p[1][2] + p[2][2]) / 3};
      const double ux = p[1][0] - p[0][0];
      const double uy = p[1][1] - p[0][1];
      const double vx = p[2][0] - p[0][0];
      const double vy = p[2][1] - p[0][1];
      const double fz = ux * vy - uy * vx;
      const double h1 = (p[1][1] - p[0][1]) * (p[2][2] - p[0][2]) - (p[1][2] - p[0][2]) * vy;
      const double h2 = (p[1][2] - p[0][2]) * vx - ux * (p[2][2] - p[0][2]);
      double fl = std::sqrt(fz * fz + h1 * h1 + h2 * h2);
      if (fl == 0) fl = 1;
      const bool cap = std::fabs(fz) / fl > 0.97 && mid[2] > L_.zRear - 0.03;
      for (int e = 0; e < 3; e++) {
        const std::string ka = pkey(p[e]);
        const std::string kb = pkey(p[(e + 1) % 3]);
        if (ka == kb) continue;
        const std::string key = std::to_string(sk.region[t]) + "|" + (ka < kb ? ka + "|" + kb : kb + "|" + ka);
        const auto it = at.find(key);
        if (it != at.end()) uses[it->second].count++;
        else {
          at.emplace(key, uses.size());
          uses.push_back({1, sk.region[t], p[e], p[(e + 1) % 3], nrm(v[e]), nrm(v[(e + 1) % 3]), mid, cap});
        }
      }
    }
  }
  for (const Use& u : uses) {
    if (u.count != 1) continue;
    const auto push = [&](std::vector<double>& list) {
      for (const A3* a : {&u.a, &u.b, &u.na, &u.nb}) list.insert(list.end(), a->begin(), a->end());
    };
    if (u.region >= 0) {
      push(partEdges[static_cast<size_t>(u.region)]);
      continue;
    }
    // an edge of the body: which part lies just across it?
    const double mx = (u.a[0] + u.b[0]) / 2;
    const double my = (u.a[1] + u.b[1]) / 2;
    const double mz = (u.a[2] + u.b[2]) / 2;
    const double dx = mx - u.mid[0];
    const double dy = my - u.mid[1];
    const double dz = mz - u.mid[2];
    double l = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (l == 0) l = 1;
    const double e = 0.003 / l;
    const int part = pointPart(mx + dx * e, my + dy * e, mz + dz * e, u.cap);
    if (part >= 0) push(bodyEdges[static_cast<size_t>(part)]);
  }
}

GeometryPtr CarOpenings::strips(const std::vector<double>& edges) {
  if (edges.empty()) return nullptr;
  const double T = 0.028;
  std::vector<float> pos;
  for (size_t i = 0; i + 11 < edges.size(); i += 12) {
    const double ax = edges[i], ay = edges[i + 1], az = edges[i + 2], bx = edges[i + 3], by = edges[i + 4], bz = edges[i + 5];
    const double nax = edges[i + 6], nay = edges[i + 7], naz = edges[i + 8], nbx = edges[i + 9], nby = edges[i + 10], nbz = edges[i + 11];
    double la = std::sqrt(nax * nax + nay * nay + naz * naz);
    if (la == 0) la = 1;
    double lb = std::sqrt(nbx * nbx + nby * nby + nbz * nbz);
    if (lb == 0) lb = 1;
    const double a2[3] = {ax - (nax / la) * T, ay - (nay / la) * T, az - (naz / la) * T};
    const double b2[3] = {bx - (nbx / lb) * T, by - (nby / lb) * T, bz - (nbz / lb) * T};
    for (const double v : {ax, ay, az, bx, by, bz, b2[0], b2[1], b2[2], ax, ay, az, b2[0], b2[1], b2[2], a2[0], a2[1], a2[2]}) pos.push_back(static_cast<float>(v));
  }
  const GeometryPtr g = make<BufferGeometry>();
  g->setAttribute("position", attr(std::move(pos), 3));
  g->computeVertexNormals();
  return g;
}

std::shared_ptr<Group> CarOpenings::engineBay() {
  const PanelLines& L = L_;
  const auto g = make<Group>();
  g->name = "engine bay";
  const auto& at = profile_;
  const double z0 = L.bz0 + 0.04;
  const double z1 = L.bz1;
  const double zm = (z0 + z1) / 2;
  const double w = jsMin(along(L.bonnetEdge, z1), along(L.bonnetEdge, z0) + 0.1);
  const auto box = [&](double sx, double sy, double sz, double x, double y, double z, const MaterialPtr& mat) {
    const auto m = make<Mesh>(boxGeometry(sx, sy, sz), mat);
    m->position.set(x, y, z);
    g->add(m);
  };
  const auto top = [&](double z) { return at(z)[6][1]; };
  // the floor of the bay, the bulkhead behind it, the radiator in front
  box(2 * w, 0.02, z1 - z0, 0, 0.4, zm, BAY());
  box(2 * w, top(z1) - 0.43, 0.03, 0, (top(z1) + 0.4) / 2 - 0.02, z1 - 0.015, BAY());
  box(2 * w - 0.2, top(z0) - 0.5, 0.05, 0, (top(z0) + 0.4) / 2 - 0.02, z0 + 0.04, BAY_METAL());
  // the engine across the car, its cover, the battery and the reservoir
  const double ez = zm + 0.06;
  const double eh = jsMin(0.32, top(ez) - 0.5);
  box(0.68, eh, 0.48, -0.05, 0.42 + eh / 2, ez, BAY());
  box(0.5, 0.03, 0.34, -0.05, 0.42 + eh + 0.012, ez, BAY_METAL());
  box(0.26, 0.19, 0.17, -w + 0.22, 0.42 + 0.095, z1 - 0.2, BAY());
  const auto res = make<Mesh>(cylinderGeometry(0.06, 0.06, 0.14, 12), BAY_METAL());
  res->position.set(w - 0.2, 0.49, z1 - 0.22);
  const auto cap = make<Mesh>(cylinderGeometry(0.035, 0.035, 0.025, 12), CAP());
  cap->position.set(w - 0.2, 0.57, z1 - 0.22);
  g->add(res, cap);
  g->visible = false;
  return g;
}

}  // namespace fs

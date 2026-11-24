// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/detailkit.ts @ v1.7.7
#include "render/detailkit.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/js.h"
#include "engine/geometries.h"

namespace fs {

GeometryPtr bare(const GeometryPtr& g) {
  GeometryPtr n = g->index ? g->toNonIndexed() : g;
  std::vector<std::string> names;
  for (const auto& entry : n->attributes) names.push_back(entry.first);
  for (const std::string& name : names)
    if (name != "position" && name != "normal") n->deleteAttribute(name);
  if (!n->getAttribute("normal")) n->computeVertexNormals();
  return n;
}

// ---- Parts ----

void Parts::add(const MaterialPtr& material, const std::vector<GeometryPtr>& geos) {
  std::vector<GeometryPtr>* l = nullptr;
  for (auto& [m, list] : lists_)
    if (m == material) l = &list;
  if (!l) {
    lists_.emplace_back(material, std::vector<GeometryPtr>{});
    l = &lists_.back().second;
  }
  for (const GeometryPtr& g : geos) l->push_back(bare(g));
}

std::vector<std::shared_ptr<Mesh>> Parts::build(Object3D& parent, const std::string& name, bool castShadow) {
  std::vector<std::shared_ptr<Mesh>> out;
  for (auto& [mat, list] : lists_) {
    if (list.empty()) continue;
    const auto m = make<Mesh>(list.size() == 1 ? list[0] : mergeGeometries(list, false), mat);
    m->name = name;
    m->castShadow = castShadow;
    parent.add(m);
    out.push_back(m);
  }
  lists_.clear();
  return out;
}

// ---- Frame ----

Frame::Frame(const Vector3& o_, const Vector3& x_, const Vector3& y_, const std::optional<Vector3>& z_) : o(o_) {
  z = (z_ ? *z_ : Vector3().crossVectors(x_, y_)).clone().normalize();
  // x and y square to z (y kept as near as it was)
  x = Vector3().crossVectors(y_, z).normalize();
  y = Vector3().crossVectors(z, x).normalize();
}

Frame Frame::on(const Vector3& p, const Vector3& n, const Vector3& up) {
  const Vector3 z = n.clone().normalize();
  Vector3 y = up.clone().addScaledVector(z, -up.dot(z));
  if (y.lengthSq() < 1e-6) y = Vector3(0, 0, -1).addScaledVector(z, z.z);
  const Vector3 x = Vector3().crossVectors(y, z);
  return Frame(p, x, y, z);
}

Vector3 Frame::at(double u, double v, double w) const { return o.clone().addScaledVector(x, u).addScaledVector(y, v).addScaledVector(z, w); }

Frame Frame::moved(double u, double v, double w, double roll) const {
  const double c = std::cos(roll);
  const double s = std::sin(roll);
  const Vector3 nx = x.clone().multiplyScalar(c).addScaledVector(y, s);
  const Vector3 ny = y.clone().multiplyScalar(c).addScaledVector(x, -s);
  return Frame(at(u, v, w), nx, ny, z);
}

GeometryPtr Frame::put(const GeometryPtr& g) const {
  Matrix4 m;
  m.makeBasis(x, y, z).setPosition(o);
  g->applyMatrix4(m);
  return g;
}

GeometryPtr Frame::box(double w, double h, double d, double u, double v, double wz) const {
  GeometryPtr g = boxGeometry(w, h, d);
  g->translate(u, v, wz);
  return put(g);
}

GeometryPtr Frame::cyl(double r, double len, double u, double v, double wz, int seg, std::optional<double> r2) const {
  GeometryPtr g = cylinderGeometry(r2 ? *r2 : r, r, len, seg);
  g->rotateX(PI / 2).translate(u, v, wz);
  return put(g);
}

GeometryPtr Frame::ring(double r, double tube, double u, double v, double wz, int seg) const {
  GeometryPtr g = torusGeometry(r, tube, 6, seg);
  g->translate(u, v, wz);
  return put(g);
}

GeometryPtr Frame::disc(double r, double u, double v, double wz, int seg) const {
  GeometryPtr g = circleGeometry(r, seg);
  g->translate(u, v, wz);
  return put(g);
}

// ---- Surface ----

Surface::Surface(const std::vector<GeometryPtr>& geos, double step) : step_(step) {
  std::vector<double> t;
  for (const GeometryPtr& g : geos) {
    const AttrPtr pos = g->getAttribute("position");
    const AttrPtr idx = g->index;
    const size_t n = idx ? idx->count() : pos->count();
    for (size_t i = 0; i + 2 < n; i += 3) {
      for (size_t k = 0; k < 3; k++) {
        const size_t v = idx ? static_cast<size_t>(idx->getX(i + k)) : i + k;
        t.push_back(pos->getX(v));
        t.push_back(pos->getY(v));
        t.push_back(pos->getZ(v));
      }
    }
  }
  tri_.assign(t.begin(), t.end());
  double lo = std::numeric_limits<double>::infinity();
  double hi = -std::numeric_limits<double>::infinity();
  for (size_t i = 2; i < t.size(); i += 3) {
    lo = jsMin(lo, static_cast<double>(tri_[i]));
    hi = jsMax(hi, static_cast<double>(tri_[i]));
  }
  z0_ = lo - step;
  const int count = static_cast<int>(std::ceil((hi - z0_) / step)) + 2;
  slices_.resize(static_cast<size_t>(jsMax(0, count)));
  const size_t faces = t.size() / 9;
  for (size_t f = 0; f < faces; f++) {
    const double a = jsMin(jsMin(tri_[f * 9 + 2], tri_[f * 9 + 5]), tri_[f * 9 + 8]);
    const double b = jsMax(jsMax(tri_[f * 9 + 2], tri_[f * 9 + 5]), tri_[f * 9 + 8]);
    for (int s = slice(a); s <= slice(b); s++) slices_[static_cast<size_t>(s)].push_back(static_cast<int>(f));
  }
  seen_.assign(faces, 0);
}

int Surface::slice(double z) const {
  return static_cast<int>(jsMax(0, jsMin(static_cast<double>(slices_.size()) - 1, std::floor((z - z0_) / step_))));
}

std::optional<Hit> Surface::hit(const Vector3& o, const Vector3& d, double far) {
  const std::vector<float>& t = tri_;
  const uint32_t stamp = ++stamp_;
  double best = far;
  int bf = -1;
  if (slices_.empty()) return std::nullopt;
  const int s0 = slice(jsMin(o.z, o.z + d.z * far));
  const int s1 = slice(jsMax(o.z, o.z + d.z * far));
  for (int s = s0; s <= s1; s++) {
    for (const int f : slices_[static_cast<size_t>(s)]) {
      if (seen_[static_cast<size_t>(f)] == stamp) continue;
      seen_[static_cast<size_t>(f)] = stamp;
      const size_t i = static_cast<size_t>(f) * 9;
      // Möller–Trumbore
      const double e1x = static_cast<double>(t[i + 3]) - t[i];
      const double e1y = static_cast<double>(t[i + 4]) - t[i + 1];
      const double e1z = static_cast<double>(t[i + 5]) - t[i + 2];
      const double e2x = static_cast<double>(t[i + 6]) - t[i];
      const double e2y = static_cast<double>(t[i + 7]) - t[i + 1];
      const double e2z = static_cast<double>(t[i + 8]) - t[i + 2];
      const double px = d.y * e2z - d.z * e2y;
      const double py = d.z * e2x - d.x * e2z;
      const double pz = d.x * e2y - d.y * e2x;
      const double det = e1x * px + e1y * py + e1z * pz;
      if (std::fabs(det) < 1e-12) continue;
      const double inv = 1 / det;
      const double tx = o.x - t[i];
      const double ty = o.y - t[i + 1];
      const double tz = o.z - t[i + 2];
      const double u = (tx * px + ty * py + tz * pz) * inv;
      if (u < 0 || u > 1) continue;
      const double qx = ty * e1z - tz * e1y;
      const double qy = tz * e1x - tx * e1z;
      const double qz = tx * e1y - ty * e1x;
      const double v = (d.x * qx + d.y * qy + d.z * qz) * inv;
      if (v < 0 || u + v > 1) continue;
      const double dist = (e2x * qx + e2y * qy + e2z * qz) * inv;
      if (dist > 1e-5 && dist < best) {
        best = dist;
        bf = f;
      }
    }
  }
  if (bf < 0) return std::nullopt;
  const size_t i = static_cast<size_t>(bf) * 9;
  Vector3 n = Vector3(static_cast<double>(t[i + 3]) - t[i], static_cast<double>(t[i + 4]) - t[i + 1], static_cast<double>(t[i + 5]) - t[i + 2])
                  .cross(Vector3(static_cast<double>(t[i + 6]) - t[i], static_cast<double>(t[i + 7]) - t[i + 1], static_cast<double>(t[i + 8]) - t[i + 2]))
                  .normalize();
  if (n.dot(d) > 0) n.negate();
  return Hit{o.clone().addScaledVector(d, best), n};
}

std::optional<Hit> Surface::onto(const Vector3& p, const Vector3& d) {
  const Vector3 dir = d.clone().normalize();
  return hit(p.clone().addScaledVector(dir, -1), dir, 2.5);
}

GeometryPtr drape(Surface& surf, const std::function<Vector3(double u, double v)>& origin, const Vector3& d, int nu, int nv, double lift) {
  const Vector3 dir = d.clone().normalize();
  std::vector<std::vector<std::optional<Hit>>> grid;
  for (int j = 0; j <= nv; j++) {
    std::vector<std::optional<Hit>> row;
    for (int i = 0; i <= nu; i++) row.push_back(surf.hit(origin(static_cast<double>(i) / nu, static_cast<double>(j) / nv), dir, 3));
    grid.push_back(std::move(row));
  }
  return sheetOf(grid, lift);
}

GeometryPtr sheetOf(const std::vector<std::vector<std::optional<Hit>>>& grid, double lift) {
  std::vector<float> pos;
  std::vector<float> nrm;
  const auto lifted = [&](const Hit& h) { return h.p.clone().addScaledVector(h.n, lift); };
  const auto push = [&](const Hit& h) {
    const Vector3 p = lifted(h);
    pos.insert(pos.end(), {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)});
    nrm.insert(nrm.end(), {static_cast<float>(h.n.x), static_cast<float>(h.n.y), static_cast<float>(h.n.z)});
  };
  for (size_t j = 0; j + 1 < grid.size(); j++) {
    for (size_t i = 0; i + 1 < grid[j].size(); i++) {
      const std::optional<Hit>& a = grid[j][i];
      const std::optional<Hit>& b = grid[j][i + 1];
      const std::optional<Hit>& c = grid[j + 1][i + 1];
      const std::optional<Hit>& e = grid[j + 1][i];
      if (!a || !b || !c || !e) continue;
      // wound so that the face looks the way the normals do
      const Vector3 f = Vector3().subVectors(lifted(*b), lifted(*a)).cross(Vector3().subVectors(lifted(*e), lifted(*a)));
      const bool out = f.dot(a->n) >= 0;
      if (out)
        for (const Hit* h : {&*a, &*b, &*c, &*a, &*c, &*e}) push(*h);
      else
        for (const Hit* h : {&*a, &*c, &*b, &*a, &*e, &*c}) push(*h);
    }
  }
  const GeometryPtr g = make<BufferGeometry>();
  g->setAttribute("position", attr(std::move(pos), 3));
  g->setAttribute("normal", attr(std::move(nrm), 3));
  return g;
}

GeometryPtr ribbon(const std::vector<Hit>& pts, double width, double lift) {
  std::vector<std::vector<std::optional<Hit>>> rows;
  const int n = static_cast<int>(pts.size());
  for (int i = 0; i < n; i++) {
    const Vector3& a = pts[static_cast<size_t>(jsMax(0, i - 1))].p;
    const Vector3& b = pts[static_cast<size_t>(jsMin(n - 1, i + 1))].p;
    const Vector3 al = Vector3().subVectors(b, a).normalize();
    const Vector3 side = Vector3().crossVectors(pts[static_cast<size_t>(i)].n, al).normalize().multiplyScalar(width / 2);
    rows.push_back({Hit{pts[static_cast<size_t>(i)].p.clone().sub(side), pts[static_cast<size_t>(i)].n}, Hit{pts[static_cast<size_t>(i)].p.clone().add(side), pts[static_cast<size_t>(i)].n}});
  }
  return sheetOf(rows, lift);
}

std::vector<Hit> along(Surface& surf, const std::function<Vector3(double t)>& path, const std::function<Vector3(double t)>& d, int n) {
  std::vector<Hit> out;
  for (int i = 0; i <= n; i++) {
    const double t = static_cast<double>(i) / n;
    const std::optional<Hit> h = surf.onto(path(t), d(t));
    if (h) out.push_back(*h);
  }
  return out;
}

GeometryPtr lathe(const std::vector<std::array<double, 2>>& profile, int seg, double phase) {
  std::vector<Vector2> pts;
  for (const auto& [r, x] : profile) pts.emplace_back(r, x);
  GeometryPtr g = latheGeometry(pts, seg, phase);
  // (the lathe turns about y: lay its axis along x)
  g->rotateZ(-PI / 2);
  return bare(g);
}

// ------------------------------------------------------------------------------------ the wheel

namespace {

/** a uniform colour on every vertex (`tint`) */
GeometryPtr tint(const GeometryPtr& g, double k) {
  const size_t n = g->getAttribute("position")->count();
  g->setAttribute("color", attr(std::vector<float>(n * 3, static_cast<float>(k)), 3));
  return g;
}

}  // namespace

WheelPartsOut wheelParts(const WheelSpec& w, int side) {
  using P = std::array<double, 2>;
  const double r = w.radius;
  const double hw = w.width / 2;
  const double rr = w.rim;
  // ---- the tyre: bead, sidewall with its bulge, shoulder, tread with grooves – from the inner bead to the outer one
  std::vector<P> prof;
  const auto side1 = [&](double s) -> std::vector<P> {
    return {
        P{rr, s * (hw - 0.012)},
        P{rr + 0.012, s * (hw + 0.002)},
        P{rr + (r - rr) * 0.45, s * (hw + 0.008)},
        P{r - (r - rr) * 0.22, s * (hw + 0.004)},
        P{r - 0.012, s * (hw - 0.01)},
        P{r - 0.002, s * (hw - 0.028)},
    };
  };
  for (const P& p : side1(-1)) prof.push_back(p);
  std::vector<double> grooves;
  if (!w.lugs)
    for (const double k : w.twin ? std::vector<double>{-0.78, -0.42, 0.42, 0.78} : std::vector<double>{-0.5, -0.17, 0.17, 0.5}) grooves.push_back(k * (hw - 0.03));
  for (size_t gi = 0; gi < grooves.size(); gi++) {
    const double gx = grooves[gi];
    const double gw = jsMin(0.006, w.width * 0.03);
    prof.insert(prof.end(), {P{r, gx - gw - 0.002}, P{r - 0.007, gx - gw}, P{r - 0.007, gx + gw}, P{r, gx + gw + 0.002}});
    // (between the two tyres of a twin wheel: down to the rims)
    // (grooves.indexOf(gx): the first of equal values)
    const size_t first = static_cast<size_t>(std::find(grooves.begin(), grooves.end(), gx) - grooves.begin());
    if (w.twin && gx < 0 && first == 1)
      prof.insert(prof.end(), {P{r - 0.01, -0.03}, P{r - (r - rr) * 0.25, -0.022}, P{rr + 0.05, -0.012}, P{rr + 0.05, 0.012}, P{r - (r - rr) * 0.25, 0.022}, P{r - 0.01, 0.03}});
  }
  {
    std::vector<P> s1 = side1(1);
    std::reverse(s1.begin(), s1.end());
    for (const P& p : s1) prof.push_back(p);
  }
  std::vector<GeometryPtr> tyreParts = {lathe(prof, 40)};
  // blocks of the tread on the shoulders – or the chevrons of a tractor's lugs
  const int blocks = w.lugs ? 22 : 36;
  for (int i = 0; i < blocks; i++) {
    const double a = (static_cast<double>(i) / blocks) * PI * 2;
    if (w.lugs) {
      for (const double s : {-1.0, 1.0}) {
        GeometryPtr lug = boxGeometry(hw * 0.95, 0.035, 0.07);
        lug->translate(s * hw * 0.5, r + 0.012, 0).rotateY(s * 0.45);
        lug->rotateX(a + (s > 0 ? PI / blocks : 0));
        tyreParts.push_back(bare(lug));
      }
    } else {
      for (const double s : {-1.0, 1.0}) {
        GeometryPtr bk = boxGeometry(0.02, 0.004, (PI * 2 * r) / blocks / 2.6);
        bk->translate(s * (hw - 0.03), r - 0.0005, 0);
        bk->rotateX(a);
        tyreParts.push_back(bare(bk));
      }
    }
  }
  const GeometryPtr tyre = mergeGeometries(tyreParts, false);

  // ---- the metal: the rim's barrel and lip, spokes (or a dished disc), hub, nuts, cap – and the brake disc
  std::vector<GeometryPtr> metal;
  const double face = hw - 0.012;
  // barrel, seen from inside (normals inwards: the x falls along the profile)
  metal.push_back(lathe({P{rr - 0.004, face - 0.004}, P{rr - 0.006, 0}, P{rr - 0.004, -face}}, 28));
  // the lip over the tyre's bead
  {
    std::vector<P> lip = {P{rr - 0.012, face - 0.006}, P{rr - 0.004, face + 0.004}, P{rr + 0.006, face + 0.004}, P{rr + 0.008, face - 0.004}};
    std::reverse(lip.begin(), lip.end());
    metal.push_back(lathe(lip, 36));
  }
  const double hubR = rr * 0.3;
  const double hubX = face - 0.024;
  // hub face and its rim
  {
    GeometryPtr h = cylinderGeometry(hubR, hubR * 1.05, 0.02, 20);
    h->rotateZ(-PI / 2).translate(hubX, 0, 0);
    metal.push_back(bare(h));
  }
  if (w.spokes > 0) {
    // spokes from the hub out to the lip, dished: the hub lies deeper than the rim
    for (int i = 0; i < w.spokes; i++) {
      const double a = (static_cast<double>(i) / w.spokes) * PI * 2;
      std::vector<std::vector<V3>> pts;
      for (const auto& [rad, x, wd] : {std::array<double, 3>{hubR * 0.9, hubX + 0.012, 0.03}, std::array<double, 3>{rr * 0.6, face - 0.006, 0.026}, std::array<double, 3>{rr - 0.01, face + 0.002, 0.034}}) {
        const double c = std::cos(a);
        const double sn = std::sin(a);
        // a ring across the spoke: its width round the wheel, its depth along x
        std::vector<V3> ring;
        for (const auto& [du, dx] : {P{-wd / 2, 0.011}, P{wd / 2, 0.011}, P{wd / 2, -0.011}, P{-wd / 2, -0.011}}) ring.push_back(V3{x + dx, c * rad - sn * du, sn * rad + c * du});
        pts.push_back(ring);
      }
      metal.push_back(tubeOf(pts));
    }
  } else {
    // a steel wheel: a dished disc from the lip down to the hub
    metal.push_back(lathe({P{rr - 0.01, face - 0.004}, P{rr * 0.62, hubX + 0.02}, P{hubR, hubX + 0.006}}, 28));
  }
  // nuts and the centre cap
  for (int i = 0; i < w.nuts; i++) {
    const double a = (static_cast<double>(i) / w.nuts) * PI * 2;
    const double nr = hubR * 0.62;
    GeometryPtr nut = cylinderGeometry(0.009, 0.009, 0.02, 6);
    nut->rotateZ(-PI / 2).translate(hubX + 0.016, std::cos(a) * nr, std::sin(a) * nr);
    metal.push_back(bare(nut));
  }
  metal.push_back(lathe({P{hubR * 0.38, hubX + 0.008}, P{hubR * 0.36, hubX + 0.02}, P{hubR * 0.22, hubX + 0.027}, P{0.001, hubX + 0.029}}, 18));
  // the brake disc (or a truck's drum) behind the spokes, darker: its vertices carry a colour
  std::vector<GeometryPtr> discParts;
  if (w.disc) {
    const double ro = rr * 0.74;
    const double ri = rr * 0.38;
    const double th = 0.024;
    // (a face looks to +x where the radius shrinks along the profile, outwards where x grows)
    discParts.push_back(lathe({P{ri, -0.004 - th}, P{ro, -0.004 - th}, P{ro, -0.004}, P{ri, -0.004}}, 32));
    // the hat up to the hub
    discParts.push_back(lathe({P{ri, -0.004}, P{hubR * 1.1, hubX - 0.012}}, 20));
  } else {
    discParts.push_back(lathe({P{rr * 0.8, -hw * 0.7}, P{rr * 0.8, hubX - 0.016}, P{hubR * 1.1, hubX - 0.014}}, 24));
  }
  std::vector<GeometryPtr> all;
  for (const GeometryPtr& g : metal) all.push_back(tint(g, 1));
  for (const GeometryPtr& g : discParts) all.push_back(tint(bare(g), 0.42));
  const GeometryPtr metalGeo = mergeGeometries(all, false);

  // ---- the caliper: astride the disc's edge, behind and above the axle
  GeometryPtr caliper;
  if (w.disc) {
    const double ro = rr * 0.74;
    const double a = 0.55;  // up from the rear, rad
    const double cz = std::cos(a) * (ro - 0.012);
    const double cy = std::sin(a) * (ro - 0.012);
    GeometryPtr body = boxGeometry(0.05, 0.04, 0.1);
    body->translate(-0.016, 0, 0);
    // (its length along the disc's edge there)
    body->rotateX(PI / 2 - a);
    body->translate(0, cy, cz);
    caliper = bare(body);
  }
  // the other side: the mirror image (the faces turned the right way round again)
  const auto mirror = [&](const GeometryPtr& g) { return side > 0 ? g : flipX(g); };
  return {mirror(tyre), mirror(metalGeo), caliper ? mirror(caliper) : nullptr};
}

GeometryPtr flipX(const GeometryPtr& g) {
  GeometryPtr out = g->index ? g->toNonIndexed() : g->clone();
  out->applyMatrix4(Matrix4().makeScale(-1, 1, 1));
  const AttrPtr pos = out->getAttribute("position");
  const AttrPtr nrm = out->getAttribute("normal");
  const AttrPtr col = out->getAttribute("color");
  for (size_t i = 0; i + 2 < pos->count(); i += 3) {
    for (const AttrPtr& a : {pos, nrm, col}) {
      if (!a) continue;
      const size_t s = static_cast<size_t>(a->itemSize);
      for (size_t k = 0; k < s; k++) {
        const double t = a->at((i + 1) * s + k);
        a->put((i + 1) * s + k, a->at((i + 2) * s + k));
        a->put((i + 2) * s + k, t);
      }
    }
  }
  return out;
}

GeometryPtr tubeOf(const std::vector<std::vector<V3>>& rings, bool caps) {
  std::vector<float> pos;
  const size_t n = rings[0].size();
  const auto tri = [&](const V3& a, const V3& b, const V3& c) {
    for (const V3* p : {&a, &b, &c})
      for (const double v : *p) pos.push_back(static_cast<float>(v));
  };
  // which way round the rings run, seen along the sweep: the skin must face out
  const auto centre = [&](const std::vector<V3>& r) {
    V3 s = {0, 0, 0};
    for (const V3& p : r) s = V3{s[0] + p[0] / n, s[1] + p[1] / n, s[2] + p[2] / n};
    return s;
  };
  const V3 c0 = centre(rings[0]);
  const V3 c1 = centre(rings.back());
  const Vector3 axis(c1[0] - c0[0], c1[1] - c0[1], c1[2] - c0[2]);
  const Vector3 a0 = Vector3(rings[0][0][0], rings[0][0][1], rings[0][0][2]).sub(Vector3(c0[0], c0[1], c0[2]));
  const Vector3 a1 = Vector3(rings[0][1][0], rings[0][1][1], rings[0][1][2]).sub(Vector3(c0[0], c0[1], c0[2]));
  const bool ccw = Vector3().crossVectors(a0, a1).dot(axis) > 0;
  for (size_t i = 0; i + 1 < rings.size(); i++) {
    for (size_t j = 0; j < n; j++) {
      const size_t k = (j + 1) % n;
      const V3& a = rings[i][j];
      const V3& b = rings[i][k];
      const V3& c = rings[i + 1][k];
      const V3& d = rings[i + 1][j];
      if (ccw) {
        tri(a, b, c);
        tri(a, c, d);
      } else {
        tri(a, c, b);
        tri(a, d, c);
      }
    }
  }
  if (caps) {
    for (const auto& [r, end] : {std::pair<const std::vector<V3>*, bool>{&rings[0], false}, std::pair<const std::vector<V3>*, bool>{&rings.back(), true}}) {
      const V3 c = centre(*r);
      for (size_t j = 0; j < n; j++) {
        const size_t k = (j + 1) % n;
        // (a ring running counter-clockwise seen from where the sweep heads: its fan looks that way)
        if (ccw == end) tri(c, (*r)[j], (*r)[k]);
        else tri(c, (*r)[k], (*r)[j]);
      }
    }
  }
  const GeometryPtr g = make<BufferGeometry>();
  g->setAttribute("position", attr(std::move(pos), 3));
  g->computeVertexNormals();
  return g;
}

// ---- WheelSet ----

WheelSet::WheelSet(const WheelSpec& spec, const std::vector<int>& sides, const Materials& materials) : sides_(sides) {
  for (const int side : {-1, 1}) {
    std::vector<int> index;
    for (size_t i = 0; i < sides.size(); i++)
      if (sides[i] == side) index.push_back(static_cast<int>(i));
    if (index.empty()) continue;
    const WheelPartsOut parts = wheelParts(spec, side);
    const auto mk = [&](const GeometryPtr& g, const MaterialPtr& mat) {
      const auto im = make<InstancedMesh>(g, mat, static_cast<int>(index.size()));
      // (always near the camera that follows the vehicle: no bounds to keep up to date)
      im->frustumCulled = false;
      im->castShadow = true;
      group->add(im);
      return im;
    };
    Set s;
    s.tyre = mk(parts.tyre, materials.tyre);
    s.metal = mk(parts.metal, materials.metal);
    s.caliper = parts.caliper ? mk(parts.caliper, materials.caliper) : nullptr;
    s.index = index;
    meshes_.push_back(s);
  }
  group->name = "detailed wheels";
}

void WheelSet::place(int i, const Vector3& pos, double steer, double spin) {
  for (Set& set : meshes_) {
    const auto it = std::find(set.index.begin(), set.index.end(), i);
    if (it == set.index.end()) continue;
    const int k = static_cast<int>(it - set.index.begin());
    e_.set(-spin, -steer, 0, Euler::YXZ);
    m_.compose(pos, q_.setFromEuler(e_), one_);
    set.tyre->setMatrixAt(k, m_);
    set.metal->setMatrixAt(k, m_);
    set.tyre->instanceMatrix->needsUpdate();
    set.metal->instanceMatrix->needsUpdate();
    if (set.caliper) {
      e_.set(0, -steer, 0, Euler::YXZ);
      m_.compose(p_.copy(pos), q_.setFromEuler(e_), one_);
      set.caliper->setMatrixAt(k, m_);
      set.caliper->instanceMatrix->needsUpdate();
    }
  }
}

TexturePtr plateTexture(const std::string& text, bool rear) {
  const Canvas c(1040, 220);
  const Ctx2D x = c.getContext2D();
  x.fillStyle("#f5f5f1");
  x.fillRect(0, 0, 1040, 220);
  x.strokeStyle("#111");
  x.lineWidth(10);
  x.strokeRect(5, 5, 1030, 210);
  // the EU band: twelve stars in a ring, the country's letter under them
  x.fillStyle("#1b3fa0");
  x.fillRect(10, 10, 90, 200);
  x.fillStyle("#ffd200");
  for (int i = 0; i < 12; i++) {
    const double a = (i / 12.0) * PI * 2;
    const double sx = 55 + std::cos(a) * 28;
    const double sy = 66 + std::sin(a) * 28;
    x.beginPath();
    for (int k = 0; k < 10; k++) {
      const double r = k % 2 == 0 ? 6 : 2.6;
      const double b = (k / 10.0) * PI * 2 - PI / 2;
      if (k == 0) x.moveTo(sx + std::cos(b) * r, sy + std::sin(b) * r);
      else x.lineTo(sx + std::cos(b) * r, sy + std::sin(b) * r);
    }
    x.fill();
  }
  x.fillStyle("#fff");
  x.font("700 60px \"DIN Alternate\", \"Bahnschrift\", \"Arial Narrow\", Arial, sans-serif");
  x.textAlign("center");
  x.textBaseline("middle");
  x.fillText("D", 55, 168);
  // the characters: the district, the seals, the rest – a little raised (a light edge above, a shadow below)
  const std::vector<std::string> parts = split(text, " ");
  const std::string district = parts.empty() ? "" : parts[0];
  std::string rest;
  for (size_t i = 1; i < parts.size(); i++) rest += (i > 1 ? " " : "") + parts[i];
  x.font("700 150px \"DIN Alternate\", \"Bahnschrift\", \"Arial Narrow\", Arial, sans-serif");
  x.textAlign("left");
  const auto draw = [&](const std::string& t, double px) {
    x.fillStyle("rgba(0, 0, 0, 0.35)");
    x.fillText(t, px + 3, 122);
    x.fillStyle("#111");
    x.fillText(t, px, 118, 900);
  };
  draw(district, 130);
  const double dw = x.measureText(district).width;
  // the seals: the inspection's (rear only) above the district's
  const double sealX = 130 + dw + 52;
  if (rear) {
    x.fillStyle("#e8c32c");
    x.beginPath();
    x.arc(sealX, 72, 30, 0, PI * 2);
    x.fill();
    x.strokeStyle("#6d5a12");
    x.lineWidth(2);
    for (int i = 0; i < 12; i++) {
      const double a = (i / 12.0) * PI * 2;
      x.beginPath();
      x.moveTo(sealX + std::cos(a) * 18, 72 + std::sin(a) * 18);
      x.lineTo(sealX + std::cos(a) * 28, 72 + std::sin(a) * 28);
      x.stroke();
    }
  }
  x.fillStyle("#c9ccd0");
  x.beginPath();
  x.arc(sealX, rear ? 150 : 110, 30, 0, PI * 2);
  x.fill();
  x.fillStyle("#7d1f1f");
  x.fillRect(sealX - 9, (rear ? 150 : 110) - 14, 18, 22);
  draw(rest, sealX + 50);
  const TexturePtr t = canvasTexture(c);
  t->srgb = true;
  t->anisotropy = 8;
  return t;
}

}  // namespace fs

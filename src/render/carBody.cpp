// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/carBody.ts @ v1.7.7
#include "render/carBody.h"

#include <cmath>
#include <map>
#include <memory>
#include <set>

#include "core/js.h"
#include "engine/geometries.h"

namespace fs {

namespace {

struct Section {
  double z = 0;
  /** half width at the waist */
  double w = 0;
  /** underside */
  double yb = 0;
  /** widest crease */
  double ym = 0;
  /** belt line (window sill) or bonnet/boot edge height */
  double ybelt = 0;
  /** half width and height of the roof edge; equal to belt values where there is no cabin */
  double wr = 0;
  double yr = 0;
  /** crown height at the centre line */
  double yc = 0;
};

Section lerpSec(const Section& a, const Section& b, double t) {
  // smooth in z so the loft has no creases at the key sections
  const double s = t * t * (3 - 2 * t);
  const auto m = [](double p, double q, double u) { return p + (q - p) * u; };
  Section r;
  r.z = m(a.z, b.z, t);
  r.w = m(a.w, b.w, s);
  r.yb = m(a.yb, b.yb, s);
  r.ym = m(a.ym, b.ym, s);
  r.ybelt = m(a.ybelt, b.ybelt, t);
  r.wr = m(a.wr, b.wr, t);
  r.yr = m(a.yr, b.yr, t);
  r.yc = m(a.yc, b.yc, t);
  return r;
}

Vec3 sub3(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }

double dot3(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

Vec3 cross3(const Vec3& a, const Vec3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }

Vec3 centroid(const std::vector<Vec3>& ring) {
  Vec3 c = {0, 0, 0};
  const double n = static_cast<double>(ring.size());
  for (const Vec3& q : ring) {
    c[0] += q[0] / n;
    c[1] += q[1] / n;
    c[2] += q[2] / n;
  }
  return c;
}

/** Normal of a closed ring (Newell): points at the viewer who sees the ring turn counter-clockwise. */
Vec3 ringNormal(const std::vector<Vec3>& ring) {
  Vec3 n = {0, 0, 0};
  for (size_t j = 0; j < ring.size(); j++) {
    const Vec3& a = ring[j];
    const Vec3& b = ring[(j + 1) % ring.size()];
    n[0] += (a[1] - b[1]) * (a[2] + b[2]);
    n[1] += (a[2] - b[2]) * (a[0] + b[0]);
    n[2] += (a[0] - b[0]) * (a[1] + b[1]);
  }
  return n;
}

/** what the original keys its maps of places with: the three coordinates, rounded (`${x}_${y}_${z}`) */
using PlaceKey = std::array<int64_t, 3>;

PlaceKey placeKey(double x, double y, double z, double scale) {
  return {static_cast<int64_t>(jsRound(x * scale)), static_cast<int64_t>(jsRound(y * scale)), static_cast<int64_t>(jsRound(z * scale))};
}

}  // namespace

// ---------------------------------------------------------------- Sculpt ----

Sculpt& Sculpt::tri(const Vec3& a, const Vec3& b, const Vec3& c) {
  p_.insert(p_.end(), {a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]});
  return *this;
}

Sculpt& Sculpt::quad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) { return tri(a, b, c).tri(a, c, d); }

Sculpt& Sculpt::tube(const std::vector<std::vector<Vec3>>& rings, bool capStart, bool capEnd, bool inward) {
  const size_t n = rings[0].size();
  for (size_t i = 0; i + 1 < rings.size(); i++) {
    const std::vector<Vec3>& a = rings[i];
    const std::vector<Vec3>& b = rings[i + 1];
    // ring turning counter-clockwise when seen from where the sweep is heading → as listed
    const bool fwd = (dot3(ringNormal(a), sub3(centroid(b), centroid(a))) >= 0) != inward;
    for (size_t j = 0; j < n; j++) {
      const size_t k = (j + 1) % n;
      if (fwd) quad(a[j], a[k], b[k], b[j]);
      else quad(a[j], b[j], b[k], a[k]);
    }
  }
  const size_t last = rings.size() - 1;
  if (capStart) fan(rings[0], sub3(centroid(rings[0]), centroid(rings[1])));
  if (capEnd) fan(rings[last], sub3(centroid(rings[last]), centroid(rings[last - 1])));
  return *this;
}

Sculpt& Sculpt::sheet(const std::vector<std::vector<Vec3>>& rows, const Vec3& facing) {
  std::vector<std::array<Vec3, 4>> quads;
  Vec3 sum = {0, 0, 0};
  for (size_t i = 0; i + 1 < rows.size(); i++) {
    for (size_t j = 0; j + 1 < rows[i].size(); j++) {
      const std::array<Vec3, 4> q = {rows[i][j], rows[i][j + 1], rows[i + 1][j + 1], rows[i + 1][j]};
      const Vec3 nq = cross3(sub3(q[2], q[0]), sub3(q[3], q[1]));
      sum[0] += nq[0];
      sum[1] += nq[1];
      sum[2] += nq[2];
      quads.push_back(q);
    }
  }
  const bool flip = dot3(sum, facing) < 0;
  for (const std::array<Vec3, 4>& q : quads) {
    if (flip) quad(q[0], q[3], q[2], q[1]);
    else quad(q[0], q[1], q[2], q[3]);
  }
  return *this;
}

Sculpt& Sculpt::fan(const std::vector<Vec3>& ring, const Vec3& facing) {
  const Vec3 c = centroid(ring);
  const bool flip = dot3(ringNormal(ring), facing) < 0;
  for (size_t j = 0; j < ring.size(); j++) {
    const Vec3& a = ring[j];
    const Vec3& b = ring[(j + 1) % ring.size()];
    if (flip) tri(c, b, a);
    else tri(c, a, b);
  }
  return *this;
}

GeometryPtr Sculpt::build(double creaseDeg) const {
  const std::vector<double>& src = p_;
  // drop slivers without area: they have no normal
  std::vector<double> pos;
  std::vector<double> faceN;
  for (size_t i = 0; i + 8 < src.size(); i += 9) {
    const Vec3 a = {src[i], src[i + 1], src[i + 2]};
    const Vec3 b = {src[i + 3], src[i + 4], src[i + 5]};
    const Vec3 c = {src[i + 6], src[i + 7], src[i + 8]};
    const Vec3 nrm = cross3(sub3(b, a), sub3(c, a));
    const double len = std::hypot(nrm[0], nrm[1], nrm[2]);
    if (len < 1e-10) continue;
    pos.insert(pos.end(), {a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]});
    faceN.insert(faceN.end(), {nrm[0] / len, nrm[1] / len, nrm[2] / len});
  }
  const size_t corners = pos.size() / 3;
  // corners that share a position, and the angle each face spans there (its weight)
  std::map<PlaceKey, std::vector<size_t>> at;
  std::vector<float> weight(corners);
  for (size_t c = 0; c < corners; c++) {
    at[placeKey(pos[c * 3], pos[c * 3 + 1], pos[c * 3 + 2], 1e4)].push_back(c);
    const size_t f = c - (c % 3);
    const Vec3 o = {pos[c * 3], pos[c * 3 + 1], pos[c * 3 + 2]};
    const size_t c1 = f + ((c - f + 1) % 3);
    const size_t c2 = f + ((c - f + 2) % 3);
    const Vec3 u = sub3({pos[c1 * 3], pos[c1 * 3 + 1], pos[c1 * 3 + 2]}, o);
    const Vec3 v = sub3({pos[c2 * 3], pos[c2 * 3 + 1], pos[c2 * 3 + 2]}, o);
    const double lengths = std::hypot(u[0], u[1], u[2]) * std::hypot(v[0], v[1], v[2]);
    const double d = dot3(u, v) / (lengths != 0 && lengths == lengths ? lengths : 1);
    weight[c] = static_cast<float>(std::acos(jsMax(-1, jsMin(1, d))));
  }
  const double cosCrease = std::cos((creaseDeg * PI) / 180);
  std::vector<float> normal(pos.size());
  for (const auto& entry : at) {
    const std::vector<size_t>& list = entry.second;
    for (const size_t c : list) {
      const size_t f = (c / 3) * 3;
      double x = 0;
      double y = 0;
      double z = 0;
      for (const size_t o : list) {
        const size_t g = (o / 3) * 3;
        if (faceN[f] * faceN[g] + faceN[f + 1] * faceN[g + 1] + faceN[f + 2] * faceN[g + 2] < cosCrease) continue;
        x += faceN[g] * weight[o];
        y += faceN[g + 1] * weight[o];
        z += faceN[g + 2] * weight[o];
      }
      const double hyp = std::hypot(x, y, z);
      const double len = hyp != 0 && hyp == hyp ? hyp : 1;
      normal[c * 3] = static_cast<float>(x / len);
      normal[c * 3 + 1] = static_cast<float>(y / len);
      normal[c * 3 + 2] = static_cast<float>(z / len);
    }
  }
  GeometryPtr g = make<BufferGeometry>();
  g->setAttribute("position", attr(std::vector<float>(pos.begin(), pos.end()), 3));
  g->setAttribute("normal", attr(std::move(normal), 3));
  return g;
}

// ------------------------------------------------------------------ Geo ----

namespace {

GeometryPtr mergeByPosition(const BufferGeometry& g) {
  const AttrPtr pos = g.getAttribute("position");
  std::map<PlaceKey, uint32_t> map;
  std::vector<float> out;
  std::vector<uint32_t> remap(pos->count());
  for (size_t i = 0; i < pos->count(); i++) {
    const PlaceKey key = placeKey(pos->getX(i), pos->getY(i), pos->getZ(i), 2000);
    auto found = map.find(key);
    if (found == map.end()) {
      found = map.emplace(key, static_cast<uint32_t>(out.size() / 3)).first;
      out.insert(out.end(), {static_cast<float>(pos->getX(i)), static_cast<float>(pos->getY(i)), static_cast<float>(pos->getZ(i))});
    }
    remap[i] = found->second;
  }
  const AttrPtr idx = g.index;
  std::vector<uint32_t> ni;
  for (size_t i = 0; i + 2 < idx->count(); i += 3) {
    const uint32_t a = remap[static_cast<size_t>(idx->getX(i))];
    const uint32_t b = remap[static_cast<size_t>(idx->getX(i + 1))];
    const uint32_t c = remap[static_cast<size_t>(idx->getX(i + 2))];
    if (a != b && b != c && a != c) ni.insert(ni.end(), {a, b, c});
  }
  GeometryPtr m = make<BufferGeometry>();
  m->setAttribute("position", attr(std::move(out), 3));
  m->setIndex(ni);
  return m;
}

class Geo {
 public:
  std::vector<double> pos;
  std::vector<uint32_t> idx;

  uint32_t v(double x, double y, double z) {
    pos.insert(pos.end(), {x, y, z});
    return static_cast<uint32_t>(pos.size() / 3 - 1);
  }

  /** Quad a,b,c,d wound so that its normal points along (nx,ny,nz). */
  void quad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, double nx, double ny, double nz) {
    const double ux = b[0] - a[0];
    const double uy = b[1] - a[1];
    const double uz = b[2] - a[2];
    const double vx = d[0] - a[0];
    const double vy = d[1] - a[1];
    const double vz = d[2] - a[2];
    const double cx = uy * vz - uz * vy;
    const double cy = uz * vx - ux * vz;
    const double cz = ux * vy - uy * vx;
    const bool flip = cx * nx + cy * ny + cz * nz < 0;
    const uint32_t i0 = v(a[0], a[1], a[2]);
    const uint32_t i1 = v(b[0], b[1], b[2]);
    const uint32_t i2 = v(c[0], c[1], c[2]);
    const uint32_t i3 = v(d[0], d[1], d[2]);
    if (flip) idx.insert(idx.end(), {i0, i2, i1, i0, i3, i2});
    else idx.insert(idx.end(), {i0, i1, i2, i0, i2, i3});
  }

  /** Triangle wound so that its normal points along (nx,ny,nz). */
  void tri(const Vec3& a, const Vec3& b, const Vec3& c, double nx, double ny, double nz) {
    const double ux = b[0] - a[0];
    const double uy = b[1] - a[1];
    const double uz = b[2] - a[2];
    const double vx = c[0] - a[0];
    const double vy = c[1] - a[1];
    const double vz = c[2] - a[2];
    const double cx = uy * vz - uz * vy;
    const double cy = uz * vx - ux * vz;
    const double cz = ux * vy - uy * vx;
    const uint32_t i0 = v(a[0], a[1], a[2]);
    const uint32_t i1 = v(b[0], b[1], b[2]);
    const uint32_t i2 = v(c[0], c[1], c[2]);
    if (cx * nx + cy * ny + cz * nz < 0) idx.insert(idx.end(), {i0, i2, i1});
    else idx.insert(idx.end(), {i0, i1, i2});
  }

  GeometryPtr build(bool smooth = true) const {
    GeometryPtr g = make<BufferGeometry>();
    g->setAttribute("position", attr(std::vector<float>(pos.begin(), pos.end()), 3));
    g->setIndex(idx);
    if (smooth) {
      // weld coincident vertices so the loft shades smoothly
      GeometryPtr merged = mergeByPosition(*g);
      merged->computeVertexNormals();
      return merged;
    }
    g->computeVertexNormals();
    return g;
  }
};

/**
 * The key cross-sections of a body and how to find the one at any z – what
 * the shell is lofted through (buildCarShell), and what parts fixed to its
 * surface are placed by (bodyProfile).
 */
struct Layout {
  CarDims d;
  double hw = 0;
  double zF = 0;
  double zR = 0;
  double zAF = 0;
  double zAR = 0;
  double H = 0;
  double belt = 0;
  double zCowl = 0;
  double zRoofF = 0;
  double zRoofR = 0;
  double zTailTop = 0;
  double cowlTop = 0;
  double cowlCrown = 0;
  std::vector<Section> keys;
  double Ra = 0;
  double zB = 0;

  /** The cross-section at any z, blended between the key sections around it. */
  Section sectionAt(double z) const {
    size_t i = 1;
    while (i < keys.size() - 1 && keys[i].z < z) i++;
    const Section& a = keys[i - 1];
    const Section& b = keys[i];
    Section s = lerpSec(a, b, jsMax(0, jsMin(1, (z - a.z) / (b.z - a.z))));
    s.z = z;
    return s;
  }

  /** Right-hand profile points of a station, bottom centre → roof centre. */
  std::vector<Vec3> profile(const Section& s) const {
    double arch = 0;
    for (const double za : {zAF, zAR}) {
      const double dz = s.z - za;
      if (std::fabs(dz) < Ra) arch = jsMax(arch, d.wheelRadius * 0.93 + std::sqrt(Ra * Ra - dz * dz) - 0.02);
    }
    const double low = jsMax(s.yb, arch);
    const double rocker = jsMax(s.yb + 0.13, arch);
    const double ym = jsMax(s.ym, rocker + 0.02);
    return {
        {0, s.yb, s.z},
        {s.w * 0.8, low, s.z},
        {s.w * 0.985, rocker, s.z},
        {s.w, ym, s.z},
        {s.w * 0.975, jsMax(s.ybelt, ym + 0.02), s.z},
        {s.wr, s.yr, s.z},
        {0, s.yc, s.z},
    };
  }
};

Layout layout(const CarDims& d) {
  Layout L;
  L.d = d;
  const double hw = L.hw = d.width / 2;
  const double zF = L.zF = -(d.wheelbase / 2 + d.frontOverhang);
  const double zR = L.zR = zF + d.length;
  const double zAF = L.zAF = -d.wheelbase / 2;
  const double zAR = L.zAR = d.wheelbase / 2;
  const double H = L.H = d.height;
  const double belt = L.belt = H * 0.655;
  const double zCowl = L.zCowl = zAF + 0.36;
  const double zRoofF = L.zRoofF = zCowl + 0.74;
  const double zRoofR = L.zRoofR = d.tail == 1 ? zR - 0.42 : d.tail == 2 ? zAR - 0.25 : zAR - 0.02;
  const double zTailTop = L.zTailTop = d.tail == 1 ? zR - 0.12 : d.tail == 2 ? zAR + 0.38 : zR - 0.32;
  const double bootY = d.tail == 2 ? belt + 0.04 : belt + 0.02;

  const auto noCabin = [](double z, double w, double yb, double top, double crown = 0.03) {
    Section s;
    s.z = z;
    s.w = w;
    s.yb = yb;
    s.ym = yb + (top - yb) * 0.55;
    s.ybelt = top - 0.015;
    s.wr = w * 0.86;
    s.yr = top;
    s.yc = top + crown;
    return s;
  };
  const auto cabin = [hw](double z, double wr, double yr, double yc, double ybelt) {
    Section s;
    s.z = z;
    s.w = hw;
    s.yb = 0.2;
    s.ym = 0.63;
    s.ybelt = ybelt;
    s.wr = wr;
    s.yr = yr;
    s.yc = yc;
    return s;
  };
  // The bonnet falls away to the front and the windscreen starts low, below the
  // window sills: from the driver's seat the base of the screen then lies under
  // the line of sight over the bonnet, and the road shows from about 6 m on.
  const double cowlTop = L.cowlTop = belt - 0.03;
  const double cowlCrown = L.cowlCrown = 0.03;
  Section cowl = noCabin(zCowl, hw, 0.2, cowlTop, cowlCrown);
  cowl.wr = hw * 0.9;
  Section tailTop = noCabin(zTailTop, hw * 0.985, 0.22, bootY + 0.01, 0.02);
  tailTop.wr = hw * 0.82;
  L.keys = {
      noCabin(zF, hw * 0.8, 0.3, 0.66, 0.01),
      noCabin(zF + 0.09, hw * 0.93, 0.21, 0.72, 0.02),
      noCabin(zF + 0.36, hw * 0.985, 0.19, 0.785),
      noCabin(zAF, hw, 0.2, 0.88, 0.035),
      cowl,
      cabin(zRoofF, hw * 0.74, H - 0.055, H - 0.004, belt),
      cabin((zRoofF + zRoofR) / 2, hw * 0.735, H - 0.045, H, belt),
      cabin(zRoofR, hw * 0.72, H - 0.07, H - 0.022, belt + 0.02),
      tailTop,
      noCabin(zR - 0.1, hw * 0.94, 0.25, bootY - 0.05, 0.015),
      noCabin(zR, hw * 0.72, 0.33, bootY - 0.2, 0.005),
  };

  L.Ra = d.wheelRadius + 0.075;
  L.zB = (zRoofF + zRoofR) / 2 - 0.02;   // B pillar
  return L;
}

}  // namespace

std::function<std::vector<std::array<double, 2>>(double z)> bodyProfile(const CarDims& d) {
  const std::shared_ptr<const Layout> L = std::make_shared<const Layout>(layout(d));
  return [L](double z) {
    std::vector<std::array<double, 2>> out;
    for (const Vec3& q : L->profile(L->sectionAt(z))) out.push_back({q[0], q[1]});
    return out;
  };
}

CarShell buildCarShell(const CarDims& d) {
  const Layout L = layout(d);
  const double hw = L.hw, zF = L.zF, zR = L.zR, zAF = L.zAF, zAR = L.zAR, H = L.H, belt = L.belt, zCowl = L.zCowl, zRoofF = L.zRoofF, zRoofR = L.zRoofR,
               zTailTop = L.zTailTop, cowlTop = L.cowlTop, cowlCrown = L.cowlCrown, Ra = L.Ra, zB = L.zB;
  const std::vector<Section>& keys = L.keys;
  const auto sectionAt = [&L](double z) { return L.sectionAt(z); };
  const auto profile = [&L](const Section& s) { return L.profile(s); };
  // dense station list: key sections, pillar edges and wheel-arch samples
  // (a Set of numbers, sorted afterwards: std::set keeps each once and in order)
  std::set<double> zs;
  for (const Section& k : keys) zs.insert(k.z);
  for (const double za : {zAF, zAR}) {
    for (const double f : {-1.0, -0.92, -0.75, -0.5, -0.25, 0.0, 0.25, 0.5, 0.75, 0.92, 1.0}) zs.insert(za + f * Ra);
  }
  for (const double z : {zB - 0.045, zB + 0.045, zRoofR - 0.17}) zs.insert(z);
  for (size_t i = 1; i < keys.size(); i++) {
    const double a = keys[i - 1].z;
    const double b = keys[i].z;
    const double n = std::ceil((b - a) / 0.22);
    for (double k = 1; k < n; k++) zs.insert(a + ((b - a) * k) / n);
  }
  std::vector<double> zList;
  for (const double z : zs)
    if (z >= zF - 1e-6 && z <= zR + 1e-6) zList.push_back(z);
  std::vector<Section> stations;
  std::set<double> keyZ;
  for (const Section& k : keys) keyZ.insert(k.z);
  for (const double z : zList) {
    if (!stations.empty() && z - stations.back().z < 0.012) {
      // two stations nearly on top of each other: keep the key section (glass
      // and pillars start exactly there), otherwise the first one
      if (keyZ.count(z) && !keyZ.count(stations.back().z)) stations.pop_back();
      else continue;
    }
    stations.push_back(sectionAt(z));
  }

  Geo paint;
  Geo glass;
  Geo trim;
  Geo lining;
  Geo headliner;
  const auto mirrorX = [](const Vec3& p) -> Vec3 { return {-p[0], p[1], p[2]}; };
  const double eps = 1e-4;
  // cabin lining: a panel pulled 2.5 cm inwards, facing in (the floor is raised a little)
  const auto inset = [](const Vec3& p, int k) -> Vec3 {
    if (k == 0) return {p[0], p[1] + 0.03, p[2]};
    return {p[0] - jsSign(p[0]) * 0.025, p[1] - (k == 5 ? 0.025 : 0), p[2]};
  };

  for (size_t i = 0; i + 1 < stations.size(); i++) {
    const std::vector<Vec3> A = profile(stations[i]);
    const std::vector<Vec3> B = profile(stations[i + 1]);
    const double zm = (stations[i].z + stations[i + 1].z) / 2;
    const bool inCabin = zm > zCowl + eps && zm < zTailTop - eps;
    const bool underRoof = zm > zRoofF - eps && zm < zRoofR + eps;
    for (int k = 0; k < 6; k++) {
      // outward hint: sideways for the flanks, down for the floor, up for the top.  The sill
      // panel lies flat where it bridges a wheel arch: there "out" is down, towards the wheel.
      const double nx = k == 0 ? 0 : k == 5 ? 0.25 : 1;
      const double ny = k == 0 ? -1 : k == 5 ? 1 : k == 4 ? 0.45 : k == 1 ? -0.35 : 0;
      Geo* target = &paint;
      bool glazed = false;
      if (k == 0) target = &trim;
      else if (k == 4 && inCabin) {
        // side glazing with A, B and C pillars left in body colour
        const bool pillarB = std::fabs(zm - zB) < 0.045;
        const bool pillarC = zm > zRoofR - 0.17;
        const bool pillarA = zm < zRoofF + 0.02;
        if (!pillarA && !pillarB && !pillarC) glazed = true;
        // glass also beside the A pillar, down to where the gap to the sill closes
        if (pillarA && zm > zCowl + 0.12) glazed = true;
      } else if (k == 5 && inCabin && !underRoof) {
        glazed = true;   // windscreen and rear window
      }
      if (glazed) target = &glass;
      for (const int side : {1, -1}) {
        const Vec3 a0 = side > 0 ? A[k] : mirrorX(A[k]);
        const Vec3 a1 = side > 0 ? A[k + 1] : mirrorX(A[k + 1]);
        const Vec3 b0 = side > 0 ? B[k] : mirrorX(B[k]);
        const Vec3 b1 = side > 0 ? B[k + 1] : mirrorX(B[k + 1]);
        target->quad(a0, a1, b1, b0, nx * side, ny, 0);
        if (!glazed) {
          Geo& inner = inCabin && k >= 4 ? headliner : lining;
          inner.quad(inset(a0, k), inset(a1, k), inset(b1, k), inset(b0, k), -nx * side, -ny, 0);
        }
      }
    }
  }
  // nose and tail caps; inside, a bulkhead closes the cabin towards the engine and behind the boot
  // (taken at their exact places, not looked up among the stations: a station that was
  // dropped for lying too close to its neighbour must not move a pillar to the car's nose)
  const Section cowlStation = sectionAt(zCowl);
  const Section roofStation = sectionAt(zRoofF);
  const Section first = stations.front();
  const Section last = stations.back();
  struct Cap {
    const Section* s;
    double nz;
    bool inside;
  };
  for (const Cap& cap : {Cap{&first, -1, false}, Cap{&last, 1, false}, Cap{&cowlStation, 1, true}, Cap{&last, -1, true}}) {
    const Section& s = *cap.s;
    const double nz = cap.nz;
    const bool inside = cap.inside;
    const std::vector<Vec3> P = profile(s);
    const double cy = (s.yb + s.yc) / 2;
    // the inner panels stand a little inside the opening they close
    const double z = inside ? s.z + nz * 0.012 : s.z;
    for (int k = 0; k < 6; k++) {
      for (const int side : {1, -1}) {
        const Vec3 p0 = side > 0 ? P[k] : mirrorX(P[k]);
        const Vec3 p1 = side > 0 ? P[k + 1] : mirrorX(P[k + 1]);
        if (inside) {
          const Vec3 q0 = inset(p0, k == 0 ? 0 : 1);
          const Vec3 q1 = inset(p1, 1);
          lining.tri({0, cy, z}, {q0[0], q0[1], z}, {q1[0], q1[1], z}, 0, 0, nz);
        } else paint.tri({0, cy, z}, p0, p1, 0, 0, nz);
      }
    }
  }

  // ---- A pillars -------------------------------------------------------------
  // Slim wedges along the edges of the windscreen: two narrow faces lie on the
  // windscreen and on the side glass, the trim between them comes to a rounded
  // edge that points at the occupant on that side, who therefore looks at the
  // pillar end-on and loses as little of the view as a pillar can cost.
  const std::vector<Vec3> pc = profile(cowlStation);
  const std::vector<Vec3> pr = profile(roofStation);
  Sculpt pillarOut;
  Sculpt pillarIn;
  const auto lerp3 = [](const Vec3& a, const Vec3& b, double t, double side) {
    return Vector3(side * (a[0] + (b[0] - a[0]) * t), a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t);
  };
  const auto v3 = [](const Vector3& v) -> Vec3 { return {v.x, v.y, v.z}; };
  for (const double side : {1.0, -1.0}) {
    const Vector3 axis = lerp3(pc[5], pr[5], 1, side).sub(lerp3(pc[5], pr[5], 0, side)).normalize();
    // across the pillar: the part of `v` at right angles to it
    const auto across = [&axis](Vector3 v) { return v.addScaledVector(axis, -v.dot(axis)).normalize(); };
    const Vector3 head(side * hw * 0.4, H - 0.26, zRoofF + 0.42);
    std::vector<std::vector<Vec3>> outer;
    std::vector<std::vector<Vec3>> inner;
    const int steps = 6;
    for (int i = 0; i <= steps; i++) {
      // a little beyond both ends, so the pillar disappears into scuttle and roof
      const double t = -0.05 + (1.1 * i) / steps;
      const Vector3 edge = lerp3(pc[5], pr[5], t, side);
      const Vector3 toMiddle = across(lerp3(pc[6], pr[6], t, side).sub(edge));
      const Vector3 toSill = across(lerp3(pc[4], pr[4], t, side).sub(edge));
      const Vector3 toHead = across(head.clone().sub(edge));
      // stouter at the foot and where it joins the roof
      const double k = 1 + 0.55 * std::pow(1 - t, 3) + 0.25 * std::pow(t, 3);
      const Vector3 corner = edge.clone().addScaledVector(toHead, -0.007);
      const Vector3 onScreen = edge.clone().addScaledVector(toMiddle, 0.021 * k).addScaledVector(toHead, -0.004);
      const Vector3 onSide = edge.clone().addScaledVector(toSill, 0.025 * k).addScaledVector(toHead, -0.004);
      const Vector3 flankScreen = edge.clone().addScaledVector(toMiddle, 0.017 * k).addScaledVector(toHead, 0.02 * k);
      const Vector3 flankSide = edge.clone().addScaledVector(toSill, 0.02 * k).addScaledVector(toHead, 0.02 * k);
      const Vector3 nose = edge.clone().addScaledVector(toMiddle, 0.005 * k).addScaledVector(toSill, 0.006 * k).addScaledVector(toHead, 0.038 * k);
      outer.push_back({v3(onSide), v3(corner), v3(onScreen)});
      inner.push_back({v3(onScreen), v3(flankScreen), v3(nose), v3(flankSide), v3(onSide)});
    }
    pillarOut.sheet(outer, {side, 0.3, -0.4});
    pillarIn.sheet(inner, {-side, -0.3, 0.6});
  }

  // outlines of the flat end faces, for whatever is fixed to the bumpers
  const auto outline = [&profile](const Section& s) {
    std::vector<std::array<double, 2>> out;
    for (const Vec3& q : profile(s)) out.push_back({q[0], q[1]});
    return out;
  };

  CarShell sh;
  sh.paint = paint.build();
  sh.glass = glass.build();
  sh.trim = trim.build();
  sh.lining = lining.build();
  sh.headliner = headliner.build();
  sh.pillars = pillarOut.build(50);
  sh.pillarTrim = pillarIn.build(70);
  sh.zFront = zF;
  sh.zRear = zR;
  sh.zAxleF = zAF;
  sh.zAxleR = zAR;
  sh.zCowl = zCowl;
  sh.zRoofFront = zRoofF;
  sh.zRoofRear = zRoofR;
  sh.zPillarB = zB;
  sh.zTail = zTailTop;
  sh.halfWidth = hw;
  sh.beltY = belt;
  sh.roofY = H;
  sh.cowlY = cowlTop;
  sh.cowlCrownY = cowlTop + cowlCrown;
  sh.noseOutline = outline(first);
  sh.tailOutline = outline(last);
  return sh;
}

WheelGeometry buildWheel(const CarDims& d) {
  const double r = d.wheelRadius;
  const double w = d.tyreWidth;
  GeometryPtr tyre = cylinderGeometry(r, r, w, 20, 1, false);
  tyre->rotateZ(PI / 2);
  GeometryPtr disc = cylinderGeometry(r * 0.66, r * 0.66, w * 1.02, 14, 1, false);
  disc->rotateZ(PI / 2);
  // five chunky spokes stand proud of the disc so the rotation is visible
  std::vector<GeometryPtr> spokes = {disc};
  for (int i = 0; i < 5; i++) {
    GeometryPtr s = boxGeometry(w * 1.06, r * 0.6, r * 0.16);
    s->translate(0, r * 0.34, 0).rotateX((i / 5.0) * PI * 2);
    spokes.push_back(s);
  }
  std::vector<float> pos;
  std::vector<uint32_t> idx;
  for (const GeometryPtr& sg : spokes) {
    const AttrPtr p = sg->getAttribute("position");
    const uint32_t base = static_cast<uint32_t>(pos.size() / 3);
    for (size_t i = 0; i < p->count(); i++) pos.insert(pos.end(), {static_cast<float>(p->getX(i)), static_cast<float>(p->getY(i)), static_cast<float>(p->getZ(i))});
    const AttrPtr ix = sg->index;
    for (size_t i = 0; i < ix->count(); i++) idx.push_back(base + static_cast<uint32_t>(ix->getX(i)));
  }
  GeometryPtr rim = make<BufferGeometry>();
  rim->setAttribute("position", attr(std::move(pos), 3));
  rim->setIndex(idx);
  rim->computeVertexNormals();
  tyre->deleteAttribute("uv");
  return {tyre, rim};
}

}  // namespace fs

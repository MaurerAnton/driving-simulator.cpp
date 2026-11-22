// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The car shell: a lofted body built from cross-sections (stations), split
 * into paint / glass / trim / lining / headliner, plus the slim A pillars,
 * the hand-built parts of the cabin (Sculpt) and the wheels.
 *
 * Origin: src/render/carBody.ts (MaurerAnton/driving-simulator @ ff37c5d,
 * v0.7.0).  The station list, the profile points, the panel rules and the
 * pillar wedges are carried over exactly, including which panels are glazed.
 */
#include "render/carBody.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <map>
#include <set>

namespace fs {

const CarDims COMPACT_DIMS = {4.28, 1.79, 1.46, 2.63, 1.54, 0.316, 0.205, 0.88, 0};

namespace {

double m(double a, double b, double s) { return a + (b - a) * s; }

Section lerpSec(const Section& a, const Section& b, double s) {
  Section r;
  r.z = a.z;
  r.w = m(a.w, b.w, s);
  r.yb = m(a.yb, b.yb, s);
  r.ym = m(a.ym, b.ym, s);
  r.ybelt = m(a.ybelt, b.ybelt, s);
  r.wr = m(a.wr, b.wr, s);
  r.yr = m(a.yr, b.yr, s);
  r.yc = m(a.yc, b.yc, s);
  return r;
}

/** Accumulates positions and triangles, winding them to a wanted normal. */
class Geo {
 public:
  std::vector<double> pos;
  std::vector<double> idx;

  size_t v(double x, double y, double z) {
    pos.push_back(x);
    pos.push_back(y);
    pos.push_back(z);
    return pos.size() / 3 - 1;
  }

  /** Quad a,b,c,d wound so that its normal points along (nx,ny,nz). */
  void quad(const double a[3], const double b[3], const double c[3], const double d[3],
            double nx, double ny, double nz) {
    double ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    double vx = d[0] - a[0], vy = d[1] - a[1], vz = d[2] - a[2];
    double cx = uy * vz - uz * vy;
    double cy = uz * vx - ux * vz;
    double cz = ux * vy - uy * vx;
    bool flip = cx * nx + cy * ny + cz * nz < 0;
    size_t i0 = v(a[0], a[1], a[2]), i1 = v(b[0], b[1], b[2]);
    size_t i2 = v(c[0], c[1], c[2]), i3 = v(d[0], d[1], d[2]);
    if (flip) {
      push(i0, i2, i1);
      push(i0, i3, i2);
    } else {
      push(i0, i1, i2);
      push(i0, i2, i3);
    }
  }

  /** Triangle wound so that its normal points along (nx,ny,nz). */
  void tri(const double a[3], const double b[3], const double c[3],
           double nx, double ny, double nz) {
    double ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    double vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    double cx = uy * vz - uz * vy;
    double cy = uz * vx - ux * vz;
    double cz = ux * vy - uy * vx;
    size_t i0 = v(a[0], a[1], a[2]), i1 = v(b[0], b[1], b[2]), i2 = v(c[0], c[1], c[2]);
    if (cx * nx + cy * ny + cz * nz < 0) push(i0, i2, i1);
    else push(i0, i1, i2);
  }

  /** Weld coincident vertices so the loft shades smoothly (three.js mergeByPosition). */
  GeometryPtr build(bool smooth = true) const {
    auto g = std::make_shared<BufferGeometry>();
    if (smooth) {
      std::map<std::string, size_t> map;
      std::vector<double> out;
      std::vector<size_t> remap(pos.size() / 3);
      char key[64];
      for (size_t i = 0; i < pos.size() / 3; i++) {
        std::snprintf(key, sizeof key, "%.3f_%.3f_%.3f", std::round(pos[i * 3] * 2000),
                      std::round(pos[i * 3 + 1] * 2000), std::round(pos[i * 3 + 2] * 2000));
        auto it = map.find(key);
        size_t id;
        if (it == map.end()) {
          id = out.size() / 3;
          out.push_back(pos[i * 3]);
          out.push_back(pos[i * 3 + 1]);
          out.push_back(pos[i * 3 + 2]);
          map[key] = id;
        } else {
          id = it->second;
        }
        remap[i] = id;
      }
      std::vector<double> ni;
      for (size_t i = 0; i < idx.size(); i += 3) {
        size_t a = remap[static_cast<size_t>(idx[i])];
        size_t b = remap[static_cast<size_t>(idx[i + 1])];
        size_t c = remap[static_cast<size_t>(idx[i + 2])];
        if (a != b && b != c && a != c) {
          ni.push_back(static_cast<double>(a));
          ni.push_back(static_cast<double>(b));
          ni.push_back(static_cast<double>(c));
        }
      }
      g->position = std::make_shared<BufferAttribute>(out, 3);
      g->index = std::make_shared<BufferAttribute>(ni, 1);
    } else {
      g->position = std::make_shared<BufferAttribute>(pos, 3);
      g->index = std::make_shared<BufferAttribute>(idx, 1);
    }
    g->computeVertexNormals();
    return g;
  }

 private:
  void push(size_t a, size_t b, size_t c) {
    idx.push_back(static_cast<double>(a));
    idx.push_back(static_cast<double>(b));
    idx.push_back(static_cast<double>(c));
  }
};

Section noCabin(double z, double w, double yb, double top, double crown = 0.03) {
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
}

Section cabinSec(double z, double wr, double yr, double yc, double ybelt) {
  Section s;
  s.z = z;
  s.w = 1;   // filled in by the caller (half width)
  s.yb = 0.2;
  s.ym = 0.63;
  s.ybelt = ybelt;
  s.wr = wr;
  s.yr = yr;
  s.yc = yc;
  return s;
}

/** Right-hand profile points of a station, bottom centre → roof centre. */
std::vector<std::array<double, 3>> profileOf(const Section& s, const CarDims& d, double zAF,
                                             double zAR, double Ra) {
  double arch = 0;
  for (double za : {zAF, zAR}) {
    double dz = s.z - za;
    if (std::fabs(dz) < Ra)
      arch = std::max(arch, d.wheelRadius * 0.93 + std::sqrt(Ra * Ra - dz * dz) - 0.02);
  }
  double low = std::max(s.yb, arch);
  double rocker = std::max(s.yb + 0.13, arch);
  double ym = std::max(s.ym, rocker + 0.02);
  return {{0, s.yb, s.z},
          {s.w * 0.8, low, s.z},
          {s.w * 0.985, rocker, s.z},
          {s.w, ym, s.z},
          {s.w * 0.975, std::max(s.ybelt, ym + 0.02), s.z},
          {s.wr, s.yr, s.z},
          {0, s.yc, s.z}};
}

std::array<double, 3> mirrorX(const std::array<double, 3>& p) {
  return {-p[0], p[1], p[2]};
}
}  // namespace
// ---------------------------------------------------------------------------
// Vec3 helpers (sub3/dot3/cross3/centroid/ringNormal) and the Sculpt class:
// how every hand-built part of the cabin is collected and shaded.
// ---------------------------------------------------------------------------

std::array<double, 3> sub3(const std::array<double, 3>& a, const std::array<double, 3>& b) {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

double dot3(const std::array<double, 3>& a, const std::array<double, 3>& b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

std::array<double, 3> cross3(const std::array<double, 3>& a, const std::array<double, 3>& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

std::array<double, 3> centroid(const std::vector<std::array<double, 3>>& ring) {
  std::array<double, 3> c = {0, 0, 0};
  for (const auto& q : ring) {
    c[0] += q[0] / static_cast<double>(ring.size());
    c[1] += q[1] / static_cast<double>(ring.size());
    c[2] += q[2] / static_cast<double>(ring.size());
  }
  return c;
}

/** Normal of a closed ring (Newell): points at the viewer who sees the ring turn counter-clockwise. */
std::array<double, 3> ringNormal(const std::vector<std::array<double, 3>>& ring) {
  std::array<double, 3> n = {0, 0, 0};
  for (size_t j = 0; j < ring.size(); j++) {
    const auto& a = ring[j];
    const auto& b = ring[(j + 1) % ring.size()];
    n[0] += (a[1] - b[1]) * (a[2] + b[2]);
    n[1] += (a[2] - b[2]) * (a[0] + b[0]);
    n[2] += (a[0] - b[0]) * (a[1] + b[1]);
  }
  return n;
}

// --- Sculpt -----------------------------------------------------------------

void Sculpt::tri(const double a[3], const double b[3], const double c[3]) {
  for (int k = 0; k < 3; k++) p_.push_back(a[k]);
  for (int k = 0; k < 3; k++) p_.push_back(b[k]);
  for (int k = 0; k < 3; k++) p_.push_back(c[k]);
}

void Sculpt::quad(const double a[3], const double b[3], const double c[3], const double d[3]) {
  tri(a, b, c);
  tri(a, c, d);
}

void Sculpt::tube(const std::vector<std::vector<std::array<double, 3>>>& rings, bool capStart,
                  bool capEnd, bool inward) {
  size_t n = rings[0].size();
  for (size_t i = 0; i + 1 < rings.size(); i++) {
    const auto& a = rings[i];
    const auto& b = rings[i + 1];
    // ring turning counter-clockwise when seen from where the sweep is heading → as listed
    bool fwd = dot3(ringNormal(a), sub3(centroid(b), centroid(a))) >= 0 != inward;
    for (size_t j = 0; j < n; j++) {
      size_t k = (j + 1) % n;
      if (fwd)
        quad(a[j].data(), a[k].data(), b[k].data(), b[j].data());
      else
        quad(a[j].data(), b[j].data(), b[k].data(), a[k].data());
    }
  }
  size_t last = rings.size() - 1;
  if (capStart)
    fan(rings[0], sub3(centroid(rings[0]), centroid(rings[1])).data());
  if (capEnd)
    fan(rings[last], sub3(centroid(rings[last]), centroid(rings[last - 1])).data());
}

void Sculpt::sheet(const std::vector<std::vector<std::array<double, 3>>>& rows,
                   const double facing[3]) {
  struct Quad {
    std::array<double, 3> q[4];
    std::array<double, 3> nq;
  };
  std::vector<Quad> quads;
  std::array<double, 3> sum = {0, 0, 0};
  for (size_t i = 0; i + 1 < rows.size(); i++) {
    for (size_t j = 0; j + 1 < rows[i].size(); j++) {
      Quad q;
      q.q[0] = rows[i][j];
      q.q[1] = rows[i][j + 1];
      q.q[2] = rows[i + 1][j + 1];
      q.q[3] = rows[i + 1][j];
      q.nq = cross3(sub3(q.q[2], q.q[0]), sub3(q.q[3], q.q[1]));
      sum[0] += q.nq[0];
      sum[1] += q.nq[1];
      sum[2] += q.nq[2];
      quads.push_back(q);
    }
  }
  bool flip = dot3(sum, {facing[0], facing[1], facing[2]}) < 0;
  for (const Quad& q : quads) {
    if (flip)
      quad(q.q[0].data(), q.q[3].data(), q.q[2].data(), q.q[1].data());
    else
      quad(q.q[0].data(), q.q[1].data(), q.q[2].data(), q.q[3].data());
  }
}

void Sculpt::fan(const std::vector<std::array<double, 3>>& ring, const double facing[3]) {
  auto c = centroid(ring);
  bool flip = dot3(ringNormal(ring), {facing[0], facing[1], facing[2]}) < 0;
  for (size_t j = 0; j < ring.size(); j++) {
    const auto& a = ring[j];
    const auto& b = ring[(j + 1) % ring.size()];
    if (flip)
      tri(c.data(), b.data(), a.data());
    else
      tri(c.data(), a.data(), b.data());
  }
}

GeometryPtr Sculpt::build(double creaseDeg) const {
  const std::vector<double>& src = p_;
  // drop slivers without area: they have no normal
  std::vector<double> pos;
  std::vector<std::array<double, 3>> faceN;
  for (size_t i = 0; i + 9 <= src.size(); i += 9) {
    std::array<double, 3> a = {src[i], src[i + 1], src[i + 2]};
    std::array<double, 3> b = {src[i + 3], src[i + 4], src[i + 5]};
    std::array<double, 3> c = {src[i + 6], src[i + 7], src[i + 8]};
    auto nrm = cross3(sub3(b, a), sub3(c, a));
    double len = std::hypot(std::hypot(nrm[0], nrm[1]), nrm[2]);
    if (len < 1e-10) continue;
    pos.push_back(a[0]); pos.push_back(a[1]); pos.push_back(a[2]);
    pos.push_back(b[0]); pos.push_back(b[1]); pos.push_back(b[2]);
    pos.push_back(c[0]); pos.push_back(c[1]); pos.push_back(c[2]);
    faceN.push_back({nrm[0] / len, nrm[1] / len, nrm[2] / len});
  }
  size_t corners = pos.size() / 3;
  // corners that share a position, and the angle each face spans there (its weight)
  std::map<std::string, std::vector<size_t>> at;
  std::vector<double> weight(corners, 0);
  char key[64];
  for (size_t c = 0; c < corners; c++) {
    std::snprintf(key, sizeof key, "%ld_%ld_%ld", std::lround(pos[c * 3] * 1e4),
                  std::lround(pos[c * 3 + 1] * 1e4), std::lround(pos[c * 3 + 2] * 1e4));
    at[key].push_back(c);
    size_t f = c - (c % 3);
    std::array<double, 3> o = {pos[c * 3], pos[c * 3 + 1], pos[c * 3 + 2]};
    size_t c1 = f + ((c - f + 1) % 3);
    size_t c2 = f + ((c - f + 2) % 3);
    std::array<double, 3> u = {pos[c1 * 3] - o[0], pos[c1 * 3 + 1] - o[1], pos[c1 * 3 + 2] - o[2]};
    std::array<double, 3> v = {pos[c2 * 3] - o[0], pos[c2 * 3 + 1] - o[1], pos[c2 * 3 + 2] - o[2]};
    double hu = std::hypot(std::hypot(u[0], u[1]), u[2]);
    double hv = std::hypot(std::hypot(v[0], v[1]), v[2]);
    double d = dot3(u, v) / ((hu * hv) > 0 ? hu * hv : 1);
    weight[c] = std::acos(std::max(-1.0, std::min(1.0, d)));
  }
  double cosCrease = std::cos((creaseDeg * PI) / 180);
  std::vector<double> normal(pos.size(), 0);
  for (const auto& kv : at) {
    const std::vector<size_t>& list = kv.second;
    for (size_t c : list) {
      size_t f = (c / 3) * 3;
      double x = 0, y = 0, z = 0;
      for (size_t o : list) {
        size_t g = (o / 3) * 3;
        if (faceN[f / 3][0] * faceN[g / 3][0] + faceN[f / 3][1] * faceN[g / 3][1] +
                faceN[f / 3][2] * faceN[g / 3][2] <
            cosCrease)
          continue;
        x += faceN[g / 3][0] * weight[o];
        y += faceN[g / 3][1] * weight[o];
        z += faceN[g / 3][2] * weight[o];
      }
      double len = std::hypot(std::hypot(x, y), z);
      len = len > 0 ? len : 1;
      normal[c * 3] = x / len;
      normal[c * 3 + 1] = y / len;
      normal[c * 3 + 2] = z / len;
    }
  }
  auto g = std::make_shared<BufferGeometry>();
  g->position = std::make_shared<BufferAttribute>(pos, 3);
  g->normal = std::make_shared<BufferAttribute>(normal, 3);
  return g;
}


CarShell buildCarShell(const CarDims& d) {
  CarShell out;
  double hw = d.width / 2;
  double zF = -(d.wheelbase / 2 + d.frontOverhang);
  double zR = zF + d.length;
  double zAF = -d.wheelbase / 2;
  double zAR = d.wheelbase / 2;
  double H = d.height;
  double belt = H * 0.655;
  double zCowl = zAF + 0.36;
  double zRoofF = zCowl + 0.74;
  double zRoofR = d.tail == 1 ? zR - 0.42 : (d.tail == 2 ? zAR - 0.25 : zAR - 0.02);
  double zTailTop = d.tail == 1 ? zR - 0.12 : (d.tail == 2 ? zAR + 0.38 : zR - 0.32);
  double bootY = d.tail == 2 ? belt + 0.04 : belt + 0.02;

  std::vector<Section> keys;
  keys.push_back(noCabin(zF, hw * 0.8, 0.3, 0.66, 0.01));
  keys.push_back(noCabin(zF + 0.09, hw * 0.93, 0.21, 0.72, 0.02));
  keys.push_back(noCabin(zF + 0.36, hw * 0.985, 0.19, 0.785));
  keys.push_back(noCabin(zAF, hw, 0.2, 0.88, 0.035));
  // The bonnet falls away to the front and the windscreen starts low, below the
  // window sills: from the driver's seat the base of the screen then lies under
  // the line of sight over the bonnet, and the road shows from about 6 m on.
  const double cowlTop = belt - 0.03;
  const double cowlCrown = 0.03;
  Section cowlSec = noCabin(zCowl, hw, 0.2, cowlTop, cowlCrown);
  cowlSec.wr = hw * 0.9;
  keys.push_back(cowlSec);
  keys.push_back(cabinSec(zRoofF, hw * 0.74, H - 0.055, H - 0.004, belt));
  keys.back().w = hw;
  keys.push_back(cabinSec((zRoofF + zRoofR) / 2, hw * 0.735, H - 0.045, H, belt));
  keys.back().w = hw;
  keys.push_back(cabinSec(zRoofR, hw * 0.72, H - 0.07, H - 0.022, belt + 0.02));
  keys.back().w = hw;
  Section tailSec = noCabin(zTailTop, hw * 0.985, 0.22, bootY + 0.01, 0.02);
  tailSec.wr = hw * 0.82;
  keys.push_back(tailSec);
  keys.push_back(noCabin(zR - 0.1, hw * 0.94, 0.25, bootY - 0.05, 0.015));
  keys.push_back(noCabin(zR, hw * 0.72, 0.33, bootY - 0.2, 0.005));

  // dense station list: key sections, pillar edges and wheel-arch samples
  std::set<double> zs;
  for (const auto& k : keys) zs.insert(k.z);
  double Ra = d.wheelRadius + 0.075;
  for (double za : {zAF, zAR})
    for (double f : {-1.0, -0.92, -0.75, -0.5, -0.25, 0.0, 0.25, 0.5, 0.75, 0.92, 1.0})
      zs.insert(za + f * Ra);
  double zB = (zRoofF + zRoofR) / 2 - 0.02;  // B pillar
  zs.insert(zB - 0.045);
  zs.insert(zB + 0.045);
  zs.insert(zRoofR - 0.17);
  for (size_t i = 1; i < keys.size(); i++) {
    double a = keys[i - 1].z, b = keys[i].z;
    int n = static_cast<int>(std::ceil((b - a) / 0.22));
    for (int k = 1; k < n; k++) zs.insert(a + ((b - a) * k) / n);
  }
  std::vector<double> zList;
  for (double z : zs)
    if (z >= zF - 1e-6 && z <= zR + 1e-6) zList.push_back(z);
  std::sort(zList.begin(), zList.end());

  /** The cross-section at any z, blended between the key sections around it. */
  auto sectionAt = [&](double z) -> Section {
    size_t i = 1;
    while (i + 1 < keys.size() && keys[i].z < z) i++;
    const Section& a = keys[i - 1];
    const Section& b = keys[i];
    double t = std::max(0.0, std::min(1.0, (z - a.z) / (b.z - a.z)));
    Section r = lerpSec(a, b, t);
    r.z = z;
    return r;
  };
  std::vector<Section> stations;
  std::set<double> keyZ;
  for (const auto& k : keys) keyZ.insert(k.z);
  for (double z : zList) {
    if (!stations.empty() && z - stations.back().z < 0.012) {
      // two stations nearly on top of each other: keep the key section (glass
      // and pillars start exactly there), otherwise the first one
      if (keyZ.count(z) && !keyZ.count(stations.back().z)) stations.pop_back();
      else continue;
    }
    stations.push_back(sectionAt(z));
  }

  Geo paint, glass, trim, lining, headliner;
  const double eps = 1e-4;

  for (size_t i = 0; i + 1 < stations.size(); i++) {
    auto A = profileOf(stations[i], d, zAF, zAR, Ra);
    auto B = profileOf(stations[i + 1], d, zAF, zAR, Ra);
    double zm = (stations[i].z + stations[i + 1].z) / 2;
    bool inCabin = zm > zCowl + eps && zm < zTailTop - eps;
    bool underRoof = zm > zRoofF - eps && zm < zRoofR + eps;
    for (int k = 0; k < 6; k++) {
      // outward hint: sideways for the flanks, down for the floor, up for the top.  The sill
      // panel lies flat where it bridges a wheel arch: there "out" is down, towards the wheel.
      double nx = k == 0 ? 0 : (k == 5 ? 0.25 : 1);
      double ny = k == 0 ? -1 : (k == 5 ? 1 : (k == 4 ? 0.45 : (k == 1 ? -0.35 : 0)));
      Geo* target = &paint;
      bool glazed = false;
      if (k == 0) {
        target = &trim;
      } else if (k == 4 && inCabin) {
        // side glazing with A, B and C pillars left in body colour
        bool pillarB = std::fabs(zm - zB) < 0.045;
        bool pillarC = zm > zRoofR - 0.17;
        bool pillarA = zm < zRoofF + 0.02;
        if (!pillarA && !pillarB && !pillarC) glazed = true;
        // glass also beside the A pillar, down to where the gap to the sill closes
        if (pillarA && zm > zCowl + 0.12) glazed = true;
      } else if (k == 5 && inCabin && !underRoof) {
        glazed = true;  // windscreen and rear window
      }
      if (glazed) target = &glass;
      for (int side = 1; side >= -1; side -= 2) {
        auto a0 = side > 0 ? A[k] : mirrorX(A[k]);
        auto a1 = side > 0 ? A[k + 1] : mirrorX(A[k + 1]);
        auto b0 = side > 0 ? B[k] : mirrorX(B[k]);
        auto b1 = side > 0 ? B[k + 1] : mirrorX(B[k + 1]);
        target->quad(a0.data(), a1.data(), b1.data(), b0.data(), nx * side, ny, 0);
        if (!glazed) {
          // cabin lining: same panel pulled 2.5 cm inwards, facing in (the floor is raised a
          // little); the roof and the pillars between the windows get their own geometry
          auto inset = [&](const std::array<double, 3>& p) {
            if (k == 0) return std::array<double, 3>{p[0], p[1] + 0.03, p[2]};
            return std::array<double, 3>{p[0] - (p[0] > 0 ? 1.0 : (p[0] < 0 ? -1.0 : 0.0)) * 0.025,
                                         p[1] - (k == 5 ? 0.025 : 0.0), p[2]};
          };
          Geo* inner = (inCabin && k >= 4) ? &headliner : &lining;
          inner->quad(inset(a0).data(), inset(a1).data(), inset(b1).data(), inset(b0).data(),
                      -nx * side, -ny, 0);
        }
      }
    }
  }

  // nose and tail caps; inside, a bulkhead closes the cabin towards the engine and behind the boot
  // (taken at their exact places, not looked up among the stations: a station that was
  // dropped for lying too close to its neighbour must not move a pillar to the car's nose)
  const Section cowlStation = sectionAt(zCowl);
  const Section roofStation = sectionAt(zRoofF);
  const Section& first = stations.front();
  const Section& last = stations.back();
  struct Cap {
    const Section* s;
    double nz;
    bool inside;
  } caps[4] = {{&first, -1, false}, {&last, 1, false}, {&cowlStation, 1, true}, {&last, -1, true}};
  for (const auto& cap : caps) {
    auto P = profileOf(*cap.s, d, zAF, zAR, Ra);
    double cy = (cap.s->yb + cap.s->yc) / 2;
    // the inner panels stand a little inside the opening they close
    double z = cap.inside ? cap.s->z + cap.nz * 0.012 : cap.s->z;
    for (int k = 0; k < 6; k++) {
      for (int side = 1; side >= -1; side -= 2) {
        auto p0 = side > 0 ? P[k] : mirrorX(P[k]);
        auto p1 = side > 0 ? P[k + 1] : mirrorX(P[k + 1]);
        if (cap.inside) {
          auto inset = [&](const std::array<double, 3>& p, int kk) {
            if (kk == 0) return std::array<double, 3>{p[0], p[1] + 0.03, p[2]};
            return std::array<double, 3>{p[0] - (p[0] > 0 ? 1.0 : (p[0] < 0 ? -1.0 : 0.0)) * 0.025, p[1], p[2]};
          };
          auto q0 = inset(p0, k == 0 ? 0 : 1);
          auto q1 = inset(p1, 1);
          double centre[3] = {0, cy, z};
          double qq0[3] = {q0[0], q0[1], z};
          double qq1[3] = {q1[0], q1[1], z};
          lining.tri(centre, qq0, qq1, 0, 0, cap.nz);
        } else {
          double centre[3] = {0, cy, z};
          paint.tri(centre, p0.data(), p1.data(), 0, 0, cap.nz);
        }
      }
    }
  }

  // ---- A pillars -------------------------------------------------------------
  // Slim wedges along the edges of the windscreen: two narrow faces lie on the
  // windscreen and on the side glass, the trim between them comes to a rounded
  // edge that points at the occupant on that side, who therefore looks at the
  // pillar end-on and loses as little of the view as a pillar can cost.
  auto pc = profileOf(cowlStation, d, zAF, zAR, Ra);
  auto pr = profileOf(roofStation, d, zAF, zAR, Ra);
  Sculpt pillarOut, pillarIn;
  // lerp between two profile points at t, scaled by the side (mirrored for -1)
  auto lerp3 = [&](const std::array<double, 3>& a, const std::array<double, 3>& b, double t,
                   int side) -> std::array<double, 3> {
    return {side * (a[0] + (b[0] - a[0]) * t), a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
  };
  for (int side = 1; side >= -1; side -= 2) {
    auto e0 = lerp3(pc[5], pr[5], 0, side);
    auto e1 = lerp3(pc[5], pr[5], 1, side);
    std::array<double, 3> axis = {e1[0] - e0[0], e1[1] - e0[1], e1[2] - e0[2]};
    double al = std::hypot(std::hypot(axis[0], axis[1]), axis[2]);
    if (al > 0) { axis[0] /= al; axis[1] /= al; axis[2] /= al; }
    // across the pillar: the part of `v` at right angles to it
    auto across = [&](const std::array<double, 3>& v) {
      double dt = dot3(v, axis);
      std::array<double, 3> r = {v[0] - axis[0] * dt, v[1] - axis[1] * dt, v[2] - axis[2] * dt};
      double rl = std::hypot(std::hypot(r[0], r[1]), r[2]);
      if (rl > 0) { r[0] /= rl; r[1] /= rl; r[2] /= rl; }
      return r;
    };
    std::array<double, 3> head = {side * hw * 0.4, H - 0.26, zRoofF + 0.42};
    std::vector<std::vector<std::array<double, 3>>> outer, inner;
    const int steps = 6;
    for (int i = 0; i <= steps; i++) {
      // a little beyond both ends, so the pillar disappears into scuttle and roof
      double t = -0.05 + (1.1 * i) / steps;
      auto edge = lerp3(pc[5], pr[5], t, side);
      auto m6 = lerp3(pc[6], pr[6], t, side);
      auto m4 = lerp3(pc[4], pr[4], t, side);
      auto toMiddle = across({m6[0] - edge[0], m6[1] - edge[1], m6[2] - edge[2]});
      auto toSill = across({m4[0] - edge[0], m4[1] - edge[1], m4[2] - edge[2]});
      auto toHead = across({head[0] - edge[0], head[1] - edge[1], head[2] - edge[2]});
      // stouter at the foot and where it joins the roof
      double k = 1 + 0.55 * std::pow(1 - t, 3) + 0.25 * std::pow(t, 3);
      auto add = [&](const std::array<double, 3>& base, const std::array<double, 3>& dir, double s) {
        return std::array<double, 3>{base[0] + dir[0] * s, base[1] + dir[1] * s, base[2] + dir[2] * s};
      };
      auto corner = add(edge, toHead, -0.007);
      auto onScreen = add(add(edge, toMiddle, 0.021 * k), toHead, -0.004);
      auto onSide = add(add(edge, toSill, 0.025 * k), toHead, -0.004);
      auto flankScreen = add(add(edge, toMiddle, 0.017 * k), toHead, 0.02 * k);
      auto flankSide = add(add(edge, toSill, 0.02 * k), toHead, 0.02 * k);
      auto nose = add(add(add(edge, toMiddle, 0.005 * k), toSill, 0.006 * k), toHead, 0.038 * k);
      outer.push_back({onSide, corner, onScreen});
      inner.push_back({onScreen, flankScreen, nose, flankSide, onSide});
    }
    double facingOut[3] = {static_cast<double>(side), 0.3, -0.4};
    double facingIn[3] = {-static_cast<double>(side), -0.3, 0.6};
    pillarOut.sheet(outer, facingOut);
    pillarIn.sheet(inner, facingIn);
  }

  // outlines of the flat end faces, for whatever is fixed to the bumpers
  auto outline = [&](const Section& sec) {
    auto P = profileOf(sec, d, zAF, zAR, Ra);
    std::vector<std::vector<double>> o;
    for (auto& q : P) o.push_back({q[0], q[1]});
    return o;
  };

  out.paint = paint.build();
  out.glass = glass.build();
  out.trim = trim.build();
  out.lining = lining.build();
  out.headliner = headliner.build();
  out.pillars = pillarOut.build(50);
  out.pillarTrim = pillarIn.build(70);
  out.zFront = zF;
  out.zRear = zR;
  out.zAxleF = zAF;
  out.zAxleR = zAR;
  out.zCowl = zCowl;
  out.zRoofFront = zRoofF;
  out.zRoofRear = zRoofR;
  out.zPillarB = zB;
  out.zTail = zTailTop;
  out.halfWidth = hw;
  out.beltY = belt;
  out.roofY = H;
  out.cowlY = cowlTop;
  out.cowlCrownY = cowlTop + cowlCrown;
  out.noseOutline = outline(first);
  out.tailOutline = outline(last);
  return out;
}

std::pair<GeometryPtr, GeometryPtr> buildWheelParts(const CarDims& d) {
  double r = d.wheelRadius;
  double w = d.tyreWidth;
  // three.js CylinderGeometry's axis is Y; the wheel spins about X, so the
  // cylinder is turned a quarter turn about Z (as the original does)
  GeometryPtr tyre = makeCylinderGeometry(r, r, w, 20, 1, false);
  tyre->rotateZ(PI / 2.0);

  std::vector<GeometryPtr> rimParts;
  GeometryPtr disc = makeCylinderGeometry(r * 0.66, r * 0.66, w * 1.02, 14, 1, false);
  disc->rotateZ(PI / 2.0);
  rimParts.push_back(disc);
  // five chunky spokes stand proud of the disc so the rotation is visible
  for (int i = 0; i < 5; i++) {
    GeometryPtr s = makeBoxGeometry(w * 1.06, r * 0.6, r * 0.16);
    s->translate(0, r * 0.34, 0);
    s->rotateX(static_cast<double>(i) / 5 * PI * 2);
    rimParts.push_back(s);
  }
  // the disc is built rotated, the spokes are boxes laid out in the wheel
  // plane (their X is the tread width, Y and Z the radius) — so both are
  // already where they belong and only need to be concatenated
  std::vector<double> pos, idx;
  for (const auto& sg : rimParts) {
    size_t base = pos.size() / 3;
    for (size_t i = 0; i < sg->position->count(); i++) {
      pos.push_back(sg->position->getX(i));
      pos.push_back(sg->position->getY(i));
      pos.push_back(sg->position->getZ(i));
    }
    if (sg->index) {
      for (size_t i = 0; i < sg->index->count(); i++)
        idx.push_back(base + static_cast<size_t>(sg->index->array[i]));
    } else {
      for (size_t i = 0; i < sg->position->count(); i++) idx.push_back(base + i);
    }
  }
  auto rim = std::make_shared<BufferGeometry>();
  rim->position = std::make_shared<BufferAttribute>(pos, 3);
  rim->index = std::make_shared<BufferAttribute>(idx, 1);
  rim->computeVertexNormals();
  tyre->deleteAttribute("uv");
  return {tyre, rim};
}

GeometryPtr buildWheel(double radius, double width, int rim) {
  CarDims d;
  d.wheelRadius = radius;
  d.tyreWidth = width;
  auto parts = buildWheelParts(d);
  (void)rim;
  // the tyre and the rim are kept apart so the game can tint them; a plain
  // caller gets the merged wheel (three.js merges them the same way)
  // the tyre and the rim are coloured separately (a dark tyre, a lighter
  // rim), exactly as the game tints them
  auto paint = [](GeometryPtr g, uint32_t color) {
    size_t n = g->position ? g->position->count() : 0;
    Color c;
    c.setHex(color);
    std::vector<double> cols(n * 3);
    for (size_t i = 0; i < n; i++) {
      cols[i * 3] = c.r;
      cols[i * 3 + 1] = c.g;
      cols[i * 3 + 2] = c.b;
    }
    g->color = std::make_shared<BufferAttribute>(cols, 3);
    return g;
  };
  paint(parts.first, 0x15161a);   // tyre
  paint(parts.second, 0xb9bcc2);  // rim
  // the tyre and the rim keep their own colours so the game can tint them;
  // a plain caller gets the merged wheel (three.js merges them too)
  auto g = mergeGeometries({parts.first, parts.second}, false);
  g->computeVertexNormals();
  return g;
}

}  // namespace fs

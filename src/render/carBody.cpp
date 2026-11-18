// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The car shell: a lofted body built from cross-sections (stations), split
 * into paint / glass / trim / lining, plus the A pillars and the wheels.
 *
 * Origin: src/render/carBody.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).  The station list, the profile points and the panel rules are
 * carried over exactly, including which panels are glazed.
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
  keys.push_back(noCabin(zF + 0.36, hw * 0.985, 0.19, 0.8));
  keys.push_back(noCabin(zAF, hw, 0.2, 0.9, 0.035));
  Section cowlSec = noCabin(zCowl, hw, 0.2, belt + 0.035, 0.03);
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

  std::vector<Section> stations;
  for (double z : zList) {
    if (!stations.empty() && z - stations.back().z < 0.012) continue;
    size_t i = 1;
    while (i + 1 < keys.size() && keys[i].z < z) i++;
    const Section& a = keys[i - 1];
    const Section& b = keys[i];
    double t = std::max(0.0, std::min(1.0, (z - a.z) / (b.z - a.z)));
    stations.push_back(lerpSec(a, b, t));
    stations.back().z = z;
  }

  Geo paint, glass, trim, lining;
  const double eps = 1e-4;

  for (size_t i = 0; i + 1 < stations.size(); i++) {
    auto A = profileOf(stations[i], d, zAF, zAR, Ra);
    auto B = profileOf(stations[i + 1], d, zAF, zAR, Ra);
    double zm = (stations[i].z + stations[i + 1].z) / 2;
    bool inCabin = zm > zCowl + eps && zm < zTailTop - eps;
    bool underRoof = zm > zRoofF - eps && zm < zRoofR + eps;
    for (int k = 0; k < 6; k++) {
      // outward hint: sideways for the flanks, down for the floor, up for the top
      double nx = k == 0 ? 0 : (k == 5 ? 0.25 : 1);
      double ny = k == 0 ? -1 : (k == 5 ? 1 : (k == 4 ? 0.45 : 0));
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
        if (pillarA && zm > zCowl + 0.3) glazed = true;  // quarter light behind the A pillar
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
          // cabin lining: same panel pulled 2.5 cm inwards, facing in
          auto inset = [&](const std::array<double, 3>& p) {
            if (k == 0) return std::array<double, 3>{p[0], p[1] + 0.03, p[2]};
            return std::array<double, 3>{p[0] - (p[0] > 0 ? 1.0 : (p[0] < 0 ? -1.0 : 0.0)) * 0.025,
                                         p[1] - (k == 5 ? 0.025 : 0.0), p[2]};
          };
          lining.quad(inset(a0).data(), inset(a1).data(), inset(b1).data(), inset(b0).data(),
                      -nx * side, -ny, 0);
        }
      }
    }
  }

  // nose and tail caps
  struct Cap {
    const Section* s;
    double nz;
  } caps[2] = {{&stations.front(), -1}, {&stations.back(), 1}};
  for (const auto& cap : caps) {
    auto P = profileOf(*cap.s, d, zAF, zAR, Ra);
    double cy = (cap.s->yb + cap.s->yc) / 2;
    for (int k = 0; k < 6; k++) {
      for (int side = 1; side >= -1; side -= 2) {
        auto p0 = side > 0 ? P[k] : mirrorX(P[k]);
        auto p1 = side > 0 ? P[k + 1] : mirrorX(P[k + 1]);
        double centre[3] = {0, cy, cap.s->z};
        paint.tri(centre, p0.data(), p1.data(), 0, 0, cap.nz);
      }
    }
  }

  // A pillars from the cowl corners up to the roof corners
  auto stationAt = [&](double z) -> const Section& {
    for (const auto& st : stations)
      if (std::fabs(st.z - z) < 1e-6) return st;
    return stations.front();
  };
  auto cowlP = profileOf(stationAt(zCowl), d, zAF, zAR, Ra)[5];
  auto roofFP = profileOf(stationAt(zRoofF), d, zAF, zAR, Ra)[5];
  Geo pillarsGeo;
  for (int side = 1; side >= -1; side -= 2) {
    Vector3 a(side * (cowlP[0] - 0.02), cowlP[1] - 0.03, cowlP[2] + 0.02);
    Vector3 b(side * (roofFP[0] - 0.005), roofFP[1], roofFP[2] + 0.01);
    Vector3 dir;
    dir.subVectors(b, a);
    double len = dir.length();
    dir.normalize();
    // a 0.06 × len × 0.06 bar from a to b (three.js: box + quaternion)
    Quaternion q;
    q.setFromUnitVectors(Vector3(0, 1, 0), dir);
    GeometryPtr g = makeBoxGeometry(0.06, len, 0.06);
    // rotate the box's +Y onto `dir`, then move its centre to the midpoint
    Matrix4 rot;
    rot.makeRotationFromQuaternion(q);
    Matrix4 t;
    t.makeTranslation((a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2);
    Matrix4 combined;
    combined.multiplyMatrices(t, rot);
    for (size_t i = 0; i < g->position->count(); i++) {
      Vector3 p(g->position->getX(i), g->position->getY(i), g->position->getZ(i));
      p.applyMatrix4(combined);
      g->position->setXYZ(i, p.x, p.y, p.z);
    }
    g->deleteAttribute("uv");
    // the primitive geometries are already non-indexed, so the vertices go
    // straight into the pillar soup (three.js calls toNonIndexed first)
    for (size_t i = 0; i < g->position->count(); i++)
      pillarsGeo.v(g->position->getX(i), g->position->getY(i), g->position->getZ(i));
  }
  auto pillars = pillarsGeo.build(false);

  out.paint = paint.build();
  out.glass = glass.build();
  out.trim = trim.build();
  out.lining = lining.build();
  out.pillars = pillars;
  out.zFront = zF;
  out.zRear = zR;
  out.zAxleF = zAF;
  out.zAxleR = zAR;
  out.zCowl = zCowl;
  out.zRoofFront = zRoofF;
  out.zRoofRear = zRoofR;
  out.halfWidth = hw;
  out.beltY = belt;
  out.roofY = H;
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

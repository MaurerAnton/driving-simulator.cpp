// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Trams and trains as seen from the road (origin: src/render/trams.ts @
 * ff37c5d, v0.7.0): the lofted car bodies as vertex-coloured triangles.
 */
#include "render/trams.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace fs {

namespace {

/** the liveries of the three vehicles (vertex colours) */
struct Livery {
  double body[3], accent[3];
};
const Livery LIVERY[3] = {
    {{0.93, 0.93, 0.90}, {0.10, 0.36, 0.68}},   // city tram
    {{0.93, 0.93, 0.90}, {0.05, 0.52, 0.52}},   // regional tram
    {{0.88, 0.88, 0.86}, {0.68, 0.13, 0.12}},   // train
};
const double GLASS[3] = {0.07, 0.10, 0.13};
const double ROOF[3] = {0.52, 0.54, 0.56};
const double DARK[3] = {0.12, 0.12, 0.13};
const double LAMP[3] = {1.0, 0.97, 0.82};
const double TAIL[3] = {0.8, 0.08, 0.06};

/** Collects coloured triangles; flat shading comes from not sharing vertices. */
struct Tris {
  std::vector<double> pos, col;

  void tri(const double a[3], const double b[3], const double c[3], const double colour[3]) {
    for (int k = 0; k < 3; k++) pos.push_back(a[k]);
    for (int k = 0; k < 3; k++) pos.push_back(b[k]);
    for (int k = 0; k < 3; k++) pos.push_back(c[k]);
    for (int i = 0; i < 3; i++)
      for (int k = 0; k < 3; k++) col.push_back(colour[k]);
  }

  /** a, b, c, d counter-clockwise seen from outside */
  void quad(const double a[3], const double b[3], const double c[3], const double d[3],
            const double colour[3]) {
    tri(a, b, c, colour);
    tri(a, c, d, colour);
  }

  /** an upright box, x0…x1, y0…y1, z0…z1 */
  void box(double x0, double x1, double y0, double y1, double z0, double z1,
           const double colour[3]) {
    double p000[3] = {x0, y0, z0}, p100[3] = {x1, y0, z0}, p010[3] = {x0, y1, z0},
           p110[3] = {x1, y1, z0}, p001[3] = {x0, y0, z1}, p101[3] = {x1, y0, z1},
           p011[3] = {x0, y1, z1}, p111[3] = {x1, y1, z1};
    quad(p001, p101, p111, p011, colour);   // back (+z)
    quad(p100, p000, p010, p110, colour);   // front (-z)
    quad(p000, p001, p011, p010, colour);   // left
    quad(p101, p100, p110, p111, colour);   // right
    quad(p011, p111, p110, p010, colour);   // top
    quad(p000, p100, p101, p001, colour);   // bottom
  }

  GeometryPtr geometry() {
    auto g = std::make_shared<BufferGeometry>();
    g->setAttribute("position", std::make_shared<BufferAttribute>(pos, 3));
    g->setAttribute("color", std::make_shared<BufferAttribute>(col, 3));
    g->computeVertexNormals();
    return g;
  }
};

/** the outline of the body at a place along the car: lower left over the roof to lower right */
void section(double hw, const double bands[5], double top, double floor, double z, double squeeze,
             double drop, Tris& t, std::vector<std::array<double, 3>>& pts) {
  pts.clear();
  double w = hw * squeeze;
  for (int i = 0; i < 5; i++) {
    double y = bands[i];
    double d = y > floor + 1 ? drop * ((y - floor) / (top - floor)) : 0;
    pts.push_back({-w, y - d, z});
  }
  pts.push_back({-w * 0.84, top - drop, z});
  pts.push_back({w * 0.84, top - drop, z});
  for (int i = 4; i >= 0; i--) {
    double y = bands[i];
    double d = y > floor + 1 ? drop * ((y - floor) / (top - floor)) : 0;
    pts.push_back({w, y - d, z});
  }
  (void)t;
}

/**
 * One car.  Local frame like every vehicle of the game: x right, y up from
 * the top of the rail, the front towards -z.  `nose`: the driver's end is
 * at the front (first car), at the back (last car) or there is none.
 */
GeometryPtr buildCar(const Consist& c, int kind, int nose /* 0 front, 1 back, 2 none */,
                     bool bellows) {
  Tris t;
  const Livery& liv = (kind >= 0 && kind <= 2) ? LIVERY[kind] : LIVERY[0];
  const double* body = liv.body;
  const double* accent = liv.accent;
  double hw = c.width / 2;
  double L = c.carLen;
  double floor = 0.34;
  double top = c.height;
  // heights of the bands of the side wall: skirt, belt, windows, band above the windows
  const double bands[5] = {floor, floor + 0.62, floor + 0.98, top - 1.02, top - 0.42};
  const double* sideColours[4] = {accent, body, GLASS, body};
  // faces between two sections, in the order of the outline
  const double* colours[16];
  int ci = 0;
  for (int i = 0; i < 4; i++) colours[ci++] = sideColours[i];
  colours[ci++] = ROOF;
  colours[ci++] = ROOF;
  colours[ci++] = ROOF;
  for (int i = 3; i >= 0; i--) colours[ci++] = sideColours[i];

  auto sectionAt = [&](double z, double squeeze, double drop,
                       std::vector<std::array<double, 3>>& pts) {
    pts.clear();
    double w = hw * squeeze;
    for (int i = 0; i < 5; i++) {
      double y = bands[i];
      double d = y > floor + 1 ? drop * ((y - floor) / (top - floor)) : 0;
      pts.push_back({-w, y - d, z});
    }
    pts.push_back({-w * 0.84, top - drop, z});
    pts.push_back({w * 0.84, top - drop, z});
    for (int i = 4; i >= 0; i--) {
      double y = bands[i];
      double d = y > floor + 1 ? drop * ((y - floor) / (top - floor)) : 0;
      pts.push_back({w, y - d, z});
    }
  };
  auto loft = [&](const std::vector<std::array<double, 3>>& a,
                  const std::vector<std::array<double, 3>>& b) {
    for (size_t i = 0; i + 1 < a.size(); i++)
      t.quad(a[i].data(), a[i + 1].data(), b[i + 1].data(), b[i].data(), colours[i]);
    // the floor
    t.quad(a.back().data(), a[0].data(), b[0].data(), b.back().data(), DARK);
  };
  auto cap = [&](const std::vector<std::array<double, 3>>& s, bool front, bool cab) {
    size_t n = s.size();
    double mid[3] = {0, (floor + top) / 2, s[0][2]};
    for (size_t i = 0; i + 1 < n; i++) {
      // a driver's end: windscreen in the window band, lamps in the belt
      int band = static_cast<int>(std::min(i, n - 2 - i));
      const double* colour;
      if (!cab) colour = DARK;
      else if (band == 2 || band == 3) colour = GLASS;
      else if (band == 1) colour = body;
      else if (band == 0) colour = accent;
      else colour = ROOF;
      if (front) t.tri(mid, s[i].data(), s[i + 1].data(), colour);
      else t.tri(mid, s[i + 1].data(), s[i].data(), colour);
    }
    if (front) t.tri(mid, s[n - 1].data(), s[0].data(), DARK);
    else t.tri(mid, s[0].data(), s[n - 1].data(), DARK);
  };

  double z0 = -L / 2, z1 = L / 2;
  double taper = std::min(1.5, L * 0.14);
  std::vector<std::array<double, 3>> stations;   // (z, squeeze, drop)
  if (nose == 0) stations.push_back({z0, 0.8, 0.3});
  if (nose == 0) stations.push_back({z0 + taper, 1, 0});
  if (nose != 0) stations.push_back({z0, 1, 0});
  if (nose == 1) stations.push_back({z1 - taper, 1, 0});
  if (nose == 1) stations.push_back({z1, 0.8, 0.3});
  if (nose != 1) stations.push_back({z1, 1, 0});
  std::vector<std::vector<std::array<double, 3>>> sections;
  for (const auto& st : stations) {
    std::vector<std::array<double, 3>> pts;
    sectionAt(st[0], st[1], st[2], pts);
    sections.push_back(pts);
  }
  // (lofting from the front to the back keeps the faces looking outwards)
  for (size_t i = 0; i + 1 < sections.size(); i++) loft(sections[i + 1], sections[i]);
  cap(sections[0], true, nose == 0);
  cap(sections.back(), false, nose == 1);

  // lamps of the driver's end: white ahead, red behind
  auto lamps = [&](double z, const double* colour, double out) {
    for (int side : {-1, 1}) {
      double x = side * hw * 0.5;
      t.box(x - 0.16, x + 0.16, floor + 0.66, floor + 0.86, std::min(z, z + out),
            std::max(z, z + out), colour);
    }
  };
  if (nose == 0) lamps(z0, LAMP, -0.03);
  if (nose == 1) lamps(z1, TAIL, 0.03);

  // running gear
  for (double k : {-0.36, 0.36})
    t.box(-hw * 0.72, hw * 0.72, 0.02, floor + 0.08, k * L - 1.05, k * L + 1.05, DARK);
  // what sits on the roof
  t.box(-hw * 0.55, hw * 0.55, top, top + 0.26, -L * 0.28, L * 0.28, ROOF);
  if (kind != RAIL_TRAIN && nose == 2) {
    // a pantograph: two arms meeting under the wire
    double y = top + 0.26;
    t.box(-0.05, 0.05, y, y + 0.9, -0.9, -0.8, DARK);
    t.box(-0.05, 0.05, y + 0.85, y + 0.93, -0.9, 0.5, DARK);
    t.box(-0.75, 0.75, y + 0.9, y + 0.96, 0.42, 0.52, DARK);
  }
  // the joint to the next car
  if (bellows)
    t.box(-hw * 0.8, hw * 0.8, floor + 0.1, top - 0.25, z1, z1 + c.gap, DARK);
  return t.geometry();
}

}  // namespace

RailActors::RailActors() { group.name = "rail vehicles"; }

GeometryPtr RailActors::carGeometry(int kind, int index, int count) {
  int nose = index == 0 ? 0 : (index == count - 1 ? 1 : 2);
  bool bellows = index < count - 1;
  char key[32];
  std::snprintf(key, sizeof key, "%d:%d:%d", kind, nose, bellows ? 1 : 0);
  auto it = geometries_.find(key);
  if (it != geometries_.end()) return it->second;
  const Consist* c = consistOf(kind);
  GeometryPtr g = buildCar(*c, kind, nose, bellows);
  geometries_[key] = g;
  return g;
}

void RailActors::update(const std::vector<RailVehicle>& vehicles) {
  std::set<int> seen;
  for (const RailVehicle& v : vehicles) {
    seen.insert(v.id);
    auto it = shown_.find(v.id);
    Shown* g = nullptr;
    if (it == shown_.end()) {
      auto made = std::make_unique<Shown>();
      for (size_t i = 0; i < v.cars.size(); i++) {
        GeometryPtr geo = carGeometry(v.kind, static_cast<int>(i), static_cast<int>(v.cars.size()));
        auto mesh = std::make_unique<Mesh>(geo, MaterialKind::Struct);
        mesh->castShadow = true;
        // YXZ order: pitch, then yaw (as the original sets the rotation)
        Mesh* raw = mesh.get();
        group.add(std::unique_ptr<Object3D>(mesh.release()));
        made->cars.push_back(raw);
      }
      g = made.get();
      shown_[v.id] = std::move(made);
    } else {
      g = it->second.get();
    }
    for (size_t i = 0; i < v.cars.size() && i < g->cars.size(); i++) {
      const RailCar& car = v.cars[i];
      Mesh* m = g->cars[i];
      m->position.set(car.x, car.y, car.z);
      m->quaternion.setFromEuler(Euler(car.pitch, car.yaw, 0, Euler::YXZ));
    }
  }
  for (auto it = shown_.begin(); it != shown_.end();) {
    if (seen.count(it->first)) {
      ++it;
      continue;
    }
    for (Mesh* m : it->second->cars) group.remove(m);
    it = shown_.erase(it);
  }
}

}  // namespace fs

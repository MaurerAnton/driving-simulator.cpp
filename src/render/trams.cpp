// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/trams.ts @ v1.7.7
#include "render/trams.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_set>

#include "core/js.h"
#include "engine/geometries.h"
#include "platform/canvas.h"

namespace fs {

namespace {

using Rgb = std::array<float, 3>;
using P3 = std::array<double, 3>;

struct Livery {
  Rgb body;
  Rgb accent;
};
const Livery& liveryOf(int kind) {
  static const auto* t = new std::map<int, Livery>{
      {RAIL_TRAM, {{0.93f, 0.93f, 0.9f}, {0.1f, 0.36f, 0.68f}}},
      {RAIL_LIGHT, {{0.93f, 0.93f, 0.9f}, {0.05f, 0.52f, 0.52f}}},
      {RAIL_TRAIN, {{0.88f, 0.88f, 0.86f}, {0.68f, 0.13f, 0.12f}}},
  };
  auto it = t->find(kind);
  return it != t->end() ? it->second : t->at(RAIL_TRAM);
}
constexpr Rgb GLASS = {0.07f, 0.1f, 0.13f};
constexpr Rgb ROOF = {0.52f, 0.54f, 0.56f};
constexpr Rgb DARK = {0.12f, 0.12f, 0.13f};
constexpr Rgb LAMP = {1, 0.97f, 0.82f};
constexpr Rgb TAIL = {0.8f, 0.08f, 0.06f};
/** inside an open door: the walls of the doorway – as dark as the inside seen through the windows –, and its threshold (a metal plate) */
constexpr Rgb INSIDE = {0.075f, 0.08f, 0.085f};
constexpr Rgb SILL = {0.55f, 0.56f, 0.57f};

/** the floor of the bodies over the top of the rail (low-floor: the doors open at its height) */
constexpr double FLOOR = 0.34;

/** the height of the contact wire over the top of the rail (render/stopmodels.ts CONTACT_WIRE_HEIGHT) */
constexpr double CONTACT_WIRE_HEIGHT = 5.6;

/** Heights of the bands of a side wall: skirt, belt, windows, the band above the windows, where the roof begins. */
std::vector<double> wallBands(double top) { return {FLOOR, FLOOR + 0.62, FLOOR + 0.98, top - 1.02, top - 0.42}; }

/** how far the slanted end of a body with a driver's cab reaches back */
double capTaper(double len) { return std::min(1.5, len * 0.14); }

/**
 * The passenger doors: double plug doors with an opening this wide, a frame
 * round it in the livery's accent colour.  A leaf is this thick; opening, it
 * first comes this far out of the wall, then slides along the wall by its
 * own width, which clears the opening.
 */
constexpr double DOOR_WIDTH = 1.3;
constexpr double DOOR_FRAME = 0.07;
constexpr double LEAF_THICK = 0.04;
constexpr double LEAF_OUT = 0.07;
constexpr double LEAF_SLIDE = DOOR_WIDTH / 2;

/** How high the opening of a door reaches: to the top of the window band, but not much over two metres. */
double doorTop(const std::vector<double>& bands) { return std::min(bands[3], FLOOR + 2.25); }

/** Collects coloured triangles; flat shading comes from not sharing vertices. */
struct Tris {
  std::vector<float> pos;
  std::vector<float> col;

  void tri(const P3& a, const P3& b, const P3& c, const Rgb& colour) {
    for (const P3* p : {&a, &b, &c})
      for (double v : *p) pos.push_back(static_cast<float>(v));
    for (int i = 0; i < 3; i++) col.insert(col.end(), colour.begin(), colour.end());
  }

  /** a, b, c, d counter-clockwise seen from outside */
  void quad(const P3& a, const P3& b, const P3& c, const P3& d, const Rgb& colour) {
    tri(a, b, c, colour);
    tri(a, c, d, colour);
  }

  /** a flat face a, b, c, d (in order round it, either way) turned to look along `n` */
  void face(const P3& a, const P3& b, const P3& c, const P3& d, const P3& n, const Rgb& colour) {
    const double ux = b[0] - a[0];
    const double uy = b[1] - a[1];
    const double uz = b[2] - a[2];
    const double vx = c[0] - a[0];
    const double vy = c[1] - a[1];
    const double vz = c[2] - a[2];
    const double along = (uy * vz - uz * vy) * n[0] + (uz * vx - ux * vz) * n[1] + (ux * vy - uy * vx) * n[2];
    if (along >= 0) quad(a, b, c, d, colour);
    else quad(d, c, b, a, colour);
  }

  /** an upright box, x0…x1, y0…y1, z0…z1 */
  void box(double x0, double x1, double y0, double y1, double z0, double z1, const Rgb& colour) {
    const auto p = [](double x, double y, double z) { return P3{x, y, z}; };
    quad(p(x0, y0, z1), p(x1, y0, z1), p(x1, y1, z1), p(x0, y1, z1), colour);   // back (+z)
    quad(p(x1, y0, z0), p(x0, y0, z0), p(x0, y1, z0), p(x1, y1, z0), colour);   // front (-z)
    quad(p(x0, y0, z0), p(x0, y0, z1), p(x0, y1, z1), p(x0, y1, z0), colour);   // left
    quad(p(x1, y0, z1), p(x1, y0, z0), p(x1, y1, z0), p(x1, y1, z1), colour);   // right
    quad(p(x0, y1, z1), p(x1, y1, z1), p(x1, y1, z0), p(x0, y1, z0), colour);   // top
    quad(p(x0, y0, z0), p(x1, y0, z0), p(x1, y0, z1), p(x0, y0, z1), colour);   // bottom
  }

  GeometryPtr geometry() {
    auto g = make<BufferGeometry>();
    g->setAttribute("position", attr(std::move(pos), 3));
    g->setAttribute("color", attr(std::move(col), 3));
    g->computeVertexNormals();
    g->computeBoundingSphere();
    return g;
  }
};

/** what a stretch of a side wall between two sections of the loft is: plain wall, the frame beside a door, a door's opening */
constexpr int WALL = 0;
constexpr int FRAME = 1;
constexpr int OPENING = 2;

/**
 * The doorway behind the opening of a door on side `s` (the wall at `x`):
 * its back `depth` into the body, its sides, its ceiling, the threshold.
 * Its back and sides are cut at the levels of the wall beside them, so that
 * they meet it corner to corner.
 */
void doorway(Tris& t, int s, double x, double depth, double z, const std::vector<double>& levels, double top) {
  const double xi = x - s * depth;
  const double za = z - DOOR_WIDTH / 2;
  const double zb = z + DOOR_WIDTH / 2;
  for (size_t j = 0; j + 1 < levels.size() && levels[j + 1] <= top; j++) {
    const double y0 = levels[j];
    const double y1 = levels[j + 1];
    t.face({xi, y0, za}, {xi, y0, zb}, {xi, y1, zb}, {xi, y1, za}, {static_cast<double>(s), 0, 0}, INSIDE);
    t.face({x, y0, za}, {xi, y0, za}, {xi, y1, za}, {x, y1, za}, {0, 0, 1}, INSIDE);
    t.face({x, y0, zb}, {xi, y0, zb}, {xi, y1, zb}, {x, y1, zb}, {0, 0, -1}, INSIDE);
  }
  t.face({x, top, za}, {xi, top, za}, {xi, top, zb}, {x, top, zb}, {0, -1, 0}, INSIDE);
  t.face({x, FLOOR, za}, {xi, FLOOR, za}, {xi, FLOOR, zb}, {x, FLOOR, zb}, {0, 1, 0}, SILL);
}

/**
 * One leaf of the door at `z` on side `s` (its face in the wall at `x`),
 * shut: the one that slides towards `way` (-1 the front, 1 the back).  A
 * panel in the body colour with a window from `window` up, dark seals where
 * the leaves meet and round the edge.  `edges`: its thin sides too – a leaf
 * out of the wall shows them; shut in the wall only its face is seen.
 */
void doorLeaf(Tris& t, int s, int way, double x, double z, double top, double window, const Rgb& body, bool edges) {
  const double w = DOOR_WIDTH / 2;
  /** a part of its face: across it from where the leaves meet (0) to the frame (w), and from y0 up to y1 */
  const auto part = [&](double u0, double u1, double y0, double y1, const Rgb& colour) {
    const double a = z + way * u0;
    const double b = z + way * u1;
    t.face({x, y0, a}, {x, y0, b}, {x, y1, b}, {x, y1, a}, {static_cast<double>(s), 0, 0}, colour);
  };
  const double seal = 0.03;
  const double rim = 0.025;
  const double stile = 0.06;
  const double head = top - rim - 0.1;
  part(0, seal, FLOOR, top, DARK);
  part(w - rim, w, FLOOR, top, DARK);
  part(seal, w - rim, top - rim, top, DARK);
  part(seal, w - rim, FLOOR, window, body);
  part(seal, w - rim, head, top - rim, body);
  part(seal, seal + stile, window, head, body);
  part(w - rim - stile, w - rim, window, head, body);
  part(seal + stile, w - rim - stile, window, head, GLASS);
  if (!edges) return;
  const double xi = x - s * LEAF_THICK;
  const double zb = z + way * w;
  t.face({x, top, z}, {xi, top, z}, {xi, top, zb}, {x, top, zb}, {0, 1, 0}, DARK);
  t.face({x, FLOOR, z}, {xi, FLOOR, z}, {xi, FLOOR, zb}, {x, FLOOR, zb}, {0, -1, 0}, DARK);
  t.face({x, FLOOR, z}, {xi, FLOOR, z}, {xi, top, z}, {x, top, z}, {0, 0, static_cast<double>(-way)}, DARK);
  t.face({x, FLOOR, zb}, {xi, FLOOR, zb}, {xi, top, zb}, {x, top, zb}, {0, 0, static_cast<double>(way)}, DARK);
}

/**
 * One car body.  Local frame like every vehicle of the game: x right, y up
 * from the top of the rail, the front towards -z.  `role` (sim/rail.ts
 * carLayout): a driver's cab at its front or back end – both on a unit of
 * one section –, whether its lamps are those of the end of the whole
 * consist, bellows to the next section, a trailer car.  `mode`: its doors
 * ('none' | 'shut' | 'open').
 */
GeometryPtr buildCar(const Consist& c, int kind, const CarRole& role, bool coupler, const std::string& mode) {
  Tris t;
  const Rgb body = liveryOf(kind).body;
  const Rgb accent = liveryOf(kind).accent;
  const double hw = c.width / 2;
  const double L = role.len;
  const bool bellows = role.bellows;
  const double floor = FLOOR;
  const double top = c.height;
  // heights of the bands of the side wall: skirt, belt, windows, band above the windows
  const std::vector<double> bands = wallBands(top);
  const std::array<Rgb, 4> sideColours = {accent, body, GLASS, body};
  // The doors: where along the body (none on the plain body).  The wall is cut at the top of
  // their openings and of their frames too – all along the body, so that the stretches of the
  // wall meet corner to corner and no crack opens between them.
  std::vector<double> doors;
  if (mode != "none")
    for (const DoorPlace& d : carDoors(role))
      if (d.side > 0) doors.push_back(d.z);
  const double opening = doorTop(bands);
  const double frameTop = std::min(opening + DOOR_FRAME, bands[4]);
  std::vector<double> levels = bands;
  if (!doors.empty()) {
    std::set<double> all(bands.begin(), bands.end());
    all.insert(opening);
    all.insert(frameTop);
    levels.assign(all.begin(), all.end());
  }
  // the band of each piece of the side wall between two levels (0 skirt … 3 above the windows)
  std::vector<int> bandOf;
  for (size_t li = 0; li + 1 < levels.size(); li++) {
    const double y = levels[li];
    size_t j = 0;
    while (j + 1 < bands.size() - 1 && bands[j + 1] <= y) j++;
    bandOf.push_back(static_cast<int>(j));
  }

  /** the outline of the body at a place along the car: points from the lower left over the roof to the lower right */
  const auto section = [&](double z, double squeeze, double drop) {
    const double w = hw * squeeze;
    std::vector<P3> pts;
    for (double y : levels) pts.push_back({-w, y - (y > floor + 1 ? drop * ((y - floor) / (top - floor)) : 0), z});
    pts.push_back({-w * 0.84, top - drop, z});
    pts.push_back({w * 0.84, top - drop, z});
    for (size_t i = levels.size(); i-- > 0;) {
      const double y = levels[i];
      pts.push_back({w, y - (y > floor + 1 ? drop * ((y - floor) / (top - floor)) : 0), z});
    }
    return pts;
  };
  // faces between two sections, in the order of the outline: the band of each (4 the edge of the roof, 5 its top) and its colour
  std::vector<int> segBand = bandOf;
  segBand.insert(segBand.end(), {4, 5, 4});
  segBand.insert(segBand.end(), bandOf.rbegin(), bandOf.rend());
  std::vector<Rgb> colours;
  for (int b : segBand) colours.push_back(b < 4 ? sideColours[static_cast<size_t>(b)] : ROOF);
  const size_t nSide = levels.size() - 1;
  const auto loft = [&](const std::vector<P3>& a, const std::vector<P3>& b, int stretch) {
    for (size_t i = 0; i + 1 < a.size(); i++) {
      Rgb colour = colours[i];
      if (stretch != WALL && (i < nSide || i >= nSide + 3)) {
        // a side wall at a door: the frame reaches up to its top, the opening is open up to its top
        const double y1 = levels[(i < nSide ? i : 2 * nSide + 2 - i) + 1];
        if (stretch == OPENING && y1 <= opening) continue;
        if (y1 <= frameTop) colour = accent;
      }
      t.quad(a[i], a[i + 1], b[i + 1], b[i], colour);
    }
    // the floor
    t.quad(a[a.size() - 1], a[0], b[0], b[b.size() - 1], DARK);
  };
  /** close the body at an end; `front`: the end looks towards -z */
  const auto cap = [&](const std::vector<P3>& s, bool front, bool cab) {
    const size_t n = s.size();
    const P3 mid = {0, (floor + top) / 2, s[0][2]};
    for (size_t i = 0; i + 1 < n; i++) {
      // a driver's end: windscreen in the window band, lamps in the belt
      const int band = segBand[i];
      const Rgb colour = !cab ? DARK : band == 2 || band == 3 ? GLASS : band == 1 ? body : band == 0 ? accent : ROOF;
      if (front) t.tri(mid, s[i], s[i + 1], colour);
      else t.tri(mid, s[i + 1], s[i], colour);
    }
    if (front) t.tri(mid, s[n - 1], s[0], DARK);
    else t.tri(mid, s[0], s[n - 1], DARK);
  };

  const double z0 = -L / 2;
  const double z1 = L / 2;
  const double taper = capTaper(L);
  // the sections along the body: where, how much narrower and lower than the body it is there (at a
  // slanted cab end), and what the stretch of wall that begins there is (WALL, FRAME, OPENING)
  struct Station {
    double z, squeeze, drop;
    int stretch;
  };
  std::vector<Station> stations;
  if (role.front) {
    stations.push_back({z0, 0.8, 0.3, WALL});
    stations.push_back({z0 + taper, 1, 0, WALL});
  } else stations.push_back({z0, 1, 0, WALL});
  const double half = DOOR_WIDTH / 2;
  for (double z : doors) {
    stations.push_back({z - half - DOOR_FRAME, 1, 0, FRAME});
    stations.push_back({z - half, 1, 0, OPENING});
    stations.push_back({z + half, 1, 0, FRAME});
    stations.push_back({z + half + DOOR_FRAME, 1, 0, WALL});
  }
  if (role.back) {
    stations.push_back({z1 - taper, 1, 0, WALL});
    stations.push_back({z1, 0.8, 0.3, WALL});
  } else stations.push_back({z1, 1, 0, WALL});
  std::vector<std::vector<P3>> sections;
  for (const Station& st : stations) sections.push_back(section(st.z, st.squeeze, st.drop));
  // (lofting from the front to the back keeps the faces looking outwards)
  for (size_t i = 0; i + 1 < sections.size(); i++) loft(sections[i + 1], sections[i], stations[i].stretch);
  cap(sections[0], true, role.front);
  cap(sections.back(), false, role.back);

  // Open: in each opening a shallow doorway, so that nothing shows through the body.  Shut: the
  // leaves in the opening, and well behind them a dark back (where they meet the frame no light
  // of the sky shows through a crack).
  const double depth = hw * 0.25;
  for (double z : doors) {
    for (int s : {-1, 1}) {
      if (mode == "open") {
        doorway(t, s, s * hw, depth, z, levels, opening);
        continue;
      }
      const double xb = s * (hw - depth);
      t.face({xb, floor, z - half}, {xb, floor, z + half}, {xb, opening, z + half}, {xb, opening, z - half}, {static_cast<double>(s), 0, 0}, INSIDE);
      for (int way : {-1, 1}) doorLeaf(t, s, way, s * hw, z, opening, bands[1], body, false);
    }
  }

  // lamps: white ahead, red behind at the ends of the whole consist – dark at a cab that faces a coupled unit
  const auto lamps = [&](double z, const Rgb& colour, double out) {
    for (int side : {-1, 1}) {
      const double x = side * hw * 0.5;
      t.box(x - 0.16, x + 0.16, floor + 0.66, floor + 0.86, std::min(z, z + out), std::max(z, z + out), colour);
    }
  };
  if (role.front) lamps(z0, role.lampsFront ? LAMP : DARK, -0.03);
  if (role.back || (role.trailer && role.lampsBack)) lamps(z1, role.lampsBack ? TAIL : DARK, 0.03);

  // running gear
  for (double k : {-0.36, 0.36}) t.box(-hw * 0.72, hw * 0.72, 0.02, floor + 0.08, k * L - 1.05, k * L + 1.05, DARK);
  // what sits on the roof (a trailer car has no equipment up there)
  if (!role.trailer) t.box(-hw * 0.55, hw * 0.55, top, top + 0.26, -L * 0.28, L * 0.28, ROOF);
  // the joint to the next section, or the coupler to the next unit or the trailer
  if (bellows) t.box(-hw * 0.8, hw * 0.8, floor + 0.1, top - 0.25, z1, z1 + c.gap, DARK);
  if (coupler) t.box(-0.18, 0.18, floor + 0.2, floor + 0.5, z1, z1 + c.couple.value_or(1.3), DARK);
  return t.geometry();
}

/** The leaves of the doors of a body on side `side` that slide towards `way`, shut: one mesh moves them all. */
GeometryPtr buildLeaves(const Consist& c, int kind, const CarRole& role, int side, int way) {
  Tris t;
  const Rgb body = liveryOf(kind).body;
  const std::vector<double> bands = wallBands(c.height);
  for (const DoorPlace& d : carDoors(role))
    if (d.side == side) doorLeaf(t, side, way, (side * c.width) / 2, d.z, doorTop(bands), bands[1], body, true);
  return t.geometry();
}

/** the four groups of leaves of a body: (side, way) – left and right, sliding to the front and to the back */
constexpr std::array<std::array<int, 2>, 4> LEAF_GROUPS = {{{-1, -1}, {-1, 1}, {1, -1}, {1, 1}}};

double ease(double x) { return x <= 0 ? 0 : x >= 1 ? 1 : x * x * (3 - 2 * x); }

/** The roof of a body and the top of its roof equipment, where a pantograph stands. */
constexpr double PANTO_FOOT = 0.26;

std::string bit(bool b) { return b ? "1" : "0"; }

}  // namespace

std::vector<DoorPlace> carDoors(const CarRole& role) {
  const double L = role.len;
  const double taper = capTaper(L);
  // the straight part of the side wall, between the slanted cab ends
  const double from = -L / 2 + (role.front ? taper : 0);
  const double to = L / 2 - (role.back ? taper : 0);
  const double half = DOOR_WIDTH / 2 + DOOR_FRAME;
  std::vector<DoorPlace> out;
  for (double k : {-0.25, 0.25}) {
    const double z = k * L;
    if (z - half < from + 0.05 || z + half > to - 0.05 || std::abs(z) < half + 0.05) continue;
    for (int side : {-1, 1}) out.push_back(DoorPlace{z, side, DOOR_WIDTH});
  }
  return out;
}

RailActors::RailActors() : group(make<Group>()), material_(lambert({.vertexColors = true})) {
  group->name = "rail vehicles";
  // A pantograph in two parts: the frame of arms, one metre high (it is stretched to the height the
  // pantograph stands at), and the head with the collector bow that slides along the wire (moved to
  // the top of the arms).
  Tris a;
  // the lower and upper arm, seen from the side a flat "Z" – as two bars rising from the frame
  a.box(-0.05, 0.05, 0, 1, -0.62, -0.52, DARK);
  a.box(-0.05, 0.05, 0, 1, 0.22, 0.32, DARK);
  a.box(-0.45, 0.45, 0, 0.05, -0.7, 0.4, DARK);
  Tris h;
  h.box(-0.78, 0.78, 0, 0.07, -0.08, 0.08, DARK);
  h.box(-0.05, 0.05, -0.06, 0, -0.6, 0.3, DARK);
  pantoArm_ = a.geometry();
  pantoHead_ = h.geometry();
  displayGeo_ = planeGeometry(1.7, 0.32);
}

void RailActors::detail(bool on) {
  if (on == detailOn_) return;
  detailOn_ = on;
  for (auto& [id, s] : shown_) group->remove(s.g);
  shown_.clear();
  showing_.clear();
  if (!on) doorGeometries_.clear();
}

GeometryPtr RailActors::carGeometry(const Consist& c, int kind, const CarRole& role, bool coupler, const std::string& mode) {
  const std::string key = std::to_string(kind) + ":" + jsStr(role.len) + ":" + bit(role.front) + bit(role.back) + bit(role.lampsFront) + bit(role.lampsBack) + bit(role.trailer) + bit(role.bellows) + bit(coupler);
  auto& cache = mode == "none" ? geometries_ : doorGeometries_;
  const std::string k = mode == "none" ? key : key + ":" + mode;
  auto it = cache.find(k);
  if (it != cache.end()) return it->second;
  GeometryPtr g = buildCar(c, kind, role, coupler, mode);
  cache[k] = g;
  return g;
}

GeometryPtr RailActors::leafGeometry(const Consist& c, int kind, const CarRole& role, int side, int way) {
  const std::string key = std::to_string(kind) + ":" + jsStr(role.len) + ":" + bit(role.front) + bit(role.back) + ":leaves" + std::to_string(side) + std::to_string(way);
  auto it = doorGeometries_.find(key);
  if (it != doorGeometries_.end()) return it->second;
  GeometryPtr g = buildLeaves(c, kind, role, side, way);
  doorGeometries_[key] = g;
  return g;
}

MaterialPtr RailActors::displayMaterial(const std::string& text) {
  auto it = displays_.find(text);
  if (it != displays_.end()) return it->second;
  const Canvas c(512, 96);
  const Ctx2D g = c.getContext2D();
  g.fillStyle("#0b0b0b");
  g.fillRect(0, 0, 512, 96);
  g.fillStyle("#ffb21a");
  g.font("bold 56px \"Arial Narrow\", Arial, sans-serif");
  g.textBaseline("middle");
  const size_t nl = text.find('\n');
  const std::string ref = text.substr(0, nl);
  std::string rest = nl == std::string::npos ? "" : text.substr(nl + 1);
  std::replace(rest.begin(), rest.end(), '\n', ' ');
  g.textAlign("left");
  g.fillText(ref, 14, 50, 110);
  g.textAlign("left");
  g.font("bold 46px \"Arial Narrow\", Arial, sans-serif");
  g.fillText(rest, 140, 50, 360);
  TexturePtr tex = canvasTexture(c);
  tex->srgb = true;
  tex->anisotropy = 4;
  MaterialPtr m = basic({.map = tex});
  displays_[text] = m;
  return m;
}

void RailActors::placeLeaves(Shown& s, double open) {
  const double out = LEAF_OUT * ease(open / 0.25);
  // (sliding starts when a leaf is clear of the wall)
  const double slide = LEAF_SLIDE * ease((open - 0.2) / 0.8);
  for (CarParts& p : s.cars) {
    if (!p.doors) continue;
    p.mesh->geometry = open > 0 ? p.doors->open : p.doors->shut;
    for (size_t k = 0; k < LEAF_GROUPS.size(); k++) {
      const int side = LEAF_GROUPS[k][0];
      const int way = LEAF_GROUPS[k][1];
      Mesh& leaf = *p.doors->leaves[k];
      leaf.visible = open > 0;
      leaf.position.set(side * out, 0, way * slide);
      leaf.updateMatrix();
    }
  }
}

void RailActors::update(const std::vector<std::unique_ptr<RailVehicle>>& vehicles, const std::vector<RailLine>& lines) {
  std::vector<const RailVehicle*> list;
  list.reserve(vehicles.size());
  for (const auto& vp : vehicles) list.push_back(vp.get());
  update(list, lines);
}

void RailActors::update(const std::vector<const RailVehicle*>& vehicles, const std::vector<RailLine>& lines) {
  std::unordered_set<int> seen;
  for (const RailVehicle* vp : vehicles) {
    const RailVehicle& v = *vp;
    seen.insert(v.id);
    auto found = shown_.find(v.id);
    if (found == shown_.end()) {
      Shown s;
      s.g = make<Group>();
      const std::vector<CarRole>& roles = carLayout(*v.consist);
      for (size_t i = 0; i < roles.size(); i++) {
        const CarRole& role = roles[i];
        const bool coupler = role.back && i < roles.size() - 1;
        // (with the detail a body with doors, drawn shut – see placeLeaves)
        const bool doors = detailOn_ && !carDoors(role).empty();
        CarParts p;
        p.mesh = make<Mesh>(carGeometry(*v.consist, v.kind, role, coupler, doors ? "shut" : "none"), material_);
        p.mesh->castShadow = true;
        p.mesh->rotation.order(Euler::YXZ);
        if (role.panto && !role.trailer && v.kind != RAIL_TRAIN) {
          // the pantograph of this unit: its arms and its head, raised and lowered with v.panto
          p.arm = make<Mesh>(pantoArm_, material_);
          p.head = make<Mesh>(pantoHead_, material_);
          p.arm->name = "pantograph";
          p.head->name = "pantograph head";
          p.arm->position.set(0, v.consist->height + PANTO_FOOT, 0);
          p.head->position.set(0, v.consist->height + PANTO_FOOT, 0);
          p.mesh->add(p.arm, p.head);
        }
        if (doors) {
          // the leaves, in four groups that each move one way; drawn only while the doors are not shut
          CarDoors d;
          for (size_t k = 0; k < LEAF_GROUPS.size(); k++) {
            auto leaf = make<Mesh>(leafGeometry(*v.consist, v.kind, role, LEAF_GROUPS[k][0], LEAF_GROUPS[k][1]), material_);
            leaf->name = "door leaves";
            leaf->visible = false;
            // (moved only when the doors move)
            leaf->matrixAutoUpdate = false;
            p.mesh->add(leaf);
            d.leaves[k] = leaf;
          }
          d.shut = p.mesh->geometry;
          d.open = carGeometry(*v.consist, v.kind, role, coupler, "open");
          p.doors = std::move(d);
        }
        s.g->add(p.mesh);
        s.cars.push_back(std::move(p));
      }
      group->add(s.g);
      found = shown_.emplace(v.id, std::move(s)).first;
    }
    Shown& s = found->second;
    s.g->visible = &v != hidden;
    // (lowered it lies folded on the roof; raised it presses its head against the wire)
    const double reach = CONTACT_WIRE_HEIGHT - v.consist->height - PANTO_FOOT;
    const double up = 0.16 + (reach - 0.16) * std::max(0.0, std::min(1.0, v.panto));
    for (size_t i = 0; i < v.cars.size() && i < s.cars.size(); i++) {
      const RailCar& car = v.cars[i];
      CarParts& p = s.cars[i];
      p.mesh->position.set(car.x, car.y, car.z);
      p.mesh->rotation.set(car.pitch, car.yaw, 0);
      if (p.arm && p.arm->scale.y != up) {
        p.arm->scale.y = up;
        p.head->position.y = v.consist->height + PANTO_FOOT + up;
      }
    }
    // the doors: only when they have moved since the last picture
    const double open = std::max(0.0, std::min(1.0, v.doors));
    if (s.doorsAt != open) {
      s.doorsAt = open;
      placeLeaves(s, open);
    }
    // the line and its destination over the windscreen (and at the back of the last car)
    const RailLine* line = v.line && v.line->index >= 0 && v.line->index < static_cast<int>(lines.size()) ? &lines[static_cast<size_t>(v.line->index)] : nullptr;
    const std::string text = line ? line->ref + "\n" + line->to : "";
    auto sh = showing_.find(v.id);
    if (sh == showing_.end() || sh->second != text) {
      showing_[v.id] = text;
      // (over the windscreen at the front, and at the back of the last body with a cab – not on a trailer car)
      const std::vector<CarRole>& roles = carLayout(*v.consist);
      size_t last = roles.size() - 1;
      while (last > 0 && !roles[last].back) last--;
      for (size_t idx : {static_cast<size_t>(0), last}) {
        CarParts& p = s.cars[idx];
        if (p.display) {
          p.mesh->remove(p.display);
          p.display.reset();
        }
      }
      if (!text.empty()) {
        const Consist& c = *v.consist;
        auto front = make<Mesh>(displayGeo_, displayMaterial(text));
        front->name = "display";
        front->position.set(0, c.height - 0.42, -roles[0].len / 2 - 0.035);
        front->rotation.y = M_PI;
        s.cars[0].mesh->add(front);
        s.cars[0].display = front;
        if (last > 0) {
          auto back = make<Mesh>(displayGeo_, displayMaterial(text));
          back->name = "display";
          back->position.set(0, c.height - 0.42, roles[last].len / 2 + 0.035);
          s.cars[last].mesh->add(back);
          s.cars[last].display = back;
        }
      }
    }
  }
  for (auto it = shown_.begin(); it != shown_.end();) {
    if (seen.count(it->first)) {
      ++it;
      continue;
    }
    group->remove(it->second.g);
    showing_.erase(it->first);
    it = shown_.erase(it);
  }
}

void RailActors::dispose() {
  geometries_.clear();
  doorGeometries_.clear();
}

}  // namespace fs

// SPDX-License-Identifier: AGPL-3.0-only
// LINK: world core platform geo
// Origin: tests/world/facade.test.ts @ v1.7.7
/**
 * What the faces of a building need for their facades (src/world/facade.h).
 * Beside what the original's test asks, the runs and numbers found for its
 * house are the very ones the original finds (printed by it under node).
 */
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <vector>

#include "core/js.h"
#include "world/facade.h"
#include "world/tilefmt.h"

using namespace fs;

namespace {

using P = std::array<double, 3>;

/** A mesh the way the map build makes the faces of buildings (tools/mapgen/buildings.ts), to compare with. */
class Faces {
 public:
  std::vector<double> pos;
  std::vector<double> a16;
  std::vector<double> idx;

  void wall(const std::vector<P>& ring, bool onGround) {
    const P n = normal(ring);
    const double hl = std::hypot(n[0], n[2]);
    const P ua = {n[2] / hl, 0, -n[0] / hl};
    double lo = JS_INF;
    double eave = -JS_INF;
    for (const P& p : ring) {
      lo = std::min(lo, p[1]);
      eave = std::max(eave, p[1]);
    }
    for (const P& p : ring)
      if (p[1] > lo + 0.6 && p[1] < eave) eave = p[1];
    double uMin = JS_INF;
    double uMax = -JS_INF;
    for (const P& p : ring) {
      const double u = p[0] * ua[0] + p[2] * ua[2];
      uMin = std::min(uMin, u);
      uMax = std::max(uMax, u);
    }
    const double L = uMax - uMin;
    add(ring, ua, {0, 1, 0}, [&](double u, double, double y) { return std::vector<double>{u, y - lo, L, eave - lo}; }, onGround ? lo + 0.02 : -JS_INF);
  }

  void roof(const std::vector<P>& ring) {
    const P n = normal(ring);
    if (n[1] >= 0.985) {
      add(ring, {1, 0, 0}, {0, 0, 1}, [](double u, double v, double) { return std::vector<double>{u, v, 0, 0}; }, -JS_INF);
      return;
    }
    const double hl = std::hypot(n[0], n[2]);
    const P ua = {n[2] / hl, 0, -n[0] / hl};
    P va = {n[1] * ua[2] - n[2] * ua[1], n[2] * ua[0] - n[0] * ua[2], n[0] * ua[1] - n[1] * ua[0]};
    if (va[1] < 0) va = {-va[0], -va[1], -va[2]};
    add(ring, ua, va, [](double u, double v, double) { return std::vector<double>{u, v, 0, 0}; }, -JS_INF);
  }

  /** a face with numbers no rule gives */
  void odd(const std::vector<P>& ring, const std::vector<double>& values) {
    const double first = static_cast<double>(pos.size() / 3);
    for (const P& p : ring) pos.insert(pos.end(), p.begin(), p.end());
    a16.insert(a16.end(), values.begin(), values.end());
    for (size_t i = 1; i + 1 < ring.size(); i++) {
      idx.push_back(first);
      idx.push_back(first + static_cast<double>(i));
      idx.push_back(first + static_cast<double>(i) + 1);
    }
  }

  struct Sections {
    std::vector<int16_t> pos;
    std::vector<uint16_t> idx;
    std::vector<int16_t> a16;
  };

  /** as the tile holds it: whole numbers around the middle of the tile */
  Sections sections(double cx, double cz, double yBase) const {
    Sections s;
    s.pos.resize(pos.size());
    for (size_t i = 0; i + 2 < pos.size(); i += 3) {
      s.pos[i] = static_cast<int16_t>(jsRound((pos[i] - cx) / POS_XZ_SCALE));
      s.pos[i + 1] = static_cast<int16_t>(jsRound((pos[i + 1] - yBase) / POS_Y_SCALE));
      s.pos[i + 2] = static_cast<int16_t>(jsRound((pos[i + 2] - cz) / POS_XZ_SCALE));
    }
    for (double v : idx) s.idx.push_back(static_cast<uint16_t>(v));
    for (double v : a16) s.a16.push_back(static_cast<int16_t>(jsRound(v / UV_SCALE)));
    return s;
  }

 private:
  void add(const std::vector<P>& ring, const P& uAxis, const P& vAxis, const std::function<std::vector<double>(double u, double v, double y)>& attr, double lowerBelow) {
    const double first = static_cast<double>(pos.size() / 3);
    std::vector<double> us, vs;
    for (const P& p : ring) {
      us.push_back(p[0] * uAxis[0] + p[1] * uAxis[1] + p[2] * uAxis[2]);
      vs.push_back(p[0] * vAxis[0] + p[1] * vAxis[1] + p[2] * vAxis[2]);
    }
    double uMin = JS_INF;
    double vMin = JS_INF;
    for (double u : us) uMin = std::min(uMin, u);
    for (double v : vs) vMin = std::min(vMin, v);
    for (size_t i = 0; i < ring.size(); i++) {
      const P& p = ring[i];
      const std::vector<double> four = attr(us[i] - uMin, vs[i] - vMin, p[1]);
      a16.insert(a16.end(), four.begin(), four.end());
      pos.push_back(p[0]);
      pos.push_back(p[1] <= lowerBelow ? p[1] - SINK : p[1]);
      pos.push_back(p[2]);
    }
    for (size_t i = 1; i + 1 < ring.size(); i++) {
      idx.push_back(first);
      idx.push_back(first + static_cast<double>(i));
      idx.push_back(first + static_cast<double>(i) + 1);
    }
  }

  static P normal(const std::vector<P>& ring) {
    double nx = 0;
    double ny = 0;
    double nz = 0;
    for (size_t i = 0; i < ring.size(); i++) {
      const P& a = ring[i];
      const P& b = ring[(i + 1) % ring.size()];
      nx += (a[1] - b[1]) * (a[2] + b[2]);
      ny += (a[2] - b[2]) * (a[0] + b[0]);
      nz += (a[0] - b[0]) * (a[1] + b[1]);
    }
    const double l = std::sqrt(nx * nx + ny * ny + nz * nz);
    return {nx / l, ny / l, nz / l};
  }
};

}  // namespace

int main() {
  // --- what the faces of a building need for their facades follows from their corners
  {
    Faces f;
    // a house: 8.47 m × 6.2 m, skew to the grid, eaves 5.53 m above the ground at 187.31 m, ridge at 8.2 m
    const double g = 187.31;
    const P a = {1010.013, g, -705.031};
    const P dir = {0.8829, 0, 0.4695};
    const P across = {-0.4695, 0, 0.8829};
    auto at = [&](double along, double over, double up) -> P { return {a[0] + dir[0] * along + across[0] * over, g + up, a[2] + dir[2] * along + across[2] * over}; };
    // the long walls (on the ground), the gable walls (five corners), two roof planes
    f.wall({at(0, 0, 0), at(8.47, 0, 0), at(8.47, 0, 5.53), at(0, 0, 5.53)}, true);
    f.wall({at(8.47, 6.2, 0), at(0, 6.2, 0), at(0, 6.2, 5.53), at(8.47, 6.2, 5.53)}, true);
    f.wall({at(8.47, 0, 0), at(8.47, 6.2, 0), at(8.47, 6.2, 5.53), at(8.47, 3.1, 8.2), at(8.47, 0, 5.53)}, true);
    f.wall({at(0, 6.2, 0), at(0, 0, 0), at(0, 0, 5.53), at(0, 3.1, 8.2), at(0, 6.2, 5.53)}, true);
    f.roof({at(0, 0, 5.53), at(8.47, 0, 5.53), at(8.47, 3.1, 8.2), at(0, 3.1, 8.2)});
    f.roof({at(8.47, 6.2, 5.53), at(0, 6.2, 5.53), at(0, 3.1, 8.2), at(8.47, 3.1, 8.2)});
    // a wall above the roof of a lower part of the building (its foot is not sunk), a level roof with six corners
    f.wall({at(10, 0, 3.07), at(14, 0, 3.07), at(14, 0, 6.9), at(10, 0, 6.9)}, false);
    f.roof({at(10, 6, 6.9), at(12, 6, 6.9), at(12, 4, 6.9), at(14, 4, 6.9), at(14, 0, 6.9), at(10, 0, 6.9)});
    // and a face whose numbers no rule gives
    const std::vector<double> oddValues = {3, -7, 11, 2, 400, 5, 11, 2, 400, 900, 11, 2};
    std::vector<double> scaled;
    for (double v : oddValues) scaled.push_back(v * UV_SCALE);
    f.odd({at(20, 0, 0), at(22, 0, 0), at(22, 0, 3)}, scaled);

    const Faces::Sections s = f.sections(1250, -750, 180);
    // (the mesh is the one the original's test makes)
    assert((s.pos == std::vector<int16_t>{-11999, 571,  2248, -11625, 571,  2447, -11625, 1284, 2447, -11999, 1284, 2248, -11771, 571,  2721, -12145, 571,  2522, -12145, 1284, 2522, -11771, 1284, 2721,
                                          -11625, 571,  2447, -11771, 571,  2721, -11771, 1284, 2721, -11698, 1551, 2584, -11625, 1284, 2447, -12145, 571,  2522, -11999, 571,  2248, -11999, 1284, 2248,
                                          -12072, 1551, 2385, -12145, 1284, 2522, -11999, 1284, 2248, -11625, 1284, 2447, -11698, 1551, 2584, -12072, 1551, 2385, -11771, 1284, 2721, -12145, 1284, 2522,
                                          -12072, 1551, 2385, -11698, 1551, 2584, -11558, 1038, 2483, -11381, 1038, 2577, -11381, 1421, 2577, -11558, 1421, 2483, -11699, 1421, 2748, -11610, 1421, 2795,
                                          -11564, 1421, 2707, -11475, 1421, 2754, -11381, 1421, 2577, -11558, 1421, 2483, -11116, 731,  2718, -11028, 731,  2765, -11028, 1031, 2765}));
    const PackedFaces packed = packFaces(s.pos, s.idx, s.a16);
    std::vector<int> rules, sizes;
    for (uint16_t r : packed.run) {
      rules.push_back(r & 7);
      sizes.push_back(r >> 3);
    }
    assert((rules == std::vector<int>{FACE_WALL_SUNK, FACE_WALL_SUNK, FACE_WALL_SUNK, FACE_WALL_SUNK, FACE_ROOF_SLOPED, FACE_ROOF_SLOPED, FACE_WALL, FACE_ROOF_FLAT, FACE_EXPLICIT}));
    assert((sizes == std::vector<int>{4, 4, 5, 5, 4, 4, 4, 6, 3}));
    assert((packed.run == std::vector<uint16_t>{36, 36, 44, 44, 34, 34, 35, 49, 24}));
    assert(packed.uvx.size() == 12);  // only the odd face keeps its numbers
    assert(packed.ruled == static_cast<int>(s.pos.size() / 3) - 3);

    const std::vector<int16_t> back = unpackFaces(s.pos, packed.run, &packed.uvx);
    assert(back.size() == s.a16.size());
    int worst = 0;
    for (size_t i = 0; i < back.size(); i++) worst = std::max(worst, std::abs(back[i] - s.a16[i]));
    assert(worst <= 2);  // to 4 cm what the map build wanted
    assert((std::vector<int16_t>(back.end() - 12, back.end()) == std::vector<int16_t>{3, -7, 11, 2, 400, 5, 11, 2, 400, 900, 11, 2}));  // the odd face has its numbers back as they were
    // the gable wall: its height is that of the eaves, not of the ridge; its length the depth of the house
    const size_t gable = (4 + 4) * 4;
    assert(std::fabs(back[gable + 3] * UV_SCALE - 5.53) < 0.05 && std::fabs(back[gable + 2] * UV_SCALE - 6.2) < 0.05);
    // the foot of a wall on the ground counts as height 0 although it is sunk into the ground
    assert(back[1] == 0);
    assert(std::fabs(s.pos[1] * POS_Y_SCALE - (g - SINK - 180)) < 0.01);
    // … and every number is the one the original works out
    assert((back == std::vector<int16_t>{0,   0,   424, 276, 424, 0,   424, 276, 424, 276, 424, 276, 0,   276, 424, 276, 0,   0,   424, 276, 424, 0,   424, 276, 424, 276, 424, 276, 0,   276, 424, 276,
                                         0,   0,   310, 276, 310, 0,   310, 276, 310, 276, 310, 276, 155, 410, 310, 276, 0,   276, 310, 276, 0,   0,   310, 276, 310, 0,   310, 276, 310, 276, 310, 276,
                                         155, 410, 310, 276, 0,   276, 310, 276, 0,   0,   0,   0,   424, 0,   0,   0,   424, 205, 0,   0,   0,   205, 0,   0,   0,   0,   0,   0,   424, 0,   0,   0,
                                         424, 205, 0,   0,   0,   205, 0,   0,   0,   0,   200, 192, 200, 0,   200, 192, 200, 192, 200, 192, 0,   192, 200, 192, 0,   265, 0,   0,   89,  312, 0,   0,
                                         135, 224, 0,   0,   224, 271, 0,   0,   318, 94,  0,   0,   141, 0,   0,   0,   3,   -7,  11,  2,   400, 5,   11,  2,   400, 900, 11,  2}));
    // a tile without the numbers of its odd faces: they are 0
    const std::vector<int16_t> bare = unpackFaces(s.pos, packed.run, nullptr);
    assert((std::vector<int16_t>(bare.end() - 12, bare.end()) == std::vector<int16_t>(12, 0)) && std::vector<int16_t>(bare.begin(), bare.end() - 12) == std::vector<int16_t>(back.begin(), back.end() - 12));
    // one rule at a time
    std::vector<int16_t> out(s.a16.size(), 0);
    assert(deriveFace(s.pos, 0, 4, FACE_ROOF_FLAT, out));
    assert((std::vector<int16_t>(out.begin(), out.begin() + 16) == std::vector<int16_t>{0, 0, 0, 0, 374, 199, 0, 0, 374, 199, 0, 0, 0, 0, 0, 0}));
    assert(!deriveFace(s.pos, 0, 2, FACE_WALL, out) && !deriveFace(s.pos, 0, 4, 7, out) && deriveFace(s.pos, 26, 6, FACE_WALL, out) && deriveFace(s.pos, 26, 6, FACE_ROOF_SLOPED, out));
  }

  // --- a mesh without faces that follow a rule is stored as it is
  {
    const std::vector<int16_t> pos = {0, 0, 0, 100, 0, 0, 100, 0, 100, 0, 0, 100, 7, 7, 7};
    const std::vector<uint16_t> idx = {0, 1, 2, 0, 2, 3};
    const std::vector<int16_t> a16 = {9, 9, 9, 9, 8, 8, 8, 8, 7, 7, 7, 7, 6, 6, 6, 6, 5, 5, 5, 5};
    const PackedFaces packed = packFaces(pos, idx, a16);
    // (the last corner belongs to no triangle: runs without a rule that follow each other are one run)
    assert((packed.run == std::vector<uint16_t>{(5 << 3) | FACE_EXPLICIT}));
    assert(unpackFaces(pos, packed.run, &packed.uvx) == a16);
    // (indices of 32 bits, as a big mesh has them: the same)
    const std::vector<uint32_t> idx32 = {0, 1, 2, 0, 2, 3};
    assert(packFaces(pos, idx32, a16).run == packed.run);
  }

  std::printf("facade: all checks passed\n");
  return 0;
}

// SPDX-License-Identifier: AGPL-3.0-only
/** Geometry checks against the TypeScript reference behaviour. */
#include "../src/core/geom.h"
#include <cassert>
#include <cstdio>

using namespace fs;

int main() {
  // angles
  assert(std::fabs(angleOf(1, 0)) < 1e-12);            // east = 0
  assert(std::fabs(angleOf(0, -1) - PI_ / 2) < 1e-12); // north = +π/2
  assert(std::fabs(wrapAngle(3 * PI_) - PI_) < 1e-12);
  assert(std::fabs(wrap2Pi(-0.5) - (TAU - 0.5)) < 1e-12);

  // line/line: crossing at (1,1) from (0,0)+(1,0) and (1,0)+(0,1)
  LineHit h = lineLine(0, 0, 1, 0, 1, 0, 0, 1);
  assert(h.has && std::fabs(h.t - 1) < 1e-12 && std::fabs(h.u - 1) < 1e-12);
  assert(!lineLine(0, 0, 1, 0, 0, 1, 1, 0).has);  // parallel

  // segment/segment: non-crossing segments report no hit
  assert(!segSeg(0, 0, 1, 0, 0, 1, 1, 1).has);

  // ring area: unit square (0,0)-(1,0)-(1,1)-(0,1) in x,z → area 1
  assert(std::fabs(ringArea({0, 0, 1, 0, 1, 1, 0, 1}) - 1.0) < 1e-12);
  assert(pointInRing(0.5, 0.5, {0, 0, 1, 0, 1, 1, 0, 1}));
  assert(!pointInRing(2, 0.5, {0, 0, 1, 0, 1, 1, 0, 1}));

  // projectOnSegment: midpoint of a 10 m segment
  assert(std::fabs(projectOnSegment(5, 1, 0, 0, 10, 0) - 0.5) < 1e-12);
  assert(projectOnSegment(-3, 1, 0, 0, 10, 0) == 0);

  // polyline: straight 10 m line along +x
  Polyline3 pl = makePolyline({0, 0, 0, 10, 0, 0});
  assert(std::fabs(pl.len - 10) < 1e-12);
  PolySample s = samplePolyline(pl, 4, newPolySample());
  assert(std::fabs(s.x - 4) < 1e-12 && std::fabs(s.tx - 1) < 1e-12 && s.seg == 0);

  // projection onto the polyline: point above the middle
  PolyProjection pr = projectOnPolyline(pl, 5, 2, PolyProjection());
  assert(std::fabs(pr.s - 5) < 1e-12);
  assert(std::fabs(pr.off - 2) < 1e-12);  // positive = left of +x = -z... check sign
  assert(pr.d2 > 0);

  // offsetPolyline: 4-point square, offset outward keeps 4 points
  std::vector<double> sq = {0, 0, 0, 10, 0, 0, 10, 0, 10, 0, 0, 10};
  auto off = offsetPolyline(sq, 1);
  assert(off.size() == sq.size());

  // slicePolyline of the 10 m line
  auto sliced = slicePolyline(pl, 2, 6);
  assert(sliced.size() == 6);
  assert(std::fabs(sliced[0] - 2) < 1e-9 && std::fabs(sliced[3] - 6) < 1e-9);

  // Grid2
  Grid2<int> g(10);
  g.insertBox(0, 0, 5, 5, 1);
  g.insertPoint(20, 20, 2);
  assert(g.at(1, 1).size() == 1 && g.at(1, 1)[0] == 1);
  int seen = 0;
  g.queryBox(18, 18, 22, 22, [&](int) { seen++; });
  assert(seen == 1);

  // RNG determinism + hash01
  Rng r1(42), r2(42);
  for (int i = 0; i < 100; i++) {
    double a = r1(), b = r2();
    assert(a == b && a >= 0 && a < 1);
  }
  assert(hash01(0) == hash01(0));
  assert(hash01(0) != hash01(1));

  std::printf("geom: all checks passed\n");
  return 0;
}

// SPDX-License-Identifier: AGPL-3.0-only
// LINK: world core platform geo
/**
 * The geometry helpers (src/core/geom.h) against the original: every number
 * expected here was printed by src/core/geom.ts @ v1.7.7 under node, for the
 * same calls.  Where only +, −, ·, / and square roots are involved the port
 * must give the very same double; angles (atan2) may differ in the last digit.
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/geom.h"

using namespace fs;

namespace {

bool same(double a, double b) { return a == b || (a != a && b != b); }
bool near(double a, double b) { return std::fabs(a - b) <= 4e-16 * std::max(1.0, std::fabs(a)); }

void expectList(const std::vector<double>& got, const std::vector<double>& want, const char* what) {
  bool ok = got.size() == want.size();
  for (size_t i = 0; ok && i < got.size(); i++) ok = same(got[i], want[i]);
  if (!ok) {
    std::fprintf(stderr, "%s:", what);
    for (double v : got) std::fprintf(stderr, " %.17g", v);
    std::fprintf(stderr, "\n");
    assert(false);
  }
}

}  // namespace

int main() {
  // (the numbers of the original are those of a multiplication and an addition, each rounded:
  // a build that fuses them into one rounding gives others – it needs -ffp-contract=off)
  {
    volatile double a = 0.1, b = 10.0, c = -1.0;
    const double fused = std::fma(a, b, c);
    const double plain = a * b + c;
    assert(fused != 0 && plain == 0 && "the compiler fuses a*b+c: build with -ffp-contract=off");
  }

  // Math.hypot as the original's engine has it (these differ from sqrt(a*a + b*b) in the last digit)
  const double hy[][3] = {
      {624.9834036570896, 972.5775727874495, 1156.0758582099738}, {-105.59368091895882, 870.6712731489313, 877.0510198026087},
      {342.14496675047326, 119.49634604132564, 362.4121341648355}, {74.13721134613138, 390.2935988224547, 397.27247499413323},
      {-387.91799516785795, 207.91673716526327, 440.1246875244553}, {587.2864730596945, -611.9837759118452, 848.1919260510648},
      {-610.7611058329983, 460.22355624485, 764.744957564941},     {-326.03836028186527, 627.845764918181, 707.4541093957849},
      {350.1563078491745, -849.3084906830027, 918.6589967299107},  {745.9426530385123, 626.5776164953494, 974.1816828062041},
      {-377.0542141874573, 132.54737906742253, 399.6732266917112}, {38.45793057161284, 143.31126266313322, 148.38170517268145},
  };
  for (const auto& h : hy) assert(jsHypot(h[0], h[1]) == h[2]);
  assert(jsHypot(0, 0) == 0 && jsHypot(3, 4) == 5 && jsHypot(-5, 12) == 13 && jsHypot(1e200, 1e200) == 1.414213562373095e+200 && jsHypot(1e-200, 1e-200) == 1.414213562373095e-200 && jsHypot(0, -7.25) == 7.25);
  assert(std::isnan(jsHypot(JS_NAN, 1)) && jsHypot(JS_NAN, JS_INF) == JS_INF && jsHypot(-JS_INF, 2) == JS_INF);

  // a line with a bend, a point given twice, heights
  const Polyline3 pl = makePolyline({0, 10, 0, 30, 11, 0, 30, 12, 40, 30, 12, 40, -10, 14, 70});
  assert(pl.len == 120);
  expectList(pl.cum, {0, 30, 70, 70, 120}, "cum");
  {
    const struct {
      double s;
      int hint;
      double x, y, z, tx, tz;
      int seg;
    } cases[] = {
        {-5, 0, 0, 10, 0, 1, 0, 0},
        {0, 0, 0, 10, 0, 1, 0, 0},
        {12.5, 0, 12.5, 10.416666666666666, 0, 1, 0, 0},
        {30, 0, 30, 11, 0, 1, 0, 0},
        {30, 1, 30, 11, 0, 0, 1, 1},
        {55, 3, 30, 11.625, 25, 0, 1, 1},
        {70, 2, 30, 12, 40, 0, 0, 2},
        {70, 3, 30, 12, 40, -0.8, 0.6, 3},
        {95.5, 0, 9.600000000000001, 13.02, 55.3, -0.8, 0.6, 3},
        {1000000000, 1, -10, 14, 70, -0.8, 0.6, 3},
        {JS_NAN, 0, JS_NAN, JS_NAN, JS_NAN, 1, 0, 0},
    };
    PolySample sm = newPolySample();
    for (const auto& c : cases) {
      PolySample& back = samplePolyline(pl, c.s, sm, c.hint);
      assert(&back == &sm);
      if (!(same(sm.x, c.x) && same(sm.y, c.y) && same(sm.z, c.z) && same(sm.tx, c.tx) && same(sm.tz, c.tz) && sm.seg == c.seg)) {
        std::fprintf(stderr, "sample %g hint %d: %.17g %.17g %.17g %.17g %.17g %d\n", c.s, c.hint, sm.x, sm.y, sm.z, sm.tx, sm.tz, sm.seg);
        assert(false);
      }
    }
    // a line of one point
    const Polyline3 dot = makePolyline({4, 5, 6});
    samplePolyline(dot, 3, sm);
    assert(sm.x == 4 && sm.y == 5 && sm.z == 6 && sm.tx == 1 && sm.tz == 0 && sm.seg == 0 && dot.len == 0);
  }
  {
    // positive `off` = LEFT of the direction: left of +x is −z
    const struct {
      double x, z, s, off, d2;
      int seg;
    } cases[] = {
        {5, 2, 5, -2, 4, 0}, {5, -2, 5, 2, 4, 0}, {31, 20, 50, 1, 1, 1}, {29, 20, 50, -1, 1, 1}, {40, 50, 70, 10, 200, 1}, {-20, 80, 120, 2, 200, 3}, {30, 40, 70, 0, 0, 1}, {10, 55.5, 95.3, 0.4000000000000014, 0.16000000000000114, 3},
    };
    PolyProjection pr;
    for (const auto& c : cases) {
      projectOnPolyline(pl, c.x, c.z, pr);
      if (!(pr.s == c.s && pr.off == c.off && pr.d2 == c.d2 && pr.seg == c.seg)) {
        std::fprintf(stderr, "project %g %g: %.17g %.17g %.17g %d\n", c.x, c.z, pr.s, pr.off, pr.d2, pr.seg);
        assert(false);
      }
    }
    projectOnPolyline(pl, 31, 20, pr, 0, 0);
    assert(pr.s == 30 && pr.off == -20 && pr.d2 == 401 && pr.seg == 0);
    // no segment to look at: nothing found (the distance stays infinite)
    projectOnPolyline(pl, 31, 20, pr, 2, 1);
    assert(pr.s == 0 && pr.off == 0 && pr.d2 == JS_INF && pr.seg == 2);
  }
  expectList(offsetPolyline(pl.pts, 1.5), {0, 10, -1.5, 31.5, 11, -1.5, 31.5, 12, 40, 32.25, 12, 43, -9.1, 14, 71.2}, "offset");
  expectList(offsetPolyline(pl.pts, -2, 1.2), {0, 10, 2, 28.302943725152286, 11, 1.6970562748477138, 28, 12, 40, 28.56, 12, 38.08, -11.2, 14, 68.4}, "offset with a mitre limit");
  expectList(offsetPolyline({0, 0, 0, 10, 0, 0, 0, 0, 0.001}, 1), {0, 0, -1, 12.499999996875, 0, -0.00012500000248462367, 0.0000999999995, 0, 1.000999995}, "offset at a hairpin");
  expectList(slicePolyline(pl, 12, 60), {12, 10.4, 0, 30, 11, 0, 30, 11.75, 30}, "slice");
  expectList(slicePolyline(pl, -3, 1e9), {0, 10, 0, 30, 11, 0, 30, 12, 40, 30, 12, 40, -10, 14, 70}, "slice of all");
  expectList(slicePolyline(pl, 30, 30), {30, 11, 0, 30, 11, 0}, "slice of nothing");
  expectList(slicePolyline(pl, 80, 20), {22, 12.4, 46, 22, 12.4, 46}, "slice backwards");

  // angles: counter-clockwise seen from above, east = 0, north = +π/2
  {
    const double got[] = {angleOf(1, 0), angleOf(0, -1), angleOf(-1, 0), angleOf(0, 1), angleOf(3, -4), wrapAngle(3 * PI), wrapAngle(-PI), wrapAngle(PI), wrapAngle(7), wrapAngle(-7), wrap2Pi(-0.5), wrap2Pi(7), wrap2Pi(-7), wrap2Pi(0)};
    const double want[] = {-0.0, 1.5707963267948966, -3.141592653589793, -1.5707963267948966, 0.9272952180016122, 3.141592653589793, 3.141592653589793, 3.141592653589793, 0.7168146928204138, -0.7168146928204138, 5.783185307179586, 0.7168146928204138, 5.5663706143591725, 0};
    for (size_t i = 0; i < sizeof got / sizeof got[0]; i++) {
      if (!near(got[i], want[i])) {
        std::fprintf(stderr, "angle %zu: %.17g for %.17g\n", i, got[i], want[i]);
        assert(false);
      }
    }
    assert(std::signbit(angleOf(1, 0)));
  }
  assert(clamp(5, 0, 3) == 3 && clamp(-1, 0, 3) == 0 && std::isnan(clamp(JS_NAN, 0, 3)) && lerp(2, 10, 0.25) == 4);
  assert(smoothstep(0, 10, 2.5) == 0.15625 && smoothstep(3, 3, 2) == 0 && smoothstep(3, 3, 3) == 1 && smoothstep(0, 1, 7) == 1);
  assert(dist2(1, 2, 4, 6) == 25 && dist(1, 2, 4, 6) == 5);

  // lines: p + t·d meets q + u·e at (1, 0): t = 1, u = 0
  {
    LineHit h = lineLine(0, 0, 1, 0, 1, 0, 0, 1);
    assert(h && h.has && h.t == 1 && h.u == 0);
    assert(!lineLine(0, 0, 1, 0, 0, 1, 1, 0));  // parallel
    h = lineLine(1, 1, 2, 1, 5, 0, -1, 3);
    assert(h.has && h.t == 1.5714285714285714 && h.u == 0.8571428571428571);
    h = segSeg(0, 0, 10, 0, 5, -5, 5, 5);
    assert(h.has && h.t == 0.5 && h.u == 0.5);
    assert(!segSeg(0, 0, 10, 0, 5, 1, 5, 5));
    assert(!segSeg(0, 0, 1, 0, 0, 1, 1, 1));
    h = segSeg(0, 0, 4, 4, 0, 4, 4, 0);
    assert(h.has && h.t == 0.5 && h.u == 0.5);
  }

  // rings: with z = south, (0,0) (1,0) (1,1) (0,1) runs clockwise seen from above: its area is −1
  assert(ringArea({0, 0, 1, 0, 1, 1, 0, 1}) == -1 && ringArea({0, 0, 0, 1, 1, 1, 1, 0}) == 1 && ringArea({0, 0, 4, 0, 4, 3}) == -6);
  assert(pointInRing(0.5, 0.5, {0, 0, 1, 0, 1, 1, 0, 1}) && !pointInRing(2, 0.5, {0, 0, 1, 0, 1, 1, 0, 1}) && !pointInRing(1, 1, {0, 0, 4, 0, 4, 3}) && pointInRing(3, 1, {0, 0, 4, 0, 4, 3}));
  {
    // (the same for a ring kept as 32-bit floats, as the tiles keep the outlines of buildings)
    const std::vector<float> ring = {0, 0, 4, 0, 4, 3};
    assert(ringArea(ring) == -6 && pointInRing(3, 1, ring) && !pointInRing(1, 1, ring));
  }
  assert(projectOnSegment(5, 1, 0, 0, 10, 0) == 0.5 && projectOnSegment(-3, 1, 0, 0, 10, 0) == 0 && projectOnSegment(13, 1, 0, 0, 10, 0) == 1 && projectOnSegment(1, 1, 2, 2, 2, 2) == 0 &&
         projectOnSegment(2.5, 7, 0, 0, 10, 0) == 0.25);

  // the reproducible random numbers
  {
    Rng r = rng(42);
    assert(r() == 0.6011037519201636 && r() == 0.44829055899754167 && r() == 0.8524657934904099);
    Rng r2 = rng(-1);
    assert(r2() == 0.8964226141106337 && r2() == 0.189478256739676);
    Rng r3 = rng(4294967296.0 * 3 + 5.9);
    assert(r3() == 0.6897749109193683);
    assert(hash01(0) == 0.5733975800685585 && hash01(1) == 0.21433574031107128 && hash01(-5) == 0.48794094240292907 && hash01(123456789) == 0.7164693470112979 && hash01(4294967303.0) == 0.3443175407592207 &&
           hash01(2.9) == 0.7041337329428643 && hash01(-2.9) == 0.21386108780279756);
  }

  // the grid: what is in a cell, and in which order after things were taken out (the last one takes the gap)
  {
    Grid2<int> grid(10);
    grid.insertBox(0, 0, 25, 5, 1);
    grid.insertBox(1, 1, 2, 2, 2);
    grid.insertPoint(3, 3, 3);
    grid.insertBox(-5, -5, 5, 5, 4);
    grid.insertPoint(9.99, 9.99, 5);
    assert(grid.at(1, 1) == (std::vector<int>{1, 2, 3, 4, 5}) && grid.at(15, 2) == std::vector<int>{1} && grid.at(-1, -1) == std::vector<int>{4} && grid.at(100, 100).empty());
    grid.removeBox(1, 1, 2, 2, 2);
    assert(grid.at(1, 1) == (std::vector<int>{1, 5, 3, 4}));
    grid.removeBox(0, 0, 25, 5, 1);
    assert(grid.at(1, 1) == (std::vector<int>{4, 5, 3}) && grid.at(15, 2).empty() && grid.at(22, 2).empty());
    std::vector<int> seen;
    grid.queryBox(-20, -20, 20, 20, [&](int v) { seen.push_back(v); });
    assert(seen == (std::vector<int>{4, 4, 4, 4, 5, 3}));
    assert(grid.cell == 10);
  }

  std::printf("geom: all checks passed\n");
  return 0;
}

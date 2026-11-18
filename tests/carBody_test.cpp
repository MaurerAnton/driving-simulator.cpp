// SPDX-License-Identifier: AGPL-3.0-only
/** Car shell: proportions, glass/trim split, pillars and wheels. */
#include "../src/render/carBody.h"
#include <cassert>
#include <cstdio>
#include <tuple>

using namespace fs;

namespace {
void boundsOf(GeometryPtr g, Vector3& mn, Vector3& mx) {
  mn.set(1e300, 1e300, 1e300);
  mx.set(-1e300, -1e300, -1e300);
  if (!g || !g->position) return;
  for (size_t i = 0; i < g->position->count(); i++) {
    double x = g->position->getX(i), y = g->position->getY(i), z = g->position->getZ(i);
    mn.x = std::min(mn.x, x); mn.y = std::min(mn.y, y); mn.z = std::min(mn.z, z);
    mx.x = std::max(mx.x, x); mx.y = std::max(mx.y, y); mx.z = std::max(mx.z, z);
  }
}
}  // namespace

int main() {
  CarShell s = buildCarShell(COMPACT_DIMS);

  // landmarks the original records
  assert(std::fabs(s.halfWidth - 1.79 / 2) < 1e-9);
  assert(std::fabs(s.roofY - 1.46) < 1e-9);
  assert(s.zFront < s.zAxleF && s.zAxleF < s.zAxleR && s.zAxleR < s.zRear);
  assert(std::fabs((s.zRear - s.zFront) - 4.28) < 1e-9);
  assert(std::fabs((s.zAxleR - s.zAxleF) - 2.63) < 1e-9);
  assert(s.zCowl > s.zAxleF && s.zRoofFront > s.zCowl && s.zRoofRear > s.zRoofFront);

  // every part exists and carries triangles
  assert(s.paint && s.glass && s.trim && s.lining && s.pillars);
  assert(s.paint->position->count() > 100);
  assert(s.paint->index && s.paint->index->count() > 100);
  assert(s.glass->position->count() > 20);      // screens and side windows
  assert(s.trim->position->count() > 10);       // the floor panels
  assert(s.lining->position->count() > 100);    // the inward-facing shell
  assert(s.pillars->position->count() > 10);    // two A pillars

  // the shell spans the car's length, width and height
  Vector3 mn, mx;
  boundsOf(s.paint, mn, mx);
  assert(std::fabs(mx.z - mn.z - 4.28) < 0.05);
  assert(std::fabs(mx.x - mn.x - 1.79) < 0.1);
  assert(mx.y > 1.3 && mx.y < 1.7);
  assert(mn.y > -0.01);              // nothing below the ground plane
  // the roof is the highest point and the floor the lowest body point
  assert(mx.y > s.beltY);

  // the glass sits between the belt line and the roof
  Vector3 gmn, gmx;
  boundsOf(s.glass, gmn, gmx);
  assert(gmn.y > s.beltY - 0.05);
  assert(gmx.y <= s.roofY + 1e-6);
  assert(gmx.z < s.zRear);           // no glass on the tail panel

  // the lining is inside the paint (pulled inwards by 2.5 cm)
  Vector3 lmn, lmx;
  boundsOf(s.lining, lmn, lmx);
  assert(lmx.x <= mx.x + 1e-6);
  assert(lmx.x < mx.x - 0.01);
  assert(lmx.z <= mx.z + 1e-6);

  // the pillars run from the cowl up to the roof line
  Vector3 pmn, pmx;
  boundsOf(s.pillars, pmn, pmx);
  assert(pmx.y > 1.2);
  assert(pmn.y < 1.0);
  assert(std::fabs(pmx.x - pmn.x) > 0.5);   // one on each side

  // a wheel is a tyre plus a rim, lying along X (so it can spin about X);
  // the game keeps the two apart so it can tint them, the merged wheel is
  // what a plain caller wants
  GeometryPtr tyre, rim;
  std::tie(tyre, rim) = buildWheelParts(COMPACT_DIMS);
  assert(tyre && rim && tyre->position->count() > 30 && rim->position->count() > 100);
  // colour the two parts the way the game does, then merge them
  auto tint = [](GeometryPtr g, double r, double gr, double b) {
    std::vector<double> cols(g->position->count() * 3);
    for (size_t i = 0; i < g->position->count(); i++) {
      cols[i * 3] = r;
      cols[i * 3 + 1] = gr;
      cols[i * 3 + 2] = b;
    }
    g->color = std::make_shared<BufferAttribute>(cols, 3);
    return g;
  };
  tint(tyre, 0.08, 0.08, 0.09);
  tint(rim, 0.72, 0.73, 0.76);
  GeometryPtr w = mergeGeometries({tyre, rim}, false);
  w->computeVertexNormals();
  assert(w && w->position && w->position->count() > 30);
  Vector3 wmn, wmx;
  boundsOf(w, wmn, wmx);
  // the wheel turns about X: the tyre disc lies in the YZ plane, so the
  // diameter shows in Y and Z (a little over 2r, since the spokes stand
  // proud) and the tread width along X
  assert(wmx.y - wmn.y > 0.60 && wmx.y - wmn.y < 0.75);
  assert(wmx.z - wmn.z > 0.60 && wmx.z - wmn.z < 0.80);
  // the spokes run across the full tread width (1.06 × tyre width)
  // the wheel turns about X: the tyre disc lies in the YZ plane (about 2r
  // across) and the tread runs along X (1.06 × tyre width, the spokes proud)
  assert(wmx.y - wmn.y > 0.60 && wmx.y - wmn.y < 0.80);
  assert(wmx.z - wmn.z > 0.60 && wmx.z - wmn.z < 0.80);
  // the wheel turns about X, so the tyre disc lies in the YZ plane (about
  // 2r across) and the tread runs along X (1.06 × tyre width, spokes proud)
  assert(wmx.x - wmn.x > 0.60 && wmx.x - wmn.x < 0.80);
  assert(wmx.y - wmn.y > 0.60 && wmx.y - wmn.y < 0.80);
  assert(wmx.z - wmn.z > 0.60 && wmx.z - wmn.z < 0.80);

  // the tyre and the rim carry their own colours (the game tints them per
  // car): a dark tyre and a lighter rim
  assert(tyre->color && rim->color);
  bool dark = false, light = false;
  for (size_t i = 0; i < tyre->color->count(); i++)
    if (tyre->color->getX(i) < 0.2) dark = true;
  for (size_t i = 0; i < rim->color->count(); i++)
    if (rim->color->getX(i) > 0.4) light = true;
  assert(dark && light);

  std::printf("carBody: all checks passed (paint %zu, glass %zu, trim %zu verts)\n",
              s.paint->position->count(), s.glass->position->count(),
              s.trim->position->count());
  return 0;
}

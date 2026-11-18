// SPDX-License-Identifier: AGPL-3.0-only
/** Geometry checks: primitives produce sane vertex data; merge works. */
#include "../src/engine/geometry.h"
#include <cassert>
#include <cstdio>

using namespace fs;

int main() {
  // box: 6 faces * 2 tris * 3 verts = 36 vertices (non-indexed)
  auto box = makeBoxGeometry(2, 4, 6);
  assert(box->position->count() == 36);
  assert(box->normal->count() == 36);
  Vector3 mn, mx;
  box->computeBoundingBox(mn, mx);
  assert(std::fabs(mn.x + 1) < 1e-9 && std::fabs(mx.x - 1) < 1e-9);
  assert(std::fabs(mn.y + 2) < 1e-9 && std::fabs(mx.y - 2) < 1e-9);
  assert(std::fabs(mn.z + 3) < 1e-9 && std::fabs(mx.z - 3) < 1e-9);

  // plane: 1 segment -> 2 triangles -> 6 vertices
  auto plane = makePlaneGeometry(10, 20);
  assert(plane->position->count() == 6);

  // sphere: indexed grid, radius respected
  auto sph = makeSphereGeometry(5, 16, 8);
  assert(sph->index && sph->position->count() == 17 * 9);
  for (size_t i = 0; i < sph->position->count(); i++) {
    double x = sph->position->getX(i), y = sph->position->getY(i), z = sph->position->getZ(i);
    assert(std::fabs(std::sqrt(x * x + y * y + z * z) - 5.0) < 1e-9);
  }

  // cylinder with caps: height respected, caps present
  auto cyl = makeCylinderGeometry(1, 2, 8, 16, 1, false);
  Vector3 cmin, cmax;
  cyl->computeBoundingBox(cmin, cmax);
  assert(std::fabs(cmax.y - 4) < 1e-9 && std::fabs(cmin.y + 4) < 1e-9);
  // 17*2 side verts + 2*(1 centre + 17 ring) = 34 + 36 = 70
  assert(cyl->position->count() == 70);

  // cone = cylinder with radiusTop 0
  auto cone = makeConeGeometry(3, 10, 24, 1, false);
  Vector3 kon, kox;
  cone->computeBoundingBox(kon, kox);
  assert(std::fabs(kox.y - 5) < 1e-9);

  // circle: centre + segments+1 ring
  auto circ = makeCircleGeometry(2, 32);
  assert(circ->position->count() == 34);

  // torus: (radial+1)*(tubular+1) vertices
  auto tor = makeTorusGeometry(2, 0.5, 8, 16);
  assert(tor->position->count() == 9 * 17);

  // icosahedron with detail 0: 20 triangles * 3 = 60 vertices
  auto ico = makeIcosahedronGeometry(1, 0);
  assert(ico->position->count() == 60);
  for (size_t i = 0; i < ico->position->count(); i++) {
    double x = ico->position->getX(i), y = ico->position->getY(i), z = ico->position->getZ(i);
    assert(std::fabs(std::sqrt(x * x + y * y + z * z) - 1.0) < 1e-9);
  }

  // merge: vertex counts add up
  auto m = mergeGeometries({box, plane});
  assert(m->position->count() == 36 + 6);
  auto mn2 = mergeAndComputeNormals({box, plane});
  assert(mn2->normal && mn2->normal->count() == mn2->position->count());

  // transform helpers
  box->translate(1, 2, 3);
  Vector3 bmin, bmax;
  box->computeBoundingBox(bmin, bmax);
  assert(std::fabs(bmin.x + 0) < 1e-9);  // -1 + 1

  std::printf("geometry: all checks passed\n");
  return 0;
}

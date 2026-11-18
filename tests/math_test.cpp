// SPDX-License-Identifier: AGPL-3.0-only
/** Math unit check: three.js-style Vector3/Quaternion/Matrix4 behaviour. */
#include "../src/engine/math.h"
#include <cassert>
#include <cstdio>

using namespace fs;

int main() {
  // Vector3 basics
  Vector3 a(1, 2, 3), b(4, 5, 6);
  assert((a + b).x == 5 && (a + b).y == 7 && (a + b).z == 9);
  assert(a.dot(b) == 32);
  Vector3 c;
  c.crossVectors(a, b);
  assert(c.x == -3 && c.y == 6 && c.z == -3);
  assert(std::abs(a.length() - std::sqrt(14.0)) < 1e-12);

  // Quaternion from axis-angle: 90° about Y rotates +X to -Z
  Quaternion q;
  q.setFromAxisAngle(Vector3(0, 1, 0), PI / 2.0);
  Vector3 v(1, 0, 0);
  v.applyQuaternion(q);
  assert(std::abs(v.x) < 1e-12 && std::abs(v.z + 1) < 1e-12);

  // Euler YXZ → matrix, then back
  Euler e(0.3, -0.7, 0.1, Euler::YXZ);
  Matrix4 m;
  m.makeRotationFromEuler(e);
  Quaternion back;
  back.setFromRotationMatrix(m);
  Quaternion expect;
  expect.setFromEuler(e);
  assert(std::fabs(back.x - expect.x) < 1e-9);
  assert(std::fabs(back.w - expect.w) < 1e-9);

  // compose / decompose round trip
  Vector3 pos(10, -2, 3.5), scl(2, 1, 0.5);
  Quaternion rot;
  rot.setFromAxisAngle(Vector3(1, 1, 1).normalize(), 0.9);
  Matrix4 mm;
  mm.compose(pos, rot, scl);
  Vector3 p2, s2;
  Quaternion r2;
  mm.decompose(p2, r2, s2);
  assert(std::fabs(p2.x - pos.x) < 1e-9);
  assert(std::fabs(s2.z - scl.z) < 1e-9);
  assert(std::fabs(r2.x - rot.x) < 1e-9);

  // inverse round trip
  Matrix4 inv;
  inv.copy(mm).invert();
  Matrix4 id;
  id.multiplyMatrices(mm, inv);
  for (int i = 0; i < 16; i++)
    assert(std::fabs(id.e[i] - ((i % 5 == 0) ? 1.0 : 0.0)) < 1e-9);

  // applyMatrix4 with translation
  Matrix4 t;
  t.makeTranslation(1, 2, 3);
  Vector3 p(0, 0, 0);
  p.applyMatrix4(t);
  assert(p.x == 1 && p.y == 2 && p.z == 3);

  // perspective projection: a point on the near plane maps to NDC -1
  Matrix4 persp;
  persp.makePerspective(-1, 1, 1, -1, 0.1, 100);
  Vector3 nearPt(0, 0, -0.1);
  nearPt.applyMatrix4(persp);
  assert(std::fabs(nearPt.z + 1.0) < 1e-9);

  // lookAt: eye at origin looking at -Z with +Y up keeps the frame
  Matrix4 la;
  la.lookAt(Vector3(0, 0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0));
  Vector3 up(0, 1, 0);
  up.applyMatrix4(la);
  assert(std::fabs(up.y - 1.0) < 1e-9);

  std::printf("math.h: all checks passed\n");
  return 0;
}

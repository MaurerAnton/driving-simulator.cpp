// SPDX-License-Identifier: AGPL-3.0-only
/** Math unit check: three.js-style Vector3/Quaternion/Matrix4 behaviour. */
// LINK: engine core platform
#include "../src/engine/math.h"
#include <cmath>
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

  // --- what three.js r187 does that the first draft did not ---

  // a matrix without an inverse turns into zeros
  Matrix4 flat;
  flat.makeScale(1, 0, 1).invert();
  for (int i = 0; i < 16; i++) assert(flat.elements[i] == 0 && flat.e[i] == 0);

  // Euler angles in all six orders: to a matrix, to a quaternion and back
  const Euler::Order orders[6] = {Euler::XYZ, Euler::YXZ, Euler::ZXY, Euler::ZYX, Euler::YZX, Euler::XZY};
  for (Euler::Order order : orders) {
    const Euler in(0.3, -0.7, 1.1, order);
    Matrix4 rm;
    rm.makeRotationFromEuler(in);
    const Euler fromMatrix = Euler().setFromRotationMatrix(rm, order);
    const Euler fromQuaternion = Euler().setFromQuaternion(Quaternion().setFromEuler(in), order);
    assert(std::fabs(fromMatrix.x - in.x) < 1e-12 && std::fabs(fromMatrix.y - in.y) < 1e-12 && std::fabs(fromMatrix.z - in.z) < 1e-12 && fromMatrix.order == order);
    assert(std::fabs(fromQuaternion.x - in.x) < 1e-12 && std::fabs(fromQuaternion.y - in.y) < 1e-12 && std::fabs(fromQuaternion.z - in.z) < 1e-12);
    const Vector3 byEuler = Vector3(1, 2, 3).applyEuler(in), byMatrix = Vector3(1, 2, 3).applyMatrix4(rm);
    assert(byEuler.distanceTo(byMatrix) < 1e-12);
  }

  // a mirrored matrix decomposes with a negative x scale
  Matrix4 mirrored;
  mirrored.compose(Vector3(1, 2, 3), Quaternion().setFromAxisAngle(Vector3(0, 1, 0), 0.4), Vector3(-2, 3, 4));
  Vector3 mp, ms;
  Quaternion mq;
  mirrored.decompose(mp, mq, ms);
  assert(std::fabs(ms.x + 2) < 1e-12 && std::fabs(ms.y - 3) < 1e-12 && std::fabs(ms.z - 4) < 1e-12);

  // colours are linear-sRGB numbers; a hex number, a CSS text and a name are sRGB
  const Color grey(0x808080);
  assert(std::fabs(grey.r - 0.21586050010324417) < 1e-12 && grey.getHex() == 0x808080 && grey.getHexString() == "808080");
  assert(Color(0.5, 0.5, 0.5).r == 0.5 && Color("#808080").equals(grey) && Color("grey").equals(grey) && Color("rgb(128, 128, 128)").equals(grey));
  HSL hsl;
  Color(0xff0000).getHSL(hsl);
  assert(hsl.h == 0 && hsl.s == 1 && hsl.l == 0.5);
  const Color green = Color().setHSL(1.0 / 3, 1, 0.5);   // (as in three.js not to the last bit: the hue goes through a remainder)
  assert(std::fabs(green.r) < 1e-12 && green.g == 1 && green.b == 0);

  // the other vectors, Matrix3
  assert(Vector2(3, 4).length() == 5 && std::fabs(Vector2(0, 1).angle() - PI / 2) < 1e-12 && Vector2(1, 0).cross(Vector2(0, 1)) == 1);
  assert(Vector4(1, 2, 3, 4).dot(Vector4(1, 1, 1, 1)) == 10 && Vector4().w == 1);
  Matrix3 normalMatrix;
  normalMatrix.getNormalMatrix(Matrix4().makeScale(2, 1, 1));
  assert(normalMatrix.elements[0] == 0.5 && normalMatrix.elements[4] == 1);
  assert(Vector3(1, 2, 3).clone().add(Vector3(1, 1, 1)) == Vector3(2, 3, 4) && Vector3(-1.5, 2.5, 0.4).round() == Vector3(-1, 3, 0));   // Math.round: halves go up

  // boxes, spheres, planes, rays, the frustum
  Box3 box;
  assert(box.isEmpty());
  box.expandByPoint(Vector3(-1, -1, -1)).expandByPoint(Vector3(1, 2, 3));
  assert(box.getCenter() == Vector3(0, 0.5, 1) && box.getSize() == Vector3(2, 3, 4) && box.containsPoint(Vector3(0, 0, 0)) && !box.containsPoint(Vector3(0, 3, 0)));
  Sphere sphere;
  assert(sphere.isEmpty());
  box.getBoundingSphere(sphere);
  assert(sphere.center == Vector3(0, 0.5, 1) && std::fabs(sphere.radius - std::sqrt(29.0) / 2) < 1e-12 && box.intersectsSphere(sphere));
  const Plane ground(Vector3(0, 1, 0), 0);
  assert(ground.distanceToPoint(Vector3(5, 2, 5)) == 2 && box.intersectsPlane(ground));
  const Ray down(Vector3(0.25, 10, 0.25), Vector3(0, -1, 0));
  Vector3 hit;
  assert(down.intersectPlane(ground, hit) && hit == Vector3(0.25, 0, 0.25) && *down.distanceToPlane(ground) == 10);
  assert(down.intersectBox(box, hit) && hit == Vector3(0.25, 2, 0.25));
  assert(down.intersectTriangle(Vector3(0, 0, 0), Vector3(0, 0, 1), Vector3(1, 0, 0), true, hit) && hit == Vector3(0.25, 0, 0.25));
  assert(down.intersectTriangle(Vector3(0, 0, 0), Vector3(1, 0, 0), Vector3(0, 0, 1), true, hit) == nullptr);   // its back
  assert(down.intersectSphere(Sphere(Vector3(0.25, 0, 0.25), 1), hit) && hit == Vector3(0.25, 1, 0.25));
  Frustum frustum;
  frustum.setFromProjectionMatrix(persp);   // looking along -z from the origin, 0.1 … 100
  assert(frustum.containsPoint(Vector3(0, 0, -5)) && !frustum.containsPoint(Vector3(0, 0, 5)) && !frustum.containsPoint(Vector3(0, 0, -200)));
  assert(frustum.intersectsSphere(Sphere(Vector3(0, 0, 1), 2)) && !frustum.intersectsSphere(Sphere(Vector3(0, 0, 3), 2)));
  Vector3 bary;
  assert(Triangle::getBarycoord(Vector3(0.25, 0, 0.25), Vector3(0, 0, 0), Vector3(1, 0, 0), Vector3(0, 0, 1), bary) && bary == Vector3(0.5, 0.25, 0.25));

  // MathUtils as three.js has it
  assert(MathUtils::clamp(5, 0, 1) == 1 && MathUtils::lerp(1, 3, 0.5) == 2 && MathUtils::smoothstep(0.5, 0, 1) == 0.5 && MathUtils::euclideanModulo(-1, 3) == 2);
  assert(std::fabs(MathUtils::degToRad(180) - PI) < 1e-15);

  std::printf("math.h: all checks passed\n");
  return 0;
}

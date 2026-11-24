// SPDX-License-Identifier: AGPL-3.0-only
// LINK: engine core platform
/**
 * The engine, asked what three.js itself is asked in tools/diff/three-x.mjs: the two
 * print the same lines, tools/diff/compare.py compares them.
 *
 *   node tools/diff/three-x.mjs [math] [scene] [geom] > /tmp/a.txt      (no section named: all)
 *   FS_JOBS=1 tools/t.sh three_x_dump > /dev/null && build/three_x_dump [math] [scene] [geom] > /tmp/b.txt
 *   tools/diff/compare.py /tmp/a.txt /tmp/b.txt
 *
 * Keep the two files alike, section by section and line by line: the random numbers
 * are drawn in the same order on both sides (so no draw inside the arguments of a
 * call here – C++ does not say which argument is worked out first).
 */
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "core/geom.h"
#include "engine/engine.h"
#include "engine/geometries.h"
#include "engine/lights.h"

using namespace fs;

// ---------------------------------------------------------------------------
// printing
// ---------------------------------------------------------------------------

static void put(double x) {
  if (x != x) std::fputs(" NaN", stdout);
  else if (x == JS_INF) std::fputs(" Infinity", stdout);
  else if (x == -JS_INF) std::fputs(" -Infinity", stdout);
  else if (x == 0) std::fputs(" 0", stdout);
  else std::printf(" %.17g", x);
}
[[maybe_unused]] static void put(float x) { put(static_cast<double>(x)); }
static void put(int x) { std::printf(" %d", x); }
static void put(unsigned x) { std::printf(" %u", x); }
[[maybe_unused]] static void put(long x) { std::printf(" %ld", x); }
[[maybe_unused]] static void put(unsigned long x) { std::printf(" %lu", x); }
static void put(bool x) { std::fputs(x ? " 1" : " 0", stdout); }
static void put(const std::string& s) {
  if (s.empty()) {
    std::fputs(" \"\"", stdout);
    return;
  }
  std::putchar(' ');
  bool space = false;
  for (char c : s) {
    const bool sp = c == ' ' || c == '\t' || c == '\n' || c == '\r';
    if (sp && !space) std::putchar('_');
    if (!sp) std::putchar(c);
    space = sp;
  }
}
static void put(const char* s) { put(std::string(s)); }
static void put(const Vector2& v) { put(v.x), put(v.y); }
static void put(const Vector3& v) { put(v.x), put(v.y), put(v.z); }
static void put(const Vector4& v) { put(v.x), put(v.y), put(v.z), put(v.w); }
static void put(const Quaternion& v) { put(v.x), put(v.y), put(v.z), put(v.w); }
static void put(const Euler& v) { put(v.x), put(v.y), put(v.z), put(Euler::orderName(v.order)); }
static void put(const Matrix3& m) {
  for (double x : m.elements) put(x);
}
static void put(const Matrix4& m) {
  for (double x : m.elements) put(x);
}
static void put(const Color& c) { put(c.r), put(c.g), put(c.b); }
static void put(const HSL& c) { put(c.h), put(c.s), put(c.l); }
static void put(const Box2& b) { put(b.min), put(b.max); }
static void put(const Box3& b) { put(b.min), put(b.max); }
static void put(const Sphere& s) { put(s.center), put(s.radius); }
static void put(const Plane& p) { put(p.normal), put(p.constant); }
static void put(const Ray& r) { put(r.origin), put(r.direction); }
static void put(const Line3& l) { put(l.start), put(l.end); }
static void put(const Triangle& t) { put(t.a), put(t.b), put(t.c); }
static void put(const Spherical& s) { put(s.radius), put(s.phi), put(s.theta); }
template <class T>
static void put(const T* p) {
  if (p) put(*p);
  else std::fputs(" null", stdout);
}
template <class T>
static void put(T* p) {
  put(static_cast<const T*>(p));
}
template <class T>
static void put(const std::optional<T>& o) {
  if (o) put(*o);
  else std::fputs(" null", stdout);
}
template <class T>
static void put(const std::vector<T>& v) {
  for (const auto& x : v) put(static_cast<T>(x));
}
template <class T, size_t N>
static void put(const std::array<T, N>& v) {
  for (const T& x : v) put(x);
}
template <class... A>
static void P(const char* label, const A&... a) {
  std::fputs(label, stdout);
  (put(a), ...);
  std::putchar('\n');
}
template <class... A>
static void P(const std::string& label, const A&... a) {
  P(label.c_str(), a...);
}
/** n numbers of a plain array */
template <class T>
static std::vector<double> arr(const T* a, size_t n) {
  return std::vector<double>(a, a + n);
}

// ---------------------------------------------------------------------------
// the random numbers (mulberry32) and what is made of them
// ---------------------------------------------------------------------------

static Rng rng(1);
static void seed(uint32_t s) { rng = Rng(s); }
static double rnd_() { return rng(); }
static double r(double lo = -10, double hi = 10) { return lo + rnd_() * (hi - lo); }
static int ri(int lo, int hi) { return lo + static_cast<int>(std::floor(rnd_() * (hi - lo + 1))); }
static Vector2 V2(double lo = -10, double hi = 10) {
  const double x = r(lo, hi), y = r(lo, hi);
  return Vector2(x, y);
}
static Vector3 V3(double lo = -10, double hi = 10) {
  const double x = r(lo, hi), y = r(lo, hi), z = r(lo, hi);
  return Vector3(x, y, z);
}
static Vector4 V4(double lo = -10, double hi = 10) {
  const double x = r(lo, hi), y = r(lo, hi), z = r(lo, hi), w = r(lo, hi);
  return Vector4(x, y, z, w);
}
static Quaternion Q() {
  const double x = r(-1, 1), y = r(-1, 1), z = r(-1, 1), w = r(-1, 1);
  return Quaternion(x, y, z, w).normalize();
}
static const Euler::Order ORDERS[6] = {Euler::XYZ, Euler::YXZ, Euler::ZXY, Euler::ZYX, Euler::YZX, Euler::XZY};
static Euler E(Euler::Order order = Euler::XYZ) {
  const double x = r(-PI, PI), y = r(-PI, PI), z = r(-PI, PI);
  return Euler(x, y, z, order);
}
/** a position, a rotation and a scale in one matrix */
static Matrix4 M4() {
  const Vector3 p = V3();
  const Quaternion q = Q();
  const Vector3 s = V3(0.5, 2);
  return Matrix4().compose(p, q, s);
}
/** sixteen numbers */
static Matrix4 M4any() {
  Matrix4 m;
  for (int i = 0; i < 16; i++) m.elements[i] = r(-2, 2);
  return m;
}
static Matrix3 M3any() {
  Matrix3 m;
  for (int i = 0; i < 9; i++) m.elements[i] = r(-2, 2);
  return m;
}
static Color C() {
  const double x = rnd_(), y = rnd_(), z = rnd_();
  return Color(x, y, z);
}
static Box3 B3() {
  const Vector3 a = V3(), b = V3();
  return Box3().setFromPoints({a, b});
}
static Box2 B2() {
  const Vector2 a = V2(), b = V2();
  return Box2().setFromPoints({a, b});
}
static Sphere SPH() {
  const Vector3 c = V3();
  const double rad = r(0.5, 6);
  return Sphere(c, rad);
}
static Plane PL() {
  const Vector3 n = V3(-1, 1).normalize();
  const double c = r(-5, 5);
  return Plane(n, c);
}
static Ray RAY() {
  const Vector3 o = V3();
  const Vector3 d = V3(-1, 1).normalize();
  return Ray(o, d);
}
static HSL hsl(const Color& c, ColorSpace space = LinearSRGBColorSpace) {
  HSL h;
  c.getHSL(h, space);
  return h;
}

// ---------------------------------------------------------------------------
// maths
// ---------------------------------------------------------------------------

static void mathSection() {
  seed(20261124);

  // --- MathUtils
  for (int i = 0; i < 6; i++) {
    const double x = r(), a = r(), b = r(), t = rnd_();
    P("mu.clamp", MathUtils::clamp(x, -3, 4), MathUtils::clamp(x, a, b));
    P("mu.euclideanModulo", MathUtils::euclideanModulo(x, 3), MathUtils::euclideanModulo(a, 0.7));
    P("mu.mapLinear", MathUtils::mapLinear(x, a, b, 2, 5));
    P("mu.inverseLerp", MathUtils::inverseLerp(a, b, x), MathUtils::inverseLerp(a, a, x));
    P("mu.lerp", MathUtils::lerp(a, b, t));
    P("mu.damp", MathUtils::damp(a, b, 3.5, t));
    P("mu.pingpong", MathUtils::pingpong(x), MathUtils::pingpong(x, 2.5));
    P("mu.smoothstep", MathUtils::smoothstep(x, -5, 5), MathUtils::smoothstep(x, a, b), MathUtils::smootherstep(x, -5, 5));
    P("mu.deg", MathUtils::degToRad(x * 18), MathUtils::radToDeg(x));
    const int n = ri(1, 5000);
    P("mu.pow2", n, MathUtils::isPowerOfTwo(n), MathUtils::isPowerOfTwo(1 << (n % 20)), MathUtils::ceilPowerOfTwo(n), MathUtils::floorPowerOfTwo(n));
  }
  {
    const double s1 = MathUtils::seededRandom(42), s2 = MathUtils::seededRandom(), s3 = MathUtils::seededRandom(), s4 = MathUtils::seededRandom(7);
    P("mu.seededRandom", s1, s2, s3, s4);
  }
  for (const char* order : {"XYX", "YZY", "ZXZ", "XZX", "YXY", "ZYZ"}) {
    const double a = r(-3, 3), b = r(-3, 3), c = r(-3, 3);
    Quaternion q;
    MathUtils::setQuaternionFromProperEuler(q, a, b, c, order);
    P("mu.properEuler", order, q);
  }

  // --- Vector2
  for (int i = 0; i < 6; i++) {
    const Vector2 a = V2(), b = V2();
    const double s = r(-3, 3), t = rnd_();
    const Matrix3 m = M3any();
    const Vector2 lo = a.clone().min(b), hi = a.clone().max(b);
    const Vector2 p = V2(-20, 20);
    P("v2.add", a.clone().add(b), a.clone().addScalar(s), Vector2().addVectors(a, b), a.clone().addScaledVector(b, s));
    P("v2.sub", a.clone().sub(b), a.clone().subScalar(s), Vector2().subVectors(a, b));
    P("v2.mul", a.clone().multiply(b), a.clone().multiplyScalar(s), a.clone().divide(b), a.clone().divideScalar(s));
    P("v2.applyMatrix3", a.clone().applyMatrix3(m));
    P("v2.minmax", lo, hi, p.clone().clamp(lo, hi), p.clone().clampScalar(-2.5, 3.5), p.clone().clampLength(2, 9));
    P("v2.round", a.clone().floor(), a.clone().ceil(), a.clone().round(), a.clone().roundToZero(), a.clone().negate());
    P("v2.dot", a.dot(b), a.cross(b), a.lengthSq(), a.length(), a.manhattanLength());
    P("v2.normalize", a.clone().normalize(), a.clone().setLength(s), a.angle(), a.angleTo(b));
    P("v2.distance", a.distanceTo(b), a.distanceToSquared(b), a.manhattanDistanceTo(b));
    P("v2.lerp", a.clone().lerp(b, t), Vector2().lerpVectors(a, b, t), a.equals(b), a.equals(a.clone()));
    P("v2.rotateAround", a.clone().rotateAround(b, s));
    P("v2.misc", a.clone().setX(s), a.clone().setY(s), a.clone().setScalar(s), a.clone().setComponent(1, s), a.getComponent(0), a.getComponent(1), a.width(), a.height());
    const double three[3] = {s, t, 3};
    P("v2.array", Vector2().fromArray(three, 1), a.toArray());
  }
  P("v2.zero", Vector2().normalize(), Vector2().angleTo(Vector2(1, 0)), Vector2().clampLength(1, 2), Vector2(0, 0).angle());

  // --- Vector3
  for (int i = 0; i < 8; i++) {
    const Vector3 a = V3(), b = V3();
    const double s = r(-3, 3), t = rnd_();
    const Matrix4 m4 = M4(), m4p = M4any();
    const Matrix3 m3 = M3any();
    const Quaternion q = Q();
    const Euler e = E(ORDERS[i % 6]);
    const Vector3 lo = a.clone().min(b), hi = a.clone().max(b);
    const Vector3 p = V3(-20, 20), n = V3(-1, 1).normalize();
    P("v3.add", a.clone().add(b), a.clone().addScalar(s), Vector3().addVectors(a, b), a.clone().addScaledVector(b, s));
    P("v3.sub", a.clone().sub(b), a.clone().subScalar(s), Vector3().subVectors(a, b));
    P("v3.mul", a.clone().multiply(b), a.clone().multiplyScalar(s), Vector3().multiplyVectors(a, b), a.clone().divide(b), a.clone().divideScalar(s));
    P("v3.applyEuler", a.clone().applyEuler(e), a.clone().applyAxisAngle(n, s));
    P("v3.applyMatrix3", a.clone().applyMatrix3(m3), a.clone().applyNormalMatrix(m3));
    P("v3.applyMatrix4", a.clone().applyMatrix4(m4), a.clone().applyMatrix4(m4p));
    P("v3.applyQuaternion", a.clone().applyQuaternion(q), a.clone().transformDirection(m4));
    P("v3.minmax", lo, hi, p.clone().clamp(lo, hi), p.clone().clampScalar(-2.5, 3.5), p.clone().clampLength(2, 9));
    P("v3.round", a.clone().floor(), a.clone().ceil(), a.clone().round(), a.clone().roundToZero(), a.clone().negate());
    P("v3.dot", a.dot(b), a.lengthSq(), a.length(), a.manhattanLength());
    P("v3.normalize", a.clone().normalize(), a.clone().setLength(s));
    P("v3.lerp", a.clone().lerp(b, t), Vector3().lerpVectors(a, b, t));
    P("v3.cross", a.clone().cross(b), Vector3().crossVectors(a, b));
    P("v3.project", a.clone().projectOnVector(b), a.clone().projectOnPlane(n), a.clone().reflect(n));
    P("v3.angleTo", a.angleTo(b), a.distanceTo(b), a.distanceToSquared(b), a.manhattanDistanceTo(b));
    P("v3.spherical", Vector3().setFromSphericalCoords(s, t * 3, a.x), Vector3().setFromCylindricalCoords(s, a.x, a.y));
    P("v3.fromMatrix", Vector3().setFromMatrixPosition(m4), Vector3().setFromMatrixScale(m4), Vector3().setFromMatrixColumn(m4, 1), Vector3().setFromMatrix3Column(m3, 2));
    P("v3.from", Vector3().setFromEuler(e), Vector3().setFromColor(Color(0.25, 0.5, 0.75)), a.equals(b), a.equals(a.clone()));
    P("v3.misc", a.clone().setX(s), a.clone().setY(s), a.clone().setZ(s), a.clone().setScalar(s), a.clone().setComponent(2, s), a.getComponent(0), a.getComponent(1), a.getComponent(2), a.clone().set(s, t));
    const double four[4] = {s, t, 3, 4};
    P("v3.array", Vector3().fromArray(four, 1), a.toArray());
  }
  P("v3.zero", Vector3().normalize(), Vector3().angleTo(Vector3(1, 0, 0)), Vector3(1, 2, 3).projectOnVector(Vector3()), Vector3().clampLength(1, 2));

  // --- Vector4
  for (int i = 0; i < 6; i++) {
    const Vector4 a = V4(), b = V4();
    const double s = r(-3, 3), t = rnd_();
    const Matrix4 m = M4any();
    const Quaternion q = Q();
    const Vector4 lo = a.clone().min(b), hi = a.clone().max(b);
    const Vector4 p = V4(-20, 20);
    P("v4.add", a.clone().add(b), a.clone().addScalar(s), Vector4().addVectors(a, b), a.clone().addScaledVector(b, s));
    P("v4.sub", a.clone().sub(b), a.clone().subScalar(s), Vector4().subVectors(a, b));
    P("v4.mul", a.clone().multiply(b), a.clone().multiplyScalar(s), a.clone().divide(b), a.clone().divideScalar(s));
    P("v4.applyMatrix4", a.clone().applyMatrix4(m), Vector4().setFromMatrixPosition(m));
    P("v4.axisAngle", Vector4().setAxisAngleFromQuaternion(q), Vector4().setAxisAngleFromRotationMatrix(Matrix4().makeRotationFromQuaternion(q)));
    P("v4.minmax", lo, hi, p.clone().clamp(lo, hi), p.clone().clampScalar(-2.5, 3.5), p.clone().clampLength(2, 9));
    P("v4.round", a.clone().floor(), a.clone().ceil(), a.clone().round(), a.clone().roundToZero(), a.clone().negate());
    P("v4.dot", a.dot(b), a.lengthSq(), a.length(), a.manhattanLength());
    P("v4.normalize", a.clone().normalize(), a.clone().setLength(s));
    P("v4.lerp", a.clone().lerp(b, t), Vector4().lerpVectors(a, b, t), a.equals(b), a.equals(a.clone()));
    P("v4.misc", a.clone().setX(s), a.clone().setW(s), a.clone().setScalar(s), a.clone().setComponent(3, s), a.getComponent(2), a.getComponent(3), a.width(), a.height(), Vector4().copy(Vector3(s, t, 1)));
  }
  {
    // the axis and angle of rotations at and near the singular cases
    const std::vector<Matrix4> cases = {
        Matrix4(),
        Matrix4().makeRotationX(PI), Matrix4().makeRotationY(PI), Matrix4().makeRotationZ(PI),
        Matrix4().makeRotationAxis(Vector3(1, 1, 0).normalize(), PI),
        Matrix4().makeRotationAxis(Vector3(0, 1, 1).normalize(), PI),
        Matrix4().makeRotationAxis(Vector3(1, 0, 1).normalize(), PI),
        Matrix4().makeRotationAxis(Vector3(1, 2, 3).normalize(), PI),
        Matrix4().makeRotationAxis(Vector3(3, 2, 1).normalize(), 0.001),
        Matrix4().makeRotationAxis(Vector3(2, 3, 1).normalize(), 3.14),
    };
    for (const Matrix4& m : cases) P("v4.axisAngle.case", Vector4().setAxisAngleFromRotationMatrix(m));
    P("v4.axisAngle.identity", Vector4().setAxisAngleFromQuaternion(Quaternion()));
  }

  // --- Quaternion
  for (int i = 0; i < 12; i++) {
    const Quaternion a = Q(), b = Q();
    const double t = rnd_(), s = r(-3, 3);
    const Euler e = E(ORDERS[i % 6]);
    const Vector3 n = V3(-1, 1).normalize(), u = V3(-1, 1).normalize();
    const Matrix4 m = Matrix4().makeRotationFromEuler(e);
    P("q.setFromEuler", Euler::orderName(e.order), Quaternion().setFromEuler(e));
    P("q.setFromAxisAngle", Quaternion().setFromAxisAngle(n, s));
    P("q.setFromRotationMatrix", Quaternion().setFromRotationMatrix(m));
    P("q.setFromUnitVectors", Quaternion().setFromUnitVectors(n, u), Quaternion().setFromUnitVectors(n, n.clone().negate()), Quaternion().setFromUnitVectors(n, n));
    P("q.angleTo", a.angleTo(b), a.clone().rotateTowards(b, 0.3), a.clone().rotateTowards(b, 10));
    P("q.invert", a.clone().invert(), a.clone().conjugate(), a.dot(b), a.lengthSq(), a.length());
    P("q.normalize", Quaternion(s, t, 2, -1).normalize(), Quaternion(0, 0, 0, 0).normalize());
    P("q.multiply", a.clone().multiply(b), a.clone().premultiply(b), Quaternion().multiplyQuaternions(a, b));
    P("q.slerp", a.clone().slerp(b, t), a.clone().slerp(b, 0), a.clone().slerp(b, 1), Quaternion().slerpQuaternions(a, b, t));
    // nearly the same rotation (the straight-line branch), and the far side (the negated one)
    const Quaternion near = a.clone().multiply(Quaternion().setFromAxisAngle(n, 0.01));
    const Quaternion far = b.clone().set(-b.x, -b.y, -b.z, -b.w);
    P("q.slerp.near", a.clone().slerp(near, t), a.clone().slerp(far, t));
    double flat[6] = {0, 0, 0, 0, 0, 0};
    const double bb[5] = {9, b.x, b.y, b.z, b.w};
    Quaternion::slerpFlat(flat, 1, a.toArray(), 0, bb, 1, t);
    P("q.slerpFlat", arr(flat, 6));
    Quaternion::slerpFlat(flat, 2, a.toArray(), 0, near.toArray(), 0, t);
    P("q.slerpFlat.near", arr(flat, 6));
    Quaternion::multiplyQuaternionsFlat(flat, 0, a.toArray(), 0, b.toArray(), 0);
    P("q.multiplyFlat", arr(flat, 6));
    const double five[5] = {1, s, t, 0.5, 2};
    P("q.misc", a.equals(b), a.equals(a.clone()), Quaternion().identity(), Quaternion().fromArray(five, 1), a.toArray());
  }
  {
    // rotations by nearly half a turn about each axis: the three branches without a positive trace
    const std::vector<Vector3> axes = {Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1), Vector3(1, 1, 0).normalize(), Vector3(0, 1, 1).normalize(), Vector3(1, 0.2, 1).normalize()};
    for (const Vector3& axis : axes) {
      for (double angle : {3.0, 3.14, PI, 2.2}) {
        const Matrix4 m = Matrix4().makeRotationAxis(axis, angle);
        P("q.setFromRotationMatrix.case", Quaternion().setFromRotationMatrix(m));
      }
    }
  }

  // --- Euler
  for (int i = 0; i < 4; i++) {
    for (Euler::Order order : ORDERS) {
      const Euler e = E(order);
      const Quaternion q = Q();
      const Matrix4 m = Matrix4().makeRotationFromEuler(e);
      P("e.matrix", Euler::orderName(order), m);
      P("e.setFromRotationMatrix", Euler(0, 0, 0, order).setFromRotationMatrix(m), Euler().setFromRotationMatrix(m, order));
      P("e.setFromQuaternion", Euler(0, 0, 0, order).setFromQuaternion(q), Euler().setFromQuaternion(q, order));
      P("e.roundtrip", Quaternion().setFromEuler(Euler().setFromQuaternion(q, order)));
      for (Euler::Order other : ORDERS) P("e.reorder", e.clone().reorder(other));
      P("e.misc", Euler().setFromVector3(Vector3(e.x, e.z, e.y), order), Euler(1, 2, 3, order).setFromVector3(Vector3(e.x, e.z, e.y)), e.equals(e.clone()), e.clone().set(1, 2, 3), e.clone().set(1, 2, 3, Euler::ZYX));
    }
  }
  for (Euler::Order order : ORDERS) {
    // at the poles (gimbal lock): the middle angle a quarter turn
    for (double quarter : {PI / 2, -PI / 2}) {
      const double a = r(-3, 3), c = r(-3, 3);
      const char middle = Euler::orderName(order)[1];
      const Euler e = middle == 'X' ? Euler(quarter, a, c, order) : middle == 'Y' ? Euler(a, quarter, c, order) : Euler(a, c, quarter, order);
      const Matrix4 m = Matrix4().makeRotationFromEuler(e);
      P("e.pole", Euler().setFromRotationMatrix(m, order), Quaternion().setFromEuler(e));
    }
  }

  // --- Matrix3
  for (int i = 0; i < 6; i++) {
    const Matrix3 a = M3any(), b = M3any();
    const double s = r(-3, 3), t = r(-3, 3);
    const Matrix4 m4 = M4();
    P("m3.set", Matrix3().set(1, 2, 3, 4, 5, 6, 7, 8, 9), Matrix3(1, 2, 3, 4, 5, 6, 7, 8, 9), Matrix3().copy(a).identity());
    P("m3.setFromMatrix4", Matrix3().setFromMatrix4(m4), Matrix3().getNormalMatrix(m4));
    P("m3.multiply", a.clone().multiply(b), a.clone().premultiply(b), Matrix3().multiplyMatrices(a, b), a.clone().multiplyScalar(s));
    P("m3.determinant", a.determinant(), a.clone().invert(), a.clone().transpose());
    Vector3 x, y, z;
    a.extractBasis(x, y, z);
    double nine[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    a.transposeIntoArray(nine);
    P("m3.extractBasis", x, y, z, arr(nine, 9));
    P("m3.uv", Matrix3().setUvTransform(s, t, 2, 3, 0.7, 0.5, 0.25));
    P("m3.make", Matrix3().makeTranslation(s, t), Matrix3().makeTranslation(Vector2(s, t)), Matrix3().makeRotation(s), Matrix3().makeScale(s, t));
    P("m3.deprecated", a.clone().scale(s, t), a.clone().rotate(s), a.clone().translate(s, t));
    double ten[10] = {9};
    a.toArray(ten, 1);
    P("m3.misc", a.equals(b), a.equals(a.clone()), Matrix3().fromArray(ten, 1), a.toArray());
  }
  P("m3.singular", Matrix3().set(1, 2, 3, 2, 4, 6, 0, 1, 5).invert(), Matrix3().set(1, 2, 3, 2, 4, 6, 0, 1, 5).determinant());

  // --- Matrix4
  for (int i = 0; i < 8; i++) {
    const Matrix4 a = M4(), b = M4(), c = M4any();
    const double s = r(-3, 3), t = r(-3, 3), u = r(-3, 3);
    const Vector3 p = V3();
    const Quaternion q = Q();
    const Vector3 sc = V3(0.2, 3), n = V3(-1, 1).normalize();
    const Euler e = E(ORDERS[i % 6]);
    P("m4.set", Matrix4().set(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16), Matrix4(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16), Matrix4().copy(a).identity());
    const Matrix3 m3 = M3any();
    P("m4.copyPosition", b.clone().copyPosition(a), Matrix4().setFromMatrix3(m3));
    Vector3 x, y, z;
    a.extractBasis(x, y, z);
    P("m4.extractBasis", x, y, z, Matrix4().makeBasis(x, y, z), Matrix4().extractRotation(a));
    P("m4.makeRotationFromEuler", Matrix4().makeRotationFromEuler(e), Matrix4().makeRotationFromQuaternion(q));
    P("m4.lookAt", Matrix4().lookAt(p, sc, n), a.clone().lookAt(p, sc, Vector3(0, 1, 0)));
    P("m4.multiply", a.clone().multiply(b), a.clone().premultiply(b), Matrix4().multiplyMatrices(a, c), a.clone().multiplyScalar(s));
    P("m4.determinant", a.determinant(), c.determinant(), a.determinantAffine(), c.determinantAffine());
    P("m4.transpose", c.clone().transpose(), a.clone().setPosition(p), a.clone().setPosition(s, t, u));
    P("m4.invert", a.clone().invert(), c.clone().invert());
    P("m4.scale", a.clone().scale(sc), a.getMaxScaleOnAxis(), c.getMaxScaleOnAxis());
    P("m4.makeTranslation", Matrix4().makeTranslation(s, t, u), Matrix4().makeTranslation(p));
    P("m4.makeRotation", Matrix4().makeRotationX(s), Matrix4().makeRotationY(t), Matrix4().makeRotationZ(u), Matrix4().makeRotationAxis(n, s));
    P("m4.makeScale", Matrix4().makeScale(s, t, u), Matrix4().makeShear(s, t, u, 0.5, -0.25, 2));
    const Matrix4 m = Matrix4().compose(p, q, sc);
    Vector3 p2, s2;
    Quaternion q2;
    m.decompose(p2, q2, s2);
    P("m4.compose", m, p2, q2, s2);
    // a mirrored one (negative determinant) and anything at all
    const Matrix4 mirrored = Matrix4().compose(p, q, Vector3(-sc.x, sc.y, sc.z));
    mirrored.decompose(p2, q2, s2);
    P("m4.decompose.mirrored", p2, q2, s2);
    c.decompose(p2, q2, s2);
    P("m4.decompose.any", p2, q2, s2);
    const double near = 0.1 + rnd_();
    const double far = near + 10 + 1000 * rnd_();
    P("m4.makePerspective", Matrix4().makePerspective(s - 5, s + 4, t + 3, t - 2, near, far), Matrix4().makeOrthographic(s - 5, s + 4, t + 3, t - 2, near, far));
    double seventeen[17] = {9};
    a.toArray(seventeen, 1);
    P("m4.misc", a.equals(b), a.equals(a.clone()), Matrix4().fromArray(seventeen, 1), a.toArray());
  }
  {
    Matrix4 flat = Matrix4().makeScale(1, 0, 1);
    Vector3 p2, s2(2, 2, 2);
    Quaternion q2(0.5, 0.5, 0.5, 0.5);
    flat.setPosition(1, 2, 3).decompose(p2, q2, s2);
    Vector3 x, y, z;
    flat.extractBasis(x, y, z);
    P("m4.singular", flat.clone().invert(), p2, q2, s2, x, y, z, Matrix4().extractRotation(flat), flat.determinant());
    // lookAt where there is nothing to look along, and straight up and down
    const Vector3 o(1, 2, 3);
    P("m4.lookAt.same", Matrix4().lookAt(o, o, Vector3(0, 1, 0)));
    P("m4.lookAt.up", Matrix4().lookAt(o, Vector3(1, 7, 3), Vector3(0, 1, 0)), Matrix4().lookAt(o, Vector3(1, -7, 3), Vector3(0, 1, 0)));
    P("m4.lookAt.upZ", Matrix4().lookAt(o, Vector3(1, 2, 9), Vector3(0, 0, 1)), Matrix4().lookAt(o, Vector3(1, 2, -9), Vector3(0, 0, -1)));
  }

  // --- Color
  const std::vector<uint32_t> hexes = {0x808890, 0xffffff, 0x000000, 0xc2d3e4, 0xfff2d0, 0x3a74c6, 0xff2a1a, 0x1aff6a, 0x141416, 0x0a0b0c, 0x010203, 0xfefdfc, 0x7f8081, 0x123456, 0xabcdef};
  for (uint32_t hex : hexes) {
    const Color c(hex);
    P("c.hex", c, c.getHex(), c.getHexString(), c.getStyle(), c.getHex(LinearSRGBColorSpace), c.getHexString(LinearSRGBColorSpace), c.getStyle(LinearSRGBColorSpace));
    P("c.setHex", Color().setHex(hex), Color().setHex(hex, LinearSRGBColorSpace), Color().set(hex), Color(1, 0, 0).setHex(hex + 0.75));
    Color rgb1, rgb2;
    P("c.getHSL", hsl(c), hsl(c, SRGBColorSpace), c.getRGB(rgb1), c.getRGB(rgb2, SRGBColorSpace));
  }
  const std::vector<std::string> styles = {"#f00", "#fa3", "#FA3", "#808890", "#FFAA00", "#ffaa0", "#12", "#12345g", "rgb(255,0,0)", "rgb( 12 , 34 , 56 )", "rgba(255,128,0,0.5)", "rgba(255,128,0,.5)",
      "rgb(100%,50%,0%)", "rgb(12.5%, .5%, 100%)", "rgba(10%,20%,30%, 0.25 )", "rgb(300,0,999)", "rgb(150%,0%,7%)", "hsl(120,50%,50%)", "hsla(-30.5, 100%, 25%, .3)", "hsl(+400,10%,90%)",
      "hsl(.5,0%,33%)", "hsl(200, 100%, 100%)", "hsl(20,120%,40%)", "skyblue", "SkyBlue", "rebeccapurple", "RED", "darkslategrey", "yellowgreen", "aliceblue", "nonsense", "",
      "rgb(1,2)", "rgb(1,2,3,)", "rgb(1.5,2,3)", "rgb(1,2,3) and more", "foo(1,2,3)", "rgb 1,2,3", " rgb(1,2,3)", "hsl(120,50,50)", "rgb(1,2,3", "rgb(10,20,30,1.5.2)", "rgb(10,20,30,2.)"};
  for (const std::string& style : styles) {
    const Color c = Color(0.1, 0.2, 0.3).setStyle(style);
    P("c.setStyle", style, c, c.getHexString(), Color(0.1, 0.2, 0.3).setStyle(style, LinearSRGBColorSpace), Color(style), Color(0.1, 0.2, 0.3).set(style));
  }
  for (int i = 0; i < 8; i++) {
    const Color a = C(), b = C();
    const double s = rnd_(), t = rnd_(), h = r(-2, 2);
    const Matrix3 m = M3any();
    P("c.setRGB", Color().setRGB(a.r, a.g, a.b), Color().setRGB(a.r, a.g, a.b, SRGBColorSpace), Color().set(a.r, a.g, a.b), Color().set(a), Color().setScalar(s));
    P("c.setHSL", Color().setHSL(h, s, t), Color().setHSL(h, s, t, SRGBColorSpace), Color().setHSL(h, 0, t), Color().setHSL(h, 1.5, t), Color().setHSL(h, s, t * 2 - 0.5));
    P("c.getHSL", hsl(a), hsl(a, SRGBColorSpace), a.getHex(), a.getHexString(), a.getStyle(), a.getStyle(LinearSRGBColorSpace));
    P("c.offsetHSL", a.clone().offsetHSL(h, s - 0.5, t - 0.5), a.clone().lerpHSL(b, t));
    P("c.convert", a.clone().convertSRGBToLinear(), a.clone().convertLinearToSRGB(), Color().copySRGBToLinear(a), Color().copyLinearToSRGB(a));
    P("c.add", a.clone().add(b), Color().addColors(a, b), a.clone().addScalar(s), a.clone().sub(b), a.clone().multiply(b), a.clone().multiplyScalar(s * 3));
    P("c.lerp", a.clone().lerp(b, t), Color().lerpColors(a, b, t), Color().setFromVector3(Vector3(s, t, h)), a.clone().applyMatrix3(m), a.equals(b), a.equals(a.clone()));
    const double four[4] = {9, s, t, h};
    P("c.over", a.clone().multiplyScalar(3).getHex(), a.clone().multiplyScalar(3).getStyle(), a.clone().addScalar(-2).getHex(), Color().fromArray(four, 1), a.toArray());
  }
  P("c.grey", hsl(Color(0.5, 0.5, 0.5)), hsl(Color(0, 0, 0)), hsl(Color(1, 1, 1)), hsl(Color(1, 0, 0)), hsl(Color(0, 1, 0)), hsl(Color(0, 0, 1)), hsl(Color(1, 0, 1)));
  P("c.default", Color(), Color(0), Color(0xffffff), Color("white"));

  // --- Box2
  for (int i = 0; i < 6; i++) {
    const Box2 a = B2(), b = B2();
    const Vector2 p = V2(), q = V2();
    const double s = r(0, 3);
    Vector2 t1, t2, t3(1, 1), t4(1, 1);
    P("b2.set", a, b, Box2().set(p, q), Box2().setFromCenterAndSize(p, Vector2(s, s + 1)), Box2().setFromPoints({p, q, a.min}));
    P("b2.get", a.getCenter(t1), a.getSize(t2), a.isEmpty(), Box2().isEmpty(), Box2().getCenter(t3), Box2().getSize(t4));
    P("b2.expand", a.clone().expandByPoint(p), a.clone().expandByVector(Vector2(s, 1)), a.clone().expandByScalar(s));
    P("b2.contains", a.containsPoint(p), a.containsPoint(a.getCenter()), a.containsBox(b), a.clone().union_(b).containsBox(b), a.intersectsBox(b), a.getParameter(p, t1));
    P("b2.clamp", a.clampPoint(p, t2), a.distanceToPoint(p), a.clone().intersect(b), a.clone().union_(b), a.clone().translate(p), a.equals(b), a.equals(a.clone()));
  }
  P("b2.empty", Box2().makeEmpty(), Box2(Vector2(0, 0), Vector2(1, 1)).intersect(Box2(Vector2(2, 2), Vector2(3, 3))));

  // --- Box3
  for (int i = 0; i < 8; i++) {
    const Box3 a = B3(), b = B3();
    const Vector3 p = V3(), q = V3();
    const double s = r(0, 3);
    const Matrix4 m = M4();
    const Sphere sph = SPH();
    const Plane pl = PL();
    Vector3 t1, t2, t3(1, 1, 1), t4(1, 1, 1);
    Sphere s1, s2(p, 3);
    P("b3.set", a, b, Box3().set(p, q), Box3().setFromCenterAndSize(p, Vector3(s, s + 1, s + 2)), Box3().setFromPoints({p, q, a.min}), Box3().setFromArray(std::vector<double>{p.x, p.y, p.z, q.x, q.y, q.z, s, s, s}));
    P("b3.get", a.getCenter(t1), a.getSize(t2), a.isEmpty(), Box3().isEmpty(), Box3().getCenter(t3), Box3().getSize(t4));
    P("b3.expand", a.clone().expandByPoint(p), a.clone().expandByVector(Vector3(s, 1, 2)), a.clone().expandByScalar(s));
    P("b3.contains", a.containsPoint(p), a.containsPoint(a.getCenter()), a.containsBox(b), a.clone().union_(b).containsBox(b), a.intersectsBox(b), a.getParameter(p, t1));
    P("b3.intersects", a.intersectsSphere(sph), a.intersectsPlane(pl), sph.intersectsBox(a), pl.intersectsBox(a));
    P("b3.clamp", a.clampPoint(p, t2), a.distanceToPoint(p), a.getBoundingSphere(s1), Box3().getBoundingSphere(s2));
    P("b3.combine", a.clone().intersect(b), a.clone().union_(b), a.clone().applyMatrix4(m), a.clone().translate(p), a.equals(b), a.equals(a.clone()), Box3().applyMatrix4(m));
  }
  {
    // triangles around a box: how many touch it, and which
    const Box3 box(Vector3(-2, -1, -3), Vector3(2, 1.5, 3));
    int hits = 0;
    std::vector<bool> which;
    for (int i = 0; i < 400; i++) {
      const Vector3 c = V3(-5, 5);
      const Vector3 ta = c.clone().add(V3(-2.5, 2.5));
      const Vector3 tb = c.clone().add(V3(-2.5, 2.5));
      const Vector3 tc = c.clone().add(V3(-2.5, 2.5));
      const Triangle tri(ta, tb, tc);
      const bool hit = box.intersectsTriangle(tri);
      if (hit != tri.intersectsBox(box)) std::abort();
      if (hit) hits++;
      if (i < 60) which.push_back(hit);
    }
    P("b3.intersectsTriangle", hits, which, Box3().intersectsTriangle(Triangle()));
  }

  // --- Sphere
  for (int i = 0; i < 8; i++) {
    const Sphere a = SPH(), b = SPH();
    const Vector3 p = V3();
    const Matrix4 m = M4();
    const Plane pl = PL();
    std::vector<Vector3> pts;
    for (int k = 0; k < 5; k++) pts.push_back(V3());
    Vector3 t1, t2;
    Box3 b1, b2(p, p);
    P("s.setFromPoints", Sphere().setFromPoints(pts), Sphere().setFromPoints(pts, &p), Sphere().set(p, 2.5), Sphere().copy(a));
    P("s.contains", a.containsPoint(p), a.containsPoint(a.center), a.distanceToPoint(p), a.intersectsSphere(b), a.intersectsPlane(pl), pl.intersectsSphere(a), pl.distanceToSphere(a));
    P("s.clamp", a.clampPoint(p, t1), a.clampPoint(a.center, t2), a.getBoundingBox(b1), Sphere().getBoundingBox(b2));
    P("s.apply", a.clone().applyMatrix4(m), a.clone().translate(p), a.clone().expandByPoint(p), a.clone().expandByPoint(a.center), Sphere().expandByPoint(p));
    P("s.union", a.clone().union_(b), a.clone().union_(Sphere()), Sphere().union_(b), a.clone().union_(Sphere(a.center, 9)), a.clone().union_(Sphere(a.center, 0.1)), a.equals(b), a.equals(a.clone()));
  }
  P("s.empty", Sphere().isEmpty(), Sphere(Vector3(1, 2, 3), 0).isEmpty(), Sphere(Vector3(1, 2, 3), 2).makeEmpty());

  // --- Plane, Line3
  for (int i = 0; i < 8; i++) {
    const Plane a = PL();
    const Vector3 p = V3(), q = V3(), w = V3();
    const Matrix4 m = M4();
    const Vector3 n = V3(-1, 1).normalize();
    const Line3 line(p, q);
    const Vector3 w2 = V3();
    const Line3 other(w, w2);
    Vector3 t1, t2, t3, t4;
    const Matrix3 normalMatrix = Matrix3().getNormalMatrix(m);
    P("p.set", Plane().set(n, 2), Plane().setComponents(1, 2, 3, 4), Plane().setFromNormalAndCoplanarPoint(n, p), Plane().setFromCoplanarPoints(p, q, w), Plane().copy(a));
    P("p.normalize", Plane(p, 3).normalize(), a.clone().negate(), a.distanceToPoint(p), a.projectPoint(p, t1), a.coplanarPoint(t2));
    P("p.intersectLine", a.intersectLine(line, t3), a.intersectLine(line, t4, false), a.intersectsLine(line));
    P("p.apply", a.clone().applyMatrix4(m), a.clone().applyMatrix4(m, &normalMatrix), a.clone().translate(p), a.equals(a.clone()), a.equals(Plane()));
    P("l.get", line.getCenter(t1), line.delta(t2), line.distanceSq(), line.distance(), line.at(0.3, t3));
    P("l.closest", line.closestPointToPointParameter(w, false), line.closestPointToPointParameter(w, true), line.closestPointToPoint(w, true, t1), line.closestPointToPoint(w, false, t2));
    {
      Vector3 c1, c2;
      const double d1 = line.distanceSqToLine3(other, &c1, &c2);
      const Vector3 c1a = c1, c2a = c2;
      const double d2 = line.distanceSqToLine3(Line3(w, w), &c1, &c2);
      const Vector3 c1b = c1, c2b = c2;
      const double d3 = Line3(p, p).distanceSqToLine3(other, &c1, &c2);
      P("l.distanceSqToLine3", d1, c1a, c2a, d2, c1b, c2b, d3, c1, c2);
    }
    P("l.apply", line.clone().applyMatrix4(m), line.equals(other), line.equals(line.clone()), Line3(p, p).distanceSqToLine3(Line3(w, w)), line.distanceSqToLine3(Line3(p.clone().add(n), q.clone().add(n))));
  }
  {
    const Plane plane(Vector3(0, 1, 0), -1);
    const Line3 inPlane(Vector3(1, 1, 1), Vector3(4, 1, 2)), beside(Vector3(1, 2, 1), Vector3(4, 2, 2));
    Vector3 t1, t2;
    P("p.intersectLine.coplanar", plane.intersectLine(inPlane, t1), plane.intersectLine(beside, t2), plane.intersectsLine(inPlane), plane.intersectsLine(beside));
  }

  // --- Frustum
  for (int i = 0; i < 4; i++) {
    const Matrix4 proj = i % 2 == 0 ? Matrix4().makePerspective(-0.08, 0.06, 0.05, -0.045, 0.1, 400) : Matrix4().makeOrthographic(-20, 30, 12, -9, 0.5, 300);
    const Vector3 vp = V3();
    const Quaternion vq = Q();
    const Matrix4 view = Matrix4().compose(vp, vq, Vector3(1, 1, 1)).invert();
    const Frustum f = Frustum().setFromProjectionMatrix(Matrix4().multiplyMatrices(proj, view));
    P("f.planes", f.planes[0], f.planes[1], f.planes[2], f.planes[3], f.planes[4], f.planes[5]);
    std::vector<bool> inSphere, inBox, inPoint;
    for (int k = 0; k < 60; k++) {
      const Vector3 c = V3(-150, 150);
      const double rad = r(1, 60);
      const Vector3 d = V3(1, 40);
      inSphere.push_back(f.intersectsSphere(Sphere(c, rad)));
      inBox.push_back(f.intersectsBox(Box3(c.clone().sub(d), c.clone().add(d))));
      inPoint.push_back(f.containsPoint(c));
    }
    P("f.intersects", inSphere, inBox, inPoint);
    P("f.copy", Frustum().copy(f).planes[3], f.clone().planes[5]);
  }

  // --- Ray
  for (int i = 0; i < 10; i++) {
    const Ray a = RAY();
    const Vector3 p = V3(), q = V3();
    const Sphere sph = SPH();
    const Plane pl = PL();
    const Box3 box = B3();
    const Matrix4 m = M4();
    const double t = r(0, 20);
    Vector3 t1, t2, t3, t4, t5;
    P("r.at", a.at(t, t1), a.clone().lookAt(p), a.clone().recast(t), Ray().set(p, q), Ray().copy(a));
    P("r.closest", a.closestPointToPoint(p, t1), a.distanceToPoint(p), a.distanceSqToPoint(p), a.closestPointToPoint(a.at(-3, t2), t3), a.distanceSqToPoint(a.at(-3, t4)));
    P("r.sphere", a.intersectSphere(sph, t1), a.intersectsSphere(sph), Ray(sph.center.clone().addScalar(0.1), a.direction).intersectSphere(sph, t2), a.intersectSphere(Sphere(), t3), a.intersectsSphere(Sphere()));
    P("r.plane", a.distanceToPlane(pl), a.intersectPlane(pl, t1), a.intersectsPlane(pl));
    P("r.box", a.intersectBox(box, t1), a.intersectsBox(box), Ray(box.getCenter(), a.direction).intersectBox(box, t2));
    P("r.apply", a.clone().applyMatrix4(m), a.equals(a.clone()), a.equals(Ray()));
    (void)t5;
  }
  {
    // rays along the axes (a division by zero in intersectBox) and in a plane
    const Box3 box(Vector3(-1, -2, -3), Vector3(1, 2, 3));
    const std::vector<Vector3> dirs = {Vector3(1, 0, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0), Vector3(0, -1, 0), Vector3(0, 0, 1), Vector3(0, 0, -1)};
    for (const Vector3& d : dirs) {
      for (const Vector3& o : {Vector3(0.5, 0.5, 0.5), Vector3(-5, 0.5, 0.5), Vector3(0.5, 9, 0.5), Vector3(0.5, 0.5, -7), Vector3(1, 2, 20), Vector3(4, 4, 4)}) {
        Vector3 t1;
        P("r.box.axis", Ray(o, d).intersectBox(box, t1));
      }
    }
    const Plane plane(Vector3(0, 1, 0), -1);
    Vector3 t1, t2;
    P("r.plane.coplanar", Ray(Vector3(0, 1, 0), Vector3(1, 0, 0)).distanceToPlane(plane), Ray(Vector3(0, 2, 0), Vector3(1, 0, 0)).distanceToPlane(plane),
      Ray(Vector3(0, 2, 0), Vector3(1, 0, 0)).intersectsPlane(plane), Ray(Vector3(0, 1, 0), Vector3(1, 0, 0)).intersectsPlane(plane),
      Ray(Vector3(0, 2, 0), Vector3(0, 1, 0)).intersectPlane(plane, t1), Ray(Vector3(0, 2, 0), Vector3(0, -1, 0)).intersectPlane(plane, t2));
  }
  {
    // the distance to segments: every region of the algorithm is met with enough of them
    double sum = 0;
    for (int i = 0; i < 300; i++) {
      const Ray a = RAY();
      const Vector3 v0 = V3();
      Vector3 v1;
      if (i % 7 == 0) {
        const double along = r(1, 5);
        v1 = v0.clone().addScaledVector(a.direction, along);
      } else {
        v1 = V3();
      }
      Vector3 onRay, onSegment;
      const double d = a.distanceSqToSegment(v0, v1, &onRay, &onSegment);
      sum += d;
      if (i < 40) P("r.distanceSqToSegment", d, onRay, onSegment, a.distanceSqToSegment(v0, v1));
    }
    P("r.distanceSqToSegment.sum", sum);
  }
  {
    // triangles: hits from the front, from the back, and misses
    int hits = 0, culled = 0;
    Vector3 sum;
    for (int i = 0; i < 600; i++) {
      const Ray a = RAY();
      const double along = r(-4, 12);
      Vector3 c;
      a.at(along, c);
      const Vector3 ta = c.clone().add(V3(-3, 3));
      const Vector3 tb = c.clone().add(V3(-3, 3));
      const Vector3 tc = c.clone().add(V3(-3, 3));
      Vector3 target(7, 7, 7), target2(7, 7, 7);
      const Vector3* hit = a.intersectTriangle(ta, tb, tc, false, target);
      const Vector3* hit2 = a.intersectTriangle(ta, tb, tc, true, target2);
      if (hit) {
        hits++;
        sum.add(target);
      }
      if (hit2) culled++;
      if (i < 60) P("r.intersectTriangle", hit, hit2, target, target2);
    }
    P("r.intersectTriangle.sum", hits, culled, sum);
    // along each axis, both ways (the six cases of the largest direction component), through corners and edges
    const Vector3 tri[3] = {Vector3(-1, -1, 0), Vector3(2, -1, 0), Vector3(-1, 2, 0)};
    const int perms[3][3] = {{0, 1, 2}, {1, 2, 0}, {2, 0, 1}};
    for (const auto& perm : perms) {
      Vector3 pts[3];
      for (int k = 0; k < 3; k++) pts[k] = Vector3().setComponent(perm[0], tri[k].x).setComponent(perm[1], tri[k].y).setComponent(perm[2], tri[k].z);
      for (int sign : {1, -1}) {
        const double ats[6][2] = {{0, 0}, {-1, -1}, {0.5, -1}, {0.5, 0.5}, {3, 3}, {-1, 2}};
        for (const auto& at : ats) {
          const Vector3 o = Vector3().setComponent(perm[0], at[0]).setComponent(perm[1], at[1]).setComponent(perm[2], -5 * sign);
          const Vector3 d = Vector3().setComponent(perm[2], sign);
          Vector3 t1, t2, t3;
          P("r.intersectTriangle.axis", Ray(o, d).intersectTriangle(pts[0], pts[1], pts[2], false, t1), Ray(o, d).intersectTriangle(pts[0], pts[1], pts[2], true, t2),
            Ray(o, d.clone().negate()).intersectTriangle(pts[0], pts[1], pts[2], false, t3));
        }
      }
    }
    Vector3 t1, t2, t3;
    P("r.intersectTriangle.degenerate", Ray(Vector3(0, 0, 5), Vector3(0, 0, -1)).intersectTriangle(tri[0], tri[0], tri[1], false, t1),
      Ray(Vector3(0, 0, 5), Vector3(0, 0, 0)).intersectTriangle(tri[0], tri[1], tri[2], false, t2),
      Ray(Vector3(0, 0, 0), Vector3(1, 0, 0)).intersectTriangle(tri[0], tri[1], tri[2], false, t3));
  }

  // --- Triangle
  for (int i = 0; i < 10; i++) {
    const Vector3 a = V3(), b = V3(), c = V3(), p = V3(), d = V3(-1, 1).normalize();
    const Triangle t(a, b, c);
    const Vector3 inside = a.clone().multiplyScalar(0.2).addScaledVector(b, 0.5).addScaledVector(c, 0.3);
    Vector3 t1, t2, t3, t4, t5, t6;
    Vector2 uv;
    Plane plane;
    P("t.normal", Triangle::getNormal(a, b, c, t1), t.getNormal(t2), t.getArea(), t.getMidpoint(t3), t.getPlane(plane));
    P("t.barycoord", Triangle::getBarycoord(p, a, b, c, t4), t.getBarycoord(inside, t5), Triangle::containsPoint(p, a, b, c), t.containsPoint(inside));
    P("t.interpolation", Triangle::getInterpolation(inside, a, b, c, Vector2(0, 0), Vector2(1, 0), Vector2(0, 1), uv),
      t.getInterpolation(p, Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1), t6));
    P("t.facing", Triangle::isFrontFacing(a, b, c, d), t.isFrontFacing(d.clone().negate()), t.equals(t.clone()), t.equals(Triangle()));
    P("t.set", Triangle().set(c, b, a), Triangle().setFromPointsAndIndices({a, b, c, p}, 3, 0, 2), Triangle().copy(t));
  }
  {
    const Vector3 a(1, 2, 3), b(2, 4, 6), c(4, 8, 12);
    Vector3 t1(1, 1, 1), t2(1, 1, 1);
    Vector2 uv(5, 5);
    P("t.degenerate", Triangle::getNormal(a, b, c, t1), Triangle::getBarycoord(a, a, b, c, t2), Triangle::containsPoint(a, a, b, c),
      Triangle::getInterpolation(a, a, b, c, Vector2(0, 0), Vector2(1, 0), Vector2(0, 1), uv), Triangle(a, b, c).getArea());
    // the closest point: every region of the triangle is met with enough points
    const Triangle t(Vector3(-2, 0, 1), Vector3(3, 0.5, -1), Vector3(0.5, 4, 0.5));
    Vector3 sum;
    for (int i = 0; i < 300; i++) {
      const Vector3 p = V3(-8, 8);
      Vector3 q;
      t.closestPointToPoint(p, q);
      sum.add(q);
      if (i < 40) P("t.closestPointToPoint", q);
    }
    Vector3 t3, t4;
    P("t.closestPointToPoint.sum", sum, Triangle(a, a, c).closestPointToPoint(Vector3(0, 0, 0), t3), Triangle(a, a, c).closestPointToPoint(Vector3(3, 5, 9), t4));
  }

  // --- Spherical
  for (int i = 0; i < 6; i++) {
    const Vector3 p = V3();
    const Spherical s = Spherical().setFromVector3(p);
    P("sph.set", s, Vector3().setFromSpherical(s), Spherical(2, i < 3 ? 0 : 4, 1).makeSafe(), Spherical().setFromCartesianCoords(0, 0, 0), Spherical().set(1, 2, 3), Spherical().copy(s), s.clone());
  }
}

// ---------------------------------------------------------------------------
// what the scene and the geometry sections share
// ---------------------------------------------------------------------------

static const char* arrayName(AttrType t) {
  switch (t) {
    case AttrType::F32: return "Float32Array";
    case AttrType::U8: return "Uint8Array";
    case AttrType::I8: return "Int8Array";
    case AttrType::U16: return "Uint16Array";
    case AttrType::I16: return "Int16Array";
    case AttrType::U32: return "Uint32Array";
    case AttrType::I32: return "Int32Array";
  }
  return "?";
}
/** every number of an attribute's array */
static std::vector<double> all(const BufferAttribute& a) {
  std::vector<double> v;
  for (size_t i = 0; i < a.length(); i++) v.push_back(a.at(i));
  return v;
}
/** an attribute: its kind of array, count, itemSize, normalized, the sum and the sum of squares, the first twelve numbers */
static void A(const std::string& label, const BufferAttribute* a) {
  if (!a) {
    P(label, "none");
    return;
  }
  double s = 0, q = 0;
  std::vector<double> first;
  for (size_t i = 0; i < a->length(); i++) {
    const double x = a->at(i);
    s += x;
    q += x * x;
    if (i < 12) first.push_back(x);
  }
  P(label, arrayName(a->type), a->count(), a->itemSize, a->normalized, s, q, first);
}
static void A(const std::string& label, const AttrPtr& a) { A(label, a.get()); }
static void A(const std::string& label, const BufferAttribute& a) { A(label, &a); }
/** a geometry: its attributes in their order, the index, the groups, the draw range and the bounds it has */
static void G(const std::string& label, const BufferGeometry& g) {
  std::string names;
  for (const auto& entry : g.attributes) names += (names.empty() ? "" : ",") + entry.first;
  if (names.empty()) names = "none";
  P(label, names, g.index ? static_cast<long>(g.index->count()) : -1L, g.groups.size(), g.drawRange.start, g.drawRange.count == INT_MAX ? -1 : g.drawRange.count);
  for (const auto& entry : g.attributes) A(label + "." + entry.first, entry.second);
  A(label + ".index", g.index);
  for (const BufferGeometry::Group& group : g.groups) P(label + ".group", group.start, group.count, group.materialIndex);
  P(label + ".bounds", g.boundingBox, g.boundingSphere);
}
static void G(const std::string& label, const GeometryPtr& g) { G(label, *g); }
/** a wavy sheet of nx × ny squares, with an index */
static GeometryPtr grid(int nx, int ny, double w, double h) {
  std::vector<float> pos, uv;
  std::vector<uint32_t> idx;
  for (int j = 0; j <= ny; j++) {
    for (int i = 0; i <= nx; i++) {
      pos.push_back(static_cast<float>((static_cast<double>(i) / nx - 0.5) * w));
      pos.push_back(static_cast<float>((static_cast<double>(j) / ny - 0.5) * h));
      pos.push_back(static_cast<float>(0.3 * std::sin(i * 1.7 + j * 0.9)));
      uv.push_back(static_cast<float>(static_cast<double>(i) / nx));
      uv.push_back(static_cast<float>(static_cast<double>(j) / ny));
    }
  }
  for (int j = 0; j < ny; j++) {
    for (int i = 0; i < nx; i++) {
      const uint32_t a = static_cast<uint32_t>(j * (nx + 1) + i), b = a + 1, c = a + static_cast<uint32_t>(nx) + 1, d = c + 1;
      for (uint32_t k : {a, b, d, a, d, c}) idx.push_back(k);
    }
  }
  auto g = make<BufferGeometry>();
  g->setAttribute("position", attr(std::move(pos), 3));
  g->setAttribute("uv", attr(std::move(uv), 2));
  g->setIndex(idx);
  return g;
}
/** n triangles anywhere, without an index */
static GeometryPtr soup(int n, double spread = 4) {
  std::vector<float> pos;
  for (int i = 0; i < n; i++) {
    const Vector3 c = V3(-spread, spread);
    for (int k = 0; k < 3; k++) {
      const Vector3 p = c.clone().add(V3(-1, 1));
      pos.push_back(static_cast<float>(p.x));
      pos.push_back(static_cast<float>(p.y));
      pos.push_back(static_cast<float>(p.z));
    }
  }
  auto g = make<BufferGeometry>();
  g->setAttribute("position", attr(std::move(pos), 3));
  return g;
}

// ---------------------------------------------------------------------------
// the scene graph
// ---------------------------------------------------------------------------

static std::string names(const Object3D& o) {
  std::string s;
  for (const ObjectPtr& c : o.children) s += (s.empty() ? "" : ",") + c->name;
  return s.empty() ? "none" : s;
}
static void place(Object3D& o, double s) {
  o.position.copy(V3());
  o.quaternion.copy(Q());
  o.scale.setScalar(s);
}
static void hitsOf(const std::string& label, const std::vector<Intersection>& hits) {
  P(label, hits.size());
  for (size_t i = 0; i < hits.size() && i < 4; i++) {
    const Intersection& h = hits[i];
    P(label + ".hit", h.distance, h.point, h.a, h.b, h.c, h.normal, h.instanceId, h.object->name);
  }
}
/** a ray at a point of the object (given in its own frame), from somewhere near */
static Raycaster rayAt(Object3D& object, double x, double y, double z, double near = 0, double far = 1e300) {
  Vector3 at(x, y, z);
  object.localToWorld(at);
  const Vector3 from = at.clone().add(V3(-6, 6));
  return Raycaster(from, at.clone().sub(from).normalize(), near, far);
}
template <class T>
static void attrKind(const char* name) {
  for (bool normalized : {false, true}) {
    auto a = attr(std::vector<T>(8), 4, normalized);
    const double x = rnd_(), y = r(-1, 1), z = r(-2, 2), w = r(0, 300);
    a->setXYZW(0, x, y, z, w);
    a->setX(1, y);
    a->setY(1, x);
    a->setZ(1, w);
    a->setW(1, z);
    P(std::string("attr.") + name, normalized, all(*a), a->getX(0), a->getY(0), a->getZ(0), a->getW(0), a->getX(1), a->getY(1), a->getZ(1), a->getW(1), a->count());
    a->setXY(0, w, z);
    a->setXYZ(1, x, x, y);
    P(std::string("attr.") + name + ".xy", all(*a));
  }
}
static const char* blendName(Blending b) {
  switch (b) {
    case Blending::None: return "None";
    case Blending::Normal: return "Normal";
    case Blending::Additive: return "Additive";
    case Blending::Multiply: return "Multiply";
  }
  return "?";
}
static const char* filterName(Filter f) {
  switch (f) {
    case Filter::Nearest: return "Nearest";
    case Filter::Linear: return "Linear";
    case Filter::NearestMipmapNearest: return "NearestMipmapNearest";
    case Filter::LinearMipmapNearest: return "LinearMipmapNearest";
    case Filter::NearestMipmapLinear: return "NearestMipmapLinear";
    case Filter::LinearMipmapLinear: return "LinearMipmapLinear";
  }
  return "?";
}
static const char* wrapName(Wrapping w) {
  switch (w) {
    case Wrapping::ClampToEdge: return "ClampToEdge";
    case Wrapping::Repeat: return "Repeat";
    case Wrapping::MirroredRepeat: return "MirroredRepeat";
  }
  return "?";
}
static void info(const char* label, const Material& m) {
  std::fputs(label, stdout);
  if (m.type == MaterialType::Shader) put("none");
  else put(m.color);
  put(m.opacity), put(m.transparent), put(static_cast<int>(m.side)), put(m.depthWrite), put(m.depthTest), put(m.fog), put(m.toneMapped), put(m.vertexColors), put(m.alphaTest),
      put(blendName(m.blending)), put(m.polygonOffset), put(m.polygonOffsetFactor), put(m.polygonOffsetUnits), put(m.colorWrite), put(m.visible), put(m.name);
  std::putchar('\n');
}
static void tex(const char* label, const Texture& t) {
  P(label, filterName(t.magFilter), filterName(t.minFilter), t.generateMipmaps, t.flipY, wrapName(t.wrapS), wrapName(t.wrapT), t.anisotropy, t.repeat, t.offset, t.version);
}

static void sceneSection() {
  seed(77001);

  // --- the angles and the quaternion of an object are one
  {
    auto o = make<Object3D>();
    P("rot.start", o->rotation.euler(), o->quaternion);
    for (int i = 0; i < 6; i++) {
      const Euler::Order order = ORDERS[i];
      const double x = r(-3, 3), y = r(-3, 3), z = r(-3, 3);
      o->rotation.order(order);
      P("rot.order", o->rotation.euler(), o->quaternion);
      o->rotation.set(x, y, z);
      P("rot.set", o->rotation.euler(), o->quaternion);
      o->rotation.y = z;
      P("rot.y", o->rotation.euler(), o->quaternion);
      o->quaternion.copy(Q());
      P("rot.fromQuaternion", o->rotation.euler(), o->quaternion);
      o->rotation.y = x;
      P("rot.y.after", o->rotation.euler(), o->quaternion);
      o->rotation.x += 0.25;
      o->rotation.z -= 0.5;
      P("rot.plus", o->rotation.euler(), o->quaternion, static_cast<double>(o->rotation.x), static_cast<double>(o->rotation.y), static_cast<double>(o->rotation.z));
      o->quaternion.setFromAxisAngle(V3(-1, 1).normalize(), y);
      o->rotation.z = 1;
      P("rot.z", o->rotation.euler(), o->quaternion);
      o->rotateY(0.4);
      o->rotateOnWorldAxis(Vector3(1, 0, 0), 0.3);
      P("rot.rotateY", o->rotation.euler(), o->quaternion);
      o->rotation.copy(Euler(z, x, y, ORDERS[(i + 2) % 6]));
      P("rot.copy", o->rotation.euler(), o->quaternion);
      o->quaternion.multiply(Q());
      o->rotation.order(order);
      P("rot.order.after", o->rotation.euler(), o->quaternion);
      o->rotation.setFromQuaternion(Q());
      P("rot.setFromQuaternion", o->rotation.euler(), o->quaternion);
      o->rotation.setFromRotationMatrix(Matrix4().makeRotationFromQuaternion(Q()));
      P("rot.setFromRotationMatrix", o->rotation.euler(), o->quaternion);
      o->rotation.set(y, z, x, ORDERS[(i + 3) % 6]);
      o->updateMatrix();
      P("rot.set.order", o->rotation.euler(), o->quaternion, o->matrix);
      o->quaternion.slerp(Q(), 0.3);
      o->rotation.x *= 2;
      P("rot.times", o->rotation.euler(), o->quaternion);
    }
    auto other = make<Object3D>();
    other->rotation.copy(o->rotation);
    P("rot.copy.rotation", other->rotation.euler(), other->quaternion);
  }

  // --- a small tree
  const MaterialPtr mat = lambert({.color = 0x808890});
  {
    auto root = make<Scene>();
    auto g1 = make<Group>(), g2 = make<Group>();
    auto m1 = make<Mesh>(grid(3, 2, 4, 2), mat);
    auto m2 = make<Object3D>();
    root->name = "root";
    g1->name = "g1";
    g2->name = "g2";
    m1->name = "m1";
    m2->name = "m2";
    root->add(g1);
    g1->add(g2);
    g2->add(m1);
    g1->add(m2);
    place(*g1, 1.5);
    place(*g2, 0.8);
    place(*m1, 1);
    place(*m2, 2);
    root->updateMatrixWorld();
    P("tree.world", g1->matrix, g1->matrixWorld, g2->matrixWorld, m1->matrixWorld, m2->matrixWorld);
    // a change below: only what is asked for is brought up to date
    g2->rotation.y = 0.7;
    g2->position.x += 1;
    m1->updateWorldMatrix(true, false);
    P("tree.updateWorldMatrix", g2->matrixWorld, m1->matrixWorld, m2->matrixWorld);
    g1->rotateOnAxis(V3(-1, 1).normalize(), 0.5);
    g1->rotateOnWorldAxis(V3(-1, 1).normalize(), -0.3);
    g1->rotateX(0.1).rotateY(0.2).rotateZ(0.3);
    g1->translateOnAxis(V3(-1, 1).normalize(), 2).translateX(1).translateY(2).translateZ(3);
    g1->updateWorldMatrix(false, true);
    P("tree.rotate", g1->position, g1->quaternion, g1->matrixWorld, m1->matrixWorld, m2->matrixWorld);
    {
      Vector3 wp, ws, wd;
      Quaternion wq;
      P("tree.getWorld", m1->getWorldPosition(wp), m1->getWorldQuaternion(wq), m1->getWorldScale(ws), m1->getWorldDirection(wd));
    }
    const Vector3 v = V3();
    {
      Vector3 a = v, b = v;
      P("tree.localToWorld", m1->localToWorld(a), m1->worldToLocal(b));
    }
    // a matrix of its own, a world matrix of its own
    m2->matrixAutoUpdate = false;
    m2->position.set(100, 100, 100);
    m2->matrix.makeRotationX(0.5).setPosition(1, 2, 3);
    root->updateMatrixWorld(true);
    P("tree.ownMatrix", m2->matrix, m2->matrixWorld);
    m2->matrixAutoUpdate = true;
    m2->position.set(1, 2, 3);
    g2->matrixWorldAutoUpdate = false;
    g2->position.y += 5;
    root->updateMatrixWorld(true);
    P("tree.ownWorldMatrix", g2->matrix, g2->matrixWorld, m1->matrixWorld, m2->matrixWorld);
    g2->matrixWorldAutoUpdate = true;
    root->updateMatrixWorld(true);
    // applyMatrix4, applyQuaternion
    m2->applyMatrix4(M4());
    P("tree.applyMatrix4", m2->position, m2->quaternion, m2->scale, m2->matrix);
    m2->applyQuaternion(Q());
    P("tree.applyQuaternion", m2->quaternion, m2->rotation.euler());
    // lookAt: an object looks along +z, a camera and a light along -z
    const Vector3 target = V3();
    m1->lookAt(target);
    P("tree.lookAt", m1->quaternion, m1->rotation.euler());
    m1->lookAt(target.x, target.y + 3, target.z);
    P("tree.lookAt.xyz", m1->quaternion);
    auto cam = perspectiveCamera();
    auto sun = directionalLight(0xffffff, 1);
    cam->name = "cam";
    sun->name = "sun";
    g2->add(cam, sun);
    cam->position.copy(V3());
    sun->position.copy(V3());
    cam->lookAt(target);
    sun->lookAt(target);
    {
      Vector3 d1, d2;
      P("tree.lookAt.camera", cam->quaternion, sun->quaternion, cam->getWorldDirection(d1), sun->getWorldDirection(d2));
    }
    cam->up.set(0, 0, 1);
    cam->lookAt(target);
    P("tree.lookAt.up", cam->quaternion, cam->matrixWorldInverse);
    // attach: to another parent, staying where it is in the world
    root->updateMatrixWorld(true);
    P("tree.beforeAttach", m1->matrixWorld, m2->matrixWorld);
    root->attach(m1);
    P("tree.attach", m1->position, m1->quaternion, m1->scale, m1->matrixWorld, m1->parent == root.get(), g2->children.size(), root->children.size());
    g2->attach(m2);
    P("tree.attach.back", m2->position, m2->quaternion, m2->scale, m2->matrixWorld, m2->parent == g2.get());
    // add, remove, clear
    P("tree.children", names(*root), names(*g1), names(*g2));
    g1->add(m1);   // (it leaves the root)
    P("tree.add", names(*root), names(*g1), m1->parent == g1.get());
    g1->add(g1);   // (itself: nothing happens)
    g1->remove(m2);   // (not a child of g1: nothing happens)
    g1->remove(m1);
    P("tree.remove", names(*g1), m1->parent == nullptr);
    g2->add(m1);
    m1->removeFromParent();
    P("tree.removeFromParent", names(*g2), m1->parent == nullptr);
    g2->add(m1);
    std::string order;
    root->traverse([&](Object3D& o) { order += (order.empty() ? "" : ",") + o.name; });
    P("tree.traverse", order);
    g2->visible = false;
    std::string seen;
    root->traverseVisible([&](Object3D& o) { seen += (seen.empty() ? "" : ",") + o.name; });
    P("tree.traverseVisible", seen);
    P("tree.getObjectByName", root->getObjectByName("m1") == m1.get(), root->getObjectByName("nothing") == nullptr, g2->getObjectByName("g1") == nullptr, m1->getObjectByName("m1") == m1.get());
    g2->clear();
    P("tree.clear", names(*g2), m1->parent == nullptr, cam->parent == nullptr);
  }

  // --- cameras
  for (int i = 0; i < 4; i++) {
    const double fov = r(20, 100), aspect = r(0.5, 2.5), near = r(0.05, 2), far = r(100, 3000);
    auto cam = perspectiveCamera(fov, aspect, near, far);
    P("cam.perspective", cam->projectionMatrix, cam->projectionMatrixInverse);
    cam->zoom = r(0.5, 3);
    cam->updateProjectionMatrix();
    P("cam.zoom", cam->projectionMatrix);
    const double ox = r(0, 900), oy = r(0, 500);
    cam->setViewOffset(1920, 1080, ox, oy, 640, 360);
    P("cam.viewOffset", cam->aspect, cam->projectionMatrix, cam->projectionMatrixInverse);
    cam->clearViewOffset();
    P("cam.clearViewOffset", cam->aspect, cam->projectionMatrix);
    cam->position.copy(V3());
    cam->quaternion.copy(Q());
    cam->updateMatrixWorld();
    {
      Vector3 d;
      P("cam.world", cam->matrixWorld, cam->matrixWorldInverse, cam->getWorldDirection(d));
    }
    const Vector3 p = V3(-30, 30);
    const Vector3 ndc = p.clone().project(*cam);
    {
      Vector3 back = ndc;
      P("cam.project", ndc, unproject(back, *cam));
    }
    // scaled: matrixWorldInverse leaves the scale out
    cam->scale.set(2, 2, 2);
    cam->updateMatrixWorld();
    P("cam.scaled", cam->matrixWorld, cam->matrixWorldInverse);
    cam->scale.set(1, 1, 1);
    // below a parent
    auto rig = make<Group>();
    rig->position.copy(V3());
    rig->quaternion.copy(Q());
    rig->add(cam);
    cam->updateWorldMatrix(true, false);
    P("cam.rig", cam->matrixWorld, cam->matrixWorldInverse);
    cam->lookAt(p);
    P("cam.rig.lookAt", cam->quaternion);
    // orthographic
    const double left = r(-50, -5), right = r(5, 50), top = r(5, 50), bottom = r(-50, -5);
    auto ortho = orthographicCamera(left, right, top, bottom, near, far);
    P("cam.orthographic", ortho->projectionMatrix, ortho->projectionMatrixInverse);
    ortho->zoom = r(0.5, 3);
    ortho->updateProjectionMatrix();
    P("cam.orthographic.zoom", ortho->projectionMatrix);
    ortho->setViewOffset(800, 600, ox / 4, oy / 4, 320, 240);
    P("cam.orthographic.viewOffset", ortho->projectionMatrix, ortho->projectionMatrixInverse);
    ortho->clearViewOffset();
    P("cam.orthographic.clearViewOffset", ortho->projectionMatrix);
    ortho->position.copy(V3());
    ortho->quaternion.copy(Q());
    ortho->updateMatrixWorld();
    {
      Vector3 ndc2 = p;
      project(ndc2, *ortho);
      P("cam.orthographic.project", ndc2, ndc2.clone().unproject(*ortho));
    }
    // rays through a point of the picture
    Raycaster ray;
    const Vector2 at = V2(-1, 1);
    ray.setFromCamera(at, *cam);
    P("cam.ray", ray.ray);
    ray.setFromCamera(at, *ortho);
    P("cam.ray.orthographic", ray.ray);
  }
  P("cam.default", perspectiveCamera()->projectionMatrix, orthographicCamera(-1, 1, 1, -1)->projectionMatrix, make<Camera>()->projectionMatrix);

  // --- lights as three.js makes them
  {
    auto dir = directionalLight(0xfff2d0, 2.5);
    P("light.directional", dir->color, dir->intensity, dir->position, dir->matrix, dir->target->position, dir->shadow.camera->projectionMatrix, dir->shadow.camera->near, dir->shadow.camera->far,
      dir->shadow.mapSize, dir->shadow.bias, dir->shadow.normalBias, dir->shadow.radius, dir->shadow.intensity, dir->shadow.autoUpdate, dir->shadow.needsUpdate, dir->castShadow);
    auto hemi = hemisphereLight(0xc2d3e4, 0x6a5d4a, 1.2);
    P("light.hemisphere", hemi->color, hemi->groundColor, hemi->intensity, hemi->position, hemi->matrix);
    auto amb = ambientLight(0x404040);
    P("light.ambient", amb->color, amb->intensity, amb->position);
    auto point = pointLight(0xffb000, 3, 25, 1.5), point2 = pointLight(0xffffff);
    P("light.point", point->color, point->intensity, point->distance, point->decay, point->position, point->shadow.camera->projectionMatrix, point2->intensity, point2->distance, point2->decay);
    auto spot = spotLight(0xfff0c8, 40, 60, 0.5, 0.3, 1.2), spot2 = spotLight(0xffffff);
    P("light.spot", spot->color, spot->intensity, spot->distance, spot->angle, spot->penumbra, spot->decay, spot->position, spot->matrix, spot->target->position, spot->shadow.camera->projectionMatrix,
      spot2->intensity, spot2->distance, spot2->angle, spot2->penumbra, spot2->decay);
    // before a shadow map is drawn: the shadow camera at the light, looking at its target, and the matrix into the map
    dir->position.copy(V3());
    dir->target->position.copy(V3());
    dir->updateMatrixWorld();
    dir->target->updateMatrixWorld();
    Frustum frustum;
    updateShadowMatrices(*dir, &frustum);
    P("light.shadow.directional", dir->shadow.camera->position, dir->shadow.camera->quaternion, dir->shadow.camera->matrixWorldInverse, dir->shadow.matrix, frustum.planes[0], frustum.planes[5]);
    spot->position.copy(V3());
    spot->target->position.copy(V3());
    spot->shadow.mapSize.set(1024, 512);
    spot->updateMatrixWorld();
    spot->target->updateMatrixWorld();
    updateShadowMatrices(*spot);
    P("light.shadow.spot", spot->shadow.camera->fov, spot->shadow.camera->aspect, spot->shadow.camera->far, spot->shadow.camera->projectionMatrix, spot->shadow.matrix);
    spot2->updateMatrixWorld();
    updateShadowMatrices(*spot2);
    P("light.shadow.spot.endless", spot2->shadow.camera->fov, spot2->shadow.camera->aspect, spot2->shadow.camera->far, spot2->shadow.matrix);
  }

  // --- levels of detail
  {
    auto lod = make<LOD>();
    auto near = make<Group>(), mid = make<Group>(), far = make<Group>();
    auto cam = perspectiveCamera();
    near->name = "near";
    mid->name = "mid";
    far->name = "far";
    lod->addLevel(far, 200, 0.1);
    lod->addLevel(near);
    lod->addLevel(mid, -50, 0.2);   // (a distance below zero counts as its amount)
    {
      std::string levelNames;
      std::vector<double> distances, hystereses;
      for (const LOD::Level& l : lod->levels) {
        levelNames += (levelNames.empty() ? "" : ",") + l.object->name;
        distances.push_back(l.distance);
        hystereses.push_back(l.hysteresis);
      }
      P("lod.levels", levelNames, distances, hystereses, lod->children.size(), lod->getCurrentLevel());
    }
    lod->position.set(10, 0, -5);
    lod->updateMatrixWorld();
    for (int d : {0, 30, 45, 55, 45, 41, 39, 150, 185, 199, 201, 185, 181, 179, 60, 20, 500, 0}) {
      cam->position.set(10 + d, 0, -5);
      cam->updateMatrixWorld();
      lod->update(*cam);
      P("lod.update", d, lod->getCurrentLevel(), near->visible, mid->visible, far->visible);
    }
    cam->zoom = 4;
    cam->position.set(10 + 300, 0, -5);
    cam->updateMatrixWorld();
    lod->update(*cam);
    P("lod.zoom", lod->getCurrentLevel(), near->visible, mid->visible, far->visible);
    // one level: there is nothing to choose
    auto single = make<LOD>();
    auto only = make<Group>();
    only->visible = false;
    single->addLevel(only);
    single->update(*cam);
    P("lod.single", single->getCurrentLevel(), only->visible);
  }

  // --- attributes
  {
    attrKind<uint8_t>("Uint8");
    attrKind<int8_t>("Int8");
    attrKind<uint16_t>("Uint16");
    attrKind<int16_t>("Int16");
    attrKind<uint32_t>("Uint32");
    attrKind<int32_t>("Int32");
    attrKind<float>("Float32");
    // clone, and the transforms
    auto pos = attr(std::vector<float>(12), 3);
    for (size_t i = 0; i < 4; i++) {
      const Vector3 p = V3();
      pos->setXYZ(i, p.x, p.y, p.z);
    }
    const Matrix4 m = M4();
    const Matrix3 m3 = M3any();
    auto c = pos->clone();
    c->applyMatrix4(m);
    A("attr.applyMatrix4", c);
    A("attr.clone", pos);
    A("attr.applyNormalMatrix", pos->clone()->applyNormalMatrix(Matrix3().getNormalMatrix(m)));
    if (all(pos->clone()->transformNormals(m)) != all(pos->clone()->applyNormalMatrix(Matrix3().getNormalMatrix(m)))) std::abort();
    A("attr.transformDirection", pos->clone()->transformDirection(m));
    A("attr.applyMatrix3", pos->clone()->applyMatrix3(m3));
    auto uv = attr(std::vector<float>{0, 0, 1, 0, 1, 1, 0.25, 0.75}, 2);
    A("attr.applyMatrix3.uv", uv->applyMatrix3(m3));
    auto inst = attr(std::vector<float>(6), 3);
    inst->divisor = 1;
    auto ic = inst->clone();
    P("attr.instanced", ic->divisor > 0, ic->divisor, ic->count());
  }

  // --- geometries
  {
    const GeometryPtr g = grid(4, 3, 6, 4);
    G("geo.grid", g);
    g->computeVertexNormals();
    g->computeBoundingBox();
    g->computeBoundingSphere();
    G("geo.normals", g);
    // 16 bits where they fit
    A("geo.index16", make<BufferGeometry>()->setIndex(std::vector<uint32_t>{0, 1, 65534}).index);
    A("geo.index32", make<BufferGeometry>()->setIndex(std::vector<uint32_t>{0, 65535, 2}).index);
    // transforms: the bounds follow, the normals turn with the inverse transpose
    const Vector3 mp = V3();
    const Quaternion mq = Q();
    const Matrix4 m = Matrix4().compose(mp, mq, Vector3(1.5, 0.5, 2));
    G("geo.applyMatrix4", g->clone()->applyMatrix4(m));
    G("geo.rotate", g->clone()->rotateX(0.3).rotateY(-0.7).rotateZ(1.1));
    G("geo.translate", g->clone()->translate(1, 2, 3).scale(2, 3, 4));
    G("geo.applyQuaternion", g->clone()->applyQuaternion(Q()));
    G("geo.center", g->clone()->translate(5, 6, 7).center());
    // without an index
    const GeometryPtr n = g->toNonIndexed();
    G("geo.toNonIndexed", n);
    n->computeVertexNormals();
    G("geo.toNonIndexed.normals", n);
    P("geo.toNonIndexed.again", n->toNonIndexed() == n);
    // groups, a draw range, colours as bytes: all of it is copied
    const GeometryPtr c = g->clone();
    auto col = attr(std::vector<uint8_t>(c->position()->count() * 4), 4, true);
    for (size_t i = 0; i < col->count(); i++) {
      const double cr = rnd_(), cg = rnd_(), cb = rnd_();
      col->setXYZW(i, cr, cg, cb, 1);
    }
    c->setAttribute("color", col);
    c->addGroup(0, 30, 0);
    c->addGroup(30, 42, 1);
    c->setDrawRange(6, 36);
    c->name = "copy me";
    const GeometryPtr c2 = c->clone();
    G("geo.clone", c2);
    P("geo.clone.name", c2->name, c2->index != c->index, c2->position() != c->position());
    G("geo.clone.toNonIndexed", c->toNonIndexed());
    c2->clearGroups();
    c2->deleteAttribute("uv");
    c2->setAttribute("uv", c->uv());
    c2->setAttribute("position", c->position());
    G("geo.attributes", c2);
    // triangles without an index
    const GeometryPtr s = soup(6);
    s->computeVertexNormals();
    s->computeBoundingSphere();
    G("geo.soup", s);
    // from points
    std::vector<Vector3> pts;
    for (int i = 0; i < 4; i++) pts.push_back(V3());
    auto fp = make<BufferGeometry>();
    fp->setFromPoints(pts);
    G("geo.setFromPoints", fp);
    std::vector<Vector3> two;
    for (int i = 0; i < 2; i++) two.push_back(V3());
    fp->setFromPoints(two);
    G("geo.setFromPoints.again", fp);
    std::vector<Vector3> six;
    for (int i = 0; i < 6; i++) six.push_back(V3());
    fp->setFromPoints(six);
    G("geo.setFromPoints.more", fp);
    // nothing in it
    auto e = make<BufferGeometry>();
    e->computeBoundingBox();
    e->computeBoundingSphere();
    e->computeVertexNormals();
    G("geo.empty", e);
  }

  // --- instances, the box of a tree, what a frustum holds
  {
    auto im = make<InstancedMesh>(grid(2, 2, 1, 1), mat, 5);
    A("inst.matrix", im->instanceMatrix);
    A("inst.color", im->instanceColor);
    for (int i = 0; i < 5; i++) im->setMatrixAt(i, M4());
    Matrix4 back;
    im->getMatrixAt(3, back);
    P("inst.getMatrixAt", back);
    im->setColorAt(2, Color(0x3a74c6));
    A("inst.instanceColor", im->instanceColor);
    Color cc, c0(0, 0, 0);
    im->getColorAt(2, cc);
    im->getColorAt(0, c0);
    P("inst.getColorAt", cc, c0, im->instanceColor->divisor, im->instanceMatrix->divisor);
    im->computeBoundingSphere();
    P("inst.boundingSphere", im->boundingSphere);
    im->count = 3;
    im->computeBoundingSphere();
    P("inst.boundingSphere.count", im->boundingSphere, im->instanceMatrix->count());
    // the box of a tree: of the geometries' boxes, or of every vertex
    auto root = make<Group>();
    auto mesh = make<Mesh>(grid(3, 3, 2, 2), mat);
    auto child = make<Mesh>(soup(4), mat);
    root->add(mesh, im);
    mesh->add(child);
    for (Object3D* o : {static_cast<Object3D*>(root.get()), static_cast<Object3D*>(mesh.get()), static_cast<Object3D*>(child.get()), static_cast<Object3D*>(im.get())}) {
      o->position.copy(V3());
      o->quaternion.copy(Q());
      o->scale.setScalar(r(0.5, 2));
    }
    root->updateMatrixWorld(true);
    {
      auto empty = make<Group>();
      P("box.setFromObject", Box3().setFromObject(*root), Box3().setFromObject(*root, true), Box3().setFromObject(*child), Box3().setFromObject(*empty));
    }
    // what a frustum holds
    auto cam = perspectiveCamera(60, 1.5, 0.1, 100);
    cam->position.copy(V3());
    cam->lookAt(0, 0, 0);
    cam->updateMatrixWorld();
    const Frustum f = Frustum().setFromProjectionMatrix(Matrix4().multiplyMatrices(cam->projectionMatrix, cam->matrixWorldInverse));
    std::vector<bool> inside;
    for (int i = 0; i < 30; i++) {
      mesh->position.copy(V3(-30, 30));
      root->updateMatrixWorld(true);
      inside.push_back(f.intersectsObject(*mesh));
      inside.push_back(f.intersectsObject(*im));
      inside.push_back(f.intersectsObject(*child));
    }
    P("frustum.intersectsObject", inside);
    auto sprite = make<Sprite>(spriteMaterial());
    sprite->center.set(0.5, 0);
    sprite->scale.set(3, 2, 1);
    std::vector<bool> spriteInside;
    for (int i = 0; i < 20; i++) {
      sprite->position.copy(V3(-30, 30));
      sprite->updateMatrixWorld();
      spriteInside.push_back(f.intersectsSprite(*sprite));
    }
    P("frustum.intersectsSprite", spriteInside);
    P("box.sprite", Box3().setFromObject(*sprite), Box3().setFromObject(*sprite, true));
  }

  // --- raycasts
  {
    auto mesh = make<Mesh>(grid(6, 5, 8, 6), basic());
    mesh->name = "grid";
    mesh->position.copy(V3());
    mesh->quaternion.copy(Q());
    mesh->scale.set(1.2, 0.8, 1.5);
    mesh->updateMatrixWorld();
    const Side sides[3] = {Side::Front, Side::Back, Side::Double};
    size_t total = 0;
    for (int i = 0; i < 45; i++) {
      const double x = r(-5, 5), y = r(-4, 4);
      const Raycaster ray = rayAt(*mesh, x, y, 0);
      mesh->material->side = sides[i % 3];
      const std::vector<Intersection> hits = ray.intersectObject(*mesh);
      total += hits.size();
      if (i < 12) hitsOf("ray.grid", hits);
    }
    P("ray.grid.total", total);
    // near and far
    mesh->material->side = Side::Double;
    {
      Vector3 at(0.5, 0.5, 0);
      mesh->localToWorld(at);
      const Vector3 from = at.clone().add(Vector3(3, 4, 5)), dir = at.clone().sub(from).normalize();
      const double ranges[6][2] = {{0, 1e300}, {0, 5}, {8, 20}, {6.5, 7.5}, {0, 6.9}, {7.3, 1e300}};
      for (const auto& range : ranges) hitsOf("ray.nearFar", Raycaster(from, dir, range[0], range[1]).intersectObject(*mesh));
    }
    // a bounding box, when the geometry has one, is asked first
    mesh->geometry->computeBoundingBox();
    total = 0;
    for (int i = 0; i < 20; i++) {
      const double x = r(-7, 7), y = r(-6, 6);
      const std::vector<Intersection> hits = rayAt(*mesh, x, y, 0).intersectObject(*mesh);
      total += hits.size();
      if (i < 4) hitsOf("ray.box", hits);
    }
    P("ray.box.total", total);
    // the draw range
    mesh->geometry->setDrawRange(30, 60);
    total = 0;
    for (int i = 0; i < 30; i++) {
      const double x = r(-4, 4), y = r(-3, 3);
      const std::vector<Intersection> hits = rayAt(*mesh, x, y, 0).intersectObject(*mesh);
      total += hits.size();
      if (i < 4) hitsOf("ray.drawRange", hits);
    }
    P("ray.drawRange.total", total);
    mesh->geometry->setDrawRange(0, INT_MAX);
    // groups, each with its own material: the front of one, the back of the other
    mesh->geometry->addGroup(0, 90, 0);
    mesh->geometry->addGroup(90, 90, 1);
    mesh->material = nullptr;
    mesh->materials = {basic({.side = Side::Front}), basic({.side = Side::Back})};
    total = 0;
    for (int i = 0; i < 40; i++) {
      const double x = r(-4, 4), y = r(-3, 3);
      const std::vector<Intersection> hits = rayAt(*mesh, x, y, 0).intersectObject(*mesh);
      total += hits.size();
      if (i < 6) hitsOf("ray.groups", hits);
    }
    P("ray.groups.total", total);
    mesh->geometry->setDrawRange(60, 60);
    total = 0;
    for (int i = 0; i < 30; i++) {
      const double x = r(-4, 4), y = r(-3, 3);
      total += rayAt(*mesh, x, y, 0).intersectObject(*mesh).size();
    }
    P("ray.groups.drawRange.total", total);
    mesh->geometry->setDrawRange(0, INT_MAX);
    // without an index
    auto soupMesh = make<Mesh>(soup(60, 5), basic({.side = Side::Double}));
    soupMesh->name = "soup";
    soupMesh->position.copy(V3());
    soupMesh->quaternion.copy(Q());
    soupMesh->updateMatrixWorld();
    total = 0;
    for (int i = 0; i < 40; i++) {
      const double x = r(-5, 5), y = r(-5, 5), z = r(-5, 5);
      const std::vector<Intersection> hits = rayAt(*soupMesh, x, y, z).intersectObject(*soupMesh);
      total += hits.size();
      if (i < 6) hitsOf("ray.soup", hits);
    }
    P("ray.soup.total", total);
    // instances
    auto im = make<InstancedMesh>(grid(2, 2, 2, 2), basic({.side = Side::Double}), 6);
    im->name = "instances";
    std::vector<Vector3> centres;
    for (int i = 0; i < 6; i++) {
      const Vector3 p = V3(-8, 8);
      const Quaternion q = Q();
      centres.push_back(p);
      im->setMatrixAt(i, Matrix4().compose(p, q, Vector3(1, 1, 1)));
    }
    im->position.copy(V3());
    im->updateMatrixWorld();
    total = 0;
    for (int i = 0; i < 30; i++) {
      const Vector3& c = centres[static_cast<size_t>(i % 6)];
      const std::vector<Intersection> hits = rayAt(*im, c.x, c.y, c.z).intersectObject(*im);
      total += hits.size();
      if (i < 8) hitsOf("ray.instances", hits);
    }
    P("ray.instances.total", total);
    // a tree: all of it, or only the object asked; a layer the raycaster does not see
    auto group = make<Group>();
    group->name = "group";
    group->add(mesh, soupMesh, im);
    group->updateMatrixWorld(true);
    for (int i = 0; i < 6; i++) {
      const double x = r(-3, 3), y = r(-3, 3);
      const Raycaster ray = rayAt(*mesh, x, y, 0);
      hitsOf("ray.tree", ray.intersectObject(*group));
      hitsOf("ray.tree.notRecursive", ray.intersectObject(*group, false));
      hitsOf("ray.tree.objects", ray.intersectObjects({im, mesh, soupMesh}, false));
      mesh->layersSet(2);
      hitsOf("ray.tree.layer", ray.intersectObject(*group));
      mesh->layersSet(0);
    }
    // levels of detail: the level for the distance is asked (and, when all below is asked, every level once more)
    auto lod = make<LOD>();
    auto fine = make<Mesh>(grid(4, 4, 4, 4), basic({.side = Side::Double}));
    auto coarse = make<Mesh>(grid(1, 1, 4, 4), basic({.side = Side::Double}));
    fine->name = "fine";
    coarse->name = "coarse";
    lod->addLevel(fine, 0);
    lod->addLevel(coarse, 50);
    lod->updateMatrixWorld(true);
    for (double z : {10.0, 80.0}) {
      const Raycaster ray(Vector3(0.3, 0.4, z), Vector3(0, 0, -1));
      hitsOf("ray.lod", ray.intersectObject(*lod, false));
      hitsOf("ray.lod.recursive", ray.intersectObject(*lod, true));
    }
  }

  // --- materials and textures as three.js makes them
  {
    const MaterialPtr basicM = basic(), lambertM = lambert(), phongM = phong(), line = lineBasic(), sprite = spriteMaterial(), points = pointsMaterial(), shader = shaderMaterial("", "");
    info("mat.basic", *basicM);
    info("mat.lambert", *lambertM);
    P("mat.lambert.more", lambertM->emissive, lambertM->emissiveIntensity, lambertM->flatShading);
    info("mat.phong", *phongM);
    P("mat.phong.more", phongM->emissive, phongM->emissiveIntensity, phongM->flatShading, phongM->specular, phongM->shininess);
    info("mat.line", *line);
    info("mat.sprite", *sprite);
    P("mat.sprite.more", sprite->rotation, sprite->sizeAttenuation);
    info("mat.points", *points);
    P("mat.points.more", points->size, points->sizeAttenuation);
    info("mat.shader", *shader);
    const MaterialPtr full = phong({.color = 0x3a74c6, .vertexColors = true, .transparent = true, .opacity = 0.4, .side = Side::Double, .depthWrite = false, .depthTest = false, .fog = false,
                                    .toneMapped = false, .flatShading = true, .alphaTest = 0.3, .blending = Blending::Additive, .polygonOffset = true, .polygonOffsetFactor = -2,
                                    .polygonOffsetUnits = -4, .emissive = 0x1aff6a, .emissiveIntensity = 0.5, .specular = 0xfff2d0, .shininess = 80, .colorWrite = false, .name = "full"});
    info("mat.params", *full);
    P("mat.params.more", full->emissive, full->emissiveIntensity, full->flatShading, full->specular, full->shininess);
    const MaterialPtr copy = full->clone();
    full->color.setHex(0);
    info("mat.clone", *copy);
    P("mat.clone.more", copy->emissive, copy->emissiveIntensity, copy->flatShading, copy->specular, copy->shininess);
    info("mat.sprite.params", *spriteMaterial({.color = 0xff2a1a, .transparent = false, .side = Side::Back, .blending = Blending::Multiply}));
    info("mat.shader.params", *shaderMaterial("", "", {}, {.transparent = true, .fog = true}));
    info("mat.sprite.clone", *sprite->clone());
    info("mat.shader.clone", *shader->clone());
    const MaterialPtr sm = shaderMaterial("v", "f", {{"a", uniform(1.5)}, {"c", uniform(Color(0x808890))}, {"v", uniform(Vector3(1, 2, 3))}});
    sm->defines = {{"SNOW", ""}};
    const MaterialPtr sc = sm->clone();
    sc->uniforms["a"]->value = 2.0;
    std::get<Color>(sc->uniforms["c"]->value).setHex(0xff0000);
    std::get<Vector3>(sc->uniforms["v"]->value).x = 9;
    std::string defines;
    for (const auto& d : sc->defines) defines += (defines.empty() ? "" : ",") + d.first;
    P("mat.shader.uniforms", std::get<double>(sm->uniforms["a"]->value), std::get<Color>(sm->uniforms["c"]->value), std::get<Vector3>(sm->uniforms["v"]->value),
      std::get<double>(sc->uniforms["a"]->value), std::get<Color>(sc->uniforms["c"]->value), std::get<Vector3>(sc->uniforms["v"]->value), sc->vertexShader, sc->fragmentShader, defines);
    tex("tex.plain", *make<Texture>());
    tex("tex.canvas", *canvasTexture(Canvas()));
    const TexturePtr dt = dataTexture(std::vector<uint8_t>(16), 2, 2);
    tex("tex.data", *dt);
    P("tex.data.size", dt->width, dt->height, dt->data->size());
    auto rt = make<RenderTarget>(320, 200);
    tex("tex.target", *rt->texture);
    P("tex.target.size", rt->width, rt->height, rt->texture->width, rt->texture->height, rt->depthBuffer, rt->samples, rt->texture->isRenderTarget);
    rt->setSize(64, 48);
    P("tex.target.setSize", rt->width, rt->height, rt->texture->width, rt->texture->height, make<RenderTarget>(8, 8, false)->depthBuffer);
  }
}

// ---------------------------------------------------------------------------
// geometries, curves, shapes
// ---------------------------------------------------------------------------

/** a geometry with its bounding sphere */
static void GS(const std::string& label, const GeometryPtr& g) {
  g->computeBoundingSphere();
  G(label, *g);
}
/** numbers in a row: how many, their sum, the sum of each times its place, and the first of them */
static void row(const std::string& label, const std::vector<double>& a, size_t first = 24) {
  double sum = 0, w = 0;
  for (size_t i = 0; i < a.size(); i++) {
    sum += a[i];
    w += a[i] * static_cast<double>(i + 1);
  }
  P(label, a.size(), sum, w, std::vector<double>(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(std::min(first, a.size()))));
}
static std::vector<double> flat2(const std::vector<Vector2>& pts) {
  std::vector<double> a;
  for (const Vector2& p : pts) {
    a.push_back(p.x);
    a.push_back(p.y);
  }
  return a;
}
static std::vector<double> flat3(const std::vector<Vector3>& pts) {
  std::vector<double> a;
  for (const Vector3& p : pts) {
    a.push_back(p.x);
    a.push_back(p.y);
    a.push_back(p.z);
  }
  return a;
}
static void faces(const std::string& label, const std::vector<std::array<int, 3>>& f) {
  std::vector<double> a;
  for (const std::array<int, 3>& t : f)
    for (int i : t) a.push_back(i);
  row(label, a, 60);
}
static void outline(const Shape& s) {
  const ShapePoints e = s.extractPoints(6);
  row("shape.points", flat2(e.shape), 16);
  for (const std::vector<Vector2>& h : e.holes) row("shape.hole", flat2(h), 16);
}

static void geomSection() {
  seed(424242);

  // --- the solids
  GS("box", boxGeometry());
  GS("box.2", boxGeometry(2, 3, 4));
  GS("box.3", boxGeometry(1.5, 0.25, 3, 2, 3, 4));
  GS("box.4", boxGeometry(0.13, 0.26, 0.1));
  GS("plane", planeGeometry());
  GS("plane.2", planeGeometry(4, 3));
  GS("plane.3", planeGeometry(10, 5, 4, 3));
  GS("cylinder", cylinderGeometry());
  GS("cylinder.2", cylinderGeometry(0.5, 0.5, 2, 8));
  GS("cylinder.3", cylinderGeometry(0.3, 0.6, 1.5, 12, 3, true));
  GS("cylinder.4", cylinderGeometry(0, 1, 2, 6, 2));
  GS("cylinder.5", cylinderGeometry(1, 0, 2, 6, 2));
  GS("cylinder.6", cylinderGeometry(0.5, 0.5, 1, 8, 1, false, 0.3, 2.2));
  GS("cylinder.7", cylinderGeometry(0.045, 0.05, 2.75, 8, 1, true));
  GS("cone", coneGeometry());
  GS("cone.2", coneGeometry(0.5, 1.2, 10));
  GS("cone.3", coneGeometry(1, 2, 8, 3, true, 0.5, 4));
  GS("sphere", sphereGeometry());
  GS("sphere.2", sphereGeometry(0.5, 8, 6));
  GS("sphere.3", sphereGeometry(1, 20, 8, PI * 1.5 - 1.1, 2.2, 1.12, 0.62));
  GS("sphere.4", sphereGeometry(1, 12, 6, 0, PI * 2, 0, PI / 2));
  GS("sphere.5", sphereGeometry(2, 5, 4, 1, 3, 0.5, 2));
  GS("sphere.6", sphereGeometry(1, 6, 10, 0.3, 1, 0.02, 1.75));
  GS("sphere.7", sphereGeometry(1, 2, 1));
  GS("torus", torusGeometry());
  GS("torus.2", torusGeometry(0.3, 0.02, 6, 24));
  GS("torus.3", torusGeometry(1, 0.4, 8, 16, PI));
  GS("torus.4", torusGeometry(1, 0.3, 6, 12, PI * 2, 0.5, 2));
  GS("circle", circleGeometry());
  GS("circle.2", circleGeometry(0.5, 12));
  GS("circle.3", circleGeometry(1, 16, 0.3, 2.5));
  GS("circle.4", circleGeometry(1, 2));
  GS("ring", ringGeometry());
  GS("ring.2", ringGeometry(0.2, 0.5, 12, 2));
  GS("ring.3", ringGeometry(1, 2, 8, 3, 0.4, 3));
  GS("ring.4", ringGeometry(1, 2, 2, 0));
  const std::vector<Vector2> profile = {Vector2(0.2, -0.11), Vector2(0.27, -0.12), Vector2(0.32, -0.09), Vector2(0.34, 0), Vector2(0.32, 0.09), Vector2(0.27, 0.12), Vector2(0.2, 0.11)};
  GS("lathe", latheGeometry({Vector2(0, -0.5), Vector2(0.5, 0), Vector2(0, 0.5)}));
  GS("lathe.2", latheGeometry(profile, 22));
  GS("lathe.3", latheGeometry(profile, 10, 0.5, 4));
  GS("lathe.4", latheGeometry({Vector2(0.5, 0), Vector2(0.25, 1)}, 5, 0, 9));
  GS("icosahedron", icosahedronGeometry());
  GS("icosahedron.2", icosahedronGeometry(0.5, 1));
  GS("icosahedron.3", icosahedronGeometry(1, 2));
  GS("icosahedron.4", icosahedronGeometry(2, 3));
  const std::vector<double> tetra = {1, 1, 1, -1, -1, 1, -1, 1, -1, 1, -1, -1};
  const std::vector<int> tetraFaces = {2, 1, 0, 0, 3, 2, 1, 3, 0, 2, 3, 1};
  GS("polyhedron", polyhedronGeometry(tetra, tetraFaces));
  GS("polyhedron.2", polyhedronGeometry(tetra, tetraFaces, 1.5, 2));
  // … as the game makes them: turned, moved, merged, without an index, with normals of their own
  {
    const GeometryPtr a = cylinderGeometry(0.03, 0.03, 0.02, 12);
    a->rotateX(PI / 2).translate(0, 1.2, -0.09);
    GS("game.cylinder", a);
    const GeometryPtr b = sphereGeometry(0.235, 16, 4, 0, PI * 2, 0, PI / 2);
    b->scale(1, 0.32, 1).translate(0, 0.87, 0);
    GS("game.sphere", b);
    const GeometryPtr c = latheGeometry(profile, 22);
    c->rotateZ(-PI / 2);
    GS("game.lathe", c);
    const GeometryPtr d = boxGeometry(0.13, 0.26, 0.1);
    d->translate(0, 1.4, -0.09);
    GS("game.box", d->toNonIndexed());
  }
  {
    const GeometryPtr g = boxGeometry(1, 2, 3, 2, 1, 1)->toNonIndexed();
    g->deleteAttribute("normal");
    g->computeVertexNormals();
    GS("box.flat", g);
    const GeometryPtr smooth = sphereGeometry(1, 8, 6);
    smooth->deleteAttribute("normal");
    smooth->computeVertexNormals();
    GS("sphere.computeVertexNormals", smooth);
  }

  // --- curves
  std::vector<Vector3> pts3;
  for (int i = 0; i < 6; i++) pts3.push_back(V3(-3, 3));
  const std::vector<std::pair<std::string, std::shared_ptr<Curve<Vector3>>>> curves3 = {
      {"line3", std::make_shared<LineCurve3>(pts3[0], pts3[1])},
      {"quadratic3", std::make_shared<QuadraticBezierCurve3>(pts3[0], pts3[1], pts3[2])},
      {"cubic3", std::make_shared<CubicBezierCurve3>(pts3[0], pts3[1], pts3[2], pts3[3])},
      {"catmull", std::make_shared<CatmullRomCurve3>(pts3)},
      {"catmull.closed", std::make_shared<CatmullRomCurve3>(pts3, true)},
      {"catmull.chordal", std::make_shared<CatmullRomCurve3>(pts3, false, "chordal")},
      {"catmull.uniform", std::make_shared<CatmullRomCurve3>(pts3, true, "catmullrom", 0.7)},
      {"catmull.two", std::make_shared<CatmullRomCurve3>(std::vector<Vector3>{pts3[4], pts3[5]}, false, CatmullRomCurve3::Centripetal)},
  };
  for (const auto& [name, c] : curves3) {
    P("curve." + name, c->getPoint(0), c->getPoint(0.37), c->getPoint(1), c->getPointAt(0.37), c->getTangent(0.37), c->getTangentAt(0.37), c->getTangent(0), c->getTangentAt(1), c->getLength());
    row("curve." + name + ".points", flat3(c->getPoints(7)));
    row("curve." + name + ".spaced", flat3(c->getSpacedPoints(7)));
    row("curve." + name + ".lengths", c->getLengths(10), 11);
    P("curve." + name + ".mapping", c->getUtoTmapping(0.25), c->getUtoTmapping(0, c->getLength() * 0.6), c->getUtoTmapping(0), c->getUtoTmapping(1));
    for (bool closed : {false, true}) {
      const FrenetFrames f = c->computeFrenetFrames(9, closed);
      std::vector<double> all3 = flat3(f.tangents);
      const std::vector<double> n3 = flat3(f.normals), b3 = flat3(f.binormals);
      all3.insert(all3.end(), n3.begin(), n3.end());
      all3.insert(all3.end(), b3.begin(), b3.end());
      row("curve." + name + ".frenet", all3, 9);
    }
  }
  {
    std::vector<double> a = flat3(CatmullRomCurve3(pts3).getPoints());
    const std::vector<double> b = flat3(CatmullRomCurve3(pts3).getSpacedPoints());
    a.insert(a.end(), b.begin(), b.end());
    row("curve.default", a, 6);
  }
  std::vector<Vector2> pts2;
  for (int i = 0; i < 5; i++) pts2.push_back(V2(-3, 3));
  const std::vector<std::pair<std::string, std::shared_ptr<Curve<Vector2>>>> curves2 = {
      {"line", std::make_shared<LineCurve>(pts2[0], pts2[1])},
      {"quadratic", std::make_shared<QuadraticBezierCurve>(pts2[0], pts2[1], pts2[2])},
      {"cubic", std::make_shared<CubicBezierCurve>(pts2[0], pts2[1], pts2[2], pts2[3])},
      {"spline", std::make_shared<SplineCurve>(pts2)},
      {"ellipse", std::make_shared<EllipseCurve>()},
      {"ellipse.arc", std::make_shared<EllipseCurve>(1, 2, 3, 1.5, 0.4, 2.9, false, 0.6)},
      {"ellipse.clockwise", std::make_shared<EllipseCurve>(1, 2, 3, 1.5, 0.4, 2.9, true)},
      {"ellipse.back", std::make_shared<EllipseCurve>(0, 0, 2, 2, 3, 1, false)},
      {"ellipse.full", std::make_shared<EllipseCurve>(0, 0, 2, 2, 1, 1 + PI * 2, true)},
      {"ellipse.none", std::make_shared<EllipseCurve>(0, 0, 2, 2, 1, 1, false)},
  };
  for (const auto& [name, c] : curves2) {
    P("curve." + name, c->getPoint(0), c->getPoint(0.37), c->getPoint(1), c->getPointAt(0.37), c->getTangent(0.37), c->getTangentAt(0.37), c->getLength());
    row("curve." + name + ".points", flat2(c->getPoints(7)));
    row("curve." + name + ".spaced", flat2(c->getSpacedPoints(7)));
  }

  // --- paths and shapes
  Path path;
  path.moveTo(0, 0).lineTo(4, 0).quadraticCurveTo(5, 0, 5, 1).lineTo(5, 3).bezierCurveTo(5, 4, 4, 5, 3, 4.5).absarc(2, 4, 1, 0.4, 2.6, false).lineTo(0, 3).absellipse(0, 2, 0.5, 1, PI / 2, -PI / 2, true, 0.2);
  P("path", path.curves.size(), path.currentPoint, path.getLength(), path.getCurveLengths(), path.getPoint(0), path.getPoint(0.5), path.getPoint(1), path.getPointAt(0.3), path.getTangent(0.3));
  row("path.points", flat2(path.getPoints()), 16);
  row("path.points.4", flat2(path.getPoints(4)), 16);
  row("path.spaced", flat2(path.getSpacedPoints()), 16);
  row("path.spaced.9", flat2(path.getSpacedPoints(9)), 16);
  path.autoClose = true;
  {
    std::vector<double> a = flat2(path.getPoints(3));
    const std::vector<double> b = flat2(path.getSpacedPoints(5));
    a.insert(a.end(), b.begin(), b.end());
    row("path.autoClose", a, 8);
  }
  path.autoClose = false;
  path.closePath();
  P("path.closePath", path.curves.size(), path.getLength(), path.currentPoint);
  path.closePath();
  P("path.closePath.again", path.curves.size());
  Path rel;
  rel.moveTo(1, 1).arc(1, 0, 1, PI, 0, true).ellipse(1, 0, 1, 0.5, PI, 0, false, 0).splineThru({Vector2(5, 2), Vector2(6, 0), Vector2(7, 1)});
  P("path.arc", rel.curves.size(), rel.currentPoint, rel.getLength());
  row("path.arc.points", flat2(rel.getPoints(5)), 16);
  const Path fromPoints(pts2);
  P("path.fromPoints", fromPoints.curves.size(), fromPoints.currentPoint, fromPoints.getLength());
  Path copy = fromPoints;
  copy.lineTo(9, 9);
  P("path.clone", copy.curves.size(), fromPoints.curves.size(), copy.curves[0] != fromPoints.curves[0], copy.getLength());

  Shape triangle({Vector2(-0.3, 0), Vector2(0, 0.52), Vector2(0.3, 0)});
  triangle.holes.push_back(make<Path>(std::vector<Vector2>{Vector2(-0.15, 0.05), Vector2(0, 0.31), Vector2(0.15, 0.05)}));
  const Shape rect({Vector2(0, 0), Vector2(2, 0), Vector2(2, 1), Vector2(0, 1)});
  const Shape rectCw({Vector2(0, 0), Vector2(0, 1), Vector2(2, 1), Vector2(2, 0), Vector2(0, 0)});
  Shape rounded;
  rounded.moveTo(0.2, 0).lineTo(1.8, 0).quadraticCurveTo(2, 0, 2, 0.2).lineTo(2, 0.8).quadraticCurveTo(2, 1, 1.8, 1).lineTo(0.2, 1).quadraticCurveTo(0, 1, 0, 0.8).lineTo(0, 0.2).quadraticCurveTo(0, 0, 0.2, 0);
  Shape disc;
  disc.absarc(0, 0, 1, 0, PI * 2, false);
  auto hole1 = make<Path>();
  hole1->absarc(0.3, 0.2, 0.25, 0, PI * 2, true);
  auto hole2 = make<Path>();
  hole2->moveTo(-0.6, -0.2).lineTo(-0.2, -0.2).lineTo(-0.2, 0.2).lineTo(-0.6, 0.2).closePath();
  disc.holes.push_back(hole1);
  disc.holes.push_back(hole2);
  Shape blob;
  blob.moveTo(0, 0).bezierCurveTo(1, -1, 3, -1, 4, 0).bezierCurveTo(5, 1, 5, 3, 4, 4).lineTo(2, 3).lineTo(0, 4).bezierCurveTo(-1, 3, -1, 1, 0, 0);
  // a long profile: so many points that the triangulation sorts them along a curve
  Shape longShape;
  longShape.moveTo(0, 0);
  for (int i = 1; i <= 60; i++) longShape.lineTo(i * 0.5, std::sin(i * 0.7) * 0.4 + (i % 2) * 0.3);
  longShape.lineTo(30, 3);
  for (int i = 60; i >= 0; i--) longShape.lineTo(i * 0.5, 3 + std::cos(i * 0.4) * 0.5 + (i % 3) * 0.2);
  auto longHole = make<Path>();
  longHole->absellipse(15, 1.6, 6, 0.3, 0, PI * 2, true, 0);
  longShape.holes.push_back(longHole);
  const std::vector<std::pair<std::string, const Shape*>> shapes = {{"triangle", &triangle}, {"rect", &rect}, {"rectCw", &rectCw}, {"rounded", &rounded}, {"disc", &disc}, {"blob", &blob}, {"long", &longShape}};
  for (const auto& [name, sp] : shapes) {
    const Shape& s = *sp;
    P("shape." + name, s.curves.size(), s.holes.size(), s.getLength(), ShapeUtils::area(s.getPoints()), ShapeUtils::isClockWise(s.getPoints()));
    outline(s);
    GS("shape." + name + ".geometry", shapeGeometry(s));
    GS("shape." + name + ".geometry.4", shapeGeometry(s, 4));
    GS("shape." + name + ".extrude", extrudeGeometry(s, {.depth = 0.6, .bevelEnabled = false}));
    GS("shape." + name + ".extrude.bevel", extrudeGeometry(s));
  }
  GS("shape.several", shapeGeometry(std::vector<Shape>{triangle, rect, disc}, 5));
  {
    const GeometryPtr g = shapeGeometry(rounded);
    g->rotateX(PI / 2);
    GS("shape.rotated", g);
  }
  GS("extrude.several", extrudeGeometry(std::vector<Shape>{triangle, rect}, {.curveSegments = 3, .steps = 3, .depth = 2, .bevelEnabled = false}));
  GS("extrude.steps", extrudeGeometry(rounded, {.curveSegments = 5, .steps = 4, .depth = 1.5, .bevelEnabled = false}));
  GS("extrude.bevel", extrudeGeometry(disc, {.curveSegments = 8, .depth = 0.4, .bevelEnabled = true, .bevelThickness = 0.05, .bevelSize = 0.04, .bevelOffset = -0.01, .bevelSegments = 2}));
  GS("extrude.bevel.default", extrudeGeometry(rect, {.bevelThickness = 0.3}));
  {
    const CatmullRomCurve3 open(pts3), closed(pts3, true);
    const LineCurve3 line(pts3[0], pts3[1]);
    GS("extrude.path", extrudeGeometry(rect, {.steps = 12, .extrudePath = &open}));
    GS("extrude.path.closed", extrudeGeometry(triangle, {.curveSegments = 4, .steps = 16, .extrudePath = &closed}));
    GS("extrude.path.line", extrudeGeometry(disc, {.curveSegments = 6, .steps = 2, .extrudePath = &line}));
  }
  {
    Shape copyShape = disc;
    copyShape.holes.pop_back();
    P("shape.clone", copyShape.holes.size(), disc.holes.size(), copyShape.holes[0] != disc.holes[0], copyShape.curves.size());
  }

  // --- tubes
  GS("tube", tubeGeometry(CatmullRomCurve3(pts3), 16, 0.017, 5, false));
  GS("tube.closed", tubeGeometry(CatmullRomCurve3(pts3, true), 20, 0.2, 6, true));
  GS("tube.line", tubeGeometry(LineCurve3(pts3[2], pts3[3]), 3, 0.5, 4));
  GS("tube.default", tubeGeometry(QuadraticBezierCurve3(Vector3(-1, -1, 0), Vector3(-1, 1, 0), Vector3(1, 1, 0))));

  // --- triangulation
  {
    std::vector<Vector2> square = {Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 1)};
    faces("tri.square", ShapeUtils::triangulateShape(square));
    // the end like the beginning: taken off; points in a line; a point twice
    std::vector<Vector2> closed = {Vector2(0, 0), Vector2(1, 0), Vector2(2, 0), Vector2(2, 1), Vector2(2, 2), Vector2(1, 2), Vector2(1, 2), Vector2(0, 2), Vector2(0, 1), Vector2(0, 0)};
    faces("tri.closed", ShapeUtils::triangulateShape(closed));
    P("tri.closed.length", closed.size());
    std::vector<Vector2> lShape = {Vector2(0, 0), Vector2(3, 0), Vector2(3, 1), Vector2(1, 1), Vector2(1, 3), Vector2(0, 3)};
    faces("tri.L", ShapeUtils::triangulateShape(lShape));
    std::vector<Vector2> lReversed(lShape.rbegin(), lShape.rend());
    faces("tri.L.reversed", ShapeUtils::triangulateShape(lReversed));
    std::vector<Vector2> frame = {Vector2(0, 0), Vector2(4, 0), Vector2(4, 4), Vector2(0, 4)};
    std::vector<std::vector<Vector2>> frameHoles = {{Vector2(1, 1), Vector2(1, 2), Vector2(2, 2), Vector2(2, 1)},
                                                    {Vector2(2.5, 2.5), Vector2(2.5, 3.5), Vector2(3.5, 3.5), Vector2(3.5, 2.5), Vector2(2.5, 2.5)},
                                                    {Vector2(1, 2.5), Vector2(1.5, 3.5), Vector2(2, 2.5)}};
    faces("tri.frame", ShapeUtils::triangulateShape(frame, frameHoles));
    P("tri.frame.holes", frameHoles[0].size(), frameHoles[1].size(), frameHoles[2].size(), ShapeUtils::area(frame), ShapeUtils::area(frameHoles[0]), ShapeUtils::isClockWise(frameHoles[0]));
    std::vector<Vector2> two = {Vector2(0, 0), Vector2(1, 1)};
    faces("tri.degenerate", ShapeUtils::triangulateShape(two));
    // stars: an outline that goes in and out about a middle, with two holes; the larger ones are sorted along a curve first
    for (int n : {7, 30, 30, 120, 120, 400}) {
      std::vector<Vector2> star;
      for (int i = 0; i < n; i++) {
        const double a = (i + rnd_() * 0.8) / n * PI * 2;
        const double rad = r(5, 10);
        star.push_back(Vector2(std::cos(a) * rad, std::sin(a) * rad));
      }
      std::vector<std::vector<Vector2>> holes;
      const double centres[2][2] = {{-2, 0}, {2, 0.5}};
      for (const auto& centre : centres) {
        std::vector<Vector2> hole;
        const int m = 5 + (n % 7);
        for (int i = 0; i < m; i++) {
          const double a = -(i + rnd_() * 0.8) / m * PI * 2;
          const double rad = r(0.3, 1);
          hole.push_back(Vector2(centre[0] + std::cos(a) * rad, centre[1] + std::sin(a) * rad));
        }
        holes.push_back(hole);
      }
      const std::string label = "tri.star." + std::to_string(n);
      std::vector<Vector2> alone = star;
      faces(label, ShapeUtils::triangulateShape(alone));
      std::vector<Vector2> snapped;
      for (const Vector2& p : star) snapped.push_back(Vector2(jsRound(p.x), jsRound(p.y)));
      faces(label + ".holes", ShapeUtils::triangulateShape(star, holes));
      // on a grid: many points in lines and with the same x
      faces(label + ".grid", ShapeUtils::triangulateShape(snapped));
    }
    const auto ints = [](const std::vector<int>& v) { return std::vector<double>(v.begin(), v.end()); };
    row("tri.earcut", ints(Earcut::triangulate({0, 0, 10, 0, 10, 10, 0, 10, 2, 2, 2, 4, 4, 4, 4, 2, 6, 6, 6, 8, 8, 8, 8, 6}, {4, 8})), 60);
    row("tri.earcut.steiner", ints(Earcut::triangulate({0, 0, 10, 0, 10, 10, 0, 10, 5, 5, 3, 3}, {4, 5})), 60);
    row("tri.earcut.empty", ints(Earcut::triangulate({})), 6);
  }

  // --- merging
  {
    const std::vector<GeometryPtr> parts = {boxGeometry(1, 2, 3), cylinderGeometry(0.5, 0.5, 2, 8), sphereGeometry(0.7, 8, 6)};
    parts[0]->translate(0, 1, 0);
    parts[1]->rotateZ(PI / 2);
    parts[2]->translate(2, 0, 0);
    GS("merge", mergeGeometries(parts, false));
    GS("merge.groups", mergeGeometries(parts, true));
    std::vector<GeometryPtr> bare;
    for (const GeometryPtr& g : parts) bare.push_back(g->toNonIndexed());
    GS("merge.bare", mergeGeometries(bare, false));
    GS("merge.bare.groups", mergeGeometries(bare, true));
    {
      const GeometryPtr noUv = boxGeometry(), noUv2 = boxGeometry();
      noUv->deleteAttribute("uv");
      noUv2->deleteAttribute("uv");
      P("merge.null", mergeGeometries({parts[0], bare[1]}) == nullptr, mergeGeometries({parts[0], noUv}) == nullptr, mergeGeometries({noUv2, parts[0]}) == nullptr);
    }
    // more than 65535 vertices: an index of 32 bits
    std::vector<GeometryPtr> many;
    for (int i = 0; i < 8; i++) many.push_back(planeGeometry(1, 1, 95, 95));
    const GeometryPtr big = mergeGeometries(many);
    {
      double sum = 0, squares = 0;
      for (size_t i = 0; i < big->index->length(); i++) {
        const double x = big->index->at(i);
        sum += x;
        squares += x * x;
      }
      P("merge.big", arrayName(big->index->type), big->index->count(), big->position()->count(), sum, squares);
    }
    // vertices that are alike become one
    const GeometryPtr welded = mergeVertices(boxGeometry(1, 2, 3));
    GS("mergeVertices", welded);
    const GeometryPtr positionsOnly = boxGeometry(1, 2, 3, 2, 2, 2)->toNonIndexed();
    positionsOnly->deleteAttribute("normal");
    positionsOnly->deleteAttribute("uv");
    GS("mergeVertices.positions", mergeVertices(positionsOnly));
    GS("mergeVertices.tolerance", mergeVertices(sphereGeometry(1, 12, 8), 0.3));
    const GeometryPtr smoothed = mergeVertices(positionsOnly);
    smoothed->computeVertexNormals();
    GS("mergeVertices.normals", smoothed);
  }
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
  struct Section {
    const char* name;
    void (*run)();
  };
  const Section sections[] = {{"math", mathSection}, {"scene", sceneSection}, {"geom", geomSection}};
  for (const Section& s : sections) {
    bool wanted = argc < 2;
    for (int i = 1; i < argc; i++) wanted = wanted || std::strcmp(argv[i], s.name) == 0;
    if (!wanted) continue;
    std::printf("# %s\n", s.name);
    s.run();
  }
  return 0;
}

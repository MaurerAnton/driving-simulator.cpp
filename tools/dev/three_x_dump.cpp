// SPDX-License-Identifier: AGPL-3.0-only
// LINK: engine core platform
/**
 * The engine, asked what three.js itself is asked in tools/diff/three-x.mjs: the two
 * print the same lines, tools/diff/compare.py compares them.
 *
 *   node tools/diff/three-x.mjs [math] [scene] [geom] > /tmp/a.txt      (no section named: all)
 *   tools/t.sh three_x_dump [math] [scene] [geom] > /tmp/b.txt
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
#include "engine/math.h"

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

int main(int argc, char** argv) {
  struct Section {
    const char* name;
    void (*run)();
  };
  const Section sections[] = {{"math", mathSection}};
  for (const Section& s : sections) {
    bool wanted = argc < 2;
    for (int i = 1; i < argc; i++) wanted = wanted || std::strcmp(argv[i], s.name) == 0;
    if (!wanted) continue;
    std::printf("# %s\n", s.name);
    s.run();
  }
  return 0;
}
